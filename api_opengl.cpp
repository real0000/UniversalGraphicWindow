/*
 * api_opengl.cpp - OpenGL graphics implementation (Windows WGL)
 */

#include "window.hpp"

#if defined(_WIN32) && !defined(WINDOW_NO_OPENGL)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <cstdint>
#include <string>
#include "glad.h"

#pragma comment(lib, "opengl32.lib")

namespace window {

//=============================================================================
// WGL Extensions
//=============================================================================

typedef HGLRC (WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
typedef BOOL (WINAPI* PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef BOOL (WINAPI* PFNWGLSWAPINTERVALEXTPROC)(int);

#define WGL_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB       0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB        0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB    0x00000001
#define WGL_CONTEXT_ES2_PROFILE_BIT_EXT     0x00000004
#define WGL_CONTEXT_FLAGS_ARB               0x2094
#define WGL_CONTEXT_DEBUG_BIT_ARB           0x0001
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_TYPE_RGBA_ARB                 0x202B
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_RED_BITS_ARB                  0x2015
#define WGL_GREEN_BITS_ARB                0x2017
#define WGL_BLUE_BITS_ARB                 0x2019
#define WGL_ALPHA_BITS_ARB                0x201B
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_STENCIL_BITS_ARB              0x2023
#define WGL_SAMPLE_BUFFERS_ARB            0x2041
#define WGL_SAMPLES_ARB                   0x2042

static PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = nullptr;
static PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = nullptr;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
static bool s_wgl_initialized = false;

//=============================================================================
// OpenGL Graphics Implementation
//=============================================================================

class GraphicsOpenGL : public Graphics {
public:
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hglrc = nullptr;
    std::string device_name;

    ~GraphicsOpenGL() override {
        if (hglrc) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc);
        }
        if (hdc && hwnd) {
            ReleaseDC(hwnd, hdc);
        }
    }

    Backend get_backend() const override { return Backend::OpenGL; }
    const char* get_backend_name() const override { return "OpenGL"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        // OpenGL doesn't need explicit resize - viewport is set by application
        (void)width;
        (void)height;
        return true;
    }

    void present() override {
        if (hdc) {
            SwapBuffers(hdc);
        }
    }

    void make_current() override {
        if (hdc && hglrc) {
            wglMakeCurrent(hdc, hglrc);
        }
    }

    void* native_device() const override { return nullptr; }
    void* native_context() const override { return hglrc; }
    void* native_swapchain() const override { return hdc; }
};

//=============================================================================
// WGL Extension Initialization
//=============================================================================

static bool init_wgl_extensions() {
    if (s_wgl_initialized) return true;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WGLLoader";
    RegisterClassW(&wc);

    HWND dummy = CreateWindowW(L"WGLLoader", L"", WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(dummy);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    int format = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, format, &pfd);

    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);

    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
    UnregisterClassW(L"WGLLoader", wc.hInstance);

    s_wgl_initialized = true;
    return wglCreateContextAttribsARB != nullptr;
}

//=============================================================================
// Creation for HWND (Win32)
//=============================================================================

Graphics* create_opengl_graphics_hwnd(void* hwnd_ptr, const Config& config) {
    if (!init_wgl_extensions()) return nullptr;

    HWND hwnd = static_cast<HWND>(hwnd_ptr);
    HDC hdc = GetDC(hwnd);

    int pixel_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
        WGL_RED_BITS_ARB, config.red_bits,
        WGL_GREEN_BITS_ARB, config.green_bits,
        WGL_BLUE_BITS_ARB, config.blue_bits,
        WGL_ALPHA_BITS_ARB, config.alpha_bits,
        WGL_DEPTH_BITS_ARB, config.depth_bits,
        WGL_STENCIL_BITS_ARB, config.stencil_bits,
        WGL_SAMPLE_BUFFERS_ARB, config.samples > 1 ? 1 : 0,
        WGL_SAMPLES_ARB, config.samples > 1 ? config.samples : 0,
        0
    };

    int pixel_format;
    UINT num_formats;
    if (!wglChoosePixelFormatARB(hdc, pixel_attribs, nullptr, 1, &pixel_format, &num_formats) || num_formats == 0) {
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    PIXELFORMATDESCRIPTOR pfd;
    DescribePixelFormat(hdc, pixel_format, sizeof(pfd), &pfd);
    SetPixelFormat(hdc, pixel_format, &pfd);

    // Get shared context if provided
    HGLRC shared_hglrc = nullptr;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
        shared_hglrc = static_cast<HGLRC>(config.shared_graphics->native_context());
    }

    // Try highest OpenGL version first, then OpenGL ES
    static const struct { int major, minor, profile; } versions[] = {
        // OpenGL Core
        {4, 6, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 5, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 4, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 3, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 2, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 1, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {4, 0, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {3, 3, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {3, 2, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {3, 1, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        {3, 0, WGL_CONTEXT_CORE_PROFILE_BIT_ARB},
        // OpenGL ES
        {3, 2, WGL_CONTEXT_ES2_PROFILE_BIT_EXT},
        {3, 1, WGL_CONTEXT_ES2_PROFILE_BIT_EXT},
        {3, 0, WGL_CONTEXT_ES2_PROFILE_BIT_EXT},
        {2, 0, WGL_CONTEXT_ES2_PROFILE_BIT_EXT},
    };

    HGLRC hglrc = nullptr;
    for (const auto& ver : versions) {
        int context_attribs[] = {
            WGL_CONTEXT_MAJOR_VERSION_ARB, ver.major,
            WGL_CONTEXT_MINOR_VERSION_ARB, ver.minor,
            WGL_CONTEXT_PROFILE_MASK_ARB, ver.profile,
            0
        };
        hglrc = wglCreateContextAttribsARB(hdc, shared_hglrc, context_attribs);
        if (hglrc) break;
    }

    if (!hglrc) {
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    wglMakeCurrent(hdc, hglrc);

    if (!gladLoadGL()) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc);
        ReleaseDC(hwnd, hdc);
        return nullptr;
    }

    if (wglSwapIntervalEXT) {
        wglSwapIntervalEXT(config.vsync ? 1 : 0);
    }

    GraphicsOpenGL* gfx = new GraphicsOpenGL();
    gfx->hwnd = hwnd;
    gfx->hdc = hdc;
    gfx->hglrc = hglrc;
    gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    return gfx;
}

} // namespace window

#endif // _WIN32 && !WINDOW_NO_OPENGL
