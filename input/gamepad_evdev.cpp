/*
 * gamepad_evdev.cpp - Linux evdev gamepad implementation
 *
 * Uses evdev for raw input device access.
 * TODO: Implement full evdev support with libudev enumeration
 */

#if defined(__linux__) && !defined(__ANDROID__)

#include "input_gamepad.hpp"
#include <cstring>

namespace window {
namespace input {

//=============================================================================
// GamepadManager::Impl - evdev Implementation (Stub)
//=============================================================================

struct GamepadManager::Impl {
    GamepadEventDispatcher dispatcher;
    GamepadState gamepads[MAX_GAMEPADS];
    float deadzone;

    Impl() : deadzone(0.1f) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            gamepads[i].reset();
        }
    }

    void update() {
        // TODO: Implement evdev polling
        // Use libudev to enumerate /dev/input/event* devices
        // Use non-blocking read with select() for input events
        // Map evdev event codes to GamepadButton/GamepadAxis
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
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (impl_->gamepads[i].connected) {
            count++;
        }
    }
    return count;
}

bool GamepadManager::is_connected(int index) const {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
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
// Force Feedback / Vibration - evdev Implementation (Stub)
//=============================================================================

bool GamepadManager::get_force_feedback_caps(int index, ForceFeedbackCaps* caps) const {
    if (!caps) return false;
    *caps = ForceFeedbackCaps();

    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    if (!impl_->gamepads[index].connected) {
        return false;
    }

    // TODO: Check for FF_RUMBLE, FF_PERIODIC, etc. using ioctl EVIOCGBIT
    caps->supported = false;
    return true;
}

bool GamepadManager::supports_force_feedback(int index) const {
    // TODO: Query evdev FF capabilities
    (void)index;
    return false;
}

bool GamepadManager::set_vibration(int index, float left_motor, float right_motor) {
    // TODO: Implement using evdev ff_effect with FF_RUMBLE
    // Use ioctl EVIOCSFF to upload effect, then write to event device to play
    (void)index;
    (void)left_motor;
    (void)right_motor;
    return false;
}

bool GamepadManager::set_trigger_vibration(int index, float left_trigger, float right_trigger) {
    // evdev doesn't have standard trigger vibration
    (void)index;
    (void)left_trigger;
    (void)right_trigger;
    return false;
}

bool GamepadManager::stop_vibration(int index) {
    (void)index;
    return false;
}

ForceFeedbackHandle GamepadManager::play_effect(int index, const ForceFeedbackEffect& effect) {
    // TODO: Upload ff_effect structure and start playback
    (void)index;
    (void)effect;
    return INVALID_FF_HANDLE;
}

bool GamepadManager::stop_effect(int index, ForceFeedbackHandle handle) {
    (void)index;
    (void)handle;
    return false;
}

bool GamepadManager::update_effect(int index, ForceFeedbackHandle handle, const ForceFeedbackEffect& effect) {
    (void)index;
    (void)handle;
    (void)effect;
    return false;
}

bool GamepadManager::stop_all_effects(int index) {
    (void)index;
    return false;
}

bool GamepadManager::pause_effects(int index) {
    (void)index;
    return false;
}

bool GamepadManager::resume_effects(int index) {
    (void)index;
    return false;
}

} // namespace input
} // namespace window

#endif // __linux__ && !__ANDROID__
