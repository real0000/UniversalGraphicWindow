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
#include <climits>
#include <string>
#include <vector>
#include <map>
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

    // wl_subcompositor interface (for subsurfaces)
    #define WL_SUBCOMPOSITOR_DESTROY 0
    #define WL_SUBCOMPOSITOR_GET_SUBSURFACE 1

    static inline struct wl_subsurface* wl_subcompositor_get_subsurface(struct wl_subcompositor *subcompositor,
                                                                         struct wl_surface *surface,
                                                                         struct wl_surface *parent) {
        struct wl_proxy *id;
        id = wl_proxy_marshal_constructor((struct wl_proxy *)subcompositor,
                                           WL_SUBCOMPOSITOR_GET_SUBSURFACE,
                                           &wl_subsurface_interface, NULL, surface, parent);
        return (struct wl_subsurface *)id;
    }

    // wl_subsurface interface
    #define WL_SUBSURFACE_DESTROY 0
    #define WL_SUBSURFACE_SET_POSITION 1
    #define WL_SUBSURFACE_PLACE_ABOVE 2
    #define WL_SUBSURFACE_PLACE_BELOW 3
    #define WL_SUBSURFACE_SET_SYNC 4
    #define WL_SUBSURFACE_SET_DESYNC 5

    static inline void wl_subsurface_set_position(struct wl_subsurface *subsurface, int32_t x, int32_t y) {
        wl_proxy_marshal((struct wl_proxy *)subsurface, WL_SUBSURFACE_SET_POSITION, x, y);
    }

    static inline void wl_subsurface_set_desync(struct wl_subsurface *subsurface) {
        wl_proxy_marshal((struct wl_proxy *)subsurface, WL_SUBSURFACE_SET_DESYNC);
    }

    static inline void wl_subsurface_destroy(struct wl_subsurface *subsurface) {
        wl_proxy_marshal((struct wl_proxy *)subsurface, WL_SUBSURFACE_DESTROY);
        wl_proxy_destroy((struct wl_proxy *)subsurface);
    }

    // wl_output listener
    struct wl_output_listener {
        void (*geometry)(void *data, struct wl_output *output, int32_t x, int32_t y,
                        int32_t physical_width, int32_t physical_height, int32_t subpixel,
                        const char *make, const char *model, int32_t transform);
        void (*mode)(void *data, struct wl_output *output, uint32_t flags,
                    int32_t width, int32_t height, int32_t refresh);
        void (*done)(void *data, struct wl_output *output);
        void (*scale)(void *data, struct wl_output *output, int32_t factor);
        void (*name)(void *data, struct wl_output *output, const char *name);
        void (*description)(void *data, struct wl_output *output, const char *description);
    };

    static inline int wl_output_add_listener(struct wl_output *output,
                                              const struct wl_output_listener *listener, void *data) {
        return wl_proxy_add_listener((struct wl_proxy *)output, (void (**)(void))listener, data);
    }
}

namespace window {

//=============================================================================
// Global Wayland Context (Root Surface Manager)
//=============================================================================

struct OutputInfo {
    wl_output* output = nullptr;
    uint32_t name = 0;  // registry name for binding
    int32_t x = 0, y = 0;
    int32_t width = 0, height = 0;
    int32_t physical_width = 0, physical_height = 0;
    int32_t refresh = 0;
    int32_t scale = 1;
    char output_name[64] = {};
    bool geometry_done = false;
    bool mode_done = false;
};

struct WaylandContext {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_subcompositor* subcompositor = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_pointer* pointer = nullptr;

    // Root surface spanning all monitors
    wl_surface* root_surface = nullptr;
    xdg_surface* root_xdg_surface = nullptr;
    xdg_toplevel* root_toplevel = nullptr;
    bool root_configured = false;

    // Total bounds of all monitors
    int32_t total_x = 0, total_y = 0;
    int32_t total_width = 0, total_height = 0;

    // Monitor tracking
    std::vector<OutputInfo> outputs;

    // XKB keyboard state (shared across all windows)
    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* xkb_keymap = nullptr;
    xkb_state* xkb_state = nullptr;
    KeyMod current_mods = KeyMod::None;

    // Reference count for cleanup
    int ref_count = 0;

    // Currently focused window (for input routing)
    Window* focused_window = nullptr;
    Window* pointer_window = nullptr;

