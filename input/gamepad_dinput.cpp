/*
 * gamepad_dinput.cpp - Windows DirectInput gamepad implementation
 *
 * DirectInput supports a wider variety of controllers than XInput,
 * including generic HID gamepads. This implementation can be used
 * alongside or instead of XInput.
 *
 * Advantages over XInput:
 * - Supports more than 4 controllers
 * - Supports generic HID gamepads (not just Xbox-compatible)
 * - Can detect more buttons and axes
 *
 * Disadvantages:
 * - No built-in vibration support for Xbox controllers
 * - Less standardized button mapping
 */

#if defined(_WIN32) && !defined(WINAPI_FAMILY) && defined(WINDOW_SUPPORT_DINPUT)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// DirectInput version must be defined before including dinput.h
#define DIRECTINPUT_VERSION 0x0800

#include "input_gamepad.hpp"
#include <windows.h>
#include <dinput.h>
#include <cstring>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace window {
namespace input {

//=============================================================================
// DirectInput Device Info
//=============================================================================

struct DInputDevice {
    IDirectInputDevice8* device;
    GUID instance_guid;
    DIJOYSTATE2 state;
    DIJOYSTATE2 prev_state;
    char name[MAX_GAMEPAD_NAME_LENGTH];
    int num_buttons;
    int num_axes;
    bool connected;
    bool acquired;

    DInputDevice() : device(nullptr), num_buttons(0), num_axes(0),
                     connected(false), acquired(false) {
        memset(&instance_guid, 0, sizeof(GUID));
        memset(&state, 0, sizeof(DIJOYSTATE2));
        memset(&prev_state, 0, sizeof(DIJOYSTATE2));
        name[0] = '\0';
    }
};

//=============================================================================
// GamepadManager::Impl - DirectInput Implementation
//=============================================================================

struct GamepadManager::Impl {
    GamepadEventDispatcher dispatcher;
    GamepadState gamepads[MAX_GAMEPADS];
    DInputDevice devices[MAX_GAMEPADS];
    IDirectInput8* dinput;
    HWND hwnd;
    float deadzone;
    int device_count;
    bool needs_enumeration;

    Impl() : dinput(nullptr), hwnd(nullptr), deadzone(0.1f),
             device_count(0), needs_enumeration(true) {
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            gamepads[i].reset();
        }
    }

    ~Impl() {
        shutdown();
    }

    bool initialize() {
        // Create DirectInput interface
        // Use IID_IDirectInput8W for Unicode or IID_IDirectInput8A for ANSI
#ifdef UNICODE
        const IID& iid = IID_IDirectInput8W;
#else
        const IID& iid = IID_IDirectInput8A;
#endif
        HRESULT hr = DirectInput8Create(
            GetModuleHandle(nullptr),
            DIRECTINPUT_VERSION,
            iid,
            (void**)&dinput,
            nullptr
        );

        if (FAILED(hr)) {
            return false;
        }

        // Get a window handle for cooperative level
        // Use the desktop window if no other window is available
        hwnd = GetDesktopWindow();

        return true;
    }

    void shutdown() {
        // Release all devices
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            if (devices[i].device) {
                devices[i].device->Unacquire();
                devices[i].device->Release();
                devices[i].device = nullptr;
            }
            devices[i].connected = false;
            devices[i].acquired = false;
        }

        // Release DirectInput
        if (dinput) {
            dinput->Release();
            dinput = nullptr;
        }

