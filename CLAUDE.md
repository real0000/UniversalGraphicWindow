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
```

### CMake Options

- `-DWINDOW_BUILD_EXAMPLES=ON|OFF` - Build examples (default: ON)
- `-DWINDOW_USE_WAYLAND=ON` - Use Wayland instead of X11 on Linux
- `-DWINDOW_ENABLE_OPENGL=ON|OFF` - OpenGL support
- `-DWINDOW_ENABLE_VULKAN=ON|OFF` - Vulkan support
- `-DWINDOW_ENABLE_D3D11=ON|OFF` - Direct3D 11 (Windows only)
- `-DWINDOW_ENABLE_D3D12=ON|OFF` - Direct3D 12 (Windows only)
- `-DWINDOW_ENABLE_METAL=ON|OFF` - Metal (Apple only)

Example executables are built as `example_basic`, `example_opengl`, `example_vulkan`, `example_d3d11`, `example_d3d12`, `example_metal` in the build directory.

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
- **`api/glad.c`, `api/glad.h`, `api/khrplatform.h`** - OpenGL loader (GLAD)
- **`CMakeLists.txt`** - Build config with platform detection (lines 20-80) and graphics backend setup

### Platform Detection

CMake sets `WINDOW_PLATFORM` and selects the appropriate source file:
- Windows: `window/window_win32.cpp` (or `window/window_uwp.cpp` for UWP)
- macOS: `window/window_macos.mm`
- iOS: `window/window_ios.mm`
- Linux: `window/window_x11.cpp` (or `window/window_wayland.cpp` with `-DWINDOW_USE_WAYLAND=ON`)
- Android: `window/window_android.cpp`

Graphics backends are conditionally enabled via `WINDOW_SUPPORT_*` defines.

### Adding New Code

- **Public API changes**: Edit `window.hpp`. Keep ABI-stable, avoid heavy dependencies.
- **New platform**: Add `window/window_<platform>.cpp`, update CMakeLists platform detection branch.
- **New graphics backend**: Add `api/api_<backend>.cpp`, follow existing patterns for `WINDOW_SUPPORT_*` guards.
- **Apple platforms**: Objective-C++ files (`.mm`) are auto-configured with `-x objective-c++`.

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
│   ├── glad.c
│   ├── glad.h
│   └── khrplatform.h
├── examples/           # Example applications
└── tests/              # Unit tests
```

### File Naming

- Platform implementations: `window/window_<lowercase_platform>.cpp` or `.mm`
- Graphics backends: `api/api_<backend>.cpp` or `.mm`
- Library target: `window` (static)
