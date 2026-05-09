# Window & Config

The `Config` struct is the single source of truth for window creation. It
holds graphics settings (shared across all windows in the same call) plus an
array of per-window entries.

```cpp
struct Config {                                 // window.hpp
    Backend  backend     = Backend::Auto;
    int      device_index = -1;
    SwapMode swap_mode    = SwapMode::Auto;
    bool     vsync        = true;
    int      samples      = 1;
    int      back_buffers = 2;
    int      color_bits   = 32;
    int      depth_bits   = 24;
    int      stencil_bits = 8;
    Graphics* shared_graphics = nullptr;

    WindowConfigEntry windows[MAX_CONFIG_WINDOWS];
    int               window_count = 1;
};
```

Field-by-field meaning: see comments in [`window.hpp`](../window.hpp).

## Single window

```cpp
window::Config config;
config.windows[0].title  = "App";
config.windows[0].width  = 1280;
config.windows[0].height = 720;

auto windows = window::Window::create(config, nullptr);
auto* win = windows.front();
```

## Multiple windows, shared context

Set `window_count` and fill more entries. All windows created in one
`create()` call share the same graphics device, so resources (textures,
buffers) made on one are visible on the others.

```cpp
window::Config config;
config.window_count = 2;
config.backend = window::Backend::D3D12;

config.windows[0].name   = "main";
config.windows[0].title  = "Main";
config.windows[0].width  = 1280;
config.windows[0].height = 720;

config.windows[1].name           = "tools";
config.windows[1].title          = "Tools";
config.windows[1].monitor_index  = 1;     // open on the second monitor
config.windows[1].width          = 480;
config.windows[1].height         = 800;

auto windows = window::Window::create(config, nullptr);
window::Window* main_win  = windows[0];
window::Window* tools_win = windows[1];

// One event loop, both windows.
while (!main_win->should_close()) {
    for (auto* w : windows) w->poll_events();
}
```

`config.find_window("main")` looks up an entry by `name` — useful when the
config came from disk (see [Config files](config-files.md)).

## Window styles

```cpp
enum class WindowStyle : uint32_t {
    None, TitleBar, Border, CloseButton, MinimizeButton, MaximizeButton,
    Resizable, Fullscreen, AlwaysOnTop, ToolWindow,
    Default = TitleBar | Border | CloseButton |
              MinimizeButton | MaximizeButton | Resizable
};
```

`WindowStyle` is a flag enum — combine with `|`. Common patterns:

```cpp
// Borderless splash
config.windows[0].style = window::WindowStyle::None;

// Tool palette: stays above the main window
config.windows[0].style = window::WindowStyle::TitleBar
                        | window::WindowStyle::CloseButton
                        | window::WindowStyle::AlwaysOnTop
                        | window::WindowStyle::ToolWindow;
```

You can change the style after creation: `win->set_style(...)`.

## Position & monitor

`x = -1` and `y = -1` mean "let the OS decide" (typically centred on
`monitor_index`). `monitor_index = 0` is the primary monitor.

To enumerate available monitors before deciding, see
[`graphics_api.hpp`](../graphics_api.hpp): `enumerate_monitors()` returns a
list of `MonitorInfo` with display modes.

## Fullscreen

```cpp
config.windows[0].fullscreen = true;        // borderless fullscreen
// or after creation:
win->set_fullscreen(true);
win->set_fullscreen(false);                  // back to windowed
```

For exclusive-mode fullscreen at a specific resolution, find the desired
`DisplayMode` first via `enumerate_monitors` / `find_display_mode`, set the
window size to match, then call `set_fullscreen(true)`.

## DPI

Windows report **physical pixels** through `get_width()` / `get_height()` and
the host scale through `get_dpi_scale()`:

```cpp
int  px      = win->get_width();
float scale  = win->get_dpi_scale();        // 1.0, 1.25, 1.5, 2.0, ...
int  logical = static_cast<int>(px / scale);
```

Use `get_dpi()` for the integer DPI value (96, 120, 144, 192, ...). DPI can
change at runtime when a window is dragged between monitors with different
scaling — listen for the resize callback (see *Events* below).

## Events

`poll_events()` drives both polling and dispatching. There are two ways to
react to events:

### Polling (simple cases)

```cpp
while (!win->should_close()) {
    win->poll_events();
    if (win->is_key_down(window::Key::Escape)) break;

    int mx, my;
    win->get_mouse_position(&mx, &my);
}
```

### Handlers (preferred)

Implement a handler interface and register it. Handlers form a priority chain
— return `true` to consume the event, `false` to pass it on.

```cpp
class MyKeys : public window::input::IKeyboardHandler {
    const char* get_handler_id() const override { return "mykeys"; }
    bool on_key(const window::KeyEvent& e) override {
        if (e.type == window::EventType::KeyDown && e.key == window::Key::F11) {
            // toggle fullscreen ...
            return true;                     // consumed
        }
        return false;
    }
};

MyKeys keys;
win->add_keyboard_handler(&keys);
```

The full input story is in [Input](input.md). Mouse, gamepad, wheel, and
char-input handlers follow the same `add_*_handler` / `remove_*_handler`
pattern.

A close callback fires when the user clicks the close button:

```cpp
win->set_close_callback([](window::Window* w) {
    // Return false to ignore the request and stay open.
    return show_save_prompt();
});
```

## Native handles

When you need to call into the platform directly:

```cpp
void* hwnd      = win->native_handle();      // HWND on Win32, NSWindow on macOS, etc.
void* display   = win->native_display();     // X11 Display*, wl_display*, ... (Linux)
```

Pair these with `Graphics::native_device()` / `native_swapchain()` for
backend-specific rendering — see [Graphics](graphics.md).

## Cursor

```cpp
win->set_cursor(window::CursorType::Hand);
win->set_cursor(window::CursorType::Hidden);
```

The available cursors are listed in
[`window.hpp`](../window.hpp) — `enum class CursorType`.

## Public API summary

```cpp
class Window {
    // factory
    static std::vector<Window*> create(const Config&, Result*);
    static std::vector<Window*> create_from_config(const char* path, Result*);
    void   destroy();

    // visibility / lifecycle
    void   show(); void hide();
    bool   is_visible() const;
    bool   should_close() const;
    void   set_close_callback(WindowCloseCallback);

    // events
    void   poll_events();

    // geometry / DPI
    int    get_width() const;  int  get_height() const;
    void   set_size(int, int);
    bool   supports_position() const;
    void   get_position(int*, int*); void set_position(int, int);
    int    get_dpi() const; float get_dpi_scale() const;

    // style & state
    void   set_style(WindowStyle);
    void   set_fullscreen(bool); bool is_fullscreen() const;
    void   set_always_on_top(bool);
    void   set_title(const char*);
    void   set_cursor(CursorType);

    // input (polling)
    bool   is_key_down(Key) const;
    bool   is_mouse_button_down(MouseButton) const;
    void   get_mouse_position(int*, int*) const;

    // input (handlers)
    void   add_keyboard_handler(input::IKeyboardHandler*);
    void   add_mouse_handler(input::IMouseHandler*);
    // ... remove_* counterparts ...

    // graphics + native
    Graphics* graphics() const;
    void*  native_handle() const;
    void*  native_display() const;
};
```
