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
- **`window_<platform>.cpp/mm`** - Platform implementations (Win32, X11, macOS, etc.)
- **`api_<backend>.cpp/mm`** - Graphics backend implementations
- **`CMakeLists.txt`** - Build config with platform detection (lines 20-80) and graphics backend setup

### Platform Detection

CMake sets `WINDOW_PLATFORM` and selects the appropriate source file:
- Windows: `window_win32.cpp` (or `window_uwp.cpp` for UWP)
- macOS: `window_macos.mm`
- iOS: `window_ios.mm`
- Linux: `window_x11.cpp` (or `window_wayland.cpp` with `-DWINDOW_USE_WAYLAND=ON`)
- Android: `window_android.cpp`

Graphics backends are conditionally enabled via `WINDOW_SUPPORT_*` defines.

### Adding New Code

- **Public API changes**: Edit `window.hpp`. Keep ABI-stable, avoid heavy dependencies.
- **New platform**: Add `window_<platform>.cpp`, update CMakeLists platform detection branch.
- **New graphics backend**: Add `api_<backend>.cpp`, follow existing patterns for `WINDOW_SUPPORT_*` guards.
- **Apple platforms**: Objective-C++ files (`.mm`) are auto-configured with `-x objective-c++`.

### File Naming

- Platform implementations: `window_<lowercase_platform>.cpp` or `.mm`
- Graphics backends: `api_<backend>.cpp` or `.mm`
- Library target: `window` (static)
