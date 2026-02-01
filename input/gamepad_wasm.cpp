/*
 * gamepad_wasm.cpp - Gamepad support for WebAssembly using HTML5 Gamepad API
 */

#ifdef WINDOW_PLATFORM_WASM

#include "input_gamepad.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace window {
namespace input {

//=============================================================================
// WASM Gamepad Implementation
//=============================================================================

class WasmGamepad : public IGamepadDevice {
public:
    int index = -1;
    char name[MAX_GAMEPAD_NAME_LENGTH] = {};
    char id[64] = {};
    bool connected = false;
    GamepadState current_state = {};
    GamepadState previous_state = {};

    const char* get_name() const override { return name; }
    const char* get_id() const override { return id; }
    bool is_connected() const override { return connected; }

    bool poll(GamepadState* out_state) override {
        if (!connected || !out_state) return false;

        previous_state = current_state;
        memset(&current_state, 0, sizeof(GamepadState));

        // Query gamepad state via JavaScript
        bool success = EM_ASM_INT({
            var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            var gp = gamepads[$0];
            if (!gp || !gp.connected) return 0;

            // Buttons
            var buttons = 0;
            if (gp.buttons.length > 0 && gp.buttons[0].pressed) buttons |= 0x1000;    // A
            if (gp.buttons.length > 1 && gp.buttons[1].pressed) buttons |= 0x2000;    // B
            if (gp.buttons.length > 2 && gp.buttons[2].pressed) buttons |= 0x4000;    // X
            if (gp.buttons.length > 3 && gp.buttons[3].pressed) buttons |= 0x8000;    // Y
            if (gp.buttons.length > 4 && gp.buttons[4].pressed) buttons |= 0x0100;    // LB
            if (gp.buttons.length > 5 && gp.buttons[5].pressed) buttons |= 0x0200;    // RB
            if (gp.buttons.length > 6 && gp.buttons[6].pressed) buttons |= 0x0001;    // LT (as button)
            if (gp.buttons.length > 7 && gp.buttons[7].pressed) buttons |= 0x0002;    // RT (as button)
            if (gp.buttons.length > 8 && gp.buttons[8].pressed) buttons |= 0x0020;    // Back
            if (gp.buttons.length > 9 && gp.buttons[9].pressed) buttons |= 0x0010;    // Start
            if (gp.buttons.length > 10 && gp.buttons[10].pressed) buttons |= 0x0040;  // LS click
            if (gp.buttons.length > 11 && gp.buttons[11].pressed) buttons |= 0x0080;  // RS click
            if (gp.buttons.length > 12 && gp.buttons[12].pressed) buttons |= 0x0001;  // D-pad up
            if (gp.buttons.length > 13 && gp.buttons[13].pressed) buttons |= 0x0002;  // D-pad down
            if (gp.buttons.length > 14 && gp.buttons[14].pressed) buttons |= 0x0004;  // D-pad left
            if (gp.buttons.length > 15 && gp.buttons[15].pressed) buttons |= 0x0008;  // D-pad right

            HEAP16[$1 >> 1] = buttons;

            // Axes (normalized to -32768 to 32767)
            var axisScale = 32767;
            HEAP16[($1 + 2) >> 1] = gp.axes.length > 0 ? Math.round(gp.axes[0] * axisScale) : 0;  // Left X
            HEAP16[($1 + 4) >> 1] = gp.axes.length > 1 ? Math.round(gp.axes[1] * axisScale) : 0;  // Left Y
            HEAP16[($1 + 6) >> 1] = gp.axes.length > 2 ? Math.round(gp.axes[2] * axisScale) : 0;  // Right X
            HEAP16[($1 + 8) >> 1] = gp.axes.length > 3 ? Math.round(gp.axes[3] * axisScale) : 0;  // Right Y

            // Triggers (from button values if available)
            HEAPU8[$1 + 10] = gp.buttons.length > 6 ? Math.round(gp.buttons[6].value * 255) : 0;  // LT
            HEAPU8[$1 + 11] = gp.buttons.length > 7 ? Math.round(gp.buttons[7].value * 255) : 0;  // RT

            return 1;
        }, index, &current_state);

        if (success) {
            *out_state = current_state;
            return true;
        }

        return false;
    }

    bool set_vibration(float left_motor, float right_motor, uint32_t duration_ms) override {
        // Vibration API support is limited in browsers
        return EM_ASM_INT({
            var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            var gp = gamepads[$0];
            if (gp && gp.vibrationActuator) {
                gp.vibrationActuator.playEffect('dual-rumble', {
                    startDelay: 0,
                    duration: $3,
                    weakMagnitude: $1,
                    strongMagnitude: $2
                });
                return 1;
            }
            return 0;
        }, index, left_motor, right_motor, duration_ms) != 0;
    }

    void stop_vibration() override {
        EM_ASM({
            var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            var gp = gamepads[$0];
            if (gp && gp.vibrationActuator) {
                gp.vibrationActuator.reset();
            }
        }, index);
    }

