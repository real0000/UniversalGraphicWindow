/*
 * audio_wasapi.cpp - Windows WASAPI Audio Backend
 *
 * Implements low-latency audio using Windows Audio Session API.
 */

#if defined(WINDOW_PLATFORM_WIN32) && defined(WINDOW_SUPPORT_WASAPI)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "audio.hpp"
#include "../internal/utf8_util.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>

#pragma comment(lib, "avrt.lib")

namespace window {
namespace audio {

// ============================================================================
// Global State
// ============================================================================

static struct {
    bool initialized = false;
    AudioBackend backend = AudioBackend::WASAPI;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    std::mutex mutex;
} g_audio_state;

// ============================================================================
// Helper Functions
// ============================================================================

static WAVEFORMATEXTENSIBLE create_wave_format(const AudioFormat& format) {
    WAVEFORMATEXTENSIBLE wfx = {};
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = static_cast<WORD>(format.channels);
    wfx.Format.nSamplesPerSec = static_cast<DWORD>(format.sample_rate);

    int bits_per_sample = 0;
    switch (format.sample_format) {
        case SampleFormat::Int16:   bits_per_sample = 16; break;
        case SampleFormat::Int24:   bits_per_sample = 24; break;
        case SampleFormat::Int32:   bits_per_sample = 32; break;
        case SampleFormat::Float32: bits_per_sample = 32; break;
        default: break;
    }

    wfx.Format.wBitsPerSample = static_cast<WORD>(bits_per_sample);
    wfx.Format.nBlockAlign = static_cast<WORD>((format.channels * bits_per_sample) / 8);
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);

    wfx.Samples.wValidBitsPerSample = static_cast<WORD>(bits_per_sample);

    // Set channel mask
    switch (format.channels) {
        case 1: wfx.dwChannelMask = SPEAKER_FRONT_CENTER; break;
        case 2: wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT; break;
        case 6: wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
                                    SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY |
                                    SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT; break;
        case 8: wfx.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT |
                                    SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY |
                                    SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
                                    SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT; break;
        default: wfx.dwChannelMask = 0; break;
    }

    // Set subformat GUID
    if (format.sample_format == SampleFormat::Float32) {
        wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    } else {
        wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
    }

    return wfx;
}

static AudioFormat format_from_waveformat(const WAVEFORMATEX* wfx) {
    AudioFormat format;
    format.sample_rate = static_cast<int>(wfx->nSamplesPerSec);
    format.channels = static_cast<int>(wfx->nChannels);
    format.layout = layout_from_channel_count(format.channels);

    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE* wfxe = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (IsEqualGUID(wfxe->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            format.sample_format = SampleFormat::Float32;
        } else if (IsEqualGUID(wfxe->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            switch (wfxe->Samples.wValidBitsPerSample) {
                case 16: format.sample_format = SampleFormat::Int16; break;
                case 24: format.sample_format = SampleFormat::Int24; break;
                case 32: format.sample_format = SampleFormat::Int32; break;
                default: format.sample_format = SampleFormat::Unknown; break;
            }
        }
    } else if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        format.sample_format = SampleFormat::Float32;
    } else if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        switch (wfx->wBitsPerSample) {
            case 16: format.sample_format = SampleFormat::Int16; break;
            case 24: format.sample_format = SampleFormat::Int24; break;
            case 32: format.sample_format = SampleFormat::Int32; break;
            default: format.sample_format = SampleFormat::Unknown; break;
        }
    }

    return format;
}


// ============================================================================
// AudioStream::Impl
// ============================================================================

struct AudioStream::Impl {
    IAudioClient* audio_client = nullptr;
    IAudioRenderClient* render_client = nullptr;
    IAudioCaptureClient* capture_client = nullptr;
    IMMDevice* device = nullptr;
    HANDLE event_handle = nullptr;
    HANDLE thread_handle = nullptr;

    AudioStreamConfig config;
    AudioFormat actual_format;
    int actual_buffer_frames = 0;

    IAudioCallback* callback = nullptr;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<AudioStreamState> state{AudioStreamState::Stopped};

    uint64_t frame_position = 0;
    double stream_start_time = 0.0;

