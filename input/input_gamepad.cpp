/*
 * input_gamepad.cpp - Platform-independent gamepad input utilities
 */

#include "input_gamepad.hpp"
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
// GamepadState
//=============================================================================

bool GamepadState::any_button_down() const {
    for (int i = 0; i < MAX_GAMEPAD_BUTTONS; i++) {
        if (buttons[i]) return true;
    }
    return false;
}

void GamepadState::reset() {
    for (int i = 0; i < MAX_GAMEPAD_BUTTONS; i++) {
        buttons[i] = false;
    }
    for (int i = 0; i < MAX_GAMEPAD_AXES; i++) {
        axes[i] = 0.0f;
    }
    connected = false;
    name[0] = '\0';
}

//=============================================================================
// GamepadEventDispatcher
//=============================================================================

GamepadEventDispatcher::GamepadEventDispatcher()
    : handler_count_(0)
    , needs_sort_(false)
{
    for (int i = 0; i < MAX_GAMEPAD_HANDLERS; i++) {
        handlers_[i] = nullptr;
    }
}

GamepadEventDispatcher::~GamepadEventDispatcher() {
    // Handlers are not owned by the dispatcher
}

bool GamepadEventDispatcher::add_handler(IGamepadHandler* handler) {
    if (!handler) return false;

    // Check if already registered
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            return false;
        }
    }

    // Check capacity
    if (handler_count_ >= MAX_GAMEPAD_HANDLERS) {
        return false;
    }

    handlers_[handler_count_++] = handler;
    needs_sort_ = true;
    return true;
}

bool GamepadEventDispatcher::remove_handler(IGamepadHandler* handler) {
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

bool GamepadEventDispatcher::remove_handler(const char* handler_id) {
    if (!handler_id) return false;

    IGamepadHandler* handler = find_handler(handler_id);
    if (handler) {
        return remove_handler(handler);
    }
    return false;
}

int GamepadEventDispatcher::get_handler_count() const {
    return handler_count_;
}

IGamepadHandler* GamepadEventDispatcher::get_handler(int index) const {
    if (index < 0 || index >= handler_count_) {
        return nullptr;
    }
    return handlers_[index];
}

IGamepadHandler* GamepadEventDispatcher::find_handler(const char* handler_id) const {
    if (!handler_id) return nullptr;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && strcmp(handlers_[i]->get_handler_id(), handler_id) == 0) {
            return handlers_[i];
        }
    }
    return nullptr;
}

void GamepadEventDispatcher::sort_handlers() {
    if (!needs_sort_ || handler_count_ <= 1) {
        needs_sort_ = false;
        return;
    }

    // Simple insertion sort (handler count is small)
    for (int i = 1; i < handler_count_; i++) {
        IGamepadHandler* key = handlers_[i];
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

bool GamepadEventDispatcher::dispatch_button(const GamepadButtonEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_button(event)) {
            return true;
        }
    }
    return false;
}

bool GamepadEventDispatcher::dispatch_axis(const GamepadAxisEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_axis(event)) {
            return true;
        }
    }
    return false;
}

void GamepadEventDispatcher::dispatch_connection(const GamepadConnectionEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i]) {
            handlers_[i]->on_connection(event);
        }
    }
}

//=============================================================================
// Utility Functions - Button
//=============================================================================

const char* gamepad_button_to_string(GamepadButton button) {
    switch (button) {
        case GamepadButton::A: return "A";
        case GamepadButton::B: return "B";
        case GamepadButton::X: return "X";
        case GamepadButton::Y: return "Y";
        case GamepadButton::LeftBumper: return "LeftBumper";
        case GamepadButton::RightBumper: return "RightBumper";
        case GamepadButton::Back: return "Back";
        case GamepadButton::Start: return "Start";
        case GamepadButton::Guide: return "Guide";
        case GamepadButton::LeftStick: return "LeftStick";
        case GamepadButton::RightStick: return "RightStick";
        case GamepadButton::DPadUp: return "DPadUp";
        case GamepadButton::DPadDown: return "DPadDown";
        case GamepadButton::DPadLeft: return "DPadLeft";
        case GamepadButton::DPadRight: return "DPadRight";
        default: return "Unknown";
    }
}

