/*
 * audio_pulseaudio.cpp - Linux PulseAudio Backend
 *
 * Implements audio using PulseAudio (libpulse).
 */

#if defined(WINDOW_PLATFORM_X11) || defined(WINDOW_PLATFORM_WAYLAND)
#if defined(WINDOW_SUPPORT_PULSEAUDIO)

#include "audio.hpp"
#include <pulse/pulseaudio.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <thread>
#include <condition_variable>

namespace window {
namespace audio {

// ============================================================================
// Global State
// ============================================================================

static struct {
    bool initialized = false;
    AudioBackend backend = AudioBackend::PulseAudio;
    pa_threaded_mainloop* mainloop = nullptr;
    pa_context* context = nullptr;
    std::mutex mutex;
} g_audio_state;

// ============================================================================
// Helper Functions
// ============================================================================

static pa_sample_format_t to_pa_format(SampleFormat format) {
    switch (format) {
        case SampleFormat::Int16:   return PA_SAMPLE_S16LE;
        case SampleFormat::Int24:   return PA_SAMPLE_S24LE;
        case SampleFormat::Int32:   return PA_SAMPLE_S32LE;
        case SampleFormat::Float32: return PA_SAMPLE_FLOAT32LE;
        default: return PA_SAMPLE_INVALID;
    }
}

static SampleFormat from_pa_format(pa_sample_format_t format) {
    switch (format) {
        case PA_SAMPLE_S16LE:
        case PA_SAMPLE_S16BE:    return SampleFormat::Int16;
        case PA_SAMPLE_S24LE:
        case PA_SAMPLE_S24BE:    return SampleFormat::Int24;
        case PA_SAMPLE_S32LE:
        case PA_SAMPLE_S32BE:    return SampleFormat::Int32;
        case PA_SAMPLE_FLOAT32LE:
        case PA_SAMPLE_FLOAT32BE: return SampleFormat::Float32;
        default: return SampleFormat::Unknown;
    }
}

// ============================================================================
// AudioStream::Impl
// ============================================================================

struct AudioStream::Impl {
    pa_stream* stream = nullptr;
    AudioStreamConfig config;
    AudioFormat actual_format;
    int actual_buffer_frames = 0;

    IAudioCallback* callback = nullptr;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> running{false};
    std::atomic<AudioStreamState> state{AudioStreamState::Stopped};

    uint64_t frame_position = 0;
    double stream_start_time = 0.0;

    std::vector<uint8_t> temp_buffer;

