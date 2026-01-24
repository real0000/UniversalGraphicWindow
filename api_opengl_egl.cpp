/*
 * api_opengl_egl.cpp - OpenGL/OpenGL ES graphics implementation using EGL
 * Cross-platform: Linux (Wayland, X11), Android, UWP (via ANGLE)
 */

#include "window.hpp"

#if !defined(WINDOW_NO_OPENGL) && (defined(__linux__) || defined(__ANDROID__) || defined(WINDOW_PLATFORM_UWP))

#include <EGL/egl.h>
#include <string>
#include <cstdint>

#ifdef __ANDROID__
#include <android/native_window.h>
#include <GLES3/gl3.h>
#include "glad_es.h"
#elif defined(WINDOW_PLATFORM_UWP)
// UWP uses ANGLE for EGL support
#include <GLES3/gl3.h>
#include "glad_es.h"
#else
#include <wayland-egl.h>
#include <GL/gl.h>
#include "glad.h"
#endif

namespace window {

//=============================================================================
// OpenGL EGL Graphics Implementation
//=============================================================================

class GraphicsOpenGLEGL : public Graphics {
public:
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLConfig egl_config = nullptr;
    std::string device_name;
    bool owns_display = false;
#if defined(__linux__) && !defined(__ANDROID__)
    wl_egl_window* egl_window = nullptr;  // Wayland-specific
#endif

    ~GraphicsOpenGLEGL() override {
        if (egl_display != EGL_NO_DISPLAY) {
            if (egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display, egl_surface);
            }
            if (egl_context != EGL_NO_CONTEXT) {
                eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(egl_display, egl_context);
            }
            if (owns_display) {
                eglTerminate(egl_display);
            }
        }
#if defined(__linux__) && !defined(__ANDROID__)
        if (egl_window) {
            wl_egl_window_destroy(egl_window);
        }
#endif
    }

    Backend get_backend() const override { return Backend::OpenGL; }
#ifdef __ANDROID__
    const char* get_backend_name() const override { return "OpenGL ES"; }
#else
    const char* get_backend_name() const override { return "OpenGL"; }
#endif
    const char* get_device_name() const override { return device_name.c_str(); }

    void* native_device() const override { return nullptr; }
    void* native_context() const override { return egl_context; }
    void* native_swapchain() const override { return egl_surface; }
#if defined(__linux__) && !defined(__ANDROID__)
    void* native_egl_window() const { return egl_window; }
#endif
};

//=============================================================================
// EGL Initialization
//=============================================================================

static EGLConfig choose_egl_config(EGLDisplay display, const Config& config, bool opengl_es) {
    EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, opengl_es ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
        EGL_RED_SIZE, config.red_bits,
        EGL_GREEN_SIZE, config.green_bits,
        EGL_BLUE_SIZE, config.blue_bits,
        EGL_ALPHA_SIZE, config.alpha_bits,
        EGL_DEPTH_SIZE, config.depth_bits,
        EGL_STENCIL_SIZE, config.stencil_bits,
        EGL_SAMPLES, config.samples > 1 ? config.samples : 0,
        EGL_NONE
    };

    EGLConfig egl_config;
    EGLint num_configs;
    if (!eglChooseConfig(display, attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
        // Try fallback without multisampling
        attribs[16] = 0; // EGL_SAMPLES = 0
        if (!eglChooseConfig(display, attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
            return nullptr;
        }
    }

    return egl_config;
}

static EGLContext create_egl_context(EGLDisplay display, EGLConfig config, bool opengl_es, EGLContext shared_context = EGL_NO_CONTEXT) {
    if (opengl_es) {
        // Try OpenGL ES 3.2, 3.1, 3.0, 2.0
        static const int es_versions[][2] = {{3, 2}, {3, 1}, {3, 0}, {2, 0}};
        for (const auto& ver : es_versions) {
            EGLint context_attribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, ver[0],
                EGL_CONTEXT_MINOR_VERSION, ver[1],
                EGL_NONE
            };
            EGLContext ctx = eglCreateContext(display, config, shared_context, context_attribs);
            if (ctx != EGL_NO_CONTEXT) return ctx;
        }
    } else {
        // Try OpenGL Core 4.6 down to 3.0
        eglBindAPI(EGL_OPENGL_API);
        static const int gl_versions[][2] = {
            {4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0},
            {3, 3}, {3, 2}, {3, 1}, {3, 0}
        };
        for (const auto& ver : gl_versions) {
            EGLint context_attribs[] = {
                EGL_CONTEXT_MAJOR_VERSION, ver[0],
                EGL_CONTEXT_MINOR_VERSION, ver[1],
                EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                EGL_NONE
            };
            EGLContext ctx = eglCreateContext(display, config, shared_context, context_attribs);
            if (ctx != EGL_NO_CONTEXT) return ctx;
        }
    }

    return EGL_NO_CONTEXT;
}