GamepadButton string_to_gamepad_button(const char* str) {
    if (!str || !*str) return GamepadButton::Unknown;

    // Xbox/Generic names
    if (str_iequals(str, "A") || str_iequals(str, "Cross")) return GamepadButton::A;
    if (str_iequals(str, "B") || str_iequals(str, "Circle")) return GamepadButton::B;
    if (str_iequals(str, "X") || str_iequals(str, "Square")) return GamepadButton::X;
    if (str_iequals(str, "Y") || str_iequals(str, "Triangle")) return GamepadButton::Y;
    if (str_iequals(str, "LeftBumper") || str_iequals(str, "LB") || str_iequals(str, "L1")) return GamepadButton::LeftBumper;
    if (str_iequals(str, "RightBumper") || str_iequals(str, "RB") || str_iequals(str, "R1")) return GamepadButton::RightBumper;
    if (str_iequals(str, "Back") || str_iequals(str, "Select") || str_iequals(str, "Share")) return GamepadButton::Back;
    if (str_iequals(str, "Start") || str_iequals(str, "Options")) return GamepadButton::Start;
    if (str_iequals(str, "Guide") || str_iequals(str, "Home") || str_iequals(str, "PS")) return GamepadButton::Guide;
    if (str_iequals(str, "LeftStick") || str_iequals(str, "L3") || str_iequals(str, "LS")) return GamepadButton::LeftStick;
    if (str_iequals(str, "RightStick") || str_iequals(str, "R3") || str_iequals(str, "RS")) return GamepadButton::RightStick;
    if (str_iequals(str, "DPadUp") || str_iequals(str, "Up")) return GamepadButton::DPadUp;
    if (str_iequals(str, "DPadDown") || str_iequals(str, "Down")) return GamepadButton::DPadDown;
    if (str_iequals(str, "DPadLeft") || str_iequals(str, "Left")) return GamepadButton::DPadLeft;
    if (str_iequals(str, "DPadRight") || str_iequals(str, "Right")) return GamepadButton::DPadRight;

    return GamepadButton::Unknown;
}

int gamepad_button_to_index(GamepadButton button) {
    if (button >= GamepadButton::Count || button == GamepadButton::Unknown) {
        return -1;
    }
    return static_cast<int>(button);
}

GamepadButton index_to_gamepad_button(int index) {
    if (index < 0 || index >= static_cast<int>(GamepadButton::Count)) {
        return GamepadButton::Unknown;
    }
    return static_cast<GamepadButton>(index);
}

//=============================================================================
// Utility Functions - Axis
//=============================================================================

const char* gamepad_axis_to_string(GamepadAxis axis) {
    switch (axis) {
        case GamepadAxis::LeftX: return "LeftX";
        case GamepadAxis::LeftY: return "LeftY";
        case GamepadAxis::RightX: return "RightX";
        case GamepadAxis::RightY: return "RightY";
        case GamepadAxis::LeftTrigger: return "LeftTrigger";
        case GamepadAxis::RightTrigger: return "RightTrigger";
        default: return "Unknown";
    }
}

GamepadAxis string_to_gamepad_axis(const char* str) {
    if (!str || !*str) return GamepadAxis::Unknown;

    if (str_iequals(str, "LeftX") || str_iequals(str, "LX")) return GamepadAxis::LeftX;
    if (str_iequals(str, "LeftY") || str_iequals(str, "LY")) return GamepadAxis::LeftY;
    if (str_iequals(str, "RightX") || str_iequals(str, "RX")) return GamepadAxis::RightX;
    if (str_iequals(str, "RightY") || str_iequals(str, "RY")) return GamepadAxis::RightY;
    if (str_iequals(str, "LeftTrigger") || str_iequals(str, "LT") || str_iequals(str, "L2")) return GamepadAxis::LeftTrigger;
    if (str_iequals(str, "RightTrigger") || str_iequals(str, "RT") || str_iequals(str, "R2")) return GamepadAxis::RightTrigger;

    return GamepadAxis::Unknown;
}

int gamepad_axis_to_index(GamepadAxis axis) {
    if (axis >= GamepadAxis::Count || axis == GamepadAxis::Unknown) {
        return -1;
    }
    return static_cast<int>(axis);
}

GamepadAxis index_to_gamepad_axis(int index) {
    if (index < 0 || index >= static_cast<int>(GamepadAxis::Count)) {
        return GamepadAxis::Unknown;
    }
    return static_cast<GamepadAxis>(index);
}

//=============================================================================
// Utility Functions - Event Type
//=============================================================================

const char* gamepad_event_type_to_string(GamepadEventType type) {
    switch (type) {
        case GamepadEventType::Connected: return "Connected";
        case GamepadEventType::Disconnected: return "Disconnected";
        case GamepadEventType::ButtonDown: return "ButtonDown";
        case GamepadEventType::ButtonUp: return "ButtonUp";
        case GamepadEventType::AxisMotion: return "AxisMotion";
        default: return "Unknown";
    }
}

} // namespace input
} // namespace window