    // All active windows (subsurfaces)
    std::map<wl_surface*, Window*> surface_to_window;

    // Shared graphics context (first window's graphics)
    Graphics* shared_graphics = nullptr;
};

// Global context instance
static WaylandContext* g_wayland_ctx = nullptr;

// Forward declarations
static void wayland_context_init();
static void wayland_context_ref();
static void wayland_context_unref();
static void wayland_context_calculate_bounds();
static void wayland_create_root_surface();

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
    // Subsurface for this window (child of root surface)
    wl_surface* surface = nullptr;
    wl_subsurface* subsurface = nullptr;

    // Position within root surface
    int x = 0;
    int y = 0;

    // Back-pointer for callbacks
    Window* owner = nullptr;

    bool should_close_flag = false;
    bool visible = false;
    bool focused = false;
    int width = 0;
    int height = 0;
    std::string title;
    std::string name;  // Window identifier
    Graphics* gfx = nullptr;
    bool owns_graphics = false;  // True if this window created the graphics context
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
    KeyMod current_mods = KeyMod::None;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;
};

//=============================================================================
// Wayland Context Callbacks (for root surface and global state)
//=============================================================================

// Forward declarations for seat listener
static const wl_seat_listener ctx_seat_listener;

static void output_geometry(void* data, wl_output* output, int32_t x, int32_t y,
                            int32_t physical_width, int32_t physical_height,
                            int32_t subpixel, const char* make, const char* model,
                            int32_t transform) {
    OutputInfo* info = static_cast<OutputInfo*>(data);
    info->x = x;
    info->y = y;
    info->physical_width = physical_width;
    info->physical_height = physical_height;
    info->geometry_done = true;
}

static void output_mode(void* data, wl_output* output, uint32_t flags,
                        int32_t width, int32_t height, int32_t refresh) {
    OutputInfo* info = static_cast<OutputInfo*>(data);
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        info->width = width;
        info->height = height;
        info->refresh = refresh;
        info->mode_done = true;
    }
}

static void output_done(void* data, wl_output* output) {
    // Recalculate total bounds when output info is complete
    if (g_wayland_ctx) {
        wayland_context_calculate_bounds();
    }
}

static void output_scale(void* data, wl_output* output, int32_t factor) {
    OutputInfo* info = static_cast<OutputInfo*>(data);
    info->scale = factor;
}

static void output_name(void* data, wl_output* output, const char* name) {
    OutputInfo* info = static_cast<OutputInfo*>(data);
    strncpy(info->output_name, name, sizeof(info->output_name) - 1);
}

static void output_description(void* data, wl_output* output, const char* description) {
    // Not used
}

static const wl_output_listener output_listener = {
    output_geometry,
    output_mode,
    output_done,
    output_scale,
    output_name,
    output_description
};

static void ctx_registry_handle_global(void* data, wl_registry* registry,
                                        uint32_t name, const char* interface,
                                        uint32_t version) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    if (strcmp(interface, "wl_compositor") == 0) {
        ctx->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        ctx->subcompositor = static_cast<wl_subcompositor*>(
            wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        ctx->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    } else if (strcmp(interface, "wl_seat") == 0) {
        ctx->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 5));
        wl_seat_add_listener(ctx->seat, &ctx_seat_listener, ctx);
    } else if (strcmp(interface, "wl_output") == 0) {
        wl_output* output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, 4));
        OutputInfo info;
        info.output = output;
        info.name = name;
        ctx->outputs.push_back(info);
        wl_output_add_listener(output, &output_listener, &ctx->outputs.back());
    }
}

static void ctx_registry_handle_global_remove(void* data, wl_registry* registry, uint32_t name) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    // Remove output if it was removed
    for (auto it = ctx->outputs.begin(); it != ctx->outputs.end(); ++it) {
        if (it->name == name) {
            wl_output_destroy(it->output);
            ctx->outputs.erase(it);
            wayland_context_calculate_bounds();
            break;
        }
    }
}

static const wl_registry_listener ctx_registry_listener = {
    ctx_registry_handle_global,
    ctx_registry_handle_global_remove
};

