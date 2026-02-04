/*
 * audio_openal.cpp - OpenAL Soft Audio Backend
 *
 * Cross-platform audio using OpenAL Soft library.
 * Works on Windows, Linux, macOS, and more.
 */

#if defined(WINDOW_SUPPORT_OPENAL)

#include "audio.hpp"
#include <AL/al.h>
#include <AL/alc.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstring>
#include <cmath>

namespace window {
namespace audio {

// ============================================================================
// Global State
// ============================================================================

static struct {
    bool initialized = false;
    AudioBackend backend = AudioBackend::OpenAL;
    ALCdevice* device = nullptr;
    ALCcontext* context = nullptr;
    std::mutex mutex;
} g_audio_state;

// ============================================================================
// Helper Functions
// ============================================================================

static ALenum to_al_format(int channels, SampleFormat format) {
    if (format == SampleFormat::Int16) {
        switch (channels) {
            case 1: return AL_FORMAT_MONO16;
            case 2: return AL_FORMAT_STEREO16;
            default: return 0;
        }
    } else if (format == SampleFormat::Float32) {
        // OpenAL Soft extension for float
        switch (channels) {
            case 1: return alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            case 2: return alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            default: return 0;
        }
    } else if (format == SampleFormat::Int32) {
        // Try to use float format as fallback
        switch (channels) {
            case 1: return alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            case 2: return alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            default: return 0;
        }
    }
    return 0;
}

static const char* al_error_string(ALenum error) {
    switch (error) {
        case AL_NO_ERROR: return "No error";
        case AL_INVALID_NAME: return "Invalid name";
        case AL_INVALID_ENUM: return "Invalid enum";
        case AL_INVALID_VALUE: return "Invalid value";
        case AL_INVALID_OPERATION: return "Invalid operation";
        case AL_OUT_OF_MEMORY: return "Out of memory";
        default: return "Unknown error";
    }
}

// ============================================================================
// AudioStream::Impl - Using buffer queue for streaming
// ============================================================================

static constexpr int NUM_STREAM_BUFFERS = 4;
static constexpr int STREAM_BUFFER_SAMPLES = 2048;

struct AudioStream::Impl {
    ALuint source = 0;
    ALuint buffers[NUM_STREAM_BUFFERS] = {};
    ALenum al_format = 0;

    AudioStreamConfig config;
    AudioFormat actual_format;
    int actual_buffer_frames = STREAM_BUFFER_SAMPLES;

    IAudioCallback* callback = nullptr;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> running{false};
    std::atomic<bool> stop_requested{false};
    std::atomic<AudioStreamState> state{AudioStreamState::Stopped};

    uint64_t frame_position = 0;
    double stream_start_time = 0.0;

    std::thread stream_thread;
    std::vector<uint8_t> temp_buffer;

