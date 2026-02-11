/*
 * window_wasm.cpp - WebAssembly/Emscripten window implementation
 *
 * Uses Emscripten HTML5 API for canvas-based rendering
 */

#ifdef WINDOW_PLATFORM_WASM

#include "window.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <map>

namespace window {

// Forward declarations
class WasmGraphics;
Graphics* create_webgl_graphics(const char* canvas_id, int width, int height, const Config& config);

//=============================================================================
// Key Code Translation
//=============================================================================

static Key translate_key_code(const char* code) {
    // Map DOM key codes to our Key enum
    if (!code) return Key::Unknown;

    // Letters
    if (strcmp(code, "KeyA") == 0) return Key::A;
    if (strcmp(code, "KeyB") == 0) return Key::B;
    if (strcmp(code, "KeyC") == 0) return Key::C;
    if (strcmp(code, "KeyD") == 0) return Key::D;
    if (strcmp(code, "KeyE") == 0) return Key::E;
    if (strcmp(code, "KeyF") == 0) return Key::F;
    if (strcmp(code, "KeyG") == 0) return Key::G;
    if (strcmp(code, "KeyH") == 0) return Key::H;
    if (strcmp(code, "KeyI") == 0) return Key::I;
    if (strcmp(code, "KeyJ") == 0) return Key::J;
    if (strcmp(code, "KeyK") == 0) return Key::K;
    if (strcmp(code, "KeyL") == 0) return Key::L;
    if (strcmp(code, "KeyM") == 0) return Key::M;
    if (strcmp(code, "KeyN") == 0) return Key::N;
    if (strcmp(code, "KeyO") == 0) return Key::O;
    if (strcmp(code, "KeyP") == 0) return Key::P;
    if (strcmp(code, "KeyQ") == 0) return Key::Q;
    if (strcmp(code, "KeyR") == 0) return Key::R;
    if (strcmp(code, "KeyS") == 0) return Key::S;
    if (strcmp(code, "KeyT") == 0) return Key::T;
    if (strcmp(code, "KeyU") == 0) return Key::U;
    if (strcmp(code, "KeyV") == 0) return Key::V;
    if (strcmp(code, "KeyW") == 0) return Key::W;
    if (strcmp(code, "KeyX") == 0) return Key::X;
    if (strcmp(code, "KeyY") == 0) return Key::Y;
    if (strcmp(code, "KeyZ") == 0) return Key::Z;

    // Numbers
    if (strcmp(code, "Digit0") == 0) return Key::Num0;
    if (strcmp(code, "Digit1") == 0) return Key::Num1;
    if (strcmp(code, "Digit2") == 0) return Key::Num2;
    if (strcmp(code, "Digit3") == 0) return Key::Num3;
    if (strcmp(code, "Digit4") == 0) return Key::Num4;
    if (strcmp(code, "Digit5") == 0) return Key::Num5;
    if (strcmp(code, "Digit6") == 0) return Key::Num6;
    if (strcmp(code, "Digit7") == 0) return Key::Num7;
    if (strcmp(code, "Digit8") == 0) return Key::Num8;
    if (strcmp(code, "Digit9") == 0) return Key::Num9;

    // Function keys
    if (strcmp(code, "F1") == 0) return Key::F1;
    if (strcmp(code, "F2") == 0) return Key::F2;
    if (strcmp(code, "F3") == 0) return Key::F3;
    if (strcmp(code, "F4") == 0) return Key::F4;
    if (strcmp(code, "F5") == 0) return Key::F5;
    if (strcmp(code, "F6") == 0) return Key::F6;
    if (strcmp(code, "F7") == 0) return Key::F7;
    if (strcmp(code, "F8") == 0) return Key::F8;
    if (strcmp(code, "F9") == 0) return Key::F9;
    if (strcmp(code, "F10") == 0) return Key::F10;
    if (strcmp(code, "F11") == 0) return Key::F11;
    if (strcmp(code, "F12") == 0) return Key::F12;

    // Navigation
    if (strcmp(code, "Escape") == 0) return Key::Escape;
    if (strcmp(code, "Tab") == 0) return Key::Tab;
    if (strcmp(code, "CapsLock") == 0) return Key::CapsLock;
    if (strcmp(code, "ShiftLeft") == 0 || strcmp(code, "ShiftRight") == 0) return Key::Shift;
    if (strcmp(code, "ControlLeft") == 0 || strcmp(code, "ControlRight") == 0) return Key::Control;
    if (strcmp(code, "AltLeft") == 0 || strcmp(code, "AltRight") == 0) return Key::Alt;
    if (strcmp(code, "MetaLeft") == 0 || strcmp(code, "MetaRight") == 0) return Key::Super;
    if (strcmp(code, "Space") == 0) return Key::Space;
    if (strcmp(code, "Enter") == 0) return Key::Enter;
    if (strcmp(code, "Backspace") == 0) return Key::Backspace;
    if (strcmp(code, "Delete") == 0) return Key::Delete;
    if (strcmp(code, "Insert") == 0) return Key::Insert;
    if (strcmp(code, "Home") == 0) return Key::Home;
    if (strcmp(code, "End") == 0) return Key::End;
    if (strcmp(code, "PageUp") == 0) return Key::PageUp;
    if (strcmp(code, "PageDown") == 0) return Key::PageDown;

    // Arrow keys
    if (strcmp(code, "ArrowUp") == 0) return Key::Up;
    if (strcmp(code, "ArrowDown") == 0) return Key::Down;
    if (strcmp(code, "ArrowLeft") == 0) return Key::Left;
    if (strcmp(code, "ArrowRight") == 0) return Key::Right;

    // Punctuation
    if (strcmp(code, "Minus") == 0) return Key::Minus;
    if (strcmp(code, "Equal") == 0) return Key::Equals;
    if (strcmp(code, "BracketLeft") == 0) return Key::LeftBracket;
    if (strcmp(code, "BracketRight") == 0) return Key::RightBracket;
    if (strcmp(code, "Backslash") == 0) return Key::Backslash;
    if (strcmp(code, "Semicolon") == 0) return Key::Semicolon;
    if (strcmp(code, "Quote") == 0) return Key::Apostrophe;
    if (strcmp(code, "Comma") == 0) return Key::Comma;
    if (strcmp(code, "Period") == 0) return Key::Period;
    if (strcmp(code, "Slash") == 0) return Key::Slash;
    if (strcmp(code, "Backquote") == 0) return Key::GraveAccent;

    return Key::Unknown;
}

static KeyMod get_key_modifiers(const EmscriptenKeyboardEvent* event) {
    KeyMod mods = KeyMod::None;
    if (event->shiftKey) mods = mods | KeyMod::Shift;
    if (event->ctrlKey) mods = mods | KeyMod::Ctrl;
    if (event->altKey) mods = mods | KeyMod::Alt;
    if (event->metaKey) mods = mods | KeyMod::Super;
    return mods;
}

static MouseButton translate_mouse_button(unsigned short button) {
    switch (button) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Middle;
        case 2: return MouseButton::Right;
        case 3: return MouseButton::X1;
        case 4: return MouseButton::X2;
        default: return MouseButton::Unknown;
    }
}

