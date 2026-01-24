/*
 * window_android.cpp - Android (NativeActivity) implementation
 * Backends: OpenGL ES (EGL), Vulkan
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_ANDROID)

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <android/looper.h>
#include <string>
#include <cstring>
#include <cstdint>

#define LOG_TAG "WindowHpp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

//=============================================================================
// Backend Configuration
//=============================================================================

#if !defined(WINDOW_NO_OPENGL)
#define WINDOW_HAS_OPENGL 1
#endif

#if !defined(WINDOW_NO_VULKAN)
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_android(void* native_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_android(void* native_window, int width, int height, const Config& config);
#endif

//=============================================================================
// Implementation Structure
//=============================================================================

struct Window::Impl {
    ANativeActivity* activity = nullptr;
    ANativeWindow* native_window = nullptr;
    ALooper* looper = nullptr;
    bool should_close_flag = false;
    bool visible = false;
    bool has_focus = false;
    int width = 0;
    int height = 0;
    std::string title;
    Graphics* gfx = nullptr;
    Config config;
};

// Global window instance for Android callbacks
static Window* g_android_window = nullptr;

//=============================================================================
// Graphics Initialization
//=============================================================================

static Graphics* create_graphics(ANativeWindow* native_window, int width, int height, const Config& config) {
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    // Try requested backend first
    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_android(native_window, width, height, config);
            if (gfx) {
                LOGI("Created OpenGL ES graphics backend");
                return gfx;
            }
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_android(native_window, width, height, config);
            if (gfx) {
                LOGI("Created Vulkan graphics backend");
                return gfx;
            }
            break;
#endif
        default:
            break;
    }

    // Fallback to default if requested backend failed or not supported
    if (!gfx && config.backend != Backend::Auto) {
        Backend fallback = get_default_backend();
        switch (fallback) {
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_android(native_window, width, height, config);
                if (gfx) {
                    LOGI("Created OpenGL ES graphics backend (fallback)");
                    return gfx;
                }
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_android(native_window, width, height, config);
                if (gfx) {
                    LOGI("Created Vulkan graphics backend (fallback)");
                    return gfx;
                }
                break;
#endif
            default:
                break;
        }
    }

    LOGE("Failed to create any graphics backend");
    return nullptr;
}

//=============================================================================
// Native Activity Callbacks
//=============================================================================

static void on_native_window_created(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowCreated");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->native_window = window;
        g_android_window->impl->width = ANativeWindow_getWidth(window);
        g_android_window->impl->height = ANativeWindow_getHeight(window);
        g_android_window->impl->visible = true;

        // Create graphics backend if not already created
        if (!g_android_window->impl->gfx) {
            g_android_window->impl->gfx = create_graphics(
                window,
                g_android_window->impl->width,
                g_android_window->impl->height,
                g_android_window->impl->config
            );
        }
    }
}

static void on_native_window_destroyed(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowDestroyed");
    if (g_android_window && g_android_window->impl) {
        // Destroy graphics backend
        delete g_android_window->impl->gfx;
        g_android_window->impl->gfx = nullptr;
        g_android_window->impl->native_window = nullptr;
        g_android_window->impl->visible = false;
    }
}

static void on_native_window_resized(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowResized");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->width = ANativeWindow_getWidth(window);
        g_android_window->impl->height = ANativeWindow_getHeight(window);
    }
}

static void on_window_focus_changed(ANativeActivity* activity, int has_focus) {
    LOGI("onWindowFocusChanged: %d", has_focus);
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->has_focus = (has_focus != 0);
    }
}

static void on_pause(ANativeActivity* activity) {
    LOGI("onPause");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->visible = false;
    }
}

static void on_resume(ANativeActivity* activity) {
    LOGI("onResume");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->visible = true;
    }
}

static void on_destroy(ANativeActivity* activity) {
    LOGI("onDestroy");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->should_close_flag = true;
    }
}

static void on_start(ANativeActivity* activity) {
    LOGI("onStart");
}

static void on_stop(ANativeActivity* activity) {
    LOGI("onStop");
}

static void on_configuration_changed(ANativeActivity* activity) {
    LOGI("onConfigurationChanged");
}

static void on_low_memory(ANativeActivity* activity) {
    LOGI("onLowMemory");
}

//=============================================================================
// Native Activity Entry Point
//=============================================================================

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,
                                          void* savedState, size_t savedStateSize) {
    LOGI("ANativeActivity_onCreate");

    activity->callbacks->onNativeWindowCreated = on_native_window_created;
    activity->callbacks->onNativeWindowDestroyed = on_native_window_destroyed;
    activity->callbacks->onNativeWindowResized = on_native_window_resized;
    activity->callbacks->onWindowFocusChanged = on_window_focus_changed;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onStart = on_start;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onConfigurationChanged = on_configuration_changed;
    activity->callbacks->onLowMemory = on_low_memory;

    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->activity = activity;
    }

    activity->instance = g_android_window;
}

//=============================================================================
// Window Implementation
//=============================================================================

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->width = config.width;
    window->impl->height = config.height;
    window->impl->title = config.title;
    window->impl->config = config;

    g_android_window = window;

    // Get looper for the current thread
    window->impl->looper = ALooper_forThread();
    if (!window->impl->looper) {
        window->impl->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }

    // Note: On Android, the actual window surface and graphics are created when
    // onNativeWindowCreated is called by the system

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        impl->gfx = nullptr;

        if (impl->activity) {
            ANativeActivity_finish(impl->activity);
        }
        delete impl;
        impl = nullptr;
    }
    g_android_window = nullptr;
    delete this;
}

void Window::show() {
    // Android manages window visibility
    if (impl) impl->visible = true;
}

void Window::hide() {
    // Android manages window visibility
    if (impl) impl->visible = false;
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    // Would require JNI to set activity title
    if (impl) impl->title = title;
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    // Android windows are managed by the system
    (void)width;
    (void)height;
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (impl->native_window) {
            if (width) *width = ANativeWindow_getWidth(impl->native_window);
            if (height) *height = ANativeWindow_getHeight(impl->native_window);
        } else {
            if (width) *width = impl->width;
            if (height) *height = impl->height;
        }
    }
}

int Window::get_width() const {
    if (impl && impl->native_window) {
        return ANativeWindow_getWidth(impl->native_window);
    }
    return impl ? impl->width : 0;
}

int Window::get_height() const {
    if (impl && impl->native_window) {
        return ANativeWindow_getHeight(impl->native_window);
    }
    return impl ? impl->height : 0;
}

bool Window::set_position(int x, int y) {
    // Android doesn't support window positioning
    (void)x;
    (void)y;
    return false;
}

bool Window::get_position(int* x, int* y) const {
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const {
    return false;
}

bool Window::should_close() const {
    return impl ? impl->should_close_flag : true;
}

void Window::set_should_close(bool close) {
    if (impl) impl->should_close_flag = close;
}

void Window::poll_events() {
    if (impl && impl->looper) {
        int events;
        void* data;

        // Poll all pending events
        while (ALooper_pollAll(0, nullptr, &events, &data) >= 0) {
            // Events processed
        }
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? impl->native_window : nullptr;
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
        case Backend::OpenGL: return "OpenGL ES";
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
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_OPENGL
    return Backend::OpenGL;
#elif defined(WINDOW_HAS_VULKAN)
    return Backend::Vulkan;
#else
    return Backend::Auto;
#endif
}

} // namespace window

#endif // WINDOW_PLATFORM_ANDROID
