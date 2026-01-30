/*
 * input_wheel.cpp - Platform-independent steering wheel utilities
 */

#include "input_wheel.hpp"
#include <cstring>
#include <cctype>
#include <cstdio>

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
// WheelEventDispatcher
//=============================================================================

WheelEventDispatcher::WheelEventDispatcher()
    : handler_count_(0)
    , needs_sort_(false)
{
    for (int i = 0; i < MAX_WHEEL_HANDLERS; i++) {
        handlers_[i] = nullptr;
    }
}

WheelEventDispatcher::~WheelEventDispatcher() {
}

bool WheelEventDispatcher::add_handler(IWheelHandler* handler) {
    if (!handler) return false;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            return false;
        }
    }

    if (handler_count_ >= MAX_WHEEL_HANDLERS) {
        return false;
    }

    handlers_[handler_count_++] = handler;
    needs_sort_ = true;
    return true;
}

bool WheelEventDispatcher::remove_handler(IWheelHandler* handler) {
    if (!handler) return false;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            for (int j = i; j < handler_count_ - 1; j++) {
                handlers_[j] = handlers_[j + 1];
            }
            handlers_[--handler_count_] = nullptr;
            return true;
        }
    }
    return false;
}

bool WheelEventDispatcher::remove_handler(const char* handler_id) {
    if (!handler_id) return false;

    IWheelHandler* handler = find_handler(handler_id);
    if (handler) {
        return remove_handler(handler);
    }
    return false;
}

int WheelEventDispatcher::get_handler_count() const {
    return handler_count_;
}

IWheelHandler* WheelEventDispatcher::get_handler(int index) const {
    if (index < 0 || index >= handler_count_) {
        return nullptr;
    }
    return handlers_[index];
}

IWheelHandler* WheelEventDispatcher::find_handler(const char* handler_id) const {
    if (!handler_id) return nullptr;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && strcmp(handlers_[i]->get_handler_id(), handler_id) == 0) {
            return handlers_[i];
        }
    }
    return nullptr;
}

void WheelEventDispatcher::sort_handlers() {
    if (!needs_sort_ || handler_count_ <= 1) {
        needs_sort_ = false;
        return;
    }

    for (int i = 1; i < handler_count_; i++) {
        IWheelHandler* key = handlers_[i];
        int key_priority = key->get_priority();
        int j = i - 1;

        while (j >= 0 && handlers_[j]->get_priority() < key_priority) {
            handlers_[j + 1] = handlers_[j];
            j--;
        }
        handlers_[j + 1] = key;
    }

    needs_sort_ = false;
}

bool WheelEventDispatcher::dispatch_button(const WheelButtonEvent& event) {
    if (needs_sort_) sort_handlers();

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_button(event)) {
            return true;
        }
    }
    return false;
}

bool WheelEventDispatcher::dispatch_axis(const WheelAxisEvent& event) {
    if (needs_sort_) sort_handlers();

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_axis(event)) {
            return true;
        }
    }
    return false;
}

bool WheelEventDispatcher::dispatch_gear(const WheelGearEvent& event) {
    if (needs_sort_) sort_handlers();

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_gear(event)) {
            return true;
        }
    }
    return false;
}

void WheelEventDispatcher::dispatch_connection(const WheelConnectionEvent& event) {
    if (needs_sort_) sort_handlers();

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i]) {
            handlers_[i]->on_connection(event);
        }
    }
}

//=============================================================================
// Utility Functions - Axis
//=============================================================================

const char* wheel_axis_to_string(WheelAxis axis) {
    switch (axis) {
        case WheelAxis::Steering: return "Steering";
        case WheelAxis::Throttle: return "Throttle";
        case WheelAxis::Brake: return "Brake";
        case WheelAxis::Clutch: return "Clutch";
        case WheelAxis::Handbrake: return "Handbrake";
        default: return "Unknown";
    }
}

WheelAxis string_to_wheel_axis(const char* str) {
    if (!str || !*str) return WheelAxis::Unknown;

    if (str_iequals(str, "Steering") || str_iequals(str, "Wheel")) return WheelAxis::Steering;
    if (str_iequals(str, "Throttle") || str_iequals(str, "Gas") || str_iequals(str, "Accelerator")) return WheelAxis::Throttle;
    if (str_iequals(str, "Brake")) return WheelAxis::Brake;
    if (str_iequals(str, "Clutch")) return WheelAxis::Clutch;
    if (str_iequals(str, "Handbrake") || str_iequals(str, "EBrake")) return WheelAxis::Handbrake;

    return WheelAxis::Unknown;
}

//=============================================================================
// Utility Functions - Button
//=============================================================================

