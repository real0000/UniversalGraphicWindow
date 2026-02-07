/*
 * wheel_dinput.cpp - Windows DirectInput steering wheel implementation
 *
 * DirectInput is required for proper steering wheel force feedback.
 * Supports most racing wheels including:
 * - Logitech (G25, G27, G29, G920, G923)
 * - Thrustmaster (T150, T300, T500, TX, TS-PC)
 * - Fanatec (CSL, CSW, DD)
 */

#if defined(_WIN32) && !defined(WINAPI_FAMILY)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define DIRECTINPUT_VERSION 0x0800

#include "input_wheel.hpp"
#include "../internal/utf8_util.hpp"
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
// Force Feedback Effect Slot
//=============================================================================

struct WheelFFSlot {
    IDirectInputEffect* effect;
    WheelFFType type;
    bool active;

    WheelFFSlot() : effect(nullptr), type(WheelFFType::None), active(false) {}
};

//=============================================================================
// DirectInput Wheel Device
//=============================================================================

struct DInputWheel {
    IDirectInputDevice8* device;
    GUID instance_guid;
    DIJOYSTATE2 state;
    DIJOYSTATE2 prev_state;

    // Device info
    char name[MAX_WHEEL_NAME_LENGTH];
    bool connected;
    bool acquired;

    // Capabilities
    WheelCaps caps;
    float rotation_range;       // Software-limited rotation
    float deadzone;
    float linearity;
    float ff_gain;

    // Force feedback
    WheelFFSlot ff_slots[MAX_WHEEL_FF_EFFECTS];
    int ff_constant_slot;       // Reserved slot for simple constant force
    int ff_spring_slot;         // Reserved slot for spring
    int ff_damper_slot;         // Reserved slot for damper

    DInputWheel() : device(nullptr), connected(false), acquired(false),
                    rotation_range(900.0f), deadzone(0.02f), linearity(1.0f),
                    ff_gain(1.0f), ff_constant_slot(-1), ff_spring_slot(-1),
                    ff_damper_slot(-1) {
        memset(&instance_guid, 0, sizeof(GUID));
        memset(&state, 0, sizeof(DIJOYSTATE2));
        memset(&prev_state, 0, sizeof(DIJOYSTATE2));
        name[0] = '\0';
        caps = WheelCaps();
    }

    void release_effects() {
        for (int i = 0; i < MAX_WHEEL_FF_EFFECTS; i++) {
            if (ff_slots[i].effect) {
                ff_slots[i].effect->Stop();
                ff_slots[i].effect->Release();
                ff_slots[i].effect = nullptr;
            }
            ff_slots[i].active = false;
            ff_slots[i].type = WheelFFType::None;
        }
        ff_constant_slot = -1;
        ff_spring_slot = -1;
        ff_damper_slot = -1;
    }

    int find_free_slot() {
        // Skip first 3 slots (reserved for simple effects)
        for (int i = 3; i < MAX_WHEEL_FF_EFFECTS; i++) {
            if (!ff_slots[i].effect) {
                return i;
            }
        }
        return -1;
    }
};

//=============================================================================
// WheelManager::Impl
//=============================================================================

struct WheelManager::Impl {
    WheelEventDispatcher dispatcher;
    WheelState wheels[MAX_WHEELS];
    DInputWheel devices[MAX_WHEELS];
    IDirectInput8* dinput;
    HWND hwnd;
    int device_count;
    bool needs_enumeration;
    float global_deadzone;

    Impl() : dinput(nullptr), hwnd(nullptr), device_count(0),
             needs_enumeration(true), global_deadzone(0.02f) {
        for (int i = 0; i < MAX_WHEELS; i++) {
            wheels[i].reset();
        }
    }

    ~Impl() {
        shutdown();
    }

    bool initialize() {
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

        hwnd = GetDesktopWindow();
        return true;
    }

