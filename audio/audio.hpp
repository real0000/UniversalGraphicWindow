/*
 * audio.hpp - Cross-Platform Audio Interface
 *
 * This is the single public header for audio functionality.
 * Include this file to access all audio features.
 */

#ifndef WINDOW_AUDIO_HPP
#define WINDOW_AUDIO_HPP

#include <cstdint>
#include <cstddef>
#include <string>

namespace window {
namespace audio {

// Forward declarations
class AudioStream;
class AudioClip;
class AudioPlayer;
class AudioFileStream;

// ============================================================================
// Constants
// ============================================================================
static constexpr int MAX_AUDIO_DEVICES = 32;
static constexpr int MAX_AUDIO_CHANNELS = 8;

// ============================================================================
// Enums
// ============================================================================

enum class AudioBackend : uint8_t {
    Auto = 0,
    WASAPI,
    CoreAudio,
    PulseAudio,
    ALSA,
    AAudio,
    OpenSLES,
    WebAudio,
    OpenAL
};

enum class AudioResult {
    Success = 0,
    ErrorUnknown,
    ErrorNotInitialized,
    ErrorAlreadyInitialized,
    ErrorDeviceNotFound,
    ErrorFormatNotSupported,
    ErrorDeviceLost,
    ErrorDeviceBusy,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorBackendNotSupported,
    ErrorStreamNotRunning,
    ErrorStreamAlreadyRunning,
    ErrorTimeout,
    ErrorFileNotFound,
    ErrorFileFormat,
    ErrorEndOfFile
};

enum class SampleFormat : uint8_t {
    Unknown = 0,
    Int16,
    Int24,
    Int32,
    Float32
};

enum class ChannelLayout : uint8_t {
    Unknown = 0,
    Mono,
    Stereo,
    Surround21,
    Surround40,
    Surround41,
    Surround51,
    Surround71
};

enum class AudioDeviceType : uint8_t {
    Output = 0,
    Input,
    Duplex
};

enum class AudioStreamMode : uint8_t {
    Playback = 0,
    Capture,
    Duplex
};

enum class AudioStreamState : uint8_t {
    Stopped = 0,
    Running,
    Paused,
    Error
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* audio_result_to_string(AudioResult result);
const char* audio_backend_to_string(AudioBackend backend);
const char* sample_format_to_string(SampleFormat format);
const char* channel_layout_to_string(ChannelLayout layout);

// ============================================================================
// AudioFormat
// ============================================================================

struct AudioFormat {
    SampleFormat sample_format = SampleFormat::Float32;
    int sample_rate = 48000;
    int channels = 2;
    ChannelLayout layout = ChannelLayout::Stereo;

    // Get bytes per sample (single channel)
    int bytes_per_sample() const {
        switch (sample_format) {
            case SampleFormat::Int16:   return 2;
            case SampleFormat::Int24:   return 3;
            case SampleFormat::Int32:   return 4;
            case SampleFormat::Float32: return 4;
            default: return 0;
        }
    }

    // Get bytes per frame (all channels)
    int bytes_per_frame() const {
        return bytes_per_sample() * channels;
    }

    // Get bytes per second
    int bytes_per_second() const {
        return bytes_per_frame() * sample_rate;
    }

    // Check if format is valid
    bool is_valid() const {
        return sample_format != SampleFormat::Unknown &&
               sample_rate > 0 &&
               channels > 0 && channels <= MAX_AUDIO_CHANNELS;
    }

    // Create a default format
    static AudioFormat default_format() {
        AudioFormat fmt;
        fmt.sample_format = SampleFormat::Float32;
        fmt.sample_rate = 48000;
        fmt.channels = 2;
        fmt.layout = ChannelLayout::Stereo;
        return fmt;
    }

    // Create a common CD quality format
    static AudioFormat cd_quality() {
        AudioFormat fmt;
        fmt.sample_format = SampleFormat::Int16;
        fmt.sample_rate = 44100;
        fmt.channels = 2;
        fmt.layout = ChannelLayout::Stereo;
        return fmt;
    }

    // Comparison operators
    bool operator==(const AudioFormat& other) const {
        return sample_format == other.sample_format &&
               sample_rate == other.sample_rate &&
               channels == other.channels &&
               layout == other.layout;
    }

