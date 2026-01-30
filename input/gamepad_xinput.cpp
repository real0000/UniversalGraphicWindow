/*
 * gamepad_xinput.cpp - Windows XInput gamepad implementation
 *
 * XInput supports up to 4 controllers (index 0-3).
 * This implementation polls all controllers on each update() call.
 */

#if defined(_WIN32) && !defined(WINAPI_FAMILY)
// Win32 desktop only (not UWP)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "input_gamepad.hpp"
#include <windows.h>
#include <xinput.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// XInput supports max 4 controllers
#define XINPUT_MAX_CONTROLLERS 4

namespace window {
namespace input {

//=============================================================================
// GamepadManager::Impl - XInput Implementation
//=============================================================================

struct GamepadManager::Impl {
    GamepadEventDispatcher dispatcher;
    GamepadState gamepads[MAX_GAMEPADS];
    DWORD packet_numbers[XINPUT_MAX_CONTROLLERS];
    float deadzone;

    Impl() : deadzone(0.1f) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            gamepads[i].reset();
        }
        for (int i = 0; i < XINPUT_MAX_CONTROLLERS; i++) {
            packet_numbers[i] = 0;
        }
    }

    double get_timestamp() {
        static LARGE_INTEGER frequency = {};
        static bool init = false;
        if (!init) {
            QueryPerformanceFrequency(&frequency);
            init = true;
        }
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    }

    float apply_deadzone(float value, float deadzone_val) {
        if (std::abs(value) < deadzone_val) {
            return 0.0f;
        }
        // Rescale value outside deadzone to 0-1 range
        float sign = value > 0 ? 1.0f : -1.0f;
        return sign * (std::abs(value) - deadzone_val) / (1.0f - deadzone_val);
    }

    float normalize_stick(SHORT raw) {
        // XInput stick range is -32768 to 32767
        float normalized = static_cast<float>(raw) / 32767.0f;
        // Clamp to -1.0 to 1.0
        if (normalized < -1.0f) normalized = -1.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        return apply_deadzone(normalized, deadzone);
    }

    float normalize_trigger(BYTE raw) {
        // XInput trigger range is 0 to 255
        float normalized = static_cast<float>(raw) / 255.0f;
        // Apply trigger deadzone (smaller than stick deadzone)
        float trigger_deadzone = deadzone * 0.5f;
        if (normalized < trigger_deadzone) {
            return 0.0f;
        }
        return (normalized - trigger_deadzone) / (1.0f - trigger_deadzone);
    }

