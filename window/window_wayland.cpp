/*
 * window_wayland.cpp - Wayland (Linux) implementation
 * Backends: OpenGL (EGL), Vulkan
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_WAYLAND)

#include <wayland-client.h>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <string>
#include <linux/input.h>
#include <xkbcommon/xkbcommon.h>
#include <sys/mman.h>
#include <unistd.h>

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
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
    #define XDG_TOPLEVEL_SET_FULLSCREEN 10
    #define XDG_TOPLEVEL_UNSET_FULLSCREEN 11

    static inline void xdg_toplevel_set_title(struct xdg_toplevel *xdg_toplevel, const char *title) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_TITLE, title);
    }

    static inline void xdg_toplevel_set_min_size(struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_MIN_SIZE, width, height);
    }

    static inline void xdg_toplevel_set_max_size(struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_MAX_SIZE, width, height);
    }

    static inline void xdg_toplevel_set_fullscreen(struct xdg_toplevel *xdg_toplevel, struct wl_output *output) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_SET_FULLSCREEN, output);
    }

    static inline void xdg_toplevel_unset_fullscreen(struct xdg_toplevel *xdg_toplevel) {
        wl_proxy_marshal((struct wl_proxy *)xdg_toplevel, XDG_TOPLEVEL_UNSET_FULLSCREEN);
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
// Key Translation
//=============================================================================

static Key translate_linux_keycode(uint32_t keycode) {
    // Linux keycodes (from linux/input-event-codes.h)
    switch (keycode) {
        case KEY_A: return Key::A; case KEY_B: return Key::B; case KEY_C: return Key::C;
        case KEY_D: return Key::D; case KEY_E: return Key::E; case KEY_F: return Key::F;
        case KEY_G: return Key::G; case KEY_H: return Key::H; case KEY_I: return Key::I;
        case KEY_J: return Key::J; case KEY_K: return Key::K; case KEY_L: return Key::L;
        case KEY_M: return Key::M; case KEY_N: return Key::N; case KEY_O: return Key::O;
        case KEY_P: return Key::P; case KEY_Q: return Key::Q; case KEY_R: return Key::R;
        case KEY_S: return Key::S; case KEY_T: return Key::T; case KEY_U: return Key::U;
        case KEY_V: return Key::V; case KEY_W: return Key::W; case KEY_X: return Key::X;
        case KEY_Y: return Key::Y; case KEY_Z: return Key::Z;
        case KEY_0: return Key::Num0; case KEY_1: return Key::Num1; case KEY_2: return Key::Num2;
        case KEY_3: return Key::Num3; case KEY_4: return Key::Num4; case KEY_5: return Key::Num5;
        case KEY_6: return Key::Num6; case KEY_7: return Key::Num7; case KEY_8: return Key::Num8;
        case KEY_9: return Key::Num9;
        case KEY_F1: return Key::F1; case KEY_F2: return Key::F2; case KEY_F3: return Key::F3;
        case KEY_F4: return Key::F4; case KEY_F5: return Key::F5; case KEY_F6: return Key::F6;
        case KEY_F7: return Key::F7; case KEY_F8: return Key::F8; case KEY_F9: return Key::F9;
        case KEY_F10: return Key::F10; case KEY_F11: return Key::F11; case KEY_F12: return Key::F12;
        case KEY_ESC: return Key::Escape;
        case KEY_TAB: return Key::Tab;
        case KEY_CAPSLOCK: return Key::CapsLock;
        case KEY_SPACE: return Key::Space;
        case KEY_ENTER: return Key::Enter;
        case KEY_BACKSPACE: return Key::Backspace;
        case KEY_DELETE: return Key::Delete;
        case KEY_INSERT: return Key::Insert;
        case KEY_HOME: return Key::Home;
        case KEY_END: return Key::End;
        case KEY_PAGEUP: return Key::PageUp;
        case KEY_PAGEDOWN: return Key::PageDown;
        case KEY_LEFT: return Key::Left;
        case KEY_RIGHT: return Key::Right;
        case KEY_UP: return Key::Up;
        case KEY_DOWN: return Key::Down;
        case KEY_LEFTSHIFT: return Key::LeftShift;
        case KEY_RIGHTSHIFT: return Key::RightShift;
        case KEY_LEFTCTRL: return Key::LeftControl;
        case KEY_RIGHTCTRL: return Key::RightControl;
        case KEY_LEFTALT: return Key::LeftAlt;
        case KEY_RIGHTALT: return Key::RightAlt;
        case KEY_LEFTMETA: return Key::LeftSuper;
        case KEY_RIGHTMETA: return Key::RightSuper;
        case KEY_GRAVE: return Key::Grave;
        case KEY_MINUS: return Key::Minus;
        case KEY_EQUAL: return Key::Equal;
        case KEY_LEFTBRACE: return Key::LeftBracket;
        case KEY_RIGHTBRACE: return Key::RightBracket;
        case KEY_BACKSLASH: return Key::Backslash;
        case KEY_SEMICOLON: return Key::Semicolon;
        case KEY_APOSTROPHE: return Key::Apostrophe;
        case KEY_COMMA: return Key::Comma;
        case KEY_DOT: return Key::Period;
        case KEY_SLASH: return Key::Slash;
        case KEY_KP0: return Key::Numpad0; case KEY_KP1: return Key::Numpad1;
        case KEY_KP2: return Key::Numpad2; case KEY_KP3: return Key::Numpad3;
        case KEY_KP4: return Key::Numpad4; case KEY_KP5: return Key::Numpad5;
        case KEY_KP6: return Key::Numpad6; case KEY_KP7: return Key::Numpad7;
        case KEY_KP8: return Key::Numpad8; case KEY_KP9: return Key::Numpad9;
        case KEY_KPDOT: return Key::NumpadDecimal;
        case KEY_KPENTER: return Key::NumpadEnter;
        case KEY_KPPLUS: return Key::NumpadAdd;
        case KEY_KPMINUS: return Key::NumpadSubtract;
        case KEY_KPASTERISK: return Key::NumpadMultiply;
        case KEY_KPSLASH: return Key::NumpadDivide;
        case KEY_NUMLOCK: return Key::NumLock;
        case KEY_SYSRQ: return Key::PrintScreen;
        case KEY_SCROLLLOCK: return Key::ScrollLock;
        case KEY_PAUSE: return Key::Pause;
        case KEY_COMPOSE: return Key::Menu;
        default: return Key::Unknown;
    }
}

static MouseButton translate_wayland_button(uint32_t button) {
    switch (button) {
        case BTN_LEFT: return MouseButton::Left;
        case BTN_RIGHT: return MouseButton::Right;
        case BTN_MIDDLE: return MouseButton::Middle;
        case BTN_SIDE: return MouseButton::X1;
        case BTN_EXTRA: return MouseButton::X2;
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
    WindowCloseCallback close_callback = nullptr;
    void* close_user_data = nullptr;

    WindowResizeCallback resize_callback = nullptr;
    void* resize_user_data = nullptr;

    WindowMoveCallback move_callback = nullptr;
    void* move_user_data = nullptr;

    WindowFocusCallback focus_callback = nullptr;
    void* focus_user_data = nullptr;

    WindowStateCallback state_callback = nullptr;
    void* state_user_data = nullptr;

    TouchCallback touch_callback = nullptr;
    void* touch_user_data = nullptr;

    DpiChangeCallback dpi_change_callback = nullptr;
    void* dpi_change_user_data = nullptr;

    DropFileCallback drop_file_callback = nullptr;
    void* drop_file_user_data = nullptr;
};

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

    // Input devices
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_pointer* pointer = nullptr;

    // XKB keyboard state
    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* xkb_keymap = nullptr;
    xkb_state* xkb_state = nullptr;

    // Back-pointer for callbacks
    Window* owner = nullptr;

    bool should_close_flag = false;
    bool visible = false;
    bool configured = false;
    bool focused = false;
    int width = 0;
    int height = 0;
    int pending_width = 0;
    int pending_height = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    bool is_fullscreen = false;
    // For fullscreen toggle restoration
    int windowed_width = 0;
    int windowed_height = 0;

    // Event callbacks
    EventCallbacks callbacks;

    // Input state
    bool mouse_in_window = false;
    KeyMod current_mods = KeyMod::None;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;
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
    } else if (strcmp(interface, "wl_seat") == 0) {
        impl->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 5));
        wl_seat_add_listener(impl->seat, &seat_listener, impl);
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

//=============================================================================
// Keyboard Callbacks
//=============================================================================

static void keyboard_keymap(void* data, wl_keyboard* keyboard,
                            uint32_t format, int fd, uint32_t size) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_shm = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    if (!impl->xkb_ctx) {
        impl->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }

    if (impl->xkb_keymap) {
        xkb_keymap_unref(impl->xkb_keymap);
    }
    impl->xkb_keymap = xkb_keymap_new_from_string(impl->xkb_ctx, map_shm,
                                                   XKB_KEYMAP_FORMAT_TEXT_V1,
                                                   XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap(map_shm, size);
    close(fd);

    if (impl->xkb_state) {
        xkb_state_unref(impl->xkb_state);
    }
    if (impl->xkb_keymap) {
        impl->xkb_state = xkb_state_new(impl->xkb_keymap);
    }
}

static void keyboard_enter(void* data, wl_keyboard* keyboard,
                           uint32_t serial, wl_surface* surface, wl_array* keys) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    impl->focused = true;

    if (impl->callbacks.focus_callback) {
        WindowFocusEvent event;
        event.type = EventType::WindowFocus;
        event.window = impl->owner;
        event.timestamp = get_event_timestamp();
        event.focused = true;
        impl->callbacks.focus_callback(event, impl->callbacks.focus_user_data);
    }
}

static void keyboard_leave(void* data, wl_keyboard* keyboard,
                           uint32_t serial, wl_surface* surface) {
    (void)keyboard; (void)serial; (void)surface;
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    impl->focused = false;

    // Reset key states and mouse device
    memset(impl->key_states, 0, sizeof(impl->key_states));
    impl->mouse_device.reset();

    if (impl->callbacks.focus_callback) {
        WindowFocusEvent event;
        event.type = EventType::WindowBlur;
        event.window = impl->owner;
        event.timestamp = get_event_timestamp();
        event.focused = false;
        impl->callbacks.focus_callback(event, impl->callbacks.focus_user_data);
    }
}

static void keyboard_key(void* data, wl_keyboard* keyboard,
                         uint32_t serial, uint32_t time, uint32_t keycode,
                         uint32_t state) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    Key key = translate_linux_keycode(keycode);
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    if (key != Key::Unknown && static_cast<int>(key) < 512) {
        impl->key_states[static_cast<int>(key)] = pressed;
    }

    if (impl->callbacks.key_callback) {
        KeyEvent event;
        event.type = pressed ? EventType::KeyDown : EventType::KeyUp;
        event.window = impl->owner;
        event.timestamp = get_event_timestamp();
        event.key = key;
        event.modifiers = impl->current_mods;
        event.scancode = keycode;
        event.repeat = false;
        impl->callbacks.key_callback(event, impl->callbacks.key_user_data);
    }

    // Character input
    if (pressed && impl->xkb_state && impl->callbacks.char_callback) {
        uint32_t keysym = xkb_state_key_get_one_sym(impl->xkb_state, keycode + 8);
        char utf8[8];
        int len = xkb_state_key_get_utf8(impl->xkb_state, keycode + 8, utf8, sizeof(utf8));
        if (len > 0) {
            // Decode UTF-8 to get codepoint
            unsigned char* p = (unsigned char*)utf8;
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
                char_event.modifiers = impl->current_mods;
                impl->callbacks.char_callback(char_event, impl->callbacks.char_user_data);
            }
        }
    }
}

static void keyboard_modifiers(void* data, wl_keyboard* keyboard,
                               uint32_t serial, uint32_t mods_depressed,
                               uint32_t mods_latched, uint32_t mods_locked,
                               uint32_t group) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    if (impl->xkb_state) {
        xkb_state_update_mask(impl->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }

    // Update current modifiers
    KeyMod mods = KeyMod::None;
    if (mods_depressed & (1 << 0)) mods = mods | KeyMod::Shift;
    if (mods_depressed & (1 << 2)) mods = mods | KeyMod::Control;
    if (mods_depressed & (1 << 3)) mods = mods | KeyMod::Alt;
    if (mods_depressed & (1 << 6)) mods = mods | KeyMod::Super;
    if (mods_locked & (1 << 1)) mods = mods | KeyMod::CapsLock;
    if (mods_locked & (1 << 4)) mods = mods | KeyMod::NumLock;
    impl->current_mods = mods;
}

static void keyboard_repeat_info(void* data, wl_keyboard* keyboard,
                                 int32_t rate, int32_t delay) {
    // Could implement key repeat based on rate/delay
}

static const wl_keyboard_listener keyboard_listener = {
    keyboard_keymap,
    keyboard_enter,
    keyboard_leave,
    keyboard_key,
    keyboard_modifiers,
    keyboard_repeat_info
};

//=============================================================================
// Pointer Callbacks
//=============================================================================

static void pointer_enter(void* data, wl_pointer* pointer,
                          uint32_t serial, wl_surface* surface,
                          wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)serial; (void)surface;
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    impl->mouse_in_window = true;
    impl->mouse_device.inject_move(wl_fixed_to_int(x), wl_fixed_to_int(y), impl->current_mods, get_event_timestamp());
}

static void pointer_leave(void* data, wl_pointer* pointer,
                          uint32_t serial, wl_surface* surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    impl->mouse_in_window = false;
}

static void pointer_motion(void* data, wl_pointer* pointer,
                           uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)pointer; (void)time;
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    int new_x = wl_fixed_to_int(x);
    int new_y = wl_fixed_to_int(y);

    impl->mouse_device.inject_move(new_x, new_y, impl->current_mods, get_event_timestamp());
}

static void pointer_button(void* data, wl_pointer* pointer,
                           uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    Window::Impl* impl = static_cast<Window::Impl*>(data);
    MouseButton btn = translate_wayland_button(button);
    bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    int x, y;
    impl->mouse_device.get_position(&x, &y);

    if (pressed) {
        impl->mouse_device.inject_button_down(btn, x, y, 1, impl->current_mods, get_event_timestamp());
    } else {
        impl->mouse_device.inject_button_up(btn, x, y, impl->current_mods, get_event_timestamp());
    }
}

static void pointer_axis(void* data, wl_pointer* pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)pointer; (void)time;
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    float scroll_value = -wl_fixed_to_double(value) / 10.0f;  // Normalize scroll
    float dx = 0, dy = 0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        dy = scroll_value;
    } else {
        dx = scroll_value;
    }
    int x, y;
    impl->mouse_device.get_position(&x, &y);
    impl->mouse_device.inject_wheel(dx, dy, x, y, impl->current_mods, get_event_timestamp());
}

static void pointer_frame(void* data, wl_pointer* pointer) {}
static void pointer_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source) {}
static void pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis) {}
static void pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete) {}

static const wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    pointer_frame,
    pointer_axis_source,
    pointer_axis_stop,
    pointer_axis_discrete
};

//=============================================================================
// Seat Callbacks
//=============================================================================

static void seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    Window::Impl* impl = static_cast<Window::Impl*>(data);

    bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;

    if (has_keyboard && !impl->keyboard) {
        impl->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(impl->keyboard, &keyboard_listener, impl);
    } else if (!has_keyboard && impl->keyboard) {
        wl_keyboard_destroy(impl->keyboard);
        impl->keyboard = nullptr;
    }

    if (has_pointer && !impl->pointer) {
        impl->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(impl->pointer, &pointer_listener, impl);
    } else if (!has_pointer && impl->pointer) {
        wl_pointer_destroy(impl->pointer);
        impl->pointer = nullptr;
    }
}

static void seat_name(void* data, wl_seat* seat, const char* name) {}

static const wl_seat_listener seat_listener = {
    seat_capabilities,
    seat_name
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

    window->impl->owner = window;  // Set back-pointer for event dispatch

    // Initialize mouse input system
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
    window->impl->mouse_device.set_window(window);

    window->impl->registry = wl_display_get_registry(display);
    wl_registry_add_listener(window->impl->registry, &registry_listener, window->impl);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);  // Second roundtrip to get seat capabilities

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
        if (impl->xkb_state) xkb_state_unref(impl->xkb_state);
        if (impl->xkb_keymap) xkb_keymap_unref(impl->xkb_keymap);
        if (impl->xkb_ctx) xkb_context_unref(impl->xkb_ctx);
        if (impl->keyboard) wl_keyboard_destroy(impl->keyboard);
        if (impl->pointer) wl_pointer_destroy(impl->pointer);
        if (impl->seat) wl_seat_destroy(impl->seat);
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

void Window::set_style(WindowStyle style) {
    if (!impl || !impl->toplevel) return;

    impl->style = style;

    // Handle fullscreen
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    // Handle resizable
    if (!has_style(style, WindowStyle::Resizable)) {
        xdg_toplevel_set_min_size(impl->toplevel, impl->width, impl->height);
        xdg_toplevel_set_max_size(impl->toplevel, impl->width, impl->height);
    } else {
        xdg_toplevel_set_min_size(impl->toplevel, 0, 0);
        xdg_toplevel_set_max_size(impl->toplevel, 0, 0);
    }

    wl_surface_commit(impl->surface);
    wl_display_flush(impl->display);
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !impl->toplevel) return;
    if (impl->is_fullscreen == fullscreen) return;

    if (fullscreen) {
        // Save windowed state
        impl->windowed_width = impl->width;
        impl->windowed_height = impl->height;

        xdg_toplevel_set_fullscreen(impl->toplevel, nullptr);
        impl->is_fullscreen = true;
        impl->style = impl->style | WindowStyle::Fullscreen;
    } else {
        xdg_toplevel_unset_fullscreen(impl->toplevel);
        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }

    wl_surface_commit(impl->surface);
    wl_display_flush(impl->display);
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    // Wayland doesn't have a standard way to set always-on-top
    // This would require compositor-specific protocols
    if (impl) {
        if (always_on_top) {
            impl->style = impl->style | WindowStyle::AlwaysOnTop;
        } else {
            impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        }
    }
}

bool Window::is_always_on_top() const {
    return impl ? has_style(impl->style, WindowStyle::AlwaysOnTop) : false;
}

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
// Event Callback Setters
//=============================================================================

void Window::set_close_callback(WindowCloseCallback callback, void* user_data) {
    if (impl) { impl->callbacks.close_callback = callback; impl->callbacks.close_user_data = user_data; }
}

void Window::set_resize_callback(WindowResizeCallback callback, void* user_data) {
    if (impl) { impl->callbacks.resize_callback = callback; impl->callbacks.resize_user_data = user_data; }
}

void Window::set_move_callback(WindowMoveCallback callback, void* user_data) {
    if (impl) { impl->callbacks.move_callback = callback; impl->callbacks.move_user_data = user_data; }
}

void Window::set_focus_callback(WindowFocusCallback callback, void* user_data) {
    if (impl) { impl->callbacks.focus_callback = callback; impl->callbacks.focus_user_data = user_data; }
}

void Window::set_state_callback(WindowStateCallback callback, void* user_data) {
    if (impl) { impl->callbacks.state_callback = callback; impl->callbacks.state_user_data = user_data; }
}

void Window::set_touch_callback(TouchCallback callback, void* user_data) {
    if (impl) { impl->callbacks.touch_callback = callback; impl->callbacks.touch_user_data = user_data; }
}

void Window::set_dpi_change_callback(DpiChangeCallback callback, void* user_data) {
    if (impl) { impl->callbacks.dpi_change_callback = callback; impl->callbacks.dpi_change_user_data = user_data; }
}

void Window::set_drop_file_callback(DropFileCallback callback, void* user_data) {
    if (impl) { impl->callbacks.drop_file_callback = callback; impl->callbacks.drop_file_user_data = user_data; }
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
    return impl ? impl->current_mods : KeyMod::None;
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
    wl_display* wl_disp = static_cast<wl_display*>(config.native_display);
    wl_surface* wl_surf = static_cast<wl_surface*>(config.native_handle);

    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_wayland(wl_disp, wl_surf, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_wayland(wl_disp, wl_surf, config.width, config.height, internal_config);
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

#endif // WINDOW_PLATFORM_WAYLAND