static void xdg_wm_base_ping_handler(void* data, xdg_wm_base* wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const xdg_wm_base_listener wm_base_listener = {
    xdg_wm_base_ping_handler
};

//=============================================================================
// Keyboard Callbacks (use global context, route to focused window)
//=============================================================================

static void ctx_keyboard_keymap(void* data, wl_keyboard* keyboard,
                                uint32_t format, int fd, uint32_t size) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_shm = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0));
    if (map_shm == MAP_FAILED) {
        close(fd);
        return;
    }

    if (!ctx->xkb_ctx) {
        ctx->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    }

    if (ctx->xkb_keymap) {
        xkb_keymap_unref(ctx->xkb_keymap);
    }
    ctx->xkb_keymap = xkb_keymap_new_from_string(ctx->xkb_ctx, map_shm,
                                                  XKB_KEYMAP_FORMAT_TEXT_V1,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap(map_shm, size);
    close(fd);

    if (ctx->xkb_state) {
        xkb_state_unref(ctx->xkb_state);
    }
    if (ctx->xkb_keymap) {
        ctx->xkb_state = xkb_state_new(ctx->xkb_keymap);
    }
}

static void ctx_keyboard_enter(void* data, wl_keyboard* keyboard,
                               uint32_t serial, wl_surface* surface, wl_array* keys) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    // Find the window that owns this surface
    auto it = ctx->surface_to_window.find(surface);
    if (it != ctx->surface_to_window.end()) {
        Window* win = it->second;
        ctx->focused_window = win;
        win->impl->focused = true;

        if (win->impl->callbacks.focus_callback) {
            WindowFocusEvent event;
            event.type = EventType::WindowFocus;
            event.window = win;
            event.timestamp = get_event_timestamp();
            event.focused = true;
            win->impl->callbacks.focus_callback(event);
        }
    }
}

static void ctx_keyboard_leave(void* data, wl_keyboard* keyboard,
                               uint32_t serial, wl_surface* surface) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    auto it = ctx->surface_to_window.find(surface);
    if (it != ctx->surface_to_window.end()) {
        Window* win = it->second;
        win->impl->focused = false;
        win->impl->keyboard_device.reset();
        win->impl->mouse_device.reset();

        if (win->impl->callbacks.focus_callback) {
            WindowFocusEvent event;
            event.type = EventType::WindowBlur;
            event.window = win;
            event.timestamp = get_event_timestamp();
            event.focused = false;
            win->impl->callbacks.focus_callback(event);
        }
    }
    ctx->focused_window = nullptr;
}

static void ctx_keyboard_key(void* data, wl_keyboard* keyboard,
                             uint32_t serial, uint32_t time, uint32_t keycode,
                             uint32_t state) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    if (!ctx->focused_window) return;

    Window* win = ctx->focused_window;
    Key key = translate_linux_keycode(keycode);
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    // Dispatch through keyboard device
    if (pressed) {
        win->impl->keyboard_device.inject_key_down(key, keycode, ctx->current_mods, get_event_timestamp());
    } else {
        win->impl->keyboard_device.inject_key_up(key, keycode, ctx->current_mods, get_event_timestamp());
    }

    // Character input
    if (pressed && ctx->xkb_state) {
        char utf8[8];
        int len = xkb_state_key_get_utf8(ctx->xkb_state, keycode + 8, utf8, sizeof(utf8));
        if (len > 0) {
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
                win->impl->keyboard_device.inject_char(codepoint, ctx->current_mods, get_event_timestamp());
            }
        }
    }
}

static void ctx_keyboard_modifiers(void* data, wl_keyboard* keyboard,
                                   uint32_t serial, uint32_t mods_depressed,
                                   uint32_t mods_latched, uint32_t mods_locked,
                                   uint32_t group) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    if (ctx->xkb_state) {
        xkb_state_update_mask(ctx->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }

    // Update current modifiers
    KeyMod mods = KeyMod::None;
    if (mods_depressed & (1 << 0)) mods = mods | KeyMod::Shift;
    if (mods_depressed & (1 << 2)) mods = mods | KeyMod::Control;
    if (mods_depressed & (1 << 3)) mods = mods | KeyMod::Alt;
    if (mods_depressed & (1 << 6)) mods = mods | KeyMod::Super;
    if (mods_locked & (1 << 1)) mods = mods | KeyMod::CapsLock;
    if (mods_locked & (1 << 4)) mods = mods | KeyMod::NumLock;
    ctx->current_mods = mods;
}

