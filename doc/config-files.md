# Config files

Both the graphics and audio subsystems can save and restore their settings
to plain INI files. Useful for shipping a "Settings" dialog, or for letting
users tweak resolution / VSync / device selection without code changes.

The parser is Boost.PropertyTree (already a dependency of the library) and
the format is plain INI — easy to hand-edit.

## Graphics config

Save and load the whole `Config` (graphics settings + window list):

```cpp
window::Config config;
window::Config::load("game.ini", &config);

// tweak in code if you want
config.windows[0].fullscreen = true;

config.save("game.ini");
```

Or skip the loading boilerplate and create windows straight from a file:

```cpp
window::Result r;
auto windows = window::Window::create_from_config("game.ini", &r);
```

### File format

```ini
[graphics]
backend       = auto         ; auto, opengl, vulkan, d3d11, d3d12, metal
device_index  = -1
device_name   =              ; if non-empty, takes precedence over device_index
swap_mode     = auto         ; auto, fifo, fifo_relaxed, mailbox, immediate
vsync         = true         ; used when swap_mode = auto
samples       = 1
back_buffers  = 2
color_bits    = 32
depth_bits    = 24
stencil_bits  = 8

[window.main]
title      = Main Window
monitor    = 0
x          = -1              ; -1 = let the OS centre it
y          = -1
width      = 1280
height     = 720
fullscreen = false
visible    = true
resizable  = true
style      = default         ; default, none, tool, ...

[window.tools]
title   = Tools
monitor = 1
width   = 480
height  = 800
```

Each `[window.<name>]` section corresponds to one `WindowConfigEntry`. The
`name` after the dot is also the entry's `name` field, so
`config.find_window("tools")` looks up the right entry after loading.

### Validation

`config.validate()` returns `true` when the values are internally consistent
(monitor index in range, dimensions positive, style flags legal, ...). Call
it after loading and before passing to `Window::create`.

## Audio config

Same shape, separate file. See [Audio](audio.md) for the API itself.

```cpp
window::audio::AudioConfig audio;
window::audio::AudioConfig::load("audio.ini", &audio);
audio.master_volume = 0.8f;
audio.save("audio.ini");
```

### File format

```ini
[audio]
backend             = auto    ; auto, wasapi, coreaudio, pulseaudio, alsa,
                              ; aaudio, webaudio, openal
output_device_index = -1      ; -1 = system default
output_device_name  =
input_device_index  = -1
input_device_name   =
sample_rate         = 48000   ; 8000–192000
channels            = 2       ; 1–8
sample_format       = float32 ; int16, int24, int32, float32
buffer_frames       = 0       ; 0 = backend-chosen
exclusive_mode      = false   ; WASAPI exclusive, low latency
master_volume       = 1.0     ; 0.0–1.0
```

The `output_device_name` and `input_device_name` fields, if non-empty, are
matched against names returned by
`AudioManager::enumerate_devices(...)`. They survive device-index reshuffles
better than the integer index, which is why the loader prefers them.

## Tips

- **Ship a default file** rather than hard-coding fallbacks — users
  appreciate a knob they can edit in Notepad.
- **Round-trip through `validate()`** in your settings UI before saving;
  it'll catch impossible combinations early (e.g. `samples = 17`).
- **Comments survive** because Boost.PropertyTree preserves whitespace —
  but only on lines you don't touch via the API. If you re-save, expect
  a clean rewrite without the original comments.
- **Keep separate files** for graphics and audio. Loading and saving them
  independently means a graphics crash won't blow away the user's audio
  preferences.