const char* wheel_button_to_string(WheelButton button) {
    switch (button) {
        case WheelButton::PaddleShiftUp: return "PaddleShiftUp";
        case WheelButton::PaddleShiftDown: return "PaddleShiftDown";
        case WheelButton::DPadUp: return "DPadUp";
        case WheelButton::DPadDown: return "DPadDown";
        case WheelButton::DPadLeft: return "DPadLeft";
        case WheelButton::DPadRight: return "DPadRight";
        case WheelButton::Button1: return "Button1";
        case WheelButton::Button2: return "Button2";
        case WheelButton::Button3: return "Button3";
        case WheelButton::Button4: return "Button4";
        case WheelButton::Button5: return "Button5";
        case WheelButton::Button6: return "Button6";
        case WheelButton::Button7: return "Button7";
        case WheelButton::Button8: return "Button8";
        case WheelButton::Button9: return "Button9";
        case WheelButton::Button10: return "Button10";
        case WheelButton::Button11: return "Button11";
        case WheelButton::Button12: return "Button12";
        case WheelButton::Button13: return "Button13";
        case WheelButton::Button14: return "Button14";
        case WheelButton::Button15: return "Button15";
        case WheelButton::Button16: return "Button16";
        case WheelButton::Start: return "Start";
        case WheelButton::Back: return "Back";
        case WheelButton::Xbox: return "Xbox";
        default: return "Unknown";
    }
}

WheelButton string_to_wheel_button(const char* str) {
    if (!str || !*str) return WheelButton::Unknown;

    if (str_iequals(str, "PaddleShiftUp") || str_iequals(str, "ShiftUp")) return WheelButton::PaddleShiftUp;
    if (str_iequals(str, "PaddleShiftDown") || str_iequals(str, "ShiftDown")) return WheelButton::PaddleShiftDown;
    if (str_iequals(str, "DPadUp")) return WheelButton::DPadUp;
    if (str_iequals(str, "DPadDown")) return WheelButton::DPadDown;
    if (str_iequals(str, "DPadLeft")) return WheelButton::DPadLeft;
    if (str_iequals(str, "DPadRight")) return WheelButton::DPadRight;
    if (str_iequals(str, "Start")) return WheelButton::Start;
    if (str_iequals(str, "Back")) return WheelButton::Back;
    if (str_iequals(str, "Xbox") || str_iequals(str, "Guide")) return WheelButton::Xbox;

    // Generic buttons
    for (int i = 1; i <= 16; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Button%d", i);
        if (str_iequals(str, buf)) {
            return static_cast<WheelButton>(static_cast<int>(WheelButton::Button1) + i - 1);
        }
    }

    return WheelButton::Unknown;
}

//=============================================================================
// Utility Functions - Gear
//=============================================================================

const char* gear_position_to_string(GearPosition gear) {
    switch (gear) {
        case GearPosition::Reverse: return "Reverse";
        case GearPosition::Neutral: return "Neutral";
        case GearPosition::Gear1: return "1st";
        case GearPosition::Gear2: return "2nd";
        case GearPosition::Gear3: return "3rd";
        case GearPosition::Gear4: return "4th";
        case GearPosition::Gear5: return "5th";
        case GearPosition::Gear6: return "6th";
        case GearPosition::Gear7: return "7th";
        default: return "Unknown";
    }
}

//=============================================================================
// Utility Functions - Force Feedback Type
//=============================================================================

const char* wheel_ff_type_to_string(WheelFFType type) {
    switch (type) {
        case WheelFFType::None: return "None";
        case WheelFFType::ConstantForce: return "ConstantForce";
        case WheelFFType::SpringForce: return "SpringForce";
        case WheelFFType::DamperForce: return "DamperForce";
        case WheelFFType::FrictionForce: return "FrictionForce";
        case WheelFFType::InertiaForce: return "InertiaForce";
        case WheelFFType::SineWave: return "SineWave";
        case WheelFFType::SquareWave: return "SquareWave";
        case WheelFFType::TriangleWave: return "TriangleWave";
        case WheelFFType::SawtoothWave: return "SawtoothWave";
        case WheelFFType::RoadRumble: return "RoadRumble";
        case WheelFFType::Collision: return "Collision";
        case WheelFFType::SlipperyRoad: return "SlipperyRoad";
        case WheelFFType::DirtRoad: return "DirtRoad";
        case WheelFFType::Kerb: return "Kerb";
        default: return "Unknown";
    }
}

//=============================================================================
// Utility Functions - Event Type
//=============================================================================

const char* wheel_event_type_to_string(WheelEventType type) {
    switch (type) {
        case WheelEventType::Connected: return "Connected";
        case WheelEventType::Disconnected: return "Disconnected";
        case WheelEventType::ButtonDown: return "ButtonDown";
        case WheelEventType::ButtonUp: return "ButtonUp";
        case WheelEventType::AxisChanged: return "AxisChanged";
        case WheelEventType::GearChanged: return "GearChanged";
        default: return "Unknown";
    }
}

} // namespace input
} // namespace window