static void ctx_keyboard_repeat_info(void* data, wl_keyboard* keyboard,
                                     int32_t rate, int32_t delay) {
    // Could implement key repeat based on rate/delay
}

static const wl_keyboard_listener ctx_keyboard_listener = {
    ctx_keyboard_keymap,
    ctx_keyboard_enter,
    ctx_keyboard_leave,
    ctx_keyboard_key,
    ctx_keyboard_modifiers,
    ctx_keyboard_repeat_info
};

//=============================================================================
// Pointer Callbacks (use global context, route to window under pointer)
//=============================================================================

static void ctx_pointer_enter(void* data, wl_pointer* pointer,
                              uint32_t serial, wl_surface* surface,
                              wl_fixed_t x, wl_fixed_t y) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    // Find the window that owns this surface
    auto it = ctx->surface_to_window.find(surface);
    if (it != ctx->surface_to_window.end()) {
        Window* win = it->second;
        ctx->pointer_window = win;
        win->impl->mouse_in_window = true;
        // Coordinates are relative to the subsurface
        win->impl->mouse_device.inject_move(wl_fixed_to_int(x), wl_fixed_to_int(y),
                                             ctx->current_mods, get_event_timestamp());
    }
}

static void ctx_pointer_leave(void* data, wl_pointer* pointer,
                              uint32_t serial, wl_surface* surface) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    auto it = ctx->surface_to_window.find(surface);
    if (it != ctx->surface_to_window.end()) {
        Window* win = it->second;
        win->impl->mouse_in_window = false;
    }
    ctx->pointer_window = nullptr;
}

static void ctx_pointer_motion(void* data, wl_pointer* pointer,
                               uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    if (!ctx->pointer_window) return;

    Window* win = ctx->pointer_window;
    int new_x = wl_fixed_to_int(x);
    int new_y = wl_fixed_to_int(y);

    win->impl->mouse_device.inject_move(new_x, new_y, ctx->current_mods, get_event_timestamp());
}

static void ctx_pointer_button(void* data, wl_pointer* pointer,
                               uint32_t serial, uint32_t time, uint32_t button,
                               uint32_t state) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    if (!ctx->pointer_window) return;

    Window* win = ctx->pointer_window;
    MouseButton btn = translate_wayland_button(button);
    bool pressed = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    int x, y;
    win->impl->mouse_device.get_position(&x, &y);

    if (pressed) {
        win->impl->mouse_device.inject_button_down(btn, x, y, 1, ctx->current_mods, get_event_timestamp());
    } else {
        win->impl->mouse_device.inject_button_up(btn, x, y, ctx->current_mods, get_event_timestamp());
    }
}

static void ctx_pointer_axis(void* data, wl_pointer* pointer,
                             uint32_t time, uint32_t axis, wl_fixed_t value) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    if (!ctx->pointer_window) return;

    Window* win = ctx->pointer_window;
    float scroll_value = -wl_fixed_to_double(value) / 10.0f;
    float dx = 0, dy = 0;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        dy = scroll_value;
    } else {
        dx = scroll_value;
    }
    int x, y;
    win->impl->mouse_device.get_position(&x, &y);
    win->impl->mouse_device.inject_wheel(dx, dy, x, y, ctx->current_mods, get_event_timestamp());
}

static void ctx_pointer_frame(void* data, wl_pointer* pointer) {}
static void ctx_pointer_axis_source(void* data, wl_pointer* pointer, uint32_t axis_source) {}
static void ctx_pointer_axis_stop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis) {}
static void ctx_pointer_axis_discrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete) {}

static const wl_pointer_listener ctx_pointer_listener = {
    ctx_pointer_enter,
    ctx_pointer_leave,
    ctx_pointer_motion,
    ctx_pointer_button,
    ctx_pointer_axis,
    ctx_pointer_frame,
    ctx_pointer_axis_source,
    ctx_pointer_axis_stop,
    ctx_pointer_axis_discrete
};

//=============================================================================
// Seat Callbacks (for global context)
//=============================================================================

