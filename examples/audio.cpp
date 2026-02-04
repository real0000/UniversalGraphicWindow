/*
 * audio.cpp - Audio example
 *
 * Demonstrates basic audio functionality:
 * - Device enumeration
 * - Audio streaming with callback
 * - Simple audio playback
 */

#include "window.hpp"
#include "input/input_keyboard.hpp"
#include "audio/audio.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace window;
using namespace window::input;

// ============================================================================
// Sine Wave Generator Callback
// ============================================================================

class SineWaveCallback : public window::audio::IAudioCallback {
public:
    float frequency = 440.0f;   // A4 note
    float amplitude = 0.3f;
    double phase = 0.0;

    bool on_audio_playback(window::audio::AudioBuffer& output,
                           const window::audio::AudioStreamTime& time) override {
        (void)time;

        float* buffer = static_cast<float*>(output.data);
        int channels = output.channel_count;
        int frames = output.frame_count;

        // Get sample rate from format (assumed 48000 for this example)
        double sample_rate = 48000.0;
        double phase_increment = 2.0 * 3.14159265358979323846 * frequency / sample_rate;

        for (int f = 0; f < frames; ++f) {
            float sample = static_cast<float>(std::sin(phase) * amplitude);
            phase += phase_increment;
            if (phase >= 2.0 * 3.14159265358979323846) {
                phase -= 2.0 * 3.14159265358979323846;
            }

            // Write to all channels
            for (int c = 0; c < channels; ++c) {
                buffer[f * channels + c] = sample;
            }
        }

        return true;  // Continue playback
    }

    void on_audio_error(window::audio::AudioResult error) override {
        printf("Audio error: %s\n", window::audio::audio_result_to_string(error));
    }
};

// ============================================================================
// Keyboard Handler for Audio Control
// ============================================================================

class AudioKeyboardHandler : public IKeyboardHandler {
public:
    Window* window = nullptr;
    SineWaveCallback* audio_callback = nullptr;
    bool muted = false;
    float volume = 0.3f;

    const char* get_handler_id() const override {
        return "audio_keyboard";
    }

    bool on_key(const KeyEvent& event) override {
        if (event.type != EventType::KeyDown) return false;

        switch (event.key) {
            case Key::Num1:
                audio_callback->frequency = 261.63f;  // C4
                printf("Note: C4 (261.63 Hz)\n");
                break;
            case Key::Num2:
                audio_callback->frequency = 329.63f;  // E4
                printf("Note: E4 (329.63 Hz)\n");
                break;
            case Key::Num3:
                audio_callback->frequency = 392.00f;  // G4
                printf("Note: G4 (392.00 Hz)\n");
                break;
            case Key::Num4:
                audio_callback->frequency = 440.00f;  // A4
                printf("Note: A4 (440.00 Hz)\n");
                break;
            case Key::Space:
                muted = !muted;
                audio_callback->amplitude = muted ? 0.0f : volume;
                printf("Mute: %s\n", muted ? "ON" : "OFF");
                break;
            case Key::Up:
                volume = std::min(1.0f, volume + 0.1f);
                if (!muted) audio_callback->amplitude = volume;
                printf("Volume: %.0f%%\n", volume * 100.0f);
                break;
            case Key::Down:
                volume = std::max(0.0f, volume - 0.1f);
                if (!muted) audio_callback->amplitude = volume;
                printf("Volume: %.0f%%\n", volume * 100.0f);
                break;
            case Key::Escape:
                if (window) window->set_should_close(true);
                break;
            default:
                break;
        }
        return false;
    }
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    printf("=== Audio Example ===\n\n");

    // Initialize audio system
    printf("Initializing audio system...\n");
    window::audio::AudioResult result = window::audio::AudioManager::initialize();
    if (result != window::audio::AudioResult::Success) {
        printf("Failed to initialize audio: %s\n",
               window::audio::audio_result_to_string(result));
        return 1;
    }

    printf("Backend: %s\n\n", window::audio::AudioManager::get_backend_name());

    // Enumerate output devices
    printf("Output devices:\n");
    window::audio::AudioDeviceEnumeration output_devices;
    int output_count = window::audio::AudioManager::enumerate_devices(
        window::audio::AudioDeviceType::Output, &output_devices);

