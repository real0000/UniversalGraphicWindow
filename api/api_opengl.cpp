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
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB          0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define WGL_CONTEXT_ES2_PROFILE_BIT_EXT           0x00000004
#define WGL_CONTEXT_FLAGS_ARB               0x2094
#define WGL_CONTEXT_DEBUG_BIT_ARB           0x0001
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_TYPE_RGBA_ARB                 0x202B
#define WGL_TYPE_RGBA_FLOAT_ARB           0x21A0
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

    void get_capabilities(GraphicsCapabilities* out_caps) const override {
        if (!out_caps || !hglrc || !hdc) return;
        GraphicsCapabilities& c = *out_caps;

        GLint v = 0;

        // Texture limits
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);               c.max_texture_size = v;
        glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &v);            c.max_texture_3d_size = v;
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);      c.max_texture_cube_size = v;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &v);       c.max_texture_array_layers = v;
        c.max_mip_levels = 1;
        { int s = c.max_texture_size; while (s > 1) { s >>= 1; ++c.max_mip_levels; } }

        // Framebuffer limits
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &v);          c.max_color_attachments = v;
        glGetIntegerv(GL_MAX_SAMPLES, &v);                    c.max_samples = v;
        glGetIntegerv(GL_MAX_FRAMEBUFFER_WIDTH, &v);          c.max_framebuffer_width = v;
        glGetIntegerv(GL_MAX_FRAMEBUFFER_HEIGHT, &v);         c.max_framebuffer_height = v;

        // Sampling
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v);        c.max_texture_bindings = v;
        { GLfloat af = 1.0f;
          glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &af);
          c.max_anisotropy = static_cast<int>(af); }

        // Vertex / buffer limits
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);             c.max_vertex_attributes = v;
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_BINDINGS, &v);     c.max_vertex_buffers = v;
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &v);    c.max_uniform_bindings = v;
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &v);         c.max_uniform_buffer_size = v;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &v); c.max_storage_bindings = v;

        // Viewports
        glGetIntegerv(GL_MAX_VIEWPORTS, &v);                  c.max_viewports = v;
        c.max_scissor_rects = c.max_viewports;

        // Compute limits
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &v); c.max_compute_group_size_x = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &v); c.max_compute_group_size_y = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &v); c.max_compute_group_size_z = v;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &v); c.max_compute_group_total = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &v); c.max_compute_dispatch_x = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &v); c.max_compute_dispatch_y = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &v); c.max_compute_dispatch_z = v;

        // API version
        glGetIntegerv(GL_MAJOR_VERSION, &v); c.api_version_major = v;
        glGetIntegerv(GL_MINOR_VERSION, &v); c.api_version_minor = v;
        c.shader_model = static_cast<float>(c.api_version_major) + c.api_version_minor * 0.1f;

        // Shader / pipeline features (derived from GL version)
        const int gl_ver = c.api_version_major * 10 + c.api_version_minor;
        c.compute_shaders    = gl_ver >= 43;
        c.geometry_shaders   = gl_ver >= 32;
        c.tessellation       = gl_ver >= 40;
        c.instancing         = gl_ver >= 30;
        c.indirect_draw      = gl_ver >= 40;
        c.multi_draw_indirect = gl_ver >= 43;
        c.base_vertex_draw   = gl_ver >= 32;
        c.occlusion_query    = true;
        c.timestamp_query    = gl_ver >= 30;
        c.depth_clamp        = gl_ver >= 32;
        c.fill_mode_wireframe = true;
        c.line_smooth        = true;

        // Texture features
        c.texture_arrays          = gl_ver >= 30;
        c.texture_3d              = true;
        c.cube_maps               = true;
        c.cube_map_arrays         = gl_ver >= 40;
        c.render_to_texture       = true;
        c.read_write_textures     = c.compute_shaders;
        c.floating_point_textures = true;
        c.integer_textures        = gl_ver >= 30;
        c.srgb_framebuffer        = true;
        c.srgb_textures           = true;
        c.depth32f                = true;
        c.stencil8                = true;

        // Compressed texture formats: inspect extension string
        GLint ext_count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);
        for (GLint i = 0; i < ext_count; i++) {
            const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (!ext) continue;
            if (strstr(ext, "texture_compression_s3tc"))   c.texture_compression_bc   = true;
            if (strstr(ext, "texture_compression_bptc"))   c.texture_compression_bc   = true;
            if (strstr(ext, "texture_compression_rgtc"))   c.texture_compression_bc   = true;
            if (strstr(ext, "texture_compression_etc2"))   c.texture_compression_etc2 = true;
            if (strstr(ext, "texture_compression_astc_ldr")) c.texture_compression_astc = true;
            if (strstr(ext, "blend_func_extended"))        c.dual_source_blend        = true;
        }
        // ETC2 is mandated by the GL 4.3 core spec
        if (gl_ver >= 43) c.texture_compression_etc2 = true;

        // Blend
        c.independent_blend = gl_ver >= 40;
    }
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

    // Derive color channel bits from color_bits
    int red_bits = 8, green_bits = 8, blue_bits = 8, alpha_bits = 8;
    int pixel_type = WGL_TYPE_RGBA_ARB;
    if (config.color_bits >= 64) {
        // 64-bit HDR: 16 bits per channel, floating point
        red_bits = 16; green_bits = 16; blue_bits = 16; alpha_bits = 16;
        pixel_type = WGL_TYPE_RGBA_FLOAT_ARB;
    } else if (config.color_bits == 16) {
        red_bits = 5; green_bits = 6; blue_bits = 5; alpha_bits = 0;
    } else if (config.color_bits == 24) {
        red_bits = 8; green_bits = 8; blue_bits = 8; alpha_bits = 0;
    }

    int pixel_attribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
        WGL_PIXEL_TYPE_ARB, pixel_type,
        WGL_RED_BITS_ARB, red_bits,
        WGL_GREEN_BITS_ARB, green_bits,
        WGL_BLUE_BITS_ARB, blue_bits,
        WGL_ALPHA_BITS_ARB, alpha_bits,
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

    // Try highest OpenGL core version first, then OpenGL ES
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

    // Set swap interval based on swap_mode
    if (wglSwapIntervalEXT) {
        int interval = 1;  // Default to vsync
        SwapMode swap_mode = config.swap_mode;
        if (swap_mode == SwapMode::Auto) {
            swap_mode = config.vsync ? SwapMode::Fifo : SwapMode::Immediate;
        }
        switch (swap_mode) {
            case SwapMode::Immediate:
                interval = 0;
                break;
            case SwapMode::Mailbox:
                // OpenGL doesn't have true mailbox, use vsync
                interval = 1;
                break;
            case SwapMode::FifoRelaxed:
                // Adaptive vsync: -1 requires WGL_EXT_swap_control_tear
                // Falls back to regular vsync if not supported
                interval = -1;
                break;
            case SwapMode::Fifo:
            case SwapMode::Auto:
            default:
                interval = 1;
                break;
        }
        wglSwapIntervalEXT(interval);
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