    std::thread audio_thread;
    DWORD task_index = 0;
    HANDLE avrt_handle = nullptr;

    void audio_thread_func();
    void process_playback();
    void process_capture();
};

void AudioStream::Impl::audio_thread_func() {
    // Set thread priority for audio
    avrt_handle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);

    running = true;
    state = AudioStreamState::Running;

    while (!stop_requested) {
        DWORD wait_result = WaitForSingleObject(event_handle, 2000);
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        if (stop_requested) break;

        if (config.mode == AudioStreamMode::Playback || config.mode == AudioStreamMode::Duplex) {
            process_playback();
        }
        if (config.mode == AudioStreamMode::Capture || config.mode == AudioStreamMode::Duplex) {
            process_capture();
        }
    }

    if (avrt_handle) {
        AvRevertMmThreadCharacteristics(avrt_handle);
        avrt_handle = nullptr;
    }

    running = false;
    state = AudioStreamState::Stopped;
}

void AudioStream::Impl::process_playback() {
    if (!render_client || !audio_client) return;

    UINT32 padding = 0;
    HRESULT hr = audio_client->GetCurrentPadding(&padding);
    if (FAILED(hr)) return;

    UINT32 frames_available = actual_buffer_frames - padding;
    if (frames_available == 0) return;

    BYTE* buffer = nullptr;
    hr = render_client->GetBuffer(frames_available, &buffer);
    if (FAILED(hr)) return;

    // Prepare audio buffer for callback
    AudioBuffer audio_buf;
    audio_buf.data = buffer;
    audio_buf.frame_count = static_cast<int>(frames_available);
    audio_buf.channel_count = actual_format.channels;
    audio_buf.format = actual_format.sample_format;

    // Clear buffer to silence
    audio_buf.clear();

    // Get stream time
    AudioStreamTime stream_time;
    LARGE_INTEGER counter, freq;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&freq);
    double current_time = static_cast<double>(counter.QuadPart) / freq.QuadPart;
    stream_time.stream_time = current_time - stream_start_time;
    stream_time.frame_position = frame_position;
    stream_time.output_latency = static_cast<double>(padding) / actual_format.sample_rate;

    // Call user callback
    bool continue_playback = true;
    if (callback) {
        continue_playback = callback->on_audio_playback(audio_buf, stream_time);
    }

    // Apply volume
    float vol = volume.load();
    if (vol < 0.999f) {
        apply_volume(buffer, actual_format.sample_format,
                     frames_available * actual_format.channels, vol);
    }

    DWORD flags = continue_playback ? 0 : AUDCLNT_BUFFERFLAGS_SILENT;
    render_client->ReleaseBuffer(frames_available, flags);

    frame_position += frames_available;

    if (!continue_playback) {
        stop_requested = true;
    }
}