    bool operator!=(const AudioFormat& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// AudioDeviceInfo
// ============================================================================

struct AudioDeviceInfo {
    std::string name;
    std::string id;
    AudioDeviceType type = AudioDeviceType::Output;
    bool is_default = false;
    int min_sample_rate = 0;
    int max_sample_rate = 0;
    int min_channels = 0;
    int max_channels = 0;
};

struct AudioDeviceEnumeration {
    AudioDeviceInfo devices[MAX_AUDIO_DEVICES];
    int count = 0;
};

// ============================================================================
// AudioBuffer
// ============================================================================

struct AudioBuffer {
    void* data = nullptr;
    int frame_count = 0;
    int channel_count = 0;
    SampleFormat format = SampleFormat::Unknown;

    // Get total size in bytes
    size_t size_bytes() const {
        int bytes_per_sample = 0;
        switch (format) {
            case SampleFormat::Int16:   bytes_per_sample = 2; break;
            case SampleFormat::Int24:   bytes_per_sample = 3; break;
            case SampleFormat::Int32:   bytes_per_sample = 4; break;
            case SampleFormat::Float32: bytes_per_sample = 4; break;
            default: return 0;
        }
        return static_cast<size_t>(frame_count) * channel_count * bytes_per_sample;
    }

    // Clear buffer to silence
    void clear();
};

// ============================================================================
// AudioStreamTime
// ============================================================================

struct AudioStreamTime {
    double stream_time = 0.0;       // Time since stream started (seconds)
    double input_latency = 0.0;     // Current input latency (seconds)
    double output_latency = 0.0;    // Current output latency (seconds)
    uint64_t frame_position = 0;    // Current frame position
};

// ============================================================================
// Audio Session Events (Apple platforms)
// ============================================================================

// Audio session event types
enum class AudioSessionEvent : uint8_t {
    // Interruption events (iOS/macOS)
    InterruptionBegan,          // Another app/system took audio focus
    InterruptionEnded,          // Interruption ended, can resume

    // Route change events (iOS/macOS)
    RouteChangeNewDeviceAvailable,      // New device connected (headphones, Bluetooth)
    RouteChangeOldDeviceUnavailable,    // Device disconnected
    RouteChangeCategoryChange,          // Audio category changed
    RouteChangeOverride,                // Route was overridden
    RouteChangeWakeFromSleep,           // Device woke from sleep
    RouteChangeNoSuitableRouteForCategory,
    RouteChangeRouteConfigurationChange,

    // Device events (macOS)
    DefaultOutputDeviceChanged,  // Default output device changed
    DefaultInputDeviceChanged,   // Default input device changed
    DeviceListChanged,           // Device list changed (added/removed)

    // Media services (iOS)
    MediaServicesWereLost,       // Media services crashed
    MediaServicesWereReset,      // Media services restarted

    // Silence secondary audio (iOS)
    SilenceSecondaryAudioHintBegan,  // Should silence secondary audio
    SilenceSecondaryAudioHintEnded   // Can resume secondary audio
};

// Options for resuming after interruption
enum class AudioInterruptionOption : uint8_t {
    None = 0,
    ShouldResume = 1    // System suggests resuming playback
};

// Audio route change reason
enum class AudioRouteChangeReason : uint8_t {
    Unknown = 0,
    NewDeviceAvailable,
    OldDeviceUnavailable,
    CategoryChange,
    Override,
    WakeFromSleep,
    NoSuitableRouteForCategory,
    RouteConfigurationChange
};

// Audio session event data
struct AudioSessionEventData {
    AudioSessionEvent event = AudioSessionEvent::InterruptionBegan;

    // For interruption events
    AudioInterruptionOption interruption_option = AudioInterruptionOption::None;

    // For route change events
    AudioRouteChangeReason route_change_reason = AudioRouteChangeReason::Unknown;

    // Previous/new device info (when available)
    std::string previous_device_name;
    std::string new_device_name;
};

// Audio session event handler interface
class IAudioSessionEventHandler {
public:
    virtual ~IAudioSessionEventHandler() = default;

    // Called when an audio session event occurs
    virtual void on_audio_session_event(const AudioSessionEventData& event) = 0;
};

// String conversion for audio session events
const char* audio_session_event_to_string(AudioSessionEvent event);

// ============================================================================
// AudioStreamConfig
// ============================================================================

struct AudioStreamConfig {
    AudioFormat format;
    AudioStreamMode mode = AudioStreamMode::Playback;
    int output_device_index = -1;   // -1 = default device
    int input_device_index = -1;    // -1 = default device
    int buffer_frames = 0;          // 0 = auto (typically 256-1024)
    int periods = 2;                // Number of periods/buffers
    bool exclusive_mode = false;    // Request exclusive device access
};

// ============================================================================
// IAudioCallback - Callback Interface for Streaming Audio
// ============================================================================

class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;

    // Called when audio output needs more data
    // Return true to continue playback, false to stop
    virtual bool on_audio_playback(AudioBuffer& output, const AudioStreamTime& time) {
        (void)output; (void)time;
        return true;
    }

    // Called when audio input has captured data
    // Return true to continue capture, false to stop
    virtual bool on_audio_capture(const AudioBuffer& input, const AudioStreamTime& time) {
        (void)input; (void)time;
        return true;
    }

    // Called when an error occurs
    virtual void on_audio_error(AudioResult error) {
        (void)error;
    }

    // Called when stream state changes
    virtual void on_audio_state_change(AudioStreamState old_state, AudioStreamState new_state) {
        (void)old_state; (void)new_state;
    }
};

// ============================================================================
// AudioStream - Low-Latency Streaming Audio
// ============================================================================

class AudioStream {
public:
    // Factory method to create an audio stream
    static AudioStream* create(const AudioStreamConfig& config, AudioResult* out_result = nullptr);

