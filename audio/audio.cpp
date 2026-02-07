/*
 * audio.cpp - Platform-Independent Audio Utilities
 *
 * Contains format conversion, string utilities, and common functionality.
 * Also includes software audio decoders for formats not natively supported.
 */

// Prevent Windows min/max macros from conflicting with std::min/std::max
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "audio.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>
#include <atomic>
#include <memory>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
namespace pt = boost::property_tree;

// External audio decoder libraries (when enabled)
#ifdef WINDOW_SUPPORT_MP3_DECODER
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "minimp3.h"
#include "minimp3_ex.h"
#endif

#ifdef WINDOW_SUPPORT_VORBIS_DECODER
// stb_vorbis - include the implementation
#include "stb_vorbis.c"
#endif

#ifdef WINDOW_SUPPORT_FLAC_DECODER
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#endif

namespace window {
namespace audio {

// ============================================================================
// Internal Audio Decoder System (Hidden from users)
// ============================================================================

namespace internal {

// Supported audio file formats
enum class AudioFileFormat {
    Unknown,
    WAV,
    OGG,
    MP3,
    FLAC,
    AIFF
};

// Internal decoder interface
class IAudioDecoder {
public:
    virtual ~IAudioDecoder() = default;
    virtual AudioResult open(FILE* file) = 0;
    virtual void close() = 0;
    virtual const AudioFormat& get_format() const = 0;
    virtual int64_t get_total_frames() const = 0;
    virtual int read_frames(void* buffer, int frame_count) = 0;
    virtual bool seek(int64_t frame_position) = 0;
    virtual int64_t get_position() const = 0;
    virtual bool is_open() const = 0;
    virtual int64_t get_data_start_offset() const = 0;
};

// Detect format from file header
static AudioFileFormat detect_format_from_header(const uint8_t* header, size_t size) {
    if (size < 12) return AudioFileFormat::Unknown;

    // WAV: "RIFF....WAVE"
    if (header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E') {
        return AudioFileFormat::WAV;
    }

    // OGG: "OggS"
    if (header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S') {
        return AudioFileFormat::OGG;
    }

    // MP3: ID3 tag or frame sync
    if ((header[0] == 'I' && header[1] == 'D' && header[2] == '3') ||  // ID3v2
        (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)) {           // Frame sync
        return AudioFileFormat::MP3;
    }

    // FLAC: "fLaC"
    if (header[0] == 'f' && header[1] == 'L' && header[2] == 'a' && header[3] == 'C') {
        return AudioFileFormat::FLAC;
    }

    // AIFF: "FORM....AIFF"
    if (header[0] == 'F' && header[1] == 'O' && header[2] == 'R' && header[3] == 'M' &&
        header[8] == 'A' && header[9] == 'I' && header[10] == 'F' && header[11] == 'F') {
        return AudioFileFormat::AIFF;
    }

    return AudioFileFormat::Unknown;
}

// ============================================================================
// WAV Decoder
// ============================================================================

class WavDecoder : public IAudioDecoder {
public:
    ~WavDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Read RIFF header
        char riff[4];
        uint32_t file_size;
        char wave[4];

        if (fread(riff, 1, 4, file_) != 4 ||
            fread(&file_size, 4, 1, file_) != 1 ||
            fread(wave, 1, 4, file_) != 4) {
            return AudioResult::ErrorFileFormat;
        }

        if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        bool found_fmt = false;
        bool found_data = false;

        // Parse chunks
        while (!feof(file_) && (!found_fmt || !found_data)) {
            char chunk_id[4];
            uint32_t chunk_size;

            if (fread(chunk_id, 1, 4, file_) != 4) break;
            if (fread(&chunk_size, 4, 1, file_) != 1) break;

            if (memcmp(chunk_id, "fmt ", 4) == 0) {
                uint16_t audio_format, channels;
                uint32_t sample_rate, byte_rate;
                uint16_t block_align, bits_per_sample;

                fread(&audio_format, 2, 1, file_);
                fread(&channels, 2, 1, file_);
                fread(&sample_rate, 4, 1, file_);
                fread(&byte_rate, 4, 1, file_);
                fread(&block_align, 2, 1, file_);
                fread(&bits_per_sample, 2, 1, file_);

                format_.sample_rate = static_cast<int>(sample_rate);
                format_.channels = static_cast<int>(channels);
                format_.layout = layout_from_channel_count(format_.channels);

                if (audio_format == 1) { // PCM
                    switch (bits_per_sample) {
                        case 16: format_.sample_format = SampleFormat::Int16; break;
                        case 24: format_.sample_format = SampleFormat::Int24; break;
                        case 32: format_.sample_format = SampleFormat::Int32; break;
                        default: return AudioResult::ErrorFileFormat;
                    }
                } else if (audio_format == 3) { // IEEE Float
                    format_.sample_format = SampleFormat::Float32;
                } else {
                    return AudioResult::ErrorFileFormat;
                }

                if (chunk_size > 16) {
                    fseek(file_, chunk_size - 16, SEEK_CUR);
                }
                found_fmt = true;

            } else if (memcmp(chunk_id, "data", 4) == 0) {
                data_start_ = ftell(file_);
                data_size_ = chunk_size;
                total_frames_ = static_cast<int64_t>(chunk_size) / format_.bytes_per_frame();
                found_data = true;
                break;
            } else {
                fseek(file_, chunk_size, SEEK_CUR);
            }
        }

        if (!found_fmt || !found_data || !format_.is_valid()) {
            return AudioResult::ErrorFileFormat;
        }

        is_open_ = true;
        current_frame_ = 0;
        return AudioResult::Success;
    }

    void close() override {
        // Note: We don't own the file handle - caller manages it
        file_ = nullptr;
        is_open_ = false;
    }

    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return data_start_; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !file_ || !buffer || frame_count <= 0) return 0;

        int64_t frames_remaining = total_frames_ - current_frame_;
        int frames_to_read = static_cast<int>(std::min(static_cast<int64_t>(frame_count), frames_remaining));

        if (frames_to_read <= 0) return 0;

        size_t bytes_to_read = static_cast<size_t>(frames_to_read) * format_.bytes_per_frame();
        size_t bytes_read = fread(buffer, 1, bytes_to_read, file_);
        int frames_read = static_cast<int>(bytes_read / format_.bytes_per_frame());

        current_frame_ += frames_read;
        return frames_read;
    }

    bool seek(int64_t frame_position) override {
        if (!is_open_ || !file_) return false;

        frame_position = std::max(int64_t(0), std::min(frame_position, total_frames_));

        long file_offset = static_cast<long>(data_start_ + frame_position * format_.bytes_per_frame());
        if (fseek(file_, file_offset, SEEK_SET) != 0) {
            return false;
        }

        current_frame_ = frame_position;
        return true;
    }

private:
    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    int64_t data_start_ = 0;
    int64_t data_size_ = 0;
    bool is_open_ = false;
};

// ============================================================================
// MP3 Decoder
// ============================================================================

#ifdef WINDOW_SUPPORT_MP3_DECODER
// Full MP3 decoder using minimp3
class Mp3Decoder : public IAudioDecoder {
public:
    ~Mp3Decoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Get file size
        fseek(file_, 0, SEEK_END);
        file_size_ = ftell(file_);
        fseek(file_, 0, SEEK_SET);

        // Read entire file into memory for minimp3
        file_data_.resize(file_size_);
        if (fread(file_data_.data(), 1, file_size_, file_) != static_cast<size_t>(file_size_)) {
            return AudioResult::ErrorFileFormat;
        }

        // Initialize decoder and decode entire file
        mp3dec_t mp3d;
        mp3dec_init(&mp3d);

        mp3dec_file_info_t info;
        if (mp3dec_load_buf(&mp3d, file_data_.data(), file_size_, &info, nullptr, nullptr) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        // Store decoded data
        format_.sample_rate = info.hz;
        format_.channels = info.channels;
        format_.layout = (info.channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
        format_.sample_format = SampleFormat::Float32;  // minimp3 outputs float with MINIMP3_FLOAT_OUTPUT

        total_frames_ = info.samples / info.channels;
        pcm_data_.assign(info.buffer, info.buffer + info.samples);
        free(info.buffer);

        is_open_ = true;
        current_frame_ = 0;
        return AudioResult::Success;
    }

    void close() override {
        file_ = nullptr;
        is_open_ = false;
        file_data_.clear();
        pcm_data_.clear();
    }

    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !buffer || frame_count <= 0) return 0;

        int64_t frames_remaining = total_frames_ - current_frame_;
        int frames_to_read = static_cast<int>(std::min(static_cast<int64_t>(frame_count), frames_remaining));

        if (frames_to_read <= 0) return 0;

        float* out = static_cast<float*>(buffer);
        size_t sample_offset = static_cast<size_t>(current_frame_) * format_.channels;
        memcpy(out, pcm_data_.data() + sample_offset, frames_to_read * format_.channels * sizeof(float));

        current_frame_ += frames_to_read;
        return frames_to_read;
    }

    bool seek(int64_t frame_position) override {
        if (!is_open_) return false;
        current_frame_ = std::max(int64_t(0), std::min(frame_position, total_frames_));
        return true;
    }

private:
    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    long file_size_ = 0;
    bool is_open_ = false;
    std::vector<uint8_t> file_data_;
    std::vector<float> pcm_data_;  // Decoded PCM data (float with MINIMP3_FLOAT_OUTPUT)
};

#else
// Stub MP3 decoder (header parsing only, outputs silence)
class Mp3Decoder : public IAudioDecoder {
public:
    ~Mp3Decoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Read header to detect format
        uint8_t header[10];
        if (fread(header, 1, 10, file_) != 10) {
            return AudioResult::ErrorFileFormat;
        }

        // Skip ID3v2 tag if present
        if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
            uint32_t tag_size = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) |
                               ((header[8] & 0x7F) << 7) | (header[9] & 0x7F);
            fseek(file_, 10 + tag_size, SEEK_SET);
        } else {
            fseek(file_, 0, SEEK_SET);
        }

        // Find frame sync and parse basic info
        uint8_t buf[4];
        while (fread(buf, 1, 4, file_) == 4) {
            if (buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0) {
                int version = (buf[1] >> 3) & 3;
                int srate_idx = (buf[2] >> 2) & 3;
                int channels = (buf[3] >> 6) & 3;

                static const int samplerates[3][4] = {
                    {44100, 48000, 32000, 0}, {22050, 24000, 16000, 0}, {11025, 12000, 8000, 0}
                };
                int srate_ver_idx = (version == 3) ? 0 : ((version == 2) ? 1 : 2);

                format_.sample_rate = samplerates[srate_ver_idx][srate_idx];
                format_.channels = (channels == 3) ? 1 : 2;
                format_.layout = (format_.channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
                format_.sample_format = SampleFormat::Float32;

                // Estimate duration
                fseek(file_, 0, SEEK_END);
                long file_size = ftell(file_);
                total_frames_ = static_cast<int64_t>((file_size / 16000.0) * format_.sample_rate);

                is_open_ = true;
                return AudioResult::Success;
            }
            fseek(file_, -3, SEEK_CUR);
        }
        return AudioResult::ErrorFileFormat;
    }

    void close() override { file_ = nullptr; is_open_ = false; }
    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !buffer || frame_count <= 0) return 0;
        int frames = static_cast<int>(std::min(static_cast<int64_t>(frame_count), total_frames_ - current_frame_));
        std::memset(buffer, 0, frames * format_.channels * sizeof(float));
        current_frame_ += frames;
        return frames;
    }

    bool seek(int64_t frame_position) override {
        current_frame_ = std::max(int64_t(0), std::min(frame_position, total_frames_));
        return true;
    }

private:
    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    bool is_open_ = false;
};
#endif // WINDOW_SUPPORT_MP3_DECODER

// ============================================================================
// OGG Vorbis Decoder
// ============================================================================

#ifdef WINDOW_SUPPORT_VORBIS_DECODER
// Full OGG Vorbis decoder using stb_vorbis
class OggDecoder : public IAudioDecoder {
public:
    ~OggDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Get file size and read into memory
        fseek(file_, 0, SEEK_END);
        long file_size = ftell(file_);
        fseek(file_, 0, SEEK_SET);

        file_data_.resize(file_size);
        if (fread(file_data_.data(), 1, file_size, file_) != static_cast<size_t>(file_size)) {
            return AudioResult::ErrorFileFormat;
        }

        // Open with stb_vorbis
        int error = 0;
        vorbis_ = stb_vorbis_open_memory(file_data_.data(), static_cast<int>(file_size), &error, nullptr);
        if (!vorbis_ || error != 0) {
            return AudioResult::ErrorFileFormat;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
        format_.sample_rate = info.sample_rate;
        format_.channels = info.channels;
        format_.layout = (info.channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
        format_.sample_format = SampleFormat::Float32;

        total_frames_ = stb_vorbis_stream_length_in_samples(vorbis_);
        is_open_ = true;
        current_frame_ = 0;

        return AudioResult::Success;
    }

    void close() override {
        if (vorbis_) {
            stb_vorbis_close(vorbis_);
            vorbis_ = nullptr;
        }
        file_ = nullptr;
        is_open_ = false;
        file_data_.clear();
    }

    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !vorbis_ || !buffer || frame_count <= 0) return 0;

        float* out = static_cast<float*>(buffer);
        int frames_read = stb_vorbis_get_samples_float_interleaved(
            vorbis_, format_.channels, out, frame_count * format_.channels);

        current_frame_ += frames_read;
        return frames_read;
    }

    bool seek(int64_t frame_position) override {
        if (!is_open_ || !vorbis_) return false;
        if (stb_vorbis_seek(vorbis_, static_cast<unsigned int>(frame_position)) == 0) {
            return false;
        }
        current_frame_ = frame_position;
        return true;
    }

private:
    FILE* file_ = nullptr;
    stb_vorbis* vorbis_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    bool is_open_ = false;
    std::vector<uint8_t> file_data_;
};

#else
// Stub OGG decoder (header parsing only, outputs silence)
class OggDecoder : public IAudioDecoder {
public:
    ~OggDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Read OGG page header
        uint8_t header[27];
        if (fread(header, 1, 27, file_) != 27 || memcmp(header, "OggS", 4) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        int segments = header[26];
        std::vector<uint8_t> segment_table(segments);
        fread(segment_table.data(), 1, segments, file_);

        int page_size = 0;
        for (int i = 0; i < segments; ++i) page_size += segment_table[i];

        std::vector<uint8_t> packet(page_size);
        fread(packet.data(), 1, page_size, file_);

        if (page_size < 30 || packet[0] != 1 || memcmp(&packet[1], "vorbis", 6) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        format_.channels = packet[11];
        format_.sample_rate = packet[12] | (packet[13] << 8) | (packet[14] << 16) | (packet[15] << 24);
        format_.layout = (format_.channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
        format_.sample_format = SampleFormat::Float32;

        fseek(file_, 0, SEEK_END);
        long file_size = ftell(file_);
        total_frames_ = static_cast<int64_t>((file_size / 16000.0) * format_.sample_rate);

        is_open_ = true;
        return AudioResult::Success;
    }

    void close() override { file_ = nullptr; is_open_ = false; }
    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !buffer || frame_count <= 0) return 0;
        int frames = static_cast<int>(std::min(static_cast<int64_t>(frame_count), total_frames_ - current_frame_));
        std::memset(buffer, 0, frames * format_.channels * sizeof(float));
        current_frame_ += frames;
        return frames;
    }

