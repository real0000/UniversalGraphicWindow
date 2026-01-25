/*
 * window_win32.cpp - Win32 implementation
 * Backends: OpenGL, Vulkan, D3D11, D3D12
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstring>
#include <string>

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_D3D11
#define WINDOW_HAS_D3D11 1
#endif

#ifdef WINDOW_SUPPORT_D3D12
#define WINDOW_HAS_D3D12 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D11
Graphics* create_d3d11_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D12
Graphics* create_d3d12_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_win32(void* hwnd, int width, int height, const Config& config);
#endif

//=============================================================================
// Window Implementation
//=============================================================================

struct Window::Impl {
    HWND hwnd = nullptr;
    bool should_close_flag = false;
    bool visible = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    // For fullscreen toggle restoration
    RECT windowed_rect = {};
    DWORD windowed_style = 0;
    DWORD windowed_ex_style = 0;
    bool is_fullscreen = false;
};

// Helper function to convert WindowStyle flags to Win32 style
static DWORD style_to_win32_style(WindowStyle style) {
    if (has_style(style, WindowStyle::Fullscreen)) {
        return WS_POPUP | WS_VISIBLE;
    }

    DWORD win_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    if (has_style(style, WindowStyle::TitleBar)) {
        win_style |= WS_CAPTION | WS_SYSMENU;
    }

    if (has_style(style, WindowStyle::Border) && !has_style(style, WindowStyle::TitleBar)) {
        win_style |= WS_POPUP | WS_BORDER;
    } else if (!has_style(style, WindowStyle::TitleBar) && !has_style(style, WindowStyle::Border)) {
        win_style |= WS_POPUP;
    }

    if (has_style(style, WindowStyle::MinimizeButton)) {
        win_style |= WS_MINIMIZEBOX;
    }

    if (has_style(style, WindowStyle::MaximizeButton)) {
        win_style |= WS_MAXIMIZEBOX;
    }

    if (has_style(style, WindowStyle::Resizable)) {
        win_style |= WS_THICKFRAME;
    }

    return win_style;
}

static DWORD style_to_win32_ex_style(WindowStyle style) {
    DWORD ex_style = WS_EX_APPWINDOW;

    if (has_style(style, WindowStyle::AlwaysOnTop)) {
        ex_style |= WS_EX_TOPMOST;
    }

    if (has_style(style, WindowStyle::ToolWindow)) {
        ex_style = (ex_style & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
    }

    return ex_style;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window::Impl* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            if (impl) impl->should_close_flag = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (impl) {
                impl->width = LOWORD(lparam);
                impl->height = HIWORD(lparam);
            }
            return 0;
        case WM_MOVE:
            if (impl) {
                impl->x = (int)(short)LOWORD(lparam);
                impl->y = (int)(short)HIWORD(lparam);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    static bool class_registered = false;
    static const wchar_t* CLASS_NAME = L"WindowHppClass";

    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExW(&wc)) {
            set_result(Result::ErrorPlatformInit);
            return nullptr;
        }
        class_registered = true;
    }

    // Combine config.style with legacy config.resizable flag
    WindowStyle effective_style = config.style;
    if (!config.resizable) {
        effective_style = effective_style & ~WindowStyle::Resizable;
    }

    DWORD style = style_to_win32_style(effective_style);
    DWORD ex_style = style_to_win32_ex_style(effective_style);

    int win_width, win_height;
    int pos_x, pos_y;

    if (has_style(effective_style, WindowStyle::Fullscreen)) {
        // Fullscreen: use entire screen
        pos_x = 0;
        pos_y = 0;
        win_width = GetSystemMetrics(SM_CXSCREEN);
        win_height = GetSystemMetrics(SM_CYSCREEN);
    } else {
        RECT rect = { 0, 0, config.width, config.height };
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        win_width = rect.right - rect.left;
        win_height = rect.bottom - rect.top;
        pos_x = config.x >= 0 ? config.x : CW_USEDEFAULT;
        pos_y = config.y >= 0 ? config.y : CW_USEDEFAULT;
    }

    int title_len = MultiByteToWideChar(CP_UTF8, 0, config.title, -1, nullptr, 0);
    wchar_t* title_wide = new wchar_t[title_len];
    MultiByteToWideChar(CP_UTF8, 0, config.title, -1, title_wide, title_len);

    HWND hwnd = CreateWindowExW(ex_style, CLASS_NAME, title_wide, style, pos_x, pos_y, win_width, win_height,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    delete[] title_wide;

    if (!hwnd) {
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->hwnd = hwnd;
    window->impl->width = config.width;
    window->impl->height = config.height;
    window->impl->title = config.title;
    window->impl->style = effective_style;
    window->impl->is_fullscreen = has_style(effective_style, WindowStyle::Fullscreen);

    RECT win_rect;
    GetWindowRect(hwnd, &win_rect);
    window->impl->x = win_rect.left;
    window->impl->y = win_rect.top;

    // Save windowed state for fullscreen toggle
    if (!window->impl->is_fullscreen) {
        window->impl->windowed_rect = win_rect;
        window->impl->windowed_style = style;
        window->impl->windowed_ex_style = ex_style;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window->impl));

    // Create graphics backend based on config.backend
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_win32(hwnd, config.width, config.height, config);
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
                gfx = create_d3d11_graphics_hwnd(hwnd, config);
                break;
#endif
#ifdef WINDOW_HAS_D3D12
            case Backend::D3D12:
                gfx = create_d3d12_graphics_hwnd(hwnd, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_hwnd(hwnd, config);
                break;
#endif
            default:
                break;
        }
    }

    if (!gfx) {
        DestroyWindow(hwnd);
        delete window->impl;
        delete window;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;

    if (config.visible) {
        ShowWindow(hwnd, SW_SHOW);
        window->impl->visible = true;
    }

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        if (impl->hwnd) DestroyWindow(impl->hwnd);
        delete impl;
        impl = nullptr;
    }
    delete this;
}

void Window::show() { if (impl && impl->hwnd) { ShowWindow(impl->hwnd, SW_SHOW); impl->visible = true; } }
void Window::hide() { if (impl && impl->hwnd) { ShowWindow(impl->hwnd, SW_HIDE); impl->visible = false; } }
bool Window::is_visible() const { return impl ? impl->visible : false; }

void Window::set_title(const char* title) {
    if (impl && impl->hwnd) {
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        wchar_t* wide = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, len);
        SetWindowTextW(impl->hwnd, wide);
        delete[] wide;
        impl->title = title;
    }
}

const char* Window::get_title() const { return impl ? impl->title.c_str() : ""; }

void Window::set_size(int width, int height) {
    if (impl && impl->hwnd) {
        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_STYLE));
        RECT rect = { 0, 0, width, height };
        AdjustWindowRect(&rect, style, FALSE);
        SetWindowPos(impl->hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    }
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (width) *width = impl->width;
        if (height) *height = impl->height;
    }
}

int Window::get_width() const { return impl ? impl->width : 0; }
int Window::get_height() const { return impl ? impl->height : 0; }

bool Window::set_position(int x, int y) {
    if (impl && impl->hwnd) {
        SetWindowPos(impl->hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        impl->x = x;
        impl->y = y;
        return true;
    }
    return false;
}

bool Window::get_position(int* x, int* y) const {
    if (impl) {
        if (x) *x = impl->x;
        if (y) *y = impl->y;
        return true;
    }
    return false;
}

bool Window::supports_position() const { return true; }

void Window::set_style(WindowStyle style) {
    if (!impl || !impl->hwnd) return;

    impl->style = style;

    // Handle fullscreen separately
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
        return;
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    DWORD win_style = style_to_win32_style(style);
    DWORD ex_style = style_to_win32_ex_style(style);

    SetWindowLongPtrW(impl->hwnd, GWL_STYLE, win_style);
    SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, ex_style);

    // Recalculate window size to maintain client area
    RECT rect = { 0, 0, impl->width, impl->height };
    AdjustWindowRectEx(&rect, win_style, FALSE, ex_style);

    SetWindowPos(impl->hwnd, has_style(style, WindowStyle::AlwaysOnTop) ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_FRAMECHANGED);
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !impl->hwnd) return;
    if (impl->is_fullscreen == fullscreen) return;

    if (fullscreen) {
        // Save current windowed state
        impl->windowed_style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_STYLE));
        impl->windowed_ex_style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE));
        GetWindowRect(impl->hwnd, &impl->windowed_rect);

        // Switch to fullscreen
        DWORD style = WS_POPUP | WS_VISIBLE;
        SetWindowLongPtrW(impl->hwnd, GWL_STYLE, style);
        SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

        int screen_width = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);

        SetWindowPos(impl->hwnd, HWND_TOP, 0, 0, screen_width, screen_height,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        impl->is_fullscreen = true;
        impl->style = impl->style | WindowStyle::Fullscreen;
    } else {
        // Restore windowed state
        SetWindowLongPtrW(impl->hwnd, GWL_STYLE, impl->windowed_style);
        SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, impl->windowed_ex_style);

        int x = impl->windowed_rect.left;
        int y = impl->windowed_rect.top;
        int w = impl->windowed_rect.right - impl->windowed_rect.left;
        int h = impl->windowed_rect.bottom - impl->windowed_rect.top;

        SetWindowPos(impl->hwnd, HWND_NOTOPMOST, x, y, w, h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    if (!impl || !impl->hwnd) return;

    if (always_on_top) {
        impl->style = impl->style | WindowStyle::AlwaysOnTop;
        SetWindowPos(impl->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else {
        impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        SetWindowPos(impl->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

bool Window::is_always_on_top() const {
    return impl ? has_style(impl->style, WindowStyle::AlwaysOnTop) : false;
}

bool Window::should_close() const { return impl ? impl->should_close_flag : true; }
void Window::set_should_close(bool close) { if (impl) impl->should_close_flag = close; }

void Window::poll_events() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

Graphics* Window::graphics() const { return impl ? impl->gfx : nullptr; }
void* Window::native_handle() const { return impl ? impl->hwnd : nullptr; }
void* Window::native_display() const { return nullptr; }

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
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11: return true;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12: return true;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_D3D11
    return Backend::D3D11;
#elif defined(WINDOW_HAS_OPENGL)
    return Backend::OpenGL;
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
    HWND hwnd = static_cast<HWND>(config.native_handle);

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_win32(hwnd, config.width, config.height, internal_config);
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

//=============================================================================
// Window::create_from_config
//=============================================================================

Window* Window::create_from_config(const char* config_filepath, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    GraphicsConfig gfx_config;

    // Try to load config file
    if (config_filepath && GraphicsConfig::load(config_filepath, &gfx_config)) {
        // Config loaded and validated
    } else {
        // Use defaults
        gfx_config = GraphicsConfig{};
    }

    // Convert to Config and create window
    Config config = gfx_config.to_config();

    return Window::create(config, out_result);
}

} // namespace window

#endif // WINDOW_PLATFORM_WIN32
