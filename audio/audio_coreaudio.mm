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

static constexpr int MAX_SESSION_EVENT_HANDLERS = 16;

static struct {
    bool initialized = false;
    AudioBackend backend = AudioBackend::CoreAudio;
    std::mutex mutex;

    // Session event handlers
    IAudioSessionEventHandler* session_handlers[MAX_SESSION_EVENT_HANDLERS] = {};
    int session_handler_count = 0;
    std::mutex session_mutex;

#if TARGET_OS_IPHONE
    id interruption_observer = nil;
    id route_change_observer = nil;
    id media_reset_observer = nil;
    id silence_secondary_observer = nil;
#endif
} g_audio_state;

// ============================================================================
// Session Event Notification
// ============================================================================

static void notify_session_event(const AudioSessionEventData& event_data) {
    std::lock_guard<std::mutex> lock(g_audio_state.session_mutex);
    for (int i = 0; i < g_audio_state.session_handler_count; ++i) {
        if (g_audio_state.session_handlers[i]) {
            g_audio_state.session_handlers[i]->on_audio_session_event(event_data);
        }
    }
}

#if TARGET_OS_IPHONE
// ============================================================================
// iOS Audio Session Notification Handlers
// ============================================================================

static void setup_ios_session_observers() {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

    // Interruption notifications
    g_audio_state.interruption_observer = [center addObserverForName:AVAudioSessionInterruptionNotification
                                                              object:nil
                                                               queue:[NSOperationQueue mainQueue]
                                                          usingBlock:^(NSNotification* notification) {
        NSDictionary* info = notification.userInfo;
        AVAudioSessionInterruptionType type = (AVAudioSessionInterruptionType)
            [[info objectForKey:AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];

        AudioSessionEventData event_data;

        if (type == AVAudioSessionInterruptionTypeBegan) {
            event_data.event = AudioSessionEvent::InterruptionBegan;
        } else {
            event_data.event = AudioSessionEvent::InterruptionEnded;

            NSNumber* optionNumber = [info objectForKey:AVAudioSessionInterruptionOptionKey];
            if (optionNumber) {
                AVAudioSessionInterruptionOptions options = [optionNumber unsignedIntegerValue];
                if (options & AVAudioSessionInterruptionOptionShouldResume) {
                    event_data.interruption_option = AudioInterruptionOption::ShouldResume;
                }
            }
        }

        notify_session_event(event_data);
    }];

    // Route change notifications
    g_audio_state.route_change_observer = [center addObserverForName:AVAudioSessionRouteChangeNotification
                                                              object:nil
                                                               queue:[NSOperationQueue mainQueue]
                                                          usingBlock:^(NSNotification* notification) {
        NSDictionary* info = notification.userInfo;
        AVAudioSessionRouteChangeReason reason = (AVAudioSessionRouteChangeReason)
            [[info objectForKey:AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];

        AudioSessionEventData event_data;

        switch (reason) {
            case AVAudioSessionRouteChangeReasonNewDeviceAvailable:
                event_data.event = AudioSessionEvent::RouteChangeNewDeviceAvailable;
                event_data.route_change_reason = AudioRouteChangeReason::NewDeviceAvailable;
                break;
            case AVAudioSessionRouteChangeReasonOldDeviceUnavailable:
                event_data.event = AudioSessionEvent::RouteChangeOldDeviceUnavailable;
                event_data.route_change_reason = AudioRouteChangeReason::OldDeviceUnavailable;
                break;
            case AVAudioSessionRouteChangeReasonCategoryChange:
                event_data.event = AudioSessionEvent::RouteChangeCategoryChange;
                event_data.route_change_reason = AudioRouteChangeReason::CategoryChange;
                break;
            case AVAudioSessionRouteChangeReasonOverride:
                event_data.event = AudioSessionEvent::RouteChangeOverride;
                event_data.route_change_reason = AudioRouteChangeReason::Override;
                break;
            case AVAudioSessionRouteChangeReasonWakeFromSleep:
                event_data.event = AudioSessionEvent::RouteChangeWakeFromSleep;
                event_data.route_change_reason = AudioRouteChangeReason::WakeFromSleep;
                break;
            case AVAudioSessionRouteChangeReasonNoSuitableRouteForCategory:
                event_data.event = AudioSessionEvent::RouteChangeNoSuitableRouteForCategory;
                event_data.route_change_reason = AudioRouteChangeReason::NoSuitableRouteForCategory;
                break;
            case AVAudioSessionRouteChangeReasonRouteConfigurationChange:
                event_data.event = AudioSessionEvent::RouteChangeRouteConfigurationChange;
                event_data.route_change_reason = AudioRouteChangeReason::RouteConfigurationChange;
                break;
            default:
                event_data.event = AudioSessionEvent::RouteChangeNewDeviceAvailable;
                event_data.route_change_reason = AudioRouteChangeReason::Unknown;
                break;
        }

        // Get previous route info
        AVAudioSessionRouteDescription* previousRoute = [info objectForKey:AVAudioSessionRouteChangePreviousRouteKey];
        if (previousRoute && previousRoute.outputs.count > 0) {
            AVAudioSessionPortDescription* port = previousRoute.outputs.firstObject;
            if (port.portName) {
                strncpy(event_data.previous_device_name, [port.portName UTF8String],
                        MAX_AUDIO_DEVICE_NAME_LENGTH - 1);
            }
        }

        // Get current route info
        AVAudioSession* session = [AVAudioSession sharedInstance];
        AVAudioSessionRouteDescription* currentRoute = session.currentRoute;
        if (currentRoute && currentRoute.outputs.count > 0) {
            AVAudioSessionPortDescription* port = currentRoute.outputs.firstObject;
            if (port.portName) {
                strncpy(event_data.new_device_name, [port.portName UTF8String],
                        MAX_AUDIO_DEVICE_NAME_LENGTH - 1);
            }
        }

        notify_session_event(event_data);
    }];

    // Media services reset notifications
    g_audio_state.media_reset_observer = [center addObserverForName:AVAudioSessionMediaServicesWereResetNotification
                                                             object:nil
                                                              queue:[NSOperationQueue mainQueue]
                                                         usingBlock:^(NSNotification* notification) {
        (void)notification;
        AudioSessionEventData event_data;
        event_data.event = AudioSessionEvent::MediaServicesWereReset;
        notify_session_event(event_data);
    }];

    // Silence secondary audio hint
    g_audio_state.silence_secondary_observer = [center addObserverForName:AVAudioSessionSilenceSecondaryAudioHintNotification
                                                                   object:nil
                                                                    queue:[NSOperationQueue mainQueue]
                                                               usingBlock:^(NSNotification* notification) {
        NSDictionary* info = notification.userInfo;
        AVAudioSessionSilenceSecondaryAudioHintType type = (AVAudioSessionSilenceSecondaryAudioHintType)
            [[info objectForKey:AVAudioSessionSilenceSecondaryAudioHintTypeKey] unsignedIntegerValue];

        AudioSessionEventData event_data;
        if (type == AVAudioSessionSilenceSecondaryAudioHintTypeBegin) {
            event_data.event = AudioSessionEvent::SilenceSecondaryAudioHintBegan;
        } else {
            event_data.event = AudioSessionEvent::SilenceSecondaryAudioHintEnded;
        }
        notify_session_event(event_data);
    }];
}

static void teardown_ios_session_observers() {
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];

    if (g_audio_state.interruption_observer) {
        [center removeObserver:g_audio_state.interruption_observer];
        g_audio_state.interruption_observer = nil;
    }
    if (g_audio_state.route_change_observer) {
        [center removeObserver:g_audio_state.route_change_observer];
        g_audio_state.route_change_observer = nil;
    }
    if (g_audio_state.media_reset_observer) {
        [center removeObserver:g_audio_state.media_reset_observer];
        g_audio_state.media_reset_observer = nil;
    }
    if (g_audio_state.silence_secondary_observer) {
        [center removeObserver:g_audio_state.silence_secondary_observer];
        g_audio_state.silence_secondary_observer = nil;
    }
}

#else // macOS

// ============================================================================
// macOS Audio Hardware Listeners
// ============================================================================

static OSStatus default_output_device_changed_listener(AudioObjectID inObjectID,
                                                       UInt32 inNumberAddresses,
                                                       const AudioObjectPropertyAddress* inAddresses,
                                                       void* inClientData) {
    (void)inObjectID; (void)inNumberAddresses; (void)inAddresses; (void)inClientData;

    AudioSessionEventData event_data;
    event_data.event = AudioSessionEvent::DefaultOutputDeviceChanged;

    // Get current device name
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    AudioDeviceID device_id = 0;
    UInt32 size = sizeof(device_id);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &device_id) == noErr) {
        CFStringRef name_ref = nullptr;
        addr.mSelector = kAudioDevicePropertyDeviceNameCFString;
        size = sizeof(name_ref);
        if (AudioObjectGetPropertyData(device_id, &addr, 0, nullptr, &size, &name_ref) == noErr && name_ref) {
            CFStringGetCString(name_ref, event_data.new_device_name,
                               MAX_AUDIO_DEVICE_NAME_LENGTH, kCFStringEncodingUTF8);
            CFRelease(name_ref);
        }
    }

    notify_session_event(event_data);
    return noErr;
}

