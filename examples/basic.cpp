/*
 * basic.cpp - Basic window example
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>

int main() {
    // Simple configuration
    window::Config config;
    strncpy(config.windows[0].title, "Window Example", window::MAX_DEVICE_NAME_LENGTH - 1);
    config.windows[0].width = 800;
    config.windows[0].height = 600;
    config.vsync = true;

    // Graphics backend selection - choose one:
    // config.backend = window::Backend::Auto;    // Default: platform's preferred backend
    // config.backend = window::Backend::OpenGL;  // Force OpenGL/OpenGL ES
    // config.backend = window::Backend::Vulkan;  // Force Vulkan
    // config.backend = window::Backend::D3D11;   // Force Direct3D 11 (Windows only)
    // config.backend = window::Backend::D3D12;   // Force Direct3D 12 (Windows only)
    // config.backend = window::Backend::Metal;   // Force Metal (Apple only)

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    // Get graphics context - works the same regardless of backend
    window::Graphics* gfx = win->graphics();

    printf("Window created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());
    printf("Size: %dx%d\n", win->get_width(), win->get_height());

    if (win->supports_position()) {
        int x, y;
        win->get_position(&x, &y);
        printf("Position: %d, %d\n", x, y);
    }

    // Main loop
    while (!win->should_close()) {
        win->poll_events();

        // Use native handles to render with backend-specific code
        // For example with D3D11:
        // ID3D11Device* device = (ID3D11Device*)gfx->native_device();
        // IDXGISwapChain* swapchain = (IDXGISwapChain*)gfx->native_swapchain();
    }

    printf("Closing...\n");
    win->destroy();

    return 0;
}