    void shutdown() {
        for (int i = 0; i < MAX_WHEELS; i++) {
            if (devices[i].device) {
                devices[i].release_effects();
                devices[i].device->Unacquire();
                devices[i].device->Release();
                devices[i].device = nullptr;
            }
            devices[i].connected = false;
        }

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

    // Check if device is likely a steering wheel based on name/properties
    static bool is_steering_wheel(const DIDEVICEINSTANCE* instance, const DIDEVCAPS* caps) {
        // Check device type
        BYTE devType = LOBYTE(instance->dwDevType);
        BYTE devSubType = HIBYTE(instance->dwDevType);

        // DI8DEVTYPE_DRIVING is the wheel device type
        if (devType == DI8DEVTYPE_DRIVING) {
            return true;
        }

        // Some wheels register as joysticks, check name
#ifdef UNICODE
        const wchar_t* name = instance->tszProductName;
        if (wcsstr(name, L"Wheel") || wcsstr(name, L"wheel") ||
            wcsstr(name, L"Racing") || wcsstr(name, L"racing") ||
            wcsstr(name, L"G27") || wcsstr(name, L"G29") || wcsstr(name, L"G920") ||
            wcsstr(name, L"G923") || wcsstr(name, L"T300") || wcsstr(name, L"T150") ||
            wcsstr(name, L"T500") || wcsstr(name, L"Thrustmaster") ||
            wcsstr(name, L"Fanatec") || wcsstr(name, L"CSL") || wcsstr(name, L"CSW")) {
            return true;
        }
#else
        const char* name = instance->tszProductName;
        if (strstr(name, "Wheel") || strstr(name, "wheel") ||
            strstr(name, "Racing") || strstr(name, "racing") ||
            strstr(name, "G27") || strstr(name, "G29") || strstr(name, "G920") ||
            strstr(name, "G923") || strstr(name, "T300") || strstr(name, "T150") ||
            strstr(name, "T500") || strstr(name, "Thrustmaster") ||
            strstr(name, "Fanatec") || strstr(name, "CSL") || strstr(name, "CSW")) {
            return true;
        }
#endif

        // Check for force feedback (most wheels have it)
        if (caps->dwFlags & DIDC_FORCEFEEDBACK) {
            // Has FF and reasonable number of axes (wheels typically have 3+)
            if (caps->dwAxes >= 3) {
                return true;
            }
        }

        return false;
    }

    static BOOL CALLBACK enum_devices_callback(const DIDEVICEINSTANCE* instance, void* context) {
        Impl* impl = static_cast<Impl*>(context);
        return impl->on_device_found(instance);
    }

    BOOL on_device_found(const DIDEVICEINSTANCE* instance) {
        if (device_count >= MAX_WHEELS) {
            return DIENUM_STOP;
        }

        // Check if already registered
        for (int i = 0; i < device_count; i++) {
            if (memcmp(&devices[i].instance_guid, &instance->guidInstance, sizeof(GUID)) == 0) {
                return DIENUM_CONTINUE;
            }
        }

        // Create device to check capabilities
        IDirectInputDevice8* device = nullptr;
        HRESULT hr = dinput->CreateDevice(instance->guidInstance, &device, nullptr);
        if (FAILED(hr)) {
            return DIENUM_CONTINUE;
        }

        // Get capabilities
        DIDEVCAPS dicaps;
        dicaps.dwSize = sizeof(DIDEVCAPS);
        hr = device->GetCapabilities(&dicaps);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Check if this is a steering wheel
        if (!is_steering_wheel(instance, &dicaps)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Set data format
        hr = device->SetDataFormat(&c_dfDIJoystick2);
        if (FAILED(hr)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        // Set cooperative level (exclusive for force feedback)
        DWORD coop_flags = DISCL_BACKGROUND;
        if (dicaps.dwFlags & DIDC_FORCEFEEDBACK) {
            coop_flags |= DISCL_EXCLUSIVE;
        } else {
            coop_flags |= DISCL_NONEXCLUSIVE;
        }
        hr = device->SetCooperativeLevel(hwnd, coop_flags);
        if (FAILED(hr)) {
            // Try non-exclusive if exclusive fails
            hr = device->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            if (FAILED(hr)) {
                device->Release();
                return DIENUM_CONTINUE;
            }
        }

        // Store device
        int idx = device_count;
        DInputWheel& wheel = devices[idx];
        wheel.device = device;
        wheel.instance_guid = instance->guidInstance;
        wheel.connected = true;
        wheel.acquired = false;

        // Copy name
#ifdef UNICODE
        internal::wide_to_utf8(instance->tszProductName, wheel.name, MAX_WHEEL_NAME_LENGTH);
#else
        strncpy(wheel.name, instance->tszProductName, MAX_WHEEL_NAME_LENGTH - 1);
        wheel.name[MAX_WHEEL_NAME_LENGTH - 1] = '\0';
#endif

        // Setup capabilities
        setup_capabilities(idx, &dicaps);

        // Setup force feedback
        if (wheel.caps.has_force_feedback) {
            setup_force_feedback(idx);
        }

        // Copy to state
        wheels[idx].name = wheel.name;
        wheels[idx].connected = true;

        device_count++;

        // Dispatch connection event
        WheelConnectionEvent event;
        event.type = WheelEventType::Connected;
        event.wheel_index = idx;
        event.timestamp = get_timestamp();
        event.name = wheels[idx].name.c_str();
        event.connected = true;
        dispatcher.dispatch_connection(event);

        return DIENUM_CONTINUE;
    }

    void setup_capabilities(int idx, const DIDEVCAPS* dicaps) {
        DInputWheel& wheel = devices[idx];
        WheelCaps& caps = wheel.caps;

        caps.num_axes = dicaps->dwAxes;
        caps.num_buttons = dicaps->dwButtons;

        // Assume standard wheel configuration
        caps.has_throttle = (dicaps->dwAxes >= 2);
        caps.has_brake = (dicaps->dwAxes >= 3);
        caps.has_clutch = (dicaps->dwAxes >= 4);

        // Estimate rotation based on common wheels
        caps.rotation_degrees = 900.0f;  // Default
        caps.min_rotation = -450.0f;
        caps.max_rotation = 450.0f;

        // Check for force feedback
        if (dicaps->dwFlags & DIDC_FORCEFEEDBACK) {
            caps.has_force_feedback = true;
            caps.max_ff_effects = MAX_WHEEL_FF_EFFECTS;
        }
    }

    static BOOL CALLBACK enum_ff_effects_callback(const DIEFFECTINFO* info, void* context) {
        WheelCaps* caps = static_cast<WheelCaps*>(context);

        if (info->guid == GUID_ConstantForce) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::ConstantForce));
        } else if (info->guid == GUID_Spring) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::SpringForce));
        } else if (info->guid == GUID_Damper) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::DamperForce));
        } else if (info->guid == GUID_Friction) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::FrictionForce));
        } else if (info->guid == GUID_Inertia) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::InertiaForce));
        } else if (info->guid == GUID_Sine) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::SineWave));
        } else if (info->guid == GUID_Square) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::SquareWave));
        } else if (info->guid == GUID_Triangle) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::TriangleWave));
        } else if (info->guid == GUID_SawtoothUp || info->guid == GUID_SawtoothDown) {
            caps->supported_ff_effects |= (1 << static_cast<int>(WheelFFType::SawtoothWave));
        }

        return DIENUM_CONTINUE;
    }

    void setup_force_feedback(int idx) {
        DInputWheel& wheel = devices[idx];

        if (!wheel.device) return;

        // Disable auto-center
        DIPROPDWORD dipdw;
        dipdw.diph.dwSize = sizeof(DIPROPDWORD);
        dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        dipdw.diph.dwObj = 0;
        dipdw.diph.dwHow = DIPH_DEVICE;
        dipdw.dwData = DIPROPAUTOCENTER_OFF;
        wheel.device->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

        // Set FF gain to maximum
        dipdw.dwData = 10000;
        wheel.device->SetProperty(DIPROP_FFGAIN, &dipdw.diph);

        // Enumerate supported effects
        wheel.device->EnumEffects(enum_ff_effects_callback, &wheel.caps, DIEFT_ALL);
    }

    void enumerate_devices() {
        if (!dinput) return;

        // Enumerate both driving devices and joysticks (some wheels register as joysticks)
        dinput->EnumDevices(DI8DEVCLASS_GAMECTRL, enum_devices_callback, this, DIEDFL_ATTACHEDONLY);

        needs_enumeration = false;
    }

    float apply_deadzone(float value, float dz) {
        if (std::abs(value) < dz) {
            return 0.0f;
        }
        float sign = value > 0 ? 1.0f : -1.0f;
        return sign * (std::abs(value) - dz) / (1.0f - dz);
    }

    float apply_linearity(float value, float linearity) {
        if (linearity == 1.0f) return value;
        float sign = value > 0 ? 1.0f : -1.0f;
        return sign * std::pow(std::abs(value), linearity);
    }

    float normalize_axis(LONG raw, bool centered = true) {
        float normalized;
        if (centered) {
            // Range: 0-65535, center at 32767
            normalized = (static_cast<float>(raw) - 32767.0f) / 32767.0f;
        } else {
            // Range: 0-65535, 0 = released, 65535 = pressed
            normalized = static_cast<float>(raw) / 65535.0f;
        }

        if (normalized < -1.0f) normalized = -1.0f;
        if (normalized > 1.0f) normalized = 1.0f;

        return normalized;
    }

    void update() {
        if (!dinput) return;

        double timestamp = get_timestamp();

        // Periodic enumeration
        static int enum_counter = 0;
        if (++enum_counter >= 120 || needs_enumeration) {
            enum_counter = 0;
            enumerate_devices();
        }

        // Update each wheel
        for (int i = 0; i < device_count; i++) {
            DInputWheel& wheel = devices[i];
            WheelState& ws = wheels[i];

            if (!wheel.device || !wheel.connected) continue;

            // Acquire if needed
            if (!wheel.acquired) {
                HRESULT hr = wheel.device->Acquire();
                if (FAILED(hr)) {
                    if (hr == DIERR_UNPLUGGED || hr == DIERR_INPUTLOST) {
                        handle_disconnect(i, timestamp);
                    }
                    continue;
                }
                wheel.acquired = true;
            }

            // Poll
            HRESULT hr = wheel.device->Poll();
            if (FAILED(hr)) {
                hr = wheel.device->Acquire();
                if (FAILED(hr)) {
                    if (hr == DIERR_UNPLUGGED || hr == DIERR_INPUTLOST) {
                        handle_disconnect(i, timestamp);
                    }
                    continue;
                }
                wheel.device->Poll();
            }

            // Get state
            wheel.prev_state = wheel.state;
            hr = wheel.device->GetDeviceState(sizeof(DIJOYSTATE2), &wheel.state);
            if (FAILED(hr)) {
                wheel.acquired = false;
                continue;
            }

            // Process steering
            float new_steering = normalize_axis(wheel.state.lX, true);
            new_steering = apply_deadzone(new_steering, wheel.deadzone);
            new_steering = apply_linearity(new_steering, wheel.linearity);

            if (std::abs(new_steering - ws.steering) > 0.001f) {
                float old_steering = ws.steering;
                ws.steering = new_steering;
                ws.steering_degrees = new_steering * (wheel.rotation_range / 2.0f);

                WheelAxisEvent event;
                event.type = WheelEventType::AxisChanged;
                event.wheel_index = i;
                event.timestamp = timestamp;
                event.axis = WheelAxis::Steering;
                event.value = new_steering;
                event.delta = new_steering - old_steering;
                dispatcher.dispatch_axis(event);
            }

            // Process pedals (typically Y = combined or throttle, Rz = brake, slider = clutch)
            auto process_pedal = [&](WheelAxis axis, LONG raw_value, float& state_value) {
                // Pedals are typically 0 (released) to 65535 (pressed)
                // Some wheels invert this, may need calibration
                float new_value = 1.0f - normalize_axis(raw_value, false);  // Invert
                new_value = apply_deadzone(new_value, wheel.deadzone * 0.5f);

                if (std::abs(new_value - state_value) > 0.001f) {
                    float old_value = state_value;
                    state_value = new_value;

                    WheelAxisEvent event;
                    event.type = WheelEventType::AxisChanged;
                    event.wheel_index = i;
                    event.timestamp = timestamp;
                    event.axis = axis;
                    event.value = new_value;
                    event.delta = new_value - old_value;
                    dispatcher.dispatch_axis(event);
                }
            };

            // Common axis mappings (may vary by wheel)
            process_pedal(WheelAxis::Throttle, wheel.state.lY, ws.throttle);
            process_pedal(WheelAxis::Brake, wheel.state.lRz, ws.brake);
            if (wheel.caps.has_clutch) {
                process_pedal(WheelAxis::Clutch, wheel.state.rglSlider[0], ws.clutch);
            }

            // Process buttons
            for (int btn = 0; btn < 32 && btn < wheel.caps.num_buttons; btn++) {
                bool is_down = (wheel.state.rgbButtons[btn] & 0x80) != 0;
                bool was_down = (wheel.prev_state.rgbButtons[btn] & 0x80) != 0;

                if (is_down != was_down && btn < MAX_WHEEL_BUTTONS) {
                    ws.buttons[btn] = is_down;

                    WheelButtonEvent event;
                    event.type = is_down ? WheelEventType::ButtonDown : WheelEventType::ButtonUp;
                    event.wheel_index = i;
                    event.timestamp = timestamp;
                    event.button = static_cast<WheelButton>(btn);
                    dispatcher.dispatch_button(event);
                }
            }

            // Process D-pad (POV hat)
            DWORD pov = wheel.state.rgdwPOV[0];
            if (pov != wheel.prev_state.rgdwPOV[0]) {
                process_dpad(i, pov, timestamp);
            }
        }
    }

    void process_dpad(int wheel_idx, DWORD pov, double timestamp) {
        WheelState& ws = wheels[wheel_idx];

        bool up = false, down = false, left = false, right = false;

        if (LOWORD(pov) != 0xFFFF) {
            if (pov >= 31500 || pov <= 4500) up = true;
            if (pov >= 4500 && pov <= 13500) right = true;
            if (pov >= 13500 && pov <= 22500) down = true;
            if (pov >= 22500 && pov <= 31500) left = true;
        }

        auto check_dpad = [&](WheelButton btn, bool is_down) {
            int btn_idx = static_cast<int>(btn);
            if (btn_idx < MAX_WHEEL_BUTTONS && is_down != ws.buttons[btn_idx]) {
                ws.buttons[btn_idx] = is_down;

                WheelButtonEvent event;
                event.type = is_down ? WheelEventType::ButtonDown : WheelEventType::ButtonUp;
                event.wheel_index = wheel_idx;
                event.timestamp = timestamp;
                event.button = btn;
                dispatcher.dispatch_button(event);
            }
        };

        check_dpad(WheelButton::DPadUp, up);
        check_dpad(WheelButton::DPadDown, down);
        check_dpad(WheelButton::DPadLeft, left);
        check_dpad(WheelButton::DPadRight, right);
    }

    void handle_disconnect(int idx, double timestamp) {
        DInputWheel& wheel = devices[idx];
        WheelState& ws = wheels[idx];

        if (!wheel.connected) return;

        wheel.connected = false;
        wheel.acquired = false;
        ws.connected = false;

        WheelConnectionEvent event;
        event.type = WheelEventType::Disconnected;
        event.wheel_index = idx;
        event.timestamp = timestamp;
        event.name = nullptr;
        event.connected = false;
        dispatcher.dispatch_connection(event);

        ws.reset();

        if (wheel.device) {
            wheel.release_effects();
            wheel.device->Unacquire();
            wheel.device->Release();
            wheel.device = nullptr;
        }

        needs_enumeration = true;
    }

    //=========================================================================
    // Force Feedback Implementation
    //=========================================================================

    WheelFFHandle create_constant_force(int idx, float force, uint32_t duration_ms) {
        DInputWheel& wheel = devices[idx];
        if (!wheel.device || !wheel.caps.has_force_feedback) return INVALID_WHEEL_FF_HANDLE;

        // Use reserved slot 0 for constant force
        int slot = 0;

        // Release existing effect
        if (wheel.ff_slots[slot].effect) {
            wheel.ff_slots[slot].effect->Stop();
            wheel.ff_slots[slot].effect->Release();
            wheel.ff_slots[slot].effect = nullptr;
        }

        DICONSTANTFORCE cf;
        cf.lMagnitude = static_cast<LONG>(force * wheel.ff_gain * 10000.0f);

        DWORD axes[1] = { DIJOFS_X };
        LONG directions[1] = { 0 };

        DIEFFECT eff;
        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = duration_ms > 0 ? duration_ms * 1000 : INFINITE;
        eff.dwGain = 10000;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 1;
        eff.rgdwAxes = axes;
        eff.rglDirection = directions;
        eff.lpEnvelope = nullptr;
        eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
        eff.lpvTypeSpecificParams = &cf;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = wheel.device->CreateEffect(GUID_ConstantForce, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_WHEEL_FF_HANDLE;

        wheel.ff_slots[slot].effect = diEffect;
        wheel.ff_slots[slot].type = WheelFFType::ConstantForce;
        wheel.ff_constant_slot = slot;

        return slot;
    }

    WheelFFHandle create_spring_force(int idx, float strength, float center, float saturation) {
        DInputWheel& wheel = devices[idx];
        if (!wheel.device || !wheel.caps.has_force_feedback) return INVALID_WHEEL_FF_HANDLE;

        int slot = 1;  // Reserved slot for spring

        if (wheel.ff_slots[slot].effect) {
            wheel.ff_slots[slot].effect->Stop();
            wheel.ff_slots[slot].effect->Release();
            wheel.ff_slots[slot].effect = nullptr;
        }

        DICONDITION cond[1];
        cond[0].lOffset = static_cast<LONG>(center * 10000.0f);
        cond[0].lPositiveCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
        cond[0].lNegativeCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
        cond[0].dwPositiveSaturation = static_cast<DWORD>(saturation * 10000.0f);
        cond[0].dwNegativeSaturation = static_cast<DWORD>(saturation * 10000.0f);
        cond[0].lDeadBand = 0;

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
        eff.cbTypeSpecificParams = sizeof(DICONDITION);
        eff.lpvTypeSpecificParams = cond;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = wheel.device->CreateEffect(GUID_Spring, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_WHEEL_FF_HANDLE;

        wheel.ff_slots[slot].effect = diEffect;
        wheel.ff_slots[slot].type = WheelFFType::SpringForce;
        wheel.ff_spring_slot = slot;

        return slot;
    }

    WheelFFHandle create_damper_force(int idx, float strength) {
        DInputWheel& wheel = devices[idx];
        if (!wheel.device || !wheel.caps.has_force_feedback) return INVALID_WHEEL_FF_HANDLE;

        int slot = 2;  // Reserved slot for damper

        if (wheel.ff_slots[slot].effect) {
            wheel.ff_slots[slot].effect->Stop();
            wheel.ff_slots[slot].effect->Release();
            wheel.ff_slots[slot].effect = nullptr;
        }

        DICONDITION cond[1];
        cond[0].lOffset = 0;
        cond[0].lPositiveCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
        cond[0].lNegativeCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
        cond[0].dwPositiveSaturation = 10000;
        cond[0].dwNegativeSaturation = 10000;
        cond[0].lDeadBand = 0;

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
        eff.cbTypeSpecificParams = sizeof(DICONDITION);
        eff.lpvTypeSpecificParams = cond;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = wheel.device->CreateEffect(GUID_Damper, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_WHEEL_FF_HANDLE;

        wheel.ff_slots[slot].effect = diEffect;
        wheel.ff_slots[slot].type = WheelFFType::DamperForce;
        wheel.ff_damper_slot = slot;

        return slot;
    }

    WheelFFHandle create_periodic_effect(int idx, const WheelFFEffect& effect) {
        DInputWheel& wheel = devices[idx];
        if (!wheel.device || !wheel.caps.has_force_feedback) return INVALID_WHEEL_FF_HANDLE;

        int slot = wheel.find_free_slot();
        if (slot < 0) return INVALID_WHEEL_FF_HANDLE;

        GUID effectGuid;
        switch (effect.type) {
            case WheelFFType::SquareWave: effectGuid = GUID_Square; break;
            case WheelFFType::TriangleWave: effectGuid = GUID_Triangle; break;
            case WheelFFType::SawtoothWave: effectGuid = GUID_SawtoothUp; break;
            default: effectGuid = GUID_Sine; break;
        }

        DIPERIODIC periodic;
        periodic.dwMagnitude = static_cast<DWORD>(effect.magnitude * wheel.ff_gain * 10000.0f);
        periodic.lOffset = static_cast<LONG>(effect.offset * 10000.0f);
        periodic.dwPhase = static_cast<DWORD>(effect.phase * 36000.0f);
        periodic.dwPeriod = static_cast<DWORD>(1000000.0f / effect.frequency_hz);

        DWORD axes[1] = { DIJOFS_X };
        LONG directions[1] = { 0 };

        DIEFFECT eff;
        ZeroMemory(&eff, sizeof(eff));
        eff.dwSize = sizeof(DIEFFECT);
        eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        eff.dwDuration = effect.duration_ms > 0 ? effect.duration_ms * 1000 : INFINITE;
        eff.dwStartDelay = effect.start_delay_ms * 1000;
        eff.dwGain = 10000;
        eff.dwTriggerButton = DIEB_NOTRIGGER;
        eff.cAxes = 1;
        eff.rgdwAxes = axes;
        eff.rglDirection = directions;
        eff.lpEnvelope = nullptr;
        eff.cbTypeSpecificParams = sizeof(DIPERIODIC);
        eff.lpvTypeSpecificParams = &periodic;

        IDirectInputEffect* diEffect = nullptr;
        HRESULT hr = wheel.device->CreateEffect(effectGuid, &eff, &diEffect, nullptr);
        if (FAILED(hr) || !diEffect) return INVALID_WHEEL_FF_HANDLE;

        wheel.ff_slots[slot].effect = diEffect;
        wheel.ff_slots[slot].type = effect.type;

        return slot;
    }

    bool start_effect(int idx, int slot) {
        DInputWheel& wheel = devices[idx];
        if (slot < 0 || slot >= MAX_WHEEL_FF_EFFECTS) return false;
        if (!wheel.ff_slots[slot].effect) return false;

        HRESULT hr = wheel.ff_slots[slot].effect->Start(1, 0);
        if (SUCCEEDED(hr)) {
            wheel.ff_slots[slot].active = true;
            return true;
        }
        return false;
    }

    bool stop_effect_impl(int idx, int slot) {
        DInputWheel& wheel = devices[idx];
        if (slot < 0 || slot >= MAX_WHEEL_FF_EFFECTS) return false;
        if (!wheel.ff_slots[slot].effect) return false;

        wheel.ff_slots[slot].effect->Stop();
        wheel.ff_slots[slot].active = false;
        return true;
    }
};

//=============================================================================
// WheelManager Implementation
//=============================================================================

WheelManager::WheelManager() : impl_(nullptr) {}

WheelManager::~WheelManager() {
    delete impl_;
}

WheelManager* WheelManager::create() {
    WheelManager* mgr = new WheelManager();
    mgr->impl_ = new WheelManager::Impl();

    if (!mgr->impl_->initialize()) {
        delete mgr;
        return nullptr;
    }

    mgr->impl_->enumerate_devices();
    return mgr;
}

void WheelManager::destroy() {
    delete this;
}

void WheelManager::update() {
    if (impl_) impl_->update();
}

bool WheelManager::add_handler(IWheelHandler* handler) {
    return impl_ ? impl_->dispatcher.add_handler(handler) : false;
}

bool WheelManager::remove_handler(IWheelHandler* handler) {
    return impl_ ? impl_->dispatcher.remove_handler(handler) : false;
}

bool WheelManager::remove_handler(const char* handler_id) {
    return impl_ ? impl_->dispatcher.remove_handler(handler_id) : false;
}

WheelEventDispatcher* WheelManager::get_dispatcher() {
    return impl_ ? &impl_->dispatcher : nullptr;
}

int WheelManager::get_wheel_count() const {
    if (!impl_) return 0;
    int count = 0;
    for (int i = 0; i < impl_->device_count; i++) {
        if (impl_->wheels[i].connected) count++;
    }
    return count;
}

bool WheelManager::is_connected(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    return impl_->wheels[index].connected;
}

const WheelState* WheelManager::get_state(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return nullptr;
    return &impl_->wheels[index];
}

bool WheelManager::get_capabilities(int index, WheelCaps* caps) const {
    if (!caps || !impl_ || index < 0 || index >= MAX_WHEELS) return false;
    *caps = impl_->devices[index].caps;
    return impl_->devices[index].connected;
}

float WheelManager::get_steering(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->wheels[index].steering;
}

float WheelManager::get_steering_degrees(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->wheels[index].steering_degrees;
}

float WheelManager::get_throttle(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->wheels[index].throttle;
}

float WheelManager::get_brake(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->wheels[index].brake;
}

float WheelManager::get_clutch(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->wheels[index].clutch;
}

GearPosition WheelManager::get_gear(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return GearPosition::Unknown;
    return impl_->wheels[index].gear;
}

bool WheelManager::is_button_down(int index, WheelButton button) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    int btn_idx = static_cast<int>(button);
    if (btn_idx < 0 || btn_idx >= MAX_WHEEL_BUTTONS) return false;
    return impl_->wheels[index].buttons[btn_idx];
}

void WheelManager::set_rotation_range(int index, float degrees) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return;
    impl_->devices[index].rotation_range = degrees;
}

