# Input

There are two complementary styles for every input device:

- **Polling** — ask the window/manager "is button X down right now?"
- **Handlers** — implement an interface, get callbacks for events.

Polling is convenient for simple games. Handlers compose better — multiple
systems can listen, and each can choose to consume or pass an event.

## Keyboard

### Polling

```cpp
if (win->is_key_down(window::Key::Space)) jump();
if (win->is_key_down(window::Key::W))     move_forward();
```

`enum class Key` in [`window.hpp`](../window.hpp) covers letters, digits,
function keys, modifiers (`Shift`, `Control`, `Alt`, `Super`, with `L`/`R`
variants), navigation, numpad, and punctuation.

### Handlers

```cpp
class Hotkeys : public window::input::IKeyboardHandler {
    const char* get_handler_id() const override { return "hotkeys"; }
    int         get_priority()  const override { return 10; }   // higher = earlier

    bool on_key(const window::KeyEvent& e) override {
        if (e.type != window::EventType::KeyDown) return false;
        if (e.key == window::Key::F11) { toggle_fullscreen(); return true; }
        if ((e.modifiers & window::KeyMod::Control) != window::KeyMod::None &&
             e.key == window::Key::Q) { quit(); return true; }
        return false;
    }

    bool on_char(const window::CharEvent& e) override {
        // Unicode code-point text input — use this for typing, not on_key.
        text_buffer.append_codepoint(e.codepoint);
        return true;
    }
};

Hotkeys hk;
win->add_keyboard_handler(&hk);
```

`KeyEvent` carries `key`, `modifiers` (bitmask), `scancode`, and a `repeat`
flag. `CharEvent` carries a Unicode `codepoint` — that's what you want for
text editors; do not try to translate `Key` codes to characters yourself.

Useful helpers in [`input/input_keyboard.hpp`](../input/input_keyboard.hpp):
`key_to_string`, `string_to_key`, `is_modifier_key`, `is_function_key`,
`key_to_keymod` (gives the matching `KeyMod` flag for a modifier `Key`).

## Mouse

### Polling

```cpp
int x, y; win->get_mouse_position(&x, &y);
if (win->is_mouse_button_down(window::MouseButton::Left)) drag(x, y);
```

### Events

```cpp
class Picker : public window::input::IMouseHandler {
    const char* get_handler_id() const override { return "picker"; }

    bool on_mouse_button(const window::MouseButtonEvent& e) override {
        if (e.type == window::EventType::MouseButtonDown &&
            e.button == window::MouseButton::Left) {
            select_at(e.x, e.y);
            return true;
        }
        return false;
    }

    bool on_mouse_move(const window::MouseMoveEvent& e) override {
        hover_at(e.x, e.y);
        return false;
    }

    bool on_mouse_wheel(const window::MouseWheelEvent& e) override {
        zoom(e.dy);
        return true;
    }
};
```

`MouseButton` is `Left`, `Right`, `Middle`, `X1`, `X2`. `MouseMoveEvent` has
both absolute (`x`, `y`) and relative (`dx`, `dy`) deltas. `MouseWheelEvent`
deltas are floats (some hardware reports sub-tick scroll).

## Gamepad

The gamepad API lives outside the window because controllers are global to
the process. Create a `GamepadManager`, call `update()` once per frame, and
either poll or attach a handler.

```cpp
auto* pads = window::input::GamepadManager::create();

while (running) {
    win->poll_events();
    pads->update();                       // poll devices, dispatch events

    // Polling
    if (pads->is_connected(0)) {
        if (pads->is_button_down(0, window::input::GamepadButton::A)) jump();
        float lx = pads->get_axis(0, window::input::GamepadAxis::LeftX);
        float lt = pads->get_axis(0, window::input::GamepadAxis::LeftTrigger);
    }
}
pads->destroy();
```

Buttons: `A B X Y`, `LeftBumper RightBumper`, `Back Start Guide`, `LeftStick
RightStick` (clicks), `DPadUp/Down/Left/Right`. Axes: `LeftX/Y`, `RightX/Y`,
`LeftTrigger`, `RightTrigger` (triggers are `0..1`, sticks are `-1..1`).

### Deadzone

```cpp
pads->set_deadzone(0.15f);                // applied to stick axes
```

### Force feedback

```cpp
if (pads->supports_force_feedback(0)) {
    pads->set_vibration(0, /*left*/0.6f, /*right*/0.6f);
    // or scheduled effects:
    window::input::ForceFeedbackEffect e;
    e.left_motor  = 0.4f;
    e.right_motor = 0.8f;
    e.duration_ms = 250;
    auto h = pads->play_effect(0, e);
    // ... pads->stop_effect(0, h);
}
```

`set_trigger_vibration(...)` works on Xbox One+ pads.

### Events

```cpp
class PadHandler : public window::input::IGamepadHandler {
    const char* get_handler_id() const override { return "pad"; }

    bool on_button(const window::input::GamepadButtonEvent& e) override {
        if (e.pressed && e.button == window::input::GamepadButton::Start) pause();
        return false;
    }
    void on_connection(const window::input::GamepadConnectionEvent& e) override {
        printf("pad %d %s\n", e.index, e.connected ? "in" : "out");
    }
};
PadHandler ph;
pads->add_handler(&ph);
```

## Steering wheel

`WheelManager` is symmetric to `GamepadManager` but exposes wheel-specific
axes (steering, throttle, brake, clutch, handbrake), gear selection, paddle
shifters, and richer force-feedback effects (constant force, spring, damper,
periodic, kerb, road-rumble). See
[`input/input_wheel.hpp`](../input/input_wheel.hpp) and
[`examples/wheel.cpp`](../examples/wheel.cpp).

```cpp
auto* wheels = window::input::WheelManager::create();
wheels->update();
if (wheels->is_connected(0)) {
    float steer = wheels->get_steering(0);   // -1..1
    float throt = wheels->get_throttle(0);   //  0..1

    wheels->set_ff_autocenter(0, true, 0.6f);
    wheels->set_spring_force(0, /*strength*/0.4f, /*center*/0.0f);
}
wheels->destroy();
```

`WheelCaps` (`get_capabilities`) tells you the rotation range (e.g. 900°) and
which inputs the device exposes — branch on it before assuming a clutch
exists.

## Handler priorities

All `IXxxHandler::get_priority()` defaults to `0`. Higher values run first.
Returning `true` from a handler stops propagation; `false` continues to the
next handler. This lets you layer "system hotkeys → modal dialog → gameplay"
without coupling them.