void AudioStream::Impl::process_capture() {
    if (!capture_client) return;

    UINT32 packet_length = 0;
    HRESULT hr = capture_client->GetNextPacketSize(&packet_length);
    if (FAILED(hr)) return;

    while (packet_length > 0) {
        BYTE* buffer = nullptr;
        UINT32 frames_read = 0;
        DWORD flags = 0;

        hr = capture_client->GetBuffer(&buffer, &frames_read, &flags, nullptr, nullptr);
        if (FAILED(hr)) break;

        // Prepare audio buffer for callback
        AudioBuffer audio_buf;
        audio_buf.data = buffer;
        audio_buf.frame_count = static_cast<int>(frames_read);
        audio_buf.channel_count = actual_format.channels;
        audio_buf.format = actual_format.sample_format;

        // Get stream time
        AudioStreamTime stream_time;
        LARGE_INTEGER counter, freq;
        QueryPerformanceCounter(&counter);
        QueryPerformanceFrequency(&freq);
        double current_time = static_cast<double>(counter.QuadPart) / freq.QuadPart;
        stream_time.stream_time = current_time - stream_start_time;
        stream_time.frame_position = frame_position;

        // Call user callback
        bool continue_capture = true;
        if (callback) {
            continue_capture = callback->on_audio_capture(audio_buf, stream_time);
        }

        capture_client->ReleaseBuffer(frames_read);
        frame_position += frames_read;

        if (!continue_capture) {
            stop_requested = true;
            break;
        }

        hr = capture_client->GetNextPacketSize(&packet_length);
        if (FAILED(hr)) break;
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

    HRESULT hr;
    IMMDevice* device = nullptr;

    // Get device
    int device_index = (config.mode == AudioStreamMode::Capture)
                       ? config.input_device_index
                       : config.output_device_index;

    if (device_index < 0) {
        // Use default device
        EDataFlow flow = (config.mode == AudioStreamMode::Capture) ? eCapture : eRender;
        hr = g_audio_state.device_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    } else {
        // Get specific device by index
        IMMDeviceCollection* collection = nullptr;
        EDataFlow flow = (config.mode == AudioStreamMode::Capture) ? eCapture : eRender;
        hr = g_audio_state.device_enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
        if (SUCCEEDED(hr)) {
            hr = collection->Item(static_cast<UINT>(device_index), &device);
            collection->Release();
        }
    }

    if (FAILED(hr) || !device) {
        if (out_result) *out_result = AudioResult::ErrorDeviceNotFound;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    stream->impl_->device = device;

    // Create audio client
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&stream->impl_->audio_client));
    if (FAILED(hr)) {
        if (out_result) *out_result = AudioResult::ErrorDeviceNotFound;
        device->Release();
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Get mix format and determine actual format
    WAVEFORMATEX* mix_format = nullptr;
    hr = stream->impl_->audio_client->GetMixFormat(&mix_format);
    if (FAILED(hr)) {
        if (out_result) *out_result = AudioResult::ErrorFormatNotSupported;
        stream->impl_->audio_client->Release();
        device->Release();
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Try requested format first, fall back to mix format
    WAVEFORMATEXTENSIBLE wfx = create_wave_format(config.format);
    WAVEFORMATEX* closest_match = nullptr;
    hr = stream->impl_->audio_client->IsFormatSupported(
        config.exclusive_mode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
        reinterpret_cast<WAVEFORMATEX*>(&wfx), &closest_match);

    WAVEFORMATEX* use_format = nullptr;
    if (hr == S_OK) {
        use_format = reinterpret_cast<WAVEFORMATEX*>(&wfx);
    } else if (closest_match) {
        use_format = closest_match;
    } else {
        use_format = mix_format;
    }

    stream->impl_->actual_format = format_from_waveformat(use_format);

    // Calculate buffer size
    REFERENCE_TIME buffer_duration;
    if (config.buffer_frames > 0) {
        buffer_duration = static_cast<REFERENCE_TIME>(
            10000000.0 * config.buffer_frames / config.format.sample_rate);
    } else {
        buffer_duration = 200000; // 20ms default
    }

    // Create event for buffer notifications
    stream->impl_->event_handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    // Initialize audio client
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (config.mode == AudioStreamMode::Capture) {
        stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK; // For loopback capture if needed
    }

    hr = stream->impl_->audio_client->Initialize(
        config.exclusive_mode ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        buffer_duration,
        config.exclusive_mode ? buffer_duration : 0,
        use_format,
        nullptr);

    if (closest_match) CoTaskMemFree(closest_match);
    CoTaskMemFree(mix_format);

    if (FAILED(hr)) {
        if (out_result) *out_result = AudioResult::ErrorFormatNotSupported;
        CloseHandle(stream->impl_->event_handle);
        stream->impl_->audio_client->Release();
        device->Release();
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Set event handle
    hr = stream->impl_->audio_client->SetEventHandle(stream->impl_->event_handle);
    if (FAILED(hr)) {
        if (out_result) *out_result = AudioResult::ErrorUnknown;
        CloseHandle(stream->impl_->event_handle);
        stream->impl_->audio_client->Release();
        device->Release();
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Get actual buffer size
    UINT32 buffer_frames;
    stream->impl_->audio_client->GetBufferSize(&buffer_frames);
    stream->impl_->actual_buffer_frames = static_cast<int>(buffer_frames);

    // Get render or capture client
    if (config.mode == AudioStreamMode::Playback || config.mode == AudioStreamMode::Duplex) {
        hr = stream->impl_->audio_client->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&stream->impl_->render_client));
        if (FAILED(hr)) {
            if (out_result) *out_result = AudioResult::ErrorUnknown;
            CloseHandle(stream->impl_->event_handle);
            stream->impl_->audio_client->Release();
            device->Release();
            delete stream->impl_;
            delete stream;
            return nullptr;
        }
    }

    if (config.mode == AudioStreamMode::Capture || config.mode == AudioStreamMode::Duplex) {
        hr = stream->impl_->audio_client->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&stream->impl_->capture_client));
        if (FAILED(hr)) {
            if (out_result) *out_result = AudioResult::ErrorUnknown;
            if (stream->impl_->render_client) stream->impl_->render_client->Release();
            CloseHandle(stream->impl_->event_handle);
            stream->impl_->audio_client->Release();
            device->Release();
            delete stream->impl_;
            delete stream;
            return nullptr;
        }
    }

    if (out_result) *out_result = AudioResult::Success;
    return stream;
}