    for (int i = 0; i < output_count; ++i) {
        const auto& dev = output_devices.devices[i];
        printf("  [%d] %s%s\n", i, dev.name, dev.is_default ? " (default)" : "");
        printf("      Sample rate: %d-%d Hz, Channels: %d-%d\n",
               dev.min_sample_rate, dev.max_sample_rate,
               dev.min_channels, dev.max_channels);
    }
    printf("\n");

    // Enumerate input devices
    printf("Input devices:\n");
    window::audio::AudioDeviceEnumeration input_devices;
    int input_count = window::audio::AudioManager::enumerate_devices(
        window::audio::AudioDeviceType::Input, &input_devices);

    for (int i = 0; i < input_count; ++i) {
        const auto& dev = input_devices.devices[i];
        printf("  [%d] %s%s\n", i, dev.name, dev.is_default ? " (default)" : "");
    }
    printf("\n");

    // Get preferred format for default device
    window::audio::AudioFormat preferred = window::audio::AudioManager::get_preferred_format(
        -1, window::audio::AudioDeviceType::Output);
    printf("Preferred format: %d Hz, %d channels, %s\n\n",
           preferred.sample_rate, preferred.channels,
           window::audio::sample_format_to_string(preferred.sample_format));

    // Create a window for input handling
    Config config;
    strncpy(config.windows[0].title, "Audio Example - Press 1-4 for notes, ESC to quit",
            MAX_DEVICE_NAME_LENGTH - 1);
    config.windows[0].width = 640;
    config.windows[0].height = 200;

    Result win_result;
    auto windows = Window::create(config, &win_result);
    if (windows.empty()) {
        printf("Failed to create window\n");
        window::audio::AudioManager::shutdown();
        return 1;
    }
    Window* win = windows[0];

    // Create audio stream for sine wave output
    window::audio::AudioStreamConfig stream_config;
    stream_config.format = window::audio::AudioFormat::default_format();
    stream_config.mode = window::audio::AudioStreamMode::Playback;

    window::audio::AudioStream* stream = window::audio::AudioStream::create(stream_config, &result);
    if (!stream) {
        printf("Failed to create audio stream: %s\n",
               window::audio::audio_result_to_string(result));
        win->destroy();
        window::audio::AudioManager::shutdown();
        return 1;
    }

    printf("Stream created:\n");
    printf("  Format: %d Hz, %d channels, %s\n",
           stream->get_format().sample_rate,
           stream->get_format().channels,
           window::audio::sample_format_to_string(stream->get_format().sample_format));
    printf("  Buffer: %d frames\n", stream->get_buffer_frames());
    printf("  Latency: %.1f ms\n\n", stream->get_output_latency() * 1000.0);

    // Set up callback
    SineWaveCallback callback;
    stream->set_callback(&callback);

    // Start stream
    result = stream->start();
    if (result != window::audio::AudioResult::Success) {
        printf("Failed to start stream: %s\n",
               window::audio::audio_result_to_string(result));
        stream->destroy();
        win->destroy();
        window::audio::AudioManager::shutdown();
        return 1;
    }

    printf("Playing sine wave. Press keys:\n");
    printf("  1 - C4 (261.63 Hz)\n");
    printf("  2 - E4 (329.63 Hz)\n");
    printf("  3 - G4 (392.00 Hz)\n");
    printf("  4 - A4 (440.00 Hz)\n");
    printf("  Space - Toggle mute\n");
    printf("  Up/Down - Volume\n");
    printf("  ESC - Quit\n\n");

    // Set up keyboard handler
    AudioKeyboardHandler keyboard_handler;
    keyboard_handler.window = win;
    keyboard_handler.audio_callback = &callback;
    win->add_keyboard_handler(&keyboard_handler);

    // Main loop
    while (!win->should_close()) {
        win->poll_events();
    }

    // Cleanup
    printf("\nShutting down...\n");

    win->remove_keyboard_handler(&keyboard_handler);
    stream->stop();
    stream->destroy();
    win->destroy();
    window::audio::AudioManager::shutdown();

    printf("Done.\n");
    return 0;
}