static OSStatus default_input_device_changed_listener(AudioObjectID inObjectID,
                                                      UInt32 inNumberAddresses,
                                                      const AudioObjectPropertyAddress* inAddresses,
                                                      void* inClientData) {
    (void)inObjectID; (void)inNumberAddresses; (void)inAddresses; (void)inClientData;

    AudioSessionEventData event_data;
    event_data.event = AudioSessionEvent::DefaultInputDeviceChanged;

    // Get current device name
    AudioObjectPropertyAddress addr = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };

    AudioDeviceID device_id = 0;
    UInt32 size = sizeof(device_id);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &device_id) == noErr) {
        CFStringRef name_ref = nullptr;
        addr.mSelector = kAudioDevicePropertyDeviceNameCFString;
        size = sizeof(name_ref);
        if (AudioObjectGetPropertyData(device_id, &addr, 0, nullptr, &size, &name_ref) == noErr && name_ref) {
            CFStringGetCString(name_ref, event_data.new_device_name,
                               MAX_AUDIO_DEVICE_NAME_LENGTH, kCFStringEncodingUTF8);
            CFRelease(name_ref);
        }
    }

    notify_session_event(event_data);
    return noErr;
}

static OSStatus device_list_changed_listener(AudioObjectID inObjectID,
                                             UInt32 inNumberAddresses,
                                             const AudioObjectPropertyAddress* inAddresses,
                                             void* inClientData) {
    (void)inObjectID; (void)inNumberAddresses; (void)inAddresses; (void)inClientData;

    AudioSessionEventData event_data;
    event_data.event = AudioSessionEvent::DeviceListChanged;
    notify_session_event(event_data);
    return noErr;
}

