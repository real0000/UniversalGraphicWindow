/*
 * wheel.cpp - Steering wheel input example
 *
 * Demonstrates how to use the WheelManager for steering wheel input.
 * Shows both event-driven and polling-based input handling.
 * Also demonstrates force feedback support.
 *
 * Connect a steering wheel (Logitech G29, Thrustmaster, etc.) and run this example.
 * Move the wheel, press pedals, and use buttons to see events.
 * Press paddle shifters to test force feedback effects.
 * Press Start+Back to exit.
 */

#include "window.hpp"
#include "input/input_wheel.hpp"
#include <cstdio>
#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#endif

using namespace window;
using namespace window::input;

//=============================================================================
// Example Wheel Handler
//=============================================================================

class ExampleWheelHandler : public IWheelHandler {
public:
    WheelManager* wheel_mgr = nullptr;

    const char* get_handler_id() const override {
        return "example_wheel_handler";
    }

    int get_priority() const override {
        return 0;
    }

    bool on_button(const WheelButtonEvent& event) override {
        const char* action = (event.type == WheelEventType::ButtonDown) ? "pressed" : "released";
        printf("Wheel %d: Button %s %s\n",
               event.wheel_index,
               wheel_button_to_string(event.button),
               action);

        // Check for exit condition (Start + Back)
        if (event.type == WheelEventType::ButtonDown) {
            if (event.button == WheelButton::Start) {
                start_pressed_ = true;
            } else if (event.button == WheelButton::Back) {
                back_pressed_ = true;
            }

            // Test constant force with paddle up
            if (event.button == WheelButton::PaddleShiftUp && wheel_mgr) {
                printf("  -> Testing constant force (right)\n");
                wheel_mgr->set_constant_force(event.wheel_index, 0.5f);
            }

            // Test constant force with paddle down
            if (event.button == WheelButton::PaddleShiftDown && wheel_mgr) {
                printf("  -> Testing constant force (left)\n");
                wheel_mgr->set_constant_force(event.wheel_index, -0.5f);
            }

            // Test spring force with Button1
            if (event.button == WheelButton::Button1 && wheel_mgr) {
                printf("  -> Testing spring force (center)\n");
                wheel_mgr->set_spring_force(event.wheel_index, 0.7f, 0.0f);
            }

            // Test damper force with Button2
            if (event.button == WheelButton::Button2 && wheel_mgr) {
                printf("  -> Testing damper force\n");
                wheel_mgr->set_damper_force(event.wheel_index, 0.5f);
            }

            // Test sine wave with Button3
            if (event.button == WheelButton::Button3 && wheel_mgr) {
                printf("  -> Testing sine wave vibration (20 Hz)\n");
                wheel_mgr->set_sine_effect(event.wheel_index, 0.5f, 20.0f);
            }

            // Stop all forces with Button4
            if (event.button == WheelButton::Button4 && wheel_mgr) {
                printf("  -> Stopping all forces\n");
                wheel_mgr->stop_all_forces(event.wheel_index);
            }

            // Test DPad for directional force
            if (event.button == WheelButton::DPadLeft && wheel_mgr) {
                printf("  -> Force feedback: strong left\n");
                wheel_mgr->set_constant_force(event.wheel_index, -0.8f);
            }
            if (event.button == WheelButton::DPadRight && wheel_mgr) {
                printf("  -> Force feedback: strong right\n");
                wheel_mgr->set_constant_force(event.wheel_index, 0.8f);
            }
        } else {
            if (event.button == WheelButton::Start) {
                start_pressed_ = false;
            } else if (event.button == WheelButton::Back) {
                back_pressed_ = false;
            }

            // Release constant force when paddle is released
            if ((event.button == WheelButton::PaddleShiftUp ||
                 event.button == WheelButton::PaddleShiftDown ||
                 event.button == WheelButton::DPadLeft ||
                 event.button == WheelButton::DPadRight) && wheel_mgr) {
                wheel_mgr->set_constant_force(event.wheel_index, 0.0f);
            }
        }

        if (start_pressed_ && back_pressed_) {
            should_exit_ = true;
        }

        return false; // Don't consume the event
    }

    bool on_axis(const WheelAxisEvent& event) override {
        // Only print if value is significant
        if (std::abs(event.delta) > 0.01f) {
            printf("Wheel %d: Axis %s = %.3f (delta: %.3f)\n",
                   event.wheel_index,
                   wheel_axis_to_string(event.axis),
                   event.value,
                   event.delta);
        }
        return false;
    }

    bool on_gear(const WheelGearEvent& event) override {
        printf("Wheel %d: Gear changed from %s to %s\n",
               event.wheel_index,
               gear_position_to_string(event.previous_gear),
               gear_position_to_string(event.gear));
        return false;
    }