    // Destroy the audio stream
    void destroy();

    // Start the audio stream
    AudioResult start();

    // Stop the audio stream
    AudioResult stop();

    // Pause the audio stream (if supported)
    AudioResult pause();

    // Resume the audio stream after pause
    AudioResult resume();

    // Check if the stream is currently running
    bool is_running() const;

    // Get current stream state
    AudioStreamState get_state() const;

    // Set the callback for audio processing
    void set_callback(IAudioCallback* callback);

    // Set stream volume (0.0 to 1.0)
    void set_volume(float volume);

    // Get current volume
    float get_volume() const;

    // Get the actual audio format being used
    const AudioFormat& get_format() const;

    // Get the actual buffer size in frames
    int get_buffer_frames() const;

    // Get estimated output latency in seconds
    double get_output_latency() const;

    // Get estimated input latency in seconds
    double get_input_latency() const;

    // Get current stream time information
    AudioStreamTime get_stream_time() const;

    // Platform-specific implementation
    struct Impl;

private:
    AudioStream() = default;
    ~AudioStream() = default;
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;

    Impl* impl_ = nullptr;
};

// ============================================================================
// AudioPlayHandle - Handle for Simple Audio Playback
// ============================================================================

using AudioPlayHandle = uint32_t;
static constexpr AudioPlayHandle INVALID_AUDIO_PLAY_HANDLE = 0;

// ============================================================================
// AudioPlayOptions
// ============================================================================

struct AudioPlayOptions {
    float volume = 1.0f;
    float pitch = 1.0f;
    float pan = 0.0f;        // -1.0 = left, 0.0 = center, 1.0 = right
    bool loop = false;
    int loop_count = -1;     // -1 = infinite, 0+ = specific count
    float fade_in = 0.0f;    // Fade in duration in seconds
    float fade_out = 0.0f;   // Fade out duration in seconds
};

// ============================================================================
// AudioClip - Audio Data Container
// ============================================================================

class AudioClip {
public:
    // Load audio from file (supports WAV, OGG, MP3 depending on backend)
    static AudioClip* load(const char* filepath, AudioResult* out_result = nullptr);

    // Create an empty audio clip with specified format
    static AudioClip* create(const AudioFormat& format, int frame_count, AudioResult* out_result = nullptr);

    // Destroy the audio clip
    void destroy();

    // Get the audio format
    const AudioFormat& get_format() const;

    // Get total frame count
    int get_frame_count() const;

    // Get duration in seconds
    double get_duration() const;

    // Get raw data pointer
    void* get_data();
    const void* get_data() const;

    // Get data size in bytes
    size_t get_data_size() const;

    // Platform-specific implementation
    struct Impl;

private:
    AudioClip() = default;
    ~AudioClip() = default;
    AudioClip(const AudioClip&) = delete;
    AudioClip& operator=(const AudioClip&) = delete;

    Impl* impl_ = nullptr;
};

// ============================================================================
// AudioFileStream - Streaming Audio from Files
// ============================================================================

// Seek origin for audio file streams
enum class AudioSeekOrigin {
    Begin,      // Seek from beginning of file
    Current,    // Seek from current position
    End         // Seek from end of file
};

class AudioFileStream {
public:
    // Open an audio file for streaming (supports WAV; OGG/MP3 with extensions)
    static AudioFileStream* open(const char* filepath, AudioResult* out_result = nullptr);

    // Close the stream and release resources
    void close();

    // Get the audio format of the stream
    const AudioFormat& get_format() const;

    // Read frames from the stream into buffer
    // Returns number of frames actually read (may be less at end of file)
    int read_frames(void* buffer, int frame_count);

    // Read frames and convert to specified format
    int read_frames_converted(void* buffer, int frame_count, SampleFormat target_format);

    // Seek to a specific frame position
    bool seek(int64_t frame_position, AudioSeekOrigin origin = AudioSeekOrigin::Begin);

    // Get current position in frames
    int64_t get_position() const;

    // Get total number of frames in the file
    int64_t get_total_frames() const;

    // Get duration in seconds
    double get_duration() const;

    // Check if at end of stream
    bool is_end_of_stream() const;

