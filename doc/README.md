# UniversalGraphicWindow — API Tutorial

Task-oriented tour of the public C++ API. Each page walks through a slice of
the library with runnable snippets, then links to the headers where the full
reference lives.

## Pages

1. [Getting started](getting-started.md) — your first window in ~20 lines.
2. [Window & Config](window.md) — single and multi-window apps, styles, DPI, native handles.
3. [Graphics](graphics.md) — backends, swap modes, device enumeration, presentation.
4. [Input](input.md) — keyboard, mouse, gamepad, steering wheel.
5. [Audio](audio.md) — streams, clips, file streaming, the player, effects.
6. [GUI](gui.md) — widget tree, layout, rendering, hit testing.
7. [Config files](config-files.md) — INI persistence for graphics and audio.

## Headers at a glance

| Subsystem | Header                                                |
|-----------|-------------------------------------------------------|
| Window    | [`window.hpp`](../window.hpp)                         |
| Graphics  | [`graphics_api.hpp`](../graphics_api.hpp)             |
| Audio     | [`audio/audio.hpp`](../audio/audio.hpp)               |
| GUI       | [`gui/gui.hpp`](../gui/gui.hpp)                       |
| Keyboard  | [`input/input_keyboard.hpp`](../input/input_keyboard.hpp) |
| Mouse     | [`input/input_mouse.hpp`](../input/input_mouse.hpp)   |
| Gamepad   | [`input/input_gamepad.hpp`](../input/input_gamepad.hpp) |
| Wheel     | [`input/input_wheel.hpp`](../input/input_wheel.hpp)   |

All public symbols live in the `window` namespace (audio in `window::audio`,
GUI in `window::gui`, input in `window::input`).

## Conventions

- **Lifecycle.** Every system uses `create()` / `destroy()`. There are no
  virtual destructors on the public side — never `delete` an interface
  pointer; call its `destroy()` method.
- **Result codes.** Most factories take a `Result*` out parameter and also
  return `nullptr`/empty on failure. Check both. `result_to_string()` returns
  a printable name.
- **Pointers, not references.** Out parameters are pointers so call sites stay
  obvious.
- **No hidden globals.** Each window owns its own graphics; audio is
  initialised explicitly via `AudioManager::initialize`.