static void ctx_seat_capabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);

    bool has_keyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    bool has_pointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;

    if (has_keyboard && !ctx->keyboard) {
        ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->keyboard, &ctx_keyboard_listener, ctx);
    } else if (!has_keyboard && ctx->keyboard) {
        wl_keyboard_destroy(ctx->keyboard);
        ctx->keyboard = nullptr;
    }

    if (has_pointer && !ctx->pointer) {
        ctx->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(ctx->pointer, &ctx_pointer_listener, ctx);
    } else if (!has_pointer && ctx->pointer) {
        wl_pointer_destroy(ctx->pointer);
        ctx->pointer = nullptr;
    }
}

static void ctx_seat_name(void* data, wl_seat* seat, const char* name) {}

static const wl_seat_listener ctx_seat_listener = {
    ctx_seat_capabilities,
    ctx_seat_name
};

//=============================================================================
// Root Surface Callbacks (for the fullscreen overlay)
//=============================================================================

static void root_xdg_surface_configure_handler(void* data, xdg_surface* surface, uint32_t serial) {
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    xdg_surface_ack_configure(surface, serial);
    ctx->root_configured = true;
}

static const xdg_surface_listener root_xdg_surface_listener = {
    root_xdg_surface_configure_handler
};

static void root_xdg_toplevel_configure_handler(void* data, xdg_toplevel* toplevel,
                                                 int32_t width, int32_t height,
                                                 wl_array* states) {
    // Root surface spans all monitors, ignore configure suggestions
}

static void root_xdg_toplevel_close_handler(void* data, xdg_toplevel* toplevel) {
    // When root surface is closed, mark all windows for close
    WaylandContext* ctx = static_cast<WaylandContext*>(data);
    for (auto& pair : ctx->surface_to_window) {
        pair.second->impl->should_close_flag = true;
    }
}

static const xdg_toplevel_listener root_toplevel_listener = {
    root_xdg_toplevel_configure_handler,
    root_xdg_toplevel_close_handler
};

//=============================================================================
// Wayland Context Management
//=============================================================================

static void wayland_context_calculate_bounds() {
    if (!g_wayland_ctx) return;

    int32_t min_x = INT_MAX, min_y = INT_MAX;
    int32_t max_x = INT_MIN, max_y = INT_MIN;

    for (const auto& out : g_wayland_ctx->outputs) {
        if (!out.mode_done) continue;
        min_x = std::min(min_x, out.x);
        min_y = std::min(min_y, out.y);
        max_x = std::max(max_x, out.x + out.width);
        max_y = std::max(max_y, out.y + out.height);
    }

    if (min_x != INT_MAX) {
        g_wayland_ctx->total_x = min_x;
        g_wayland_ctx->total_y = min_y;
        g_wayland_ctx->total_width = max_x - min_x;
        g_wayland_ctx->total_height = max_y - min_y;
    }
}

static void wayland_create_root_surface() {
    if (!g_wayland_ctx || g_wayland_ctx->root_surface) return;

    WaylandContext* ctx = g_wayland_ctx;

    // Create root surface
    ctx->root_surface = wl_compositor_create_surface(ctx->compositor);
    if (!ctx->root_surface) return;

    // Create XDG surface for root
    ctx->root_xdg_surface = xdg_wm_base_get_xdg_surface(ctx->wm_base, ctx->root_surface);
    xdg_surface_add_listener(ctx->root_xdg_surface, &root_xdg_surface_listener, ctx);

    // Create toplevel for root (will be set fullscreen)
    ctx->root_toplevel = xdg_surface_get_toplevel(ctx->root_xdg_surface);
    xdg_toplevel_add_listener(ctx->root_toplevel, &root_toplevel_listener, ctx);
    xdg_toplevel_set_title(ctx->root_toplevel, "UniversalGraphicWindow Root");

    // Set fullscreen to cover all monitors
    xdg_toplevel_set_fullscreen(ctx->root_toplevel, nullptr);

    wl_surface_commit(ctx->root_surface);

    // Wait for configure
    while (!ctx->root_configured) {
        wl_display_dispatch(ctx->display);
    }
}