    void on_connection(const WheelConnectionEvent& event) override {
        if (event.connected) {
            printf("Wheel %d connected: %s\n",
                   event.wheel_index,
                   event.name ? event.name : "Unknown");

            // Query capabilities
            if (wheel_mgr) {
                WheelCaps caps;
                if (wheel_mgr->get_capabilities(event.wheel_index, &caps)) {
                    printf("  Capabilities:\n");
                    printf("    Rotation: %.0f degrees (%.0f to %.0f)\n",
                           caps.rotation_degrees, caps.min_rotation, caps.max_rotation);
                    printf("    Pedals: Throttle=%s, Brake=%s, Clutch=%s, Handbrake=%s\n",
                           caps.has_throttle ? "Yes" : "No",
                           caps.has_brake ? "Yes" : "No",
                           caps.has_clutch ? "Yes" : "No",
                           caps.has_handbrake ? "Yes" : "No");
                    printf("    Combined pedals: %s\n", caps.combined_pedals ? "Yes" : "No");
                    printf("    Shifter: Paddles=%s, H-pattern=%s (%d gears), Sequential=%s\n",
                           caps.has_paddle_shifters ? "Yes" : "No",
                           caps.has_h_shifter ? "Yes" : "No",
                           caps.h_shifter_gears,
                           caps.has_sequential_shifter ? "Yes" : "No");
                    printf("    Force feedback: %s\n", caps.has_force_feedback ? "Yes" : "No");
                    if (caps.has_force_feedback) {
                        printf("      Max effects: %d\n", caps.max_ff_effects);
                        if (caps.max_ff_torque_nm > 0) {
                            printf("      Max torque: %.1f Nm\n", caps.max_ff_torque_nm);
                        }
                    }
                    printf("    Buttons: %d, Axes: %d\n", caps.num_buttons, caps.num_axes);
                }

                // Enable default spring force for self-centering
                if (wheel_mgr->supports_force_feedback(event.wheel_index)) {
                    printf("  Enabling default spring force...\n");
                    wheel_mgr->set_spring_force(event.wheel_index, 0.3f, 0.0f);
                }
            }
        } else {
            printf("Wheel %d disconnected\n", event.wheel_index);
        }
    }

    bool should_exit() const { return should_exit_; }

private:
    bool start_pressed_ = false;
    bool back_pressed_ = false;
    bool should_exit_ = false;
};

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== Steering Wheel Input Example ===\n");
    printf("Connect a steering wheel to see input events.\n");
    printf("\n");
    printf("Controls:\n");
    printf("  Paddle Up     - Test constant force (right)\n");
    printf("  Paddle Down   - Test constant force (left)\n");
    printf("  D-Pad L/R     - Test strong directional force\n");
    printf("  Button 1      - Test spring force (self-centering)\n");
    printf("  Button 2      - Test damper force (resistance)\n");
    printf("  Button 3      - Test sine wave vibration\n");
    printf("  Button 4      - Stop all forces\n");
    printf("  Start + Back  - Exit\n");
    printf("\n");

    // Create the wheel manager
    WheelManager* wheel = WheelManager::create();
    if (!wheel) {
        printf("Error: Failed to create WheelManager\n");
        return 1;
    }

    // Create and register our event handler
    ExampleWheelHandler handler;
    handler.wheel_mgr = wheel;
    wheel->add_handler(&handler);

    // Set deadzone (default is 0.02)
    wheel->set_deadzone(0.02f);
    printf("Deadzone set to: %.2f\n\n", wheel->get_deadzone());

    // Main loop
    printf("Waiting for input (polling at ~60Hz)...\n\n");

    while (!handler.should_exit()) {
        // Poll wheels - this detects connections and dispatches events
        wheel->update();

        // Example: Direct state queries (polling-based input)
        if (wheel->is_connected(0)) {
            // Get steering value for direct control
            // float steering = wheel->get_steering(0);
            // float throttle = wheel->get_throttle(0);
            // float brake = wheel->get_brake(0);
            // GearPosition gear = wheel->get_gear(0);
            // Apply to game...
        }

        // Sleep to simulate ~60Hz polling
#ifdef _WIN32
        Sleep(16);
#else
        struct timespec ts = { 0, 16000000 }; // 16ms
        nanosleep(&ts, nullptr);
#endif
    }

    printf("\nExiting...\n");

    // Stop any force feedback before exiting
    for (int i = 0; i < MAX_WHEELS; i++) {
        if (wheel->is_connected(i)) {
            wheel->stop_all_forces(i);
        }
    }

    // Clean up
    wheel->remove_handler(&handler);
    wheel->destroy();

    return 0;
}