float WheelManager::get_rotation_range(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 900.0f;
    return impl_->devices[index].rotation_range;
}

void WheelManager::set_deadzone(float deadzone) {
    if (impl_) {
        impl_->global_deadzone = deadzone;
        for (int i = 0; i < MAX_WHEELS; i++) {
            impl_->devices[i].deadzone = deadzone;
        }
    }
}

float WheelManager::get_deadzone() const {
    return impl_ ? impl_->global_deadzone : 0.02f;
}

void WheelManager::set_linearity(int index, float linearity) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return;
    impl_->devices[index].linearity = linearity;
}

//=============================================================================
// Force Feedback Methods
//=============================================================================

bool WheelManager::supports_force_feedback(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    return impl_->devices[index].caps.has_force_feedback;
}

bool WheelManager::get_ff_capabilities(int index, WheelCaps* caps) const {
    return get_capabilities(index, caps);
}

bool WheelManager::set_constant_force(int index, float force) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    WheelFFHandle handle = impl_->create_constant_force(index, force, 0);
    if (handle != INVALID_WHEEL_FF_HANDLE) {
        return impl_->start_effect(index, handle);
    }
    return false;
}

bool WheelManager::set_spring_force(int index, float strength, float center) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    WheelFFHandle handle = impl_->create_spring_force(index, strength, center, 1.0f);
    if (handle != INVALID_WHEEL_FF_HANDLE) {
        return impl_->start_effect(index, handle);
    }
    return false;
}