//=============================================================================
// Window Implementation
//=============================================================================

struct Window::Impl {
    Window* owner = nullptr;
    std::string canvas_id = "#canvas";
    std::string title = "Window";
    int width = 800;
    int height = 600;
    bool visible = true;
    bool should_close = false;
    bool focused = true;
    WindowStyle style = WindowStyle::Default;

    // Graphics
    Graphics* gfx = nullptr;

    // Input state
    bool key_states[512] = {};
    bool mouse_button_states[8] = {};
    int mouse_x = 0;
    int mouse_y = 0;
    KeyMod current_modifiers = KeyMod::None;

    // Input handlers
    input::MouseEventDispatcher mouse_dispatcher;
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultMouseDevice mouse_device;
    input::DefaultKeyboardDevice keyboard_device;

    // Callbacks
    WindowCloseCallback close_callback;
    WindowResizeCallback resize_callback;
    WindowFocusCallback focus_callback;
};

// Global window map for event dispatch
static std::map<std::string, Window*> g_windows;

//=============================================================================
// Event Callbacks
//=============================================================================

static EM_BOOL key_callback(int event_type, const EmscriptenKeyboardEvent* event, void* user_data) {
    Window* window = static_cast<Window*>(user_data);
    if (!window || !window->impl) return EM_FALSE;

    Key key = translate_key_code(event->code);
    KeyMod mods = get_key_modifiers(event);
    window->impl->current_modifiers = mods;

    if (key != Key::Unknown) {
        bool pressed = (event_type == EMSCRIPTEN_EVENT_KEYDOWN);
        window->impl->key_states[static_cast<int>(key)] = pressed;

        input::KeyboardEvent kb_event;
        kb_event.type = pressed ? input::KeyboardEventType::KeyDown : input::KeyboardEventType::KeyUp;
        kb_event.key = key;
        kb_event.modifiers = mods;
        kb_event.repeat = event->repeat;

        window->impl->keyboard_dispatcher.dispatch(kb_event);
    }

    // Handle text input for printable characters
    if (event_type == EMSCRIPTEN_EVENT_KEYPRESS && event->charCode != 0) {
        input::KeyboardEvent char_event;
        char_event.type = input::KeyboardEventType::Character;
        char_event.character = event->charCode;
        char_event.modifiers = mods;

        window->impl->keyboard_dispatcher.dispatch(char_event);
    }

    return EM_TRUE;
}