    // Check if stream is valid and open
    bool is_open() const;

    // Reset to beginning
    void rewind();

    // Platform-specific implementation
    struct Impl;

private:
    AudioFileStream() = default;
    ~AudioFileStream() = default;
    AudioFileStream(const AudioFileStream&) = delete;
    AudioFileStream& operator=(const AudioFileStream&) = delete;

    Impl* impl_ = nullptr;
};

// ============================================================================
// StreamingAudioCallback - Helper for Streaming Playback
// ============================================================================

// Helper class that implements IAudioCallback for streaming from AudioFileStream
class StreamingAudioCallback : public IAudioCallback {
public:
    StreamingAudioCallback();
    ~StreamingAudioCallback();

    // Set the source stream (takes ownership if owns_stream is true)
    void set_source(AudioFileStream* stream, bool owns_stream = false);

    // Get the current source stream
    AudioFileStream* get_source() const;

    // Set whether to loop the stream
    void set_looping(bool loop);
    bool is_looping() const;

    // Set volume (0.0 to 1.0)
    void set_volume(float volume);
    float get_volume() const;

    // Check if playback has finished (reached end without looping)
    bool is_finished() const;

    // Reset playback to beginning
    void reset();

    // IAudioCallback implementation
    bool on_audio_playback(AudioBuffer& output, const AudioStreamTime& time) override;
    void on_audio_error(AudioResult error) override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// ============================================================================
// AudioPlayer - Simple Audio Playback
// ============================================================================

class AudioPlayer {
public:
    // Create an audio player on the specified device
    static AudioPlayer* create(int device_index = -1, AudioResult* out_result = nullptr);

    // Destroy the audio player
    void destroy();

    // Play an audio clip
    AudioPlayHandle play(AudioClip* clip, const AudioPlayOptions& options = AudioPlayOptions());

    // Stop a playing sound
    void stop(AudioPlayHandle handle);

    // Stop all playing sounds
    void stop_all();

    // Check if a sound is playing
    bool is_playing(AudioPlayHandle handle) const;

    // Set volume for a playing sound
    void set_volume(AudioPlayHandle handle, float volume);

    // Set pitch for a playing sound
    void set_pitch(AudioPlayHandle handle, float pitch);

    // Set pan for a playing sound
    void set_pan(AudioPlayHandle handle, float pan);

    // Pause a playing sound
    void pause(AudioPlayHandle handle);

    // Resume a paused sound
    void resume(AudioPlayHandle handle);

    // Update the player (call each frame for accurate timing)
    void update();

    // Get number of sounds currently playing
    int get_playing_count() const;

    // Set master volume for all sounds
    void set_master_volume(float volume);

    // Get master volume
    float get_master_volume() const;

    // Platform-specific implementation
    struct Impl;

private:
    AudioPlayer() = default;
    ~AudioPlayer() = default;
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    Impl* impl_ = nullptr;
};

// ============================================================================
// Audio Resampler Interface
// ============================================================================

// Resampler quality presets
enum class ResamplerQuality : uint8_t {
    Fastest,    // Linear interpolation (lowest CPU, some aliasing)
    Low,        // Linear with basic anti-aliasing
    Medium,     // Cubic interpolation
    High,       // Windowed sinc (8-tap)
    Best        // Windowed sinc (16-tap, highest quality)
};

// Configuration for creating a resampler
struct ResamplerConfig {
    int input_rate = 48000;         // Input sample rate in Hz
    int output_rate = 48000;        // Output sample rate in Hz
    int channels = 2;               // Number of channels
    ResamplerQuality quality = ResamplerQuality::Medium;
    int max_input_frames = 4096;    // Maximum input frames per process call
};

// Abstract audio resampler interface
class IAudioResampler {
public:
    virtual ~IAudioResampler() = default;

    // Process samples (interleaved float format)
    // Returns number of output frames written
    // input: interleaved float samples
    // output: buffer for resampled output (must have room for at least get_output_frames_max() frames)
    // input_frames: number of input frames to process
    virtual int process(const float* input, float* output, int input_frames) = 0;

    // Process samples with format conversion
    // Handles conversion from source format to float internally
    virtual int process_convert(const void* input, SampleFormat input_format,
                                float* output, int input_frames) = 0;

    // Reset resampler state (call when seeking or starting new stream)
    virtual void reset() = 0;

    // Get current input sample rate
    virtual int get_input_rate() const = 0;

    // Get current output sample rate
    virtual int get_output_rate() const = 0;

    // Get number of channels
    virtual int get_channels() const = 0;

    // Get the quality setting
    virtual ResamplerQuality get_quality() const = 0;

    // Set new sample rates (may reset internal state)
    virtual void set_rates(int input_rate, int output_rate) = 0;