    void stream_thread_func();
    void fill_buffer(ALuint buffer);
};

void AudioStream::Impl::fill_buffer(ALuint buffer) {
    if (!callback) {
        // Fill with silence
        std::memset(temp_buffer.data(), 0, temp_buffer.size());
    } else {
        // Prepare audio buffer for callback
        AudioBuffer audio_buf;
        audio_buf.data = temp_buffer.data();
        audio_buf.frame_count = actual_buffer_frames;
        audio_buf.channel_count = actual_format.channels;
        audio_buf.format = actual_format.sample_format;

        // Clear buffer
        audio_buf.clear();

        // Get stream time
        AudioStreamTime stream_time;
        stream_time.frame_position = frame_position;

        // Call user callback
        callback->on_audio_playback(audio_buf, stream_time);

        // Apply volume
        float vol = volume.load();
        if (vol < 0.999f) {
            apply_volume(temp_buffer.data(), actual_format.sample_format,
                         actual_buffer_frames * actual_format.channels, vol);
        }
    }

    // Queue the buffer
    alBufferData(buffer, al_format, temp_buffer.data(),
                 static_cast<ALsizei>(temp_buffer.size()),
                 actual_format.sample_rate);

    frame_position += actual_buffer_frames;
}

void AudioStream::Impl::stream_thread_func() {
    running = true;
    state = AudioStreamState::Running;

    // Fill initial buffers
    for (int i = 0; i < NUM_STREAM_BUFFERS; ++i) {
        fill_buffer(buffers[i]);
    }

    // Queue all buffers
    alSourceQueueBuffers(source, NUM_STREAM_BUFFERS, buffers);

    // Start playing
    alSourcePlay(source);

    while (!stop_requested) {
        // Check for processed buffers
        ALint processed = 0;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

        while (processed > 0) {
            ALuint buffer;
            alSourceUnqueueBuffers(source, 1, &buffer);

            // Refill the buffer
            fill_buffer(buffer);

            // Re-queue it
            alSourceQueueBuffers(source, 1, &buffer);

            processed--;
        }

        // Check if source stopped (buffer underrun)
        ALint source_state;
        alGetSourcei(source, AL_SOURCE_STATE, &source_state);
        if (source_state != AL_PLAYING && !stop_requested) {
            alSourcePlay(source);
        }

        // Sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Stop the source
    alSourceStop(source);

    // Unqueue all buffers
    ALint queued = 0;
    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
    while (queued > 0) {
        ALuint buffer;
        alSourceUnqueueBuffers(source, 1, &buffer);
        queued--;
    }

    running = false;
    state = AudioStreamState::Stopped;
}

// ============================================================================
// AudioStream Implementation
// ============================================================================

AudioStream* AudioStream::create(const AudioStreamConfig& config, AudioResult* out_result) {
    if (!g_audio_state.initialized) {
        if (out_result) *out_result = AudioResult::ErrorNotInitialized;
        return nullptr;
    }

    // OpenAL primarily supports playback
    if (config.mode == AudioStreamMode::Capture) {
        // Capture requires ALC_EXT_CAPTURE extension - simplified for now
        if (out_result) *out_result = AudioResult::ErrorNotSupported;
        return nullptr;
    }

    AudioStream* stream = new AudioStream();
    stream->impl_ = new AudioStream::Impl();
    stream->impl_->config = config;

    // Determine format
    stream->impl_->actual_format = config.format;

    // Check if float format is supported
    ALenum al_format = to_al_format(config.format.channels, config.format.sample_format);
    if (al_format == 0) {
        // Fall back to Int16
        stream->impl_->actual_format.sample_format = SampleFormat::Int16;
        al_format = to_al_format(config.format.channels, SampleFormat::Int16);
    }

    if (al_format == 0) {
        if (out_result) *out_result = AudioResult::ErrorFormatNotSupported;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    stream->impl_->al_format = al_format;

    // Create source
    alGenSources(1, &stream->impl_->source);
    if (alGetError() != AL_NO_ERROR) {
        if (out_result) *out_result = AudioResult::ErrorOutOfMemory;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Create buffers
    alGenBuffers(NUM_STREAM_BUFFERS, stream->impl_->buffers);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteSources(1, &stream->impl_->source);
        if (out_result) *out_result = AudioResult::ErrorOutOfMemory;
        delete stream->impl_;
        delete stream;
        return nullptr;
    }

    // Calculate buffer size
    int buffer_frames = config.buffer_frames > 0 ? config.buffer_frames : STREAM_BUFFER_SAMPLES;
    stream->impl_->actual_buffer_frames = buffer_frames;

    // Allocate temp buffer
    size_t buffer_size = buffer_frames * stream->impl_->actual_format.bytes_per_frame();
    stream->impl_->temp_buffer.resize(buffer_size);

    if (out_result) *out_result = AudioResult::Success;
    return stream;
}

void AudioStream::destroy() {
    if (!impl_) return;

    stop();

    if (impl_->source) {
        alDeleteSources(1, &impl_->source);
    }
    alDeleteBuffers(NUM_STREAM_BUFFERS, impl_->buffers);

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioResult AudioStream::start() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (impl_->running) return AudioResult::ErrorStreamAlreadyRunning;

    impl_->stop_requested = false;
    impl_->frame_position = 0;

    // Start streaming thread
    impl_->stream_thread = std::thread(&AudioStream::Impl::stream_thread_func, impl_);

    return AudioResult::Success;
}

AudioResult AudioStream::stop() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (!impl_->running) return AudioResult::Success;

    impl_->stop_requested = true;

    if (impl_->stream_thread.joinable()) {
        impl_->stream_thread.join();
    }

    return AudioResult::Success;
}

AudioResult AudioStream::pause() {
    if (!impl_ || !impl_->running) return AudioResult::ErrorStreamNotRunning;
    alSourcePause(impl_->source);
    impl_->state = AudioStreamState::Paused;
    return AudioResult::Success;
}

AudioResult AudioStream::resume() {
    if (!impl_ || impl_->state != AudioStreamState::Paused) return AudioResult::ErrorStreamNotRunning;
    alSourcePlay(impl_->source);
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
    if (impl_) {
        impl_->volume = std::max(0.0f, std::min(1.0f, volume));
        alSourcef(impl_->source, AL_GAIN, impl_->volume.load());
    }
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
    if (!impl_) return 0.0;
    // Estimate latency based on buffer configuration
    return static_cast<double>(impl_->actual_buffer_frames * NUM_STREAM_BUFFERS) /
           impl_->actual_format.sample_rate;
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
// AudioClip::Impl
// ============================================================================

struct AudioClip::Impl {
    AudioFormat format;
    int frame_count = 0;
    std::vector<uint8_t> data;
    ALuint buffer = 0;  // OpenAL buffer for playback
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

    // Create OpenAL buffer
    if (g_audio_state.initialized) {
        alGenBuffers(1, &clip->impl_->buffer);
        if (alGetError() == AL_NO_ERROR && clip->impl_->buffer) {
            ALenum al_format = to_al_format(format.channels, format.sample_format);
            if (al_format != 0) {
                alBufferData(clip->impl_->buffer, al_format,
                             clip->impl_->data.data(),
                             static_cast<ALsizei>(clip->impl_->data.size()),
                             format.sample_rate);
            }
        }
    }

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

    if (impl_->buffer) {
        alDeleteBuffers(1, &impl_->buffer);
    }

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
// AudioPlayer::Impl - Simple sound playback using OpenAL sources
// ============================================================================

static constexpr int MAX_PLAYING_SOURCES = 32;

struct PlayingSource {
    ALuint source = 0;
    AudioPlayHandle handle = 0;
    bool active = false;
};

struct AudioPlayer::Impl {
    PlayingSource sources[MAX_PLAYING_SOURCES];
    std::mutex mutex;
    std::atomic<float> master_volume{1.0f};
    AudioPlayHandle next_handle = 1;

    ALuint get_free_source();
    PlayingSource* find_source(AudioPlayHandle handle);
};

ALuint AudioPlayer::Impl::get_free_source() {
    // Find an inactive source
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (!sources[i].active) {
            if (sources[i].source == 0) {
                alGenSources(1, &sources[i].source);
                if (alGetError() != AL_NO_ERROR) {
                    return 0;
                }
            }
            return sources[i].source;
        }

        // Check if source has finished playing
        ALint state;
        alGetSourcei(sources[i].source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && state != AL_PAUSED) {
            sources[i].active = false;
            return sources[i].source;
        }
    }
    return 0;
}

PlayingSource* AudioPlayer::Impl::find_source(AudioPlayHandle handle) {
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (sources[i].active && sources[i].handle == handle) {
            return &sources[i];
        }
    }
    return nullptr;
}

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

    stop_all();

    // Delete all sources
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (impl_->sources[i].source) {
            alDeleteSources(1, &impl_->sources[i].source);
        }
    }

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioPlayHandle AudioPlayer::play(AudioClip* clip, const AudioPlayOptions& options) {
    if (!impl_ || !clip || !clip->impl_ || !clip->impl_->buffer) {
        return INVALID_AUDIO_PLAY_HANDLE;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);

    ALuint source = impl_->get_free_source();
    if (source == 0) {
        return INVALID_AUDIO_PLAY_HANDLE;
    }

    // Configure source
    alSourcei(source, AL_BUFFER, clip->impl_->buffer);
    alSourcef(source, AL_GAIN, options.volume * impl_->master_volume.load());
    alSourcef(source, AL_PITCH, options.pitch);
    alSourcei(source, AL_LOOPING, options.loop ? AL_TRUE : AL_FALSE);

    // Set position for panning (simple left-right)
    alSource3f(source, AL_POSITION, options.pan, 0.0f, 0.0f);

    // Start playing
    alSourcePlay(source);

    // Track the source
    AudioPlayHandle handle = impl_->next_handle++;
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (impl_->sources[i].source == source) {
            impl_->sources[i].handle = handle;
            impl_->sources[i].active = true;
            break;
        }
    }