    bool seek(int64_t frame_position) override {
        current_frame_ = std::max(int64_t(0), std::min(frame_position, total_frames_));
        return true;
    }

private:
    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    bool is_open_ = false;
};
#endif // WINDOW_SUPPORT_VORBIS_DECODER

// ============================================================================
// FLAC Decoder
// ============================================================================

#ifdef WINDOW_SUPPORT_FLAC_DECODER
// Full FLAC decoder using dr_flac
class FlacDecoder : public IAudioDecoder {
public:
    ~FlacDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Get file size and read into memory
        fseek(file_, 0, SEEK_END);
        long file_size = ftell(file_);
        fseek(file_, 0, SEEK_SET);

        file_data_.resize(file_size);
        if (fread(file_data_.data(), 1, file_size, file_) != static_cast<size_t>(file_size)) {
            return AudioResult::ErrorFileFormat;
        }

        // Open with dr_flac
        flac_ = drflac_open_memory(file_data_.data(), file_size, nullptr);
        if (!flac_) {
            return AudioResult::ErrorFileFormat;
        }

        format_.sample_rate = flac_->sampleRate;
        format_.channels = flac_->channels;
        format_.layout = (flac_->channels == 1) ? ChannelLayout::Mono :
                        (flac_->channels == 2) ? ChannelLayout::Stereo :
                        layout_from_channel_count(flac_->channels);

        // dr_flac outputs 32-bit signed integers, we'll convert to float
        format_.sample_format = SampleFormat::Float32;

        total_frames_ = flac_->totalPCMFrameCount;
        is_open_ = true;
        current_frame_ = 0;

        return AudioResult::Success;
    }

    void close() override {
        if (flac_) {
            drflac_close(flac_);
            flac_ = nullptr;
        }
        file_ = nullptr;
        is_open_ = false;
        file_data_.clear();
    }

    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !flac_ || !buffer || frame_count <= 0) return 0;

        // dr_flac reads to s32, we need to convert to float
        std::vector<drflac_int32> temp(frame_count * format_.channels);
        drflac_uint64 frames_read = drflac_read_pcm_frames_s32(flac_, frame_count, temp.data());

        // Convert s32 to float
        float* out = static_cast<float*>(buffer);
        for (size_t i = 0; i < frames_read * format_.channels; ++i) {
            out[i] = static_cast<float>(temp[i]) / 2147483648.0f;
        }

        current_frame_ += frames_read;
        return static_cast<int>(frames_read);
    }

    bool seek(int64_t frame_position) override {
        if (!is_open_ || !flac_) return false;
        if (!drflac_seek_to_pcm_frame(flac_, frame_position)) {
            return false;
        }
        current_frame_ = frame_position;
        return true;
    }

private:
    FILE* file_ = nullptr;
    drflac* flac_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    bool is_open_ = false;
    std::vector<uint8_t> file_data_;
};

#else
// Stub FLAC decoder (header parsing only, outputs silence)
class FlacDecoder : public IAudioDecoder {
public:
    ~FlacDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        char sig[4];
        if (fread(sig, 1, 4, file_) != 4 || memcmp(sig, "fLaC", 4) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        // Read metadata blocks
        bool last_block = false;
        while (!last_block) {
            uint8_t block_header[4];
            if (fread(block_header, 1, 4, file_) != 4) return AudioResult::ErrorFileFormat;

            last_block = (block_header[0] & 0x80) != 0;
            int block_type = block_header[0] & 0x7F;
            int block_size = (block_header[1] << 16) | (block_header[2] << 8) | block_header[3];

            if (block_type == 0 && block_size >= 34) {  // STREAMINFO
                uint8_t info[34];
                if (fread(info, 1, 34, file_) != 34) return AudioResult::ErrorFileFormat;

                format_.sample_rate = (info[10] << 12) | (info[11] << 4) | ((info[12] >> 4) & 0x0F);
                format_.channels = ((info[12] >> 1) & 0x07) + 1;
                format_.layout = (format_.channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;
                format_.sample_format = SampleFormat::Float32;

                total_frames_ = ((int64_t)(info[13] & 0x0F) << 32) |
                               (info[14] << 24) | (info[15] << 16) | (info[16] << 8) | info[17];

                if (block_size > 34) fseek(file_, block_size - 34, SEEK_CUR);
            } else {
                fseek(file_, block_size, SEEK_CUR);
            }
        }

        is_open_ = format_.is_valid();
        return is_open_ ? AudioResult::Success : AudioResult::ErrorFileFormat;
    }

    void close() override { file_ = nullptr; is_open_ = false; }
    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return 0; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !buffer || frame_count <= 0) return 0;
        int frames = static_cast<int>(std::min(static_cast<int64_t>(frame_count), total_frames_ - current_frame_));
        std::memset(buffer, 0, frames * format_.channels * sizeof(float));
        current_frame_ += frames;
        return frames;
    }

    bool seek(int64_t frame_position) override {
        current_frame_ = std::max(int64_t(0), std::min(frame_position, total_frames_));
        return true;
    }

private:
    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    bool is_open_ = false;
};
#endif // WINDOW_SUPPORT_FLAC_DECODER

// ============================================================================
// AIFF Decoder
// ============================================================================

class AiffDecoder : public IAudioDecoder {
public:
    ~AiffDecoder() override { close(); }

    AudioResult open(FILE* file) override {
        file_ = file;

        // Read FORM header
        char form[4], aiff[4];
        uint32_t file_size;

        if (fread(form, 1, 4, file_) != 4 ||
            fread(&file_size, 4, 1, file_) != 1 ||
            fread(aiff, 1, 4, file_) != 4) {
            return AudioResult::ErrorFileFormat;
        }

        // AIFF is big-endian
        file_size = swap_endian(file_size);

        if (memcmp(form, "FORM", 4) != 0 || memcmp(aiff, "AIFF", 4) != 0) {
            return AudioResult::ErrorFileFormat;
        }

        bool found_comm = false;
        bool found_ssnd = false;

        // Parse chunks
        while (!feof(file_) && (!found_comm || !found_ssnd)) {
            char chunk_id[4];
            uint32_t chunk_size;

            if (fread(chunk_id, 1, 4, file_) != 4) break;
            if (fread(&chunk_size, 4, 1, file_) != 1) break;
            chunk_size = swap_endian(chunk_size);

            if (memcmp(chunk_id, "COMM", 4) == 0) {
                uint16_t channels;
                uint32_t num_frames;
                uint16_t bits_per_sample;
                uint8_t sample_rate_bytes[10];  // 80-bit extended precision

                fread(&channels, 2, 1, file_);
                fread(&num_frames, 4, 1, file_);
                fread(&bits_per_sample, 2, 1, file_);
                fread(sample_rate_bytes, 1, 10, file_);

                channels = swap_endian16(channels);
                num_frames = swap_endian(num_frames);
                bits_per_sample = swap_endian16(bits_per_sample);

                format_.channels = channels;
                total_frames_ = num_frames;
                format_.sample_rate = extended_to_int(sample_rate_bytes);
                format_.layout = (channels == 1) ? ChannelLayout::Mono : ChannelLayout::Stereo;

                switch (bits_per_sample) {
                    case 8:
                    case 16: format_.sample_format = SampleFormat::Int16; break;
                    case 24: format_.sample_format = SampleFormat::Int24; break;
                    case 32: format_.sample_format = SampleFormat::Int32; break;
                    default: format_.sample_format = SampleFormat::Int16; break;
                }

                bits_per_sample_ = bits_per_sample;

                // Skip rest of chunk
                if (chunk_size > 18) {
                    fseek(file_, chunk_size - 18, SEEK_CUR);
                }
                found_comm = true;

            } else if (memcmp(chunk_id, "SSND", 4) == 0) {
                uint32_t offset, block_size;
                fread(&offset, 4, 1, file_);
                fread(&block_size, 4, 1, file_);
                offset = swap_endian(offset);

                data_start_ = ftell(file_) + offset;
                data_size_ = chunk_size - 8 - offset;
                found_ssnd = true;
                break;
            } else {
                // Skip unknown chunk (round up to even boundary)
                fseek(file_, (chunk_size + 1) & ~1, SEEK_CUR);
            }
        }

        if (!found_comm || !found_ssnd || !format_.is_valid()) {
            return AudioResult::ErrorFileFormat;
        }

        fseek(file_, static_cast<long>(data_start_), SEEK_SET);
        is_open_ = true;
        current_frame_ = 0;

        return AudioResult::Success;
    }

    void close() override {
        file_ = nullptr;
        is_open_ = false;
    }

    const AudioFormat& get_format() const override { return format_; }
    int64_t get_total_frames() const override { return total_frames_; }
    int64_t get_position() const override { return current_frame_; }
    bool is_open() const override { return is_open_; }
    int64_t get_data_start_offset() const override { return data_start_; }

    int read_frames(void* buffer, int frame_count) override {
        if (!is_open_ || !file_ || !buffer || frame_count <= 0) return 0;

        int64_t frames_remaining = total_frames_ - current_frame_;
        int frames_to_read = static_cast<int>(std::min(static_cast<int64_t>(frame_count), frames_remaining));

        if (frames_to_read <= 0) return 0;

        // AIFF data is big-endian, need to convert
        int bytes_per_sample = (bits_per_sample_ + 7) / 8;
        int bytes_per_frame = bytes_per_sample * format_.channels;
        std::vector<uint8_t> temp(frames_to_read * bytes_per_frame);

        size_t bytes_read = fread(temp.data(), 1, temp.size(), file_);
        int frames_read = static_cast<int>(bytes_read / bytes_per_frame);

        // Convert big-endian to native
        uint8_t* dst = static_cast<uint8_t*>(buffer);
        for (int i = 0; i < frames_read * format_.channels; ++i) {
            const uint8_t* src = temp.data() + i * bytes_per_sample;
            if (bytes_per_sample == 2) {
                dst[0] = src[1];
                dst[1] = src[0];
            } else if (bytes_per_sample == 3) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
            } else if (bytes_per_sample == 4) {
                dst[0] = src[3];
                dst[1] = src[2];
                dst[2] = src[1];
                dst[3] = src[0];
            }
            dst += bytes_per_sample;
        }

        current_frame_ += frames_read;
        return frames_read;
    }

    bool seek(int64_t frame_position) override {
        if (!is_open_ || !file_) return false;

        frame_position = std::max(int64_t(0), std::min(frame_position, total_frames_));

        int bytes_per_sample = (bits_per_sample_ + 7) / 8;
        long file_offset = static_cast<long>(data_start_ + frame_position * bytes_per_sample * format_.channels);
        if (fseek(file_, file_offset, SEEK_SET) != 0) {
            return false;
        }

        current_frame_ = frame_position;
        return true;
    }

private:
    static uint32_t swap_endian(uint32_t val) {
        return ((val & 0xFF000000) >> 24) |
               ((val & 0x00FF0000) >> 8) |
               ((val & 0x0000FF00) << 8) |
               ((val & 0x000000FF) << 24);
    }

    static uint16_t swap_endian16(uint16_t val) {
        return (val >> 8) | (val << 8);
    }

    static int extended_to_int(const uint8_t* bytes) {
        // Convert 80-bit extended precision to integer
        // Simplified - just gets the integer part
        int sign = (bytes[0] & 0x80) ? -1 : 1;
        int exponent = ((bytes[0] & 0x7F) << 8) | bytes[1];
        exponent -= 16383 + 31;

        uint32_t mantissa = (bytes[2] << 24) | (bytes[3] << 16) | (bytes[4] << 8) | bytes[5];

        if (exponent >= 0) {
            return sign * static_cast<int>(mantissa >> (31 - exponent));
        } else {
            return sign * static_cast<int>(mantissa >> (31 - exponent));
        }
    }

    FILE* file_ = nullptr;
    AudioFormat format_;
    int64_t total_frames_ = 0;
    int64_t current_frame_ = 0;
    int64_t data_start_ = 0;
    int64_t data_size_ = 0;
    int bits_per_sample_ = 16;
    bool is_open_ = false;
};

// ============================================================================
// Decoder Factory
// ============================================================================

static std::unique_ptr<IAudioDecoder> create_decoder(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::WAV:  return std::make_unique<WavDecoder>();
        case AudioFileFormat::MP3:  return std::make_unique<Mp3Decoder>();
        case AudioFileFormat::OGG:  return std::make_unique<OggDecoder>();
        case AudioFileFormat::FLAC: return std::make_unique<FlacDecoder>();
        case AudioFileFormat::AIFF: return std::make_unique<AiffDecoder>();
        default: return nullptr;
    }
}

static AudioFileFormat detect_file_format(const char* filepath) {
    FILE* file = fopen(filepath, "rb");
    if (!file) return AudioFileFormat::Unknown;

    uint8_t header[12];
    size_t read = fread(header, 1, 12, file);
    fclose(file);

    if (read < 12) return AudioFileFormat::Unknown;
    return detect_format_from_header(header, read);
}

// ============================================================================
// Shared Audio Loading Helper (for platform backends)
// ============================================================================

// Load complete audio file into memory using software decoder
// Returns audio data in the native format of the file
bool load_audio_file(const char* filepath, AudioFormat* out_format,
                     std::vector<uint8_t>* out_data, AudioResult* out_result) {
    if (!filepath || !out_format || !out_data) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return false;
    }

    // Detect format
    AudioFileFormat file_format = detect_file_format(filepath);
    if (file_format == AudioFileFormat::Unknown) {
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return false;
    }

    // Open file
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        if (out_result) *out_result = AudioResult::ErrorFileNotFound;
        return false;
    }

    // Create decoder
    auto decoder = create_decoder(file_format);
    if (!decoder) {
        fclose(file);
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return false;
    }

    // Open decoder
    AudioResult result = decoder->open(file);
    if (result != AudioResult::Success) {
        fclose(file);
        if (out_result) *out_result = result;
        return false;
    }

    // Get format info
    *out_format = decoder->get_format();
    int64_t total_frames = decoder->get_total_frames();

    if (total_frames <= 0 || !out_format->is_valid()) {
        decoder->close();
        fclose(file);
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return false;
    }

    // Allocate buffer
    size_t data_size = static_cast<size_t>(total_frames) * out_format->bytes_per_frame();
    out_data->resize(data_size);

    // Read all data
    int frames_read = decoder->read_frames(out_data->data(), static_cast<int>(total_frames));

    decoder->close();
    fclose(file);

    if (frames_read <= 0) {
        out_data->clear();
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return false;
    }

    // Resize to actual data read
    out_data->resize(static_cast<size_t>(frames_read) * out_format->bytes_per_frame());

    if (out_result) *out_result = AudioResult::Success;
    return true;
}

} // namespace internal