    void update() {
        double timestamp = get_timestamp();

        for (DWORD i = 0; i < XINPUT_MAX_CONTROLLERS; i++) {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(XINPUT_STATE));
            DWORD result = XInputGetState(i, &state);

            bool was_connected = gamepads[i].connected;
            bool is_connected = (result == ERROR_SUCCESS);

            // Handle connection changes
            if (is_connected != was_connected) {
                gamepads[i].connected = is_connected;

                GamepadConnectionEvent event;
                event.type = is_connected ? GamepadEventType::Connected : GamepadEventType::Disconnected;
                event.gamepad_index = static_cast<int>(i);
                event.timestamp = timestamp;
                event.connected = is_connected;

                if (is_connected) {
                    // XInput doesn't provide device names, use generic name
                    snprintf(gamepads[i].name, MAX_GAMEPAD_NAME_LENGTH, "XInput Controller %d", i + 1);
                    event.name = gamepads[i].name;
                } else {
                    gamepads[i].reset();
                    event.name = nullptr;
                }

                dispatcher.dispatch_connection(event);
            }

            if (!is_connected) {
                continue;
            }

            // Skip if state hasn't changed (optimization)
            if (state.dwPacketNumber == packet_numbers[i]) {
                continue;
            }
            packet_numbers[i] = state.dwPacketNumber;

            XINPUT_GAMEPAD& pad = state.Gamepad;
            GamepadState& gs = gamepads[i];

            // Process buttons
            struct ButtonMapping {
                WORD xinput_mask;
                GamepadButton button;
            };

            static const ButtonMapping button_map[] = {
                { XINPUT_GAMEPAD_A, GamepadButton::A },
                { XINPUT_GAMEPAD_B, GamepadButton::B },
                { XINPUT_GAMEPAD_X, GamepadButton::X },
                { XINPUT_GAMEPAD_Y, GamepadButton::Y },
                { XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftBumper },
                { XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightBumper },
                { XINPUT_GAMEPAD_BACK, GamepadButton::Back },
                { XINPUT_GAMEPAD_START, GamepadButton::Start },
                { XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftStick },
                { XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightStick },
                { XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp },
                { XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DPadDown },
                { XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DPadLeft },
                { XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DPadRight },
            };

            for (const auto& mapping : button_map) {
                bool is_down = (pad.wButtons & mapping.xinput_mask) != 0;
                int btn_idx = gamepad_button_to_index(mapping.button);

                if (btn_idx >= 0 && is_down != gs.buttons[btn_idx]) {
                    gs.buttons[btn_idx] = is_down;

                    GamepadButtonEvent event;
                    event.type = is_down ? GamepadEventType::ButtonDown : GamepadEventType::ButtonUp;
                    event.gamepad_index = static_cast<int>(i);
                    event.timestamp = timestamp;
                    event.button = mapping.button;
                    dispatcher.dispatch_button(event);
                }
            }

            // Note: Guide button requires XINPUT_GAMEPAD_GUIDE which needs XInputGetStateEx
            // and may not be available on all systems

            // Process axes
            struct AxisMapping {
                GamepadAxis axis;
                float new_value;
            };

            AxisMapping axis_values[] = {
                { GamepadAxis::LeftX, normalize_stick(pad.sThumbLX) },
                { GamepadAxis::LeftY, normalize_stick(static_cast<SHORT>(-pad.sThumbLY)) }, // Invert Y
                { GamepadAxis::RightX, normalize_stick(pad.sThumbRX) },
                { GamepadAxis::RightY, normalize_stick(static_cast<SHORT>(-pad.sThumbRY)) }, // Invert Y
                { GamepadAxis::LeftTrigger, normalize_trigger(pad.bLeftTrigger) },
                { GamepadAxis::RightTrigger, normalize_trigger(pad.bRightTrigger) },
            };

            for (const auto& axis_val : axis_values) {
                int axis_idx = gamepad_axis_to_index(axis_val.axis);
                if (axis_idx < 0) continue;

                float old_value = gs.axes[axis_idx];
                float new_value = axis_val.new_value;

                // Only dispatch if value changed significantly
                if (std::abs(new_value - old_value) > 0.001f) {
                    gs.axes[axis_idx] = new_value;

                    GamepadAxisEvent event;
                    event.type = GamepadEventType::AxisMotion;
                    event.gamepad_index = static_cast<int>(i);
                    event.timestamp = timestamp;
                    event.axis = axis_val.axis;
                    event.value = new_value;
                    event.delta = new_value - old_value;
                    dispatcher.dispatch_axis(event);
                }
            }
        }
    }
};

//=============================================================================
// GamepadManager
//=============================================================================

GamepadManager::GamepadManager() : impl_(nullptr) {}

GamepadManager::~GamepadManager() {
    delete impl_;
}

GamepadManager* GamepadManager::create() {
    GamepadManager* mgr = new GamepadManager();
    mgr->impl_ = new GamepadManager::Impl();
    return mgr;
}

void GamepadManager::destroy() {
    delete this;
}

void GamepadManager::update() {
    if (impl_) {
        impl_->update();
    }
}

bool GamepadManager::add_handler(IGamepadHandler* handler) {
    if (impl_) {
        return impl_->dispatcher.add_handler(handler);
    }
    return false;
}

bool GamepadManager::remove_handler(IGamepadHandler* handler) {
    if (impl_) {
        return impl_->dispatcher.remove_handler(handler);
    }
    return false;
}

bool GamepadManager::remove_handler(const char* handler_id) {
    if (impl_) {
        return impl_->dispatcher.remove_handler(handler_id);
    }
    return false;
}

GamepadEventDispatcher* GamepadManager::get_dispatcher() {
    if (impl_) {
        return &impl_->dispatcher;
    }
    return nullptr;
}

int GamepadManager::get_gamepad_count() const {
    if (!impl_) return 0;
    int count = 0;
    for (int i = 0; i < XINPUT_MAX_CONTROLLERS; i++) {
        if (impl_->gamepads[i].connected) {
            count++;
        }
    }
    return count;
}

bool GamepadManager::is_connected(int index) const {
    if (!impl_ || index < 0 || index >= XINPUT_MAX_CONTROLLERS) {
        return false;
    }
    return impl_->gamepads[index].connected;
}

const GamepadState* GamepadManager::get_state(int index) const {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return nullptr;
    }
    return &impl_->gamepads[index];
}

bool GamepadManager::is_button_down(int index, GamepadButton button) const {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    int btn_idx = gamepad_button_to_index(button);
    if (btn_idx < 0 || btn_idx >= MAX_GAMEPAD_BUTTONS) {
        return false;
    }
    return impl_->gamepads[index].buttons[btn_idx];
}

float GamepadManager::get_axis(int index, GamepadAxis axis) const {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return 0.0f;
    }
    int axis_idx = gamepad_axis_to_index(axis);
    if (axis_idx < 0 || axis_idx >= MAX_GAMEPAD_AXES) {
        return 0.0f;
    }
    return impl_->gamepads[index].axes[axis_idx];
}

