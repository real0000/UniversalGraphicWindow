# Audio

`audio/audio.hpp` is an independent module — you can use it without ever
creating a window. Three abstractions cover almost every use case:

| Class             | Use for                                                  |
|-------------------|----------------------------------------------------------|
| `AudioStream`     | Low-latency I/O with a real-time callback (synth, VOIP). |
| `AudioPlayer`     | Fire-and-forget playback of `AudioClip`s (SFX).          |
| `AudioFileStream` | Streaming a long file off disk (music, ambience).        |

Initialise once at startup, shut down at exit:

```cpp
#include "audio/audio.hpp"
using namespace window::audio;

if (AudioManager::initialize(AudioBackend::Auto) != AudioResult::Success) {
    return 1;
}
// ... use audio ...
AudioManager::shutdown();
```

`AudioManager` also exposes `enumerate_devices(AudioDeviceType, ...)`,
`get_preferred_format(...)`, and `is_format_supported(...)`.

## AudioFormat

```cpp
struct AudioFormat {
    SampleFormat  sample_format;       // Int16 / Int24 / Int32 / Float32
    int           sample_rate;         // 8000–192000
    int           channels;            // 1–8
    ChannelLayout layout;              // Mono, Stereo, Surround51, ...
};
AudioFormat fmt = AudioFormat::default_format();   // 48 kHz stereo float32
AudioFormat cd  = AudioFormat::cd_quality();       // 44.1 kHz stereo int16
```

## Streams (real-time callback)

For anything that needs to *generate* or *consume* audio in real time — a
synth, a mixer, a recorder, voice chat — implement `IAudioCallback` and feed
it to a stream:

```cpp
class Sine : public IAudioCallback {
    double phase = 0.0;

    bool on_audio_playback(AudioBuffer& out, const AudioStreamTime&) override {
        const double step = 2.0 * 3.14159265 * 440.0 / out.format.sample_rate;
        float* dst = static_cast<float*>(out.data);
        for (int i = 0; i < out.frame_count; ++i) {
            float s = static_cast<float>(0.2 * std::sin(phase));
            for (int c = 0; c < out.format.channels; ++c) *dst++ = s;
            phase += step;
        }
        return true;                              // continue
    }
};

AudioStreamConfig cfg;
cfg.format = AudioFormat::default_format();
cfg.mode   = AudioStreamMode::Playback;

AudioResult r;
AudioStream* s = AudioStream::create(cfg, &r);

Sine sine;
s->set_callback(&sine);
s->start();
// ...
s->stop();
s->destroy();
```

The callback runs on a real-time audio thread. Treat it like an interrupt:
no allocations, no locks against non-RT code, no syscalls.

For input (capture), set `cfg.mode = AudioStreamMode::Capture` and override
`on_audio_capture(...)` instead. For full-duplex, use
`AudioStreamMode::Duplex` and override both.

`s->get_output_latency()` reports the round-trip latency in seconds —
typically a few ms on WASAPI/CoreAudio shared mode, sub-millisecond in
exclusive mode.

## Player (sound effects)

For game-style "play this clip now" with no callback boilerplate:

```cpp
AudioResult r;
AudioClip*   shot = AudioClip::load("shot.wav", &r);
AudioPlayer* p    = AudioPlayer::create(/*device*/ -1, &r);

AudioPlayOptions opt;
opt.volume = 0.8f;
opt.pitch  = 1.0f;
auto h = p->play(shot, opt);

// runtime tweaks
p->set_volume(h, 0.5f);
p->set_pan   (h, -1.0f);                   // hard left
p->stop      (h);

p->update();                                // call once per frame
```

`AudioPlayer` mixes any number of overlapping plays internally. Call
`set_master_volume(...)` to scale the whole bus.

## Streaming long files

Don't load 100 MB of music into memory — open it as a stream and let a
helper feed an `AudioStream` for you:

```cpp
AudioResult r;
auto* file = AudioFileStream::open("music.ogg", &r);

StreamingAudioCallback cb;
cb.set_source(file, /*owns*/ true);
cb.set_looping(true);
cb.set_volume(0.7f);

AudioStreamConfig cfg;
cfg.format = file->get_format();
cfg.mode   = AudioStreamMode::Playback;

auto* s = AudioStream::create(cfg, &r);
s->set_callback(&cb);
s->start();
```

Or read frames manually:

```cpp
const auto& fmt = file->get_format();
float buffer[1024 * 2];                     // 1024 stereo frames
while (!file->is_end_of_stream()) {
    int got = file->read_frames(buffer, 1024);
    process(buffer, got, fmt.channels);
}
file->seek(0, AudioSeekOrigin::Begin);      // rewind
file->close();
```

Supported formats depend on which decoder options were enabled at build
time (`WINDOW_ENABLE_MP3_DECODER`, `WINDOW_ENABLE_VORBIS_DECODER`,
`WINDOW_ENABLE_FLAC_DECODER`). WAV is always available.

## Effects

A small effects library is in `audio/audio.hpp`:

```cpp
AudioEffectChain chain;
chain.add_effect(new AudioEffectGain(/*db*/ -6.0f));
chain.add_effect(new AudioEffectBiquadFilter(BiquadType::LowPass, 8000.0f));
chain.add_effect(new AudioEffectReverb(/*room*/ 0.6f, /*wet*/ 0.3f));

// inside your callback:
chain.process(buffer, frame_count, channels);
```

Built-ins include `Gain`, `Pan`, `Delay`, `BiquadFilter`, `Compressor`,
`Limiter`, `NoiseGate`, `Reverb`, `Chorus`, `Distortion`. They share the
`IAudioEffect` interface so you can drop in your own.

## Backend reference

| Platform | Backend     | Notes                         |
|----------|-------------|-------------------------------|
| Windows  | WASAPI      | Shared and exclusive modes.   |
| macOS/iOS| CoreAudio   | AudioUnit-based.              |
| Linux    | PulseAudio  | Default.                      |
| Linux    | ALSA        | Fallback if Pulse missing.    |
| Android  | AAudio      | Android 8.0+.                 |
| WASM     | Web Audio   | AudioWorklet.                 |
| Cross    | OpenAL Soft | Opt-in via `WINDOW_ENABLE_OPENAL`. |

`AudioConfig` mirrors `Config` for graphics — you can save and load audio
settings to INI; see [Config files](config-files.md).

A complete worked example lives in [`examples/audio.cpp`](../examples/audio.cpp).
