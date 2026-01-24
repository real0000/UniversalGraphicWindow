/*
 * window_uwp.cpp - UWP (Universal Windows Platform) implementation
 * Backends: OpenGL (via ANGLE/EGL), Vulkan, D3D11, D3D12
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_UWP)

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>

#include <string>

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::UI::ViewManagement;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;

namespace window {

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define WINDOW_HAS_VULKAN 1
#endif

#ifdef WINDOW_SUPPORT_D3D11
#define WINDOW_HAS_D3D11 1
#endif

#ifdef WINDOW_SUPPORT_D3D12
#define WINDOW_HAS_D3D12 1
#endif

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D11
Graphics* create_d3d11_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D12
Graphics* create_d3d12_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

//=============================================================================
// Window Implementation
//=============================================================================

struct Window::Impl {
    CoreWindow core_window = nullptr;
    bool should_close_flag = false;
    bool visible = true;
    int width = 0;
    int height = 0;
    float dpi = 96.0f;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    bool is_fullscreen = false;
};

// Global window instance for UWP
static Window* g_uwp_window = nullptr;

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    // In UWP, we need to get the CoreWindow from the current thread
    CoreWindow core_window = CoreWindow::GetForCurrentThread();
    if (!core_window) {
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->core_window = core_window;
    window->impl->title = config.title;

    // Get window size
    auto bounds = core_window.Bounds();
    window->impl->width = static_cast<int>(bounds.Width);
    window->impl->height = static_cast<int>(bounds.Height);

    // Get DPI
    DisplayInformation display_info = DisplayInformation::GetForCurrentView();
    window->impl->dpi = display_info.LogicalDpi();

    // Setup event handlers
    core_window.Closed([](CoreWindow const&, CoreWindowEventArgs const&) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->should_close_flag = true;
        }
    });

    core_window.SizeChanged([](CoreWindow const&, WindowSizeChangedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->width = static_cast<int>(args.Size().Width);
            g_uwp_window->impl->height = static_cast<int>(args.Size().Height);
        }
    });

    core_window.VisibilityChanged([](CoreWindow const&, VisibilityChangedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->visible = args.Visible();
        }
    });

    g_uwp_window = window;

    // Create graphics backend based on config.backend
    void* core_window_abi = winrt::get_abi(core_window);
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
            break;
#endif
        default:
            break;
    }

    // Fallback to default if requested backend failed or not supported
    if (!gfx && config.backend != Backend::Auto) {
        Backend fallback = get_default_backend();
        switch (fallback) {
#ifdef WINDOW_HAS_D3D11
            case Backend::D3D11:
                gfx = create_d3d11_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_D3D12
            case Backend::D3D12:
                gfx = create_d3d12_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
            default:
                break;
        }
    }

    if (!gfx) {
        delete window->impl;
        delete window;
        g_uwp_window = nullptr;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;

    // Set title
    auto view = ApplicationView::GetForCurrentView();
    int len = MultiByteToWideChar(CP_UTF8, 0, config.title, -1, nullptr, 0);
    wchar_t* wide = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, config.title, -1, wide, len);
    view.Title(wide);
    delete[] wide;

    if (config.visible) {
        core_window.Activate();
    }

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        delete impl;
        impl = nullptr;
    }
    g_uwp_window = nullptr;
    delete this;
}

void Window::show() {
    if (impl && impl->core_window) {
        impl->core_window.Activate();
        impl->visible = true;
    }
}

void Window::hide() {
    // UWP doesn't support hiding the main window
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl) {
        auto view = ApplicationView::GetForCurrentView();
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        wchar_t* wide = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, len);
        view.Title(wide);
        delete[] wide;
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl) {
        auto view = ApplicationView::GetForCurrentView();
        view.TryResizeView(Size(static_cast<float>(width), static_cast<float>(height)));
    }
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (width) *width = impl->width;
        if (height) *height = impl->height;
    }
}

int Window::get_width() const {
    return impl ? impl->width : 0;
}

int Window::get_height() const {
    return impl ? impl->height : 0;
}

bool Window::set_position(int x, int y) {
    // UWP doesn't allow positioning windows
    (void)x; (void)y;
    return false;
}

bool Window::get_position(int* x, int* y) const {
    // UWP windows don't have a position in the traditional sense
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const {
    return false;
}

void Window::set_style(WindowStyle style) {
    if (!impl) return;

    impl->style = style;

    // Handle fullscreen
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl) return;
    if (impl->is_fullscreen == fullscreen) return;

    auto view = ApplicationView::GetForCurrentView();

    if (fullscreen) {
        if (view.TryEnterFullScreenMode()) {
            impl->is_fullscreen = true;
            impl->style = impl->style | WindowStyle::Fullscreen;
        }
    } else {
        view.ExitFullScreenMode();
        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    // UWP doesn't support always-on-top for regular apps
    if (impl) {
        if (always_on_top) {
            impl->style = impl->style | WindowStyle::AlwaysOnTop;
        } else {
            impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        }
    }
}

bool Window::is_always_on_top() const {
    return impl ? has_style(impl->style, WindowStyle::AlwaysOnTop) : false;
}

bool Window::should_close() const {
    return impl ? impl->should_close_flag : true;
}

void Window::set_should_close(bool close) {
    if (impl) impl->should_close_flag = close;
}

void Window::poll_events() {
    if (impl && impl->core_window) {
        impl->core_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? winrt::get_abi(impl->core_window) : nullptr;
}

void* Window::native_display() const {
    return nullptr;
}

//=============================================================================
// Utility Functions
//=============================================================================

const char* result_to_string(Result result) {
    switch (result) {
        case Result::Success: return "Success";
        case Result::ErrorUnknown: return "Unknown error";
        case Result::ErrorPlatformInit: return "Platform initialization failed";
        case Result::ErrorWindowCreation: return "Window creation failed";
        case Result::ErrorGraphicsInit: return "Graphics initialization failed";
        case Result::ErrorNotSupported: return "Not supported";
        case Result::ErrorInvalidParameter: return "Invalid parameter";
        case Result::ErrorOutOfMemory: return "Out of memory";
        case Result::ErrorDeviceLost: return "Device lost";
        default: return "Unknown";
    }
}

const char* backend_to_string(Backend backend) {
    switch (backend) {
        case Backend::Auto: return "Auto";
        case Backend::OpenGL: return "OpenGL";
        case Backend::Vulkan: return "Vulkan";
        case Backend::D3D11: return "Direct3D 11";
        case Backend::D3D12: return "Direct3D 12";
        case Backend::Metal: return "Metal";
        default: return "Unknown";
    }
}

bool is_backend_supported(Backend backend) {
    switch (backend) {
        case Backend::Auto: return true;
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL: return true;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan: return true;
#endif
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11: return true;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_D3D11
    return Backend::D3D11;
#elif defined(WINDOW_HAS_D3D12)
    return Backend::D3D12;
#else
    return Backend::Auto;
#endif
}

//=============================================================================
// Graphics Context for External Windows
//=============================================================================

Graphics* Graphics::create(const ExternalWindowConfig& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    if (!config.native_handle) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    if (config.width <= 0 || config.height <= 0) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    // Convert ExternalWindowConfig to Config for backend creation
    Config internal_config;
    internal_config.width = config.width;
    internal_config.height = config.height;
    internal_config.vsync = config.vsync;
    internal_config.samples = config.samples;
    internal_config.red_bits = config.red_bits;
    internal_config.green_bits = config.green_bits;
    internal_config.blue_bits = config.blue_bits;
    internal_config.alpha_bits = config.alpha_bits;
    internal_config.depth_bits = config.depth_bits;
    internal_config.stencil_bits = config.stencil_bits;
    internal_config.back_buffers = config.back_buffers;
    internal_config.backend = config.backend;
    internal_config.shared_graphics = config.shared_graphics;

    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    Graphics* gfx = nullptr;
    void* core_window = config.native_handle;

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
        default:
            break;
    }

    if (!gfx) {
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    set_result(Result::Success);
    return gfx;
}

void Graphics::destroy() {
    delete this;
}

} // namespace window

#endif // WINDOW_PLATFORM_UWP