void AudioStream::destroy() {
    if (!impl_) return;

    stop();

    if (impl_->render_client) impl_->render_client->Release();
    if (impl_->capture_client) impl_->capture_client->Release();
    if (impl_->audio_client) impl_->audio_client->Release();
    if (impl_->device) impl_->device->Release();
    if (impl_->event_handle) CloseHandle(impl_->event_handle);

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioResult AudioStream::start() {
    if (!impl_ || !impl_->audio_client) return AudioResult::ErrorNotInitialized;
    if (impl_->running) return AudioResult::ErrorStreamAlreadyRunning;

    // Reset state
    impl_->stop_requested = false;
    impl_->frame_position = 0;

    // Get start time
    LARGE_INTEGER counter, freq;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&freq);
    impl_->stream_start_time = static_cast<double>(counter.QuadPart) / freq.QuadPart;

    // Start audio client
    HRESULT hr = impl_->audio_client->Start();
    if (FAILED(hr)) {
        return AudioResult::ErrorUnknown;
    }

    // Start audio thread
    impl_->audio_thread = std::thread(&AudioStream::Impl::audio_thread_func, impl_);

    return AudioResult::Success;
}

AudioResult AudioStream::stop() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (!impl_->running) return AudioResult::Success;

    impl_->stop_requested = true;

    // Signal the event to wake up the thread
    if (impl_->event_handle) {
        SetEvent(impl_->event_handle);
    }

    // Wait for thread to finish
    if (impl_->audio_thread.joinable()) {
        impl_->audio_thread.join();
    }

    // Stop audio client
    if (impl_->audio_client) {
        impl_->audio_client->Stop();
        impl_->audio_client->Reset();
    }

    return AudioResult::Success;
}

AudioResult AudioStream::pause() {
    if (!impl_ || !impl_->audio_client) return AudioResult::ErrorNotInitialized;
    if (!impl_->running) return AudioResult::ErrorStreamNotRunning;

    impl_->audio_client->Stop();
    impl_->state = AudioStreamState::Paused;
    return AudioResult::Success;
}

AudioResult AudioStream::resume() {
    if (!impl_ || !impl_->audio_client) return AudioResult::ErrorNotInitialized;
    if (impl_->state != AudioStreamState::Paused) return AudioResult::ErrorStreamNotRunning;

    impl_->audio_client->Start();
    impl_->state = AudioStreamState::Running;
    return AudioResult::Success;
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
    if (!impl_ || !impl_->audio_client) return 0.0;

    REFERENCE_TIME latency = 0;
    impl_->audio_client->GetStreamLatency(&latency);
    return static_cast<double>(latency) / 10000000.0;
}

double AudioStream::get_input_latency() const {
    return get_output_latency(); // Same for WASAPI
}

AudioStreamTime AudioStream::get_stream_time() const {
    AudioStreamTime time;
    if (!impl_) return time;

    LARGE_INTEGER counter, freq;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&freq);
    double current_time = static_cast<double>(counter.QuadPart) / freq.QuadPart;
    time.stream_time = current_time - impl_->stream_start_time;
    time.frame_position = impl_->frame_position;
    time.output_latency = get_output_latency();
    time.input_latency = get_input_latency();
    return time;
}