// ============================================================================
// AudioFileStream Implementation (Uses Internal Decoder System)
// ============================================================================

struct AudioFileStream::Impl {
    FILE* file = nullptr;
    std::unique_ptr<internal::IAudioDecoder> decoder;
    AudioFormat format;
    int64_t total_frames = 0;
    int64_t current_frame = 0;
    bool is_open_flag = false;
    std::vector<uint8_t> read_buffer;  // Temp buffer for format conversion
};

AudioFileStream* AudioFileStream::open(const char* filepath, AudioResult* out_result) {
    if (!filepath) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return nullptr;
    }

    // Detect file format
    internal::AudioFileFormat file_format = internal::detect_file_format(filepath);
    if (file_format == internal::AudioFileFormat::Unknown) {
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return nullptr;
    }

    // Open file
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        if (out_result) *out_result = AudioResult::ErrorFileNotFound;
        return nullptr;
    }

    // Create decoder for format
    auto decoder = internal::create_decoder(file_format);
    if (!decoder) {
        fclose(file);
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return nullptr;
    }

    // Open decoder
    AudioResult result = decoder->open(file);
    if (result != AudioResult::Success) {
        fclose(file);
        if (out_result) *out_result = result;
        return nullptr;
    }

    AudioFileStream* stream = new AudioFileStream();
    stream->impl_ = new AudioFileStream::Impl();
    stream->impl_->file = file;
    stream->impl_->decoder = std::move(decoder);
    stream->impl_->format = stream->impl_->decoder->get_format();
    stream->impl_->total_frames = stream->impl_->decoder->get_total_frames();
    stream->impl_->current_frame = 0;
    stream->impl_->is_open_flag = true;

    if (out_result) *out_result = AudioResult::Success;
    return stream;
}

void AudioFileStream::close() {
    if (!impl_) return;

    if (impl_->decoder) {
        impl_->decoder->close();
        impl_->decoder.reset();
    }
    if (impl_->file) {
        fclose(impl_->file);
        impl_->file = nullptr;
    }
    impl_->is_open_flag = false;

    delete impl_;
    impl_ = nullptr;
    delete this;
}

const AudioFormat& AudioFileStream::get_format() const {
    static AudioFormat empty;
    return impl_ ? impl_->format : empty;
}

int AudioFileStream::read_frames(void* buffer, int frame_count) {
    if (!impl_ || !impl_->decoder || !buffer || frame_count <= 0) return 0;

    int frames_read = impl_->decoder->read_frames(buffer, frame_count);
    impl_->current_frame = impl_->decoder->get_position();
    return frames_read;
}

int AudioFileStream::read_frames_converted(void* buffer, int frame_count, SampleFormat target_format) {
    if (!impl_ || !buffer || frame_count <= 0) return 0;

    if (impl_->format.sample_format == target_format) {
        return read_frames(buffer, frame_count);
    }

    // Read into temp buffer then convert
    size_t src_frame_size = impl_->format.bytes_per_frame();
    impl_->read_buffer.resize(frame_count * src_frame_size);

    int frames_read = read_frames(impl_->read_buffer.data(), frame_count);
    if (frames_read <= 0) return 0;

    // Convert samples
    int total_samples = frames_read * impl_->format.channels;
    convert_samples(impl_->read_buffer.data(), impl_->format.sample_format,
                    buffer, target_format, total_samples);

    return frames_read;
}

bool AudioFileStream::seek(int64_t frame_position, AudioSeekOrigin origin) {
    if (!impl_ || !impl_->decoder) return false;

    int64_t target_frame = 0;
    switch (origin) {
        case AudioSeekOrigin::Begin:
            target_frame = frame_position;
            break;
        case AudioSeekOrigin::Current:
            target_frame = impl_->current_frame + frame_position;
            break;
        case AudioSeekOrigin::End:
            target_frame = impl_->total_frames + frame_position;
            break;
    }

    // Clamp to valid range
    target_frame = std::max(int64_t(0), std::min(target_frame, impl_->total_frames));

    bool result = impl_->decoder->seek(target_frame);
    if (result) {
        impl_->current_frame = impl_->decoder->get_position();
    }
    return result;
}

int64_t AudioFileStream::get_position() const {
    return impl_ && impl_->decoder ? impl_->decoder->get_position() : 0;
}

int64_t AudioFileStream::get_total_frames() const {
    return impl_ && impl_->decoder ? impl_->decoder->get_total_frames() : 0;
}

double AudioFileStream::get_duration() const {
    if (!impl_ || impl_->format.sample_rate <= 0) return 0.0;
    return static_cast<double>(impl_->total_frames) / impl_->format.sample_rate;
}

bool AudioFileStream::is_end_of_stream() const {
    if (!impl_ || !impl_->decoder) return true;
    return impl_->decoder->get_position() >= impl_->decoder->get_total_frames();
}

bool AudioFileStream::is_open() const {
    return impl_ && impl_->decoder && impl_->decoder->is_open();
}

void AudioFileStream::rewind() {
    seek(0, AudioSeekOrigin::Begin);
}

// ============================================================================
// StreamingAudioCallback Implementation
// ============================================================================

struct StreamingAudioCallback::Impl {
    AudioFileStream* source = nullptr;
    bool owns_source = false;
    std::atomic<bool> looping{false};
    std::atomic<float> volume{1.0f};
    std::atomic<bool> finished{false};
    std::vector<uint8_t> convert_buffer;
};

StreamingAudioCallback::StreamingAudioCallback() {
    impl_ = new Impl();
}

StreamingAudioCallback::~StreamingAudioCallback() {
    if (impl_) {
        if (impl_->owns_source && impl_->source) {
            impl_->source->close();
        }
        delete impl_;
    }
}

void StreamingAudioCallback::set_source(AudioFileStream* stream, bool owns_stream) {
    if (impl_->owns_source && impl_->source) {
        impl_->source->close();
    }
    impl_->source = stream;
    impl_->owns_source = owns_stream;
    impl_->finished = false;
}

AudioFileStream* StreamingAudioCallback::get_source() const {
    return impl_ ? impl_->source : nullptr;
}

void StreamingAudioCallback::set_looping(bool loop) {
    if (impl_) impl_->looping = loop;
}

bool StreamingAudioCallback::is_looping() const {
    return impl_ ? impl_->looping.load() : false;
}

void StreamingAudioCallback::set_volume(float volume) {
    if (impl_) impl_->volume = std::max(0.0f, std::min(1.0f, volume));
}

float StreamingAudioCallback::get_volume() const {
    return impl_ ? impl_->volume.load() : 0.0f;
}

bool StreamingAudioCallback::is_finished() const {
    return impl_ ? impl_->finished.load() : true;
}

void StreamingAudioCallback::reset() {
    if (impl_ && impl_->source) {
        impl_->source->rewind();
        impl_->finished = false;
    }
}

bool StreamingAudioCallback::on_audio_playback(AudioBuffer& output, const AudioStreamTime& time) {
    (void)time;

    if (!impl_ || !impl_->source || !impl_->source->is_open()) {
        // Fill with silence
        if (output.data) {
            std::memset(output.data, 0, output.size_bytes());
        }
        return false;
    }

    const AudioFormat& src_format = impl_->source->get_format();
    float* out_buffer = static_cast<float*>(output.data);
    int frames_needed = output.frame_count;
    int frames_filled = 0;

    // Ensure we have a conversion buffer if needed
    bool needs_conversion = (src_format.sample_format != SampleFormat::Float32 ||
                             src_format.channels != output.channel_count);

    if (needs_conversion) {
        size_t needed_size = frames_needed * src_format.bytes_per_frame();
        if (impl_->convert_buffer.size() < needed_size) {
            impl_->convert_buffer.resize(needed_size);
        }
    }

    while (frames_filled < frames_needed) {
        int frames_to_read = frames_needed - frames_filled;
        int frames_read = 0;

        if (needs_conversion) {
            // Read into temp buffer
            frames_read = impl_->source->read_frames(impl_->convert_buffer.data(), frames_to_read);

            if (frames_read > 0) {
                // Convert to float32
                int src_samples = frames_read * src_format.channels;
                std::vector<float> temp_float(src_samples);

                convert_samples(impl_->convert_buffer.data(), src_format.sample_format,
                                temp_float.data(), SampleFormat::Float32, src_samples);

                // Mix/copy to output (handle channel conversion)
                for (int f = 0; f < frames_read; ++f) {
                    for (int c = 0; c < output.channel_count; ++c) {
                        int src_channel = (c < src_format.channels) ? c : (src_format.channels - 1);
                        out_buffer[(frames_filled + f) * output.channel_count + c] =
                            temp_float[f * src_format.channels + src_channel];
                    }
                }
            }
        } else {
            // Direct read - format matches
            frames_read = impl_->source->read_frames(
                out_buffer + frames_filled * output.channel_count,
                frames_to_read);
        }

        frames_filled += frames_read;

        // Check if we hit end of stream
        if (impl_->source->is_end_of_stream()) {
            if (impl_->looping) {
                impl_->source->rewind();
            } else {
                // Fill remaining with silence
                if (frames_filled < frames_needed) {
                    size_t remaining_samples = (frames_needed - frames_filled) * output.channel_count;
                    std::memset(out_buffer + frames_filled * output.channel_count, 0,
                                remaining_samples * sizeof(float));
                }
                impl_->finished = true;
                break;
            }
        }

        if (frames_read == 0 && !impl_->looping) {
            break;
        }
    }

    // Apply volume
    float vol = impl_->volume.load();
    if (vol < 0.999f) {
        int total_samples = frames_needed * output.channel_count;
        for (int i = 0; i < total_samples; ++i) {
            out_buffer[i] *= vol;
        }
    }

    return !impl_->finished;
}

void StreamingAudioCallback::on_audio_error(AudioResult error) {
    (void)error;
    if (impl_) {
        impl_->finished = true;
    }
}

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* audio_result_to_string(AudioResult result) {
    switch (result) {
        case AudioResult::Success:                return "Success";
        case AudioResult::ErrorUnknown:           return "Unknown error";
        case AudioResult::ErrorNotInitialized:    return "Audio not initialized";
        case AudioResult::ErrorAlreadyInitialized: return "Audio already initialized";
        case AudioResult::ErrorDeviceNotFound:    return "Device not found";
        case AudioResult::ErrorFormatNotSupported: return "Format not supported";
        case AudioResult::ErrorDeviceLost:        return "Device lost";
        case AudioResult::ErrorDeviceBusy:        return "Device busy";
        case AudioResult::ErrorInvalidParameter:  return "Invalid parameter";
        case AudioResult::ErrorOutOfMemory:       return "Out of memory";
        case AudioResult::ErrorBackendNotSupported: return "Backend not supported";
        case AudioResult::ErrorStreamNotRunning:  return "Stream not running";
        case AudioResult::ErrorStreamAlreadyRunning: return "Stream already running";
        case AudioResult::ErrorTimeout:           return "Timeout";
        case AudioResult::ErrorFileNotFound:      return "File not found";
        case AudioResult::ErrorFileFormat:        return "Invalid file format";
        case AudioResult::ErrorEndOfFile:         return "End of file";
        default: return "Unknown";
    }
}

const char* audio_backend_to_string(AudioBackend backend) {
    switch (backend) {
        case AudioBackend::Auto:      return "Auto";
        case AudioBackend::WASAPI:    return "WASAPI";
        case AudioBackend::CoreAudio: return "CoreAudio";
        case AudioBackend::PulseAudio: return "PulseAudio";
        case AudioBackend::ALSA:      return "ALSA";
        case AudioBackend::AAudio:    return "AAudio";
        case AudioBackend::OpenSLES:  return "OpenSL ES";
        case AudioBackend::WebAudio:  return "Web Audio";
        case AudioBackend::OpenAL:    return "OpenAL";
        default: return "Unknown";
    }
}

const char* sample_format_to_string(SampleFormat format) {
    switch (format) {
        case SampleFormat::Unknown:  return "Unknown";
        case SampleFormat::Int16:    return "Int16";
        case SampleFormat::Int24:    return "Int24";
        case SampleFormat::Int32:    return "Int32";
        case SampleFormat::Float32:  return "Float32";
        default: return "Unknown";
    }
}

const char* channel_layout_to_string(ChannelLayout layout) {
    switch (layout) {
        case ChannelLayout::Unknown:     return "Unknown";
        case ChannelLayout::Mono:        return "Mono";
        case ChannelLayout::Stereo:      return "Stereo";
        case ChannelLayout::Surround21:  return "2.1 Surround";
        case ChannelLayout::Surround40:  return "4.0 Surround";
        case ChannelLayout::Surround41:  return "4.1 Surround";
        case ChannelLayout::Surround51:  return "5.1 Surround";
        case ChannelLayout::Surround71:  return "7.1 Surround";
        default: return "Unknown";
    }
}

const char* audio_session_event_to_string(AudioSessionEvent event) {
    switch (event) {
        case AudioSessionEvent::InterruptionBegan:               return "Interruption began";
        case AudioSessionEvent::InterruptionEnded:               return "Interruption ended";
        case AudioSessionEvent::RouteChangeNewDeviceAvailable:   return "New device available";
        case AudioSessionEvent::RouteChangeOldDeviceUnavailable: return "Device unavailable";
        case AudioSessionEvent::RouteChangeCategoryChange:       return "Category change";
        case AudioSessionEvent::RouteChangeOverride:             return "Route override";
        case AudioSessionEvent::RouteChangeWakeFromSleep:        return "Wake from sleep";
        case AudioSessionEvent::RouteChangeNoSuitableRouteForCategory: return "No suitable route";
        case AudioSessionEvent::RouteChangeRouteConfigurationChange:   return "Route config change";
        case AudioSessionEvent::DefaultOutputDeviceChanged:      return "Default output changed";
        case AudioSessionEvent::DefaultInputDeviceChanged:       return "Default input changed";
        case AudioSessionEvent::DeviceListChanged:               return "Device list changed";
        case AudioSessionEvent::MediaServicesWereLost:           return "Media services lost";
        case AudioSessionEvent::MediaServicesWereReset:          return "Media services reset";
        case AudioSessionEvent::SilenceSecondaryAudioHintBegan:  return "Silence secondary audio";
        case AudioSessionEvent::SilenceSecondaryAudioHintEnded:  return "Resume secondary audio";
        default: return "Unknown";
    }
}