static void wayland_context_init() {
    if (g_wayland_ctx) return;

    wl_display* display = wl_display_connect(nullptr);
    if (!display) return;

    g_wayland_ctx = new WaylandContext();
    g_wayland_ctx->display = display;

    g_wayland_ctx->registry = wl_display_get_registry(display);
    wl_registry_add_listener(g_wayland_ctx->registry, &ctx_registry_listener, g_wayland_ctx);

    // First roundtrip to get globals
    wl_display_roundtrip(display);
    // Second roundtrip to get output info and seat capabilities
    wl_display_roundtrip(display);

    if (g_wayland_ctx->wm_base) {
        xdg_wm_base_add_listener(g_wayland_ctx->wm_base, &wm_base_listener, g_wayland_ctx);
    }

    // Calculate total bounds across all monitors
    wayland_context_calculate_bounds();

    // Create root surface
    if (g_wayland_ctx->compositor && g_wayland_ctx->wm_base && g_wayland_ctx->subcompositor) {
        wayland_create_root_surface();
    }
}

static void wayland_context_ref() {
    if (!g_wayland_ctx) {
        wayland_context_init();
    }
    if (g_wayland_ctx) {
        g_wayland_ctx->ref_count++;
    }
}

static void wayland_context_unref() {
    if (!g_wayland_ctx) return;

    g_wayland_ctx->ref_count--;
    if (g_wayland_ctx->ref_count <= 0) {
        // Cleanup
        if (g_wayland_ctx->root_toplevel) xdg_toplevel_destroy(g_wayland_ctx->root_toplevel);
        if (g_wayland_ctx->root_xdg_surface) xdg_surface_destroy(g_wayland_ctx->root_xdg_surface);
        if (g_wayland_ctx->root_surface) wl_surface_destroy(g_wayland_ctx->root_surface);

        if (g_wayland_ctx->xkb_state) xkb_state_unref(g_wayland_ctx->xkb_state);
        if (g_wayland_ctx->xkb_keymap) xkb_keymap_unref(g_wayland_ctx->xkb_keymap);
        if (g_wayland_ctx->xkb_ctx) xkb_context_unref(g_wayland_ctx->xkb_ctx);

        if (g_wayland_ctx->keyboard) wl_keyboard_destroy(g_wayland_ctx->keyboard);
        if (g_wayland_ctx->pointer) wl_pointer_destroy(g_wayland_ctx->pointer);
        if (g_wayland_ctx->seat) wl_seat_destroy(g_wayland_ctx->seat);

        for (auto& out : g_wayland_ctx->outputs) {
            wl_output_destroy(out.output);
        }

        if (g_wayland_ctx->wm_base) xdg_wm_base_destroy(g_wayland_ctx->wm_base);
        if (g_wayland_ctx->subcompositor) wl_subcompositor_destroy(g_wayland_ctx->subcompositor);
        if (g_wayland_ctx->compositor) wl_compositor_destroy(g_wayland_ctx->compositor);
        if (g_wayland_ctx->registry) wl_registry_destroy(g_wayland_ctx->registry);
        if (g_wayland_ctx->display) wl_display_disconnect(g_wayland_ctx->display);

        delete g_wayland_ctx;
        g_wayland_ctx = nullptr;
    }
}

//=============================================================================
// Window Implementation (Subsurface-based)
//=============================================================================

