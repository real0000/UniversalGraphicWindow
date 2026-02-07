/*
 * vulkan.cpp - Vulkan window example
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>

#if defined(WINDOW_PLATFORM_WIN32) || defined(WINDOW_PLATFORM_X11) || defined(WINDOW_PLATFORM_WAYLAND) || defined(WINDOW_PLATFORM_MACOS)

#include <vulkan/vulkan.h>

int main() {
    // Create window with Vulkan backend
    window::Config config;
    config.windows[0].title = "Vulkan Example";
    config.windows[0].width = 800;
    config.windows[0].height = 600;
    config.backend = window::Backend::Vulkan;

    window::Result result;
    auto windows = window::Window::create(config, &result);

    if (result != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();

    printf("Vulkan context created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());

    // Get native Vulkan handles
    VkDevice device = static_cast<VkDevice>(gfx->native_device());
    VkSwapchainKHR swapchain = static_cast<VkSwapchainKHR>(gfx->native_swapchain());

    printf("VkDevice: %p\n", (void*)device);
    printf("VkSwapchainKHR: %p\n", (void*)swapchain);

    // Main loop
    while (!win->should_close()) {
        win->poll_events();

        // Use native Vulkan handles for rendering
        // The library creates device, swapchain, etc.
        // User is responsible for command buffers, render passes, etc.
    }

    win->destroy();
    printf("Vulkan resources cleaned up.\n");
    return 0;
}

#else

int main() {
    printf("Vulkan example is not available on this platform.\n");
    return 0;
}

#endif
