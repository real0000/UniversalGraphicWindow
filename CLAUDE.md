# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Windows: prefer explicit generator
cmake -S . -B build -G "Visual Studio 16 2019"
cmake --build build --config Release

# Run tests
cd build && ctest -C Release
```

### CMake Options

- `-DWINDOW_BUILD_EXAMPLES=ON|OFF` - Build examples (default: ON)
- `-DWINDOW_BUILD_TESTS=ON|OFF` - Build unit tests (default: ON)
- `-DWINDOW_USE_WAYLAND=ON` - Use Wayland instead of X11 on Linux
- `-DWINDOW_ENABLE_OPENGL=ON|OFF` - OpenGL support
- `-DWINDOW_ENABLE_VULKAN=ON|OFF` - Vulkan support
- `-DWINDOW_ENABLE_D3D11=ON|OFF` - Direct3D 11 (Windows only)
- `-DWINDOW_ENABLE_D3D12=ON|OFF` - Direct3D 12 (Windows only)
- `-DWINDOW_ENABLE_METAL=ON|OFF` - Metal (Apple only)
- `-DWINDOW_ENABLE_DINPUT=ON` - Use DirectInput for gamepad (Windows, default: XInput)

Example executables are built as `example_basic`, `example_opengl`, `example_vulkan`, `example_d3d11`, `example_d3d12`, `example_metal`, `example_gamepad`, `example_wheel` in the build directory.

### Dependencies

- **C++17** - Required (set in CMakeLists.txt)
- **Boost** - Optional, for INI config parsing via PropertyTree. Falls back to built-in parser if not found.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│         Public API (window.hpp)                         │
│  Config struct, Window class, Graphics interface        │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────┐
│    Platform Layer (window_*.cpp/mm)                     │
│  Win32, UWP, macOS, iOS, Android, X11, Wayland          │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────┴────────────────────────────────┐
│    Graphics Layer (api_*.cpp/mm)                        │
│  OpenGL, OpenGL ES/EGL, Vulkan, D3D11, D3D12, Metal     │
└─────────────────────────────────────────────────────────┘
```

### Key Files

- **`window.hpp`** - Single public header (the only file consumers include)
- **`window/window_<platform>.cpp/mm`** - Platform implementations (Win32, X11, macOS, etc.)
- **`api/api_<backend>.cpp/mm`** - Graphics backend implementations
- **`api/graphics_config.cpp`** - Config save/load (INI format) and multi-window creation
- **`api/glad.c`, `api/glad.h`, `api/khrplatform.h`** - OpenGL loader (GLAD)
- **`CMakeLists.txt`** - Build config with platform detection and graphics backend setup

### Config Structure

The `Config` struct supports multi-window configurations:

```cpp
struct Config {
    // Graphics settings (shared across all windows)
    Backend backend = Backend::Auto;
    int device_index = -1;
    char device_name[MAX_DEVICE_NAME_LENGTH] = {};
    SwapMode swap_mode = SwapMode::Auto;  // Presentation mode
    bool vsync = true;    // Used when swap_mode is Auto
    int samples = 1;
    int back_buffers = 2;
    int color_bits = 32;
    int depth_bits = 24;
    int stencil_bits = 8;
    Graphics* shared_graphics = nullptr;

    // Window configurations
    WindowConfigEntry windows[MAX_CONFIG_WINDOWS];
    int window_count = 1;

    // Methods
    bool save(const char* filepath) const;
    static bool load(const char* filepath, Config* out_config);
    bool validate();
    WindowConfigEntry* find_window(const char* name);
};
```

SwapMode values:
- `Fifo` - VSync ON, wait for vertical blank (no tearing)
- `FifoRelaxed` - Adaptive VSync, may tear if frame is late
- `Mailbox` - Triple buffering, low latency, no tearing
- `Immediate` - VSync OFF, lowest latency, may tear
- `Auto` - Uses `vsync` bool to determine mode

Window-specific settings are in `WindowConfigEntry`:
```cpp
struct WindowConfigEntry {
    char name[MAX_WINDOW_NAME_LENGTH] = "main";
    char title[MAX_DEVICE_NAME_LENGTH] = "Window";
    int monitor_index = 0;
    int x = -1, y = -1;  // -1 = auto-center
    int width = 800, height = 600;
    bool fullscreen = false;
    bool visible = true;
    bool resizable = true;
    WindowStyle style = WindowStyle::Default;
};
```