    return handle;
}

void AudioPlayer::stop(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        alSourceStop(ps->source);
        ps->active = false;
    }
}

void AudioPlayer::stop_all() {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (impl_->sources[i].active && impl_->sources[i].source) {
            alSourceStop(impl_->sources[i].source);
            impl_->sources[i].active = false;
        }
    }
}

bool AudioPlayer::is_playing(AudioPlayHandle handle) const {
    if (!impl_) return false;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = const_cast<AudioPlayer::Impl*>(impl_)->find_source(handle);
    if (!ps) return false;

    ALint state;
    alGetSourcei(ps->source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void AudioPlayer::set_volume(AudioPlayHandle handle, float volume) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        alSourcef(ps->source, AL_GAIN, volume * impl_->master_volume.load());
    }
}

void AudioPlayer::set_pitch(AudioPlayHandle handle, float pitch) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        alSourcef(ps->source, AL_PITCH, pitch);
    }
}

void AudioPlayer::set_pan(AudioPlayHandle handle, float pan) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        alSource3f(ps->source, AL_POSITION, pan, 0.0f, 0.0f);
    }
}

void AudioPlayer::pause(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        alSourcePause(ps->source);
    }
}

void AudioPlayer::resume(AudioPlayHandle handle) {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    PlayingSource* ps = impl_->find_source(handle);
    if (ps) {
        ALint state;
        alGetSourcei(ps->source, AL_SOURCE_STATE, &state);
        if (state == AL_PAUSED) {
            alSourcePlay(ps->source);
        }
    }
}