bool WheelManager::set_damper_force(int index, float strength) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    WheelFFHandle handle = impl_->create_damper_force(index, strength);
    if (handle != INVALID_WHEEL_FF_HANDLE) {
        return impl_->start_effect(index, handle);
    }
    return false;
}

bool WheelManager::set_friction_force(int index, float strength) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    // Friction is similar to damper
    DInputWheel& wheel = impl_->devices[index];
    if (!wheel.device || !wheel.caps.has_force_feedback) return false;

    int slot = 2;  // Share with damper slot

    if (wheel.ff_slots[slot].effect) {
        wheel.ff_slots[slot].effect->Stop();
        wheel.ff_slots[slot].effect->Release();
        wheel.ff_slots[slot].effect = nullptr;
    }

    DICONDITION cond[1];
    cond[0].lOffset = 0;
    cond[0].lPositiveCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
    cond[0].lNegativeCoefficient = static_cast<LONG>(strength * wheel.ff_gain * 10000.0f);
    cond[0].dwPositiveSaturation = 10000;
    cond[0].dwNegativeSaturation = 10000;
    cond[0].lDeadBand = 0;

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
    eff.cbTypeSpecificParams = sizeof(DICONDITION);
    eff.lpvTypeSpecificParams = cond;

    IDirectInputEffect* diEffect = nullptr;
    HRESULT hr = wheel.device->CreateEffect(GUID_Friction, &eff, &diEffect, nullptr);
    if (FAILED(hr) || !diEffect) return false;

    wheel.ff_slots[slot].effect = diEffect;
    wheel.ff_slots[slot].type = WheelFFType::FrictionForce;

    return impl_->start_effect(index, slot);
}