    // Get the maximum number of output frames for given input frames
    virtual int get_output_frames_max(int input_frames) const = 0;

    // Get the latency in input frames
    virtual int get_latency_frames() const = 0;

    // Flush remaining samples (call at end of stream)
    // Returns number of output frames written
    virtual int flush(float* output) = 0;

    // Get ratio (output_rate / input_rate)
    double get_ratio() const {
        return static_cast<double>(get_output_rate()) / static_cast<double>(get_input_rate());
    }
};

// CPU-based audio resampler implementation
// Uses linear or sinc interpolation depending on quality setting
class AudioResamplerCPU : public IAudioResampler {
public:
    // Create a resampler with the specified configuration
    static AudioResamplerCPU* create(const ResamplerConfig& config, AudioResult* out_result = nullptr);

    // Destroy the resampler
    void destroy();

    // IAudioResampler implementation
    int process(const float* input, float* output, int input_frames) override;
    int process_convert(const void* input, SampleFormat input_format,
                        float* output, int input_frames) override;
    void reset() override;
    int get_input_rate() const override;
    int get_output_rate() const override;
    int get_channels() const override;
    ResamplerQuality get_quality() const override;
    void set_rates(int input_rate, int output_rate) override;
    int get_output_frames_max(int input_frames) const override;
    int get_latency_frames() const override;
    int flush(float* output) override;

    struct Impl;

private:
    AudioResamplerCPU() = default;
    ~AudioResamplerCPU() = default;
    AudioResamplerCPU(const AudioResamplerCPU&) = delete;
    AudioResamplerCPU& operator=(const AudioResamplerCPU&) = delete;

    Impl* impl_ = nullptr;
};

// ============================================================================
// Audio Effects Interface
// ============================================================================

// Common audio effect types
enum class AudioEffectType : uint8_t {
    Custom = 0,         // User-defined effect
    Gain,               // Volume/gain adjustment
    Pan,                // Stereo panning
    Delay,              // Simple delay
    LowPassFilter,      // Low-pass filter
    HighPassFilter,     // High-pass filter
    BandPassFilter,     // Band-pass filter
    Notch,              // Notch filter
    PeakingEQ,          // Peaking equalizer
    LowShelf,           // Low shelf filter
    HighShelf,          // High shelf filter
    Compressor,         // Dynamics compressor
    Limiter,            // Hard limiter
    NoiseGate,          // Noise gate
    Reverb,             // Reverb (basic)
    Chorus,             // Chorus effect
    Distortion          // Distortion/overdrive
};

// Biquad filter type for AudioEffectBiquadFilter
enum class BiquadFilterType : uint8_t {
    LowPass = 0,
    HighPass,
    BandPass,
    Notch,
    AllPass,
    PeakingEQ,
    LowShelf,
    HighShelf
};

// Abstract audio effect interface
class IAudioEffect {
public:
    virtual ~IAudioEffect() = default;

    // Process audio samples in-place (interleaved float format)
    // samples: interleaved audio samples
    // frame_count: number of frames to process
    // channels: number of channels in the audio
    virtual void process(float* samples, int frame_count, int channels) = 0;

    // Process audio samples from input to output buffer
    // Can be same buffer for in-place processing
    virtual void process(const float* input, float* output, int frame_count, int channels) {
        // Default implementation: copy then process in-place
        if (input != output) {
            for (int i = 0; i < frame_count * channels; ++i) {
                output[i] = input[i];
            }
        }
        process(output, frame_count, channels);
    }

    // Reset effect state (call when seeking or starting new audio)
    virtual void reset() = 0;

    // Get effect type
    virtual AudioEffectType get_type() const = 0;

    // Get latency introduced by this effect in frames
    virtual int get_latency_frames() const { return 0; }

    // Check if effect is enabled
    virtual bool is_enabled() const = 0;

    // Enable/disable the effect
    virtual void set_enabled(bool enabled) = 0;

    // Get/set dry/wet mix (0.0 = all dry, 1.0 = all wet)
    virtual float get_mix() const { return 1.0f; }
    virtual void set_mix(float mix) { (void)mix; }
};

// ============================================================================
// Audio Effect Chain
// ============================================================================

static constexpr int MAX_AUDIO_EFFECTS = 16;

// Chain of audio effects processed in sequence
class AudioEffectChain {
public:
    AudioEffectChain();
    ~AudioEffectChain();

    // Add an effect to the end of the chain
    // Returns false if chain is full
    bool add_effect(IAudioEffect* effect);

    // Insert effect at specific position
    bool insert_effect(int index, IAudioEffect* effect);

    // Remove effect from chain (does not delete it)
    bool remove_effect(IAudioEffect* effect);

