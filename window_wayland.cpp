/*
 * window_wayland.cpp - Wayland (Linux) implementation
 * Backends: OpenGL (EGL), Vulkan
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_WAYLAND)

#include <wayland-client.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <linux/input.h>

//=============================================================================
// Backend Configuration
//=============================================================================

#if !defined(WINDOW_NO_OPENGL)
#define WINDOW_HAS_OPENGL 1
#endif

#if !defined(WINDOW_NO_VULKAN)
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#define WINDOW_HAS_VULKAN 1
#endif

// XDG Shell protocol definitions
extern "C" {
    struct xdg_wm_base;
    struct xdg_surface;
    struct xdg_toplevel;

    extern const struct wl_interface xdg_wm_base_interface;

    #define XDG_WM_BASE_DESTROY 0
    #define XDG_WM_BASE_CREATE_POSITIONER 1
    #define XDG_WM_BASE_GET_XDG_SURFACE 2
    #define XDG_WM_BASE_PONG 3

    static inline void xdg_wm_base_pong(struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
        wl_proxy_marshal((struct wl_proxy *)xdg_wm_base, XDG_WM_BASE_PONG, serial);
    }

    static inline void xdg_wm_base_destroy(struct xdg_wm_base *xdg_wm_base) {
        wl_proxy_marshal((struct wl_proxy *)xdg_wm_base, XDG_WM_BASE_DESTROY);
        wl_proxy_destroy((struct wl_proxy *)xdg_wm_base);
    }

    extern const struct wl_interface xdg_surface_interface;

    static inline struct xdg_surface* xdg_wm_base_get_xdg_surface(struct xdg_wm_base *xdg_wm_base, struct wl_surface *surface) {
        struct wl_proxy *id;
        id = wl_proxy_marshal_constructor((struct wl_proxy *)xdg_wm_base, XDG_WM_BASE_GET_XDG_SURFACE, &xdg_surface_interface, NULL, surface);
        return (struct xdg_surface *)id;
    }

    #define XDG_SURFACE_DESTROY 0
    #define XDG_SURFACE_GET_TOPLEVEL 1
    #define XDG_SURFACE_ACK_CONFIGURE 4

    extern const struct wl_interface xdg_toplevel_interface;

    static inline struct xdg_toplevel* xdg_surface_get_toplevel(struct xdg_surface *xdg_surface) {
        struct wl_proxy *id;
        id = wl_proxy_marshal_constructor((struct wl_proxy *)xdg_surface, XDG_SURFACE_GET_TOPLEVEL, &xdg_toplevel_interface, NULL);
        return (struct xdg_toplevel *)id;
    }

    static inline void xdg_surface_ack_configure(struct xdg_surface *xdg_surface, uint32_t serial) {
        wl_proxy_marshal((struct wl_proxy *)xdg_surface, XDG_SURFACE_ACK_CONFIGURE, serial);
    }

    static inline void xdg_surface_destroy(struct xdg_surface *xdg_surface) {
        wl_proxy_marshal((struct wl_proxy *)xdg_surface, XDG_SURFACE_DESTROY);
        wl_proxy_destroy((struct wl_proxy *)xdg_surface);
    }

    #define XDG_TOPLEVEL_DESTROY 0
    #define XDG_TOPLEVEL_SET_PARENT 1
    #define XDG_TOPLEVEL_SET_TITLE 2
    #define XDG_TOPLEVEL_SET_APP_ID 3
    #define XDG_TOPLEVEL_SET_MIN_SIZE 7
    #define XDG_TOPLEVEL_SET_MAX_SIZE 8

    static inline void xdg_toplevel_set_title(struct xdg_toplevel *xdg_toplevel, const char *title) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_TITLE, title);
    }

    static inline void xdg_toplevel_set_min_size(struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_MIN_SIZE, width, height);
    }

    static inline void xdg_toplevel_set_max_size(struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_MAX_SIZE, width, height);
    }

    static inline void xdg_toplevel_destroy(struct xdg_toplevel *xdg_toplevel) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_DESTROY);
        wl_proxy_destroy((struct wl_proxy *)xdg_toplevel);
    }

    struct xdg_wm_base_listener {
        void (*ping)(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial);
    };

    struct xdg_surface_listener {
        void (*configure)(void *data, struct xdg_surface *xdg_surface, uint32_t serial);
    };

    struct xdg_toplevel_listener {
        void (*configure)(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states);
        void (*close)(void *data, struct xdg_toplevel *xdg_toplevel);
    };

    static inline int xdg_wm_base_add_listener(struct xdg_wm_base *xdg_wm_base, const struct xdg_wm_base_listener *listener, void *data) {
        return wl_proxy_add_listener((struct wl_proxy *)xdg_wm_base, (void (**)(void))listener, data);
    }

    static inline int xdg_surface_add_listener(struct xdg_surface *xdg_surface, const struct xdg_surface_listener *listener, void *data) {
        return wl_proxy_add_listener((struct wl_proxy *)xdg_surface, (void (**)(void))listener, data);
    }

    static inline int xdg_toplevel_add_listener(struct xdg_toplevel *xdg_toplevel, const struct xdg_toplevel_listener *listener, void *data) {
        return wl_proxy_add_listener((struct wl_proxy *)xdg_toplevel, (void (**)(void))listener, data);
    }
}

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_wayland(void* wl_display, void* wl_surface, int width, int height, const Config& config);
void resize_opengl_graphics_wayland(Graphics* gfx, int width, int height);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_wayland(void* display, void* wl_surface, int width, int height, const Config& config);
#endif

//=============================================================================
// Implementation Structure
//=============================================================================

struct Window::Impl {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_surface* surface = nullptr;
    xdg_wm_base* wm_base = nullptr;
    xdg_surface* xdg_surf = nullptr;
    xdg_toplevel* toplevel = nullptr;

    bool should_close_flag = false;
    bool visible = false;
    bool configured = false;
    int width = 0;
    int height = 0;
    int pending_width = 0;
    int pending_height = 0;
    std::string title;
    Graphics* gfx = nullptr;
};

//=============================================================================
// Wayland Callbacks
//=============================================================================

static void registry_handle_global(void* data, wl_registry* registry,
                                    uint32_t name, const char* interface,
                                    uint32_t version) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    if (strcmp(interface, "wl_compositor") == 0) {
        impl->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        impl->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    }
}

static void registry_handle_global_remove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

static void xdg_wm_base_ping_handler(void* data, xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const xdg_wm_base_listener wm_base_listener = {
    xdg_wm_base_ping_handler
};

static void xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    xdg_surface_ack_configure(surface, serial);

    if (impl->pending_width > 0 && impl->pending_height > 0) {
        impl->width = impl->pending_width;
        impl->height = impl->pending_height;

#ifdef WINDOW_HAS_OPENGL
        if (impl->gfx && impl->gfx->get_backend() == Backend::OpenGL) {
            resize_opengl_graphics_wayland(impl->gfx, impl->width, impl->height);
        }
#endif
    }

    impl->configured = true;
}

static const xdg_surface_listener xdg_surface_listener_impl = {
    xdg_surface_configure_handler
};

static void xdg_toplevel_configure_handler(void* data, xdg_toplevel* toplevel,
                                            int32_t width, int32_t height,
                                            wl_array* states) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    if (width > 0 && height > 0) {
        impl->pending_width = width;
        impl->pending_height = height;
    }
}

static void xdg_toplevel_close_handler(void* data, xdg_toplevel* toplevel) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    impl->should_close_flag = true;
}

static const xdg_toplevel_listener toplevel_listener = {
    xdg_toplevel_configure_handler,
    xdg_toplevel_close_handler
};

//=============================================================================
// Window Implementation
//=============================================================================

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    wl_display* display = wl_display_connect(nullptr);
    if (!display) {
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->display = display;
    window->impl->width = config.width;
    window->impl->height = config.height;
    window->impl->pending_width = config.width;
    window->impl->pending_height = config.height;
    window->impl->title = config.title;

    window->impl->registry = wl_display_get_registry(display);
    wl_registry_add_listener(window->impl->registry, &registry_listener, window->impl);
    wl_display_roundtrip(display);

    if (!window->impl->compositor || !window->impl->wm_base) {
        wl_display_disconnect(display);
        delete window->impl;
        delete window;
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    xdg_wm_base_add_listener(window->impl->wm_base, &wm_base_listener, window->impl);

    window->impl->surface = wl_compositor_create_surface(window->impl->compositor);
    if (!window->impl->surface) {
        wl_display_disconnect(display);
        delete window->impl;
        delete window;
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    window->impl->xdg_surf = xdg_wm_base_get_xdg_surface(window->impl->wm_base, window->impl->surface);
    xdg_surface_add_listener(window->impl->xdg_surf, &xdg_surface_listener_impl, window->impl);

    window->impl->toplevel = xdg_surface_get_toplevel(window->impl->xdg_surf);
    xdg_toplevel_add_listener(window->impl->toplevel, &toplevel_listener, window->impl);
    xdg_toplevel_set_title(window->impl->toplevel, config.title);

    if (!config.resizable) {
        xdg_toplevel_set_min_size(window->impl->toplevel, config.width, config.height);
        xdg_toplevel_set_max_size(window->impl->toplevel, config.width, config.height);
    }

    wl_surface_commit(window->impl->surface);

    while (!window->impl->configured) {
        wl_display_dispatch(display);
    }

    // Create graphics backend based on config.backend
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_wayland(display, window->impl->surface, window->impl->width, window->impl->height, config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_wayland(display, window->impl->surface, config.width, config.height, config);
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
                gfx = create_opengl_graphics_wayland(display, window->impl->surface, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_wayland(display, window->impl->surface, config.width, config.height, config);
                break;
#endif
            default:
                break;
        }
    }

    if (!gfx) {
        xdg_toplevel_destroy(window->impl->toplevel);
        xdg_surface_destroy(window->impl->xdg_surf);
        wl_surface_destroy(window->impl->surface);
        wl_display_disconnect(display);
        delete window->impl;
        delete window;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;
    window->impl->visible = config.visible;

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        if (impl->toplevel) xdg_toplevel_destroy(impl->toplevel);
        if (impl->xdg_surf) xdg_surface_destroy(impl->xdg_surf);
        if (impl->surface) wl_surface_destroy(impl->surface);
        if (impl->wm_base) xdg_wm_base_destroy(impl->wm_base);
        if (impl->compositor) wl_compositor_destroy(impl->compositor);
        if (impl->registry) wl_registry_destroy(impl->registry);
        if (impl->display) wl_display_disconnect(impl->display);
        delete impl;
        impl = nullptr;
    }
    delete this;
}

void Window::show() {
    if (impl && impl->surface) {
        wl_surface_commit(impl->surface);
        wl_display_flush(impl->display);
        impl->visible = true;
    }
}

void Window::hide() {
    impl->visible = false;
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl && impl->toplevel) {
        xdg_toplevel_set_title(impl->toplevel, title);
        wl_display_flush(impl->display);
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl) {
        impl->width = width;
        impl->height = height;
#ifdef WINDOW_HAS_OPENGL
        if (impl->gfx && impl->gfx->get_backend() == Backend::OpenGL) {
            resize_opengl_graphics_wayland(impl->gfx, width, height);
        }
#endif
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
    (void)x; (void)y;
    return false;
}

bool Window::get_position(int* x, int* y) const {
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const { return false; }
bool Window::should_close() const { return impl ? impl->should_close_flag : true; }
void Window::set_should_close(bool close) { if (impl) impl->should_close_flag = close; }

void Window::poll_events() {
    if (impl && impl->display) {
        wl_display_dispatch_pending(impl->display);
        wl_display_flush(impl->display);
    }
}

Graphics* Window::graphics() const { return impl ? impl->gfx : nullptr; }
void* Window::native_handle() const { return impl ? impl->surface : nullptr; }
void* Window::native_display() const { return impl ? impl->display : nullptr; }

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

#endif // WINDOW_PLATFORM_WAYLAND