bool WheelManager::set_sine_effect(int index, float magnitude, float frequency_hz) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    WheelFFEffect effect;
    effect.type = WheelFFType::SineWave;
    effect.magnitude = magnitude;
    effect.frequency_hz = frequency_hz;
    effect.duration_ms = 0;  // Infinite

    WheelFFHandle handle = impl_->create_periodic_effect(index, effect);
    if (handle != INVALID_WHEEL_FF_HANDLE) {
        return impl_->start_effect(index, handle);
    }
    return false;
}

bool WheelManager::stop_all_forces(int index) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    DInputWheel& wheel = impl_->devices[index];
    if (!wheel.device) return false;

    // Stop all effects
    for (int i = 0; i < MAX_WHEEL_FF_EFFECTS; i++) {
        if (wheel.ff_slots[i].effect) {
            wheel.ff_slots[i].effect->Stop();
            wheel.ff_slots[i].active = false;
        }
    }

    wheel.device->SendForceFeedbackCommand(DISFFC_STOPALL);
    return true;
}

WheelFFHandle WheelManager::play_effect(int index, const WheelFFEffect& effect) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return INVALID_WHEEL_FF_HANDLE;

    WheelFFHandle handle = INVALID_WHEEL_FF_HANDLE;

    switch (effect.type) {
        case WheelFFType::ConstantForce:
            handle = impl_->create_constant_force(index, effect.constant_force, effect.duration_ms);
            break;
        case WheelFFType::SpringForce:
            handle = impl_->create_spring_force(index, effect.coefficient, effect.center_point, effect.saturation);
            break;
        case WheelFFType::DamperForce:
            handle = impl_->create_damper_force(index, effect.coefficient);
            break;
        case WheelFFType::SineWave:
        case WheelFFType::SquareWave:
        case WheelFFType::TriangleWave:
        case WheelFFType::SawtoothWave:
            handle = impl_->create_periodic_effect(index, effect);
            break;
        default:
            break;
    }

    if (handle != INVALID_WHEEL_FF_HANDLE) {
        impl_->start_effect(index, handle);
    }

    return handle;
}