    // Remove effect at index
    bool remove_effect_at(int index);

    // Get effect at index
    IAudioEffect* get_effect(int index) const;

    // Get number of effects in chain
    int get_effect_count() const;

    // Clear all effects from chain (does not delete them)
    void clear();

    // Process audio through the entire chain
    void process(float* samples, int frame_count, int channels);

    // Reset all effects in chain
    void reset();

    // Get total latency of all effects in frames
    int get_total_latency_frames() const;

    // Enable/disable entire chain
    void set_enabled(bool enabled);
    bool is_enabled() const;

    // Set master output gain for the chain
    void set_output_gain(float gain);
    float get_output_gain() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

// ============================================================================
// Built-in Audio Effects
// ============================================================================

// Gain effect - adjusts volume
class AudioEffectGain : public IAudioEffect {
public:
    static AudioEffectGain* create(float gain_db = 0.0f);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Gain; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;

    // Set gain in decibels (-inf to +inf, 0 = unity)
    void set_gain_db(float db);
    float get_gain_db() const;

    // Set gain as linear multiplier (0.0 to inf, 1.0 = unity)
    void set_gain_linear(float gain);
    float get_gain_linear() const;

    struct Impl;
private:
    AudioEffectGain() = default;
    ~AudioEffectGain() = default;
    Impl* impl_ = nullptr;
};

// Pan effect - stereo panning
class AudioEffectPan : public IAudioEffect {
public:
    static AudioEffectPan* create(float pan = 0.0f);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Pan; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;

    // Set pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
    void set_pan(float pan);
    float get_pan() const;

    // Set pan law in dB (typically -3.0 or -4.5 for constant power)
    void set_pan_law_db(float db);

    struct Impl;
private:
    AudioEffectPan() = default;
    ~AudioEffectPan() = default;
    Impl* impl_ = nullptr;
};

// Delay effect - simple delay line
class AudioEffectDelay : public IAudioEffect {
public:
    // max_delay_ms: maximum delay time in milliseconds
    static AudioEffectDelay* create(int sample_rate, float max_delay_ms = 1000.0f);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Delay; }
    int get_latency_frames() const override;
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;
    float get_mix() const override;
    void set_mix(float mix) override;

    // Set delay time in milliseconds
    void set_delay_ms(float ms);
    float get_delay_ms() const;

    // Set feedback amount (0.0 = no feedback, 1.0 = infinite)
    void set_feedback(float feedback);
    float get_feedback() const;

    struct Impl;
private:
    AudioEffectDelay() = default;
    ~AudioEffectDelay() = default;
    Impl* impl_ = nullptr;
};

// Biquad filter - versatile IIR filter
class AudioEffectBiquadFilter : public IAudioEffect {
public:
    static AudioEffectBiquadFilter* create(int sample_rate, BiquadFilterType type = BiquadFilterType::LowPass);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override;
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;

    // Set filter type
    void set_filter_type(BiquadFilterType type);
    BiquadFilterType get_filter_type() const;

    // Set cutoff/center frequency in Hz
    void set_frequency(float hz);
    float get_frequency() const;

    // Set Q factor (resonance)
    void set_q(float q);
    float get_q() const;

    // Set gain in dB (for peaking/shelf filters)
    void set_gain_db(float db);
    float get_gain_db() const;

    struct Impl;
private:
    AudioEffectBiquadFilter() = default;
    ~AudioEffectBiquadFilter() = default;
    Impl* impl_ = nullptr;
};

// Compressor effect - dynamics compression
class AudioEffectCompressor : public IAudioEffect {
public:
    static AudioEffectCompressor* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Compressor; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;
    float get_mix() const override;
    void set_mix(float mix) override;

    // Set threshold in dB (compression starts above this level)
    void set_threshold_db(float db);
    float get_threshold_db() const;

    // Set ratio (e.g., 4.0 = 4:1 compression)
    void set_ratio(float ratio);
    float get_ratio() const;

    // Set attack time in milliseconds
    void set_attack_ms(float ms);
    float get_attack_ms() const;

    // Set release time in milliseconds
    void set_release_ms(float ms);
    float get_release_ms() const;

    // Set makeup gain in dB
    void set_makeup_gain_db(float db);
    float get_makeup_gain_db() const;

    // Set knee width in dB (0 = hard knee)
    void set_knee_db(float db);
    float get_knee_db() const;

    // Get current gain reduction in dB
    float get_gain_reduction_db() const;

    struct Impl;
private:
    AudioEffectCompressor() = default;
    ~AudioEffectCompressor() = default;
    Impl* impl_ = nullptr;
};

// Limiter effect - hard/soft limiting
class AudioEffectLimiter : public IAudioEffect {
public:
    static AudioEffectLimiter* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Limiter; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;

