/*
 * events.cpp - Event handling example
 * Demonstrates mouse, keyboard, and window event callbacks
 */

#include "window.hpp"
#include <cstdio>

using namespace window;

// Event callback functions
void on_key(const KeyEvent& event, void* user_data) {
    const char* action = event.type == EventType::KeyDown ? "pressed" :
                         event.type == EventType::KeyUp ? "released" : "repeated";
    printf("Key %s: %s (scancode: %d)", action, key_to_string(event.key), event.scancode);

    if (has_mod(event.modifiers, KeyMod::Shift)) printf(" +Shift");
    if (has_mod(event.modifiers, KeyMod::Control)) printf(" +Ctrl");
    if (has_mod(event.modifiers, KeyMod::Alt)) printf(" +Alt");
    if (has_mod(event.modifiers, KeyMod::Super)) printf(" +Super");
    printf("\n");

    // Press Escape to close
    if (event.key == Key::Escape && event.type == EventType::KeyDown) {
        event.window->set_should_close(true);
    }

    // Press F11 to toggle fullscreen
    if (event.key == Key::F11 && event.type == EventType::KeyDown) {
        event.window->set_fullscreen(!event.window->is_fullscreen());
    }
}

void on_char(const CharEvent& event, void* user_data) {
    if (event.codepoint < 128 && event.codepoint >= 32) {
        printf("Character: '%c' (U+%04X)\n", (char)event.codepoint, event.codepoint);
    } else {
        printf("Character: U+%04X\n", event.codepoint);
    }
}

void on_mouse_move(const MouseMoveEvent& event, void* user_data) {
    // Only print occasionally to avoid spam
    static int counter = 0;
    if (++counter % 10 == 0) {
        printf("Mouse move: (%d, %d) delta: (%d, %d)\n", event.x, event.y, event.dx, event.dy);
    }
}

void on_mouse_button(const MouseButtonEvent& event, void* user_data) {
    const char* action = event.type == EventType::MouseDown ? "pressed" : "released";
    printf("Mouse %s %s at (%d, %d)", mouse_button_to_string(event.button), action, event.x, event.y);
    if (event.clicks > 1) {
        printf(" [%d clicks]", event.clicks);
    }
    printf("\n");
}

void on_mouse_scroll(const MouseScrollEvent& event, void* user_data) {
    printf("Mouse scroll: (%.2f, %.2f) at (%d, %d)\n", event.dx, event.dy, event.x, event.y);
}

void on_mouse_cross(const MouseCrossEvent& event, void* user_data) {
    printf("Mouse %s window at (%d, %d)\n",
           event.entered ? "entered" : "left", event.x, event.y);
}

void on_resize(const WindowResizeEvent& event, void* user_data) {
    printf("Window resized: %dx%d%s\n", event.width, event.height,
           event.minimized ? " (minimized)" : "");
}

void on_move(const WindowMoveEvent& event, void* user_data) {
    printf("Window moved: (%d, %d)\n", event.x, event.y);
}

void on_focus(const WindowFocusEvent& event, void* user_data) {
    printf("Window %s focus\n", event.focused ? "gained" : "lost");
}

void on_state(const WindowStateEvent& event, void* user_data) {
    const char* state = event.minimized ? "minimized" :
                        event.maximized ? "maximized" : "restored";
    printf("Window %s\n", state);
}

void on_close(const WindowCloseEvent& event, void* user_data) {
    printf("Window close requested\n");
}

void on_drop_file(const DropFileEvent& event, void* user_data) {
    printf("Files dropped (%d):\n", event.count);
    for (int i = 0; i < event.count; i++) {
        printf("  %s\n", event.paths[i]);
    }
}

void on_dpi_change(const DpiChangeEvent& event, void* user_data) {
    printf("DPI changed: %d (scale: %.2f)\n", event.dpi, event.scale);
}

int main() {
    printf("Event Handling Example\n");
    printf("======================\n");
    printf("Press Escape to quit\n");
    printf("Press F11 to toggle fullscreen\n");
    printf("Try: clicking, scrolling, typing, resizing, dragging files\n\n");

    Config config;
    config.title = "Event Handling Example";
    config.width = 800;
    config.height = 600;

    Result result;
    Window* window = Window::create(config, &result);

    if (!window) {
        printf("Failed to create window: %s\n", result_to_string(result));
        return 1;
    }

    printf("Window created with %s backend\n\n", window->graphics()->get_backend_name());

    // Set up all event callbacks
    window->set_key_callback(on_key, nullptr);
    window->set_char_callback(on_char, nullptr);
    window->set_mouse_move_callback(on_mouse_move, nullptr);
    window->set_mouse_button_callback(on_mouse_button, nullptr);
    window->set_mouse_scroll_callback(on_mouse_scroll, nullptr);
    window->set_mouse_cross_callback(on_mouse_cross, nullptr);
    window->set_resize_callback(on_resize, nullptr);
    window->set_move_callback(on_move, nullptr);
    window->set_focus_callback(on_focus, nullptr);
    window->set_state_callback(on_state, nullptr);
    window->set_close_callback(on_close, nullptr);
    window->set_drop_file_callback(on_drop_file, nullptr);
    window->set_dpi_change_callback(on_dpi_change, nullptr);

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
    window->destroy();

    return 0;
}