// ============================================================================
// AudioClip::Impl
// ============================================================================

struct AudioClip::Impl {
    AudioFormat format;
    int frame_count = 0;
    std::vector<uint8_t> data;
};

// Forward declaration of internal loader (defined in audio.cpp)
namespace internal {
    bool load_audio_file(const char* filepath, AudioFormat* out_format,
                         std::vector<uint8_t>* out_data, AudioResult* out_result);
}

AudioClip* AudioClip::load(const char* filepath, AudioResult* out_result) {
    if (!filepath) {
        if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
        return nullptr;
    }

    AudioFormat format;
    std::vector<uint8_t> audio_data;

    // Use the internal software loader which supports multiple formats
    if (!internal::load_audio_file(filepath, &format, &audio_data, out_result)) {
        return nullptr;
    }

    AudioClip* clip = new AudioClip();
    clip->impl_ = new AudioClip::Impl();
    clip->impl_->format = format;
    clip->impl_->frame_count = static_cast<int>(audio_data.size() / format.bytes_per_frame());
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
// AudioPlayer::Impl
// ============================================================================

struct PlayingSound {
    AudioClip* clip = nullptr;
    AudioPlayOptions options;
    int current_frame = 0;
    int loops_remaining = 0;
    bool paused = false;
    float current_volume = 1.0f;
    AudioPlayHandle handle = 0;
};

struct AudioPlayer::Impl : public IAudioCallback {
    AudioStream* stream = nullptr;
    std::vector<PlayingSound> playing_sounds;
    std::mutex mutex;
    std::atomic<float> master_volume{1.0f};
    AudioPlayHandle next_handle = 1;
    AudioFormat format;

    bool on_audio_playback(AudioBuffer& output, const AudioStreamTime& time) override {
        (void)time;
        std::lock_guard<std::mutex> lock(mutex);

        float* buffer = static_cast<float*>(output.data);
        int total_samples = output.frame_count * output.channel_count;

        // Clear buffer
        std::memset(buffer, 0, total_samples * sizeof(float));

        float master_vol = master_volume.load();

        // Mix all playing sounds
        for (auto it = playing_sounds.begin(); it != playing_sounds.end(); ) {
            PlayingSound& sound = *it;
            if (sound.paused) {
                ++it;
                continue;
            }

            AudioClip* clip = sound.clip;
            if (!clip) {
                it = playing_sounds.erase(it);
                continue;
            }

            const AudioFormat& clip_format = clip->get_format();
            const void* clip_data = clip->get_data();
            int clip_frames = clip->get_frame_count();

            // Simple mixing (assumes same format)
            int frames_to_mix = std::min(output.frame_count, clip_frames - sound.current_frame);
            float vol = sound.options.volume * master_vol * sound.current_volume;

            if (clip_format.sample_format == SampleFormat::Float32) {
                const float* src = static_cast<const float*>(clip_data);
                src += sound.current_frame * clip_format.channels;

                for (int f = 0; f < frames_to_mix; ++f) {
                    for (int c = 0; c < std::min(clip_format.channels, output.channel_count); ++c) {
                        buffer[f * output.channel_count + c] += src[f * clip_format.channels + c] * vol;
                    }
                }
            } else if (clip_format.sample_format == SampleFormat::Int16) {
                const int16_t* src = static_cast<const int16_t*>(clip_data);
                src += sound.current_frame * clip_format.channels;

                for (int f = 0; f < frames_to_mix; ++f) {
                    for (int c = 0; c < std::min(clip_format.channels, output.channel_count); ++c) {
                        float sample = static_cast<float>(src[f * clip_format.channels + c]) / 32768.0f;
                        buffer[f * output.channel_count + c] += sample * vol;
                    }
                }
            }

            sound.current_frame += frames_to_mix;

            // Check if sound finished
            if (sound.current_frame >= clip_frames) {
                if (sound.options.loop && (sound.options.loop_count < 0 || sound.loops_remaining > 0)) {
                    sound.current_frame = 0;
                    if (sound.options.loop_count > 0) {
                        sound.loops_remaining--;
                    }
                    ++it;
                } else {
                    it = playing_sounds.erase(it);
                }
            } else {
                ++it;
            }
        }

        // Clamp output
        for (int i = 0; i < total_samples; ++i) {
            buffer[i] = std::max(-1.0f, std::min(1.0f, buffer[i]));
        }

        return true;
    }
};

AudioPlayer* AudioPlayer::create(int device_index, AudioResult* out_result) {
    if (!g_audio_state.initialized) {
        if (out_result) *out_result = AudioResult::ErrorNotInitialized;
        return nullptr;
    }

    AudioStreamConfig config;
    config.format = AudioFormat::default_format();
    config.mode = AudioStreamMode::Playback;
    config.output_device_index = device_index;

    AudioResult result;
    AudioStream* stream = AudioStream::create(config, &result);
    if (!stream) {
        if (out_result) *out_result = result;
        return nullptr;
    }

    AudioPlayer* player = new AudioPlayer();
    player->impl_ = new AudioPlayer::Impl();
    player->impl_->stream = stream;
    player->impl_->format = stream->get_format();

    stream->set_callback(player->impl_);
    stream->start();

    if (out_result) *out_result = AudioResult::Success;
    return player;
}

void AudioPlayer::destroy() {
    if (!impl_) return;

    if (impl_->stream) {
        impl_->stream->stop();
        impl_->stream->destroy();
    }

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioPlayHandle AudioPlayer::play(AudioClip* clip, const AudioPlayOptions& options) {
    if (!impl_ || !clip) return INVALID_AUDIO_PLAY_HANDLE;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    PlayingSound sound;
    sound.clip = clip;
    sound.options = options;
    sound.current_frame = 0;
    sound.loops_remaining = options.loop_count;
    sound.paused = false;
    sound.current_volume = options.volume;
    sound.handle = impl_->next_handle++;

    impl_->playing_sounds.push_back(sound);

    return sound.handle;
}

void AudioPlayer::stop(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = std::find_if(impl_->playing_sounds.begin(), impl_->playing_sounds.end(),
                           [handle](const PlayingSound& s) { return s.handle == handle; });
    if (it != impl_->playing_sounds.end()) {
        impl_->playing_sounds.erase(it);
    }
}

void AudioPlayer::stop_all() {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->playing_sounds.clear();
}

bool AudioPlayer::is_playing(AudioPlayHandle handle) const {
    if (!impl_) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = std::find_if(impl_->playing_sounds.begin(), impl_->playing_sounds.end(),
                           [handle](const PlayingSound& s) { return s.handle == handle; });
    return it != impl_->playing_sounds.end() && !it->paused;
}

void AudioPlayer::set_volume(AudioPlayHandle handle, float volume) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = std::find_if(impl_->playing_sounds.begin(), impl_->playing_sounds.end(),
                           [handle](const PlayingSound& s) { return s.handle == handle; });
    if (it != impl_->playing_sounds.end()) {
        it->current_volume = std::max(0.0f, std::min(1.0f, volume));
    }
}

void AudioPlayer::set_pitch(AudioPlayHandle handle, float pitch) {
    (void)handle; (void)pitch;
    // Pitch shifting requires resampling - not implemented in basic version
}

void AudioPlayer::set_pan(AudioPlayHandle handle, float pan) {
    (void)handle; (void)pan;
    // Pan adjustment - not implemented in basic version
}

void AudioPlayer::pause(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = std::find_if(impl_->playing_sounds.begin(), impl_->playing_sounds.end(),
                           [handle](const PlayingSound& s) { return s.handle == handle; });
    if (it != impl_->playing_sounds.end()) {
        it->paused = true;
    }
}

void AudioPlayer::resume(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = std::find_if(impl_->playing_sounds.begin(), impl_->playing_sounds.end(),
                           [handle](const PlayingSound& s) { return s.handle == handle; });
    if (it != impl_->playing_sounds.end()) {
        it->paused = false;
    }
}

void AudioPlayer::update() {
    // No-op for WASAPI - audio processing happens in callback thread
}

int AudioPlayer::get_playing_count() const {
    if (!impl_) return 0;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    int count = 0;
    for (const auto& sound : impl_->playing_sounds) {
        if (!sound.paused) count++;
    }
    return count;
}

void AudioPlayer::set_master_volume(float volume) {
    if (impl_) impl_->master_volume = std::max(0.0f, std::min(1.0f, volume));
}

float AudioPlayer::get_master_volume() const {
    return impl_ ? impl_->master_volume.load() : 0.0f;
}

// ============================================================================
// AudioManager Implementation
// ============================================================================

AudioResult AudioManager::initialize(AudioBackend backend) {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (g_audio_state.initialized) {
        return AudioResult::ErrorAlreadyInitialized;
    }

    if (backend != AudioBackend::Auto && backend != AudioBackend::WASAPI) {
        return AudioResult::ErrorBackendNotSupported;
    }

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return AudioResult::ErrorUnknown;
    }

    // Create device enumerator
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator),
                          reinterpret_cast<void**>(&g_audio_state.device_enumerator));
    if (FAILED(hr)) {
        return AudioResult::ErrorUnknown;
    }

    g_audio_state.backend = AudioBackend::WASAPI;
    g_audio_state.initialized = true;

    return AudioResult::Success;
}

void AudioManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (!g_audio_state.initialized) return;

    if (g_audio_state.device_enumerator) {
        g_audio_state.device_enumerator->Release();
        g_audio_state.device_enumerator = nullptr;
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
    return audio_backend_to_string(g_audio_state.backend);
}

bool AudioManager::is_backend_supported(AudioBackend backend) {
    return backend == AudioBackend::Auto || backend == AudioBackend::WASAPI;
}

AudioBackend AudioManager::get_default_backend() {
    return AudioBackend::WASAPI;
}

int AudioManager::enumerate_devices(AudioDeviceType type, AudioDeviceEnumeration* out) {
    if (!g_audio_state.initialized || !out) return 0;

    out->count = 0;

    EDataFlow flow = (type == AudioDeviceType::Input) ? eCapture : eRender;

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = g_audio_state.device_enumerator->EnumAudioEndpoints(
        flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return 0;

    UINT count = 0;
    collection->GetCount(&count);

    // Get default device ID
    IMMDevice* default_device = nullptr;
    wchar_t* default_id = nullptr;
    hr = g_audio_state.device_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &default_device);
    if (SUCCEEDED(hr) && default_device) {
        default_device->GetId(&default_id);
        default_device->Release();
    }

    for (UINT i = 0; i < count && out->count < MAX_AUDIO_DEVICES; ++i) {
        IMMDevice* device = nullptr;
        hr = collection->Item(i, &device);
        if (FAILED(hr)) continue;

        AudioDeviceInfo& info = out->devices[out->count];
        info.type = type;

        // Get device ID
        wchar_t* device_id = nullptr;
        device->GetId(&device_id);
        if (device_id) {
            ::internal::wide_to_utf8(device_id, info.id, sizeof(info.id));
            info.is_default = default_id && wcscmp(device_id, default_id) == 0;
            CoTaskMemFree(device_id);
        }

        // Get device name
        IPropertyStore* props = nullptr;
        hr = device->OpenPropertyStore(STGM_READ, &props);
        if (SUCCEEDED(hr)) {
            PROPVARIANT name;
            PropVariantInit(&name);
            hr = props->GetValue(PKEY_Device_FriendlyName, &name);
            if (SUCCEEDED(hr) && name.vt == VT_LPWSTR) {
                ::internal::wide_to_utf8(name.pwszVal, info.name, sizeof(info.name));
            }
            PropVariantClear(&name);
            props->Release();
        }

        // Get format info
        IAudioClient* client = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void**>(&client));
        if (SUCCEEDED(hr)) {
            WAVEFORMATEX* format = nullptr;
            hr = client->GetMixFormat(&format);
            if (SUCCEEDED(hr)) {
                info.min_sample_rate = static_cast<int>(format->nSamplesPerSec);
                info.max_sample_rate = static_cast<int>(format->nSamplesPerSec);
                info.min_channels = static_cast<int>(format->nChannels);
                info.max_channels = static_cast<int>(format->nChannels);
                CoTaskMemFree(format);
            }
            client->Release();
        }

        device->Release();
        out->count++;
    }

    if (default_id) CoTaskMemFree(default_id);
    collection->Release();

    return out->count;
}

