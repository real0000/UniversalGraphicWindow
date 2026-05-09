# GUI

The GUI module is a retained-mode widget tree that produces backend-agnostic
draw lists. The library generates the geometry; *you* draw it with your own
renderer, using the native handles from [Graphics](graphics.md).

## What's in the box

Headers under [`gui/`](../gui/):

| Header             | What it adds                                          |
|--------------------|-------------------------------------------------------|
| `gui.hpp`          | Core types — `IGuiWidget`, `WidgetRenderInfo`, enums. |
| `gui_context.hpp`  | `IGuiContext` factory, theme.                         |
| `gui_controls.hpp` | Buttons, sliders, checkboxes, radio.                  |
| `gui_label.hpp`    | Labels, multi-line text, text inputs.                 |
| `gui_panel.hpp`    | Panels (rect with border + title).                    |
| `gui_sizer.hpp`    | Layout managers (box, grid, flow).                    |
| `gui_scroll.hpp`   | Scroll views.                                         |
| `gui_list.hpp`     | List boxes.                                           |
| `gui_tree.hpp`     | Tree views.                                           |
| `gui_tab.hpp`      | Tab controls.                                         |
| `gui_menu.hpp`     | Menus, context menus.                                 |
| `gui_toolbar.hpp`  | Toolbars.                                             |
| `gui_dialog.hpp`   | Modal dialogs and popups.                             |
| `gui_property.hpp` | Property grids.                                       |
| `gui_animation.hpp`| Animated values, easing, timelines.                   |
| `gui_serialization.hpp` | Save / load widget trees.                        |

Everything is in the `window::gui` namespace.

## Anatomy of a widget

`IGuiWidget` (in [`gui/gui.hpp`](../gui/gui.hpp)) is the base interface every
control implements. It has four responsibilities:

1. **Hierarchy** — `add_child`, `get_parent`, `get_child(i)`.
2. **Geometry & layout** — `set_bounds`, `set_min_size`, `set_alignment`,
   `set_size_mode`, `layout_children`.
3. **State** — `is_visible`, `is_enabled`, `get_state` (`Normal`, `Hovered`,
   `Pressed`, `Focused`, `Disabled`).
4. **Input** — `handle_mouse_move`, `handle_mouse_button`, `handle_key`,
   `hit_test`. Returning `true` consumes the event.

You normally don't implement `IGuiWidget` directly — call the factory on the
`IGuiContext` to get a concrete control.

## Building a tree

```cpp
#include "gui/gui.hpp"
#include "gui/gui_context.hpp"
#include "gui/gui_controls.hpp"
#include "gui/gui_sizer.hpp"

using namespace window::gui;

IGuiContext* ctx = IGuiContext::create();             // factory + theme

auto* root   = ctx->create_panel();
auto* sizer  = ctx->create_box_sizer(LayoutDirection::Vertical);
root->set_layout(sizer);

auto* button = ctx->create_button("Click me");
button->set_on_click([]() { puts("clicked"); });
sizer->add(button);

auto* slider = ctx->create_slider(0.0f, 1.0f, 0.5f);
slider->set_on_change([](float v) { volume = v; });
sizer->add(slider);
```

Constructor names mirror the factory methods (`create_button`,
`create_label`, `create_slider`, `create_checkbox`, `create_text_input`,
`create_list`, `create_tree`, `create_tab`, ...).

## Driving it from a window

The GUI doesn't own input or rendering — you wire it up to a `Window`:

```cpp
class GuiBridge : public window::input::IMouseHandler,
                  public window::input::IKeyboardHandler {
    IGuiWidget* root;
public:
    explicit GuiBridge(IGuiWidget* r) : root(r) {}

    bool on_mouse_move(const window::MouseMoveEvent& e) override {
        return root->handle_mouse_move({(float)e.x, (float)e.y});
    }
    bool on_mouse_button(const window::MouseButtonEvent& e) override {
        return root->handle_mouse_button(
            e.button,
            e.type == window::EventType::MouseButtonDown,
            {(float)e.x, (float)e.y});
    }
    bool on_key(const window::KeyEvent& e) override {
        return root->handle_key(
            (int)e.key,
            e.type == window::EventType::KeyDown,
            (int)e.modifiers);
    }
    // identifiers omitted for brevity
};

GuiBridge bridge(root);
win->add_mouse_handler(&bridge);
win->add_keyboard_handler(&bridge);
```

## Rendering

Each frame, you ask the root widget for its render info, and it returns
component pools you can iterate:

```cpp
const WidgetRenderInfo& info = root->get_render_info(win);

for (const DrawRef& d : info.get_draw_order()) {
    switch (d.kind) {
    case DrawKind::Color:   draw_rect(info.colors[d.index]);    break;
    case DrawKind::Texture: draw_quad(info.textures[d.index]);  break;
    case DrawKind::Text:    draw_text(info.texts[d.index]);     break;
    case DrawKind::Slice9:  draw_slice9(info.slices[d.index]);  break;
    }
}
```

`get_render_info()` is dirty-tracked — if nothing changed since the last
call, it returns the same buffers without rebuilding. Call `mark_dirty()` if
you mutate widget state through means other than the public API and need a
re-layout.

For batching:

```cpp
for (const BatchSpan& span : info.get_batches()) {
    bind_material(span.material);
    issue_draws(span);
}
```

A complete renderer for one backend is in
[`examples/gui.cpp`](../examples/gui.cpp); serialization round-trip is in
[`examples/gui_serialization.cpp`](../examples/gui_serialization.cpp).

## Layout

Layout is handled by sizers, not by absolute positioning. Each child has a
`SizeMode` per axis:

| Mode      | Meaning                                              |
|-----------|------------------------------------------------------|
| `Fixed`   | Use `min_size`.                                      |
| `Relative`| Fraction of the parent.                              |
| `Auto`    | Shrink-to-fit content.                               |
| `Fill`    | Take whatever space is left.                         |

Plus `Alignment` (9-way: TopLeft … BottomRight) for how a smaller child sits
inside a larger slot.

Built-in sizers: box (horizontal/vertical), grid, flow, stack. They live in
[`gui/gui_sizer.hpp`](../gui/gui_sizer.hpp).

## Animation

```cpp
#include "gui/gui_animation.hpp"

AnimatedFloat opacity(0.0f);
opacity.animate_to(1.0f, /*duration*/ 0.3f, Easing::EaseOutCubic);

// per frame:
opacity.update(dt);
panel->set_opacity(opacity.value());
```

Timelines, sequences, and parallel groups are in the same header. Use them
for tooltips, fades, slide-in panels, etc.

## Serialization

```cpp
#include "gui/gui_serialization.hpp"

save_widget_tree(root, "ui.json");
auto* loaded = load_widget_tree(ctx, "ui.json");
```

Useful for editor tooling — see [`examples/gui_serialization.cpp`](../examples/gui_serialization.cpp).

## Why this shape?

- **No GPU code in the GUI** — it produces draw lists, you render. That keeps
  the GUI usable on any backend (D3D11, Vulkan, Metal, …) and any engine.
- **Retained-mode** — widgets remember their state across frames, so input
  routing, focus, and animation work without you re-issuing the tree each
  frame.
- **Component pools** — render commands live in flat arrays sorted by depth,
  which makes batching trivial.
