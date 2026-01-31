/*
 * events.cpp - Event handling example
 * Demonstrates mouse, keyboard, and window event callbacks using handlers
 */

#include "window.hpp"
#include "input/input_keyboard.hpp"
#include "input/input_mouse.hpp"
#include <cstdio>
#include <cstring>

using namespace window;
using namespace window::input;

//=============================================================================
// Keyboard Handler
//=============================================================================

class ExampleKeyboardHandler : public IKeyboardHandler {
public:
    Window* window = nullptr;

    const char* get_handler_id() const override {
        return "example_keyboard";
    }

    bool on_key(const KeyEvent& event) override {
        const char* action = event.type == EventType::KeyDown ? "pressed" :
                             event.type == EventType::KeyUp ? "released" : "repeated";
        printf("Key %s: %s (scancode: %d)", action, window::key_to_string(event.key), event.scancode);

        if (has_mod(event.modifiers, KeyMod::Shift)) printf(" +Shift");
        if (has_mod(event.modifiers, KeyMod::Control)) printf(" +Ctrl");
        if (has_mod(event.modifiers, KeyMod::Alt)) printf(" +Alt");
        if (has_mod(event.modifiers, KeyMod::Super)) printf(" +Super");
        printf("\n");

        // Press Escape to close
        if (event.key == Key::Escape && event.type == EventType::KeyDown) {
            if (window) window->set_should_close(true);
        }

        // Press F11 to toggle fullscreen
        if (event.key == Key::F11 && event.type == EventType::KeyDown) {
            if (window) window->set_fullscreen(!window->is_fullscreen());
        }

        return false; // Don't consume the event
    }

    bool on_char(const CharEvent& event) override {
        if (event.codepoint < 128 && event.codepoint >= 32) {
            printf("Character: '%c' (U+%04X)\n", (char)event.codepoint, event.codepoint);
        } else {
            printf("Character: U+%04X\n", event.codepoint);
        }
        return false;
    }
};

//=============================================================================
// Mouse Handler
//=============================================================================

class ExampleMouseHandler : public IMouseHandler {
public:
    const char* get_handler_id() const override {
        return "example_mouse";
    }

    bool on_mouse_move(const MouseMoveEvent& event) override {
        // Only print occasionally to avoid spam
        static int counter = 0;
        if (++counter % 10 == 0) {
            printf("Mouse move: (%d, %d) delta: (%d, %d)\n", event.x, event.y, event.dx, event.dy);
        }
        return false;
    }

    bool on_mouse_button(const MouseButtonEvent& event) override {
        const char* action = event.type == EventType::MouseDown ? "pressed" : "released";
        printf("Mouse %s %s at (%d, %d)", window::mouse_button_to_string(event.button), action, event.x, event.y);
        if (event.clicks > 1) {
            printf(" [%d clicks]", event.clicks);
        }
        printf("\n");
        return false;
    }

    bool on_mouse_wheel(const MouseWheelEvent& event) override {
        printf("Mouse scroll: (%.2f, %.2f) at (%d, %d)\n", event.dx, event.dy, event.x, event.y);
        return false;
    }
};

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("Event Handling Example\n");
    printf("======================\n");
    printf("Press Escape to quit\n");
    printf("Press F11 to toggle fullscreen\n");
    printf("Try: clicking, scrolling, typing, resizing, dragging files\n\n");

    Config config;
    strncpy(config.windows[0].title, "Event Handling Example", MAX_DEVICE_NAME_LENGTH - 1);
    config.windows[0].width = 800;
    config.windows[0].height = 600;

    Result result;
    Window* window = Window::create(config, &result);

    if (!window) {
        printf("Failed to create window: %s\n", result_to_string(result));
        return 1;
    }

    printf("Window created with %s backend\n\n", window->graphics()->get_backend_name());

    // Create and register handlers
    ExampleKeyboardHandler keyboard_handler;
    keyboard_handler.window = window;
    window->add_keyboard_handler(&keyboard_handler);

    ExampleMouseHandler mouse_handler;
    window->add_mouse_handler(&mouse_handler);

    // Set up window event callbacks using std::function (lambdas can capture context)
    window->set_resize_callback([](const WindowResizeEvent& event) {
        printf("Window resized: %dx%d%s\n", event.width, event.height,
               event.minimized ? " (minimized)" : "");
    });

    window->set_move_callback([](const WindowMoveEvent& event) {
        printf("Window moved: (%d, %d)\n", event.x, event.y);
    });

    window->set_focus_callback([](const WindowFocusEvent& event) {
        printf("Window %s focus\n", event.focused ? "gained" : "lost");
    });

    window->set_state_callback([](const WindowStateEvent& event) {
        const char* state = event.minimized ? "minimized" :
                            event.maximized ? "maximized" : "restored";
        printf("Window %s\n", state);
    });

    window->set_close_callback([](const WindowCloseEvent& event) {
        printf("Window close requested\n");
    });

    window->set_drop_file_callback([](const DropFileEvent& event) {
        printf("Files dropped (%d):\n", event.count);
        for (int i = 0; i < event.count; i++) {
            printf("  %s\n", event.paths[i]);
        }
    });

    window->set_dpi_change_callback([](const DpiChangeEvent& event) {
        printf("DPI changed: %d (scale: %.2f)\n", event.dpi, event.scale);
    });

    // Main loop
    while (!window->should_close()) {
        window->poll_events();

        // Example of polling input state (alternative to callbacks)
        if (window->is_key_down(Key::W)) {
            // Move forward in a game
        }
        if (window->is_mouse_button_down(MouseButton::Left)) {
            // Shooting or selecting
        }

        // Clear and present
        window->graphics()->present();
    }

    printf("\nWindow closed\n");

    // Clean up handlers
    window->remove_keyboard_handler(&keyboard_handler);
    window->remove_mouse_handler(&mouse_handler);

    window->destroy();

    return 0;
}