bool AudioManager::get_default_device(AudioDeviceType type, AudioDeviceInfo* out) {
    if (!g_audio_state.initialized || !out) return false;

    EDataFlow flow = (type == AudioDeviceType::Input) ? eCapture : eRender;

    IMMDevice* device = nullptr;
    HRESULT hr = g_audio_state.device_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    if (FAILED(hr) || !device) return false;

    out->type = type;
    out->is_default = true;

    // Get device ID
    wchar_t* device_id = nullptr;
    device->GetId(&device_id);
    if (device_id) {
        ::internal::wide_to_utf8(device_id, out->id, sizeof(out->id));
        CoTaskMemFree(device_id);
    }

    // Get device name
    IPropertyStore* props = nullptr;
    hr = device->OpenPropertyStore(STGM_READ, &props);
    if (SUCCEEDED(hr)) {
        PROPVARIANT name;
        PropVariantInit(&name);
        hr = props->GetValue(PKEY_Device_FriendlyName, &name);
        if (SUCCEEDED(hr) && name.vt == VT_LPWSTR) {
            ::internal::wide_to_utf8(name.pwszVal, out->name, sizeof(out->name));
        }
        PropVariantClear(&name);
        props->Release();
    }

    device->Release();
    return true;
}

bool AudioManager::get_device_info(int device_index, AudioDeviceType type, AudioDeviceInfo* out) {
    if (!out) return false;

    AudioDeviceEnumeration enumeration;
    enumerate_devices(type, &enumeration);

    if (device_index < 0 || device_index >= enumeration.count) {
        return false;
    }

    *out = enumeration.devices[device_index];
    return true;
}