static void setup_macos_audio_listeners() {
    // Default output device changed
    AudioObjectPropertyAddress addr_output = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr_output,
                                   default_output_device_changed_listener, nullptr);

    // Default input device changed
    AudioObjectPropertyAddress addr_input = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr_input,
                                   default_input_device_changed_listener, nullptr);

    // Device list changed
    AudioObjectPropertyAddress addr_devices = {
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectAddPropertyListener(kAudioObjectSystemObject, &addr_devices,
                                   device_list_changed_listener, nullptr);
}

static void teardown_macos_audio_listeners() {
    AudioObjectPropertyAddress addr_output = {
        .mSelector = kAudioHardwarePropertyDefaultOutputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr_output,
                                      default_output_device_changed_listener, nullptr);

    AudioObjectPropertyAddress addr_input = {
        .mSelector = kAudioHardwarePropertyDefaultInputDevice,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr_input,
                                      default_input_device_changed_listener, nullptr);

    AudioObjectPropertyAddress addr_devices = {
        .mSelector = kAudioHardwarePropertyDevices,
        .mScope = kAudioObjectPropertyScopeGlobal,
        .mElement = kAudioObjectPropertyElementMain
    };
    AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &addr_devices,
                                      device_list_changed_listener, nullptr);
}

#endif // TARGET_OS_IPHONE

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

        // Set up iOS session observers
        setup_ios_session_observers();
    }
#else
    // Set up macOS audio hardware listeners
    setup_macos_audio_listeners();
#endif

    g_audio_state.backend = AudioBackend::CoreAudio;
    g_audio_state.initialized = true;

    return AudioResult::Success;
}

void AudioManager::shutdown() {
    std::lock_guard<std::mutex> lock(g_audio_state.mutex);

    if (!g_audio_state.initialized) {
        return;
    }

#if TARGET_OS_IPHONE
    @autoreleasepool {
        teardown_ios_session_observers();
    }
#else
    teardown_macos_audio_listeners();
#endif

    // Clear session handlers
    {
        std::lock_guard<std::mutex> session_lock(g_audio_state.session_mutex);
        for (int i = 0; i < MAX_SESSION_EVENT_HANDLERS; ++i) {
            g_audio_state.session_handlers[i] = nullptr;
        }
        g_audio_state.session_handler_count = 0;
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

bool AudioManager::register_session_event_handler(IAudioSessionEventHandler* handler) {
    if (!handler) return false;

    std::lock_guard<std::mutex> lock(g_audio_state.session_mutex);

    // Check if already registered
    for (int i = 0; i < g_audio_state.session_handler_count; ++i) {
        if (g_audio_state.session_handlers[i] == handler) {
            return true;  // Already registered
        }
    }

    // Find empty slot
    if (g_audio_state.session_handler_count >= MAX_SESSION_EVENT_HANDLERS) {
        return false;  // No room
    }

    g_audio_state.session_handlers[g_audio_state.session_handler_count++] = handler;
    return true;
}

void AudioManager::unregister_session_event_handler(IAudioSessionEventHandler* handler) {
    if (!handler) return;

    std::lock_guard<std::mutex> lock(g_audio_state.session_mutex);

    for (int i = 0; i < g_audio_state.session_handler_count; ++i) {
        if (g_audio_state.session_handlers[i] == handler) {
            // Shift remaining handlers down
            for (int j = i; j < g_audio_state.session_handler_count - 1; ++j) {
                g_audio_state.session_handlers[j] = g_audio_state.session_handlers[j + 1];
            }
            g_audio_state.session_handlers[--g_audio_state.session_handler_count] = nullptr;
            return;
        }
    }
}

bool AudioManager::are_session_events_supported() {
    return true;  // CoreAudio supports session events
}

} // namespace audio
} // namespace window

#endif // (WINDOW_PLATFORM_MACOS || WINDOW_PLATFORM_IOS) && WINDOW_SUPPORT_COREAUDIO