bool WheelManager::stop_effect(int index, WheelFFHandle handle) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    return impl_->stop_effect_impl(index, handle);
}

bool WheelManager::update_effect(int index, WheelFFHandle handle, const WheelFFEffect& effect) {
    // Stop and recreate effect
    if (!stop_effect(index, handle)) return false;

    DInputWheel& wheel = impl_->devices[index];
    if (handle >= 0 && handle < MAX_WHEEL_FF_EFFECTS && wheel.ff_slots[handle].effect) {
        wheel.ff_slots[handle].effect->Release();
        wheel.ff_slots[handle].effect = nullptr;
    }

    WheelFFHandle new_handle = play_effect(index, effect);
    return (new_handle != INVALID_WHEEL_FF_HANDLE);
}

bool WheelManager::set_ff_gain(int index, float gain) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    DInputWheel& wheel = impl_->devices[index];
    wheel.ff_gain = gain;

    if (!wheel.device) return false;

    DIPROPDWORD dipdw;
    dipdw.diph.dwSize = sizeof(DIPROPDWORD);
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipdw.diph.dwObj = 0;
    dipdw.diph.dwHow = DIPH_DEVICE;
    dipdw.dwData = static_cast<DWORD>(gain * 10000.0f);

    HRESULT hr = wheel.device->SetProperty(DIPROP_FFGAIN, &dipdw.diph);
    return SUCCEEDED(hr);
}

