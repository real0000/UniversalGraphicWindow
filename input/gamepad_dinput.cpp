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

// Force feedback effect tracking
struct FFEffect {
    IDirectInputEffect* effect;
    ForceFeedbackType type;
    bool active;

    FFEffect() : effect(nullptr), type(ForceFeedbackType::None), active(false) {}
};

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

    // Force feedback
    bool ff_supported;
    uint32_t ff_supported_effects;
    FFEffect ff_effects[MAX_FORCE_FEEDBACK_EFFECTS];
    int ff_effect_count;

    DInputDevice() : device(nullptr), num_buttons(0), num_axes(0),
                     connected(false), acquired(false),
                     ff_supported(false), ff_supported_effects(0), ff_effect_count(0) {
        memset(&instance_guid, 0, sizeof(GUID));
        memset(&state, 0, sizeof(DIJOYSTATE2));
        memset(&prev_state, 0, sizeof(DIJOYSTATE2));
        name[0] = '\0';
    }

    void release_effects() {
        for (int i = 0; i < MAX_FORCE_FEEDBACK_EFFECTS; i++) {
            if (ff_effects[i].effect) {
                ff_effects[i].effect->Stop();
                ff_effects[i].effect->Release();
                ff_effects[i].effect = nullptr;
            }
            ff_effects[i].active = false;
        }
        ff_effect_count = 0;
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
                devices[i].release_effects();
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

    // Callback for enumerating supported force feedback effects
    static BOOL CALLBACK enum_effects_callback(const DIEFFECTINFO* info, void* context) {
        DInputDevice* dev = static_cast<DInputDevice*>(context);

        // Map DirectInput effect GUIDs to our types
        if (info->guid == GUID_ConstantForce) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Constant));
        } else if (info->guid == GUID_RampForce) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Ramp));
        } else if (info->guid == GUID_Square || info->guid == GUID_Sine ||
                   info->guid == GUID_Triangle || info->guid == GUID_SawtoothUp ||
                   info->guid == GUID_SawtoothDown) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Periodic));
        } else if (info->guid == GUID_Spring) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Spring));
        } else if (info->guid == GUID_Damper) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Damper));
        } else if (info->guid == GUID_Inertia) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Inertia));
        } else if (info->guid == GUID_Friction) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Friction));
        } else if (info->guid == GUID_CustomForce) {
            dev->ff_supported_effects |= (1 << static_cast<int>(ForceFeedbackType::Custom));
        }

        return DIENUM_CONTINUE;
    }

    void setup_force_feedback(int device_idx) {
        DInputDevice& dev = devices[device_idx];

        if (!dev.device) return;

        // Get device capabilities
        DIDEVCAPS caps;
        caps.dwSize = sizeof(DIDEVCAPS);
        HRESULT hr = dev.device->GetCapabilities(&caps);
        if (FAILED(hr)) return;

        // Check if device supports force feedback
        if (!(caps.dwFlags & DIDC_FORCEFEEDBACK)) {
            dev.ff_supported = false;
            return;
        }

        dev.ff_supported = true;

        // Disable auto-center spring for better force feedback control
        DIPROPDWORD dipdw;
        dipdw.diph.dwSize = sizeof(DIPROPDWORD);
        dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dipdw.diph.dwObj = 0;
        dipdw.diph.dwHow = DIPH_DEVICE;
        dipdw.dwData = DIPROPAUTOCENTER_OFF;
        dev.device->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

        // Enumerate supported effects
        dev.ff_supported_effects = (1 << static_cast<int>(ForceFeedbackType::Rumble)); // All FF devices support rumble
        dev.device->EnumEffects(enum_effects_callback, &dev, DIEFT_ALL);
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

        // Setup force feedback after device is fully initialized
        setup_force_feedback(idx);

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

        // Release effects and device
        if (dev.device) {
            dev.release_effects();
            dev.device->Unacquire();
            dev.device->Release();
            dev.device = nullptr;
        }
        dev.ff_supported = false;
        dev.ff_supported_effects = 0;

        // Request re-enumeration
        needs_enumeration = true;
    }

    //=========================================================================
    // Force Feedback Helper Methods
    //=========================================================================

    ForceFeedbackHandle create_constant_effect(int device_idx, const ForceFeedbackEffect& effect) {
        DInputDevice& dev = devices[device_idx];
        if (!dev.ff_supported || !dev.device) return INVALID_FF_HANDLE;

        // Find free slot
        int slot = -1;
        for (int i = 0; i < MAX_FORCE_FEEDBACK_EFFECTS; i++) {
            if (!dev.ff_effects[i].effect) {
                slot = i;
                break;
            }
        }
        if (slot < 0) return INVALID_FF_HANDLE;

        // Convert direction from degrees to DirectInput format
        LONG direction = static_cast<LONG>(effect.direction * 100); // Hundredths of degrees

        // Setup constant force effect
        DICONSTANTFORCE cf;
        cf.lMagnitude = static_cast<LONG>(effect.magnitude * effect.gain * 10000.0f);

        DIENVELOPE envelope;
        envelope.dwSize = sizeof(DIENVELOPE);
        envelope.dwAttackLevel = static_cast<DWORD>(effect.attack_level * 10000.0f);
        envelope.dwAttackTime = effect.attack_time_ms * 1000;
        envelope.dwFadeLevel = static_cast<DWORD>(effect.fade_level * 10000.0f);
        envelope.dwFadeTime = effect.fade_time_ms * 1000;

        DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
        LONG directions[2] = { direction, 0 };

        DIEFFECT eff;
        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = effect.duration_ms > 0 ? effect.duration_ms * 1000 : INFINITE;
        eff.dwStartDelay = effect.start_delay_ms * 1000;
        eff.dwGain = static_cast<DWORD>(effect.gain * 10000.0f);
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 2;
        eff.rgdwAxes = axes;
        eff.rglDirection = directions;
        eff.lpEnvelope = (effect.attack_time_ms > 0 || effect.fade_time_ms > 0) ? &envelope : nullptr;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = dev.device->CreateEffect(GUID_ConstantForce, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_FF_HANDLE;

        dev.ff_effects[slot].effect = diEffect;
        dev.ff_effects[slot].type = ForceFeedbackType::Constant;
        dev.ff_effects[slot].active = false;
        dev.ff_effect_count++;

        return slot;
    }

    ForceFeedbackHandle create_periodic_effect(int device_idx, const ForceFeedbackEffect& effect) {
        DInputDevice& dev = devices[device_idx];
        if (!dev.ff_supported || !dev.device) return INVALID_FF_HANDLE;

        int slot = -1;
        for (int i = 0; i < MAX_FORCE_FEEDBACK_EFFECTS; i++) {
            if (!dev.ff_effects[i].effect) {
                slot = i;
                break;
            }
        }
        if (slot < 0) return INVALID_FF_HANDLE;

        // Map waveform to DirectInput GUID
        GUID effectGuid;
        switch (effect.waveform) {
            case ForceFeedbackWaveform::Square: effectGuid = GUID_Square; break;
            case ForceFeedbackWaveform::Triangle: effectGuid = GUID_Triangle; break;
            case ForceFeedbackWaveform::SawtoothUp: effectGuid = GUID_SawtoothUp; break;
            case ForceFeedbackWaveform::SawtoothDown: effectGuid = GUID_SawtoothDown; break;
            default: effectGuid = GUID_Sine; break;
        }

        DIPERIODIC periodic;
        periodic.dwMagnitude = static_cast<DWORD>(effect.magnitude * effect.gain * 10000.0f);
        periodic.lOffset = static_cast<LONG>(effect.offset * 10000.0f);
        periodic.dwPhase = static_cast<DWORD>(effect.phase * 36000.0f); // Hundredths of degrees
        periodic.dwPeriod = static_cast<DWORD>(effect.period_ms * 1000.0f);

        DWORD axes[2] = { DIJOFS_X, DIJOFS_Y };
        LONG directions[2] = { static_cast<LONG>(effect.direction * 100), 0 };

        DIEFFECT eff;
        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = effect.duration_ms > 0 ? effect.duration_ms * 1000 : INFINITE;
        eff.dwStartDelay = effect.start_delay_ms * 1000;
        eff.dwGain = static_cast<DWORD>(effect.gain * 10000.0f);
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 2;
        eff.rgdwAxes = axes;
        eff.rglDirection = directions;
        eff.lpEnvelope = nullptr;
        eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
        eff.lpvTypeSpecificParams = &periodic;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = dev.device->CreateEffect(effectGuid, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_FF_HANDLE;

        dev.ff_effects[slot].effect = diEffect;
        dev.ff_effects[slot].type = ForceFeedbackType::Periodic;
        dev.ff_effects[slot].active = false;
        dev.ff_effect_count++;

        return slot;
    }

    bool start_effect(int device_idx, int slot) {
        DInputDevice& dev = devices[device_idx];
        if (slot < 0 || slot >= MAX_FORCE_FEEDBACK_EFFECTS) return false;
        if (!dev.ff_effects[slot].effect) return false;

        HRESULT hr = dev.ff_effects[slot].effect->Start(1, 0);
        if (SUCCEEDED(hr)) {
            dev.ff_effects[slot].active = true;
            return true;
        }
        return false;
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

//=============================================================================
// Force Feedback / Vibration - DirectInput Implementation
//=============================================================================

bool GamepadManager::get_force_feedback_caps(int index, ForceFeedbackCaps* caps) const {
    if (!caps) return false;

    *caps = ForceFeedbackCaps();

    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    const DInputDevice& dev = impl_->devices[index];
    if (!dev.connected) {
        return false;
    }

    caps->supported = dev.ff_supported;
    if (!dev.ff_supported) {
        return true;
    }

    caps->has_rumble = true; // All FF devices support rumble-like effects
    caps->has_left_motor = true;
    caps->has_right_motor = true;
    caps->has_trigger_rumble = false;
    caps->has_advanced_effects = (dev.ff_supported_effects != 0);
    caps->supported_effects = dev.ff_supported_effects;
    caps->max_simultaneous_effects = MAX_FORCE_FEEDBACK_EFFECTS;

    return true;
}

bool GamepadManager::supports_force_feedback(int index) const {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }
    return impl_->devices[index].ff_supported;
}

bool GamepadManager::set_vibration(int index, float left_motor, float right_motor) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (!dev.connected || !dev.ff_supported || !dev.device) {
        return false;
    }

    // Clamp values
    if (left_motor < 0.0f) left_motor = 0.0f;
    if (left_motor > 1.0f) left_motor = 1.0f;
    if (right_motor < 0.0f) right_motor = 0.0f;
    if (right_motor > 1.0f) right_motor = 1.0f;

    // Stop any existing rumble effect in slot 0 (reserved for simple vibration)
    if (dev.ff_effects[0].effect) {
        dev.ff_effects[0].effect->Stop();
        dev.ff_effects[0].effect->Release();
        dev.ff_effects[0].effect = nullptr;
        dev.ff_effects[0].active = false;
    }

    // If both motors are 0, just stop
    if (left_motor < 0.001f && right_motor < 0.001f) {
        return true;
    }

    // Create a periodic effect to simulate rumble
    // Left motor = low frequency (slower), right motor = high frequency (faster)
    float combined = (left_motor + right_motor) / 2.0f;

    DIPERIODIC periodic;
    periodic.dwMagnitude = static_cast<DWORD>(combined * 10000.0f);
    periodic.lOffset = 0;
    periodic.dwPhase = 0;
    // Lower frequency for left motor emphasis, higher for right
    float freq_factor = (left_motor > right_motor) ? 0.5f : 1.5f;
    periodic.dwPeriod = static_cast<DWORD>(20000.0f / freq_factor); // ~50-150 Hz

    DWORD axes[1] = { DIJOFS_X };
    LONG directions[1] = { 0 };

    DIEFFECT eff;
    ZeroMemory(&eff, sizeof(eff));
    eff.dwSize = sizeof(DIEFFECT);
    eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
    eff.dwDuration = INFINITE;
    eff.dwGain = 10000;
    eff.dwTriggerButton = DIEB_NOTRIGGER;
    eff.cAxes = 1;
    eff.rgdwAxes = axes;
    eff.rglDirection = directions;
    eff.lpEnvelope = nullptr;
    eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
    eff.lpvTypeSpecificParams = &periodic;

    IDirectInputEffect* diEffect = nullptr;
    HRESULT hr = dev.device->CreateEffect(GUID_Sine, &eff, &diEffect, nullptr);
    if (FAILED(hr) || !diEffect) {
        // Try constant force as fallback
        DICONSTANTFORCE cf;
        cf.lMagnitude = static_cast<LONG>(combined * 10000.0f);

        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;

        hr = dev.device->CreateEffect(GUID_ConstantForce, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) {
            return false;
        }
    }

    dev.ff_effects[0].effect = diEffect;
    dev.ff_effects[0].type = ForceFeedbackType::Rumble;
    dev.ff_effects[0].active = true;

    hr = diEffect->Start(1, 0);
    return SUCCEEDED(hr);
}

bool GamepadManager::set_trigger_vibration(int index, float left_trigger, float right_trigger) {
    // DirectInput doesn't support trigger-specific vibration
    (void)index;
    (void)left_trigger;
    (void)right_trigger;
    return false;
}

bool GamepadManager::stop_vibration(int index) {
    return set_vibration(index, 0.0f, 0.0f);
}

ForceFeedbackHandle GamepadManager::play_effect(int index, const ForceFeedbackEffect& effect) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return INVALID_FF_HANDLE;
    }

    DInputDevice& dev = impl_->devices[index];
    if (!dev.connected || !dev.ff_supported) {
        return INVALID_FF_HANDLE;
    }

    ForceFeedbackHandle handle = INVALID_FF_HANDLE;

    switch (effect.type) {
        case ForceFeedbackType::Rumble: {
            // Use set_vibration for rumble
            if (set_vibration(index, effect.left_motor, effect.right_motor)) {
                handle = 0; // Rumble uses slot 0
            }
            break;
        }
        case ForceFeedbackType::Constant: {
            handle = impl_->create_constant_effect(index, effect);
            if (handle != INVALID_FF_HANDLE) {
                impl_->start_effect(index, handle);
            }
            break;
        }
        case ForceFeedbackType::Periodic: {
            handle = impl_->create_periodic_effect(index, effect);
            if (handle != INVALID_FF_HANDLE) {
                impl_->start_effect(index, handle);
            }
            break;
        }
        default:
            // Other effect types not implemented yet
            break;
    }

    return handle;
}