static EM_BOOL mouse_callback(int event_type, const EmscriptenMouseEvent* event, void* user_data) {
    Window* window = static_cast<Window*>(user_data);
    if (!window || !window->impl) return EM_FALSE;

    int x = event->targetX;
    int y = event->targetY;

    window->impl->mouse_x = x;
    window->impl->mouse_y = y;

    input::MouseEvent mouse_event;
    mouse_event.x = x;
    mouse_event.y = y;

    switch (event_type) {
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            mouse_event.type = input::MouseEventType::Move;
            mouse_event.dx = event->movementX;
            mouse_event.dy = event->movementY;
            break;

        case EMSCRIPTEN_EVENT_MOUSEDOWN: {
            MouseButton btn = translate_mouse_button(event->button);
            if (btn != MouseButton::Unknown) {
                window->impl->mouse_button_states[static_cast<int>(btn)] = true;
            }
            mouse_event.type = input::MouseEventType::ButtonDown;
            mouse_event.button = btn;
            break;
        }

        case EMSCRIPTEN_EVENT_MOUSEUP: {
            MouseButton btn = translate_mouse_button(event->button);
            if (btn != MouseButton::Unknown) {
                window->impl->mouse_button_states[static_cast<int>(btn)] = false;
            }
            mouse_event.type = input::MouseEventType::ButtonUp;
            mouse_event.button = btn;
            break;
        }

        default:
            return EM_FALSE;
    }

    window->impl->mouse_dispatcher.dispatch(mouse_event);
    return EM_TRUE;
}

static EM_BOOL wheel_callback(int event_type, const EmscriptenWheelEvent* event, void* user_data) {
    (void)event_type;
    Window* window = static_cast<Window*>(user_data);
    if (!window || !window->impl) return EM_FALSE;

    input::MouseEvent mouse_event;
    mouse_event.type = input::MouseEventType::Wheel;
    mouse_event.x = window->impl->mouse_x;
    mouse_event.y = window->impl->mouse_y;
    mouse_event.wheel_delta = static_cast<int>(-event->deltaY);
    mouse_event.wheel_delta_x = static_cast<int>(-event->deltaX);

    window->impl->mouse_dispatcher.dispatch(mouse_event);
    return EM_TRUE;
}

