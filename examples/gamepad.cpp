/*
 * gamepad.cpp - Gamepad input example
 *
 * Demonstrates how to use the GamepadManager for game controller input.
 * Shows both event-driven and polling-based input handling.
 * Also demonstrates force feedback (vibration) support.
 *
 * Connect an Xbox controller (or compatible gamepad) and run this example.
 * Press buttons and move sticks to see events.
 * Press A to test vibration, B to stop vibration.
 * Press Start+Back to exit.
 */

#include "window.hpp"
#include "input/input_gamepad.hpp"
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
// Example Gamepad Handler
//=============================================================================

class ExampleGamepadHandler : public IGamepadHandler {
public:
    GamepadManager* gamepad_mgr = nullptr;

    const char* get_handler_id() const override {
        return "example_handler";
    }

    int get_priority() const override {
        return 0;
    }

    bool on_button(const GamepadButtonEvent& event) override {
        const char* action = (event.type == GamepadEventType::ButtonDown) ? "pressed" : "released";
        printf("Gamepad %d: Button %s %s\n",
               event.gamepad_index,
               gamepad_button_to_string(event.button),
               action);

        // Check for exit condition (Start + Back)
        if (event.type == GamepadEventType::ButtonDown) {
            if (event.button == GamepadButton::Start) {
                start_pressed_ = true;
            } else if (event.button == GamepadButton::Back) {
                back_pressed_ = true;
            }

            // Test vibration with A button
            if (event.button == GamepadButton::A && gamepad_mgr) {
                printf("  -> Testing vibration (left=0.5, right=1.0)\n");
                gamepad_mgr->set_vibration(event.gamepad_index, 0.5f, 1.0f);
            }

            // Stop vibration with B button
            if (event.button == GamepadButton::B && gamepad_mgr) {
                printf("  -> Stopping vibration\n");
                gamepad_mgr->stop_vibration(event.gamepad_index);
            }

            // Test stronger vibration with X button
            if (event.button == GamepadButton::X && gamepad_mgr) {
                printf("  -> Testing strong vibration (left=1.0, right=1.0)\n");
                gamepad_mgr->set_vibration(event.gamepad_index, 1.0f, 1.0f);
            }

            // Test light vibration with Y button
            if (event.button == GamepadButton::Y && gamepad_mgr) {
                printf("  -> Testing light vibration (left=0.2, right=0.2)\n");
                gamepad_mgr->set_vibration(event.gamepad_index, 0.2f, 0.2f);
            }
        } else {
            if (event.button == GamepadButton::Start) {
                start_pressed_ = false;
            } else if (event.button == GamepadButton::Back) {
                back_pressed_ = false;
            }
        }

        if (start_pressed_ && back_pressed_) {
            should_exit_ = true;
        }

        return false; // Don't consume the event
    }

    bool on_axis(const GamepadAxisEvent& event) override {
        // Only print if value is significant
        if (std::abs(event.value) > 0.01f || std::abs(event.delta) > 0.1f) {
            printf("Gamepad %d: Axis %s = %.3f (delta: %.3f)\n",
                   event.gamepad_index,
                   gamepad_axis_to_string(event.axis),
                   event.value,
                   event.delta);
        }
        return false;
    }

    void on_connection(const GamepadConnectionEvent& event) override {
        if (event.connected) {
            printf("Gamepad %d connected: %s\n",
                   event.gamepad_index,
                   event.name ? event.name : "Unknown");

            // Query force feedback capabilities
            if (gamepad_mgr) {
                ForceFeedbackCaps caps;
                if (gamepad_mgr->get_force_feedback_caps(event.gamepad_index, &caps)) {
                    printf("  Force feedback: %s\n", caps.supported ? "Yes" : "No");
                    if (caps.supported) {
                        printf("    Left motor: %s, Right motor: %s\n",
                               caps.has_left_motor ? "Yes" : "No",
                               caps.has_right_motor ? "Yes" : "No");
                        printf("    Trigger rumble: %s\n",
                               caps.has_trigger_rumble ? "Yes" : "No");
                        printf("    Advanced effects: %s\n",
                               caps.has_advanced_effects ? "Yes" : "No");
                    }
                }
            }
        } else {
            printf("Gamepad %d disconnected\n", event.gamepad_index);
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
    printf("=== Gamepad Input Example ===\n");
    printf("Connect a gamepad to see input events.\n");
    printf("\n");
    printf("Controls:\n");
    printf("  A      - Test medium vibration\n");
    printf("  B      - Stop vibration\n");
    printf("  X      - Test strong vibration\n");
    printf("  Y      - Test light vibration\n");
    printf("  Start + Back - Exit\n");
    printf("\n");

    // Create the gamepad manager
    GamepadManager* gamepad = GamepadManager::create();
    if (!gamepad) {
        printf("Error: Failed to create GamepadManager\n");
        return 1;
    }

    // Create and register our event handler
    ExampleGamepadHandler handler;
    handler.gamepad_mgr = gamepad;  // Give handler access for vibration control
    gamepad->add_handler(&handler);

    // Set deadzone (default is 0.1)
    gamepad->set_deadzone(0.15f);
    printf("Deadzone set to: %.2f\n\n", gamepad->get_deadzone());

    // Main loop
    printf("Waiting for input (polling at ~60Hz)...\n\n");

    while (!handler.should_exit()) {
        // Poll gamepads - this detects connections and dispatches events
        gamepad->update();

        // Example: Direct state queries (polling-based input)
        // This is useful for game-style continuous input
        if (gamepad->is_connected(0)) {
            // Check if A button is held (for continuous actions like jumping)
            // if (gamepad->is_button_down(0, GamepadButton::A)) {
            //     // Player is holding jump
            // }

            // Get stick values for movement
            // float move_x = gamepad->get_axis(0, GamepadAxis::LeftX);
            // float move_y = gamepad->get_axis(0, GamepadAxis::LeftY);
            // Apply movement...
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

    // Stop any vibration before exiting
    for (int i = 0; i < 8; i++) {
        if (gamepad->is_connected(i)) {
            gamepad->stop_vibration(i);
        }
    }

    // Clean up
    gamepad->remove_handler(&handler);
    gamepad->destroy();

    return 0;
}
