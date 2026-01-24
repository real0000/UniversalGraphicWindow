/*
 * api_opengl_glx.cpp - OpenGL graphics implementation (X11 GLX)
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_X11) && !defined(WINDOW_NO_OPENGL)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <cstring>
#include <string>

typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARBPROC)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

#define GLX_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB       0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB        0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB    0x00000001
#define GLX_CONTEXT_FLAGS_ARB               0x2094
#define GLX_CONTEXT_DEBUG_BIT_ARB           0x0001

namespace window {

//=============================================================================
// OpenGL Graphics Implementation
//=============================================================================

class GraphicsOpenGLX11 : public Graphics {
public:
    Display* display = nullptr;
    ::Window xwindow = 0;
    GLXContext context = nullptr;
    std::string device_name;

    ~GraphicsOpenGLX11() override {
        if (context && display) {
            glXMakeCurrent(display, None, nullptr);
            glXDestroyContext(display, context);
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
        if (display && xwindow) {
            glXSwapBuffers(display, xwindow);
        }
    }

    void make_current() override {
        if (display && xwindow && context) {
            glXMakeCurrent(display, xwindow, context);
        }
    }

    void* native_device() const override { return nullptr; }
    void* native_context() const override { return context; }
    void* native_swapchain() const override { return display; }
};

//=============================================================================
// GLX FBConfig Selection
//=============================================================================

bool select_glx_fbconfig(void* display_ptr, int screen, const Config& config,
                         void** out_fbconfig, void** out_visual, int* out_depth) {
    Display* display = static_cast<Display*>(display_ptr);

    int glx_attribs[] = {
        GLX_X_RENDERABLE, True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE, config.red_bits,
        GLX_GREEN_SIZE, config.green_bits,
        GLX_BLUE_SIZE, config.blue_bits,
        GLX_ALPHA_SIZE, config.alpha_bits,
        GLX_DEPTH_SIZE, config.depth_bits,
        GLX_STENCIL_SIZE, config.stencil_bits,
        GLX_DOUBLEBUFFER, True,
        GLX_SAMPLE_BUFFERS, config.samples > 1 ? 1 : 0,
        GLX_SAMPLES, config.samples > 1 ? config.samples : 0,
        None
    };

    int fb_count;
    GLXFBConfig* fb_configs = glXChooseFBConfig(display, screen, glx_attribs, &fb_count);
    if (!fb_configs || fb_count == 0) {
        return false;
    }

    GLXFBConfig fb_config = fb_configs[0];
    XFree(fb_configs);

    XVisualInfo* vi = glXGetVisualFromFBConfig(display, fb_config);
    if (!vi) {
        return false;
    }

    *out_fbconfig = reinterpret_cast<void*>(fb_config);
    *out_visual = vi->visual;
    *out_depth = vi->depth;
    XFree(vi);

    return true;
}

//=============================================================================
// OpenGL Context Creation
//=============================================================================

Graphics* create_opengl_graphics_x11(void* display_ptr, unsigned long window,
                                      void* fbconfig_ptr, const Config& config) {
    Display* display = static_cast<Display*>(display_ptr);
    GLXFBConfig fb_config = reinterpret_cast<GLXFBConfig>(fbconfig_ptr);

    PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB");

    // Get shared context if provided
    GLXContext shared_context = nullptr;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
        shared_context = static_cast<GLXContext>(config.shared_graphics->native_context());
    }

    GLXContext glx_context = nullptr;

    if (glXCreateContextAttribsARB) {
        // Try highest OpenGL version first
        static const struct { int major, minor; } versions[] = {
            {4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},
            {3, 3}, {3, 2}, {3, 1}, {3, 0}
        };

        for (const auto& ver : versions) {
            int context_attribs[] = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, ver.major,
                GLX_CONTEXT_MINOR_VERSION_ARB, ver.minor,
                GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                None
            };
            glx_context = glXCreateContextAttribsARB(display, fb_config, shared_context, True, context_attribs);
            if (glx_context) break;
        }
    }

    if (!glx_context) {
        // Fallback to legacy context
        XVisualInfo* vi = glXGetVisualFromFBConfig(display, fb_config);
        glx_context = glXCreateContext(display, vi, shared_context, True);
        XFree(vi);
    }

    if (!glx_context) {
        return nullptr;
    }

    glXMakeCurrent(display, window, glx_context);

    // Enable vsync if available
    typedef void (*PFNGLXSWAPINTERVALEXTPROC)(Display*, GLXDrawable, int);
    PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT =
        (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
    if (glXSwapIntervalEXT) {
        glXSwapIntervalEXT(display, window, config.vsync ? 1 : 0);
    }

    GraphicsOpenGLX11* gfx = new GraphicsOpenGLX11();
    gfx->display = display;
    gfx->xwindow = window;
    gfx->context = glx_context;
    gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    return gfx;
}

} // namespace window

#endif // WINDOW_PLATFORM_X11 && !WINDOW_NO_OPENGL