static EM_BOOL resize_callback(int event_type, const EmscriptenUiEvent* event, void* user_data) {
    (void)event_type;
    Window* window = static_cast<Window*>(user_data);
    if (!window || !window->impl) return EM_FALSE;

    // Get canvas size
    int width, height;
    emscripten_get_canvas_element_size(window->impl->canvas_id.c_str(), &width, &height);

    if (width != window->impl->width || height != window->impl->height) {
        window->impl->width = width;
        window->impl->height = height;

        if (window->impl->resize_callback) {
            window->impl->resize_callback(window, width, height);
        }
    }

    return EM_TRUE;
}

static EM_BOOL focus_callback(int event_type, const EmscriptenFocusEvent* event, void* user_data) {
    (void)event;
    Window* window = static_cast<Window*>(user_data);
    if (!window || !window->impl) return EM_FALSE;

    bool focused = (event_type == EMSCRIPTEN_EVENT_FOCUS);
    window->impl->focused = focused;

    if (window->impl->focus_callback) {
        window->impl->focus_callback(window, focused);
    }

    return EM_TRUE;
}

//=============================================================================
// Window Creation
//=============================================================================

Window* create_window_impl(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    const WindowConfigEntry& win_cfg = config.windows[0];

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->owner = window;
    window->impl->width = win_cfg.width;
    window->impl->height = win_cfg.height;
    window->impl->title = win_cfg.title;
    window->impl->style = win_cfg.style;
    window->impl->visible = win_cfg.visible;

    // Use canvas ID from window name or default
    window->impl->canvas_id = std::string("#") + win_cfg.name;

    // Try to set canvas size
    emscripten_set_canvas_element_size(window->impl->canvas_id.c_str(),
                                       win_cfg.width, win_cfg.height);

    // Set up event listeners
    const char* canvas = window->impl->canvas_id.c_str();

    emscripten_set_keydown_callback(canvas, window, EM_TRUE, key_callback);
    emscripten_set_keyup_callback(canvas, window, EM_TRUE, key_callback);
    emscripten_set_keypress_callback(canvas, window, EM_TRUE, key_callback);

    emscripten_set_mousedown_callback(canvas, window, EM_TRUE, mouse_callback);
    emscripten_set_mouseup_callback(canvas, window, EM_TRUE, mouse_callback);
    emscripten_set_mousemove_callback(canvas, window, EM_TRUE, mouse_callback);

    emscripten_set_wheel_callback(canvas, window, EM_TRUE, wheel_callback);

    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, window, EM_TRUE, resize_callback);

    emscripten_set_focus_callback(canvas, window, EM_TRUE, focus_callback);
    emscripten_set_blur_callback(canvas, window, EM_TRUE, focus_callback);

    // Create graphics context (WebGL)
    Graphics* gfx = nullptr;
    if (config.backend == Backend::Auto || config.backend == Backend::OpenGL) {
#ifdef WINDOW_SUPPORT_OPENGL
        gfx = create_webgl_graphics(canvas, win_cfg.width, win_cfg.height, config);
#endif
    }

    if (!gfx) {
        delete window->impl;
        delete window;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;

    // Register window
    g_windows[window->impl->canvas_id] = window;

    // Set document title
    EM_ASM({
        document.title = UTF8ToString($0);
    }, win_cfg.title);

    set_result(Result::Success);
    return window;
}

//=============================================================================
// Window Methods
//=============================================================================