//=============================================================================
// Creation for Wayland
//=============================================================================

#if defined(__linux__) && !defined(__ANDROID__)
Graphics* create_opengl_graphics_wayland(void* wl_display_ptr, void* wl_surface_ptr, int width, int height, const Config& config) {
    struct wl_display* wl_display = static_cast<struct wl_display*>(wl_display_ptr);
    struct wl_surface* wl_surface = static_cast<struct wl_surface*>(wl_surface_ptr);

    EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType)wl_display);
    if (egl_display == EGL_NO_DISPLAY) return nullptr;

    if (!eglInitialize(egl_display, nullptr, nullptr)) return nullptr;

    // Get shared context if provided
    EGLContext shared_context = EGL_NO_CONTEXT;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
        shared_context = static_cast<EGLContext>(config.shared_graphics->native_context());
    }

    // Try OpenGL first, then OpenGL ES
    bool use_gles = false;
    EGLConfig egl_config = choose_egl_config(egl_display, config, false);
    if (!egl_config) {
        egl_config = choose_egl_config(egl_display, config, true);
        use_gles = true;
    }
    if (!egl_config) {
        eglTerminate(egl_display);
        return nullptr;
    }

    // Create wl_egl_window for Wayland
    wl_egl_window* egl_window = wl_egl_window_create(wl_surface, width, height);
    if (!egl_window) {
        eglTerminate(egl_display);
        return nullptr;
    }

    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, nullptr);
    if (egl_surface == EGL_NO_SURFACE) {
        wl_egl_window_destroy(egl_window);
        eglTerminate(egl_display);
        return nullptr;
    }

    EGLContext egl_context = create_egl_context(egl_display, egl_config, use_gles, shared_context);
    if (egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(egl_display, egl_surface);
        wl_egl_window_destroy(egl_window);
        eglTerminate(egl_display);
        return nullptr;
    }

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    if (!gladLoadGL()) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display, egl_context);
        eglDestroySurface(egl_display, egl_surface);
        wl_egl_window_destroy(egl_window);
        eglTerminate(egl_display);
        return nullptr;
    }

    eglSwapInterval(egl_display, config.vsync ? 1 : 0);

    GraphicsOpenGLEGL* gfx = new GraphicsOpenGLEGL();
    gfx->egl_display = egl_display;
    gfx->egl_context = egl_context;
    gfx->egl_surface = egl_surface;
    gfx->egl_config = egl_config;
    gfx->egl_window = egl_window;
    gfx->owns_display = true;
    gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    return gfx;
}

void resize_opengl_graphics_wayland(Graphics* gfx, int width, int height) {
    GraphicsOpenGLEGL* egl_gfx = static_cast<GraphicsOpenGLEGL*>(gfx);
    if (egl_gfx && egl_gfx->egl_window) {
        wl_egl_window_resize(egl_gfx->egl_window, width, height, 0, 0);
    }
}
#endif