float WheelManager::get_ff_gain(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 1.0f;
    return impl_->devices[index].ff_gain;
}

bool WheelManager::set_ff_autocenter(int index, bool enabled, float strength) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    DInputWheel& wheel = impl_->devices[index];
    if (!wheel.device) return false;

    DIPROPDWORD dipdw;
    dipdw.diph.dwSize = sizeof(DIPROPDWORD);
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dipdw.diph.dwObj = 0;
    dipdw.diph.dwHow = DIPH_DEVICE;
    dipdw.dwData = enabled ? DIPROPAUTOCENTER_ON : DIPROPAUTOCENTER_OFF;

    HRESULT hr = wheel.device->SetProperty(DIPROP_AUTOCENTER, &dipdw.diph);

    if (enabled && SUCCEEDED(hr)) {
        // Set spring to simulate auto-center strength
        set_spring_force(index, strength, 0.0f);
    }

    return SUCCEEDED(hr);
}

bool WheelManager::pause_ff(int index) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    DInputWheel& wheel = impl_->devices[index];
    if (!wheel.device) return false;

    HRESULT hr = wheel.device->SendForceFeedbackCommand(DISFFC_PAUSE);
    return SUCCEEDED(hr);
}

bool WheelManager::resume_ff(int index) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;

    DInputWheel& wheel = impl_->devices[index];
    if (!wheel.device) return false;

    HRESULT hr = wheel.device->SendForceFeedbackCommand(DISFFC_CONTINUE);
    return SUCCEEDED(hr);
}

} // namespace input
} // namespace window

#endif // _WIN32 && !WINAPI_FAMILY
