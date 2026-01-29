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

} // namespace input
} // namespace window

#endif // __linux__ && !__ANDROID__