    static void stream_write_callback(pa_stream* s, size_t nbytes, void* userdata);
    static void stream_state_callback(pa_stream* s, void* userdata);
};

void AudioStream::Impl::stream_write_callback(pa_stream* s, size_t nbytes, void* userdata) {
    AudioStream::Impl* impl = static_cast<AudioStream::Impl*>(userdata);
    if (!impl) return;

    void* data;
    if (pa_stream_begin_write(s, &data, &nbytes) < 0) return;

    int frame_count = static_cast<int>(nbytes / impl->actual_format.bytes_per_frame());

    // Prepare audio buffer
    AudioBuffer audio_buf;
    audio_buf.data = data;
    audio_buf.frame_count = frame_count;
    audio_buf.channel_count = impl->actual_format.channels;
    audio_buf.format = impl->actual_format.sample_format;

    // Clear buffer
    memset(data, 0, nbytes);

    // Get stream time
    AudioStreamTime stream_time;
    stream_time.frame_position = impl->frame_position;

    // Call user callback
    if (impl->callback) {
        impl->callback->on_audio_playback(audio_buf, stream_time);
    }

    // Apply volume
    float vol = impl->volume.load();
    if (vol < 0.999f) {
        apply_volume(data, impl->actual_format.sample_format,
                     frame_count * impl->actual_format.channels, vol);
    }

    pa_stream_write(s, data, nbytes, nullptr, 0, PA_SEEK_RELATIVE);
    impl->frame_position += frame_count;
}

void AudioStream::Impl::stream_state_callback(pa_stream* s, void* userdata) {
    AudioStream::Impl* impl = static_cast<AudioStream::Impl*>(userdata);
    if (!impl) return;

    pa_stream_state_t state = pa_stream_get_state(s);
    switch (state) {
        case PA_STREAM_READY:
            impl->state = AudioStreamState::Running;
            break;
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            impl->state = AudioStreamState::Error;
            impl->running = false;
            break;
        default:
            break;
    }
}

// ============================================================================
// AudioStream Implementation
// ============================================================================

AudioStream* AudioStream::create(const AudioStreamConfig& config, AudioResult* out_result) {
    if (!g_audio_state.initialized) {
        if (out_result) *out_result = AudioResult::ErrorNotInitialized;
        return nullptr;
    }

    AudioStream* stream = new AudioStream();
    stream->impl_ = new AudioStream::Impl();
    stream->impl_->config = config;

    // Set up sample spec
    pa_sample_spec sample_spec;
    sample_spec.format = to_pa_format(config.format.sample_format);
    sample_spec.rate = static_cast<uint32_t>(config.format.sample_rate);
    sample_spec.channels = static_cast<uint8_t>(config.format.channels);

    if (sample_spec.format == PA_SAMPLE_INVALID) {
        sample_spec.format = PA_SAMPLE_FLOAT32LE;
    }

    // Create stream
    pa_threaded_mainloop_lock(g_audio_state.mainloop);

    stream->impl_->stream = pa_stream_new(g_audio_state.context, "Audio Stream",
                                          &sample_spec, nullptr);
    if (!stream->impl_->stream) {
        pa_threaded_mainloop_unlock(g_audio_state.mainloop);
        if (out_result) *out_result = AudioResult::ErrorUnknown;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Set callbacks
    pa_stream_set_write_callback(stream->impl_->stream,
                                 &AudioStream::Impl::stream_write_callback,
                                 stream->impl_);
    pa_stream_set_state_callback(stream->impl_->stream,
                                 &AudioStream::Impl::stream_state_callback,
                                 stream->impl_);

    // Calculate buffer attributes
    int buffer_frames = config.buffer_frames > 0 ? config.buffer_frames : 512;
    size_t buffer_bytes = buffer_frames * pa_frame_size(&sample_spec);

    pa_buffer_attr buffer_attr;
    buffer_attr.maxlength = static_cast<uint32_t>(buffer_bytes * 4);
    buffer_attr.tlength = static_cast<uint32_t>(buffer_bytes);
    buffer_attr.prebuf = static_cast<uint32_t>(buffer_bytes);
    buffer_attr.minreq = static_cast<uint32_t>(buffer_bytes / 4);
    buffer_attr.fragsize = static_cast<uint32_t>(buffer_bytes);

    // Connect stream
    pa_stream_flags_t flags = static_cast<pa_stream_flags_t>(
        PA_STREAM_INTERPOLATE_TIMING |
        PA_STREAM_AUTO_TIMING_UPDATE |
        PA_STREAM_ADJUST_LATENCY
    );

    int ret;
    if (config.mode == AudioStreamMode::Capture) {
        ret = pa_stream_connect_record(stream->impl_->stream, nullptr,
                                       &buffer_attr, flags);
    } else {
        ret = pa_stream_connect_playback(stream->impl_->stream, nullptr,
                                         &buffer_attr, flags, nullptr, nullptr);
    }

    if (ret < 0) {
        pa_stream_unref(stream->impl_->stream);
        pa_threaded_mainloop_unlock(g_audio_state.mainloop);
        if (out_result) *out_result = AudioResult::ErrorUnknown;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    pa_threaded_mainloop_unlock(g_audio_state.mainloop);

    // Wait for stream to be ready
    while (pa_stream_get_state(stream->impl_->stream) == PA_STREAM_CREATING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (pa_stream_get_state(stream->impl_->stream) != PA_STREAM_READY) {
        pa_threaded_mainloop_lock(g_audio_state.mainloop);
        pa_stream_disconnect(stream->impl_->stream);
        pa_stream_unref(stream->impl_->stream);
        pa_threaded_mainloop_unlock(g_audio_state.mainloop);
        if (out_result) *out_result = AudioResult::ErrorUnknown;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    stream->impl_->actual_format.sample_format = from_pa_format(sample_spec.format);
    stream->impl_->actual_format.sample_rate = static_cast<int>(sample_spec.rate);
    stream->impl_->actual_format.channels = static_cast<int>(sample_spec.channels);
    stream->impl_->actual_format.layout = layout_from_channel_count(sample_spec.channels);
    stream->impl_->actual_buffer_frames = buffer_frames;

    stream->impl_->running = true;

    if (out_result) *out_result = AudioResult::Success;
    return stream;
}

void AudioStream::destroy() {
    if (!impl_) return;

    stop();

    if (impl_->stream) {
        pa_threaded_mainloop_lock(g_audio_state.mainloop);
        pa_stream_disconnect(impl_->stream);
        pa_stream_unref(impl_->stream);
        pa_threaded_mainloop_unlock(g_audio_state.mainloop);
    }

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioResult AudioStream::start() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (impl_->running) return AudioResult::ErrorStreamAlreadyRunning;

    pa_threaded_mainloop_lock(g_audio_state.mainloop);
    pa_stream_cork(impl_->stream, 0, nullptr, nullptr);
    pa_threaded_mainloop_unlock(g_audio_state.mainloop);

    impl_->running = true;
    impl_->state = AudioStreamState::Running;
    return AudioResult::Success;
}

AudioResult AudioStream::stop() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (!impl_->running) return AudioResult::Success;

    pa_threaded_mainloop_lock(g_audio_state.mainloop);
    pa_stream_cork(impl_->stream, 1, nullptr, nullptr);
    pa_threaded_mainloop_unlock(g_audio_state.mainloop);

    impl_->running = false;
    impl_->state = AudioStreamState::Stopped;
    return AudioResult::Success;
}

AudioResult AudioStream::pause() {
    return stop();
}

AudioResult AudioStream::resume() {
    return start();
}

bool AudioStream::is_running() const {
    return impl_ && impl_->running;
}

AudioStreamState AudioStream::get_state() const {
    return impl_ ? impl_->state.load() : AudioStreamState::Stopped;
}

void AudioStream::set_callback(IAudioCallback* callback) {
    if (impl_) impl_->callback = callback;
}

void AudioStream::set_volume(float volume) {
    if (impl_) impl_->volume = std::max(0.0f, std::min(1.0f, volume));
}

float AudioStream::get_volume() const {
    return impl_ ? impl_->volume.load() : 0.0f;
}

const AudioFormat& AudioStream::get_format() const {
    static AudioFormat empty;
    return impl_ ? impl_->actual_format : empty;
}

int AudioStream::get_buffer_frames() const {
    return impl_ ? impl_->actual_buffer_frames : 0;
}

double AudioStream::get_output_latency() const {
    if (!impl_ || !impl_->stream) return 0.0;

    pa_usec_t latency = 0;
    int negative = 0;
    pa_threaded_mainloop_lock(g_audio_state.mainloop);
    if (pa_stream_get_latency(impl_->stream, &latency, &negative) >= 0) {
        pa_threaded_mainloop_unlock(g_audio_state.mainloop);
        return static_cast<double>(latency) / 1000000.0;
    }
    pa_threaded_mainloop_unlock(g_audio_state.mainloop);
    return 0.0;
}

double AudioStream::get_input_latency() const {
    return get_output_latency();
}

AudioStreamTime AudioStream::get_stream_time() const {
    AudioStreamTime time;
    if (impl_) {
        time.frame_position = impl_->frame_position;
        time.output_latency = get_output_latency();
    }
    return time;
}

// ============================================================================
// AudioClip Implementation
// ============================================================================

struct AudioClip::Impl {
    AudioFormat format;
    int frame_count = 0;
    std::vector<uint8_t> data;
};

AudioClip* AudioClip::load(const char* filepath, AudioResult* out_result) {
    // Simple WAV loader (same as WASAPI version)
    if (!filepath) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return nullptr;
    }

    FILE* file = fopen(filepath, "rb");
    if (!file) {
        if (out_result) *out_result = AudioResult::ErrorFileNotFound;
        return nullptr;
    }

    // Read RIFF header
    char riff[4];
    uint32_t file_size;
    char wave[4];
    fread(riff, 1, 4, file);
    fread(&file_size, 4, 1, file);
    fread(wave, 1, 4, file);

    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(wave, "WAVE", 4) != 0) {
        fclose(file);
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return nullptr;
    }

    AudioFormat format;
    std::vector<uint8_t> audio_data;
    int frame_count = 0;

    while (!feof(file)) {
        char chunk_id[4];
        uint32_t chunk_size;
        if (fread(chunk_id, 1, 4, file) != 4) break;
        if (fread(&chunk_size, 4, 1, file) != 1) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint16_t audio_format, channels;
            uint32_t sample_rate, byte_rate;
            uint16_t block_align, bits_per_sample;

            fread(&audio_format, 2, 1, file);
            fread(&channels, 2, 1, file);
            fread(&sample_rate, 4, 1, file);
            fread(&byte_rate, 4, 1, file);
            fread(&block_align, 2, 1, file);
            fread(&bits_per_sample, 2, 1, file);

            format.sample_rate = static_cast<int>(sample_rate);
            format.channels = static_cast<int>(channels);
            format.layout = layout_from_channel_count(format.channels);

            if (audio_format == 1) { // PCM
                switch (bits_per_sample) {
                    case 16: format.sample_format = SampleFormat::Int16; break;
                    case 24: format.sample_format = SampleFormat::Int24; break;
                    case 32: format.sample_format = SampleFormat::Int32; break;
                    default: format.sample_format = SampleFormat::Unknown; break;
                }
            } else if (audio_format == 3) { // IEEE Float
                format.sample_format = SampleFormat::Float32;
            }

            if (chunk_size > 16) {
                fseek(file, chunk_size - 16, SEEK_CUR);
            }
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            audio_data.resize(chunk_size);
            fread(audio_data.data(), 1, chunk_size, file);
            frame_count = static_cast<int>(chunk_size / format.bytes_per_frame());
        } else {
            fseek(file, chunk_size, SEEK_CUR);
        }
    }

    fclose(file);

    if (audio_data.empty() || !format.is_valid()) {
        if (out_result) *out_result = AudioResult::ErrorFileFormat;
        return nullptr;
    }

    AudioClip* clip = new AudioClip();
    clip->impl_ = new AudioClip::Impl();
    clip->impl_->format = format;
    clip->impl_->frame_count = frame_count;
    clip->impl_->data = std::move(audio_data);

    if (out_result) *out_result = AudioResult::Success;
    return clip;
}

AudioClip* AudioClip::create(const AudioFormat& format, int frame_count, AudioResult* out_result) {
    if (!format.is_valid() || frame_count <= 0) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return nullptr;
    }