bool AudioManager::is_format_supported(int device_index, const AudioFormat& format, AudioStreamMode mode) {
    if (!g_audio_state.initialized) return false;

    EDataFlow flow = (mode == AudioStreamMode::Capture) ? eCapture : eRender;

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = g_audio_state.device_enumerator->EnumAudioEndpoints(
        flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return false;

    IMMDevice* device = nullptr;
    if (device_index < 0) {
        hr = g_audio_state.device_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    } else {
        hr = collection->Item(static_cast<UINT>(device_index), &device);
    }
    collection->Release();

    if (FAILED(hr) || !device) return false;

    IAudioClient* client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    device->Release();

    if (FAILED(hr)) return false;

    WAVEFORMATEXTENSIBLE wfx = create_wave_format(format);
    hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED,
                                   reinterpret_cast<WAVEFORMATEX*>(&wfx), nullptr);
    client->Release();

    return hr == S_OK;
}

AudioFormat AudioManager::get_preferred_format(int device_index, AudioDeviceType type) {
    AudioFormat format;
    if (!g_audio_state.initialized) return format;

    EDataFlow flow = (type == AudioDeviceType::Input) ? eCapture : eRender;

    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = g_audio_state.device_enumerator->EnumAudioEndpoints(
        flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) return format;

    IMMDevice* device = nullptr;
    if (device_index < 0) {
        hr = g_audio_state.device_enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    } else {
        hr = collection->Item(static_cast<UINT>(device_index), &device);
    }
    collection->Release();

    if (FAILED(hr) || !device) return format;

    IAudioClient* client = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    device->Release();

    if (FAILED(hr)) return format;

    WAVEFORMATEX* mix_format = nullptr;
    hr = client->GetMixFormat(&mix_format);
    client->Release();

    if (SUCCEEDED(hr) && mix_format) {
        format = format_from_waveformat(mix_format);
        CoTaskMemFree(mix_format);
    }

    return format;
}

bool AudioManager::register_session_event_handler(IAudioSessionEventHandler* handler) {
    (void)handler;
    // Session events not implemented on Windows
    // Could be implemented using IMMNotificationClient for device changes
    return false;
}

void AudioManager::unregister_session_event_handler(IAudioSessionEventHandler* handler) {
    (void)handler;
}

bool AudioManager::are_session_events_supported() {
    return false;  // Not yet implemented for WASAPI
}

} // namespace audio
} // namespace window

#endif // WINDOW_PLATFORM_WIN32 && WINDOW_SUPPORT_WASAPI
