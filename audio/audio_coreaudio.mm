/*
 * audio_coreaudio.mm - macOS/iOS CoreAudio Backend
 *
 * Implements audio using Apple's AudioToolbox framework.
 */

#if (defined(WINDOW_PLATFORM_MACOS) || defined(WINDOW_PLATFORM_IOS)) && defined(WINDOW_SUPPORT_COREAUDIO)

#include "audio.hpp"
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>

namespace window {
namespace audio {

// ============================================================================
// Global State
// ============================================================================

static struct {
    bool initialized = false;
    AudioBackend backend = AudioBackend::CoreAudio;
    std::mutex mutex;
} g_audio_state;

// ============================================================================
// AudioStream::Impl
// ============================================================================

struct AudioStream::Impl {
    AudioComponentInstance audio_unit = nullptr;
    AudioStreamConfig config;
    AudioFormat actual_format;
    int actual_buffer_frames = 512;

    IAudioCallback* callback = nullptr;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> running{false};
    std::atomic<AudioStreamState> state{AudioStreamState::Stopped};

    uint64_t frame_position = 0;
    double stream_start_time = 0.0;

    static OSStatus render_callback(void* inRefCon,
                                    AudioUnitRenderActionFlags* ioActionFlags,
                                    const AudioTimeStamp* inTimeStamp,
                                    UInt32 inBusNumber,
                                    UInt32 inNumberFrames,
                                    AudioBufferList* ioData);
};

OSStatus AudioStream::Impl::render_callback(void* inRefCon,
                                            AudioUnitRenderActionFlags* ioActionFlags,
                                            const AudioTimeStamp* inTimeStamp,
                                            UInt32 inBusNumber,
                                            UInt32 inNumberFrames,
                                            AudioBufferList* ioData) {
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;

    AudioStream::Impl* impl = static_cast<AudioStream::Impl*>(inRefCon);
    if (!impl || !impl->callback) {
        // Fill with silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; ++i) {
            memset(ioData->mBuffers[i].mData, 0, ioData->mBuffers[i].mDataByteSize);
        }
        return noErr;
    }

    // Prepare audio buffer
    AudioBuffer audio_buf;
    audio_buf.data = ioData->mBuffers[0].mData;
    audio_buf.frame_count = static_cast<int>(inNumberFrames);
    audio_buf.channel_count = impl->actual_format.channels;
    audio_buf.format = impl->actual_format.sample_format;

    // Get stream time
    AudioStreamTime stream_time;
    stream_time.frame_position = impl->frame_position;
    // TODO: Calculate actual stream time

    // Call user callback
    bool continue_playback = impl->callback->on_audio_playback(audio_buf, stream_time);

    // Apply volume
    float vol = impl->volume.load();
    if (vol < 0.999f) {
        apply_volume(ioData->mBuffers[0].mData, impl->actual_format.sample_format,
                     inNumberFrames * impl->actual_format.channels, vol);
    }

    impl->frame_position += inNumberFrames;

    if (!continue_playback) {
        *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
    }

    return noErr;
}

// ============================================================================
// AudioStream Implementation
// ============================================================================

AudioStream* AudioStream::create(const AudioStreamConfig& config, AudioResult* out_result) {
    if (!g_audio_state.initialized) {
        if (out_result) *out_result = AudioResult::ErrorNotInitialized;
        return nullptr;
    }

    @autoreleasepool {
        AudioStream* stream = new AudioStream();
        stream->impl_ = new AudioStream::Impl();
        stream->impl_->config = config;

        // Set up audio component description
        AudioComponentDescription desc = {
            .componentType = kAudioUnitType_Output,
#if TARGET_OS_IPHONE
            .componentSubType = kAudioUnitSubType_RemoteIO,
#else
            .componentSubType = kAudioUnitSubType_DefaultOutput,
#endif
            .componentManufacturer = kAudioUnitManufacturer_Apple,
            .componentFlags = 0,
            .componentFlagsMask = 0
        };

        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) {
            if (out_result) *out_result = AudioResult::ErrorDeviceNotFound;
            delete stream->impl_;
            delete stream;
            return nullptr;
        }

        OSStatus status = AudioComponentInstanceNew(component, &stream->impl_->audio_unit);
        if (status != noErr) {
            if (out_result) *out_result = AudioResult::ErrorDeviceNotFound;
            delete stream->impl_;
            delete stream;
            return nullptr;
        }

        // Set up audio format
        AudioStreamBasicDescription asbd = {};
        asbd.mSampleRate = config.format.sample_rate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mBitsPerChannel = 32;
        asbd.mChannelsPerFrame = config.format.channels;
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = asbd.mChannelsPerFrame * (asbd.mBitsPerChannel / 8);
        asbd.mBytesPerPacket = asbd.mBytesPerFrame * asbd.mFramesPerPacket;

        status = AudioUnitSetProperty(stream->impl_->audio_unit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Input,
                                      0, &asbd, sizeof(asbd));
        if (status != noErr) {
            AudioComponentInstanceDispose(stream->impl_->audio_unit);
            if (out_result) *out_result = AudioResult::ErrorFormatNotSupported;
            delete stream->impl_;
            delete stream;
            return nullptr;
        }

        // Set render callback
        AURenderCallbackStruct callback_struct = {
            .inputProc = &AudioStream::Impl::render_callback,
            .inputProcRefCon = stream->impl_
        };

        status = AudioUnitSetProperty(stream->impl_->audio_unit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0, &callback_struct, sizeof(callback_struct));
        if (status != noErr) {
            AudioComponentInstanceDispose(stream->impl_->audio_unit);
            if (out_result) *out_result = AudioResult::ErrorUnknown;
            delete stream->impl_;
            delete stream;
            return nullptr;
        }

        // Initialize audio unit
        status = AudioUnitInitialize(stream->impl_->audio_unit);
        if (status != noErr) {
            AudioComponentInstanceDispose(stream->impl_->audio_unit);
            if (out_result) *out_result = AudioResult::ErrorUnknown;
            delete stream->impl_;
            delete stream;
            return nullptr;
        }

        stream->impl_->actual_format = config.format;
        stream->impl_->actual_format.sample_format = SampleFormat::Float32;

        if (out_result) *out_result = AudioResult::Success;
        return stream;
    }
}