// ============================================================================
// AudioBuffer Implementation
// ============================================================================

void AudioBuffer::clear() {
    if (data && frame_count > 0 && channel_count > 0 && format != SampleFormat::Unknown) {
        std::memset(data, 0, size_bytes());
    }
}

// ============================================================================
// Sample Conversion Utilities
// ============================================================================

// Convert int16 to float32
static inline float int16_to_float(int16_t sample) {
    return static_cast<float>(sample) / 32768.0f;
}

// Convert int24 (stored in int32) to float32
static inline float int24_to_float(int32_t sample) {
    return static_cast<float>(sample) / 8388608.0f;
}

// Convert int32 to float32
static inline float int32_to_float(int32_t sample) {
    return static_cast<float>(sample) / 2147483648.0f;
}

// Convert float32 to int16
static inline int16_t float_to_int16(float sample) {
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int16_t>(sample * 32767.0f);
}

// Convert float32 to int24 (stored in int32)
static inline int32_t float_to_int24(float sample) {
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int32_t>(sample * 8388607.0f);
}

// Convert float32 to int32
static inline int32_t float_to_int32(float sample) {
    sample = std::max(-1.0f, std::min(1.0f, sample));
    return static_cast<int32_t>(sample * 2147483647.0f);
}

void convert_samples(const void* src, SampleFormat src_format,
                     void* dst, SampleFormat dst_format,
                     int sample_count) {
    if (!src || !dst || sample_count <= 0) return;
    if (src_format == dst_format) {
        // Same format, just copy
        int bytes = 0;
        switch (src_format) {
            case SampleFormat::Int16:   bytes = 2; break;
            case SampleFormat::Int24:   bytes = 3; break;
            case SampleFormat::Int32:   bytes = 4; break;
            case SampleFormat::Float32: bytes = 4; break;
            default: return;
        }
        std::memcpy(dst, src, static_cast<size_t>(sample_count) * bytes);
        return;
    }

    // Convert to float first, then to target format
    // This is a two-step process for simplicity

    // First: read source samples
    const uint8_t* src_ptr = static_cast<const uint8_t*>(src);
    uint8_t* dst_ptr = static_cast<uint8_t*>(dst);

    for (int i = 0; i < sample_count; ++i) {
        float sample = 0.0f;

        // Read source sample as float
        switch (src_format) {
            case SampleFormat::Int16: {
                int16_t s;
                std::memcpy(&s, src_ptr, sizeof(s));
                sample = int16_to_float(s);
                src_ptr += 2;
                break;
            }
            case SampleFormat::Int24: {
                // Read 3 bytes and sign extend
                int32_t s = 0;
                s = src_ptr[0] | (src_ptr[1] << 8) | (src_ptr[2] << 16);
                if (s & 0x800000) s |= 0xFF000000; // Sign extend
                sample = int24_to_float(s);
                src_ptr += 3;
                break;
            }
            case SampleFormat::Int32: {
                int32_t s;
                std::memcpy(&s, src_ptr, sizeof(s));
                sample = int32_to_float(s);
                src_ptr += 4;
                break;
            }
            case SampleFormat::Float32: {
                std::memcpy(&sample, src_ptr, sizeof(sample));
                src_ptr += 4;
                break;
            }
            default:
                return;
        }

        // Write to destination format
        switch (dst_format) {
            case SampleFormat::Int16: {
                int16_t s = float_to_int16(sample);
                std::memcpy(dst_ptr, &s, sizeof(s));
                dst_ptr += 2;
                break;
            }
            case SampleFormat::Int24: {
                int32_t s = float_to_int24(sample);
                dst_ptr[0] = static_cast<uint8_t>(s & 0xFF);
                dst_ptr[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                dst_ptr[2] = static_cast<uint8_t>((s >> 16) & 0xFF);
                dst_ptr += 3;
                break;
            }
            case SampleFormat::Int32: {
                int32_t s = float_to_int32(sample);
                std::memcpy(dst_ptr, &s, sizeof(s));
                dst_ptr += 4;
                break;
            }
            case SampleFormat::Float32: {
                std::memcpy(dst_ptr, &sample, sizeof(sample));
                dst_ptr += 4;
                break;
            }
            default:
                return;
        }
    }
}

// ============================================================================
// Mixing Utilities
// ============================================================================

void mix_samples(const void* src, void* dst, SampleFormat format,
                 int sample_count, float volume) {
    if (!src || !dst || sample_count <= 0) return;

    switch (format) {
        case SampleFormat::Int16: {
            const int16_t* s = static_cast<const int16_t*>(src);
            int16_t* d = static_cast<int16_t*>(dst);
            for (int i = 0; i < sample_count; ++i) {
                float mixed = int16_to_float(d[i]) + int16_to_float(s[i]) * volume;
                d[i] = float_to_int16(mixed);
            }
            break;
        }
        case SampleFormat::Int32: {
            const int32_t* s = static_cast<const int32_t*>(src);
            int32_t* d = static_cast<int32_t*>(dst);
            for (int i = 0; i < sample_count; ++i) {
                float mixed = int32_to_float(d[i]) + int32_to_float(s[i]) * volume;
                d[i] = float_to_int32(mixed);
            }
            break;
        }
        case SampleFormat::Float32: {
            const float* s = static_cast<const float*>(src);
            float* d = static_cast<float*>(dst);
            for (int i = 0; i < sample_count; ++i) {
                d[i] = std::max(-1.0f, std::min(1.0f, d[i] + s[i] * volume));
            }
            break;
        }
        default:
            break;
    }
}

void apply_volume(void* data, SampleFormat format, int sample_count, float volume) {
    if (!data || sample_count <= 0) return;

    if (std::abs(volume - 1.0f) < 0.0001f) return; // No change needed

    switch (format) {
        case SampleFormat::Int16: {
            int16_t* d = static_cast<int16_t*>(data);
            for (int i = 0; i < sample_count; ++i) {
                float sample = int16_to_float(d[i]) * volume;
                d[i] = float_to_int16(sample);
            }
            break;
        }
        case SampleFormat::Int32: {
            int32_t* d = static_cast<int32_t*>(data);
            for (int i = 0; i < sample_count; ++i) {
                float sample = int32_to_float(d[i]) * volume;
                d[i] = float_to_int32(sample);
            }
            break;
        }
        case SampleFormat::Float32: {
            float* d = static_cast<float*>(data);
            for (int i = 0; i < sample_count; ++i) {
                d[i] *= volume;
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// Channel Interleaving Utilities
// ============================================================================

void interleave_channels(const float* const* src, float* dst, int channels, int frames) {
    if (!src || !dst || channels <= 0 || frames <= 0) return;

    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            dst[f * channels + c] = src[c][f];
        }
    }
}

void deinterleave_channels(const float* src, float** dst, int channels, int frames) {
    if (!src || !dst || channels <= 0 || frames <= 0) return;

    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            dst[c][f] = src[f * channels + c];
        }
    }
}

// ============================================================================
// AudioResamplerCPU Implementation
// ============================================================================

// Constants for sinc resampling
static constexpr int SINC_TAPS_HIGH = 8;    // 8-tap for High quality
static constexpr int SINC_TAPS_BEST = 16;   // 16-tap for Best quality
static constexpr int SINC_TABLE_SIZE = 512; // Interpolation table resolution
static constexpr double PI = 3.14159265358979323846;

// Sinc function
static inline double sinc(double x) {
    if (std::abs(x) < 1e-8) return 1.0;
    return std::sin(PI * x) / (PI * x);
}

// Kaiser window function (beta controls the shape)
static inline double kaiser_window(double n, double N, double beta) {
    // Approximation of I0 (modified Bessel function of order 0)
    auto bessel_i0 = [](double x) -> double {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 25; ++k) {
            term *= (x * x) / (4.0 * k * k);
            sum += term;
            if (term < 1e-12) break;
        }
        return sum;
    };

    double half_N = (N - 1.0) / 2.0;
    double alpha = (n - half_N) / half_N;
    double arg = beta * std::sqrt(1.0 - alpha * alpha);
    return bessel_i0(arg) / bessel_i0(beta);
}

struct AudioResamplerCPU::Impl {
    ResamplerConfig config;
    int input_rate = 48000;
    int output_rate = 48000;
    int channels = 2;
    ResamplerQuality quality = ResamplerQuality::Medium;

    // Resampling state
    double phase = 0.0;         // Fractional sample position
    double phase_increment = 1.0;

    // History buffer for interpolation (per channel)
    std::vector<std::vector<float>> history;
    int history_size = 0;
    int history_pos = 0;

    // Precomputed sinc filter tables (for High/Best quality)
    std::vector<std::vector<float>> sinc_table;
    int sinc_taps = 0;

    // Conversion buffer for process_convert
    std::vector<float> convert_buffer;

    void init() {
        phase = 0.0;
        phase_increment = static_cast<double>(input_rate) / static_cast<double>(output_rate);

        // Determine history size based on quality
        switch (quality) {
            case ResamplerQuality::Fastest:
            case ResamplerQuality::Low:
                history_size = 2;   // Linear interpolation
                break;
            case ResamplerQuality::Medium:
                history_size = 4;   // Cubic interpolation
                break;
            case ResamplerQuality::High:
                sinc_taps = SINC_TAPS_HIGH;
                history_size = sinc_taps;
                build_sinc_table();
                break;
            case ResamplerQuality::Best:
                sinc_taps = SINC_TAPS_BEST;
                history_size = sinc_taps;
                build_sinc_table();
                break;
        }

        // Allocate history buffers
        history.resize(channels);
        for (int c = 0; c < channels; ++c) {
            history[c].resize(history_size, 0.0f);
        }
        history_pos = 0;
    }

    void build_sinc_table() {
        // Build a table of sinc values for various fractional positions
        sinc_table.resize(SINC_TABLE_SIZE);

        double cutoff = std::min(1.0, static_cast<double>(output_rate) / input_rate);
        double beta = 6.0;  // Kaiser window parameter

        for (int t = 0; t < SINC_TABLE_SIZE; ++t) {
            sinc_table[t].resize(sinc_taps);

            double frac = static_cast<double>(t) / SINC_TABLE_SIZE;

            // Generate windowed sinc coefficients
            double sum = 0.0;
            for (int i = 0; i < sinc_taps; ++i) {
                double n = static_cast<double>(i) - (sinc_taps - 1) / 2.0 - frac;
                double w = kaiser_window(static_cast<double>(i), sinc_taps, beta);
                double s = sinc(n * cutoff) * cutoff * w;
                sinc_table[t][i] = static_cast<float>(s);
                sum += s;
            }

            // Normalize
            if (sum > 1e-8) {
                for (int i = 0; i < sinc_taps; ++i) {
                    sinc_table[t][i] /= static_cast<float>(sum);
                }
            }
        }
    }

    void push_sample(int channel, float sample) {
        history[channel][history_pos] = sample;
    }

    void advance_history() {
        history_pos = (history_pos + 1) % history_size;
    }

    float get_history(int channel, int offset) const {
        int idx = (history_pos - offset + history_size) % history_size;
        return history[channel][idx];
    }

    // Linear interpolation
    float interpolate_linear(int channel, double frac) const {
        float s0 = get_history(channel, 1);
        float s1 = get_history(channel, 0);
        return s0 + static_cast<float>(frac) * (s1 - s0);
    }

    // Cubic interpolation (Catmull-Rom)
    float interpolate_cubic(int channel, double frac) const {
        float s0 = get_history(channel, 3);
        float s1 = get_history(channel, 2);
        float s2 = get_history(channel, 1);
        float s3 = get_history(channel, 0);

        float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
        float a1 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
        float a2 = -0.5f * s0 + 0.5f * s2;
        float a3 = s1;

        float t = static_cast<float>(frac);
        return a0 * t * t * t + a1 * t * t + a2 * t + a3;
    }

    // Sinc interpolation
    float interpolate_sinc(int channel, double frac) const {
        // Get table index
        int table_idx = static_cast<int>(frac * SINC_TABLE_SIZE);
        if (table_idx >= SINC_TABLE_SIZE) table_idx = SINC_TABLE_SIZE - 1;

        const auto& coeffs = sinc_table[table_idx];
        float sum = 0.0f;

        for (int i = 0; i < sinc_taps; ++i) {
            int offset = sinc_taps - 1 - i;
            sum += get_history(channel, offset) * coeffs[i];
        }

        return sum;
    }

    float interpolate(int channel, double frac) const {
        switch (quality) {
            case ResamplerQuality::Fastest:
            case ResamplerQuality::Low:
                return interpolate_linear(channel, frac);
            case ResamplerQuality::Medium:
                return interpolate_cubic(channel, frac);
            case ResamplerQuality::High:
            case ResamplerQuality::Best:
                return interpolate_sinc(channel, frac);
            default:
                return interpolate_linear(channel, frac);
        }
    }
};

AudioResamplerCPU* AudioResamplerCPU::create(const ResamplerConfig& config, AudioResult* out_result) {
    if (config.input_rate <= 0 || config.output_rate <= 0 || config.channels <= 0) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return nullptr;
    }

    AudioResamplerCPU* resampler = new AudioResamplerCPU();
    resampler->impl_ = new Impl();
    resampler->impl_->config = config;
    resampler->impl_->input_rate = config.input_rate;
    resampler->impl_->output_rate = config.output_rate;
    resampler->impl_->channels = config.channels;
    resampler->impl_->quality = config.quality;
    resampler->impl_->init();

    if (out_result) *out_result = AudioResult::Success;
    return resampler;
}

void AudioResamplerCPU::destroy() {
    if (impl_) {
        delete impl_;
        impl_ = nullptr;
    }
    delete this;
}

int AudioResamplerCPU::process(const float* input, float* output, int input_frames) {
    if (!impl_ || !input || !output || input_frames <= 0) return 0;

    int channels = impl_->channels;
    int output_frames = 0;
    int input_pos = 0;

    while (input_pos < input_frames) {
        // Push input samples to history
        for (int c = 0; c < channels; ++c) {
            impl_->push_sample(c, input[input_pos * channels + c]);
        }
        impl_->advance_history();
        input_pos++;

        // Generate output samples while phase < 1
        while (impl_->phase < 1.0 && output_frames < get_output_frames_max(input_frames)) {
            double frac = impl_->phase;

            for (int c = 0; c < channels; ++c) {
                output[output_frames * channels + c] = impl_->interpolate(c, frac);
            }
            output_frames++;

            impl_->phase += impl_->phase_increment;
        }

        if (impl_->phase >= 1.0) {
            impl_->phase -= 1.0;
        }
    }

    return output_frames;
}

int AudioResamplerCPU::process_convert(const void* input, SampleFormat input_format,
                                        float* output, int input_frames) {
    if (!impl_ || !input || !output || input_frames <= 0) return 0;

    // Convert input to float first
    int total_samples = input_frames * impl_->channels;
    impl_->convert_buffer.resize(total_samples);

    convert_samples(input, input_format, impl_->convert_buffer.data(),
                    SampleFormat::Float32, total_samples);

    return process(impl_->convert_buffer.data(), output, input_frames);
}