void AudioPlayer::update() {
    if (!impl_) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);

    // Clean up finished sources
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (impl_->sources[i].active && impl_->sources[i].source) {
            ALint state;
            alGetSourcei(impl_->sources[i].source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING && state != AL_PAUSED) {
                impl_->sources[i].active = false;
            }
        }
    }
}

int AudioPlayer::get_playing_count() const {
    if (!impl_) return 0;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    int count = 0;
    for (int i = 0; i < MAX_PLAYING_SOURCES; ++i) {
        if (impl_->sources[i].active) {
            ALint state;
            alGetSourcei(impl_->sources[i].source, AL_SOURCE_STATE, &state);
            if (state == AL_PLAYING) {
                count++;
            }
        }
    }
    return count;
}

void AudioPlayer::set_master_volume(float volume) {
    if (impl_) {
        impl_->master_volume = std::max(0.0f, std::min(1.0f, volume));
        alListenerf(AL_GAIN, impl_->master_volume.load());
    }
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

    if (backend != AudioBackend::Auto && backend != AudioBackend::OpenAL) {
        return AudioResult::ErrorBackendNotSupported;
    }

    // Open default device
    g_audio_state.device = alcOpenDevice(nullptr);
    if (!g_audio_state.device) {
        return AudioResult::ErrorDeviceNotFound;
    }

    // Create context
    g_audio_state.context = alcCreateContext(g_audio_state.device, nullptr);
    if (!g_audio_state.context) {
        alcCloseDevice(g_audio_state.device);
        g_audio_state.device = nullptr;
        return AudioResult::ErrorUnknown;
    }

    // Make context current
    if (!alcMakeContextCurrent(g_audio_state.context)) {
        alcDestroyContext(g_audio_state.context);
        alcCloseDevice(g_audio_state.device);
        g_audio_state.context = nullptr;
        g_audio_state.device = nullptr;
        return AudioResult::ErrorUnknown;
    }

    g_audio_state.backend = AudioBackend::OpenAL;
    g_audio_state.initialized = true;

    return AudioResult::Success;
}

void AudioManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (!g_audio_state.initialized) return;

    alcMakeContextCurrent(nullptr);

    if (g_audio_state.context) {
        alcDestroyContext(g_audio_state.context);
        g_audio_state.context = nullptr;
    }

    if (g_audio_state.device) {
        alcCloseDevice(g_audio_state.device);
        g_audio_state.device = nullptr;
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
    return "OpenAL";
}

