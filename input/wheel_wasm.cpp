/*
 * wheel_wasm.cpp - Steering wheel stub for WebAssembly
 *
 * The HTML5 Gamepad API has limited support for steering wheels.
 * This is a stub implementation that treats wheels as gamepads.
 */

#ifdef WINDOW_PLATFORM_WASM

#include "input_wheel.hpp"
#include <cstring>

namespace window {
namespace input {

//=============================================================================
// WASM Wheel Implementation (Stub)
//=============================================================================

class WasmWheel : public IWheelDevice {
public:
    char name[MAX_WHEEL_NAME_LENGTH] = "No Wheel";
    char id[64] = "wasm_wheel";
    bool connected = false;

    const char* get_name() const override { return name; }
    const char* get_id() const override { return id; }
    bool is_connected() const override { return connected; }

    bool poll(WheelState* out_state) override {
        if (!out_state) return false;
        memset(out_state, 0, sizeof(WheelState));
        return false;
    }

    bool set_force_feedback(const ForceFeedbackEffect& effect) override {
        (void)effect;
        return false;
    }

    void stop_force_feedback() override {
        // No-op
    }

    bool get_capabilities(WheelCapabilities* caps) const override {
        if (!caps) return false;
        memset(caps, 0, sizeof(WheelCapabilities));
        return false;
    }
};

static WasmWheel g_wheel;

int enumerate_wheels(WheelEnumeration* out_wheels) {
    if (!out_wheels) return 0;
    memset(out_wheels, 0, sizeof(WheelEnumeration));
    // No steering wheel support in WASM
    return 0;
}

IWheelDevice* get_wheel_device(int index) {
    (void)index;
    return nullptr;
}

void poll_wheels() {
    // No-op
}

} // namespace input
} // namespace window

#endif // WINDOW_PLATFORM_WASM