void AudioResamplerCPU::reset() {
    if (!impl_) return;

    impl_->phase = 0.0;
    impl_->history_pos = 0;

    for (auto& ch_history : impl_->history) {
        std::fill(ch_history.begin(), ch_history.end(), 0.0f);
    }
}

int AudioResamplerCPU::get_input_rate() const {
    return impl_ ? impl_->input_rate : 0;
}

int AudioResamplerCPU::get_output_rate() const {
    return impl_ ? impl_->output_rate : 0;
}

int AudioResamplerCPU::get_channels() const {
    return impl_ ? impl_->channels : 0;
}

ResamplerQuality AudioResamplerCPU::get_quality() const {
    return impl_ ? impl_->quality : ResamplerQuality::Medium;
}

void AudioResamplerCPU::set_rates(int input_rate, int output_rate) {
    if (!impl_ || input_rate <= 0 || output_rate <= 0) return;

    impl_->input_rate = input_rate;
    impl_->output_rate = output_rate;
    impl_->phase_increment = static_cast<double>(input_rate) / static_cast<double>(output_rate);

    // Rebuild sinc table if using sinc interpolation (cutoff depends on rates)
    if (impl_->quality == ResamplerQuality::High || impl_->quality == ResamplerQuality::Best) {
        impl_->build_sinc_table();
    }

    reset();
}

int AudioResamplerCPU::get_output_frames_max(int input_frames) const {
    if (!impl_ || input_frames <= 0) return 0;

    // Calculate maximum output frames based on ratio
    double ratio = static_cast<double>(impl_->output_rate) / static_cast<double>(impl_->input_rate);
    return static_cast<int>(std::ceil(input_frames * ratio)) + 1;
}

int AudioResamplerCPU::get_latency_frames() const {
    if (!impl_) return 0;

    // Latency is approximately half the filter length for symmetric filters
    return impl_->history_size / 2;
}

int AudioResamplerCPU::flush(float* output) {
    if (!impl_ || !output) return 0;

    // Feed zeros to flush the filter
    int latency = get_latency_frames();
    int channels = impl_->channels;

    std::vector<float> zeros(latency * channels, 0.0f);
    return process(zeros.data(), output, latency);
}

// ============================================================================
// AudioConfig Implementation
// ============================================================================

// Helper functions for parsing
static bool parse_audio_backend(const std::string& value, AudioBackend* out) {
    if (!out) return false;
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "auto") { *out = AudioBackend::Auto; return true; }
    if (lower == "wasapi") { *out = AudioBackend::WASAPI; return true; }
    if (lower == "coreaudio") { *out = AudioBackend::CoreAudio; return true; }
    if (lower == "pulseaudio") { *out = AudioBackend::PulseAudio; return true; }
    if (lower == "alsa") { *out = AudioBackend::ALSA; return true; }
    if (lower == "aaudio") { *out = AudioBackend::AAudio; return true; }
    if (lower == "opensles") { *out = AudioBackend::OpenSLES; return true; }
    if (lower == "webaudio") { *out = AudioBackend::WebAudio; return true; }
    if (lower == "openal") { *out = AudioBackend::OpenAL; return true; }

    return false;
}

static bool parse_sample_format(const std::string& value, SampleFormat* out) {
    if (!out) return false;
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "int16" || lower == "s16") { *out = SampleFormat::Int16; return true; }
    if (lower == "int24" || lower == "s24") { *out = SampleFormat::Int24; return true; }
    if (lower == "int32" || lower == "s32") { *out = SampleFormat::Int32; return true; }
    if (lower == "float32" || lower == "f32" || lower == "float") { *out = SampleFormat::Float32; return true; }

    return false;
}

static const char* audio_backend_to_config_string(AudioBackend backend) {
    switch (backend) {
        case AudioBackend::Auto:      return "auto";
        case AudioBackend::WASAPI:    return "wasapi";
        case AudioBackend::CoreAudio: return "coreaudio";
        case AudioBackend::PulseAudio: return "pulseaudio";
        case AudioBackend::ALSA:      return "alsa";
        case AudioBackend::AAudio:    return "aaudio";
        case AudioBackend::OpenSLES:  return "opensles";
        case AudioBackend::WebAudio:  return "webaudio";
        case AudioBackend::OpenAL:    return "openal";
        default: return "auto";
    }
}

static const char* sample_format_to_config_string(SampleFormat format) {
    switch (format) {
        case SampleFormat::Int16:   return "int16";
        case SampleFormat::Int24:   return "int24";
        case SampleFormat::Int32:   return "int32";
        case SampleFormat::Float32: return "float32";
        default: return "float32";
    }
}

bool AudioConfig::save(const char* filepath) const {
    try {
        std::ofstream file(filepath);
        if (!file) return false;

        file << "# Audio Configuration File\n";
        file << "# Generated by window audio library\n\n";

        file << "[audio]\n";
        file << "backend = " << audio_backend_to_config_string(backend) << "\n";
        file << "output_device_index = " << output_device_index << "\n";
        file << "output_device_name = " << output_device_name << "\n";
        file << "input_device_index = " << input_device_index << "\n";
        file << "input_device_name = " << input_device_name << "\n";
        file << "sample_rate = " << sample_rate << "\n";
        file << "channels = " << channels << "\n";
        file << "sample_format = " << sample_format_to_config_string(sample_format) << "\n";
        file << "buffer_frames = " << buffer_frames << "\n";
        file << "exclusive_mode = " << (exclusive_mode ? "true" : "false") << "\n";
        file << "master_volume = " << master_volume << "\n";
        file << "\n";

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool AudioConfig::load(const char* filepath, AudioConfig* out_config) {
    if (!out_config) return false;

    try {
        pt::ptree tree;
        pt::read_ini(filepath, tree);

        // Initialize with defaults
        *out_config = AudioConfig{};

        // Parse audio section
        if (auto audio = tree.get_child_optional("audio")) {
            std::string backend_str = audio->get<std::string>("backend", "auto");
            parse_audio_backend(backend_str, &out_config->backend);

            out_config->output_device_index = audio->get<int>("output_device_index", -1);
            out_config->output_device_name = audio->get<std::string>("output_device_name", "");
            out_config->input_device_index = audio->get<int>("input_device_index", -1);
            out_config->input_device_name = audio->get<std::string>("input_device_name", "");
            out_config->sample_rate = audio->get<int>("sample_rate", 48000);
            out_config->channels = audio->get<int>("channels", 2);

            std::string format_str = audio->get<std::string>("sample_format", "float32");
            parse_sample_format(format_str, &out_config->sample_format);

            out_config->buffer_frames = audio->get<int>("buffer_frames", 0);
            out_config->exclusive_mode = audio->get<bool>("exclusive_mode", false);
            out_config->master_volume = audio->get<float>("master_volume", 1.0f);
        }

        // Validate after loading
        out_config->validate();

        return true;
    } catch (const pt::ini_parser_error&) {
        return false;
    } catch (const std::exception&) {
        return false;
    }
}

bool AudioConfig::validate() {
    bool all_valid = true;

    // Validate sample rate
    if (sample_rate < 8000 || sample_rate > 192000) {
        sample_rate = 48000;
        all_valid = false;
    }

    // Validate channels
    if (channels < 1 || channels > MAX_AUDIO_CHANNELS) {
        channels = 2;
        all_valid = false;
    }

    // Validate sample format
    if (sample_format == SampleFormat::Unknown) {
        sample_format = SampleFormat::Float32;
        all_valid = false;
    }

    // Validate buffer frames (0 = auto, otherwise reasonable range)
    if (buffer_frames < 0 || buffer_frames > 8192) {
        buffer_frames = 0;  // Auto
        all_valid = false;
    }

    // Validate master volume
    if (master_volume < 0.0f || master_volume > 1.0f) {
        master_volume = std::max(0.0f, std::min(1.0f, master_volume));
        all_valid = false;
    }

    // Note: Backend validation happens at runtime when AudioManager::initialize() is called
    // We don't validate it here since is_backend_supported() is platform-specific

    return all_valid;
}

// ============================================================================
// Audio Effect Chain Implementation
// ============================================================================

struct AudioEffectChain::Impl {
    IAudioEffect* effects[MAX_AUDIO_EFFECTS] = {};
    int effect_count = 0;
    bool enabled = true;
    float output_gain = 1.0f;
};

AudioEffectChain::AudioEffectChain() {
    impl_ = new Impl();
}

AudioEffectChain::~AudioEffectChain() {
    delete impl_;
}

bool AudioEffectChain::add_effect(IAudioEffect* effect) {
    if (!effect || impl_->effect_count >= MAX_AUDIO_EFFECTS) return false;
    impl_->effects[impl_->effect_count++] = effect;
    return true;
}

bool AudioEffectChain::insert_effect(int index, IAudioEffect* effect) {
    if (!effect || index < 0 || index > impl_->effect_count ||
        impl_->effect_count >= MAX_AUDIO_EFFECTS) return false;

    // Shift effects to make room
    for (int i = impl_->effect_count; i > index; --i) {
        impl_->effects[i] = impl_->effects[i - 1];
    }
    impl_->effects[index] = effect;
    impl_->effect_count++;
    return true;
}

bool AudioEffectChain::remove_effect(IAudioEffect* effect) {
    for (int i = 0; i < impl_->effect_count; ++i) {
        if (impl_->effects[i] == effect) {
            return remove_effect_at(i);
        }
    }
    return false;
}

bool AudioEffectChain::remove_effect_at(int index) {
    if (index < 0 || index >= impl_->effect_count) return false;

    // Shift effects down
    for (int i = index; i < impl_->effect_count - 1; ++i) {
        impl_->effects[i] = impl_->effects[i + 1];
    }
    impl_->effects[--impl_->effect_count] = nullptr;
    return true;
}

IAudioEffect* AudioEffectChain::get_effect(int index) const {
    if (index < 0 || index >= impl_->effect_count) return nullptr;
    return impl_->effects[index];
}

int AudioEffectChain::get_effect_count() const {
    return impl_->effect_count;
}

void AudioEffectChain::clear() {
    for (int i = 0; i < impl_->effect_count; ++i) {
        impl_->effects[i] = nullptr;
    }
    impl_->effect_count = 0;
}

void AudioEffectChain::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled || !samples || frame_count <= 0 || channels <= 0) return;

    // Process through each effect in the chain
    for (int i = 0; i < impl_->effect_count; ++i) {
        IAudioEffect* effect = impl_->effects[i];
        if (effect && effect->is_enabled()) {
            effect->process(samples, frame_count, channels);
        }
    }

    // Apply output gain
    if (impl_->output_gain != 1.0f) {
        int sample_count = frame_count * channels;
        for (int i = 0; i < sample_count; ++i) {
            samples[i] *= impl_->output_gain;
        }
    }
}

void AudioEffectChain::reset() {
    for (int i = 0; i < impl_->effect_count; ++i) {
        if (impl_->effects[i]) {
            impl_->effects[i]->reset();
        }
    }
}

int AudioEffectChain::get_total_latency_frames() const {
    int total = 0;
    for (int i = 0; i < impl_->effect_count; ++i) {
        if (impl_->effects[i]) {
            total += impl_->effects[i]->get_latency_frames();
        }
    }
    return total;
}

void AudioEffectChain::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

bool AudioEffectChain::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectChain::set_output_gain(float gain) {
    impl_->output_gain = std::max(0.0f, gain);
}

float AudioEffectChain::get_output_gain() const {
    return impl_->output_gain;
}

// ============================================================================
// AudioEffectGain Implementation
// ============================================================================

struct AudioEffectGain::Impl {
    float gain_linear = 1.0f;
    float gain_db = 0.0f;
    bool enabled = true;
};

AudioEffectGain* AudioEffectGain::create(float gain_db) {
    AudioEffectGain* effect = new AudioEffectGain();
    effect->impl_ = new Impl();
    effect->set_gain_db(gain_db);
    return effect;
}

void AudioEffectGain::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectGain::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled || impl_->gain_linear == 1.0f) return;

    int count = frame_count * channels;
    for (int i = 0; i < count; ++i) {
        samples[i] *= impl_->gain_linear;
    }
}

void AudioEffectGain::reset() {
    // Gain has no state to reset
}

bool AudioEffectGain::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectGain::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void AudioEffectGain::set_gain_db(float db) {
    impl_->gain_db = db;
    impl_->gain_linear = std::pow(10.0f, db / 20.0f);
}

float AudioEffectGain::get_gain_db() const {
    return impl_->gain_db;
}

void AudioEffectGain::set_gain_linear(float gain) {
    impl_->gain_linear = std::max(0.0f, gain);
    impl_->gain_db = (gain > 0.0f) ? (20.0f * std::log10(gain)) : -100.0f;
}

float AudioEffectGain::get_gain_linear() const {
    return impl_->gain_linear;
}

// ============================================================================
// AudioEffectPan Implementation
// ============================================================================

struct AudioEffectPan::Impl {
    float pan = 0.0f;           // -1 to 1
    float left_gain = 1.0f;
    float right_gain = 1.0f;
    float pan_law_db = -3.0f;   // Constant power pan law
    bool enabled = true;

    void update_gains() {
        // Constant power panning
        float angle = (pan + 1.0f) * 0.25f * static_cast<float>(PI);
        left_gain = std::cos(angle);
        right_gain = std::sin(angle);

        // Apply pan law compensation
        float compensation = std::pow(10.0f, pan_law_db / 20.0f);
        float center_boost = 1.0f / compensation;

        // Interpolate compensation based on pan position
        float pan_abs = std::abs(pan);
        float comp = 1.0f + (center_boost - 1.0f) * (1.0f - pan_abs);
        left_gain *= comp;
        right_gain *= comp;
    }
};

AudioEffectPan* AudioEffectPan::create(float pan) {
    AudioEffectPan* effect = new AudioEffectPan();
    effect->impl_ = new Impl();
    effect->set_pan(pan);
    return effect;
}

void AudioEffectPan::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectPan::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled || channels < 2) return;

    for (int f = 0; f < frame_count; ++f) {
        float* frame = samples + f * channels;
        frame[0] *= impl_->left_gain;
        frame[1] *= impl_->right_gain;
    }
}

void AudioEffectPan::reset() {
    // Pan has no state to reset
}

bool AudioEffectPan::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectPan::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void AudioEffectPan::set_pan(float pan) {
    impl_->pan = std::max(-1.0f, std::min(1.0f, pan));
    impl_->update_gains();
}

float AudioEffectPan::get_pan() const {
    return impl_->pan;
}

void AudioEffectPan::set_pan_law_db(float db) {
    impl_->pan_law_db = db;
    impl_->update_gains();
}