Window* create_window_impl(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    // Initialize or reference the global Wayland context
    wayland_context_ref();

    if (!g_wayland_ctx || !g_wayland_ctx->compositor || !g_wayland_ctx->subcompositor ||
        !g_wayland_ctx->root_surface) {
        wayland_context_unref();
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    WaylandContext* ctx = g_wayland_ctx;
    const WindowConfigEntry& win_cfg = config.windows[0];

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->owner = window;
    window->impl->width = win_cfg.width;
    window->impl->height = win_cfg.height;
    window->impl->title = win_cfg.title;
    window->impl->name = win_cfg.name;
    window->impl->style = win_cfg.style;

    // Calculate position (use config or default to center)
    if (win_cfg.x >= 0) {
        window->impl->x = win_cfg.x;
    } else {
        // Center on primary output
        if (!ctx->outputs.empty()) {
            window->impl->x = ctx->outputs[0].x + (ctx->outputs[0].width - win_cfg.width) / 2;
        } else {
            window->impl->x = 100;
        }
    }
    if (win_cfg.y >= 0) {
        window->impl->y = win_cfg.y;
    } else {
        if (!ctx->outputs.empty()) {
            window->impl->y = ctx->outputs[0].y + (ctx->outputs[0].height - win_cfg.height) / 2;
        } else {
            window->impl->y = 100;
        }
    }

    // Initialize input systems
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
    window->impl->mouse_device.set_window(window);
    window->impl->keyboard_device.set_dispatcher(&window->impl->keyboard_dispatcher);
    window->impl->keyboard_device.set_window(window);

    // Create subsurface
    window->impl->surface = wl_compositor_create_surface(ctx->compositor);
    if (!window->impl->surface) {
        delete window->impl;
        delete window;
        wayland_context_unref();
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    window->impl->subsurface = wl_subcompositor_get_subsurface(
        ctx->subcompositor, window->impl->surface, ctx->root_surface);
    if (!window->impl->subsurface) {
        wl_surface_destroy(window->impl->surface);
        delete window->impl;
        delete window;
        wayland_context_unref();
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    // Position the subsurface within root surface
    wl_subsurface_set_position(window->impl->subsurface, window->impl->x, window->impl->y);
    wl_subsurface_set_desync(window->impl->subsurface);  // Independent updates

    // Register surface for input routing
    ctx->surface_to_window[window->impl->surface] = window;

    // Create or share graphics context
    Graphics* gfx = nullptr;

    if (config.shared_graphics) {
        // Use shared graphics context
        gfx = config.shared_graphics;
        window->impl->owns_graphics = false;
    } else if (ctx->shared_graphics) {
        // Use context's shared graphics
        gfx = ctx->shared_graphics;
        window->impl->owns_graphics = false;
    } else {
        // Create new graphics context
        Backend requested = config.backend;
        if (requested == Backend::Auto) {
            requested = get_default_backend();
        }

        switch (requested) {
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_wayland(ctx->display, window->impl->surface,
                                                      win_cfg.width, win_cfg.height, config);
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_wayland(ctx->display, window->impl->surface,
                                                      win_cfg.width, win_cfg.height, config);
                break;
#endif
            default:
                break;
        }

        // Fallback to default if requested backend failed
        if (!gfx && config.backend != Backend::Auto) {
            Backend fallback = get_default_backend();
            switch (fallback) {
#ifdef WINDOW_HAS_OPENGL
                case Backend::OpenGL:
                    gfx = create_opengl_graphics_wayland(ctx->display, window->impl->surface,
                                                          win_cfg.width, win_cfg.height, config);
                    break;
#endif
#ifdef WINDOW_HAS_VULKAN
                case Backend::Vulkan:
                    gfx = create_vulkan_graphics_wayland(ctx->display, window->impl->surface,
                                                          win_cfg.width, win_cfg.height, config);
                    break;
#endif
                default:
                    break;
            }
        }

        if (gfx) {
            window->impl->owns_graphics = true;
            ctx->shared_graphics = gfx;  // Share for future windows
        }
    }

    if (!gfx) {
        ctx->surface_to_window.erase(window->impl->surface);
        wl_subsurface_destroy(window->impl->subsurface);
        wl_surface_destroy(window->impl->surface);
        delete window->impl;
        delete window;
        wayland_context_unref();
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;
    window->impl->visible = win_cfg.visible;

    // Commit the surface
    wl_surface_commit(window->impl->surface);
    wl_surface_commit(ctx->root_surface);
    wl_display_flush(ctx->display);

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        // Remove from surface tracking
        if (g_wayland_ctx && impl->surface) {
            g_wayland_ctx->surface_to_window.erase(impl->surface);

            // Clear focused/pointer window if it's this window
            if (g_wayland_ctx->focused_window == this) {
                g_wayland_ctx->focused_window = nullptr;
            }
            if (g_wayland_ctx->pointer_window == this) {
                g_wayland_ctx->pointer_window = nullptr;
            }

            // If this window owned the shared graphics, clear it
            if (impl->owns_graphics && g_wayland_ctx->shared_graphics == impl->gfx) {
                g_wayland_ctx->shared_graphics = nullptr;
            }
        }

        // Only delete graphics if we own it
        if (impl->owns_graphics) {
            delete impl->gfx;
        }

        // Destroy subsurface and surface
        if (impl->subsurface) wl_subsurface_destroy(impl->subsurface);
        if (impl->surface) wl_surface_destroy(impl->surface);

        delete impl;
        impl = nullptr;

        // Unref the global context
        wayland_context_unref();
    }
    delete this;
}

void Window::show() {
    if (impl && impl->surface && g_wayland_ctx) {
        wl_surface_commit(impl->surface);
        wl_surface_commit(g_wayland_ctx->root_surface);
        wl_display_flush(g_wayland_ctx->display);
        impl->visible = true;
    }
}

void Window::hide() {
    if (impl) impl->visible = false;
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl) {
        impl->title = title;
        // Subsurfaces don't have titles - title is stored for reference only
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
    if (!impl || !impl->subsurface || !g_wayland_ctx) return false;

    impl->x = x;
    impl->y = y;
    wl_subsurface_set_position(impl->subsurface, x, y);
    wl_surface_commit(impl->surface);
    wl_surface_commit(g_wayland_ctx->root_surface);
    wl_display_flush(g_wayland_ctx->display);
    return true;
}

bool Window::get_position(int* x, int* y) const {
    if (impl) {
        if (x) *x = impl->x;
        if (y) *y = impl->y;
        return true;
    }
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const { return true; }  // Subsurfaces support positioning

void Window::set_style(WindowStyle style) {
    if (!impl) return;

    impl->style = style;

    // Handle fullscreen for subsurface
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    if (g_wayland_ctx) {
        wl_surface_commit(impl->surface);
        wl_display_flush(g_wayland_ctx->display);
    }
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !g_wayland_ctx) return;
    if (impl->is_fullscreen == fullscreen) return;

    if (fullscreen) {
        // Save windowed state
        impl->windowed_x = impl->x;
        impl->windowed_y = impl->y;
        impl->windowed_width = impl->width;
        impl->windowed_height = impl->height;

        // For subsurface "fullscreen": resize to cover the output and reposition
        if (!g_wayland_ctx->outputs.empty()) {
            const OutputInfo& out = g_wayland_ctx->outputs[0];
            impl->x = out.x;
            impl->y = out.y;
            impl->width = out.width;
            impl->height = out.height;
            wl_subsurface_set_position(impl->subsurface, impl->x, impl->y);
        }

        impl->is_fullscreen = true;
        impl->style = impl->style | WindowStyle::Fullscreen;
    } else {
        // Restore windowed state
        impl->x = impl->windowed_x;
        impl->y = impl->windowed_y;
        impl->width = impl->windowed_width;
        impl->height = impl->windowed_height;
        wl_subsurface_set_position(impl->subsurface, impl->x, impl->y);

        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }

    wl_surface_commit(impl->surface);
    wl_surface_commit(g_wayland_ctx->root_surface);
    wl_display_flush(g_wayland_ctx->display);
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    // Subsurfaces can be reordered - but keeping simple for now
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
    if (g_wayland_ctx && g_wayland_ctx->display) {
        wl_display_dispatch_pending(g_wayland_ctx->display);
        wl_display_flush(g_wayland_ctx->display);
    }
}

Graphics* Window::graphics() const { return impl ? impl->gfx : nullptr; }
void* Window::native_handle() const { return impl ? impl->surface : nullptr; }
void* Window::native_display() const { return g_wayland_ctx ? g_wayland_ctx->display : nullptr; }

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
    return g_wayland_ctx ? g_wayland_ctx->current_mods : KeyMod::None;
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

//=============================================================================
// Message Box (stub)
//=============================================================================

static MessageBoxButton msgbox_default_button(MessageBoxType type) {
    switch (type) {
        case MessageBoxType::Ok:               return MessageBoxButton::Ok;
        case MessageBoxType::OkCancel:         return MessageBoxButton::Ok;
        case MessageBoxType::YesNo:            return MessageBoxButton::Yes;
        case MessageBoxType::YesNoCancel:      return MessageBoxButton::Yes;
        case MessageBoxType::RetryCancel:      return MessageBoxButton::Cancel;
        case MessageBoxType::AbortRetryIgnore: return MessageBoxButton::Abort;
        default:                               return MessageBoxButton::None;
    }
}

MessageBoxButton Window::show_message_box(
    const char* title, const char* message,
    MessageBoxType type, MessageBoxIcon icon, Window* parent)
{
    (void)title; (void)message; (void)icon; (void)parent;
    return msgbox_default_button(type);
}

void Window::show_message_box_async(
    const char* title, const char* message,
    MessageBoxType type, MessageBoxIcon icon,
    Window* parent, MessageBoxCallback callback)
{
    if (callback) {
        callback(show_message_box(title, message, type, icon, parent));
    }
}

} // namespace window

#endif // WINDOW_PLATFORM_WAYLAND