    // Set ceiling in dB (maximum output level)
    void set_ceiling_db(float db);
    float get_ceiling_db() const;

    // Set release time in milliseconds
    void set_release_ms(float ms);
    float get_release_ms() const;

    // Get current gain reduction in dB
    float get_gain_reduction_db() const;

    struct Impl;
private:
    AudioEffectLimiter() = default;
    ~AudioEffectLimiter() = default;
    Impl* impl_ = nullptr;
};

// Noise gate effect - reduces noise below threshold
class AudioEffectNoiseGate : public IAudioEffect {
public:
    static AudioEffectNoiseGate* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::NoiseGate; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;

    // Set threshold in dB (gate opens above this level)
    void set_threshold_db(float db);
    float get_threshold_db() const;

    // Set attack time in milliseconds (how fast gate opens)
    void set_attack_ms(float ms);
    float get_attack_ms() const;

    // Set hold time in milliseconds (minimum time gate stays open)
    void set_hold_ms(float ms);
    float get_hold_ms() const;

    // Set release time in milliseconds (how fast gate closes)
    void set_release_ms(float ms);
    float get_release_ms() const;

    // Set range in dB (how much to attenuate when closed, 0 = full mute)
    void set_range_db(float db);
    float get_range_db() const;

    // Get current gate state (0.0 = closed, 1.0 = open)
    float get_gate_level() const;

    struct Impl;
private:
    AudioEffectNoiseGate() = default;
    ~AudioEffectNoiseGate() = default;
    Impl* impl_ = nullptr;
};

// Reverb effect - simple algorithmic reverb
class AudioEffectReverb : public IAudioEffect {
public:
    static AudioEffectReverb* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Reverb; }
    int get_latency_frames() const override;
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;
    float get_mix() const override;
    void set_mix(float mix) override;

    // Set room size (0.0 = small, 1.0 = large)
    void set_room_size(float size);
    float get_room_size() const;

    // Set damping (0.0 = bright, 1.0 = dark)
    void set_damping(float damping);
    float get_damping() const;

    // Set stereo width (0.0 = mono, 1.0 = full stereo)
    void set_width(float width);
    float get_width() const;

    // Set pre-delay in milliseconds
    void set_pre_delay_ms(float ms);
    float get_pre_delay_ms() const;

    // Freeze mode (infinite decay)
    void set_freeze(bool freeze);
    bool is_frozen() const;

    struct Impl;
private:
    AudioEffectReverb() = default;
    ~AudioEffectReverb() = default;
    Impl* impl_ = nullptr;
};

// Chorus effect - modulated delay for richness
class AudioEffectChorus : public IAudioEffect {
public:
    static AudioEffectChorus* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Chorus; }
    int get_latency_frames() const override;
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;
    float get_mix() const override;
    void set_mix(float mix) override;

    // Set LFO rate in Hz (modulation speed)
    void set_rate_hz(float hz);
    float get_rate_hz() const;

    // Set depth (modulation amount, 0.0 to 1.0)
    void set_depth(float depth);
    float get_depth() const;

    // Set delay time in milliseconds (center delay)
    void set_delay_ms(float ms);
    float get_delay_ms() const;

    // Set feedback amount (-1.0 to 1.0)
    void set_feedback(float feedback);
    float get_feedback() const;

    // Set number of voices (1-4)
    void set_voices(int voices);
    int get_voices() const;

    struct Impl;
private:
    AudioEffectChorus() = default;
    ~AudioEffectChorus() = default;
    Impl* impl_ = nullptr;
};

// Distortion effect - overdrive/distortion
class AudioEffectDistortion : public IAudioEffect {
public:
    // Distortion algorithm types
    enum class Mode : uint8_t {
        SoftClip,       // Soft clipping (warm overdrive)
        HardClip,       // Hard clipping (harsh distortion)
        Tanh,           // Hyperbolic tangent (tube-like)
        Foldback,       // Foldback distortion (synth-like)
        Bitcrush        // Bit reduction (lo-fi)
    };

    static AudioEffectDistortion* create(int sample_rate);
    void destroy();

    void process(float* samples, int frame_count, int channels) override;
    void reset() override;
    AudioEffectType get_type() const override { return AudioEffectType::Distortion; }
    bool is_enabled() const override;
    void set_enabled(bool enabled) override;
    float get_mix() const override;
    void set_mix(float mix) override;

    // Set distortion mode
    void set_mode(Mode mode);
    Mode get_mode() const;

    // Set drive amount (1.0 = unity, higher = more distortion)
    void set_drive(float drive);
    float get_drive() const;

    // Set output level (0.0 to 1.0)
    void set_output_level(float level);
    float get_output_level() const;