    AudioClip* clip = new AudioClip();
    clip->impl_ = new AudioClip::Impl();
    clip->impl_->format = format;
    clip->impl_->frame_count = frame_count;
    clip->impl_->data.resize(static_cast<size_t>(format.bytes_per_frame()) * frame_count);

    if (out_result) *out_result = AudioResult::Success;
    return clip;
}

void AudioClip::destroy() {
    if (!impl_) return;
    delete impl_;
    impl_ = nullptr;
    delete this;
}

const AudioFormat& AudioClip::get_format() const {
    static AudioFormat empty;
    return impl_ ? impl_->format : empty;
}

int AudioClip::get_frame_count() const {
    return impl_ ? impl_->frame_count : 0;
}

double AudioClip::get_duration() const {
    if (!impl_ || impl_->format.sample_rate <= 0) return 0.0;
    return static_cast<double>(impl_->frame_count) / impl_->format.sample_rate;
}

void* AudioClip::get_data() {
    return impl_ ? impl_->data.data() : nullptr;
}

const void* AudioClip::get_data() const {
    return impl_ ? impl_->data.data() : nullptr;
}

size_t AudioClip::get_data_size() const {
    return impl_ ? impl_->data.size() : 0;
}

// ============================================================================
// AudioPlayer Implementation (Stub)
// ============================================================================

struct AudioPlayer::Impl {
    std::atomic<float> master_volume{1.0f};
};

AudioPlayer* AudioPlayer::create(int device_index, AudioResult* out_result) {
    (void)device_index;
    if (!g_audio_state.initialized) {
        if (out_result) *out_result = AudioResult::ErrorNotInitialized;
        return nullptr;
    }

    AudioPlayer* player = new AudioPlayer();
    player->impl_ = new AudioPlayer::Impl();

    if (out_result) *out_result = AudioResult::Success;
    return player;
}

void AudioPlayer::destroy() {
    if (!impl_) return;
    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioPlayHandle AudioPlayer::play(AudioClip* clip, const AudioPlayOptions& options) {
    (void)clip; (void)options;
    return INVALID_AUDIO_PLAY_HANDLE;
}

void AudioPlayer::stop(AudioPlayHandle handle) { (void)handle; }
void AudioPlayer::stop_all() {}
bool AudioPlayer::is_playing(AudioPlayHandle handle) const { (void)handle; return false; }
void AudioPlayer::set_volume(AudioPlayHandle handle, float volume) { (void)handle; (void)volume; }
void AudioPlayer::set_pitch(AudioPlayHandle handle, float pitch) { (void)handle; (void)pitch; }
void AudioPlayer::set_pan(AudioPlayHandle handle, float pan) { (void)handle; (void)pan; }
void AudioPlayer::pause(AudioPlayHandle handle) { (void)handle; }
void AudioPlayer::resume(AudioPlayHandle handle) { (void)handle; }
void AudioPlayer::update() {}
int AudioPlayer::get_playing_count() const { return 0; }
void AudioPlayer::set_master_volume(float volume) { if (impl_) impl_->master_volume = volume; }
float AudioPlayer::get_master_volume() const { return impl_ ? impl_->master_volume.load() : 0.0f; }

// ============================================================================
// AudioManager Implementation
// ============================================================================

static void context_state_callback(pa_context* c, void* userdata) {
    (void)userdata;
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY || state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        pa_threaded_mainloop_signal(g_audio_state.mainloop, 0);
    }
}

AudioResult AudioManager::initialize(AudioBackend backend) {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (g_audio_state.initialized) {
        return AudioResult::ErrorAlreadyInitialized;
    }

    if (backend != AudioBackend::Auto && backend != AudioBackend::PulseAudio) {
        return AudioResult::ErrorBackendNotSupported;
    }

    // Create mainloop
    g_audio_state.mainloop = pa_threaded_mainloop_new();
    if (!g_audio_state.mainloop) {
        return AudioResult::ErrorUnknown;
    }

    // Create context
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(g_audio_state.mainloop);
    g_audio_state.context = pa_context_new(api, "Window Audio");
    if (!g_audio_state.context) {
        pa_threaded_mainloop_free(g_audio_state.mainloop);
        g_audio_state.mainloop = nullptr;
        return AudioResult::ErrorUnknown;
    }

    pa_context_set_state_callback(g_audio_state.context, context_state_callback, nullptr);

    // Connect
    if (pa_context_connect(g_audio_state.context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_context_unref(g_audio_state.context);
        pa_threaded_mainloop_free(g_audio_state.mainloop);
        g_audio_state.context = nullptr;
        g_audio_state.mainloop = nullptr;
        return AudioResult::ErrorUnknown;
    }

    // Start mainloop
    if (pa_threaded_mainloop_start(g_audio_state.mainloop) < 0) {
        pa_context_disconnect(g_audio_state.context);
        pa_context_unref(g_audio_state.context);
        pa_threaded_mainloop_free(g_audio_state.mainloop);
        g_audio_state.context = nullptr;
        g_audio_state.mainloop = nullptr;
        return AudioResult::ErrorUnknown;
    }

    // Wait for connection
    pa_threaded_mainloop_lock(g_audio_state.mainloop);
    while (pa_context_get_state(g_audio_state.context) == PA_CONTEXT_CONNECTING) {
        pa_threaded_mainloop_wait(g_audio_state.mainloop);
    }
    pa_context_state_t state = pa_context_get_state(g_audio_state.context);
    pa_threaded_mainloop_unlock(g_audio_state.mainloop);

    if (state != PA_CONTEXT_READY) {
        pa_threaded_mainloop_stop(g_audio_state.mainloop);
        pa_context_disconnect(g_audio_state.context);
        pa_context_unref(g_audio_state.context);
        pa_threaded_mainloop_free(g_audio_state.mainloop);
        g_audio_state.context = nullptr;
        g_audio_state.mainloop = nullptr;
        return AudioResult::ErrorUnknown;
    }

    g_audio_state.backend = AudioBackend::PulseAudio;
    g_audio_state.initialized = true;

    return AudioResult::Success;
}

void AudioManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (!g_audio_state.initialized) return;

    if (g_audio_state.mainloop) {
        pa_threaded_mainloop_stop(g_audio_state.mainloop);
    }

    if (g_audio_state.context) {
        pa_context_disconnect(g_audio_state.context);
        pa_context_unref(g_audio_state.context);
        g_audio_state.context = nullptr;
    }

    if (g_audio_state.mainloop) {
        pa_threaded_mainloop_free(g_audio_state.mainloop);
        g_audio_state.mainloop = nullptr;
    }

    g_audio_state.initialized = false;
}

bool AudioManager::is_initialized() {
    return g_audio_state.initialized;
}

AudioBackend AudioManager::get_backend() {
    return g_audio_state.backend;
}

const char* AudioManager::get_backend_name() {
    return "PulseAudio";
}

bool AudioManager::is_backend_supported(AudioBackend backend) {
    return backend == AudioBackend::Auto || backend == AudioBackend::PulseAudio;
}

AudioBackend AudioManager::get_default_backend() {
    return AudioBackend::PulseAudio;
}

int AudioManager::enumerate_devices(AudioDeviceType type, AudioDeviceEnumeration* out) {
    if (!out) return 0;
    out->count = 0;

    // For simplicity, just report default device
    AudioDeviceInfo& info = out->devices[0];
    strncpy(info.name, "Default PulseAudio Device", sizeof(info.name) - 1);
    info.type = type;
    info.is_default = true;
    info.min_sample_rate = 8000;
    info.max_sample_rate = 192000;
    info.min_channels = 1;
    info.max_channels = 8;
    out->count = 1;

    return out->count;
}

bool AudioManager::get_default_device(AudioDeviceType type, AudioDeviceInfo* out) {
    AudioDeviceEnumeration enumeration;
    enumerate_devices(type, &enumeration);
    if (enumeration.count > 0 && out) {
        *out = enumeration.devices[0];
        return true;
    }
    return false;
}

bool AudioManager::get_device_info(int device_index, AudioDeviceType type, AudioDeviceInfo* out) {
    if (device_index != 0) return false;
    return get_default_device(type, out);
}

bool AudioManager::is_format_supported(int device_index, const AudioFormat& format, AudioStreamMode mode) {
    (void)device_index; (void)mode;
    return format.is_valid() &&
           format.sample_rate >= 8000 && format.sample_rate <= 192000 &&
           format.channels >= 1 && format.channels <= 8;
}

AudioFormat AudioManager::get_preferred_format(int device_index, AudioDeviceType type) {
    (void)device_index; (void)type;
    return AudioFormat::default_format();
}

} // namespace audio
} // namespace window

#endif // WINDOW_SUPPORT_PULSEAUDIO
#endif // WINDOW_PLATFORM_X11 || WINDOW_PLATFORM_WAYLAND
