/*
 * window_x11.cpp - X11 (Linux) implementation
 * Backends: OpenGL, Vulkan
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <string>

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define VK_USE_PLATFORM_XLIB_KHR
#include <vulkan/vulkan.h>
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
bool select_glx_fbconfig(void* display, int screen, const Config& config,
                         void** out_fbconfig, void** out_visual, int* out_depth);
Graphics* create_opengl_graphics_x11(void* display, unsigned long window,
                                      void* fbconfig, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_xlib(void* display, unsigned long window, int width, int height, const Config& config);
#endif

//=============================================================================
// Implementation Structure
//=============================================================================

struct Window::Impl {
    Display* display = nullptr;
    ::Window xwindow = 0;
    int screen = 0;
    Atom wm_delete_window = 0;
    Atom wm_protocols = 0;
    bool should_close_flag = false;
    bool visible = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    bool is_fullscreen = false;
    // For fullscreen toggle restoration
    int windowed_x = 0;
    int windowed_y = 0;
    int windowed_width = 0;
    int windowed_height = 0;

#ifdef WINDOW_HAS_OPENGL
    void* fb_config = nullptr;
#endif
};

// Helper to send _NET_WM_STATE client message
static void send_wm_state_event(Display* display, ::Window window, bool add, Atom state1, Atom state2 = 0) {
    XEvent event = {};
    event.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = add ? 1 : 0; // _NET_WM_STATE_ADD or _NET_WM_STATE_REMOVE
    event.xclient.data.l[1] = state1;
    event.xclient.data.l[2] = state2;
    event.xclient.data.l[3] = 1; // Source indication: normal application

    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
}

//=============================================================================
// Window Implementation
//=============================================================================

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    int screen = DefaultScreen(display);

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->display = display;
    window->impl->screen = screen;
    window->impl->width = config.width;
    window->impl->height = config.height;
    window->impl->title = config.title;

    Visual* visual = DefaultVisual(display, screen);
    int depth = DefaultDepth(display, screen);
    Colormap colormap = 0;

    // Determine which backend to use
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

#ifdef WINDOW_HAS_OPENGL
    void* fb_config = nullptr;
    void* glx_visual = nullptr;
    int glx_depth = 0;
    // Only setup GLX if OpenGL is requested
    if (requested == Backend::OpenGL) {
        if (!select_glx_fbconfig(display, screen, config, &fb_config, &glx_visual, &glx_depth)) {
            XCloseDisplay(display);
            delete window->impl;
            delete window;
            set_result(Result::ErrorGraphicsInit);
            return nullptr;
        }
        window->impl->fb_config = fb_config;
        visual = static_cast<Visual*>(glx_visual);
        depth = glx_depth;
        colormap = XCreateColormap(display, RootWindow(display, screen), visual, AllocNone);
    }
#endif

    if (!colormap) {
        colormap = DefaultColormap(display, screen);
    }

    XSetWindowAttributes attrs = {};
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                       ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                       StructureNotifyMask | FocusChangeMask;
    attrs.colormap = colormap;
    attrs.background_pixel = BlackPixel(display, screen);
    attrs.border_pixel = 0;

    unsigned long attr_mask = CWBackPixel | CWEventMask | CWColormap | CWBorderPixel;

    int pos_x = config.x >= 0 ? config.x : 0;
    int pos_y = config.y >= 0 ? config.y : 0;

    ::Window xwindow = XCreateWindow(
        display,
        RootWindow(display, screen),
        pos_x, pos_y,
        config.width, config.height,
        0,
        depth,
        InputOutput,
        visual,
        attr_mask,
        &attrs
    );

    if (!xwindow) {
        XCloseDisplay(display);
        delete window->impl;
        delete window;
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    window->impl->xwindow = xwindow;

    // Set window title
    XStoreName(display, xwindow, config.title);

    // Set _NET_WM_NAME for UTF-8 support
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, xwindow, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char*)config.title, strlen(config.title));

    // Handle window close
    window->impl->wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    window->impl->wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, xwindow, &window->impl->wm_delete_window, 1);

    // Combine config.style with legacy config.resizable flag
    WindowStyle effective_style = config.style;
    if (!config.resizable) {
        effective_style = effective_style & ~WindowStyle::Resizable;
    }

    window->impl->style = effective_style;

    // Set resizable hints
    if (!has_style(effective_style, WindowStyle::Resizable)) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = config.width;
        hints->min_height = hints->max_height = config.height;
        XSetWMNormalHints(display, xwindow, hints);
        XFree(hints);
    }

    // Set window type hints for borderless/tool windows
    if (!has_style(effective_style, WindowStyle::TitleBar) && !has_style(effective_style, WindowStyle::Border)) {
        // Borderless window - set override redirect or use _MOTIF_WM_HINTS
        Atom motif_hints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
        struct {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } hints = { 2, 0, 0, 0, 0 }; // MWM_HINTS_DECORATIONS, no decorations
        XChangeProperty(display, xwindow, motif_hints, motif_hints, 32, PropModeReplace,
                        (unsigned char*)&hints, 5);
    }

    // Center window if position not specified
    if (config.x < 0 || config.y < 0) {
        int screen_width = DisplayWidth(display, screen);
        int screen_height = DisplayHeight(display, screen);
        int new_x = (screen_width - config.width) / 2;
        int new_y = (screen_height - config.height) / 2;
        XMoveWindow(display, xwindow, new_x, new_y);
        window->impl->x = new_x;
        window->impl->y = new_y;
    } else {
        window->impl->x = config.x;
        window->impl->y = config.y;
    }

    // Create graphics backend based on config.backend
    Graphics* gfx = nullptr;
    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_x11(display, xwindow, window->impl->fb_config, config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_xlib(display, xwindow, config.width, config.height, config);
            break;
#endif
        default:
            break;
    }

    // Fallback to Vulkan if requested backend failed (OpenGL fallback not possible
    // because window must be created with GLX-compatible visual)
    if (!gfx && config.backend != Backend::Auto) {
#ifdef WINDOW_HAS_VULKAN
        if (requested != Backend::Vulkan) {
            gfx = create_vulkan_graphics_xlib(display, xwindow, config.width, config.height, config);
        }
#endif
    }

    if (!gfx) {
        XDestroyWindow(display, xwindow);
        XCloseDisplay(display);
        delete window->impl;
        delete window;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;

    if (config.visible) {
        XMapWindow(display, xwindow);
        window->impl->visible = true;
    }

    XFlush(display);

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        if (impl->xwindow) {
            XDestroyWindow(impl->display, impl->xwindow);
        }
        if (impl->display) {
            XCloseDisplay(impl->display);
        }
        delete impl;
        impl = nullptr;
    }
    delete this;
}

void Window::show() {
    if (impl && impl->display && impl->xwindow) {
        XMapWindow(impl->display, impl->xwindow);
        XFlush(impl->display);
        impl->visible = true;
    }
}

void Window::hide() {
    if (impl && impl->display && impl->xwindow) {
        XUnmapWindow(impl->display, impl->xwindow);
        XFlush(impl->display);
        impl->visible = false;
    }
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl && impl->display && impl->xwindow) {
        XStoreName(impl->display, impl->xwindow, title);

        Atom net_wm_name = XInternAtom(impl->display, "_NET_WM_NAME", False);
        Atom utf8_string = XInternAtom(impl->display, "UTF8_STRING", False);
        XChangeProperty(impl->display, impl->xwindow, net_wm_name, utf8_string, 8, PropModeReplace,
                        (unsigned char*)title, strlen(title));

        XFlush(impl->display);
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl && impl->display && impl->xwindow) {
        XResizeWindow(impl->display, impl->xwindow, width, height);
        XFlush(impl->display);
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
    if (impl && impl->display && impl->xwindow) {
        XMoveWindow(impl->display, impl->xwindow, x, y);
        XFlush(impl->display);
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

bool Window::supports_position() const {
    return true;
}

void Window::set_style(WindowStyle style) {
    if (!impl || !impl->display || !impl->xwindow) return;

    impl->style = style;

    // Handle fullscreen
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    // Handle always-on-top
    Atom above = XInternAtom(impl->display, "_NET_WM_STATE_ABOVE", False);
    send_wm_state_event(impl->display, impl->xwindow, has_style(style, WindowStyle::AlwaysOnTop), above);

    // Handle resizable
    if (!has_style(style, WindowStyle::Resizable)) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = impl->width;
        hints->min_height = hints->max_height = impl->height;
        XSetWMNormalHints(impl->display, impl->xwindow, hints);
        XFree(hints);
    } else {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = 0;
        XSetWMNormalHints(impl->display, impl->xwindow, hints);
        XFree(hints);
    }

    // Handle decorations (title bar, border)
    Atom motif_hints = XInternAtom(impl->display, "_MOTIF_WM_HINTS", False);
    struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } hints = { 2, 0, 0, 0, 0 };

    if (has_style(style, WindowStyle::TitleBar) || has_style(style, WindowStyle::Border)) {
        hints.decorations = 1; // Enable decorations
    }

    XChangeProperty(impl->display, impl->xwindow, motif_hints, motif_hints, 32, PropModeReplace,
                    (unsigned char*)&hints, 5);

    XFlush(impl->display);
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !impl->display || !impl->xwindow) return;
    if (impl->is_fullscreen == fullscreen) return;

    Atom fullscreen_atom = XInternAtom(impl->display, "_NET_WM_STATE_FULLSCREEN", False);

    if (fullscreen) {
        // Save windowed state
        impl->windowed_x = impl->x;
        impl->windowed_y = impl->y;
        impl->windowed_width = impl->width;
        impl->windowed_height = impl->height;

        send_wm_state_event(impl->display, impl->xwindow, true, fullscreen_atom);
        impl->is_fullscreen = true;
        impl->style = impl->style | WindowStyle::Fullscreen;
    } else {
        send_wm_state_event(impl->display, impl->xwindow, false, fullscreen_atom);

        // Restore windowed state
        XMoveResizeWindow(impl->display, impl->xwindow,
                          impl->windowed_x, impl->windowed_y,
                          impl->windowed_width, impl->windowed_height);

        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }

    XFlush(impl->display);
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    if (!impl || !impl->display || !impl->xwindow) return;

    Atom above = XInternAtom(impl->display, "_NET_WM_STATE_ABOVE", False);
    send_wm_state_event(impl->display, impl->xwindow, always_on_top, above);

    if (always_on_top) {
        impl->style = impl->style | WindowStyle::AlwaysOnTop;
    } else {
        impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
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
    if (!impl || !impl->display) return;

    while (XPending(impl->display)) {
        XEvent event;
        XNextEvent(impl->display, &event);

        switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == impl->wm_delete_window) {
                    impl->should_close_flag = true;
                }
                break;

            case ConfigureNotify:
                if (event.xconfigure.width != impl->width || event.xconfigure.height != impl->height) {
                    impl->width = event.xconfigure.width;
                    impl->height = event.xconfigure.height;
                }
                impl->x = event.xconfigure.x;
                impl->y = event.xconfigure.y;
                break;

            case MapNotify:
                impl->visible = true;
                break;

            case UnmapNotify:
                impl->visible = false;
                break;

            default:
                break;
        }
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? reinterpret_cast<void*>(impl->xwindow) : nullptr;
}

void* Window::native_display() const {
    return impl ? impl->display : nullptr;
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

//=============================================================================
// Graphics Context for External Windows
//=============================================================================

Graphics* Graphics::create(const ExternalWindowConfig& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    if (!config.native_handle) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    if (!config.native_display) {
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
    Display* display = static_cast<Display*>(config.native_display);
    ::Window xwindow = reinterpret_cast<::Window>(config.native_handle);

    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL: {
            // For external windows with OpenGL, we need to select an FBConfig
            int screen = DefaultScreen(display);
            void* fb_config = nullptr;
            void* visual = nullptr;
            int depth = 0;
            if (select_glx_fbconfig(display, screen, internal_config, &fb_config, &visual, &depth)) {
                gfx = create_opengl_graphics_x11(display, xwindow, fb_config, internal_config);
            }
            break;
        }
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_xlib(display, xwindow, config.width, config.height, internal_config);
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

#endif // WINDOW_PLATFORM_X11