### Config File Format (INI)

```ini
[graphics]
backend = auto
device_index = -1
device_name =
swap_mode = auto    # fifo, fifo_relaxed, mailbox, immediate, auto
vsync = true        # Used when swap_mode = auto
samples = 1
back_buffers = 2
color_bits = 32
depth_bits = 24
stencil_bits = 8

[window.main]
title = Main Window
monitor = 0
x = -1
y = -1
width = 1280
height = 720
fullscreen = false
style = default

[window.secondary]
title = Secondary Window
monitor = 1
width = 800
height = 600
```

### Multi-Window Creation

```cpp
// Create multiple windows with shared graphics context
std::vector<Window*> windows = Window::create(config, &result);

// Or load from config file
std::vector<Window*> windows = Window::create_from_config("config.ini", &result);
```

### Platform Detection

CMake sets `WINDOW_PLATFORM` and selects the appropriate source file:
- Windows: `window/window_win32.cpp` (or `window/window_uwp.cpp` for UWP)
- macOS: `window/window_macos.mm`
- iOS: `window/window_ios.mm`
- Linux: `window/window_x11.cpp` (or `window/window_wayland.cpp` with `-DWINDOW_USE_WAYLAND=ON`)
- Android: `window/window_android.cpp`

Graphics backends are conditionally enabled via `WINDOW_SUPPORT_*` defines.

### Wayland-Specific Architecture

On Wayland, multi-window support uses a compositor-style architecture:
- A root surface spans all monitors (fullscreen)
- Individual windows are created as subsurfaces of the root
- Input events are routed to the appropriate window via surface lookup
- Uses `wl_subcompositor` for subsurface management

### Adding New Code

- **Public API changes**: Edit `window.hpp`. Keep ABI-stable, avoid heavy dependencies.
- **New platform**: Add `window/window_<platform>.cpp`, update CMakeLists platform detection branch.
- **New graphics backend**: Add `api/api_<backend>.cpp`, follow existing patterns for `WINDOW_SUPPORT_*` guards.
- **Apple platforms**: Objective-C++ files (`.mm`) are auto-configured with `-x objective-c++`.
- **Config changes**: Update `api/graphics_config.cpp` for INI save/load (both Boost and fallback paths).

### Directory Structure

```
UniversalGraphicWindow/
├── window.hpp          # Public API header
├── CMakeLists.txt      # Build configuration
├── window/             # Platform implementations
│   ├── window_win32.cpp
│   ├── window_uwp.cpp
│   ├── window_macos.mm
│   ├── window_ios.mm
│   ├── window_x11.cpp
│   ├── window_wayland.cpp
│   └── window_android.cpp
├── api/                # Graphics backends
│   ├── api_opengl.cpp
│   ├── api_opengl_glx.cpp
│   ├── api_opengl_egl.cpp
│   ├── api_opengl_cocoa.mm
│   ├── api_vulkan.cpp
│   ├── api_d3d11.cpp
│   ├── api_d3d12.cpp
│   ├── api_metal.mm
│   ├── graphics_config.cpp  # Config parsing (Boost/fallback)
│   ├── glad.c
│   ├── glad.h
│   └── khrplatform.h
├── input/              # Input device handling
│   ├── input_keyboard.cpp/.hpp
│   ├── input_mouse.cpp/.hpp
│   ├── input_gamepad.cpp/.hpp
│   ├── input_wheel.cpp/.hpp
│   ├── gamepad_xinput.cpp    # Windows XInput
│   ├── gamepad_dinput.cpp    # Windows DirectInput
│   ├── gamepad_evdev.cpp     # Linux evdev
│   ├── gamepad_gccontroller.mm # Apple GCController
│   └── wheel_*.cpp           # Steering wheel implementations
├── device/             # Device enumeration
│   ├── device_win32.cpp
│   ├── device_linux.cpp
│   ├── device_apple.mm
│   └── device_android.cpp
├── examples/           # Example applications
└── tests/              # Unit tests
```

### File Naming

- Platform implementations: `window/window_<lowercase_platform>.cpp` or `.mm`
- Graphics backends: `api/api_<backend>.cpp` or `.mm`
- Input handlers: `input/<device>_<backend>.cpp`
- Library target: `window` (static)
