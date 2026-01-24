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
// Backend Configuration
//=============================================================================

#if !defined(WINDOW_NO_OPENGL)
#define WINDOW_HAS_OPENGL 1
#endif

#if !defined(WINDOW_NO_D3D11)
#define WINDOW_HAS_D3D11 1
#endif

#if !defined(WINDOW_NO_D3D12)
#define WINDOW_HAS_D3D12 1
#endif

#if !defined(WINDOW_NO_VULKAN)
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
};

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

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!config.resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);

    RECT rect = { 0, 0, config.width, config.height };
    AdjustWindowRect(&rect, style, FALSE);

    int win_width = rect.right - rect.left;
    int win_height = rect.bottom - rect.top;
    int pos_x = config.x >= 0 ? config.x : CW_USEDEFAULT;
    int pos_y = config.y >= 0 ? config.y : CW_USEDEFAULT;

    int title_len = MultiByteToWideChar(CP_UTF8, 0, config.title, -1, nullptr, 0);
    wchar_t* title_wide = new wchar_t[title_len];
    MultiByteToWideChar(CP_UTF8, 0, config.title, -1, title_wide, title_len);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, title_wide, style, pos_x, pos_y, win_width, win_height,
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

    RECT win_rect;
    GetWindowRect(hwnd, &win_rect);
    window->impl->x = win_rect.left;
    window->impl->y = win_rect.top;

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

} // namespace window

#endif // WINDOW_PLATFORM_WIN32