// ============================================================================
// AudioEffectDelay Implementation
// ============================================================================

struct AudioEffectDelay::Impl {
    std::vector<float> buffer;
    int write_pos = 0;
    int delay_samples = 0;
    int max_delay_samples = 0;
    int sample_rate = 48000;
    int channels = 2;
    float delay_ms = 0.0f;
    float feedback = 0.0f;
    float mix = 0.5f;
    bool enabled = true;
};

AudioEffectDelay* AudioEffectDelay::create(int sample_rate, float max_delay_ms) {
    AudioEffectDelay* effect = new AudioEffectDelay();
    effect->impl_ = new Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->max_delay_samples = static_cast<int>(max_delay_ms * sample_rate / 1000.0f);
    effect->impl_->buffer.resize(effect->impl_->max_delay_samples * MAX_AUDIO_CHANNELS, 0.0f);
    return effect;
}

void AudioEffectDelay::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectDelay::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled || impl_->delay_samples == 0) return;

    impl_->channels = channels;
    int buffer_size = impl_->max_delay_samples;

    for (int f = 0; f < frame_count; ++f) {
        for (int c = 0; c < channels; ++c) {
            int idx = f * channels + c;

            // Read from delay buffer
            int read_pos = (impl_->write_pos - impl_->delay_samples + buffer_size) % buffer_size;
            float delayed = impl_->buffer[read_pos * channels + c];

            // Write to delay buffer (input + feedback)
            impl_->buffer[impl_->write_pos * channels + c] =
                samples[idx] + delayed * impl_->feedback;

            // Mix dry and wet signals
            samples[idx] = samples[idx] * (1.0f - impl_->mix) + delayed * impl_->mix;
        }

        impl_->write_pos = (impl_->write_pos + 1) % buffer_size;
    }
}

void AudioEffectDelay::reset() {
    std::fill(impl_->buffer.begin(), impl_->buffer.end(), 0.0f);
    impl_->write_pos = 0;
}

int AudioEffectDelay::get_latency_frames() const {
    return 0; // Delay doesn't add processing latency
}

bool AudioEffectDelay::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectDelay::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

float AudioEffectDelay::get_mix() const {
    return impl_->mix;
}

void AudioEffectDelay::set_mix(float mix) {
    impl_->mix = std::max(0.0f, std::min(1.0f, mix));
}

void AudioEffectDelay::set_delay_ms(float ms) {
    impl_->delay_ms = std::max(0.0f, ms);
    impl_->delay_samples = std::min(
        static_cast<int>(ms * impl_->sample_rate / 1000.0f),
        impl_->max_delay_samples
    );
}

float AudioEffectDelay::get_delay_ms() const {
    return impl_->delay_ms;
}

void AudioEffectDelay::set_feedback(float feedback) {
    impl_->feedback = std::max(0.0f, std::min(0.99f, feedback));
}

float AudioEffectDelay::get_feedback() const {
    return impl_->feedback;
}

// ============================================================================
// AudioEffectBiquadFilter Implementation
// ============================================================================

struct AudioEffectBiquadFilter::Impl {
    BiquadFilterType type = BiquadFilterType::LowPass;
    int sample_rate = 48000;
    float frequency = 1000.0f;
    float q = 0.707f;           // Butterworth
    float gain_db = 0.0f;
    bool enabled = true;

    // Biquad coefficients
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;

    // State variables per channel
    float x1[MAX_AUDIO_CHANNELS] = {};
    float x2[MAX_AUDIO_CHANNELS] = {};
    float y1[MAX_AUDIO_CHANNELS] = {};
    float y2[MAX_AUDIO_CHANNELS] = {};

    void calculate_coefficients() {
        float w0 = 2.0f * static_cast<float>(PI) * frequency / sample_rate;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 / (2.0f * q);
        float A = std::pow(10.0f, gain_db / 40.0f);

        float a0;

        switch (type) {
            case BiquadFilterType::LowPass:
                b0 = (1.0f - cos_w0) / 2.0f;
                b1 = 1.0f - cos_w0;
                b2 = (1.0f - cos_w0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;

            case BiquadFilterType::HighPass:
                b0 = (1.0f + cos_w0) / 2.0f;
                b1 = -(1.0f + cos_w0);
                b2 = (1.0f + cos_w0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;

            case BiquadFilterType::BandPass:
                b0 = alpha;
                b1 = 0.0f;
                b2 = -alpha;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;

            case BiquadFilterType::Notch:
                b0 = 1.0f;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;

            case BiquadFilterType::AllPass:
                b0 = 1.0f - alpha;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f + alpha;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;

            case BiquadFilterType::PeakingEQ: {
                float alpha_A = alpha * A;
                float alpha_div_A = alpha / A;
                b0 = 1.0f + alpha_A;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f - alpha_A;
                a0 = 1.0f + alpha_div_A;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha_div_A;
                break;
            }

            case BiquadFilterType::LowShelf: {
                float sqrtA = std::sqrt(A);
                float sqrtA_alpha = 2.0f * sqrtA * alpha;
                b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + sqrtA_alpha);
                b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
                b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - sqrtA_alpha);
                a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + sqrtA_alpha;
                a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
                a2 = (A + 1.0f) + (A - 1.0f) * cos_w0 - sqrtA_alpha;
                break;
            }

            case BiquadFilterType::HighShelf: {
                float sqrtA = std::sqrt(A);
                float sqrtA_alpha = 2.0f * sqrtA * alpha;
                b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + sqrtA_alpha);
                b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
                b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - sqrtA_alpha);
                a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + sqrtA_alpha;
                a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
                a2 = (A + 1.0f) - (A - 1.0f) * cos_w0 - sqrtA_alpha;
                break;
            }
        }

        // Normalize coefficients
        b0 /= a0;
        b1 /= a0;
        b2 /= a0;
        a1 /= a0;
        a2 /= a0;
    }
};

AudioEffectBiquadFilter* AudioEffectBiquadFilter::create(int sample_rate, BiquadFilterType type) {
    AudioEffectBiquadFilter* effect = new AudioEffectBiquadFilter();
    effect->impl_ = new Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->type = type;
    effect->impl_->calculate_coefficients();
    return effect;
}

void AudioEffectBiquadFilter::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectBiquadFilter::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int f = 0; f < frame_count; ++f) {
        for (int c = 0; c < channels && c < MAX_AUDIO_CHANNELS; ++c) {
            int idx = f * channels + c;
            float x0 = samples[idx];

            // Direct Form I biquad
            float y0 = impl_->b0 * x0 +
                       impl_->b1 * impl_->x1[c] +
                       impl_->b2 * impl_->x2[c] -
                       impl_->a1 * impl_->y1[c] -
                       impl_->a2 * impl_->y2[c];

            // Update state
            impl_->x2[c] = impl_->x1[c];
            impl_->x1[c] = x0;
            impl_->y2[c] = impl_->y1[c];
            impl_->y1[c] = y0;

            samples[idx] = y0;
        }
    }
}

void AudioEffectBiquadFilter::reset() {
    for (int c = 0; c < MAX_AUDIO_CHANNELS; ++c) {
        impl_->x1[c] = impl_->x2[c] = 0.0f;
        impl_->y1[c] = impl_->y2[c] = 0.0f;
    }
}

AudioEffectType AudioEffectBiquadFilter::get_type() const {
    switch (impl_->type) {
        case BiquadFilterType::LowPass:   return AudioEffectType::LowPassFilter;
        case BiquadFilterType::HighPass:  return AudioEffectType::HighPassFilter;
        case BiquadFilterType::BandPass:  return AudioEffectType::BandPassFilter;
        case BiquadFilterType::Notch:     return AudioEffectType::Notch;
        case BiquadFilterType::PeakingEQ: return AudioEffectType::PeakingEQ;
        case BiquadFilterType::LowShelf:  return AudioEffectType::LowShelf;
        case BiquadFilterType::HighShelf: return AudioEffectType::HighShelf;
        default: return AudioEffectType::Custom;
    }
}

bool AudioEffectBiquadFilter::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectBiquadFilter::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void AudioEffectBiquadFilter::set_filter_type(BiquadFilterType type) {
    impl_->type = type;
    impl_->calculate_coefficients();
}

BiquadFilterType AudioEffectBiquadFilter::get_filter_type() const {
    return impl_->type;
}

void AudioEffectBiquadFilter::set_frequency(float hz) {
    impl_->frequency = std::max(20.0f, std::min(static_cast<float>(impl_->sample_rate) / 2.0f, hz));
    impl_->calculate_coefficients();
}

float AudioEffectBiquadFilter::get_frequency() const {
    return impl_->frequency;
}

void AudioEffectBiquadFilter::set_q(float q) {
    impl_->q = std::max(0.1f, std::min(20.0f, q));
    impl_->calculate_coefficients();
}

float AudioEffectBiquadFilter::get_q() const {
    return impl_->q;
}

void AudioEffectBiquadFilter::set_gain_db(float db) {
    impl_->gain_db = std::max(-24.0f, std::min(24.0f, db));
    impl_->calculate_coefficients();
}

float AudioEffectBiquadFilter::get_gain_db() const {
    return impl_->gain_db;
}

// ============================================================================
// AudioEffectCompressor Implementation
// ============================================================================

struct AudioEffectCompressor::Impl {
    int sample_rate = 48000;
    float threshold_db = -20.0f;
    float ratio = 4.0f;
    float attack_ms = 10.0f;
    float release_ms = 100.0f;
    float makeup_gain_db = 0.0f;
    float knee_db = 0.0f;
    float mix = 1.0f;
    bool enabled = true;

    // Runtime state
    float envelope = 0.0f;
    float gain_reduction_db = 0.0f;
    float attack_coef = 0.0f;
    float release_coef = 0.0f;
    float makeup_gain_linear = 1.0f;

    void update_coefficients() {
        attack_coef = std::exp(-1.0f / (attack_ms * sample_rate / 1000.0f));
        release_coef = std::exp(-1.0f / (release_ms * sample_rate / 1000.0f));
        makeup_gain_linear = std::pow(10.0f, makeup_gain_db / 20.0f);
    }

    float compute_gain(float input_db) {
        // Compute output level based on threshold, ratio, and knee
        float output_db;

        if (knee_db > 0.0f) {
            // Soft knee
            float knee_start = threshold_db - knee_db / 2.0f;
            float knee_end = threshold_db + knee_db / 2.0f;

            if (input_db < knee_start) {
                output_db = input_db;
            } else if (input_db > knee_end) {
                output_db = threshold_db + (input_db - threshold_db) / ratio;
            } else {
                // Quadratic interpolation in the knee region
                float x = input_db - knee_start;
                float knee_factor = (1.0f / ratio - 1.0f) / (2.0f * knee_db);
                output_db = input_db + knee_factor * x * x;
            }
        } else {
            // Hard knee
            if (input_db < threshold_db) {
                output_db = input_db;
            } else {
                output_db = threshold_db + (input_db - threshold_db) / ratio;
            }
        }

        return output_db - input_db;  // Return gain reduction
    }
};

AudioEffectCompressor* AudioEffectCompressor::create(int sample_rate) {
    AudioEffectCompressor* effect = new AudioEffectCompressor();
    effect->impl_ = new Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->update_coefficients();
    return effect;
}

void AudioEffectCompressor::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectCompressor::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int f = 0; f < frame_count; ++f) {
        // Find peak level across all channels
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float abs_sample = std::abs(samples[f * channels + c]);
            if (abs_sample > peak) peak = abs_sample;
        }

        // Convert to dB
        float input_db = (peak > 1e-6f) ? (20.0f * std::log10(peak)) : -120.0f;

        // Compute target gain reduction
        float target_gr = impl_->compute_gain(input_db);

        // Apply envelope follower (attack/release)
        if (target_gr < impl_->gain_reduction_db) {
            // Attack
            impl_->gain_reduction_db = target_gr + impl_->attack_coef *
                (impl_->gain_reduction_db - target_gr);
        } else {
            // Release
            impl_->gain_reduction_db = target_gr + impl_->release_coef *
                (impl_->gain_reduction_db - target_gr);
        }

        // Convert gain reduction to linear
        float gain = std::pow(10.0f, impl_->gain_reduction_db / 20.0f) *
                     impl_->makeup_gain_linear;

        // Apply gain to all channels
        for (int c = 0; c < channels; ++c) {
            int idx = f * channels + c;
            float dry = samples[idx];
            float wet = samples[idx] * gain;
            samples[idx] = dry * (1.0f - impl_->mix) + wet * impl_->mix;
        }
    }
}

void AudioEffectCompressor::reset() {
    impl_->envelope = 0.0f;
    impl_->gain_reduction_db = 0.0f;
}

bool AudioEffectCompressor::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectCompressor::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

float AudioEffectCompressor::get_mix() const {
    return impl_->mix;
}

void AudioEffectCompressor::set_mix(float mix) {
    impl_->mix = std::max(0.0f, std::min(1.0f, mix));
}

void AudioEffectCompressor::set_threshold_db(float db) {
    impl_->threshold_db = std::max(-60.0f, std::min(0.0f, db));
}

float AudioEffectCompressor::get_threshold_db() const {
    return impl_->threshold_db;
}

void AudioEffectCompressor::set_ratio(float ratio) {
    impl_->ratio = std::max(1.0f, std::min(100.0f, ratio));
}

float AudioEffectCompressor::get_ratio() const {
    return impl_->ratio;
}

