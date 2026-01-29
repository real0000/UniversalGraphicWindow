/*
 * input_mouse.cpp - Cross-platform mouse input utilities
 */

#include "input_mouse.hpp"
#include <cstring>
#include <cctype>

namespace window {
namespace input {

//=============================================================================
// Helper Functions
//=============================================================================

static bool str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

//=============================================================================
// Mouse Button Utilities
//=============================================================================

const char* mouse_button_to_string(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return "Left";
        case MouseButton::Right: return "Right";
        case MouseButton::Middle: return "Middle";
        case MouseButton::X1: return "X1";
        case MouseButton::X2: return "X2";
        case MouseButton::Unknown: return "Unknown";
        default: return "Unknown";
    }
}

MouseButton string_to_mouse_button(const char* str) {
    if (!str || !*str) return MouseButton::Unknown;

    if (str_iequals(str, "Left") || str_iequals(str, "LMB") || str_iequals(str, "Mouse1") || str_iequals(str, "Button1")) return MouseButton::Left;
    if (str_iequals(str, "Right") || str_iequals(str, "RMB") || str_iequals(str, "Mouse2") || str_iequals(str, "Button2")) return MouseButton::Right;
    if (str_iequals(str, "Middle") || str_iequals(str, "MMB") || str_iequals(str, "Mouse3") || str_iequals(str, "Button3")) return MouseButton::Middle;
    if (str_iequals(str, "X1") || str_iequals(str, "Back") || str_iequals(str, "Mouse4") || str_iequals(str, "Button4") || str_iequals(str, "XButton1")) return MouseButton::X1;
    if (str_iequals(str, "X2") || str_iequals(str, "Forward") || str_iequals(str, "Mouse5") || str_iequals(str, "Button5") || str_iequals(str, "XButton2")) return MouseButton::X2;

    return MouseButton::Unknown;
}

int mouse_button_to_index(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return 0;
        case MouseButton::Right: return 1;
        case MouseButton::Middle: return 2;
        case MouseButton::X1: return 3;
        case MouseButton::X2: return 4;
        default: return -1;
    }
}

MouseButton index_to_mouse_button(int index) {
    switch (index) {
        case 0: return MouseButton::Left;
        case 1: return MouseButton::Right;
        case 2: return MouseButton::Middle;
        case 3: return MouseButton::X1;
        case 4: return MouseButton::X2;
        default: return MouseButton::Unknown;
    }
}

bool is_primary_button(MouseButton button) {
    return button == MouseButton::Left || button == MouseButton::Right;
}

bool is_extra_button(MouseButton button) {
    return button == MouseButton::X1 || button == MouseButton::X2;
}

//=============================================================================
// Mouse State
//=============================================================================

bool MouseState::any_button_down() const {
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        if (buttons[i]) return true;
    }
    return false;
}

void MouseState::get_delta(int* dx, int* dy) const {
    if (dx) *dx = x - last_x;
    if (dy) *dy = y - last_y;
}

void MouseState::set_position(int new_x, int new_y) {
    last_x = x;
    last_y = y;
    x = new_x;
    y = new_y;
}

void MouseState::reset_scroll() {
    scroll_x = 0.0f;
    scroll_y = 0.0f;
}

void MouseState::reset() {
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) {
        buttons[i] = false;
    }
    x = 0;
    y = 0;
    last_x = 0;
    last_y = 0;
    scroll_x = 0.0f;
    scroll_y = 0.0f;
}

//=============================================================================
// MouseEventDispatcher
//=============================================================================

MouseEventDispatcher::MouseEventDispatcher()
    : handler_count_(0)
    , needs_sort_(false)
{
    for (int i = 0; i < MAX_MOUSE_HANDLERS; i++) {
        handlers_[i] = nullptr;
    }
}

MouseEventDispatcher::~MouseEventDispatcher() {
    // Handlers are not owned by the dispatcher, so don't delete them
}

bool MouseEventDispatcher::add_handler(IMouseHandler* handler) {
    if (!handler) return false;

    // Check if already registered
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            return false; // Already registered
        }
    }

    // Check capacity
    if (handler_count_ >= MAX_MOUSE_HANDLERS) {
        return false;
    }

    handlers_[handler_count_++] = handler;
    needs_sort_ = true;
    return true;
}

bool MouseEventDispatcher::remove_handler(IMouseHandler* handler) {
    if (!handler) return false;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            // Shift remaining handlers down
            for (int j = i; j < handler_count_ - 1; j++) {
                handlers_[j] = handlers_[j + 1];
            }
            handlers_[--handler_count_] = nullptr;
            return true;
        }
    }
    return false;
}

bool MouseEventDispatcher::remove_handler(const char* handler_id) {
    if (!handler_id) return false;

    IMouseHandler* handler = find_handler(handler_id);
    if (handler) {
        return remove_handler(handler);
    }
    return false;
}

int MouseEventDispatcher::get_handler_count() const {
    return handler_count_;
}

IMouseHandler* MouseEventDispatcher::get_handler(int index) const {
    if (index < 0 || index >= handler_count_) {
        return nullptr;
    }
    return handlers_[index];
}