    // Set tone (for built-in filter, 0.0 = dark, 1.0 = bright)
    void set_tone(float tone);
    float get_tone() const;

    // Bitcrush-specific: set bit depth (1-16)
    void set_bit_depth(int bits);
    int get_bit_depth() const;

    // Bitcrush-specific: set sample rate reduction factor
    void set_downsample(int factor);
    int get_downsample() const;

    struct Impl;
private:
    AudioEffectDistortion() = default;
    ~AudioEffectDistortion() = default;
    Impl* impl_ = nullptr;
};

// ============================================================================
// AudioConfig - Audio Configuration for Config Files
// ============================================================================

struct AudioConfig {
    AudioBackend backend = AudioBackend::Auto;
    int output_device_index = -1;           // -1 = default device
    int input_device_index = -1;            // -1 = default device
    std::string output_device_name;
    std::string input_device_name;
    int sample_rate = 48000;
    int channels = 2;
    SampleFormat sample_format = SampleFormat::Float32;
    int buffer_frames = 0;                  // 0 = auto
    bool exclusive_mode = false;
    float master_volume = 1.0f;

    // Create default configuration
    static AudioConfig default_config() {
        return AudioConfig{};
    }

    // Save audio config to INI file
    bool save(const char* filepath) const;

    // Load audio config from INI file
    static bool load(const char* filepath, AudioConfig* out_config);

    // Validate and fix invalid settings
    bool validate();
};

// ============================================================================
// AudioManager - Global Audio Management
// ============================================================================

class AudioManager {
public:
    // Initialize the audio system with the specified backend
    static AudioResult initialize(AudioBackend backend = AudioBackend::Auto);

    // Shutdown the audio system
    static void shutdown();

    // Check if audio system is initialized
    static bool is_initialized();

    // Get the current audio backend
    static AudioBackend get_backend();

    // Get backend name string
    static const char* get_backend_name();

    // Check if a backend is supported
    static bool is_backend_supported(AudioBackend backend);

    // Get the default backend for the current platform
    static AudioBackend get_default_backend();

    // Enumerate audio devices
    static int enumerate_devices(AudioDeviceType type, AudioDeviceEnumeration* out);

    // Get the default device info
    static bool get_default_device(AudioDeviceType type, AudioDeviceInfo* out);

    // Get device info by index
    static bool get_device_info(int device_index, AudioDeviceType type, AudioDeviceInfo* out);

    // Check if a format is supported by a device
    static bool is_format_supported(int device_index, const AudioFormat& format, AudioStreamMode mode);

    // Get the preferred format for a device
    static AudioFormat get_preferred_format(int device_index, AudioDeviceType type);

    // Session event handling (primarily Apple platforms)
    // Register a handler to receive audio session events
    // Returns true if handler was registered successfully
    static bool register_session_event_handler(IAudioSessionEventHandler* handler);

    // Unregister a previously registered handler
    static void unregister_session_event_handler(IAudioSessionEventHandler* handler);

    // Check if session events are supported on current platform/backend
    static bool are_session_events_supported();

private:
    AudioManager() = delete;
    ~AudioManager() = delete;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Get channel count from layout
inline int channel_count_from_layout(ChannelLayout layout) {
    switch (layout) {
        case ChannelLayout::Mono:       return 1;
        case ChannelLayout::Stereo:     return 2;
        case ChannelLayout::Surround21: return 3;
        case ChannelLayout::Surround40: return 4;
        case ChannelLayout::Surround41: return 5;
        case ChannelLayout::Surround51: return 6;
        case ChannelLayout::Surround71: return 8;
        default: return 0;
    }
}

// Get layout from channel count
inline ChannelLayout layout_from_channel_count(int channels) {
    switch (channels) {
        case 1: return ChannelLayout::Mono;
        case 2: return ChannelLayout::Stereo;
        case 3: return ChannelLayout::Surround21;
        case 4: return ChannelLayout::Surround40;
        case 5: return ChannelLayout::Surround41;
        case 6: return ChannelLayout::Surround51;
        case 8: return ChannelLayout::Surround71;
        default: return ChannelLayout::Unknown;
    }
}

// Convert samples between formats
void convert_samples(const void* src, SampleFormat src_format,
                     void* dst, SampleFormat dst_format,
                     int sample_count);

// Mix samples (additive)
void mix_samples(const void* src, void* dst, SampleFormat format,
                 int sample_count, float volume = 1.0f);

// Apply volume to samples
void apply_volume(void* data, SampleFormat format, int sample_count, float volume);

// Interleave/deinterleave channels
void interleave_channels(const float* const* src, float* dst, int channels, int frames);
void deinterleave_channels(const float* src, float** dst, int channels, int frames);

} // namespace audio
} // namespace window

#endif // WINDOW_AUDIO_HPP
