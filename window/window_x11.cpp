/*
 * window_x11.cpp - X11 (Linux) implementation
 * Backends: OpenGL, Vulkan
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"
#include "ibus_client.hpp"

#if defined(WINDOW_PLATFORM_X11)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <clocale>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

// X11 defines `None` and `Success` as macros that collide with enum members
// in window.hpp (MessageBoxIcon::None, Result::Success, ...).
#ifdef None
#undef None
#endif
#ifdef Success
#undef Success
#endif

// The raw values behind X11's None / Success macros (undef'd just above).
static constexpr unsigned long kX11None    = 0UL;   // X11 None (e.g. an empty Atom/property)
static constexpr int           kX11Success = 0;     // X11 Success (XGetWindowProperty status)

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <dirent.h>     // directory listing for the file dialog
#include <sys/stat.h>   // stat() to tell files from directories
#include <unistd.h>
#include <pwd.h>        // home directory fallback

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
    // CLIPBOARD selection support. We hold the copied text and serve it to
    // requesters via SelectionRequest; get_clipboard_text() converts the other way.
    Atom a_clipboard = 0;   // CLIPBOARD
    Atom a_targets   = 0;   // TARGETS
    Atom a_utf8      = 0;   // UTF8_STRING
    Atom a_text      = 0;   // TEXT
    Atom a_clip_prop = 0;   // our transfer property (UGW_CLIPBOARD)
    std::string clipboard_text;
    // Mouse-cursor shapes, created lazily and cached by CursorType.
    ::Cursor x_cursors[static_cast<int>(CursorType::Count)] = {};
    CursorType cur_cursor = CursorType::Arrow;
    bool should_close_flag = false;
    bool visible = false;
    int width = 0;        // physical (framebuffer) pixels
    int height = 0;
    int x = 0;
    int y = 0;
    int dpi = 96;
    float dpi_scale = 1.0f;
    std::string title;
    Graphics* gfx = nullptr;
    bool owns_graphics = true;  // Whether this window owns its graphics context
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

    // XIM for text input (legacy fallback)
    XIM xim = nullptr;
    XIC xic = nullptr;
    // Preferred path: talk to ibus directly over D-Bus (channel A) — see
    // ibus_client.hpp. When connected, the XIM xic is NOT created and keys are
    // routed here instead.
    IBusClient ibus;
    bool       use_ibus = false;

#ifdef WINDOW_HAS_OPENGL
    void* fb_config = nullptr;
#endif
};

// Detect screen DPI on X11.
// Priority:
//   1. GDK_DPI_SCALE / GDK_SCALE env vars (HiDPI fractional/integer hint)
//   2. Xft.dpi from RESOURCE_MANAGER (set by GNOME / DE)
//   3. Physical screen size from XDisplayWidth/HeightMM (often unreliable)
//   4. 96
static int detect_x11_dpi(Display* display, int screen) {
    // 1. GDK_SCALE (integer multiplier — only honour for HiDPI)
    int gdk_scale = 1;
    if (const char* s = std::getenv("GDK_SCALE")) {
        int v = std::atoi(s);
        if (v >= 1 && v <= 4) gdk_scale = v;
    }
    // 2. Xft.dpi from XResources
    char* rm = XResourceManagerString(display);
    if (rm && *rm) {
        XrmDatabase db = XrmGetStringDatabase(rm);
        if (db) {
            char* type = nullptr;
            XrmValue val = {};
            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &val) && val.addr) {
                double dpi = std::atof(val.addr);
                XrmDestroyDatabase(db);
                if (dpi > 0) return static_cast<int>(dpi * gdk_scale + 0.5);
            }
            XrmDestroyDatabase(db);
        }
    }
    // 3. Physical size fallback
    int wpx = DisplayWidth(display, screen);
    int wmm = DisplayWidthMM(display, screen);
    if (wmm > 0) {
        double dpi = (static_cast<double>(wpx) / wmm) * 25.4;
        // X often reports a fake 96 DPI; only trust values clearly above that.
        if (dpi > 100.0) return static_cast<int>(dpi * gdk_scale + 0.5);
    }
    return 96 * gdk_scale;
}

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

// XInitThreads() must precede every other Xlib call in the process. This app
// renders / pumps events on one thread while AI + network work runs on another
// (run_interactive), and Xlib's XIM synchronous round-trip — the blocking read
// XFilterEvent performs while an IME composes — corrupts/deadlocks the shared
// connection without Xlib's internal locks. (It only bites once an IME is
// actually focused, which is why plain ASCII never tripped it.) The function-
// local static runs the init exactly once, and both XOpenDisplay sites call
// this first so the very first Xlib call is always XInitThreads.
static void ensure_x_threads() {
    static const int initialized = XInitThreads();   // non-zero on success
    (void)initialized;
}

// Opt-in keyboard tracing (set AIW_KEY_DEBUG=1): logs every key/focus event and
// what each KeyPress decoded to, so input drops can be localised to the X layer.
static bool key_debug() {
    static const bool on = std::getenv("AIW_KEY_DEBUG") != nullptr;
    return on;
}

Window* create_window_impl(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    ensure_x_threads();
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
    window->impl->dpi = detect_x11_dpi(display, screen);
    window->impl->dpi_scale = static_cast<float>(window->impl->dpi) / 96.0f;
    // Convert logical → physical px.
    window->impl->width = static_cast<int>(win_cfg.width  * window->impl->dpi_scale + 0.5f);
    window->impl->height = static_cast<int>(win_cfg.height * window->impl->dpi_scale + 0.5f);
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

    int pos_x = win_cfg.x >= 0 ? static_cast<int>(win_cfg.x * window->impl->dpi_scale + 0.5f) : 0;
    int pos_y = win_cfg.y >= 0 ? static_cast<int>(win_cfg.y * window->impl->dpi_scale + 0.5f) : 0;

    ::Window xwindow = XCreateWindow(
        display,
        RootWindow(display, screen),
        pos_x, pos_y,
        window->impl->width, window->impl->height,
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

    // Initialize keyboard input system
    window->impl->keyboard_device.set_dispatcher(&window->impl->keyboard_dispatcher);
    window->impl->keyboard_device.set_window(window);

    // Initialize XIM for text input. XIM only talks to the platform input method
    // (ibus / fcitx) once the C locale and the X locale modifiers are set from the
    // environment; without this it stays in the "C" locale and composed CJK / other
    // non-Latin characters never arrive — input silently falls back to ASCII-only
    // XLookupString. Safe + idempotent to set here (honours $LC_*, $XMODIFIERS).
    if (std::setlocale(LC_CTYPE, "") && XSupportsLocale())
        XSetLocaleModifiers("");
    // Escape hatch: AIW_NO_IME=1 skips all input methods, so keys go straight
    // through XLookupString (ASCII-only, but always delivered).
    if (std::getenv("AIW_NO_IME") == nullptr) {
        // Preferred: talk to ibus directly over D-Bus (channel A) — the same path
        // Qt/GTK use. Works even when the legacy XIM bridge is disabled/broken.
        Window* w = window;
        w->impl->ibus.on_commit = [w](const std::string& utf8) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
            const size_t len = utf8.size();
            const KeyMod mods = get_x11_modifiers(0);
            const double ts = get_event_timestamp();
            for (size_t i = 0; i < len; ) {
                unsigned char c = p[i]; uint32_t cp = 0; int adv = 1;
                if (c < 0x80) { cp = c; adv = 1; }
                else if ((c & 0xE0) == 0xC0 && i + 1 < len) { cp = ((c & 0x1Fu) << 6) | (p[i+1] & 0x3Fu); adv = 2; }
                else if ((c & 0xF0) == 0xE0 && i + 2 < len) { cp = ((c & 0x0Fu) << 12) | ((p[i+1] & 0x3Fu) << 6) | (p[i+2] & 0x3Fu); adv = 3; }
                else if ((c & 0xF8) == 0xF0 && i + 3 < len) { cp = ((c & 0x07u) << 18) | ((p[i+1] & 0x3Fu) << 12) | ((p[i+2] & 0x3Fu) << 6) | (p[i+3] & 0x3Fu); adv = 4; }
                if (cp >= 32 || cp == '\t' || cp == '\n' || cp == '\r')
                    w->impl->keyboard_device.inject_char(cp, mods, ts);
                i += adv;
            }
        };
        w->impl->ibus.on_forward = [w](uint32_t keyval, uint32_t, uint32_t) {
            if (keyval >= 0x20 && keyval < 0x7f)        // a key ibus handed back
                w->impl->keyboard_device.inject_char(keyval, get_x11_modifiers(0), get_event_timestamp());
        };
        w->impl->ibus.on_preedit = [w](const std::string& text, int cursor) {  // composing text → inline
            w->impl->keyboard_device.inject_preedit(text, cursor);
        };
        window->impl->use_ibus = window->impl->ibus.connect("aiwrapper");
    }
    // Fall back to the legacy XIM bridge only if ibus D-Bus was unavailable.
    if (!window->impl->use_ibus && std::getenv("AIW_NO_IME") == nullptr)
        window->impl->xim = XOpenIM(display, nullptr, nullptr, nullptr);
    if (window->impl->xim) {
        window->impl->xic = XCreateIC(window->impl->xim,
                                       XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                       XNClientWindow, xwindow,
                                       XNFocusWindow, xwindow,
                                       nullptr);
        if (window->impl->xic) XSetICFocus(window->impl->xic);   // window starts focused
    }

    // Set window title
    XStoreName(display, xwindow, win_cfg.title.c_str());

    // Set _NET_WM_NAME for UTF-8 support
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XChangeProperty(display, xwindow, net_wm_name, utf8_string, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(win_cfg.title.c_str()),
                    static_cast<int>(win_cfg.title.size()));

    // Handle window close
    window->impl->wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    window->impl->wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, xwindow, &window->impl->wm_delete_window, 1);

    // CLIPBOARD selection atoms
    window->impl->a_clipboard = XInternAtom(display, "CLIPBOARD", False);
    window->impl->a_targets   = XInternAtom(display, "TARGETS", False);
    window->impl->a_utf8      = XInternAtom(display, "UTF8_STRING", False);
    window->impl->a_text      = XInternAtom(display, "TEXT", False);
    window->impl->a_clip_prop = XInternAtom(display, "UGW_CLIPBOARD", False);

    WindowStyle effective_style = win_cfg.style;
    window->impl->style = effective_style;

    // Set resizable hints
    if (!has_style(effective_style, WindowStyle::Resizable)) {
        XSizeHints* hints = XAllocSizeHints();
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = window->impl->width;
        hints->min_height = hints->max_height = window->impl->height;
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
        int new_x = (screen_width - window->impl->width) / 2;
        int new_y = (screen_height - window->impl->height) / 2;
        XMoveWindow(display, xwindow, new_x, new_y);
        window->impl->x = new_x;
        window->impl->y = new_y;
    } else {
        window->impl->x = pos_x;
        window->impl->y = pos_y;
    }

    // Use shared graphics if provided, otherwise create new one
    Graphics* gfx = config.shared_graphics;

    if (!gfx) {
        // Create graphics backend based on config.backend
        switch (requested) {
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_x11(display, xwindow, window->impl->fb_config, config);
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_xlib(display, xwindow, window->impl->width, window->impl->height, config);
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
                gfx = create_vulkan_graphics_xlib(display, xwindow, window->impl->width, window->impl->height, config);
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
    }

    window->impl->gfx = gfx;
    window->impl->owns_graphics = (config.shared_graphics == nullptr);

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
        // Capture the backend before the graphics object is freed below.
        const bool vulkan_backend = impl->gfx && impl->gfx->get_backend() == Backend::Vulkan;
        if (impl->owns_graphics && impl->gfx) {
            impl->gfx->destroy();
        }
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
            // NVIDIA's Vulkan ICD leaves a dangling XESetCloseDisplay hook after
            // vkDestroyInstance, so XCloseDisplay() then jumps into freed driver
            // code and crashes. Leak the Display for Vulkan windows (a one-time
            // teardown leak — the same mitigation GLFW uses for this driver bug).
            if (!vulkan_backend) XCloseDisplay(impl->display);
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

    // Decode a key event (IME-aware) and feed the key-down + any character(s) to
    // the input devices. Shared by the normal KeyPress path and the auto-repeat
    // collapse below, so a held/repeated key still emits repeated characters.
    auto handle_key_input = [&](XKeyEvent& ke, bool repeat) {
        KeySym keysym = 0;
        Status status = 0;
        char stackbuf[64];
        char* text = stackbuf;
        std::string heap;          // only used if an IME commit overflows stackbuf
        int len = 0;
        if (impl->xic) {
            len = Xutf8LookupString(impl->xic, &ke, text, sizeof(stackbuf) - 1, &keysym, &status);
            if (status == XBufferOverflow && len > 0) {
                heap.resize(static_cast<size_t>(len) + 1);
                text = heap.data();
                len = Xutf8LookupString(impl->xic, &ke, text, len, &keysym, &status);
            }
        } else {
            len = XLookupString(&ke, text, sizeof(stackbuf) - 1, &keysym, nullptr);
        }
        const Key    key  = translate_keysym(keysym);
        const KeyMod mods = get_x11_modifiers(ke.state);
        const double ts   = get_event_timestamp();
        impl->keyboard_device.inject_key_down(key, mods, ke.keycode, repeat, ts);
        if (key_debug())
            std::fprintf(stderr, "[key] %s keysym=0x%lx len=%d status=%d\n",
                         repeat ? "repeat" : "down", (unsigned long)keysym, len, (int)status);
        // An IME commit may carry several codepoints in one event (a whole CJK
        // word); walk the UTF-8 buffer and inject each.
        if (len > 0 && status != XLookupKeySym) {
            const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
            for (int i = 0; i < len; ) {
                unsigned char c = p[i];
                uint32_t cp = 0; int adv = 1;
                if (c < 0x80) {
                    cp = c; adv = 1;
                } else if ((c & 0xE0) == 0xC0 && i + 1 < len) {
                    cp = ((c & 0x1Fu) << 6) | (p[i + 1] & 0x3Fu); adv = 2;
                } else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
                    cp = ((c & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu); adv = 3;
                } else if ((c & 0xF8) == 0xF0 && i + 3 < len) {
                    cp = ((c & 0x07u) << 18) | ((p[i + 1] & 0x3Fu) << 12) |
                         ((p[i + 2] & 0x3Fu) << 6) | (p[i + 3] & 0x3Fu); adv = 4;
                }
                if (cp >= 32 || cp == '\t' || cp == '\n' || cp == '\r') {
                    impl->keyboard_device.inject_char(cp, mods, ts);
                    if (key_debug()) std::fprintf(stderr, "[key]   inject_char U+%04X\n", cp);
                }
                i += adv;
            }
        }
    };

    while (XPending(impl->display)) {
        XEvent event;
        XNextEvent(impl->display, &event);

        // Filter events for XIM
        if (impl->xic && XFilterEvent(&event, impl->xwindow)) {
            if (key_debug() && (event.type == KeyPress || event.type == KeyRelease))
                std::fprintf(stderr, "[key] FILTERED type=%d keycode=%u\n",
                             event.type, event.xkey.keycode);
            continue;
        }
        if (key_debug() && (event.type == KeyPress || event.type == FocusIn || event.type == FocusOut))
            std::fprintf(stderr, "[xev] type=%d focus_mode=%d\n", event.type,
                         (event.type == FocusIn || event.type == FocusOut) ? event.xfocus.mode : -1);

        switch (event.type) {
            case SelectionClear:
                // Another app took CLIPBOARD ownership; drop our cached text.
                if (event.xselectionclear.selection == impl->a_clipboard)
                    impl->clipboard_text.clear();
                break;

            case SelectionRequest: {
                // Serve our copied text to a requester (paste in another app).
                const XSelectionRequestEvent& req = event.xselectionrequest;
                XSelectionEvent notify{};
                notify.type      = SelectionNotify;
                notify.display   = req.display;
                notify.requestor = req.requestor;
                notify.selection = req.selection;
                notify.target    = req.target;
                notify.property  = req.property ? req.property : req.target;
                notify.time      = req.time;
                if (req.target == impl->a_targets) {
                    Atom targets[] = {impl->a_targets, impl->a_utf8, impl->a_text, XA_STRING};
                    XChangeProperty(impl->display, req.requestor, notify.property, XA_ATOM, 32,
                                    PropModeReplace, reinterpret_cast<unsigned char*>(targets),
                                    static_cast<int>(sizeof(targets) / sizeof(targets[0])));
                } else if (req.target == impl->a_utf8 || req.target == XA_STRING ||
                           req.target == impl->a_text) {
                    XChangeProperty(impl->display, req.requestor, notify.property, req.target, 8,
                                    PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(impl->clipboard_text.data()),
                                    static_cast<int>(impl->clipboard_text.size()));
                } else {
                    notify.property = kX11None;   // unsupported target
                }
                XSendEvent(impl->display, req.requestor, False, 0,
                           reinterpret_cast<XEvent*>(&notify));
                XFlush(impl->display);
                break;
            }

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
                // Grab/ungrab pseudo-focus (e.g. the IME's candidate window taking
                // a keyboard grab) must not re-focus the IC — doing so resets the
                // in-progress composition, so the next keystroke is lost.
                if (event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab)
                    break;
                impl->focused = true;
                if (impl->use_ibus) impl->ibus.focus_in();
                else if (impl->xic) XSetICFocus(impl->xic);   // route IME composition to this IC
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
                // Ignore grab/ungrab pseudo-focus: the IME candidate window grabs
                // the keyboard mid-composition, and tearing down the IC + resetting
                // input here is exactly what drops the second composition key.
                if (event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab)
                    break;
                impl->focused = false;
                if (impl->use_ibus) impl->ibus.focus_out();
                // NOTE: deliberately do NOT XUnsetICFocus here. Transient FocusOut
                // events (the IME's own preedit/candidate window, the compositor)
                // would otherwise reset the in-progress composition. ibus routes by
                // whichever IC last took focus, so leaving ours focused is safe; the
                // next FocusIn re-asserts XSetICFocus anyway.
                // Reset input state on focus loss to avoid stuck keys
                impl->keyboard_device.reset();
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
                // ibus (D-Bus) gets first refusal: if it consumes the key for
                // composition, the resulting text arrives via on_commit; we must
                // not also decode it locally.
                if (impl->use_ibus) {
                    char t8[8]; KeySym ks = 0;
                    XLookupString(&event.xkey, t8, sizeof t8, &ks, nullptr);
                    if (impl->ibus.process_key(uint32_t(ks), event.xkey.keycode - 8, event.xkey.state, true)) {
                        if (key_debug()) std::fprintf(stderr, "[key] ibus handled keysym=0x%lx\n", (unsigned long)ks);
                        break;
                    }
                }
                handle_key_input(event.xkey, false);
                break;
            }

            case KeyRelease: {
                // Let ibus see releases too (engines track them); if it consumes
                // the release, skip local handling. Done before the auto-repeat
                // collapse so the collapse only governs the non-ibus path.
                if (impl->use_ibus) {
                    char t8[8]; KeySym ks = 0;
                    XLookupString(&event.xkey, t8, sizeof t8, &ks, nullptr);
                    if (impl->ibus.process_key(uint32_t(ks), event.xkey.keycode - 8, event.xkey.state, false))
                        break;
                }
                // X11 generates KeyRelease+KeyPress pairs for auto-repeat — collapse them
                // into a single KeyRepeat event.
                if (XPending(impl->display)) {
                    XEvent next;
                    XPeekEvent(impl->display, &next);
                    if (next.type == KeyPress && next.xkey.time == event.xkey.time &&
                        next.xkey.keycode == event.xkey.keycode) {
                        XNextEvent(impl->display, &next);  // Consume the paired KeyPress
                        // Auto-repeat: emit the KeyRepeat *and* the character(s), so
                        // holding/repeating a key keeps inserting text (decode the
                        // consumed KeyPress, which carries the right state for lookup).
                        handle_key_input(next.xkey, true);
                        break;
                    }
                }

                KeySym keysym = XkbKeycodeToKeysym(impl->display, event.xkey.keycode, 0, 0);
                Key key = translate_keysym(keysym);
                KeyMod mods = get_x11_modifiers(event.xkey.state);
                impl->keyboard_device.inject_key_up(key, mods, event.xkey.keycode, get_event_timestamp());
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
    // Drain any ibus signals that arrived outside a ProcessKeyEvent round-trip
    // (late commits / preedit updates).
    if (impl->use_ibus) impl->ibus.pump();
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void Window::set_cursor(CursorType cursor) {
    if (!impl || !impl->display || cursor == impl->cur_cursor) return;
    impl->cur_cursor = cursor;
    unsigned int shape = XC_left_ptr;   // Arrow / default
    switch (cursor) {
        case CursorType::IBeam:      shape = XC_xterm;              break;
        case CursorType::Hand:       shape = XC_hand2;              break;
        case CursorType::Crosshair:  shape = XC_crosshair;          break;
        case CursorType::ResizeH:    shape = XC_sb_h_double_arrow;  break;
        case CursorType::ResizeV:    shape = XC_sb_v_double_arrow;  break;
        case CursorType::ResizeAll:  shape = XC_fleur;              break;
        case CursorType::Wait:
        case CursorType::WaitArrow:  shape = XC_watch;              break;
        case CursorType::NotAllowed: shape = XC_X_cursor;           break;
        case CursorType::Help:       shape = XC_question_arrow;     break;
        default:                     shape = XC_left_ptr;           break;
    }
    const int idx = static_cast<int>(cursor);
    if (idx < 0 || idx >= static_cast<int>(CursorType::Count)) return;
    if (!impl->x_cursors[idx])
        impl->x_cursors[idx] = XCreateFontCursor(impl->display, shape);
    XDefineCursor(impl->display, impl->xwindow, impl->x_cursors[idx]);
    XFlush(impl->display);
}

void* Window::native_handle() const {
    return impl ? reinterpret_cast<void*>(impl->xwindow) : nullptr;
}

void* Window::native_display() const {
    return impl ? impl->display : nullptr;
}

void Window::set_clipboard_text(const char* utf8) {
    if (!impl || !impl->display) return;
    impl->clipboard_text = utf8 ? utf8 : "";
    // Own CLIPBOARD; requesters are served from the event loop (SelectionRequest).
    XSetSelectionOwner(impl->display, impl->a_clipboard, impl->xwindow, CurrentTime);
    XFlush(impl->display);
}

std::string Window::get_clipboard_text() {
    if (!impl || !impl->display) return {};
    // Fast path: we own the selection — no round-trip needed.
    if (XGetSelectionOwner(impl->display, impl->a_clipboard) == impl->xwindow)
        return impl->clipboard_text;
    // Ask the current owner to write UTF8_STRING into our property, then wait
    // briefly for the SelectionNotify (bounded so we never stall the UI).
    XConvertSelection(impl->display, impl->a_clipboard, impl->a_utf8, impl->a_clip_prop,
                      impl->xwindow, CurrentTime);
    XFlush(impl->display);
    XEvent ev;
    for (int spins = 0; spins < 100; ++spins) {   // ~100ms budget
        if (XCheckTypedWindowEvent(impl->display, impl->xwindow, SelectionNotify, &ev)) {
            if (ev.xselection.property == kX11None) return {};
            Atom actual_type = 0; int actual_fmt = 0;
            unsigned long nitems = 0, bytes_after = 0; unsigned char* prop = nullptr;
            if (XGetWindowProperty(impl->display, impl->xwindow, impl->a_clip_prop, 0, (~0L), True,
                                   AnyPropertyType, &actual_type, &actual_fmt, &nitems,
                                   &bytes_after, &prop) == kX11Success && prop) {
                std::string out(reinterpret_cast<char*>(prop), nitems);
                XFree(prop);
                return out;
            }
            return {};
        }
        usleep(1000);
    }
    return {};
}

float Window::get_dpi_scale() const { return impl ? impl->dpi_scale : 1.0f; }
int   Window::get_dpi() const       { return impl ? impl->dpi       : 96;  }

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

//=============================================================================
// Message Box
//=============================================================================

namespace {

struct X11MsgBoxButton {
    std::string label;
    MessageBoxButton result;
    int x, y, width, height;
    bool hovered;
    bool pressed;
};

struct X11MsgBoxState {
    Display* display;
    ::Window dialog;
    GC gc;
    XFontStruct* font;

    std::vector<std::string> message_lines;
    std::vector<X11MsgBoxButton> buttons;

    int dialog_width;
    int dialog_height;
    int icon_size;
    int text_x;
    int button_y;
    int default_button;
    int cancel_button;
    MessageBoxIcon icon;

    MessageBoxButton result;
    bool done;
    Atom wm_delete_window;
    Atom wm_protocols;
};

static void x11_msgbox_compute_layout(X11MsgBoxState& s, const char* message, MessageBoxType type) {
    const int padding = 20;
    const int btn_height = 28;
    const int btn_pad = 12;
    const int btn_spacing = 10;
    const int icon_dim = 32;
    const int icon_text_gap = 12;
    const int text_btn_gap = 20;
    const int min_btn_width = 80;
    const int line_spacing = 4;

    int font_height = s.font->ascent + s.font->descent;

    // Split message into lines
    s.message_lines.clear();
    std::string msg = message ? message : "";
    size_t pos;
    while ((pos = msg.find('\n')) != std::string::npos) {
        s.message_lines.push_back(msg.substr(0, pos));
        msg = msg.substr(pos + 1);
    }
    if (!msg.empty() || s.message_lines.empty()) {
        s.message_lines.push_back(msg);
    }

    // Text width
    int max_text_width = 0;
    for (const auto& line : s.message_lines) {
        int w = XTextWidth(s.font, line.c_str(), (int)line.size());
        if (w > max_text_width) max_text_width = w;
    }

    bool has_icon = (s.icon != MessageBoxIcon::None);
    s.icon_size = has_icon ? icon_dim : 0;
    s.text_x = padding + (has_icon ? (icon_dim + icon_text_gap) : 0);

    int text_area_height = (int)s.message_lines.size() * (font_height + line_spacing);

    // Build buttons
    s.buttons.clear();
    s.default_button = 0;
    s.cancel_button = -1;

    auto add_btn = [&](const char* label, MessageBoxButton result) {
        X11MsgBoxButton btn;
        btn.label = label;
        btn.result = result;
        btn.width = std::max(min_btn_width,
            XTextWidth(s.font, label, (int)strlen(label)) + btn_pad * 2);
        btn.height = btn_height;
        btn.hovered = false;
        btn.pressed = false;
        s.buttons.push_back(btn);
    };

    switch (type) {
        case MessageBoxType::Ok:
            add_btn("OK", MessageBoxButton::Ok);
            s.cancel_button = 0;
            break;
        case MessageBoxType::OkCancel:
            add_btn("OK", MessageBoxButton::Ok);
            add_btn("Cancel", MessageBoxButton::Cancel);
            s.cancel_button = 1;
            break;
        case MessageBoxType::YesNo:
            add_btn("Yes", MessageBoxButton::Yes);
            add_btn("No", MessageBoxButton::No);
            s.cancel_button = 1;
            break;
        case MessageBoxType::YesNoCancel:
            add_btn("Yes", MessageBoxButton::Yes);
            add_btn("No", MessageBoxButton::No);
            add_btn("Cancel", MessageBoxButton::Cancel);
            s.cancel_button = 2;
            break;
        case MessageBoxType::RetryCancel:
            add_btn("Retry", MessageBoxButton::Retry);
            add_btn("Cancel", MessageBoxButton::Cancel);
            s.cancel_button = 1;
            break;
        case MessageBoxType::AbortRetryIgnore:
            add_btn("Abort", MessageBoxButton::Abort);
            add_btn("Retry", MessageBoxButton::Retry);
            add_btn("Ignore", MessageBoxButton::Ignore);
            s.cancel_button = -1;
            break;
    }

    // Total button row width
    int total_btn_width = 0;
    for (auto& btn : s.buttons) {
        total_btn_width += btn.width;
    }
    total_btn_width += btn_spacing * ((int)s.buttons.size() - 1);

    // Dialog size
    int content_width = std::max(max_text_width + s.text_x - padding, total_btn_width);
    s.dialog_width = content_width + padding * 2;
    s.dialog_width = std::max(s.dialog_width, 300);

    s.button_y = padding + std::max(text_area_height, s.icon_size) + text_btn_gap;
    s.dialog_height = s.button_y + btn_height + padding;

    // Position buttons centered
    int btn_x = (s.dialog_width - total_btn_width) / 2;
    for (auto& btn : s.buttons) {
        btn.x = btn_x;
        btn.y = s.button_y;
        btn_x += btn.width + btn_spacing;
    }
}

static void x11_msgbox_draw_icon(X11MsgBoxState& s) {
    if (s.icon == MessageBoxIcon::None) return;

    int cx = 20 + 16;
    int cy = 20 + 16;

    switch (s.icon) {
        case MessageBoxIcon::Info: {
            XSetForeground(s.display, s.gc, 0x3366CC);
            XFillArc(s.display, s.dialog, s.gc, cx - 14, cy - 14, 28, 28, 0, 360 * 64);
            XSetForeground(s.display, s.gc, 0xFFFFFF);
            XDrawString(s.display, s.dialog, s.gc, cx - 2, cy + 5, "i", 1);
            break;
        }
        case MessageBoxIcon::Warning: {
            XPoint tri[3] = {{(short)cx, (short)(cy - 14)},
                             {(short)(cx - 14), (short)(cy + 12)},
                             {(short)(cx + 14), (short)(cy + 12)}};
            XSetForeground(s.display, s.gc, 0xFFAA00);
            XFillPolygon(s.display, s.dialog, s.gc, tri, 3, Convex, CoordModeOrigin);
            XSetForeground(s.display, s.gc, 0x000000);
            XDrawString(s.display, s.dialog, s.gc, cx - 2, cy + 8, "!", 1);
            break;
        }
        case MessageBoxIcon::Error: {
            XSetForeground(s.display, s.gc, 0xCC3333);
            XFillArc(s.display, s.dialog, s.gc, cx - 14, cy - 14, 28, 28, 0, 360 * 64);
            XSetForeground(s.display, s.gc, 0xFFFFFF);
            XDrawString(s.display, s.dialog, s.gc, cx - 3, cy + 5, "X", 1);
            break;
        }
        case MessageBoxIcon::Question: {
            XSetForeground(s.display, s.gc, 0x3366CC);
            XFillArc(s.display, s.dialog, s.gc, cx - 14, cy - 14, 28, 28, 0, 360 * 64);
            XSetForeground(s.display, s.gc, 0xFFFFFF);
            XDrawString(s.display, s.dialog, s.gc, cx - 3, cy + 5, "?", 1);
            break;
        }
        default: break;
    }
}

static void x11_msgbox_draw(X11MsgBoxState& s) {
    // Background
    XSetForeground(s.display, s.gc, 0xF0F0F0);
    XFillRectangle(s.display, s.dialog, s.gc, 0, 0, s.dialog_width, s.dialog_height);

    // Icon
    x11_msgbox_draw_icon(s);

    // Message text
    int font_height = s.font->ascent + s.font->descent;
    int line_spacing = 4;
    XSetForeground(s.display, s.gc, 0x000000);
    for (size_t i = 0; i < s.message_lines.size(); i++) {
        int text_y = 20 + s.font->ascent + (int)i * (font_height + line_spacing);
        XDrawString(s.display, s.dialog, s.gc,
                    s.text_x, text_y,
                    s.message_lines[i].c_str(), (int)s.message_lines[i].size());
    }

    // Buttons
    for (size_t i = 0; i < s.buttons.size(); i++) {
        auto& btn = s.buttons[i];

        unsigned long bg;
        if (btn.pressed) {
            bg = 0xA0A0A0;
        } else if (btn.hovered) {
            bg = 0xD8D8D8;
        } else if ((int)i == s.default_button) {
            bg = 0x4488CC;
        } else {
            bg = 0xE0E0E0;
        }

        XSetForeground(s.display, s.gc, bg);
        XFillRectangle(s.display, s.dialog, s.gc, btn.x, btn.y, btn.width, btn.height);

        XSetForeground(s.display, s.gc, 0x888888);
        XDrawRectangle(s.display, s.dialog, s.gc, btn.x, btn.y, btn.width - 1, btn.height - 1);

        unsigned long text_color = ((int)i == s.default_button && !btn.pressed)
                                   ? 0xFFFFFF : 0x000000;
        XSetForeground(s.display, s.gc, text_color);
        int tw = XTextWidth(s.font, btn.label.c_str(), (int)btn.label.size());
        int tx = btn.x + (btn.width - tw) / 2;
        int ty = btn.y + (btn.height + s.font->ascent - s.font->descent) / 2;
        XDrawString(s.display, s.dialog, s.gc, tx, ty,
                    btn.label.c_str(), (int)btn.label.size());
    }
}

} // anonymous namespace

MessageBoxButton Window::show_message_box(
    const char* title,
    const char* message,
    MessageBoxType type,
    MessageBoxIcon icon,
    Window* parent)
{
    ensure_x_threads();
    Display* display = XOpenDisplay(nullptr);
    if (!display) return MessageBoxButton::None;

    int screen = DefaultScreen(display);

    X11MsgBoxState s;
    s.display = display;
    s.icon = icon;
    s.result = MessageBoxButton::None;
    s.done = false;

    // Load font
    s.font = XLoadQueryFont(display, "-*-helvetica-medium-r-*-*-14-*-*-*-*-*-*-*");
    if (!s.font) s.font = XLoadQueryFont(display, "-*-fixed-medium-r-*-*-14-*-*-*-*-*-*-*");
    if (!s.font) s.font = XLoadQueryFont(display, "fixed");
    if (!s.font) {
        XCloseDisplay(display);
        return MessageBoxButton::None;
    }

    // Compute layout
    x11_msgbox_compute_layout(s, message, type);

    // Position: center on parent or screen
    int pos_x, pos_y;
    ::Window parent_xwin = 0;
    if (parent && parent->impl) {
        parent_xwin = parent->impl->xwindow;
    }

    if (parent_xwin) {
        XWindowAttributes parent_attrs;
        XGetWindowAttributes(display, parent_xwin, &parent_attrs);
        int parent_x, parent_y;
        ::Window child;
        XTranslateCoordinates(display, parent_xwin, RootWindow(display, screen),
                              0, 0, &parent_x, &parent_y, &child);
        pos_x = parent_x + (parent_attrs.width - s.dialog_width) / 2;
        pos_y = parent_y + (parent_attrs.height - s.dialog_height) / 2;
    } else {
        pos_x = (DisplayWidth(display, screen) - s.dialog_width) / 2;
        pos_y = (DisplayHeight(display, screen) - s.dialog_height) / 2;
    }

    // Create dialog window
    XSetWindowAttributes attrs = {};
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                       ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    attrs.background_pixel = WhitePixel(display, screen);

    s.dialog = XCreateWindow(
        display, RootWindow(display, screen),
        pos_x, pos_y, s.dialog_width, s.dialog_height,
        0, DefaultDepth(display, screen),
        InputOutput, DefaultVisual(display, screen),
        CWBackPixel | CWEventMask, &attrs);

    // Set title
    XStoreName(display, s.dialog, title ? title : "");
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    if (title) {
        XChangeProperty(display, s.dialog, net_wm_name, utf8_string, 8,
                        PropModeReplace, (unsigned char*)title, (int)strlen(title));
    }

    // Set dialog window type
    Atom net_wm_window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dialog_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, s.dialog, net_wm_window_type, XA_ATOM, 32,
                    PropModeReplace, (unsigned char*)&dialog_type, 1);

    // Transient for parent
    if (parent_xwin) {
        XSetTransientForHint(display, s.dialog, parent_xwin);
    }

    // Fixed size
    XSizeHints* size_hints = XAllocSizeHints();
    size_hints->flags = PMinSize | PMaxSize | PPosition;
    size_hints->min_width = size_hints->max_width = s.dialog_width;
    size_hints->min_height = size_hints->max_height = s.dialog_height;
    size_hints->x = pos_x;
    size_hints->y = pos_y;
    XSetWMNormalHints(display, s.dialog, size_hints);
    XFree(size_hints);

    // Handle WM_DELETE_WINDOW
    s.wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    s.wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, s.dialog, &s.wm_delete_window, 1);

    // Create GC
    s.gc = XCreateGC(display, s.dialog, 0, nullptr);
    XSetFont(display, s.gc, s.font->fid);

    // Show
    XMapRaised(display, s.dialog);
    XFlush(display);

    // Event loop
    while (!s.done) {
        XEvent event;
        XNextEvent(display, &event);

        if (event.xany.window != s.dialog) continue;

        switch (event.type) {
            case Expose:
                if (event.xexpose.count == 0) {
                    x11_msgbox_draw(s);
                }
                break;

            case ButtonPress: {
                int mx = event.xbutton.x;
                int my = event.xbutton.y;
                for (auto& btn : s.buttons) {
                    if (mx >= btn.x && mx < btn.x + btn.width &&
                        my >= btn.y && my < btn.y + btn.height) {
                        btn.pressed = true;
                        x11_msgbox_draw(s);
                        XFlush(display);
                    }
                }
                break;
            }

            case ButtonRelease: {
                int mx = event.xbutton.x;
                int my = event.xbutton.y;
                for (auto& btn : s.buttons) {
                    if (btn.pressed &&
                        mx >= btn.x && mx < btn.x + btn.width &&
                        my >= btn.y && my < btn.y + btn.height) {
                        s.result = btn.result;
                        s.done = true;
                    }
                    btn.pressed = false;
                }
                if (!s.done) {
                    x11_msgbox_draw(s);
                    XFlush(display);
                }
                break;
            }

            case MotionNotify: {
                int mx = event.xmotion.x;
                int my = event.xmotion.y;
                bool needs_redraw = false;
                for (auto& btn : s.buttons) {
                    bool in_btn = (mx >= btn.x && mx < btn.x + btn.width &&
                                   my >= btn.y && my < btn.y + btn.height);
                    if (in_btn != btn.hovered) {
                        btn.hovered = in_btn;
                        needs_redraw = true;
                    }
                }
                if (needs_redraw) {
                    x11_msgbox_draw(s);
                    XFlush(display);
                }
                break;
            }

            case KeyPress: {
                KeySym keysym = XLookupKeysym(&event.xkey, 0);
                if (keysym == XK_Return || keysym == XK_KP_Enter) {
                    if (s.default_button >= 0 && s.default_button < (int)s.buttons.size()) {
                        s.result = s.buttons[s.default_button].result;
                        s.done = true;
                    }
                } else if (keysym == XK_Escape) {
                    if (s.cancel_button >= 0 && s.cancel_button < (int)s.buttons.size()) {
                        s.result = s.buttons[s.cancel_button].result;
                        s.done = true;
                    }
                } else if (keysym == XK_Tab) {
                    s.default_button = (s.default_button + 1) % (int)s.buttons.size();
                    x11_msgbox_draw(s);
                    XFlush(display);
                }
                break;
            }

            case ClientMessage:
                if ((Atom)event.xclient.data.l[0] == s.wm_delete_window) {
                    if (s.cancel_button >= 0 && s.cancel_button < (int)s.buttons.size()) {
                        s.result = s.buttons[s.cancel_button].result;
                    } else {
                        s.result = MessageBoxButton::None;
                    }
                    s.done = true;
                }
                break;

            default:
                break;
        }
    }

    // Cleanup
    XFreeGC(display, s.gc);
    XFreeFont(display, s.font);
    XDestroyWindow(display, s.dialog);
    XCloseDisplay(display);

    return s.result;
}

void Window::show_message_box_async(
    const char* title,
    const char* message,
    MessageBoxType type,
    MessageBoxIcon icon,
    Window* parent,
    MessageBoxCallback callback)
{
    if (!callback) return;

    std::string title_copy = title ? title : "";
    std::string message_copy = message ? message : "";

    std::thread([title_copy, message_copy, type, icon, parent, callback]() {
        MessageBoxButton result = Window::show_message_box(
            title_copy.c_str(), message_copy.c_str(), type, icon, parent);
        callback(result);
    }).detach();
}

//=============================================================================
// Common Dialogs (X11 has no native dialogs, so we draw our own windows)
//=============================================================================
// X11 ships no standard file/color/font chooser. Following the same approach
// as the message box above, each dialog is a fixed-size top-level window we
// draw ourselves with Xlib, double-buffered to avoid flicker, running its own
// modal event loop until the user confirms or cancels. No external toolkit
// (GTK/Qt) or desktop-portal dependency is required.

namespace {

//-------------------------------------------------------------------------
// Small reusable chrome shared by the file/color/font dialogs.
//-------------------------------------------------------------------------

struct X11Button {
    std::string label;
    int x = 0, y = 0, w = 0, h = 0;
    bool hovered = false;
    bool primary = false;   // drawn highlighted (default action)
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct X11Dialog {
    Display* display = nullptr;
    int screen = 0;
    ::Window window = 0;
    GC gc = nullptr;
    XFontStruct* font = nullptr;
    Pixmap back = 0;        // double-buffer
    Atom wm_delete = 0;
    Atom wm_protocols = 0;
    int width = 0, height = 0;
    bool done = false;

    int line_h() const { return font->ascent + font->descent; }
    int text_w(const std::string& s) const { return XTextWidth(font, s.c_str(), (int)s.size()); }

    bool create(const char* title, int w, int h, Window* parent);
    void destroy();

    // Drawing into the back-buffer.
    void fill(int x, int y, int w, int h, unsigned long color) {
        XSetForeground(display, gc, color);
        XFillRectangle(display, back, gc, x, y, w, h);
    }
    void frame(int x, int y, int w, int h, unsigned long color) {
        XSetForeground(display, gc, color);
        XDrawRectangle(display, back, gc, x, y, w - 1, h - 1);
    }
    void text(int x, int baseline, const std::string& s, unsigned long color) {
        XSetForeground(display, gc, color);
        XDrawString(display, back, gc, x, baseline, s.c_str(), (int)s.size());
    }
    void text_clipped(int x, int baseline, const std::string& s, unsigned long color,
                      int cx, int cy, int cw, int ch) {
        XRectangle clip{(short)cx, (short)cy, (unsigned short)cw, (unsigned short)ch};
        XSetClipRectangles(display, gc, 0, 0, &clip, 1, Unsorted);
        text(x, baseline, s, color);
        XSetClipMask(display, gc, 0);   // None (the X11 macro is #undef'd in this file)
    }
    void button(const X11Button& b) {
        unsigned long bg = b.hovered ? (b.primary ? 0x5599DD : 0xD8D8D8)
                                     : (b.primary ? 0x4488CC : 0xE0E0E0);
        fill(b.x, b.y, b.w, b.h, bg);
        frame(b.x, b.y, b.w, b.h, 0x888888);
        unsigned long fg = b.primary ? 0xFFFFFF : 0x000000;
        int tw = text_w(b.label);
        text(b.x + (b.w - tw) / 2, b.y + (b.h + font->ascent - font->descent) / 2, b.label, fg);
    }
    void present() {
        XCopyArea(display, back, window, gc, 0, 0, width, height, 0, 0);
        XFlush(display);
    }
};

static XFontStruct* load_ui_font(Display* display) {
    XFontStruct* f = XLoadQueryFont(display, "-*-helvetica-medium-r-*-*-14-*-*-*-*-*-*-*");
    if (!f) f = XLoadQueryFont(display, "-*-fixed-medium-r-*-*-14-*-*-*-*-*-*-*");
    if (!f) f = XLoadQueryFont(display, "fixed");
    return f;
}

bool X11Dialog::create(const char* title, int w, int h, Window* parent) {
    ensure_x_threads();
    display = XOpenDisplay(nullptr);
    if (!display) return false;
    screen = DefaultScreen(display);
    width = w; height = h;

    font = load_ui_font(display);
    if (!font) { XCloseDisplay(display); display = nullptr; return false; }

    // Center on the parent window if given, else on the screen.
    int pos_x, pos_y;
    ::Window parent_xwin = 0;
    Window::Impl* pim = internal_get_impl(parent);
    if (pim) parent_xwin = pim->xwindow;
    if (parent_xwin) {
        XWindowAttributes pa;
        XGetWindowAttributes(display, parent_xwin, &pa);
        int ax, ay; ::Window child;
        XTranslateCoordinates(display, parent_xwin, RootWindow(display, screen), 0, 0, &ax, &ay, &child);
        pos_x = ax + (pa.width - w) / 2;
        pos_y = ay + (pa.height - h) / 2;
    } else {
        pos_x = (DisplayWidth(display, screen) - w) / 2;
        pos_y = (DisplayHeight(display, screen) - h) / 2;
    }
    if (pos_x < 0) pos_x = 0;
    if (pos_y < 0) pos_y = 0;

    XSetWindowAttributes attrs = {};
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                       ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    attrs.background_pixel = 0xF0F0F0;
    window = XCreateWindow(display, RootWindow(display, screen),
                           pos_x, pos_y, w, h, 0, DefaultDepth(display, screen),
                           InputOutput, DefaultVisual(display, screen),
                           CWBackPixel | CWEventMask, &attrs);

    XStoreName(display, window, title ? title : "");
    Atom net_name = XInternAtom(display, "_NET_WM_NAME", False);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    if (title)
        XChangeProperty(display, window, net_name, utf8, 8, PropModeReplace,
                        (unsigned char*)title, (int)strlen(title));

    Atom wtype = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom dlg = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    XChangeProperty(display, window, wtype, XA_ATOM, 32, PropModeReplace, (unsigned char*)&dlg, 1);
    if (parent_xwin) XSetTransientForHint(display, window, parent_xwin);

    XSizeHints* sh = XAllocSizeHints();
    sh->flags = PMinSize | PMaxSize | PPosition;
    sh->min_width = sh->max_width = w;
    sh->min_height = sh->max_height = h;
    sh->x = pos_x; sh->y = pos_y;
    XSetWMNormalHints(display, window, sh);
    XFree(sh);

    wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    gc = XCreateGC(display, window, 0, nullptr);
    XSetFont(display, gc, font->fid);
    back = XCreatePixmap(display, window, w, h, DefaultDepth(display, screen));

    XMapRaised(display, window);
    XFlush(display);
    return true;
}

void X11Dialog::destroy() {
    if (!display) return;
    if (back) XFreePixmap(display, back);
    if (gc) XFreeGC(display, gc);
    if (font) XFreeFont(display, font);
    if (window) XDestroyWindow(display, window);
    XCloseDisplay(display);
    display = nullptr;
}

// True when this ClientMessage is the window-manager close request.
static bool is_close_event(const X11Dialog& d, const XEvent& e) {
    return e.type == ClientMessage &&
           (Atom)e.xclient.message_type == d.wm_protocols &&
           (Atom)e.xclient.data.l[0] == d.wm_delete;
}

//-------------------------------------------------------------------------
// Filesystem helpers
//-------------------------------------------------------------------------

static std::string to_lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::vector<std::string> parse_extensions(const std::string& spec) {
    std::vector<std::string> exts;
    std::string s = spec.empty() ? std::string("*") : spec;
    size_t start = 0;
    while (start <= s.size()) {
        size_t sep = s.find(';', start);
        std::string e = s.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        while (!e.empty() && (e.front() == ' ' || e.front() == '\t')) e.erase(e.begin());
        while (!e.empty() && (e.back() == ' ' || e.back() == '\t')) e.pop_back();
        if (e == "*" || e == "*.*") { exts.clear(); return exts; }  // match everything
        if (!e.empty()) {
            if (e.rfind("*.", 0) == 0) e = e.substr(2);
            else if (e.front() == '.') e = e.substr(1);
            exts.push_back(to_lower(e));
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return exts;
}

static bool ext_matches(const std::string& name, const std::vector<std::string>& exts) {
    if (exts.empty()) return true;
    size_t dot = name.rfind('.');
    std::string e = (dot == std::string::npos) ? std::string() : to_lower(name.substr(dot + 1));
    for (const auto& x : exts) if (e == x) return true;
    return false;
}

static std::string path_home() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return "/";
}

static std::string path_parent(const std::string& base) {
    if (base == "/" || base.empty()) return "/";
    std::string b = base;
    if (b.size() > 1 && b.back() == '/') b.pop_back();
    size_t slash = b.rfind('/');
    if (slash == std::string::npos || slash == 0) return "/";
    return b.substr(0, slash);
}

static std::string path_join(const std::string& base, const std::string& name) {
    if (name == "..") return path_parent(base);
    if (base == "/") return "/" + name;
    return base + "/" + name;
}

struct X11FileEntry { std::string name; bool is_dir; };

static void list_dir(const std::string& path, const std::vector<std::string>& exts,
                     bool folder_mode, std::vector<X11FileEntry>& out) {
    out.clear();
    if (DIR* d = opendir(path.c_str())) {
        while (struct dirent* de = readdir(d)) {
            std::string nm = de->d_name;
            if (nm == "." || nm == "..") continue;
            if (!nm.empty() && nm[0] == '.') continue;  // hide dotfiles
            struct stat st;
            bool is_dir = false;
            if (stat(path_join(path, nm).c_str(), &st) == 0) is_dir = S_ISDIR(st.st_mode);
            if (is_dir) out.push_back({nm, true});
            else if (!folder_mode && ext_matches(nm, exts)) out.push_back({nm, false});
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end(), [](const X11FileEntry& a, const X11FileEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;            // directories first
        return to_lower(a.name) < to_lower(b.name);
    });
    if (path != "/") out.insert(out.begin(), {"..", true});
}

//-------------------------------------------------------------------------
// File / folder dialog
//-------------------------------------------------------------------------

struct X11FileDialog {
    X11Dialog dlg;
    bool save_mode = false;
    bool folder_mode = false;
    std::vector<std::string> exts;

    std::string cwd;
    std::vector<X11FileEntry> entries;
    int selected = -1;
    int scroll = 0;
    std::string name_field;

    // layout (computed once; window is fixed size)
    int pad = 12;
    int list_x = 0, list_y = 0, list_w = 0, list_h = 0, row_h = 0, visible_rows = 0;
    int field_y = 0;
    X11Button ok, cancel, up;

    FileDialogResult result;

    void layout() {
        row_h = dlg.line_h() + 8;
        list_x = pad;
        list_y = pad + dlg.line_h() + 10;                 // below the path label
        list_w = dlg.width - pad * 2;
        int bottom_area = pad + 32 + 8 + 32 + pad;        // field row + button row
        list_h = dlg.height - list_y - bottom_area;
        if (list_h < row_h) list_h = row_h;
        visible_rows = list_h / row_h;
        field_y = list_y + list_h + 8;

        int by = field_y + 32 + 8;
        cancel = {"Cancel", dlg.width - pad - 90, by, 90, 30, false, false};
        ok     = {save_mode ? "Save" : (folder_mode ? "Choose" : "Open"),
                  dlg.width - pad - 90 - 10 - 90, by, 90, 30, false, true};
        up     = {"Up", pad, by, 60, 30, false, false};
    }

    void refresh() {
        list_dir(cwd, exts, folder_mode, entries);
        selected = -1;
        scroll = 0;
    }

    void clamp_scroll() {
        int max_scroll = (int)entries.size() - visible_rows;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll > max_scroll) scroll = max_scroll;
        if (scroll < 0) scroll = 0;
    }

    void redraw() {
        dlg.fill(0, 0, dlg.width, dlg.height, 0xF0F0F0);

        // Current path (clipped to width).
        std::string path_label = cwd;
        dlg.text_clipped(pad, pad + dlg.font->ascent, path_label, 0x000000,
                         pad, pad, dlg.width - pad * 2, dlg.line_h());

        // List box.
        dlg.fill(list_x, list_y, list_w, list_h, 0xFFFFFF);
        dlg.frame(list_x, list_y, list_w, list_h, 0x888888);
        for (int i = 0; i < visible_rows; ++i) {
            int idx = scroll + i;
            if (idx >= (int)entries.size()) break;
            int ry = list_y + i * row_h;
            if (idx == selected) dlg.fill(list_x + 1, ry + 1, list_w - 2, row_h, 0x4488CC);
            unsigned long fg = (idx == selected) ? 0xFFFFFF : 0x000000;
            const X11FileEntry& e = entries[idx];
            std::string label = (e.is_dir ? "[" + e.name + "]" : e.name);
            dlg.text_clipped(list_x + 8, ry + dlg.font->ascent + 4, label, fg,
                             list_x + 1, ry + 1, list_w - 2, row_h);
        }

        // Filename field (hidden for folder picker).
        if (!folder_mode) {
            dlg.fill(pad, field_y, dlg.width - pad * 2, 30, 0xFFFFFF);
            dlg.frame(pad, field_y, dlg.width - pad * 2, 30, 0x888888);
            std::string shown = name_field + "_";
            dlg.text_clipped(pad + 6, field_y + dlg.font->ascent + 7, shown, 0x000000,
                             pad + 2, field_y, dlg.width - pad * 2 - 4, 30);
        }

        dlg.button(up);
        dlg.button(ok);
        dlg.button(cancel);
        dlg.present();
    }

    void enter_entry(int idx) {
        if (idx < 0 || idx >= (int)entries.size()) return;
        const X11FileEntry& e = entries[idx];
        if (e.is_dir) {
            cwd = path_join(cwd, e.name);
            refresh();
        } else {
            name_field = e.name;
        }
    }

    void confirm() {
        if (folder_mode) {
            std::string chosen = cwd;
            if (selected >= 0 && selected < (int)entries.size() && entries[selected].is_dir &&
                entries[selected].name != "..")
                chosen = path_join(cwd, entries[selected].name);
            result.paths.push_back(chosen);
            result.ok = true;
            dlg.done = true;
            return;
        }
        if (name_field.empty()) return;            // nothing to confirm yet
        result.paths.push_back(path_join(cwd, name_field));
        result.ok = true;
        dlg.done = true;
    }

    // Handles wheel scrolling and the buttons outside the list area. Clicks
    // inside the list (incl. double-click navigation) are handled in run().
    void on_button_press(int mx, int my, unsigned int button) {
        if (button == Button4) { scroll -= 1; clamp_scroll(); redraw(); return; }  // wheel up
        if (button == Button5) { scroll += 1; clamp_scroll(); redraw(); return; }  // wheel down
        if (button != Button1) return;

        if (cancel.contains(mx, my)) { dlg.done = true; return; }
        if (ok.contains(mx, my))     { confirm(); return; }
        if (up.contains(mx, my))     { cwd = path_parent(cwd); refresh(); redraw(); return; }
    }

    void on_key(KeySym ks, const char* buf, int n) {
        if (ks == XK_Escape) { dlg.done = true; return; }
        if (ks == XK_Return || ks == XK_KP_Enter) {
            if (selected >= 0 && selected < (int)entries.size() && entries[selected].is_dir)
                enter_entry(selected);
            else
                confirm();
            redraw();
            return;
        }
        if (ks == XK_Up)   { if (selected > 0) { selected--; if (selected < scroll) scroll = selected; } redraw(); return; }
        if (ks == XK_Down) { if (selected + 1 < (int)entries.size()) { selected++; if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1; } redraw(); return; }
        if (folder_mode) return;  // no text field
        if (ks == XK_BackSpace) { if (!name_field.empty()) name_field.pop_back(); redraw(); return; }
        if (n > 0 && (unsigned char)buf[0] >= 0x20) { name_field.append(buf, n); redraw(); }
    }

    FileDialogResult run(const FileDialogOptions& options, bool save, bool folder, Window* parent) {
        save_mode = save; folder_mode = folder;
        if (!folder && !options.filters.empty()) {
            int fi = options.default_filter;
            if (fi < 0 || fi >= (int)options.filters.size()) fi = 0;
            exts = parse_extensions(options.filters[fi].spec);
        }
        cwd = options.initial_dir;
        struct stat st;
        if (cwd.empty() || stat(cwd.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) cwd = path_home();
        if (save) name_field = options.initial_name;

        const char* title = !options.title.empty() ? options.title.c_str()
                          : (folder ? "Select Folder" : (save ? "Save File" : "Open File"));
        if (!dlg.create(title, 580, 440, parent)) return result;
        layout();
        refresh();
        redraw();

        Time last_click = 0; int last_click_idx = -1;
        while (!dlg.done) {
            XEvent ev;
            XNextEvent(dlg.display, &ev);
            if (ev.xany.window != dlg.window && ev.type != ClientMessage) continue;
            switch (ev.type) {
                case Expose:
                    if (ev.xexpose.count == 0) dlg.present();
                    break;
                case ButtonPress: {
                    // Double-click detection on the list.
                    int mx = ev.xbutton.x, my = ev.xbutton.y;
                    if (ev.xbutton.button == Button1 &&
                        mx >= list_x && mx < list_x + list_w && my >= list_y && my < list_y + list_h) {
                        int idx = scroll + (my - list_y) / row_h;
                        bool dbl = (idx == last_click_idx) && (ev.xbutton.time - last_click < 400);
                        last_click = ev.xbutton.time; last_click_idx = idx;
                        if (idx >= 0 && idx < (int)entries.size()) {
                            selected = idx;
                            if (!entries[idx].is_dir) name_field = entries[idx].name;
                            if (dbl) { enter_entry(idx); last_click_idx = -1; }
                            redraw();
                        }
                        break;
                    }
                    on_button_press(mx, my, ev.xbutton.button);
                    break;
                }
                case MotionNotify: {
                    int mx = ev.xmotion.x, my = ev.xmotion.y;
                    bool oh = ok.hovered, ch = cancel.hovered, uh = up.hovered;
                    ok.hovered = ok.contains(mx, my);
                    cancel.hovered = cancel.contains(mx, my);
                    up.hovered = up.contains(mx, my);
                    if (oh != ok.hovered || ch != cancel.hovered || uh != up.hovered) redraw();
                    break;
                }
                case KeyPress: {
                    char buf[16]; KeySym ks;
                    int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, nullptr);
                    on_key(ks, buf, n);
                    break;
                }
                case ClientMessage:
                    if (is_close_event(dlg, ev)) dlg.done = true;
                    break;
            }
        }
        dlg.destroy();
        return result;
    }
};

//-------------------------------------------------------------------------
// Color dialog (RGBA sliders + preview)
//-------------------------------------------------------------------------

struct X11ColorDialog {
    X11Dialog dlg;
    bool allow_alpha = false;
    int ch[4] = {0, 0, 0, 255};   // r, g, b, a
    int n_ch = 3;
    int dragging = -1;

    int pad = 16;
    int sw_x = 0, sw_y = 0, sw_w = 100, sw_h = 100;   // preview swatch
    int bar_x = 0, bar_w = 0, bar_h = 18, bar_gap = 0, bars_y = 0;
    X11Button ok, cancel;
    ColorDialogResult result;

    int bar_y(int i) const { return bars_y + i * bar_gap; }

    void layout() {
        sw_x = pad; sw_y = pad; sw_w = 96; sw_h = 96;
        bar_x = sw_x + sw_w + 24;
        bar_w = dlg.width - bar_x - pad - 48;     // leave room for value text
        bar_gap = 30;
        bars_y = pad + 6;
        int by = pad + sw_h + 30;
        if (by < bars_y + n_ch * bar_gap + 10) by = bars_y + n_ch * bar_gap + 10;
        cancel = {"Cancel", dlg.width - pad - 90, by, 90, 30, false, false};
        ok     = {"OK", dlg.width - pad - 90 - 10 - 90, by, 90, 30, false, true};
    }

    unsigned long preview_pixel() const {
        // Approximate the swatch over the dialog background to convey alpha.
        if (!allow_alpha) return ((unsigned long)ch[0] << 16) | (ch[1] << 8) | ch[2];
        float a = ch[3] / 255.0f;
        int r = (int)(ch[0] * a + 0xF0 * (1 - a));
        int g = (int)(ch[1] * a + 0xF0 * (1 - a));
        int b = (int)(ch[2] * a + 0xF0 * (1 - a));
        return ((unsigned long)r << 16) | (g << 8) | b;
    }

    void redraw() {
        dlg.fill(0, 0, dlg.width, dlg.height, 0xF0F0F0);

        // Preview swatch.
        dlg.fill(sw_x, sw_y, sw_w, sw_h, preview_pixel());
        dlg.frame(sw_x, sw_y, sw_w, sw_h, 0x888888);
        char hex[16];
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", ch[0], ch[1], ch[2]);
        dlg.text(sw_x, sw_y + sw_h + dlg.line_h() + 4, hex, 0x000000);

        const char* names[4] = {"R", "G", "B", "A"};
        for (int i = 0; i < n_ch; ++i) {
            int y = bar_y(i);
            dlg.text(bar_x - 16, y + dlg.font->ascent + (bar_h - dlg.line_h()) / 2, names[i], 0x000000);
            // Gradient bar: step the channel across the width.
            for (int px = 0; px < bar_w; ++px) {
                int rgb[3] = {ch[0], ch[1], ch[2]};
                int v = px * 255 / (bar_w > 1 ? bar_w - 1 : 1);
                unsigned long color;
                if (i < 3) { rgb[i] = v; color = ((unsigned long)rgb[0] << 16) | (rgb[1] << 8) | rgb[2]; }
                else       { color = ((unsigned long)v << 16) | (v << 8) | v; }   // alpha: grayscale
                XSetForeground(dlg.display, dlg.gc, color);
                XDrawLine(dlg.display, dlg.back, dlg.gc, bar_x + px, y, bar_x + px, y + bar_h);
            }
            dlg.frame(bar_x, y, bar_w, bar_h, 0x888888);
            // Knob.
            int kx = bar_x + ch[i] * (bar_w - 1) / 255;
            dlg.fill(kx - 2, y - 3, 5, bar_h + 6, 0x202020);
            char val[8]; snprintf(val, sizeof(val), "%d", ch[i]);
            dlg.text(bar_x + bar_w + 8, y + dlg.font->ascent + (bar_h - dlg.line_h()) / 2, val, 0x000000);
        }

        dlg.button(ok);
        dlg.button(cancel);
        dlg.present();
    }

    int bar_at(int mx, int my) const {
        for (int i = 0; i < n_ch; ++i) {
            int y = bar_y(i);
            if (mx >= bar_x - 4 && mx <= bar_x + bar_w + 4 && my >= y - 4 && my <= y + bar_h + 4)
                return i;
        }
        return -1;
    }

    void set_from_x(int i, int mx) {
        int v = (mx - bar_x) * 255 / (bar_w > 1 ? bar_w - 1 : 1);
        if (v < 0) v = 0; if (v > 255) v = 255;
        ch[i] = v;
    }

    ColorDialogResult run(const ColorDialogOptions& options, Window* parent) {
        allow_alpha = options.allow_alpha;
        n_ch = allow_alpha ? 4 : 3;
        ch[0] = options.initial.r; ch[1] = options.initial.g;
        ch[2] = options.initial.b; ch[3] = options.initial.a;

        const char* title = !options.title.empty() ? options.title.c_str() : "Select Color";
        if (!dlg.create(title, 420, 260, parent)) return result;
        layout();
        redraw();

        while (!dlg.done) {
            XEvent ev;
            XNextEvent(dlg.display, &ev);
            if (ev.xany.window != dlg.window && ev.type != ClientMessage) continue;
            switch (ev.type) {
                case Expose: if (ev.xexpose.count == 0) dlg.present(); break;
                case ButtonPress: {
                    int mx = ev.xbutton.x, my = ev.xbutton.y;
                    if (ev.xbutton.button != Button1) break;
                    if (cancel.contains(mx, my)) { dlg.done = true; break; }
                    if (ok.contains(mx, my)) {
                        result.color = {(uint8_t)ch[0], (uint8_t)ch[1], (uint8_t)ch[2],
                                        allow_alpha ? (uint8_t)ch[3] : options.initial.a};
                        result.ok = true; dlg.done = true; break;
                    }
                    dragging = bar_at(mx, my);
                    if (dragging >= 0) { set_from_x(dragging, mx); redraw(); }
                    break;
                }
                case ButtonRelease:
                    if (ev.xbutton.button == Button1) dragging = -1;
                    break;
                case MotionNotify: {
                    int mx = ev.xmotion.x, my = ev.xmotion.y;
                    if (dragging >= 0 && (ev.xmotion.state & Button1Mask)) { set_from_x(dragging, mx); redraw(); break; }
                    bool oh = ok.hovered, chh = cancel.hovered;
                    ok.hovered = ok.contains(mx, my);
                    cancel.hovered = cancel.contains(mx, my);
                    if (oh != ok.hovered || chh != cancel.hovered) redraw();
                    break;
                }
                case KeyPress: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    if (ks == XK_Escape) dlg.done = true;
                    else if (ks == XK_Return || ks == XK_KP_Enter) {
                        result.color = {(uint8_t)ch[0], (uint8_t)ch[1], (uint8_t)ch[2],
                                        allow_alpha ? (uint8_t)ch[3] : options.initial.a};
                        result.ok = true; dlg.done = true;
                    }
                    break;
                }
                case ClientMessage: if (is_close_event(dlg, ev)) dlg.done = true; break;
            }
        }
        dlg.destroy();
        return result;
    }
};

//-------------------------------------------------------------------------
// Font dialog (family list + size + bold/italic + preview)
//-------------------------------------------------------------------------

static std::vector<std::string> enumerate_font_families(Display* display) {
    std::vector<std::string> fams;
    int count = 0;
    char** names = XListFonts(display, "-*-*-medium-r-normal-*-*-*-*-*-*-*-iso8859-1", 4000, &count);
    if (names) {
        for (int i = 0; i < count; ++i) {
            // XLFD: -foundry-family-weight-slant-...; family is field index 2.
            const char* p = names[i];
            int field = 0; const char* fam_start = nullptr; const char* fam_end = nullptr;
            for (const char* c = p; *c; ++c) {
                if (*c == '-') {
                    field++;
                    if (field == 2) fam_start = c + 1;
                    else if (field == 3) { fam_end = c; break; }
                }
            }
            if (fam_start && fam_end && fam_end > fam_start)
                fams.emplace_back(fam_start, fam_end);
        }
        XFreeFontNames(names);
    }
    std::sort(fams.begin(), fams.end());
    fams.erase(std::unique(fams.begin(), fams.end()), fams.end());
    if (fams.empty()) fams.push_back("fixed");
    return fams;
}

struct X11FontDialog {
    X11Dialog dlg;
    std::vector<std::string> families;
    int selected = 0;
    int scroll = 0;
    int size_pt = 12;
    bool bold = false, italic = false;
    XFontStruct* preview = nullptr;

    int pad = 12;
    int list_x = 0, list_y = 0, list_w = 0, list_h = 0, row_h = 0, visible_rows = 0;
    X11Button ok, cancel, size_up, size_down, bold_btn, italic_btn;
    int preview_y = 0;
    FontDialogResult result;

    void layout() {
        row_h = dlg.line_h() + 6;
        list_x = pad; list_y = pad + dlg.line_h() + 6;
        list_w = 240;
        list_h = 200;
        visible_rows = list_h / row_h;

        int rx = list_x + list_w + 20;
        size_down = {"-", rx, list_y, 30, 28, false, false};
        size_up   = {"+", rx + 90, list_y, 30, 28, false, false};
        bold_btn   = {"Bold",   rx, list_y + 44, 80, 28, false, false};
        italic_btn = {"Italic", rx + 90, list_y + 44, 80, 28, false, false};

        preview_y = list_y + list_h + 16;
        int by = dlg.height - pad - 30;
        cancel = {"Cancel", dlg.width - pad - 90, by, 90, 30, false, false};
        ok     = {"OK", dlg.width - pad - 90 - 10 - 90, by, 90, 30, false, true};
    }

    void load_preview() {
        if (preview) { XFreeFont(dlg.display, preview); preview = nullptr; }
        if (selected < 0 || selected >= (int)families.size()) return;
        char xlfd[256];
        snprintf(xlfd, sizeof(xlfd), "-*-%s-%s-%s-normal-*-%d-*-*-*-*-*-iso8859-1",
                 families[selected].c_str(), bold ? "bold" : "medium",
                 italic ? "i" : "r", size_pt > 0 ? size_pt : 12);
        preview = XLoadQueryFont(dlg.display, xlfd);
        if (!preview) {
            // Try oblique slant, then any registry, before giving up.
            snprintf(xlfd, sizeof(xlfd), "-*-%s-%s-%s-normal-*-%d-*-*-*-*-*-*-*",
                     families[selected].c_str(), bold ? "bold" : "medium",
                     italic ? "o" : "r", size_pt > 0 ? size_pt : 12);
            preview = XLoadQueryFont(dlg.display, xlfd);
        }
    }

    void clamp_scroll() {
        int ms = (int)families.size() - visible_rows; if (ms < 0) ms = 0;
        if (scroll > ms) scroll = ms; if (scroll < 0) scroll = 0;
    }

    void redraw() {
        dlg.fill(0, 0, dlg.width, dlg.height, 0xF0F0F0);
        dlg.text(pad, pad + dlg.font->ascent, "Family", 0x000000);

        dlg.fill(list_x, list_y, list_w, list_h, 0xFFFFFF);
        dlg.frame(list_x, list_y, list_w, list_h, 0x888888);
        for (int i = 0; i < visible_rows; ++i) {
            int idx = scroll + i; if (idx >= (int)families.size()) break;
            int ry = list_y + i * row_h;
            if (idx == selected) dlg.fill(list_x + 1, ry + 1, list_w - 2, row_h, 0x4488CC);
            dlg.text_clipped(list_x + 6, ry + dlg.font->ascent + 3, families[idx],
                             idx == selected ? 0xFFFFFF : 0x000000,
                             list_x + 1, ry + 1, list_w - 2, row_h);
        }

        char sz[32]; snprintf(sz, sizeof(sz), "Size: %d", size_pt);
        dlg.text(size_down.x + 38, size_down.y + dlg.font->ascent + 6, sz, 0x000000);
        dlg.button(size_down); dlg.button(size_up);
        bold_btn.primary = bold; italic_btn.primary = italic;
        dlg.button(bold_btn); dlg.button(italic_btn);

        // Preview.
        dlg.fill(pad, preview_y, dlg.width - pad * 2, dlg.height - preview_y - 50, 0xFFFFFF);
        dlg.frame(pad, preview_y, dlg.width - pad * 2, dlg.height - preview_y - 50, 0x888888);
        const char* sample = "AaBbYyZz 0123";
        if (preview) {
            XSetFont(dlg.display, dlg.gc, preview->fid);
            XSetForeground(dlg.display, dlg.gc, 0x000000);
            XDrawString(dlg.display, dlg.back, dlg.gc, pad + 10,
                        preview_y + 10 + preview->ascent, sample, (int)strlen(sample));
            XSetFont(dlg.display, dlg.gc, dlg.font->fid);   // restore UI font
        } else {
            dlg.text(pad + 10, preview_y + 10 + dlg.font->ascent, sample, 0x000000);
        }

        dlg.button(ok);
        dlg.button(cancel);
        dlg.present();
    }

    FontDialogResult run(const FontDialogOptions& options, Window* parent) {
        size_pt = (int)(options.initial.size_pt + 0.5f); if (size_pt < 1) size_pt = 12;
        bold = options.initial.bold; italic = options.initial.italic;

        const char* title = !options.title.empty() ? options.title.c_str() : "Select Font";
        if (!dlg.create(title, 520, 420, parent)) return result;
        families = enumerate_font_families(dlg.display);
        if (!options.initial.family.empty()) {
            std::string want = to_lower(options.initial.family);
            for (int i = 0; i < (int)families.size(); ++i)
                if (to_lower(families[i]) == want) { selected = i; break; }
        }
        layout();   // sets visible_rows
        scroll = (selected >= visible_rows) ? selected - visible_rows + 1 : 0;
        clamp_scroll();
        load_preview();
        redraw();

        while (!dlg.done) {
            XEvent ev;
            XNextEvent(dlg.display, &ev);
            if (ev.xany.window != dlg.window && ev.type != ClientMessage) continue;
            switch (ev.type) {
                case Expose: if (ev.xexpose.count == 0) dlg.present(); break;
                case ButtonPress: {
                    int mx = ev.xbutton.x, my = ev.xbutton.y;
                    if (ev.xbutton.button == Button4) { scroll--; clamp_scroll(); redraw(); break; }
                    if (ev.xbutton.button == Button5) { scroll++; clamp_scroll(); redraw(); break; }
                    if (ev.xbutton.button != Button1) break;
                    if (cancel.contains(mx, my)) { dlg.done = true; break; }
                    if (ok.contains(mx, my)) {
                        result.font = options.initial;
                        if (selected >= 0 && selected < (int)families.size())
                            result.font.family = families[selected];
                        result.font.size_pt = (float)size_pt;
                        result.font.bold = bold; result.font.italic = italic;
                        result.ok = true; dlg.done = true; break;
                    }
                    if (size_down.contains(mx, my)) { if (size_pt > 1) size_pt--; load_preview(); redraw(); break; }
                    if (size_up.contains(mx, my))   { if (size_pt < 96) size_pt++; load_preview(); redraw(); break; }
                    if (bold_btn.contains(mx, my))  { bold = !bold; load_preview(); redraw(); break; }
                    if (italic_btn.contains(mx, my)){ italic = !italic; load_preview(); redraw(); break; }
                    if (mx >= list_x && mx < list_x + list_w && my >= list_y && my < list_y + list_h) {
                        int idx = scroll + (my - list_y) / row_h;
                        if (idx >= 0 && idx < (int)families.size()) { selected = idx; load_preview(); redraw(); }
                    }
                    break;
                }
                case MotionNotify: {
                    int mx = ev.xmotion.x, my = ev.xmotion.y;
                    bool changed = false;
                    auto upd = [&](X11Button& b){ bool h = b.contains(mx,my); if (h!=b.hovered){b.hovered=h; changed=true;} };
                    upd(ok); upd(cancel); upd(size_up); upd(size_down); upd(bold_btn); upd(italic_btn);
                    if (changed) redraw();
                    break;
                }
                case KeyPress: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    if (ks == XK_Escape) dlg.done = true;
                    else if (ks == XK_Up && selected > 0) { selected--; if (selected < scroll) scroll = selected; load_preview(); redraw(); }
                    else if (ks == XK_Down && selected + 1 < (int)families.size()) { selected++; if (selected >= scroll + visible_rows) scroll = selected - visible_rows + 1; load_preview(); redraw(); }
                    break;
                }
                case ClientMessage: if (is_close_event(dlg, ev)) dlg.done = true; break;
            }
        }
        if (preview) XFreeFont(dlg.display, preview);
        preview = nullptr;
        dlg.destroy();
        return result;
    }
};

} // anonymous namespace

FileDialogResult Window::show_open_file_dialog(const FileDialogOptions& options) {
    X11FileDialog d; return d.run(options, /*save*/false, /*folder*/false, options.parent);
}
FileDialogResult Window::show_save_file_dialog(const FileDialogOptions& options) {
    X11FileDialog d; return d.run(options, /*save*/true, /*folder*/false, options.parent);
}
FileDialogResult Window::show_folder_dialog(const FileDialogOptions& options) {
    X11FileDialog d; return d.run(options, /*save*/false, /*folder*/true, options.parent);
}
ColorDialogResult Window::show_color_dialog(const ColorDialogOptions& options) {
    X11ColorDialog d; return d.run(options, options.parent);
}
FontDialogResult Window::show_font_dialog(const FontDialogOptions& options) {
    X11FontDialog d; return d.run(options, options.parent);
}

} // namespace window

#endif // WINDOW_PLATFORM_X11