bool AudioManager::is_backend_supported(AudioBackend backend) {
    return backend == AudioBackend::Auto || backend == AudioBackend::OpenAL;
}

AudioBackend AudioManager::get_default_backend() {
    return AudioBackend::OpenAL;
}

int AudioManager::enumerate_devices(AudioDeviceType type, AudioDeviceEnumeration* out) {
    if (!out) return 0;
    out->count = 0;

    // Check for enumeration extension
    if (!alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT")) {
        // Just report default device
        AudioDeviceInfo& info = out->devices[0];
        strncpy(info.name, "Default OpenAL Device", sizeof(info.name) - 1);
        info.type = type;
        info.is_default = true;
        info.min_sample_rate = 8000;
        info.max_sample_rate = 192000;
        info.min_channels = 1;
        info.max_channels = 2;
        out->count = 1;
        return 1;
    }

    const ALCchar* devices = nullptr;

    if (type == AudioDeviceType::Input) {
        if (alcIsExtensionPresent(nullptr, "ALC_EXT_CAPTURE")) {
            devices = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER);
        }
    } else {
        devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    }

    if (!devices) {
        return 0;
    }

    // Get default device name
    const ALCchar* default_device = nullptr;
    if (type == AudioDeviceType::Input) {
        default_device = alcGetString(nullptr, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
    } else {
        default_device = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
    }

    // Parse device list (null-separated, double-null terminated)
    while (*devices && out->count < MAX_AUDIO_DEVICES) {
        AudioDeviceInfo& info = out->devices[out->count];

        strncpy(info.name, devices, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        strncpy(info.id, devices, sizeof(info.id) - 1);
        info.id[sizeof(info.id) - 1] = '\0';
        info.type = type;
        info.is_default = default_device && strcmp(devices, default_device) == 0;
        info.min_sample_rate = 8000;
        info.max_sample_rate = 192000;
        info.min_channels = 1;
        info.max_channels = 2;

        out->count++;
        devices += strlen(devices) + 1;
    }

    return out->count;
}

bool AudioManager::get_default_device(AudioDeviceType type, AudioDeviceInfo* out) {
    if (!out) return false;

    const ALCchar* name = nullptr;
    if (type == AudioDeviceType::Input) {
        name = alcGetString(nullptr, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
    } else {
        name = alcGetString(nullptr, ALC_DEFAULT_DEVICE_SPECIFIER);
    }

    if (!name) {
        strncpy(out->name, "Default OpenAL Device", sizeof(out->name) - 1);
    } else {
        strncpy(out->name, name, sizeof(out->name) - 1);
    }
    out->name[sizeof(out->name) - 1] = '\0';
    out->type = type;
    out->is_default = true;
    out->min_sample_rate = 8000;
    out->max_sample_rate = 192000;
    out->min_channels = 1;
    out->max_channels = 2;

    return true;
}

bool AudioManager::get_device_info(int device_index, AudioDeviceType type, AudioDeviceInfo* out) {
    AudioDeviceEnumeration enumeration;
    enumerate_devices(type, &enumeration);

    if (device_index < 0 || device_index >= enumeration.count) {
        return get_default_device(type, out);
    }

    if (out) {
        *out = enumeration.devices[device_index];
    }
    return true;
}

bool AudioManager::is_format_supported(int device_index, const AudioFormat& format, AudioStreamMode mode) {
    (void)device_index; (void)mode;

    // OpenAL supports mono and stereo, various sample rates
    if (!format.is_valid()) return false;
    if (format.channels != 1 && format.channels != 2) return false;
    if (format.sample_rate < 8000 || format.sample_rate > 192000) return false;

    // Check format support
    ALenum al_format = to_al_format(format.channels, format.sample_format);
    return al_format != 0;
}

AudioFormat AudioManager::get_preferred_format(int device_index, AudioDeviceType type) {
    (void)device_index; (void)type;

    AudioFormat format;
    format.sample_format = SampleFormat::Int16;  // Most compatible
    format.sample_rate = 44100;
    format.channels = 2;
    format.layout = ChannelLayout::Stereo;
    return format;
}

} // namespace audio
} // namespace window

#endif // WINDOW_SUPPORT_OPENAL