void AudioStream::destroy() {
    if (!impl_) return;

    stop();

    if (impl_->audio_unit) {
        AudioUnitUninitialize(impl_->audio_unit);
        AudioComponentInstanceDispose(impl_->audio_unit);
    }

    delete impl_;
    impl_ = nullptr;
    delete this;
}

AudioResult AudioStream::start() {
    if (!impl_ || !impl_->audio_unit) return AudioResult::ErrorNotInitialized;
    if (impl_->running) return AudioResult::ErrorStreamAlreadyRunning;

    OSStatus status = AudioOutputUnitStart(impl_->audio_unit);
    if (status != noErr) {
        return AudioResult::ErrorUnknown;
    }

    impl_->running = true;
    impl_->state = AudioStreamState::Running;
    return AudioResult::Success;
}

AudioResult AudioStream::stop() {
    if (!impl_) return AudioResult::ErrorNotInitialized;
    if (!impl_->running) return AudioResult::Success;

    if (impl_->audio_unit) {
        AudioOutputUnitStop(impl_->audio_unit);
    }

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
    return 0.01; // Approximate 10ms latency
}

double AudioStream::get_input_latency() const {
    return 0.01;
}

AudioStreamTime AudioStream::get_stream_time() const {
    AudioStreamTime time;
    if (impl_) {
        time.frame_position = impl_->frame_position;
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
    @autoreleasepool {
        if (!filepath) {
            if (out_result) *out_result = AudioResult::ErrorInvalidParameter;
            return nullptr;
        }

        NSString* path = [NSString stringWithUTF8String:filepath];
        NSURL* url = [NSURL fileURLWithPath:path];

        ExtAudioFileRef audio_file = nullptr;
        OSStatus status = ExtAudioFileOpenURL((__bridge CFURLRef)url, &audio_file);
        if (status != noErr) {
            if (out_result) *out_result = AudioResult::ErrorFileNotFound;
            return nullptr;
        }

        // Get file format
        AudioStreamBasicDescription file_format;
        UInt32 prop_size = sizeof(file_format);
        status = ExtAudioFileGetProperty(audio_file,
                                         kExtAudioFileProperty_FileDataFormat,
                                         &prop_size, &file_format);
        if (status != noErr) {
            ExtAudioFileDispose(audio_file);
            if (out_result) *out_result = AudioResult::ErrorFileFormat;
            return nullptr;
        }

        // Get frame count
        SInt64 total_frames = 0;
        prop_size = sizeof(total_frames);
        ExtAudioFileGetProperty(audio_file,
                                kExtAudioFileProperty_FileLengthFrames,
                                &prop_size, &total_frames);

        // Set up output format (Float32)
        AudioStreamBasicDescription output_format = {};
        output_format.mSampleRate = file_format.mSampleRate;
        output_format.mFormatID = kAudioFormatLinearPCM;
        output_format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        output_format.mBitsPerChannel = 32;
        output_format.mChannelsPerFrame = file_format.mChannelsPerFrame;
        output_format.mFramesPerPacket = 1;
        output_format.mBytesPerFrame = output_format.mChannelsPerFrame * 4;
        output_format.mBytesPerPacket = output_format.mBytesPerFrame;

        status = ExtAudioFileSetProperty(audio_file,
                                         kExtAudioFileProperty_ClientDataFormat,
                                         sizeof(output_format), &output_format);
        if (status != noErr) {
            ExtAudioFileDispose(audio_file);
            if (out_result) *out_result = AudioResult::ErrorFileFormat;
            return nullptr;
        }

        // Allocate buffer and read data
        size_t data_size = total_frames * output_format.mBytesPerFrame;
        std::vector<uint8_t> audio_data(data_size);

        AudioBufferList buffer_list;
        buffer_list.mNumberBuffers = 1;
        buffer_list.mBuffers[0].mNumberChannels = output_format.mChannelsPerFrame;
        buffer_list.mBuffers[0].mDataByteSize = static_cast<UInt32>(data_size);
        buffer_list.mBuffers[0].mData = audio_data.data();

        UInt32 frames_to_read = static_cast<UInt32>(total_frames);
        status = ExtAudioFileRead(audio_file, &frames_to_read, &buffer_list);
        ExtAudioFileDispose(audio_file);

        if (status != noErr) {
            if (out_result) *out_result = AudioResult::ErrorFileFormat;
            return nullptr;
        }

        // Create clip
        AudioClip* clip = new AudioClip();
        clip->impl_ = new AudioClip::Impl();
        clip->impl_->format.sample_format = SampleFormat::Float32;
        clip->impl_->format.sample_rate = static_cast<int>(output_format.mSampleRate);
        clip->impl_->format.channels = static_cast<int>(output_format.mChannelsPerFrame);
        clip->impl_->format.layout = layout_from_channel_count(clip->impl_->format.channels);
        clip->impl_->frame_count = static_cast<int>(frames_to_read);
        clip->impl_->data = std::move(audio_data);

        if (out_result) *out_result = AudioResult::Success;
        return clip;
    }
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
// AudioPlayer Implementation (Stub - uses AudioStream internally)
// ============================================================================

struct AudioPlayer::Impl {
    // Simplified implementation - would need full mixing in production
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
    // Stub - would require full mixing implementation
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

AudioResult AudioManager::initialize(AudioBackend backend) {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (g_audio_state.initialized) {
        return AudioResult::ErrorAlreadyInitialized;
    }

    if (backend != AudioBackend::Auto && backend != AudioBackend::CoreAudio) {
        return AudioResult::ErrorBackendNotSupported;
    }

#if TARGET_OS_IPHONE
    @autoreleasepool {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSError* error = nil;
        [session setCategory:AVAudioSessionCategoryPlayback error:&error];
        [session setActive:YES error:&error];
    }
#endif

    g_audio_state.backend = AudioBackend::CoreAudio;
    g_audio_state.initialized = true;

    return AudioResult::Success;
}

void AudioManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);
    g_audio_state.initialized = false;
}

bool AudioManager::is_initialized() {
    return g_audio_state.initialized;
}

AudioBackend AudioManager::get_backend() {
    return g_audio_state.backend;
}

const char* AudioManager::get_backend_name() {
    return "CoreAudio";
}

bool AudioManager::is_backend_supported(AudioBackend backend) {
    return backend == AudioBackend::Auto || backend == AudioBackend::CoreAudio;
}

AudioBackend AudioManager::get_default_backend() {
    return AudioBackend::CoreAudio;
}

int AudioManager::enumerate_devices(AudioDeviceType type, AudioDeviceEnumeration* out) {
    if (!out) return 0;
    out->count = 0;

#if !TARGET_OS_IPHONE
    // macOS: Use AudioObjectGetPropertyData
    AudioObjectPropertyAddress property_address = {
        .mSelector = (type == AudioDeviceType::Input)
                     ? kAudioHardwarePropertyDefaultInputDevice
                     : kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    AudioDeviceID device_id = 0;
    UInt32 size = sizeof(device_id);
    OSStatus status = AudioObjectGetPropertyData(kAudioObjectSystemObject,
                                                 &property_address,
                                                 0, nullptr, &size, &device_id);
    if (status == noErr && device_id != 0) {
        AudioDeviceInfo& info = out->devices[0];
        info.type = type;
        info.is_default = true;

        // Get device name
        CFStringRef name_ref = nullptr;
        property_address.mSelector = kAudioDevicePropertyDeviceNameCFString;
        size = sizeof(name_ref);
        status = AudioObjectGetPropertyData(device_id, &property_address,
                                            0, nullptr, &size, &name_ref);
        if (status == noErr && name_ref) {
            CFStringGetCString(name_ref, info.name, sizeof(info.name), kCFStringEncodingUTF8);
            CFRelease(name_ref);
        }

        // Get sample rate
        Float64 sample_rate = 0;
        property_address.mSelector = kAudioDevicePropertyNominalSampleRate;
        size = sizeof(sample_rate);
        AudioObjectGetPropertyData(device_id, &property_address, 0, nullptr, &size, &sample_rate);
        info.min_sample_rate = static_cast<int>(sample_rate);
        info.max_sample_rate = static_cast<int>(sample_rate);

        out->count = 1;
    }
#else
    // iOS: Report a single default device
    AudioDeviceInfo& info = out->devices[0];
    strncpy(info.name, "Default Audio Device", sizeof(info.name) - 1);
    info.type = type;
    info.is_default = true;
    info.min_sample_rate = 44100;
    info.max_sample_rate = 48000;
    out->count = 1;
#endif

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
    // CoreAudio supports most common formats
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

#endif // (WINDOW_PLATFORM_MACOS || WINDOW_PLATFORM_IOS) && WINDOW_SUPPORT_COREAUDIO