        device_count = 0;
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
        float sign = value > 0 ? 1.0f : -1.0f;
        return sign * (std::abs(value) - deadzone_val) / (1.0f - deadzone_val);
    }

    float normalize_axis(LONG raw) {
        // DirectInput axis range is typically -32768 to 32767 (or 0 to 65535)
        // Normalize to -1.0 to 1.0
        float normalized = static_cast<float>(raw - 32767) / 32767.0f;
        if (normalized < -1.0f) normalized = -1.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        return apply_deadzone(normalized, deadzone);
    }

    float normalize_axis_unsigned(LONG raw) {
        // For axes that are 0 to 65535 (triggers)
        float normalized = static_cast<float>(raw) / 65535.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;
        // Apply smaller deadzone for triggers
        float trigger_deadzone = deadzone * 0.5f;
        if (normalized < trigger_deadzone) {
            return 0.0f;
        }
        return (normalized - trigger_deadzone) / (1.0f - trigger_deadzone);
    }

    // Static callback for device enumeration
    static BOOL CALLBACK enum_devices_callback(const DIDEVICEINSTANCE* instance, void* context) {
        Impl* impl = static_cast<Impl*>(context);
        return impl->on_device_found(instance);
    }

    BOOL on_device_found(const DIDEVICEINSTANCE* instance) {
        if (device_count >= MAX_GAMEPADS) {
            return DIENUM_STOP;
        }

        // Check if this device is already registered
        for (int i = 0; i < device_count; i++) {
            if (memcmp(&devices[i].instance_guid, &instance->guidInstance, sizeof(GUID)) == 0) {
                return DIENUM_CONTINUE; // Already have this device
            }
        }

        // Create device
        IDirectInputDevice8* device = nullptr;
        HRESULT hr = dinput->CreateDevice(instance->guidInstance, &device, nullptr);
        if (FAILED(hr)) {
            return DIENUM_CONTINUE;
        }

        // Set data format to joystick
        hr = device->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Set cooperative level (non-exclusive, background)
        hr = device->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Get device capabilities
        DIDEVCAPS caps;
        caps.dwSize = sizeof(DIDEVCAPS);
        hr = device->GetCapabilities(&caps);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Store device info
        int idx = device_count;
        devices[idx].device = device;
        devices[idx].instance_guid = instance->guidInstance;
        devices[idx].num_buttons = caps.dwButtons;
        devices[idx].num_axes = caps.dwAxes;
        devices[idx].connected = true;
        devices[idx].acquired = false;

        // Copy device name (handle both Unicode and ANSI builds)
#ifdef UNICODE
        WideCharToMultiByte(CP_UTF8, 0, instance->tszProductName, -1,
                           devices[idx].name, MAX_GAMEPAD_NAME_LENGTH, nullptr, nullptr);
#else
        strncpy(devices[idx].name, instance->tszProductName, MAX_GAMEPAD_NAME_LENGTH - 1);
#endif
        devices[idx].name[MAX_GAMEPAD_NAME_LENGTH - 1] = '\0';

        // Copy to gamepad state
        strncpy(gamepads[idx].name, devices[idx].name, MAX_GAMEPAD_NAME_LENGTH - 1);
        gamepads[idx].name[MAX_GAMEPAD_NAME_LENGTH - 1] = '\0';
        gamepads[idx].connected = true;

        device_count++;

        // Dispatch connection event
        double timestamp = get_timestamp();
        GamepadConnectionEvent event;
        event.type = GamepadEventType::Connected;
        event.gamepad_index = idx;
        event.timestamp = timestamp;
        event.name = gamepads[idx].name;
        event.connected = true;
        dispatcher.dispatch_connection(event);

        return DIENUM_CONTINUE;
    }

    void enumerate_devices() {
        if (!dinput) return;

        // Enumerate game controllers
        dinput->EnumDevices(
            DI8DEVCLASS_GAMECTRL,
            enum_devices_callback,
            this,
            DIEDFL_ATTACHEDONLY
        );

        needs_enumeration = false;
    }

    GamepadButton map_button(int button_index) {
        // Map DirectInput button indices to GamepadButton
        // This mapping may vary by controller, but we use a common layout
        switch (button_index) {
            case 0: return GamepadButton::A;           // Usually A/Cross
            case 1: return GamepadButton::B;           // Usually B/Circle
            case 2: return GamepadButton::X;           // Usually X/Square
            case 3: return GamepadButton::Y;           // Usually Y/Triangle
            case 4: return GamepadButton::LeftBumper;  // L1/LB
            case 5: return GamepadButton::RightBumper; // R1/RB
            case 6: return GamepadButton::Back;        // Back/Select
            case 7: return GamepadButton::Start;       // Start
            case 8: return GamepadButton::LeftStick;   // L3
            case 9: return GamepadButton::RightStick;  // R3
            case 10: return GamepadButton::Guide;      // Guide (if available)
            default: return GamepadButton::Unknown;
        }
    }

    void process_pov(int device_idx, DWORD pov, double timestamp) {
        GamepadState& gs = gamepads[device_idx];

        // POV hat is centered at -1 (0xFFFF) or gives angle in hundredths of degrees
        bool up = false, down = false, left = false, right = false;

        if (LOWORD(pov) != 0xFFFF) {
            // POV is pressed, determine direction
            // 0 = up, 9000 = right, 18000 = down, 27000 = left
            if (pov >= 31500 || pov <= 4500) up = true;
            if (pov >= 4500 && pov <= 13500) right = true;
            if (pov >= 13500 && pov <= 22500) down = true;
            if (pov >= 22500 && pov <= 31500) left = true;
        }

        // Check for D-pad changes
        auto check_dpad = [&](GamepadButton btn, bool is_down, int btn_idx) {
            if (is_down != gs.buttons[btn_idx]) {
                gs.buttons[btn_idx] = is_down;

                GamepadButtonEvent event;
                event.type = is_down ? GamepadEventType::ButtonDown : GamepadEventType::ButtonUp;
                event.gamepad_index = device_idx;
                event.timestamp = timestamp;
                event.button = btn;
                dispatcher.dispatch_button(event);
            }
        };

        check_dpad(GamepadButton::DPadUp, up, gamepad_button_to_index(GamepadButton::DPadUp));
        check_dpad(GamepadButton::DPadDown, down, gamepad_button_to_index(GamepadButton::DPadDown));
        check_dpad(GamepadButton::DPadLeft, left, gamepad_button_to_index(GamepadButton::DPadLeft));
        check_dpad(GamepadButton::DPadRight, right, gamepad_button_to_index(GamepadButton::DPadRight));
    }

    void update() {
        if (!dinput) return;

        double timestamp = get_timestamp();

        // Periodically re-enumerate to detect new devices
        // In a real implementation, you might use device notifications instead
        static int enum_counter = 0;
        if (++enum_counter >= 60 || needs_enumeration) { // Every ~1 second at 60Hz
            enum_counter = 0;
            enumerate_devices();
        }

        // Poll each device
        for (int i = 0; i < device_count; i++) {
            DInputDevice& dev = devices[i];
            GamepadState& gs = gamepads[i];

            if (!dev.device || !dev.connected) continue;

            // Try to acquire device if not acquired
            if (!dev.acquired) {
                HRESULT hr = dev.device->Acquire();
                if (FAILED(hr)) {
                    // Device may be disconnected
                    if (hr == DIERR_UNPLUGGED || hr == DIERR_INPUTLOST) {
                        handle_disconnect(i, timestamp);
                    }
                    continue;
                }
                dev.acquired = true;
            }

            // Poll the device
            HRESULT hr = dev.device->Poll();
            if (FAILED(hr)) {
                // Try to reacquire
                hr = dev.device->Acquire();
                if (FAILED(hr)) {
                    if (hr == DIERR_UNPLUGGED || hr == DIERR_INPUTLOST) {
                        handle_disconnect(i, timestamp);
                    }
                    continue;
                }
                hr = dev.device->Poll();
                if (FAILED(hr)) continue;
            }

            // Get device state
            dev.prev_state = dev.state;
            hr = dev.device->GetDeviceState(sizeof(DIJOYSTATE2), &dev.state);
            if (FAILED(hr)) {
                dev.acquired = false;
                continue;
            }

            // Process buttons
            for (int btn = 0; btn < 128 && btn < dev.num_buttons; btn++) {
                bool is_down = (dev.state.rgbButtons[btn] & 0x80) != 0;
                bool was_down = (dev.prev_state.rgbButtons[btn] & 0x80) != 0;

                GamepadButton mapped = map_button(btn);
                if (mapped == GamepadButton::Unknown) continue;

                int btn_idx = gamepad_button_to_index(mapped);
                if (btn_idx < 0) continue;

                if (is_down != was_down) {
                    gs.buttons[btn_idx] = is_down;

                    GamepadButtonEvent event;
                    event.type = is_down ? GamepadEventType::ButtonDown : GamepadEventType::ButtonUp;
                    event.gamepad_index = i;
                    event.timestamp = timestamp;
                    event.button = mapped;
                    dispatcher.dispatch_button(event);
                }
            }

            // Process POV hat (D-pad)
            if (dev.state.rgdwPOV[0] != dev.prev_state.rgdwPOV[0]) {
                process_pov(i, dev.state.rgdwPOV[0], timestamp);
            }

            // Process axes
            struct AxisData {
                GamepadAxis axis;
                LONG current;
                LONG previous;
                bool is_trigger;
            };

            AxisData axes[] = {
                { GamepadAxis::LeftX, dev.state.lX, dev.prev_state.lX, false },
                { GamepadAxis::LeftY, dev.state.lY, dev.prev_state.lY, false },
                { GamepadAxis::RightX, dev.state.lZ, dev.prev_state.lZ, false },      // Often Z axis
                { GamepadAxis::RightY, dev.state.lRz, dev.prev_state.lRz, false },    // Often Rz axis
                { GamepadAxis::LeftTrigger, dev.state.lRx, dev.prev_state.lRx, true }, // Rx for trigger
                { GamepadAxis::RightTrigger, dev.state.lRy, dev.prev_state.lRy, true }, // Ry for trigger
            };

            for (const auto& axis_data : axes) {
                int axis_idx = gamepad_axis_to_index(axis_data.axis);
                if (axis_idx < 0) continue;

                float new_value = axis_data.is_trigger
                    ? normalize_axis_unsigned(axis_data.current)
                    : normalize_axis(axis_data.current);
                float old_value = gs.axes[axis_idx];

                // For Y axes, invert to match expected convention (up = negative)
                if (axis_data.axis == GamepadAxis::LeftY || axis_data.axis == GamepadAxis::RightY) {
                    new_value = -new_value;
                }

                if (std::abs(new_value - old_value) > 0.001f) {
                    gs.axes[axis_idx] = new_value;

                    GamepadAxisEvent event;
                    event.type = GamepadEventType::AxisMotion;
                    event.gamepad_index = i;
                    event.timestamp = timestamp;
                    event.axis = axis_data.axis;
                    event.value = new_value;
                    event.delta = new_value - old_value;
                    dispatcher.dispatch_axis(event);
                }
            }
        }
    }

    void handle_disconnect(int device_idx, double timestamp) {
        DInputDevice& dev = devices[device_idx];
        GamepadState& gs = gamepads[device_idx];

        if (!dev.connected) return;

        dev.connected = false;
        dev.acquired = false;
        gs.connected = false;

        // Dispatch disconnect event
        GamepadConnectionEvent event;
        event.type = GamepadEventType::Disconnected;
        event.gamepad_index = device_idx;
        event.timestamp = timestamp;
        event.name = nullptr;
        event.connected = false;
        dispatcher.dispatch_connection(event);

        // Reset state
        gs.reset();

        // Release device
        if (dev.device) {
            dev.device->Unacquire();
            dev.device->Release();
            dev.device = nullptr;
        }

        // Request re-enumeration
        needs_enumeration = true;
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

    if (!mgr->impl_->initialize()) {
        delete mgr;
        return nullptr;
    }

    // Initial enumeration
    mgr->impl_->enumerate_devices();

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
    for (int i = 0; i < impl_->device_count; i++) {
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

#endif // _WIN32 && !WINAPI_FAMILY && WINDOW_SUPPORT_DINPUT
