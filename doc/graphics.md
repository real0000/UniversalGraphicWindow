# Graphics

The library provides a *thin* abstraction: it creates the window, the swap
chain, and a graphics device for the chosen backend, then hands you the
native handles. You write the actual rendering with your backend's API.

## Picking a backend

```cpp
enum class Backend { Auto, OpenGL, Vulkan, D3D11, D3D12, Metal };
```

`Backend::Auto` chooses the platform default (`get_default_backend()`).
`is_backend_supported(b)` reports whether the build has a given backend
compiled in. `backend_to_string(b)` gives a printable name.

A typical fallback chain (also used by the project wizard):

| Platform | Priority                                  |
|----------|-------------------------------------------|
| Windows  | D3D12 → Vulkan → D3D11 → OpenGL           |
| Apple    | Metal → Vulkan (MoltenVK) → OpenGL/ES     |
| Linux    | Vulkan → OpenGL                           |
| Android  | Vulkan → OpenGL ES                        |

```cpp
for (window::Backend b : chain) {
    config.backend = b;
    auto windows = window::Window::create(config, &result);
    if (result == window::Result::Success && !windows.empty()) {
        // success — break out
        break;
    }
}
```

## Picking a GPU

`enumerate_devices()` in [`graphics_api.hpp`](../graphics_api.hpp) lists the
available adapters for a backend, with VRAM, vendor, and a default flag:

```cpp
window::DeviceEnumeration devices;
int n = window::enumerate_devices(window::Backend::D3D12, &devices);
for (int i = 0; i < n; ++i) {
    const auto& d = devices.devices[i];
    printf("[%d] %s — %llu MB%s\n",
           d.device_index, d.name.c_str(),
           (unsigned long long)(d.dedicated_video_memory >> 20),
           d.is_default ? " (default)" : "");
}

// Select the second adapter:
config.device_index = 1;                     // or set device_name
```

## Swap mode

```cpp
enum class SwapMode { Fifo, FifoRelaxed, Mailbox, Immediate, Auto };
```

| Mode          | VSync          | Tearing | Latency  |
|---------------|----------------|---------|----------|
| Fifo          | on             | no      | medium   |
| FifoRelaxed   | adaptive       | maybe   | medium   |
| Mailbox       | triple-buffer  | no      | low      |
| Immediate     | off            | yes     | lowest   |
| Auto          | derived from `config.vsync` |  |          |

```cpp
config.swap_mode = window::SwapMode::Mailbox;
```

If the requested mode isn't supported, the implementation falls back to the
nearest available one — query `get_capabilities()` afterwards if you need to
know what you actually got.

## Sample count, depth/stencil

`samples` controls MSAA on the default render target (1, 2, 4, 8, ...).
`color_bits`, `depth_bits`, `stencil_bits` control the swap-chain format.
Defaults are `32 / 24 / 8` — i.e. 8-bit BGRA with D24S8.

## The `Graphics` object

```cpp
window::Graphics* gfx = win->graphics();

const char* backend_name = gfx->get_backend_name();
const char* device_name  = gfx->get_device_name();
gfx->resize(new_w, new_h);
gfx->present();                              // submit + flip
```

For OpenGL, call `gfx->make_current()` whenever you switch to a window from
another thread (or from a different window in a multi-window app).

## Native handles

The library hands you the platform/backend objects directly so you can use
your favourite rendering code:

```cpp
// Windows + D3D11
auto* device   = static_cast<ID3D11Device*>(gfx->native_device());
auto* context  = static_cast<ID3D11DeviceContext*>(gfx->native_context());
auto* swap     = static_cast<IDXGISwapChain*>(gfx->native_swapchain());

// Vulkan
auto* phys     = static_cast<VkPhysicalDevice*>(gfx->native_device());
auto* dev      = static_cast<VkDevice*>(gfx->native_context());
auto* swap     = static_cast<VkSwapchainKHR*>(gfx->native_swapchain());

// Metal
auto* metalDev = (id<MTLDevice>)gfx->native_device();
auto* layer    = (CAMetalLayer*)gfx->native_swapchain();
```

The exact native types per backend are documented in
[`graphics_api.hpp`](../graphics_api.hpp) above the `Graphics` class. The
example files show working pipelines:
[`examples/d3d11.cpp`](../examples/d3d11.cpp),
[`examples/d3d12.cpp`](../examples/d3d12.cpp),
[`examples/vulkan.cpp`](../examples/vulkan.cpp),
[`examples/opengl.cpp`](../examples/opengl.cpp),
[`examples/metal.mm`](../examples/metal.mm).

## Capabilities

`get_capabilities(GraphicsCapabilities*)` fills out a struct describing the
device's limits and feature flags (max texture size, max samples, sRGB
support, compute support, BC/ETC/ASTC compression support, ...). Useful for
guarding optional rendering paths.

## Headless / external windows

If you already have a window from somewhere else (a host app, a UI toolkit),
use `Graphics::create(const ExternalWindowConfig&, Result*)` to attach a
graphics device to that native handle without going through `Window`. See
[`examples/external_window.cpp`](../examples/external_window.cpp).

## Texture formats

`enum class TextureFormat` in [`graphics_api.hpp`](../graphics_api.hpp)
covers everything the supported backends agree on — from `R8_UNORM` and
`RGBA8_UNORM_SRGB` to BC, ETC2/EAC and ASTC families plus depth/stencil
combinations. Helpers:

```cpp
const char* texture_format_to_string(TextureFormat);
int  texture_format_bytes_per_pixel(TextureFormat);     // 0 for compressed
bool texture_format_is_compressed(TextureFormat);
bool texture_format_is_depth_stencil(TextureFormat);
```