IMouseHandler* MouseEventDispatcher::find_handler(const char* handler_id) const {
    if (!handler_id) return nullptr;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && strcmp(handlers_[i]->get_handler_id(), handler_id) == 0) {
            return handlers_[i];
        }
    }
    return nullptr;
}

void MouseEventDispatcher::sort_handlers() {
    if (!needs_sort_ || handler_count_ <= 1) {
        needs_sort_ = false;
        return;
    }

    // Simple insertion sort (handler count is small, max 16)
    for (int i = 1; i < handler_count_; i++) {
        IMouseHandler* key = handlers_[i];
        int key_priority = key->get_priority();
        int j = i - 1;

        // Sort in descending order of priority (higher priority first)
        while (j >= 0 && handlers_[j]->get_priority() < key_priority) {
            handlers_[j + 1] = handlers_[j];
            j--;
        }
        handlers_[j + 1] = key;
    }

    needs_sort_ = false;
}

bool MouseEventDispatcher::dispatch_move(const MouseMoveEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    // Dispatch to handlers in priority order
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_mouse_move(event)) {
            return true; // Event consumed
        }
    }

    return false;
}

bool MouseEventDispatcher::dispatch_button(const MouseButtonEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    // Dispatch to handlers in priority order
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_mouse_button(event)) {
            return true; // Event consumed
        }
    }

    return false;
}

bool MouseEventDispatcher::dispatch_wheel(const MouseWheelEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    // Dispatch to handlers in priority order
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_mouse_wheel(event)) {
            return true; // Event consumed
        }
    }

    return false;
}

//=============================================================================
// DefaultMouseDevice
//=============================================================================

DefaultMouseDevice::DefaultMouseDevice()
    : dispatcher_(nullptr)
    , window_(nullptr)
    , active_(true)
{
    state_.reset();
}

DefaultMouseDevice::~DefaultMouseDevice() {
    // Dispatcher is not owned
}

const char* DefaultMouseDevice::get_source_id() const {
    return "default_mouse";
}

bool DefaultMouseDevice::is_active() const {
    return active_;
}

const MouseState& DefaultMouseDevice::get_state() const {
    return state_;
}

void DefaultMouseDevice::set_dispatcher(MouseEventDispatcher* dispatcher) {
    dispatcher_ = dispatcher;
}

void DefaultMouseDevice::set_window(Window* window) {
    window_ = window;
}

void DefaultMouseDevice::inject_move(int x, int y, KeyMod modifiers, double timestamp) {
    int dx = x - state_.x;
    int dy = y - state_.y;
    state_.set_position(x, y);

    if (dispatcher_) {
        MouseMoveEvent event;
        event.type = EventType::MouseMove;
        event.window = window_;
        event.timestamp = timestamp;
        event.x = x;
        event.y = y;
        event.dx = dx;
        event.dy = dy;
        event.modifiers = modifiers;
        dispatcher_->dispatch_move(event);
    }
}

void DefaultMouseDevice::inject_button_down(MouseButton button, int x, int y, int clicks, KeyMod modifiers, double timestamp) {
    int index = mouse_button_to_index(button);
    if (index >= 0 && index < MAX_MOUSE_BUTTONS) {
        state_.buttons[index] = true;
    }

    if (dispatcher_) {
        MouseButtonEvent event;
        event.type = EventType::MouseDown;
        event.window = window_;
        event.timestamp = timestamp;
        event.button = button;
        event.x = x;
        event.y = y;
        event.clicks = clicks;
        event.modifiers = modifiers;
        dispatcher_->dispatch_button(event);
    }
}

void DefaultMouseDevice::inject_button_up(MouseButton button, int x, int y, KeyMod modifiers, double timestamp) {
    int index = mouse_button_to_index(button);
    if (index >= 0 && index < MAX_MOUSE_BUTTONS) {
        state_.buttons[index] = false;
    }

    if (dispatcher_) {
        MouseButtonEvent event;
        event.type = EventType::MouseUp;
        event.window = window_;
        event.timestamp = timestamp;
        event.button = button;
        event.x = x;
        event.y = y;
        event.clicks = 1;
        event.modifiers = modifiers;
        dispatcher_->dispatch_button(event);
    }
}

void DefaultMouseDevice::inject_wheel(float dx, float dy, int x, int y, KeyMod modifiers, double timestamp) {
    state_.scroll_x += dx;
    state_.scroll_y += dy;

    if (dispatcher_) {
        MouseWheelEvent event;
        event.type = EventType::MouseWheel;
        event.window = window_;
        event.timestamp = timestamp;
        event.dx = dx;
        event.dy = dy;
        event.x = x;
        event.y = y;
        event.modifiers = modifiers;
        dispatcher_->dispatch_wheel(event);
    }
}

bool DefaultMouseDevice::is_button_down(MouseButton button) const {
    int index = mouse_button_to_index(button);
    if (index >= 0 && index < MAX_MOUSE_BUTTONS) {
        return state_.buttons[index];
    }
    return false;
}

void DefaultMouseDevice::get_position(int* x, int* y) const {
    if (x) *x = state_.x;
    if (y) *y = state_.y;
}

void DefaultMouseDevice::reset() {
    state_.reset();
}

} // namespace input
} // namespace window