void GamepadManager::set_deadzone(float deadzone_val) {
    if (impl_) {
        impl_->deadzone = deadzone_val;
        if (impl_->deadzone < 0.0f) impl_->deadzone = 0.0f;
        if (impl_->deadzone > 0.9f) impl_->deadzone = 0.9f;
    }
}

float GamepadManager::get_deadzone() const {
    if (impl_) {
        return impl_->deadzone;
    }
    return 0.1f;
}

//=============================================================================
// Force Feedback / Vibration - XInput Implementation
//=============================================================================

bool GamepadManager::get_force_feedback_caps(int index, ForceFeedbackCaps* caps) const {
    if (!caps) return false;

    // Initialize to defaults
    *caps = ForceFeedbackCaps();

    if (!impl_ || index < 0 || index >= XINPUT_MAX_CONTROLLERS) {
        return false;
    }

    if (!impl_->gamepads[index].connected) {
        return false;
    }

    // XInput always supports basic rumble
    caps->supported = true;
    caps->has_rumble = true;
    caps->has_left_motor = true;
    caps->has_right_motor = true;
    caps->has_trigger_rumble = false;  // Only Xbox One+ via Windows.Gaming.Input
    caps->has_advanced_effects = false; // XInput only supports simple rumble
    caps->supported_effects = (1 << static_cast<int>(ForceFeedbackType::Rumble));
    caps->max_simultaneous_effects = 1;

    return true;
}

bool GamepadManager::supports_force_feedback(int index) const {
    if (!impl_ || index < 0 || index >= XINPUT_MAX_CONTROLLERS) {
        return false;
    }
    // XInput controllers always support vibration
    return impl_->gamepads[index].connected;
}

bool GamepadManager::set_vibration(int index, float left_motor, float right_motor) {
    if (!impl_ || index < 0 || index >= XINPUT_MAX_CONTROLLERS) {
        return false;
    }

    if (!impl_->gamepads[index].connected) {
        return false;
    }

    // Clamp values to 0.0-1.0
    if (left_motor < 0.0f) left_motor = 0.0f;
    if (left_motor > 1.0f) left_motor = 1.0f;
    if (right_motor < 0.0f) right_motor = 0.0f;
    if (right_motor > 1.0f) right_motor = 1.0f;

    // Convert to XInput range (0-65535)
    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = static_cast<WORD>(left_motor * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(right_motor * 65535.0f);

    DWORD result = XInputSetState(static_cast<DWORD>(index), &vibration);
    return (result == ERROR_SUCCESS);
}

bool GamepadManager::set_trigger_vibration(int index, float left_trigger, float right_trigger) {
    // XInput does not support trigger rumble
    // This would require Windows.Gaming.Input (Xbox One controllers)
    (void)index;
    (void)left_trigger;
    (void)right_trigger;
    return false;
}

bool GamepadManager::stop_vibration(int index) {
    return set_vibration(index, 0.0f, 0.0f);
}

ForceFeedbackHandle GamepadManager::play_effect(int index, const ForceFeedbackEffect& effect) {
    if (!impl_ || index < 0 || index >= XINPUT_MAX_CONTROLLERS) {
        return INVALID_FF_HANDLE;
    }

    if (!impl_->gamepads[index].connected) {
        return INVALID_FF_HANDLE;
    }

    // XInput only supports Rumble type
    if (effect.type != ForceFeedbackType::Rumble) {
        return INVALID_FF_HANDLE;
    }

    // Apply the effect
    float left = effect.left_motor * effect.gain;
    float right = effect.right_motor * effect.gain;

    if (set_vibration(index, left, right)) {
        // XInput doesn't have effect handles, return a simple indicator
        // In a more complete implementation, we'd track timed effects
        return 0;
    }

    return INVALID_FF_HANDLE;
}

bool GamepadManager::stop_effect(int index, ForceFeedbackHandle handle) {
    (void)handle; // XInput doesn't use handles
    return stop_vibration(index);
}

bool GamepadManager::update_effect(int index, ForceFeedbackHandle handle, const ForceFeedbackEffect& effect) {
    (void)handle; // XInput doesn't use handles

    if (effect.type != ForceFeedbackType::Rumble) {
        return false;
    }

    float left = effect.left_motor * effect.gain;
    float right = effect.right_motor * effect.gain;

    return set_vibration(index, left, right);
}

bool GamepadManager::stop_all_effects(int index) {
    return stop_vibration(index);
}

bool GamepadManager::pause_effects(int index) {
    // XInput doesn't support pause, just stop
    return stop_vibration(index);
}

bool GamepadManager::resume_effects(int index) {
    // XInput doesn't support resume
    (void)index;
    return false;
}

} // namespace input
} // namespace window

#endif // _WIN32 && !WINAPI_FAMILY