void AudioEffectCompressor::set_attack_ms(float ms) {
    impl_->attack_ms = std::max(0.1f, std::min(500.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectCompressor::get_attack_ms() const {
    return impl_->attack_ms;
}

void AudioEffectCompressor::set_release_ms(float ms) {
    impl_->release_ms = std::max(1.0f, std::min(5000.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectCompressor::get_release_ms() const {
    return impl_->release_ms;
}

void AudioEffectCompressor::set_makeup_gain_db(float db) {
    impl_->makeup_gain_db = std::max(0.0f, std::min(40.0f, db));
    impl_->update_coefficients();
}

float AudioEffectCompressor::get_makeup_gain_db() const {
    return impl_->makeup_gain_db;
}

void AudioEffectCompressor::set_knee_db(float db) {
    impl_->knee_db = std::max(0.0f, std::min(20.0f, db));
}

float AudioEffectCompressor::get_knee_db() const {
    return impl_->knee_db;
}

float AudioEffectCompressor::get_gain_reduction_db() const {
    return impl_->gain_reduction_db;
}

// ============================================================================
// AudioEffectLimiter Implementation
// ============================================================================

struct AudioEffectLimiter::Impl {
    int sample_rate = 48000;
    float ceiling_db = 0.0f;
    float ceiling_linear = 1.0f;
    float release_ms = 50.0f;
    float release_coef = 0.0f;
    bool enabled = true;

    float gain_reduction = 1.0f;  // Linear
    float gain_reduction_db = 0.0f;

    void update_coefficients() {
        ceiling_linear = std::pow(10.0f, ceiling_db / 20.0f);
        release_coef = std::exp(-1.0f / (release_ms * sample_rate / 1000.0f));
    }
};

AudioEffectLimiter* AudioEffectLimiter::create(int sample_rate) {
    AudioEffectLimiter* effect = new AudioEffectLimiter();
    effect->impl_ = new Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->update_coefficients();
    return effect;
}

void AudioEffectLimiter::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectLimiter::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int f = 0; f < frame_count; ++f) {
        // Find peak level across all channels
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float abs_sample = std::abs(samples[f * channels + c]);
            if (abs_sample > peak) peak = abs_sample;
        }

        // Calculate required gain reduction
        float target_gain = 1.0f;
        if (peak > impl_->ceiling_linear) {
            target_gain = impl_->ceiling_linear / peak;
        }

        // Apply instant attack, smooth release
        if (target_gain < impl_->gain_reduction) {
            // Instant attack
            impl_->gain_reduction = target_gain;
        } else {
            // Smooth release
            impl_->gain_reduction = target_gain + impl_->release_coef *
                (impl_->gain_reduction - target_gain);
        }

        // Update dB value for metering
        impl_->gain_reduction_db = 20.0f * std::log10(std::max(impl_->gain_reduction, 1e-6f));

        // Apply gain to all channels
        for (int c = 0; c < channels; ++c) {
            samples[f * channels + c] *= impl_->gain_reduction;
        }
    }
}

void AudioEffectLimiter::reset() {
    impl_->gain_reduction = 1.0f;
    impl_->gain_reduction_db = 0.0f;
}

bool AudioEffectLimiter::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectLimiter::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void AudioEffectLimiter::set_ceiling_db(float db) {
    impl_->ceiling_db = std::max(-20.0f, std::min(0.0f, db));
    impl_->update_coefficients();
}

float AudioEffectLimiter::get_ceiling_db() const {
    return impl_->ceiling_db;
}

void AudioEffectLimiter::set_release_ms(float ms) {
    impl_->release_ms = std::max(1.0f, std::min(1000.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectLimiter::get_release_ms() const {
    return impl_->release_ms;
}

float AudioEffectLimiter::get_gain_reduction_db() const {
    return impl_->gain_reduction_db;
}

// ============================================================================
// AudioEffectNoiseGate Implementation
// ============================================================================

struct AudioEffectNoiseGate::Impl {
    int sample_rate = 48000;
    bool enabled = true;
    float threshold_db = -40.0f;
    float threshold_linear = 0.01f;
    float attack_ms = 1.0f;
    float hold_ms = 50.0f;
    float release_ms = 100.0f;
    float range_db = -80.0f;
    float range_linear = 0.0001f;

    // Envelope follower state
    float envelope = 0.0f;
    float gate_level = 0.0f;
    int hold_counter = 0;

    // Coefficients
    float attack_coeff = 0.0f;
    float release_coeff = 0.0f;
    int hold_samples = 0;

    void update_coefficients() {
        attack_coeff = std::exp(-1.0f / (attack_ms * 0.001f * sample_rate));
        release_coeff = std::exp(-1.0f / (release_ms * 0.001f * sample_rate));
        hold_samples = static_cast<int>(hold_ms * 0.001f * sample_rate);
        threshold_linear = std::pow(10.0f, threshold_db / 20.0f);
        range_linear = std::pow(10.0f, range_db / 20.0f);
    }
};

AudioEffectNoiseGate* AudioEffectNoiseGate::create(int sample_rate) {
    AudioEffectNoiseGate* effect = new AudioEffectNoiseGate();
    effect->impl_ = new AudioEffectNoiseGate::Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->update_coefficients();
    return effect;
}

void AudioEffectNoiseGate::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectNoiseGate::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int i = 0; i < frame_count; ++i) {
        // Compute peak level across all channels
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float sample = std::abs(samples[i * channels + c]);
            if (sample > peak) peak = sample;
        }

        // Envelope follower
        if (peak > impl_->envelope) {
            impl_->envelope = peak + impl_->attack_coeff * (impl_->envelope - peak);
        } else {
            impl_->envelope = peak + impl_->release_coeff * (impl_->envelope - peak);
        }

        // Gate logic
        if (impl_->envelope > impl_->threshold_linear) {
            // Signal above threshold - open gate
            impl_->hold_counter = impl_->hold_samples;
            impl_->gate_level = impl_->gate_level + impl_->attack_coeff * (1.0f - impl_->gate_level);
        } else if (impl_->hold_counter > 0) {
            // In hold phase
            impl_->hold_counter--;
        } else {
            // Close gate
            float target = impl_->range_linear;
            impl_->gate_level = target + impl_->release_coeff * (impl_->gate_level - target);
        }

        // Apply gate
        for (int c = 0; c < channels; ++c) {
            samples[i * channels + c] *= impl_->gate_level;
        }
    }
}

void AudioEffectNoiseGate::reset() {
    impl_->envelope = 0.0f;
    impl_->gate_level = 0.0f;
    impl_->hold_counter = 0;
}

bool AudioEffectNoiseGate::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectNoiseGate::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void AudioEffectNoiseGate::set_threshold_db(float db) {
    impl_->threshold_db = std::max(-80.0f, std::min(0.0f, db));
    impl_->update_coefficients();
}

float AudioEffectNoiseGate::get_threshold_db() const {
    return impl_->threshold_db;
}

void AudioEffectNoiseGate::set_attack_ms(float ms) {
    impl_->attack_ms = std::max(0.1f, std::min(100.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectNoiseGate::get_attack_ms() const {
    return impl_->attack_ms;
}

void AudioEffectNoiseGate::set_hold_ms(float ms) {
    impl_->hold_ms = std::max(0.0f, std::min(1000.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectNoiseGate::get_hold_ms() const {
    return impl_->hold_ms;
}

void AudioEffectNoiseGate::set_release_ms(float ms) {
    impl_->release_ms = std::max(1.0f, std::min(2000.0f, ms));
    impl_->update_coefficients();
}

float AudioEffectNoiseGate::get_release_ms() const {
    return impl_->release_ms;
}

void AudioEffectNoiseGate::set_range_db(float db) {
    impl_->range_db = std::max(-80.0f, std::min(0.0f, db));
    impl_->update_coefficients();
}

float AudioEffectNoiseGate::get_range_db() const {
    return impl_->range_db;
}

float AudioEffectNoiseGate::get_gate_level() const {
    return impl_->gate_level;
}

// ============================================================================
// AudioEffectReverb Implementation
// ============================================================================

// Freeverb-style reverb implementation
struct AudioEffectReverb::Impl {
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASSES = 4;

    // Comb filter delay times (in samples at 44100 Hz)
    static constexpr int COMB_TUNING_L[NUM_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int COMB_TUNING_R[NUM_COMBS] = {1116+23, 1188+23, 1277+23, 1356+23, 1422+23, 1491+23, 1557+23, 1617+23};

    // Allpass filter delay times
    static constexpr int ALLPASS_TUNING_L[NUM_ALLPASSES] = {556, 441, 341, 225};
    static constexpr int ALLPASS_TUNING_R[NUM_ALLPASSES] = {556+23, 441+23, 341+23, 225+23};

    int sample_rate = 48000;
    bool enabled = true;
    float mix = 0.3f;
    float room_size = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;
    float pre_delay_ms = 0.0f;
    bool freeze = false;

    // Processed parameters
    float feedback = 0.0f;
    float damp1 = 0.0f;
    float damp2 = 0.0f;
    float wet1 = 0.0f;
    float wet2 = 0.0f;

    // Comb filters
    struct CombFilter {
        std::vector<float> buffer;
        int buffer_size = 0;
        int buffer_idx = 0;
        float filter_store = 0.0f;
    };
    CombFilter combs_l[NUM_COMBS];
    CombFilter combs_r[NUM_COMBS];

    // Allpass filters
    struct AllpassFilter {
        std::vector<float> buffer;
        int buffer_size = 0;
        int buffer_idx = 0;
    };
    AllpassFilter allpasses_l[NUM_ALLPASSES];
    AllpassFilter allpasses_r[NUM_ALLPASSES];

    // Pre-delay buffer
    std::vector<float> predelay_buffer_l;
    std::vector<float> predelay_buffer_r;
    int predelay_size = 0;
    int predelay_idx = 0;

    void init_buffers() {
        float scale = static_cast<float>(sample_rate) / 44100.0f;

        for (int i = 0; i < NUM_COMBS; ++i) {
            combs_l[i].buffer_size = static_cast<int>(COMB_TUNING_L[i] * scale);
            combs_l[i].buffer.resize(combs_l[i].buffer_size, 0.0f);
            combs_l[i].buffer_idx = 0;
            combs_l[i].filter_store = 0.0f;

            combs_r[i].buffer_size = static_cast<int>(COMB_TUNING_R[i] * scale);
            combs_r[i].buffer.resize(combs_r[i].buffer_size, 0.0f);
            combs_r[i].buffer_idx = 0;
            combs_r[i].filter_store = 0.0f;
        }

        for (int i = 0; i < NUM_ALLPASSES; ++i) {
            allpasses_l[i].buffer_size = static_cast<int>(ALLPASS_TUNING_L[i] * scale);
            allpasses_l[i].buffer.resize(allpasses_l[i].buffer_size, 0.0f);
            allpasses_l[i].buffer_idx = 0;

            allpasses_r[i].buffer_size = static_cast<int>(ALLPASS_TUNING_R[i] * scale);
            allpasses_r[i].buffer.resize(allpasses_r[i].buffer_size, 0.0f);
            allpasses_r[i].buffer_idx = 0;
        }

        // Pre-delay buffer (max 500ms)
        int max_predelay = static_cast<int>(0.5f * sample_rate);
        predelay_buffer_l.resize(max_predelay, 0.0f);
        predelay_buffer_r.resize(max_predelay, 0.0f);
        predelay_idx = 0;
        update_predelay();
    }

    void update_parameters() {
        feedback = room_size * 0.28f + 0.7f;
        damp1 = damping * 0.4f;
        damp2 = 1.0f - damp1;
        wet1 = width * 0.5f + 0.5f;
        wet2 = (1.0f - width) * 0.5f;

        if (freeze) {
            feedback = 1.0f;
            damp1 = 0.0f;
            damp2 = 1.0f;
        }
    }

    void update_predelay() {
        predelay_size = static_cast<int>(pre_delay_ms * 0.001f * sample_rate);
        predelay_size = std::max(1, std::min(predelay_size, static_cast<int>(predelay_buffer_l.size())));
    }

    float process_comb(CombFilter& comb, float input) {
        float output = comb.buffer[comb.buffer_idx];
        comb.filter_store = output * damp2 + comb.filter_store * damp1;
        comb.buffer[comb.buffer_idx] = input + comb.filter_store * feedback;
        if (++comb.buffer_idx >= comb.buffer_size) comb.buffer_idx = 0;
        return output;
    }

    float process_allpass(AllpassFilter& ap, float input) {
        float bufout = ap.buffer[ap.buffer_idx];
        float output = -input + bufout;
        ap.buffer[ap.buffer_idx] = input + bufout * 0.5f;
        if (++ap.buffer_idx >= ap.buffer_size) ap.buffer_idx = 0;
        return output;
    }
};

constexpr int AudioEffectReverb::Impl::COMB_TUNING_L[NUM_COMBS];
constexpr int AudioEffectReverb::Impl::COMB_TUNING_R[NUM_COMBS];
constexpr int AudioEffectReverb::Impl::ALLPASS_TUNING_L[NUM_ALLPASSES];
constexpr int AudioEffectReverb::Impl::ALLPASS_TUNING_R[NUM_ALLPASSES];

AudioEffectReverb* AudioEffectReverb::create(int sample_rate) {
    AudioEffectReverb* effect = new AudioEffectReverb();
    effect->impl_ = new AudioEffectReverb::Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->init_buffers();
    effect->impl_->update_parameters();
    return effect;
}

void AudioEffectReverb::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectReverb::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int i = 0; i < frame_count; ++i) {
        float in_l = samples[i * channels];
        float in_r = (channels >= 2) ? samples[i * channels + 1] : in_l;

        // Pre-delay
        int predelay_read = (impl_->predelay_idx - impl_->predelay_size + impl_->predelay_buffer_l.size())
                           % impl_->predelay_buffer_l.size();
        float delayed_l = impl_->predelay_buffer_l[predelay_read];
        float delayed_r = impl_->predelay_buffer_r[predelay_read];
        impl_->predelay_buffer_l[impl_->predelay_idx] = in_l;
        impl_->predelay_buffer_r[impl_->predelay_idx] = in_r;
        if (++impl_->predelay_idx >= static_cast<int>(impl_->predelay_buffer_l.size())) {
            impl_->predelay_idx = 0;
        }

        float input = (delayed_l + delayed_r) * 0.5f;

        // Accumulate comb filters in parallel
        float out_l = 0.0f;
        float out_r = 0.0f;
        for (int c = 0; c < AudioEffectReverb::Impl::NUM_COMBS; ++c) {
            out_l += impl_->process_comb(impl_->combs_l[c], input);
            out_r += impl_->process_comb(impl_->combs_r[c], input);
        }

        // Feed through allpasses in series
        for (int a = 0; a < AudioEffectReverb::Impl::NUM_ALLPASSES; ++a) {
            out_l = impl_->process_allpass(impl_->allpasses_l[a], out_l);
            out_r = impl_->process_allpass(impl_->allpasses_r[a], out_r);
        }

        // Mix wet and dry with width
        float wet_l = out_l * impl_->wet1 + out_r * impl_->wet2;
        float wet_r = out_r * impl_->wet1 + out_l * impl_->wet2;

        samples[i * channels] = in_l * (1.0f - impl_->mix) + wet_l * impl_->mix;
        if (channels >= 2) {
            samples[i * channels + 1] = in_r * (1.0f - impl_->mix) + wet_r * impl_->mix;
        }
    }
}

void AudioEffectReverb::reset() {
    for (int i = 0; i < AudioEffectReverb::Impl::NUM_COMBS; ++i) {
        std::fill(impl_->combs_l[i].buffer.begin(), impl_->combs_l[i].buffer.end(), 0.0f);
        std::fill(impl_->combs_r[i].buffer.begin(), impl_->combs_r[i].buffer.end(), 0.0f);
        impl_->combs_l[i].filter_store = 0.0f;
        impl_->combs_r[i].filter_store = 0.0f;
    }
    for (int i = 0; i < AudioEffectReverb::Impl::NUM_ALLPASSES; ++i) {
        std::fill(impl_->allpasses_l[i].buffer.begin(), impl_->allpasses_l[i].buffer.end(), 0.0f);
        std::fill(impl_->allpasses_r[i].buffer.begin(), impl_->allpasses_r[i].buffer.end(), 0.0f);
    }
    std::fill(impl_->predelay_buffer_l.begin(), impl_->predelay_buffer_l.end(), 0.0f);
    std::fill(impl_->predelay_buffer_r.begin(), impl_->predelay_buffer_r.end(), 0.0f);
}

int AudioEffectReverb::get_latency_frames() const {
    return impl_->predelay_size;
}

bool AudioEffectReverb::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectReverb::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

float AudioEffectReverb::get_mix() const {
    return impl_->mix;
}

void AudioEffectReverb::set_mix(float mix) {
    impl_->mix = std::max(0.0f, std::min(1.0f, mix));
}

void AudioEffectReverb::set_room_size(float size) {
    impl_->room_size = std::max(0.0f, std::min(1.0f, size));
    impl_->update_parameters();
}

float AudioEffectReverb::get_room_size() const {
    return impl_->room_size;
}

void AudioEffectReverb::set_damping(float damping) {
    impl_->damping = std::max(0.0f, std::min(1.0f, damping));
    impl_->update_parameters();
}

float AudioEffectReverb::get_damping() const {
    return impl_->damping;
}

void AudioEffectReverb::set_width(float width) {
    impl_->width = std::max(0.0f, std::min(1.0f, width));
    impl_->update_parameters();
}

float AudioEffectReverb::get_width() const {
    return impl_->width;
}

void AudioEffectReverb::set_pre_delay_ms(float ms) {
    impl_->pre_delay_ms = std::max(0.0f, std::min(500.0f, ms));
    impl_->update_predelay();
}

float AudioEffectReverb::get_pre_delay_ms() const {
    return impl_->pre_delay_ms;
}

void AudioEffectReverb::set_freeze(bool freeze) {
    impl_->freeze = freeze;
    impl_->update_parameters();
}

bool AudioEffectReverb::is_frozen() const {
    return impl_->freeze;
}

// ============================================================================
// AudioEffectChorus Implementation
// ============================================================================

struct AudioEffectChorus::Impl {
    static constexpr int MAX_VOICES = 4;
    static constexpr float MAX_DELAY_MS = 50.0f;

    int sample_rate = 48000;
    bool enabled = true;
    float mix = 0.5f;
    float rate_hz = 1.5f;
    float depth = 0.5f;
    float delay_ms = 7.0f;
    float feedback = 0.0f;
    int voices = 2;

    // Delay buffer
    std::vector<float> delay_buffer_l;
    std::vector<float> delay_buffer_r;
    int buffer_size = 0;
    int write_idx = 0;

    // LFO state for each voice
    struct VoiceLFO {
        float phase = 0.0f;
        float phase_offset = 0.0f;
    };
    VoiceLFO voice_lfo[MAX_VOICES];

    float phase_increment = 0.0f;
    float base_delay_samples = 0.0f;
    float depth_samples = 0.0f;

    void init_buffers() {
        buffer_size = static_cast<int>(MAX_DELAY_MS * 0.001f * sample_rate * 2);
        delay_buffer_l.resize(buffer_size, 0.0f);
        delay_buffer_r.resize(buffer_size, 0.0f);
        write_idx = 0;

        // Initialize voice phase offsets
        for (int v = 0; v < MAX_VOICES; ++v) {
            voice_lfo[v].phase = 0.0f;
            voice_lfo[v].phase_offset = static_cast<float>(v) / MAX_VOICES;
        }

        update_parameters();
    }

    void update_parameters() {
        phase_increment = rate_hz / sample_rate;
        base_delay_samples = delay_ms * 0.001f * sample_rate;
        depth_samples = depth * base_delay_samples * 0.5f;
    }

    float read_delay(const std::vector<float>& buffer, float delay_samples) {
        float read_pos = write_idx - delay_samples;
        while (read_pos < 0) read_pos += buffer_size;

        int idx0 = static_cast<int>(read_pos);
        int idx1 = (idx0 + 1) % buffer_size;
        float frac = read_pos - idx0;

        // Linear interpolation
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }
};

AudioEffectChorus* AudioEffectChorus::create(int sample_rate) {
    AudioEffectChorus* effect = new AudioEffectChorus();
    effect->impl_ = new AudioEffectChorus::Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->init_buffers();
    return effect;
}

void AudioEffectChorus::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectChorus::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    const float two_pi = 6.28318530718f;

    for (int i = 0; i < frame_count; ++i) {
        float in_l = samples[i * channels];
        float in_r = (channels >= 2) ? samples[i * channels + 1] : in_l;

        // Write to delay buffer (with feedback)
        impl_->delay_buffer_l[impl_->write_idx] = in_l;
        impl_->delay_buffer_r[impl_->write_idx] = in_r;

        // Accumulate chorus output from all voices
        float chorus_l = 0.0f;
        float chorus_r = 0.0f;

        for (int v = 0; v < impl_->voices; ++v) {
            // Compute LFO value for this voice
            float phase = impl_->voice_lfo[v].phase + impl_->voice_lfo[v].phase_offset;
            if (phase >= 1.0f) phase -= 1.0f;
            float lfo = std::sin(phase * two_pi);

            // Compute delay for this voice
            float delay = impl_->base_delay_samples + lfo * impl_->depth_samples;
            delay = std::max(1.0f, delay);

            // Read from delay line
            chorus_l += impl_->read_delay(impl_->delay_buffer_l, delay);
            chorus_r += impl_->read_delay(impl_->delay_buffer_r, delay);
        }

        // Normalize by number of voices
        float voice_scale = 1.0f / impl_->voices;
        chorus_l *= voice_scale;
        chorus_r *= voice_scale;

        // Apply feedback
        impl_->delay_buffer_l[impl_->write_idx] += chorus_l * impl_->feedback;
        impl_->delay_buffer_r[impl_->write_idx] += chorus_r * impl_->feedback;

        // Mix dry and wet
        samples[i * channels] = in_l * (1.0f - impl_->mix) + chorus_l * impl_->mix;
        if (channels >= 2) {
            samples[i * channels + 1] = in_r * (1.0f - impl_->mix) + chorus_r * impl_->mix;
        }

        // Advance write position
        if (++impl_->write_idx >= impl_->buffer_size) impl_->write_idx = 0;

        // Advance LFO phase (same for all voices)
        impl_->voice_lfo[0].phase += impl_->phase_increment;
        if (impl_->voice_lfo[0].phase >= 1.0f) impl_->voice_lfo[0].phase -= 1.0f;
    }
}

void AudioEffectChorus::reset() {
    std::fill(impl_->delay_buffer_l.begin(), impl_->delay_buffer_l.end(), 0.0f);
    std::fill(impl_->delay_buffer_r.begin(), impl_->delay_buffer_r.end(), 0.0f);
    impl_->write_idx = 0;
    for (int v = 0; v < AudioEffectChorus::Impl::MAX_VOICES; ++v) {
        impl_->voice_lfo[v].phase = 0.0f;
    }
}

int AudioEffectChorus::get_latency_frames() const {
    return static_cast<int>(impl_->base_delay_samples);
}

bool AudioEffectChorus::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectChorus::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

float AudioEffectChorus::get_mix() const {
    return impl_->mix;
}

void AudioEffectChorus::set_mix(float mix) {
    impl_->mix = std::max(0.0f, std::min(1.0f, mix));
}

void AudioEffectChorus::set_rate_hz(float hz) {
    impl_->rate_hz = std::max(0.01f, std::min(10.0f, hz));
    impl_->update_parameters();
}

float AudioEffectChorus::get_rate_hz() const {
    return impl_->rate_hz;
}

void AudioEffectChorus::set_depth(float depth) {
    impl_->depth = std::max(0.0f, std::min(1.0f, depth));
    impl_->update_parameters();
}

float AudioEffectChorus::get_depth() const {
    return impl_->depth;
}

void AudioEffectChorus::set_delay_ms(float ms) {
    impl_->delay_ms = std::max(1.0f, std::min(AudioEffectChorus::Impl::MAX_DELAY_MS, ms));
    impl_->update_parameters();
}

float AudioEffectChorus::get_delay_ms() const {
    return impl_->delay_ms;
}

void AudioEffectChorus::set_feedback(float feedback) {
    impl_->feedback = std::max(-0.9f, std::min(0.9f, feedback));
}

float AudioEffectChorus::get_feedback() const {
    return impl_->feedback;
}

void AudioEffectChorus::set_voices(int voices) {
    impl_->voices = std::max(1, std::min(AudioEffectChorus::Impl::MAX_VOICES, voices));
}

int AudioEffectChorus::get_voices() const {
    return impl_->voices;
}

// ============================================================================
// AudioEffectDistortion Implementation
// ============================================================================

struct AudioEffectDistortion::Impl {
    int sample_rate = 48000;
    bool enabled = true;
    float mix = 1.0f;
    AudioEffectDistortion::Mode mode = AudioEffectDistortion::Mode::SoftClip;
    float drive = 2.0f;
    float output_level = 0.5f;
    float tone = 0.5f;
    int bit_depth = 8;
    int downsample = 1;

    // Tone filter state (simple one-pole)
    float filter_state_l = 0.0f;
    float filter_state_r = 0.0f;
    float filter_coeff = 0.0f;

    // Sample hold for bitcrush
    float hold_l = 0.0f;
    float hold_r = 0.0f;
    int hold_counter = 0;

    void update_filter() {
        // tone: 0 = dark (heavy filtering), 1 = bright (less filtering)
        filter_coeff = 0.1f + tone * 0.9f;
    }

    float apply_distortion(float sample) {
        float in = sample * drive;
        float out = 0.0f;

        switch (mode) {
            case AudioEffectDistortion::Mode::SoftClip:
                // Soft clipping using tanh-like curve
                if (in > 1.0f) {
                    out = 2.0f / 3.0f;
                } else if (in < -1.0f) {
                    out = -2.0f / 3.0f;
                } else {
                    out = in - (in * in * in) / 3.0f;
                }
                break;

            case AudioEffectDistortion::Mode::HardClip:
                out = std::max(-1.0f, std::min(1.0f, in));
                break;

            case AudioEffectDistortion::Mode::Tanh:
                out = std::tanh(in);
                break;

            case AudioEffectDistortion::Mode::Foldback:
                // Foldback distortion
                while (in > 1.0f || in < -1.0f) {
                    if (in > 1.0f) {
                        in = 2.0f - in;
                    } else if (in < -1.0f) {
                        in = -2.0f - in;
                    }
                }
                out = in;
                break;

            case AudioEffectDistortion::Mode::Bitcrush:
                // Quantization handled separately
                out = in;
                break;
        }

        return out * output_level;
    }

    float apply_bitcrush(float sample) {
        // Bit reduction
        float levels = std::pow(2.0f, static_cast<float>(bit_depth)) - 1.0f;
        float quantized = std::round(sample * levels) / levels;
        return quantized * output_level;
    }
};

AudioEffectDistortion* AudioEffectDistortion::create(int sample_rate) {
    AudioEffectDistortion* effect = new AudioEffectDistortion();
    effect->impl_ = new AudioEffectDistortion::Impl();
    effect->impl_->sample_rate = sample_rate;
    effect->impl_->update_filter();
    return effect;
}

void AudioEffectDistortion::destroy() {
    delete impl_;
    delete this;
}

void AudioEffectDistortion::process(float* samples, int frame_count, int channels) {
    if (!impl_->enabled) return;

    for (int i = 0; i < frame_count; ++i) {
        float dry_l = samples[i * channels];
        float dry_r = (channels >= 2) ? samples[i * channels + 1] : dry_l;

        float wet_l, wet_r;

        if (impl_->mode == AudioEffectDistortion::Mode::Bitcrush) {
            // Sample rate reduction
            if (impl_->hold_counter <= 0) {
                impl_->hold_l = impl_->apply_bitcrush(dry_l * impl_->drive);
                impl_->hold_r = impl_->apply_bitcrush(dry_r * impl_->drive);
                impl_->hold_counter = impl_->downsample;
            }
            impl_->hold_counter--;
            wet_l = impl_->hold_l;
            wet_r = impl_->hold_r;
        } else {
            wet_l = impl_->apply_distortion(dry_l);
            wet_r = impl_->apply_distortion(dry_r);
        }

        // Apply tone filter (one-pole low-pass)
        impl_->filter_state_l = impl_->filter_state_l + impl_->filter_coeff * (wet_l - impl_->filter_state_l);
        impl_->filter_state_r = impl_->filter_state_r + impl_->filter_coeff * (wet_r - impl_->filter_state_r);

        wet_l = impl_->filter_state_l;
        wet_r = impl_->filter_state_r;

        // Mix
        samples[i * channels] = dry_l * (1.0f - impl_->mix) + wet_l * impl_->mix;
        if (channels >= 2) {
            samples[i * channels + 1] = dry_r * (1.0f - impl_->mix) + wet_r * impl_->mix;
        }
    }
}

void AudioEffectDistortion::reset() {
    impl_->filter_state_l = 0.0f;
    impl_->filter_state_r = 0.0f;
    impl_->hold_l = 0.0f;
    impl_->hold_r = 0.0f;
    impl_->hold_counter = 0;
}

bool AudioEffectDistortion::is_enabled() const {
    return impl_->enabled;
}

void AudioEffectDistortion::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

float AudioEffectDistortion::get_mix() const {
    return impl_->mix;
}

void AudioEffectDistortion::set_mix(float mix) {
    impl_->mix = std::max(0.0f, std::min(1.0f, mix));
}

void AudioEffectDistortion::set_mode(Mode mode) {
    impl_->mode = mode;
}

AudioEffectDistortion::Mode AudioEffectDistortion::get_mode() const {
    return impl_->mode;
}

void AudioEffectDistortion::set_drive(float drive) {
    impl_->drive = std::max(1.0f, std::min(100.0f, drive));
}

float AudioEffectDistortion::get_drive() const {
    return impl_->drive;
}

void AudioEffectDistortion::set_output_level(float level) {
    impl_->output_level = std::max(0.0f, std::min(1.0f, level));
}

float AudioEffectDistortion::get_output_level() const {
    return impl_->output_level;
}

void AudioEffectDistortion::set_tone(float tone) {
    impl_->tone = std::max(0.0f, std::min(1.0f, tone));
    impl_->update_filter();
}

float AudioEffectDistortion::get_tone() const {
    return impl_->tone;
}

void AudioEffectDistortion::set_bit_depth(int bits) {
    impl_->bit_depth = std::max(1, std::min(16, bits));
}

int AudioEffectDistortion::get_bit_depth() const {
    return impl_->bit_depth;
}

void AudioEffectDistortion::set_downsample(int factor) {
    impl_->downsample = std::max(1, std::min(64, factor));
}

int AudioEffectDistortion::get_downsample() const {
    return impl_->downsample;
}

} // namespace audio
} // namespace window