    bool get_battery_info(float* level, bool* charging) const override {
        // Battery info not available via standard Gamepad API
        if (level) *level = 1.0f;
        if (charging) *charging = false;
        return false;
    }
};

//=============================================================================
// Gamepad Manager
//=============================================================================

static WasmGamepad g_gamepads[MAX_GAMEPADS];
static bool g_initialized = false;

static EM_BOOL gamepad_connected_callback(int event_type, const EmscriptenGamepadEvent* event, void* user_data) {
    (void)user_data;

    if (event->index >= 0 && event->index < MAX_GAMEPADS) {
        WasmGamepad& gp = g_gamepads[event->index];
        gp.index = event->index;
        gp.connected = (event_type == EMSCRIPTEN_EVENT_GAMEPADCONNECTED);

        if (gp.connected) {
            strncpy(gp.name, event->id, MAX_GAMEPAD_NAME_LENGTH - 1);
            gp.name[MAX_GAMEPAD_NAME_LENGTH - 1] = '\0';
            snprintf(gp.id, sizeof(gp.id), "wasm_gamepad_%d", event->index);
            printf("Gamepad connected: %s\n", gp.name);
        } else {
            printf("Gamepad disconnected: %s\n", gp.name);
        }
    }

    return EM_TRUE;
}

int enumerate_gamepads(GamepadEnumeration* out_gamepads) {
    if (!out_gamepads) return 0;

    memset(out_gamepads, 0, sizeof(GamepadEnumeration));

    // Initialize event listeners on first call
    if (!g_initialized) {
        emscripten_set_gamepadconnected_callback(nullptr, EM_TRUE, gamepad_connected_callback);
        emscripten_set_gamepaddisconnected_callback(nullptr, EM_TRUE, gamepad_connected_callback);

        // Initialize gamepad array
        for (int i = 0; i < MAX_GAMEPADS; i++) {
            g_gamepads[i].index = i;
            g_gamepads[i].connected = false;
        }

        g_initialized = true;
    }

    // Query current gamepad state
    int count = EM_ASM_INT({
        var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
        var count = 0;
        for (var i = 0; i < gamepads.length && i < $0; i++) {
            if (gamepads[i] && gamepads[i].connected) {
                count++;
            }
        }
        return count;
    }, MAX_GAMEPADS);

    // Update gamepad info
    for (int i = 0; i < MAX_GAMEPADS && out_gamepads->gamepad_count < MAX_GAMEPADS; i++) {
        bool connected = EM_ASM_INT({
            var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
            var gp = gamepads[$0];
            return (gp && gp.connected) ? 1 : 0;
        }, i) != 0;

        if (connected) {
            g_gamepads[i].connected = true;
            g_gamepads[i].index = i;

            // Get name
            char* name = (char*)EM_ASM_PTR({
                var gamepads = navigator.getGamepads ? navigator.getGamepads() : [];
                var gp = gamepads[$0];
                if (gp) {
                    var name = gp.id || "Unknown Gamepad";
                    var len = lengthBytesUTF8(name) + 1;
                    var ptr = _malloc(len);
                    stringToUTF8(name, ptr, len);
                    return ptr;
                }
                return 0;
            }, i);

            if (name) {
                strncpy(g_gamepads[i].name, name, MAX_GAMEPAD_NAME_LENGTH - 1);
                g_gamepads[i].name[MAX_GAMEPAD_NAME_LENGTH - 1] = '\0';
                free(name);
            }

            snprintf(g_gamepads[i].id, sizeof(g_gamepads[i].id), "wasm_gamepad_%d", i);

            GamepadInfo& info = out_gamepads->gamepads[out_gamepads->gamepad_count];
            strncpy(info.name, g_gamepads[i].name, MAX_GAMEPAD_NAME_LENGTH - 1);
            info.name[MAX_GAMEPAD_NAME_LENGTH - 1] = '\0';
            strncpy(info.id, g_gamepads[i].id, sizeof(info.id) - 1);
            info.id[sizeof(info.id) - 1] = '\0';
            info.index = i;
            info.is_connected = true;
            info.has_rumble = true;  // Assume true, may not work on all browsers

            out_gamepads->gamepad_count++;
        } else {
            g_gamepads[i].connected = false;
        }
    }

    return out_gamepads->gamepad_count;
}

IGamepadDevice* get_gamepad_device(int index) {
    if (index < 0 || index >= MAX_GAMEPADS) return nullptr;
    if (!g_gamepads[index].connected) return nullptr;
    return &g_gamepads[index];
}

void poll_gamepads() {
    // In WASM, polling happens automatically via the browser
    // This function can be used to trigger a state update
    for (int i = 0; i < MAX_GAMEPADS; i++) {
        if (g_gamepads[i].connected) {
            GamepadState state;
            g_gamepads[i].poll(&state);
        }
    }
}

} // namespace input
} // namespace window

#endif // WINDOW_PLATFORM_WASM