void Window::destroy() {
    if (impl) {
        // Unregister window
        g_windows.erase(impl->canvas_id);

        // Remove event listeners
        const char* canvas = impl->canvas_id.c_str();
        emscripten_set_keydown_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_keyup_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_keypress_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_mousedown_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_mouseup_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_mousemove_callback(canvas, nullptr, EM_FALSE, nullptr);
        emscripten_set_wheel_callback(canvas, nullptr, EM_FALSE, nullptr);

        if (impl->gfx) {
            impl->gfx->destroy();
        }

        delete impl;
        impl = nullptr;
    }
    delete this;
}

void Window::show() {
    if (impl) {
        impl->visible = true;
        // Show canvas via JavaScript
        EM_ASM({
            var canvas = document.querySelector(UTF8ToString($0));
            if (canvas) canvas.style.display = 'block';
        }, impl->canvas_id.c_str());
    }
}

void Window::hide() {
    if (impl) {
        impl->visible = false;
        EM_ASM({
            var canvas = document.querySelector(UTF8ToString($0));
            if (canvas) canvas.style.display = 'none';
        }, impl->canvas_id.c_str());
    }
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl && title) {
        impl->title = title;
        EM_ASM({
            document.title = UTF8ToString($0);
        }, title);
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl && width > 0 && height > 0) {
        impl->width = width;
        impl->height = height;
        emscripten_set_canvas_element_size(impl->canvas_id.c_str(), width, height);
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
    (void)x; (void)y;
    // Canvas position is controlled by CSS, not directly settable
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

void Window::set_style(WindowStyle style) {
    if (impl) {
        impl->style = style;
    }
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (impl) {
        if (fullscreen) {
            EmscriptenFullscreenStrategy strategy = {};
            strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
            strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
            strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
            emscripten_request_fullscreen_strategy(impl->canvas_id.c_str(), EM_TRUE, &strategy);
        } else {
            emscripten_exit_fullscreen();
        }
    }
}

bool Window::is_fullscreen() const {
    EmscriptenFullscreenChangeEvent event;
    if (emscripten_get_fullscreen_status(&event) == EMSCRIPTEN_RESULT_SUCCESS) {
        return event.isFullscreen;
    }
    return false;
}

void Window::set_always_on_top(bool always_on_top) {
    (void)always_on_top;
    // Not supported in browser
}

bool Window::is_always_on_top() const {
    return false;
}

bool Window::should_close() const {
    return impl ? impl->should_close : true;
}

void Window::set_should_close(bool close) {
    if (impl) {
        impl->should_close = close;
    }
}

void Window::poll_events() {
    // In Emscripten, events are dispatched automatically via callbacks
    // This is a no-op, but we can yield to the browser here
    emscripten_sleep(0);
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    // Return canvas ID as native handle
    return impl ? (void*)impl->canvas_id.c_str() : nullptr;
}

// Callbacks
void Window::set_close_callback(WindowCloseCallback callback) {
    if (impl) impl->close_callback = callback;
}

void Window::set_resize_callback(WindowResizeCallback callback) {
    if (impl) impl->resize_callback = callback;
}

void Window::set_move_callback(WindowMoveCallback callback) {
    (void)callback; // Not supported
}

void Window::set_focus_callback(WindowFocusCallback callback) {
    if (impl) impl->focus_callback = callback;
}

void Window::set_state_callback(WindowStateCallback callback) {
    (void)callback; // Not supported
}

void Window::set_touch_callback(TouchCallback callback) {
    (void)callback; // TODO: Implement touch support
}

void Window::set_dpi_change_callback(DpiChangeCallback callback) {
    (void)callback; // TODO: Implement DPI change detection
}

void Window::set_drop_file_callback(DropFileCallback callback) {
    (void)callback; // TODO: Implement file drop
}

// Input state
bool Window::is_key_down(Key key) const {
    if (!impl) return false;
    int index = static_cast<int>(key);
    return (index >= 0 && index < 512) ? impl->key_states[index] : false;
}

bool Window::is_mouse_button_down(MouseButton button) const {
    if (!impl) return false;
    int index = static_cast<int>(button);
    return (index >= 0 && index < 8) ? impl->mouse_button_states[index] : false;
}

void Window::get_mouse_position(int* x, int* y) const {
    if (impl) {
        if (x) *x = impl->mouse_x;
        if (y) *y = impl->mouse_y;
    }
}

KeyMod Window::get_current_modifiers() const {
    return impl ? impl->current_modifiers : KeyMod::None;
}

// Mouse handlers
bool Window::add_mouse_handler(input::IMouseHandler* handler) {
    return impl ? impl->mouse_dispatcher.add_handler(handler) : false;
}

bool Window::remove_mouse_handler(input::IMouseHandler* handler) {
    return impl ? impl->mouse_dispatcher.remove_handler(handler) : false;
}

bool Window::remove_mouse_handler(const char* handler_id) {
    return impl ? impl->mouse_dispatcher.remove_handler(handler_id) : false;
}

input::MouseEventDispatcher* Window::get_mouse_dispatcher() {
    return impl ? &impl->mouse_dispatcher : nullptr;
}

// Keyboard handlers
bool Window::add_keyboard_handler(input::IKeyboardHandler* handler) {
    return impl ? impl->keyboard_dispatcher.add_handler(handler) : false;
}

bool Window::remove_keyboard_handler(input::IKeyboardHandler* handler) {
    return impl ? impl->keyboard_dispatcher.remove_handler(handler) : false;
}

bool Window::remove_keyboard_handler(const char* handler_id) {
    return impl ? impl->keyboard_dispatcher.remove_handler(handler_id) : false;
}

input::KeyboardEventDispatcher* Window::get_keyboard_dispatcher() {
    return impl ? &impl->keyboard_dispatcher : nullptr;
}

// Cursor
void Window::set_cursor(CursorType cursor) {
    const char* css_cursor = "default";
    switch (cursor) {
        case CursorType::Arrow: css_cursor = "default"; break;
        case CursorType::IBeam: css_cursor = "text"; break;
        case CursorType::Crosshair: css_cursor = "crosshair"; break;
        case CursorType::Hand: css_cursor = "pointer"; break;
        case CursorType::ResizeH: css_cursor = "ew-resize"; break;
        case CursorType::ResizeV: css_cursor = "ns-resize"; break;
        case CursorType::ResizeNESW: css_cursor = "nesw-resize"; break;
        case CursorType::ResizeNWSE: css_cursor = "nwse-resize"; break;
        case CursorType::ResizeAll: css_cursor = "move"; break;
        case CursorType::NotAllowed: css_cursor = "not-allowed"; break;
        case CursorType::Wait: css_cursor = "wait"; break;
        case CursorType::WaitArrow: css_cursor = "progress"; break;
        case CursorType::Help: css_cursor = "help"; break;
        case CursorType::Hidden: css_cursor = "none"; break;
        default: break;
    }

    if (impl) {
        EM_ASM({
            var canvas = document.querySelector(UTF8ToString($0));
            if (canvas) canvas.style.cursor = UTF8ToString($1);
        }, impl->canvas_id.c_str(), css_cursor);
    }
}

CursorType Window::get_cursor() const {
    return CursorType::Arrow; // Not tracked
}

void Window::set_cursor_visible(bool visible) {
    set_cursor(visible ? CursorType::Arrow : CursorType::Hidden);
}

bool Window::is_cursor_visible() const {
    return true; // Not tracked
}

void Window::set_cursor_confined(bool confined) {
    if (impl && confined) {
        emscripten_request_pointerlock(impl->canvas_id.c_str(), EM_TRUE);
    } else {
        emscripten_exit_pointerlock();
    }
}

bool Window::is_cursor_confined() const {
    EmscriptenPointerlockChangeEvent event;
    if (emscripten_get_pointerlock_status(&event) == EMSCRIPTEN_RESULT_SUCCESS) {
        return event.isActive;
    }
    return false;
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

#endif // WINDOW_PLATFORM_WASM