//=============================================================================
// Creation for Android
//=============================================================================

#ifdef __ANDROID__
Graphics* create_opengl_graphics_android(void* native_window, int width, int height, const Config& config) {
    EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY) return nullptr;

    if (!eglInitialize(egl_display, nullptr, nullptr)) return nullptr;

    // Get shared context if provided
    EGLContext shared_context = EGL_NO_CONTEXT;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
        shared_context = static_cast<EGLContext>(config.shared_graphics->native_context());
    }

    EGLConfig egl_config = choose_egl_config(egl_display, config, true); // Always use OpenGL ES on Android
    if (!egl_config) {
        eglTerminate(egl_display);
        return nullptr;
    }

    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)native_window, nullptr);
    if (egl_surface == EGL_NO_SURFACE) {
        eglTerminate(egl_display);
        return nullptr;
    }

    EGLContext egl_context = create_egl_context(egl_display, egl_config, true, shared_context);
    if (egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
        return nullptr;
    }

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    if (!gladLoadGLES2()) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display, egl_context);
        eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
        return nullptr;
    }

    if (config.vsync) {
        eglSwapInterval(egl_display, 1);
    } else {
        eglSwapInterval(egl_display, 0);
    }

    GraphicsOpenGLEGL* gfx = new GraphicsOpenGLEGL();
    gfx->egl_display = egl_display;
    gfx->egl_context = egl_context;
    gfx->egl_surface = egl_surface;
    gfx->egl_config = egl_config;
    gfx->owns_display = true;
    gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    return gfx;
}
#endif

//=============================================================================
// Creation for UWP (via ANGLE)
//=============================================================================

#ifdef WINDOW_PLATFORM_UWP
Graphics* create_opengl_graphics_corewindow(void* core_window, int width, int height, const Config& config) {
    // Get ANGLE display for UWP
    EGLint display_attribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE,
        EGL_NONE
    };

    EGLDisplay egl_display = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, display_attribs);
    if (egl_display == EGL_NO_DISPLAY) {
        // Fallback to default display
        egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
    if (egl_display == EGL_NO_DISPLAY) return nullptr;

    if (!eglInitialize(egl_display, nullptr, nullptr)) return nullptr;

    // Get shared context if provided
    EGLContext shared_context = EGL_NO_CONTEXT;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
        shared_context = static_cast<EGLContext>(config.shared_graphics->native_context());
    }

    // Use OpenGL ES on UWP via ANGLE
    EGLConfig egl_config = choose_egl_config(egl_display, config, true);
    if (!egl_config) {
        eglTerminate(egl_display);
        return nullptr;
    }

    // Create surface from CoreWindow
    EGLint surface_attribs[] = {
        EGL_NONE
    };
    EGLSurface egl_surface = eglCreateWindowSurface(egl_display, egl_config,
                                                     (EGLNativeWindowType)core_window, surface_attribs);
    if (egl_surface == EGL_NO_SURFACE) {
        eglTerminate(egl_display);
        return nullptr;
    }

    EGLContext egl_context = create_egl_context(egl_display, egl_config, true, shared_context);
    if (egl_context == EGL_NO_CONTEXT) {
        eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
        return nullptr;
    }

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    if (!gladLoadGLES2()) {
        eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display, egl_context);
        eglDestroySurface(egl_display, egl_surface);
        eglTerminate(egl_display);
        return nullptr;
    }

    eglSwapInterval(egl_display, config.vsync ? 1 : 0);

    GraphicsOpenGLEGL* gfx = new GraphicsOpenGLEGL();
    gfx->egl_display = egl_display;
    gfx->egl_context = egl_context;
    gfx->egl_surface = egl_surface;
    gfx->egl_config = egl_config;
    gfx->owns_display = true;
    gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    return gfx;
}
#endif

} // namespace window

#endif // !WINDOW_NO_OPENGL && (__linux__ || __ANDROID__ || WINDOW_PLATFORM_UWP)
