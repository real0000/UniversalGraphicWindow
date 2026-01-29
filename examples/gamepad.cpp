/*
 * gamepad.cpp - Gamepad input example
 *
 * Demonstrates how to use the GamepadManager for game controller input.
 * Shows both event-driven and polling-based input handling.
 *
 * Connect an Xbox controller (or compatible gamepad) and run this example.
 * Press buttons and move sticks to see events. Press Start+Back to exit.
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
    printf("Press Start + Back (Select) simultaneously to exit.\n");
    printf("\n");

    // Create the gamepad manager
    GamepadManager* gamepad = GamepadManager::create();
    if (!gamepad) {
        printf("Error: Failed to create GamepadManager\n");
        return 1;
    }

    // Create and register our event handler
    ExampleGamepadHandler handler;
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

    // Clean up
    gamepad->remove_handler(&handler);
    gamepad->destroy();

    return 0;
}