bool GamepadManager::stop_effect(int index, ForceFeedbackHandle handle) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (handle < 0 || handle >= MAX_FORCE_FEEDBACK_EFFECTS) {
        return false;
    }

    if (dev.ff_effects[handle].effect) {
        dev.ff_effects[handle].effect->Stop();
        dev.ff_effects[handle].active = false;
        return true;
    }

    return false;
}

bool GamepadManager::update_effect(int index, ForceFeedbackHandle handle, const ForceFeedbackEffect& effect) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (handle < 0 || handle >= MAX_FORCE_FEEDBACK_EFFECTS) {
        return false;
    }

    if (!dev.ff_effects[handle].effect) {
        return false;
    }

    // For rumble, just call set_vibration
    if (effect.type == ForceFeedbackType::Rumble) {
        return set_vibration(index, effect.left_motor, effect.right_motor);
    }

    // For other effects, we'd need to rebuild the DIEFFECT structure
    // For now, stop and recreate
    stop_effect(index, handle);

    // Release old effect
    dev.ff_effects[handle].effect->Release();
    dev.ff_effects[handle].effect = nullptr;
    dev.ff_effect_count--;

    // Create new effect
    ForceFeedbackHandle new_handle = play_effect(index, effect);
    return (new_handle != INVALID_FF_HANDLE);
}

bool GamepadManager::stop_all_effects(int index) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (!dev.device) return false;

    // Stop all effects
    for (int i = 0; i < MAX_FORCE_FEEDBACK_EFFECTS; i++) {
        if (dev.ff_effects[i].effect) {
            dev.ff_effects[i].effect->Stop();
            dev.ff_effects[i].active = false;
        }
    }

    // Also send stop to device
    dev.device->SendForceFeedbackCommand(DISFFC_STOPALL);

    return true;
}

bool GamepadManager::pause_effects(int index) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (!dev.device) return false;

    HRESULT hr = dev.device->SendForceFeedbackCommand(DISFFC_PAUSE);
    return SUCCEEDED(hr);
}

bool GamepadManager::resume_effects(int index) {
    if (!impl_ || index < 0 || index >= MAX_GAMEPADS) {
        return false;
    }

    DInputDevice& dev = impl_->devices[index];
    if (!dev.device) return false;

    HRESULT hr = dev.device->SendForceFeedbackCommand(DISFFC_CONTINUE);
    return SUCCEEDED(hr);
}

} // namespace input
} // namespace window

#endif // _WIN32 && !WINAPI_FAMILY && WINDOW_SUPPORT_DINPUT
