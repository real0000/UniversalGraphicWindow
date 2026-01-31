/*
 * window_x11.cpp - X11 (Linux) implementation
 * Backends: OpenGL, Vulkan
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <ctime>
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
// Key Translation
//=============================================================================

static Key translate_keysym(KeySym keysym) {
    // Letters
    if (keysym >= XK_a && keysym <= XK_z) {
        return static_cast<Key>('A' + (keysym - XK_a));
    }
    if (keysym >= XK_A && keysym <= XK_Z) {
        return static_cast<Key>('A' + (keysym - XK_A));
    }

    // Numbers
    if (keysym >= XK_0 && keysym <= XK_9) {
        return static_cast<Key>('0' + (keysym - XK_0));
    }

    switch (keysym) {
        // Function keys
        case XK_F1: return Key::F1; case XK_F2: return Key::F2; case XK_F3: return Key::F3;
        case XK_F4: return Key::F4; case XK_F5: return Key::F5; case XK_F6: return Key::F6;
        case XK_F7: return Key::F7; case XK_F8: return Key::F8; case XK_F9: return Key::F9;
        case XK_F10: return Key::F10; case XK_F11: return Key::F11; case XK_F12: return Key::F12;

        // Navigation
        case XK_Escape: return Key::Escape;
        case XK_Tab: return Key::Tab;
        case XK_Caps_Lock: return Key::CapsLock;
        case XK_space: return Key::Space;
        case XK_Return: return Key::Enter;
        case XK_BackSpace: return Key::Backspace;
        case XK_Delete: return Key::Delete;
        case XK_Insert: return Key::Insert;
        case XK_Home: return Key::Home;
        case XK_End: return Key::End;
        case XK_Page_Up: return Key::PageUp;
        case XK_Page_Down: return Key::PageDown;
        case XK_Left: return Key::Left;
        case XK_Right: return Key::Right;
        case XK_Up: return Key::Up;
        case XK_Down: return Key::Down;

        // Modifiers
        case XK_Shift_L: return Key::LeftShift;
        case XK_Shift_R: return Key::RightShift;
        case XK_Control_L: return Key::LeftControl;
        case XK_Control_R: return Key::RightControl;
        case XK_Alt_L: return Key::LeftAlt;
        case XK_Alt_R: return Key::RightAlt;
        case XK_Super_L: return Key::LeftSuper;
        case XK_Super_R: return Key::RightSuper;

        // Punctuation
        case XK_grave: case XK_asciitilde: return Key::Grave;
        case XK_minus: case XK_underscore: return Key::Minus;
        case XK_equal: case XK_plus: return Key::Equal;
        case XK_bracketleft: case XK_braceleft: return Key::LeftBracket;
        case XK_bracketright: case XK_braceright: return Key::RightBracket;
        case XK_backslash: case XK_bar: return Key::Backslash;
        case XK_semicolon: case XK_colon: return Key::Semicolon;
        case XK_apostrophe: case XK_quotedbl: return Key::Apostrophe;
        case XK_comma: case XK_less: return Key::Comma;
        case XK_period: case XK_greater: return Key::Period;
        case XK_slash: case XK_question: return Key::Slash;

        // Numpad
        case XK_KP_0: return Key::Numpad0; case XK_KP_1: return Key::Numpad1;
        case XK_KP_2: return Key::Numpad2; case XK_KP_3: return Key::Numpad3;
        case XK_KP_4: return Key::Numpad4; case XK_KP_5: return Key::Numpad5;
        case XK_KP_6: return Key::Numpad6; case XK_KP_7: return Key::Numpad7;
        case XK_KP_8: return Key::Numpad8; case XK_KP_9: return Key::Numpad9;
        case XK_KP_Decimal: return Key::NumpadDecimal;
        case XK_KP_Enter: return Key::NumpadEnter;
        case XK_KP_Add: return Key::NumpadAdd;
        case XK_KP_Subtract: return Key::NumpadSubtract;
        case XK_KP_Multiply: return Key::NumpadMultiply;
        case XK_KP_Divide: return Key::NumpadDivide;
        case XK_Num_Lock: return Key::NumLock;

        // Other
        case XK_Print: return Key::PrintScreen;
        case XK_Scroll_Lock: return Key::ScrollLock;
        case XK_Pause: return Key::Pause;
        case XK_Menu: return Key::Menu;

        default: return Key::Unknown;
    }
}

static KeyMod get_x11_modifiers(unsigned int state) {
    KeyMod mods = KeyMod::None;
    if (state & ShiftMask) mods = mods | KeyMod::Shift;
    if (state & ControlMask) mods = mods | KeyMod::Control;
    if (state & Mod1Mask) mods = mods | KeyMod::Alt;
    if (state & Mod4Mask) mods = mods | KeyMod::Super;
    if (state & LockMask) mods = mods | KeyMod::CapsLock;
    if (state & Mod2Mask) mods = mods | KeyMod::NumLock;
    return mods;
}

static MouseButton translate_x11_button(unsigned int button) {
    switch (button) {
        case Button1: return MouseButton::Left;
        case Button2: return MouseButton::Middle;
        case Button3: return MouseButton::Right;
        case 8: return MouseButton::X1;  // Back button
        case 9: return MouseButton::X2;  // Forward button
        default: return MouseButton::Unknown;
    }
}

static double get_event_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

//=============================================================================
// Event Callbacks Storage
//=============================================================================

struct EventCallbacks {
    WindowCloseCallback close_callback;
    WindowResizeCallback resize_callback;
    WindowMoveCallback move_callback;
    WindowFocusCallback focus_callback;
    WindowStateCallback state_callback;
    TouchCallback touch_callback;
    DpiChangeCallback dpi_change_callback;
    DropFileCallback drop_file_callback;
};

//=============================================================================
// Implementation Structure
//=============================================================================

struct Window::Impl {
    Display* display = nullptr;
    ::Window xwindow = 0;
    Window* owner = nullptr;  // Back-pointer for event dispatch
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

    // Event callbacks
    EventCallbacks callbacks;

    // Input state
    bool mouse_in_window = false;
    bool focused = true;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;

    // XIM for text input
    XIM xim = nullptr;
    XIC xic = nullptr;

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

    const WindowConfigEntry& win_cfg = config.windows[0];

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->display = display;
    window->impl->screen = screen;
    window->impl->width = win_cfg.width;
    window->impl->height = win_cfg.height;
    window->impl->title = win_cfg.title;

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
                       StructureNotifyMask | FocusChangeMask |
                       EnterWindowMask | LeaveWindowMask;
    attrs.colormap = colormap;
    attrs.background_pixel = BlackPixel(display, screen);
    attrs.border_pixel = 0;

    unsigned long attr_mask = CWBackPixel | CWEventMask | CWColormap | CWBorderPixel;

    int pos_x = win_cfg.x >= 0 ? win_cfg.x : 0;
    int pos_y = win_cfg.y >= 0 ? win_cfg.y : 0;

    ::Window xwindow = XCreateWindow(
        display,
        RootWindow(display, screen),
        pos_x, pos_y,
        win_cfg.width, win_cfg.height,
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
    window->impl->owner = window;  // Set back-pointer for event dispatch

    // Initialize mouse input system
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
    window->impl->mouse_device.set_window(window);

    // Initialize XIM for text input
    window->impl->xim = XOpenIM(display, nullptr, nullptr, nullptr);
    if (window->impl->xim) {
        window->impl->xic = XCreateIC(window->impl->xim,
                                       XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                       XNClientWindow, xwindow,
                                       XNFocusWindow, xwindow,
                                       nullptr);
    }

    // Set window title
    XStoreName(display, xwindow, win_cfg.title);

    // Set _NET_WM_NAME for UTF-8 support
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, xwindow, net_wm_name, utf8_string, 8, PropModeReplace,
                    (unsigned char*)win_cfg.title, strlen(win_cfg.title));

    // Handle window close
    window->impl->wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    window->impl->wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, xwindow, &window->impl->wm_delete_window, 1);

    // Combine config.style with legacy config.resizable flag
    WindowStyle effective_style = win_cfg.style;
    if (!win_cfg.resizable) {
        effective_style = effective_style & ~WindowStyle::Resizable;
    }

    window->impl->style = effective_style;

    // Set resizable hints
    if (!has_style(effective_style, WindowStyle::Resizable)) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = win_cfg.width;
        hints->min_height = hints->max_height = win_cfg.height;
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
    if (win_cfg.x < 0 || win_cfg.y < 0) {
        int screen_width = DisplayWidth(display, screen);
        int screen_height = DisplayHeight(display, screen);
        int new_x = (screen_width - win_cfg.width) / 2;
        int new_y = (screen_height - win_cfg.height) / 2;
        XMoveWindow(display, xwindow, new_x, new_y);
        window->impl->x = new_x;
        window->impl->y = new_y;
    } else {
        window->impl->x = win_cfg.x;
        window->impl->y = win_cfg.y;
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
            gfx = create_vulkan_graphics_xlib(display, xwindow, win_cfg.width, win_cfg.height, config);
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
            gfx = create_vulkan_graphics_xlib(display, xwindow, win_cfg.width, win_cfg.height, config);
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

    if (win_cfg.visible) {
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
        if (impl->xic) {
            XDestroyIC(impl->xic);
        }
        if (impl->xim) {
            XCloseIM(impl->xim);
        }
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

        // Filter events for XIM
        if (impl->xic && XFilterEvent(&event, impl->xwindow)) {
            continue;
        }

        switch (event.type) {
            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == impl->wm_delete_window) {
                    impl->should_close_flag = true;
                    if (impl->callbacks.close_callback) {
                        WindowCloseEvent close_event;
                        close_event.type = EventType::WindowClose;
                        close_event.window = impl->owner;
                        close_event.timestamp = get_event_timestamp();
                        impl->callbacks.close_callback(close_event);
                    }
                }
                break;

            case ConfigureNotify: {
                bool size_changed = (event.xconfigure.width != impl->width ||
                                     event.xconfigure.height != impl->height);
                bool pos_changed = (event.xconfigure.x != impl->x || event.xconfigure.y != impl->y);

                if (size_changed) {
                    impl->width = event.xconfigure.width;
                    impl->height = event.xconfigure.height;
                    if (impl->callbacks.resize_callback) {
                        WindowResizeEvent resize_event;
                        resize_event.type = EventType::WindowResize;
                        resize_event.window = impl->owner;
                        resize_event.timestamp = get_event_timestamp();
                        resize_event.width = impl->width;
                        resize_event.height = impl->height;
                        resize_event.minimized = false;
                        impl->callbacks.resize_callback(resize_event);
                    }
                }

                if (pos_changed) {
                    impl->x = event.xconfigure.x;
                    impl->y = event.xconfigure.y;
                    if (impl->callbacks.move_callback) {
                        WindowMoveEvent move_event;
                        move_event.type = EventType::WindowMove;
                        move_event.window = impl->owner;
                        move_event.timestamp = get_event_timestamp();
                        move_event.x = impl->x;
                        move_event.y = impl->y;
                        impl->callbacks.move_callback(move_event);
                    }
                }
                break;
            }

            case MapNotify:
                impl->visible = true;
                break;

            case UnmapNotify:
                impl->visible = false;
                break;

            case FocusIn:
                impl->focused = true;
                if (impl->callbacks.focus_callback) {
                    WindowFocusEvent focus_event;
                    focus_event.type = EventType::WindowFocus;
                    focus_event.window = impl->owner;
                    focus_event.timestamp = get_event_timestamp();
                    focus_event.focused = true;
                    impl->callbacks.focus_callback(focus_event);
                }
                break;

            case FocusOut:
                impl->focused = false;
                // Reset key states on focus loss
                memset(impl->key_states, 0, sizeof(impl->key_states));
                impl->mouse_device.reset();
                if (impl->callbacks.focus_callback) {
                    WindowFocusEvent focus_event;
                    focus_event.type = EventType::WindowBlur;
                    focus_event.window = impl->owner;
                    focus_event.timestamp = get_event_timestamp();
                    focus_event.focused = false;
                    impl->callbacks.focus_callback(focus_event);
                }
                break;

            case KeyPress: {
                KeySym keysym;
                char text[32];
                int len = 0;

                if (impl->xic) {
                    Status status;
                    len = Xutf8LookupString(impl->xic, &event.xkey, text, sizeof(text) - 1, &keysym, &status);
                } else {
                    len = XLookupString(&event.xkey, text, sizeof(text) - 1, &keysym, nullptr);
                }

                Key key = translate_keysym(keysym);
                if (key != Key::Unknown && static_cast<int>(key) < 512) {
                    impl->key_states[static_cast<int>(key)] = true;
                }

                if (impl->callbacks.key_callback) {
                    KeyEvent key_event;
                    key_event.type = EventType::KeyDown;
                    key_event.window = impl->owner;
                    key_event.timestamp = get_event_timestamp();
                    key_event.key = key;
                    key_event.modifiers = get_x11_modifiers(event.xkey.state);
                    key_event.scancode = event.xkey.keycode;
                    key_event.repeat = false;
                    impl->callbacks.key_callback(key_event, impl->callbacks.key_user_data);
                }

                // Character input
                if (len > 0 && impl->callbacks.char_callback) {
                    text[len] = '\0';
                    // Decode UTF-8 to get codepoint
                    unsigned char* p = (unsigned char*)text;
                    uint32_t codepoint = 0;
                    if (p[0] < 0x80) {
                        codepoint = p[0];
                    } else if ((p[0] & 0xE0) == 0xC0 && len >= 2) {
                        codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
                    } else if ((p[0] & 0xF0) == 0xE0 && len >= 3) {
                        codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
                    } else if ((p[0] & 0xF8) == 0xF0 && len >= 4) {
                        codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                                    ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
                    }

                    if (codepoint >= 32 || codepoint == '\t' || codepoint == '\n' || codepoint == '\r') {
                        CharEvent char_event;
                        char_event.type = EventType::CharInput;
                        char_event.window = impl->owner;
                        char_event.timestamp = get_event_timestamp();
                        char_event.codepoint = codepoint;
                        char_event.modifiers = get_x11_modifiers(event.xkey.state);
                        impl->callbacks.char_callback(char_event, impl->callbacks.char_user_data);
                    }
                }
                break;
            }

            case KeyRelease: {
                // Check for key repeat (X11 generates KeyRelease+KeyPress for repeats)
                if (XPending(impl->display)) {
                    XEvent next;
                    XPeekEvent(impl->display, &next);
                    if (next.type == KeyPress && next.xkey.time == event.xkey.time &&
                        next.xkey.keycode == event.xkey.keycode) {
                        // This is a repeat, skip the release
                        XNextEvent(impl->display, &next);  // Consume the KeyPress

                        KeySym keysym = XkbKeycodeToKeysym(impl->display, event.xkey.keycode, 0, 0);
                        Key key = translate_keysym(keysym);

                        if (impl->callbacks.key_callback) {
                            KeyEvent key_event;
                            key_event.type = EventType::KeyRepeat;
                            key_event.window = impl->owner;
                            key_event.timestamp = get_event_timestamp();
                            key_event.key = key;
                            key_event.modifiers = get_x11_modifiers(event.xkey.state);
                            key_event.scancode = event.xkey.keycode;
                            key_event.repeat = true;
                            impl->callbacks.key_callback(key_event, impl->callbacks.key_user_data);
                        }
                        break;
                    }
                }

                KeySym keysym = XkbKeycodeToKeysym(impl->display, event.xkey.keycode, 0, 0);
                Key key = translate_keysym(keysym);

                if (key != Key::Unknown && static_cast<int>(key) < 512) {
                    impl->key_states[static_cast<int>(key)] = false;
                }

                if (impl->callbacks.key_callback) {
                    KeyEvent key_event;
                    key_event.type = EventType::KeyUp;
                    key_event.window = impl->owner;
                    key_event.timestamp = get_event_timestamp();
                    key_event.key = key;
                    key_event.modifiers = get_x11_modifiers(event.xkey.state);
                    key_event.scancode = event.xkey.keycode;
                    key_event.repeat = false;
                    impl->callbacks.key_callback(key_event, impl->callbacks.key_user_data);
                }
                break;
            }

            case ButtonPress: {
                unsigned int button = event.xbutton.button;
                int x = event.xbutton.x;
                int y = event.xbutton.y;
                KeyMod modifiers = get_x11_modifiers(event.xbutton.state);
                double timestamp = get_event_timestamp();

                // Scroll wheel (buttons 4-7)
                if (button >= 4 && button <= 7) {
                    float dx = 0, dy = 0;
                    switch (button) {
                        case 4: dy = 1.0f; break;  // Up
                        case 5: dy = -1.0f; break; // Down
                        case 6: dx = -1.0f; break; // Left
                        case 7: dx = 1.0f; break;  // Right
                    }
                    impl->mouse_device.inject_wheel(dx, dy, x, y, modifiers, timestamp);
                    break;
                }

                MouseButton btn = translate_x11_button(button);
                impl->mouse_device.inject_button_down(btn, x, y, 1, modifiers, timestamp);
                break;
            }

            case ButtonRelease: {
                unsigned int button = event.xbutton.button;
                int x = event.xbutton.x;
                int y = event.xbutton.y;

                // Ignore scroll wheel releases
                if (button >= 4 && button <= 7) break;

                MouseButton btn = translate_x11_button(button);
                impl->mouse_device.inject_button_up(btn, x, y, get_x11_modifiers(event.xbutton.state), get_event_timestamp());
                break;
            }

            case MotionNotify: {
                int x = event.xmotion.x;
                int y = event.xmotion.y;
                impl->mouse_device.inject_move(x, y, get_x11_modifiers(event.xmotion.state), get_event_timestamp());
                break;
            }

            case EnterNotify:
                impl->mouse_in_window = true;
                impl->mouse_device.inject_move(event.xcrossing.x, event.xcrossing.y, KeyMod::None, get_event_timestamp());
                break;

            case LeaveNotify:
                impl->mouse_in_window = false;
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
// Event Callback Setters
//=============================================================================

void Window::set_close_callback(WindowCloseCallback callback) {
    if (impl) { impl->callbacks.close_callback = callback; }
}

void Window::set_resize_callback(WindowResizeCallback callback) {
    if (impl) { impl->callbacks.resize_callback = callback; }
}

void Window::set_move_callback(WindowMoveCallback callback) {
    if (impl) { impl->callbacks.move_callback = callback; }
}

void Window::set_focus_callback(WindowFocusCallback callback) {
    if (impl) { impl->callbacks.focus_callback = callback; }
}

void Window::set_state_callback(WindowStateCallback callback) {
    if (impl) { impl->callbacks.state_callback = callback; }
}

void Window::set_touch_callback(TouchCallback callback) {
    if (impl) { impl->callbacks.touch_callback = callback; }
}

void Window::set_dpi_change_callback(DpiChangeCallback callback) {
    if (impl) { impl->callbacks.dpi_change_callback = callback; }
}

void Window::set_drop_file_callback(DropFileCallback callback) {
    if (impl) { impl->callbacks.drop_file_callback = callback; }
}

//=============================================================================
// Input State Queries
//=============================================================================

bool Window::is_key_down(Key key) const {
    if (!impl || key == Key::Unknown) return false;
    return impl->keyboard_device.is_key_down(key);
}

bool Window::is_mouse_button_down(MouseButton button) const {
    if (!impl) return false;
    return impl->mouse_device.is_button_down(button);
}

void Window::get_mouse_position(int* x, int* y) const {
    if (impl) {
        impl->mouse_device.get_position(x, y);
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

KeyMod Window::get_current_modifiers() const {
    if (!impl || !impl->display) return KeyMod::None;

    ::Window root, child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(impl->display, impl->xwindow, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
    return get_x11_modifiers(mask);
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

// key_to_string, mouse_button_to_string, event_type_to_string
// are implemented in input/input_keyboard.cpp

//=============================================================================
// Mouse Handler API
//=============================================================================

bool Window::add_mouse_handler(input::IMouseHandler* handler) {
    if (!impl) return false;
    return impl->mouse_dispatcher.add_handler(handler);
}

bool Window::remove_mouse_handler(input::IMouseHandler* handler) {
    if (!impl) return false;
    return impl->mouse_dispatcher.remove_handler(handler);
}

bool Window::remove_mouse_handler(const char* handler_id) {
    if (!impl) return false;
    return impl->mouse_dispatcher.remove_handler(handler_id);
}

input::MouseEventDispatcher* Window::get_mouse_dispatcher() {
    return impl ? &impl->mouse_dispatcher : nullptr;
}

//=============================================================================
// Keyboard Handler API
//=============================================================================

bool Window::add_keyboard_handler(input::IKeyboardHandler* handler) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.add_handler(handler);
}

bool Window::remove_keyboard_handler(input::IKeyboardHandler* handler) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.remove_handler(handler);
}

bool Window::remove_keyboard_handler(const char* handler_id) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.remove_handler(handler_id);
}

input::KeyboardEventDispatcher* Window::get_keyboard_dispatcher() {
    return impl ? &impl->keyboard_dispatcher : nullptr;
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
    internal_config.windows[0].width = config.width;
    internal_config.windows[0].height = config.height;
    internal_config.vsync = config.vsync;
    internal_config.samples = config.samples;
    // Derive color_bits from individual color channel bits
    internal_config.color_bits = config.red_bits + config.green_bits + config.blue_bits + config.alpha_bits;
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
