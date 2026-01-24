/*
 * window.hpp - Cross-platform window and graphics library
 *
 * Supported platforms:
 *   - Windows (Win32)
 *   - UWP (Windows Runtime)
 *   - X11 (Linux)
 *   - Wayland (Linux)
 *   - iOS (UIKit)
 *   - Android (NativeActivity)
 *   - macOS (Cocoa)
 *
 * Graphics backends (auto-selected per platform):
 *   - OpenGL/OpenGL ES
 *   - Vulkan
 *   - Direct3D 11/12
 *   - Metal
 */

#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <cstdint>

//=============================================================================
// Platform Detection (Internal)
//=============================================================================

#if defined(_WIN32)
    #if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
        #define WINDOW_PLATFORM_UWP
    #else
        #define WINDOW_PLATFORM_WIN32
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define WINDOW_PLATFORM_IOS
    #else
        #define WINDOW_PLATFORM_MACOS
    #endif
#elif defined(__ANDROID__)
    #define WINDOW_PLATFORM_ANDROID
#elif defined(__linux__)
    #if defined(WINDOW_USE_WAYLAND)
        #define WINDOW_PLATFORM_WAYLAND
    #else
        #define WINDOW_PLATFORM_X11
    #endif
#else
    #error "Unsupported platform"
#endif

//=============================================================================
// Public API
//=============================================================================

namespace window {

//-----------------------------------------------------------------------------
// Enumerations
//-----------------------------------------------------------------------------

enum class Result {
    Success = 0,
    ErrorUnknown,
    ErrorPlatformInit,
    ErrorWindowCreation,
    ErrorGraphicsInit,
    ErrorNotSupported,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorDeviceLost
};

enum class Backend {
    Auto = 0,   // Auto-select best backend for platform
    OpenGL,
    Vulkan,
    D3D11,
    D3D12,
    Metal
};

//-----------------------------------------------------------------------------
// Forward Declarations
//-----------------------------------------------------------------------------

class Graphics;

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------

struct Config {
    const char* title = "Window";
    int width = 800;
    int height = 600;
    int x = -1;             // -1 = centered/default
    int y = -1;             // -1 = centered/default
    bool resizable = true;
    bool visible = true;
    bool vsync = true;
    int samples = 1;        // MSAA samples (1 = disabled)
    // Back buffer settings
    int red_bits = 8;
    int green_bits = 8;
    int blue_bits = 8;
    int alpha_bits = 8;
    int depth_bits = 24;
    int stencil_bits = 8;
    int back_buffers = 2;   // Swap chain buffer count (2 = double buffering, 3 = triple buffering)
    // Graphics backend selection
    // Auto = platform default, or specify OpenGL/Vulkan/D3D11/D3D12/Metal
    Backend backend = Backend::Auto;
    // Shared context - for resource sharing between windows
    // OpenGL: shares textures, buffers, shaders
    // D3D11/D3D12/Vulkan/Metal: shares device (creates new swapchain)
    Graphics* shared_graphics = nullptr;
};

//-----------------------------------------------------------------------------
// Graphics Context
//-----------------------------------------------------------------------------

class Graphics {
public:
    virtual ~Graphics() = default;

    // Backend info
    virtual Backend get_backend() const = 0;
    virtual const char* get_backend_name() const = 0;
    virtual const char* get_device_name() const = 0;

    // Native handles
    virtual void* native_device() const = 0;
    virtual void* native_context() const = 0;
    virtual void* native_swapchain() const = 0;

protected:
    Graphics() = default;
};

//-----------------------------------------------------------------------------
// Window
//-----------------------------------------------------------------------------

class Window {
public:
    // Creation/destruction
    static Window* create(const Config& config, Result* out_result = nullptr);
    void destroy();

    // Visibility
    void show();
    void hide();
    bool is_visible() const;

    // Title
    void set_title(const char* title);
    const char* get_title() const;

    // Size
    void set_size(int width, int height);
    void get_size(int* width, int* height) const;
    int get_width() const;
    int get_height() const;

    // Position (returns false if not supported on platform)
    bool set_position(int x, int y);
    bool get_position(int* x, int* y) const;
    bool supports_position() const;

    // State
    bool should_close() const;
    void set_should_close(bool close);
    void poll_events();

    // Graphics
    Graphics* graphics() const;

    // Native handles
    void* native_handle() const;    // HWND, Window, NSWindow*, etc.
    void* native_display() const;   // Display*, wl_display*, etc.

    // Implementation detail - public for platform callbacks
    struct Impl;

private:
    Window() = default;
    ~Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Impl* impl = nullptr;
};

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------

const char* result_to_string(Result result);
const char* backend_to_string(Backend backend);
bool is_backend_supported(Backend backend);
Backend get_default_backend();

} // namespace window

#endif // WINDOW_HPP
