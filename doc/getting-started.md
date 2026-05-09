# Getting started

The smallest useful program: open a window, get a graphics context, run an
event loop until the user closes it.

```cpp
#include "window.hpp"
#include <cstdio>

int main() {
    window::Config config;
    config.windows[0].title  = "Hello";
    config.windows[0].width  = 800;
    config.windows[0].height = 600;
    config.vsync = true;

    window::Result result;
    auto windows = window::Window::create(config, &result);
    if (result != window::Result::Success || windows.empty()) {
        printf("create failed: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();
    printf("Backend: %s, Device: %s\n",
           gfx->get_backend_name(), gfx->get_device_name());

    while (!win->should_close()) {
        win->poll_events();
        // ... render with native handles from gfx ...
    }
    win->destroy();
    return 0;
}
```

Compile against the `window` CMake target:

```cmake
add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE window)
```

See [`examples/basic.cpp`](../examples/basic.cpp) for the runnable version.

## What just happened

1. **`Config`** describes everything you want — graphics settings (`backend`,
   `vsync`, `samples`, depth/stencil bits) and per-window settings in
   `config.windows[i]`. Defaults are sensible for desktop apps.
2. **`Window::create(config, &result)`** returns a `std::vector<Window*>`. It
   returns a vector because one config can produce multiple windows that share
   a graphics context — see [Window & Config](window.md). The single-window
   case is just `windows[0]`.
3. **`win->graphics()`** gives you a [`Graphics`](graphics.md) object. The
   library does not draw for you. You ask it for native handles
   (`native_device()`, `native_swapchain()`) and render with backend-specific
   code, then call `gfx->present()` (or your own present, depending on
   backend).
4. **`win->poll_events()`** drains the OS event queue, dispatches input to
   handlers, and updates `should_close()`.
5. **`win->destroy()`** releases the OS window and the graphics resources it
   owns. Don't `delete` it.

## Backend fallback

`Backend::Auto` picks the platform default (D3D12 on Windows, Metal on Apple,
Vulkan on Linux/Android, OpenGL elsewhere). To prefer something specific with
a graceful fallback chain:

```cpp
const window::Backend chain[] = {
#ifdef WINDOW_PLATFORM_WIN32
    window::Backend::D3D12, window::Backend::Vulkan,
    window::Backend::D3D11, window::Backend::OpenGL,
#elif defined(WINDOW_PLATFORM_MACOS)
    window::Backend::Metal, window::Backend::Vulkan, window::Backend::OpenGL,
#else
    window::Backend::Vulkan, window::Backend::OpenGL,
#endif
};

std::vector<window::Window*> windows;
for (window::Backend b : chain) {
    config.backend = b;
    windows = window::Window::create(config, &result);
    if (result == window::Result::Success && !windows.empty()) break;
}
```

The wizard at [`setup/wizard.ps1`](../setup/wizard.ps1) /
[`setup/wizard.sh`](../setup/wizard.sh) generates a project with this pattern
already wired up.

## Where to go next

- More than one window, custom styles, fullscreen, DPI: [Window & Config](window.md).
- Picking a GPU, querying capabilities, presenting: [Graphics](graphics.md).
- Reacting to keyboard/mouse: [Input](input.md).
