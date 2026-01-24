/*
 * basic.cpp - Basic window example with unified graphics API
 */

#include "window.hpp"
#include <cstdio>
#include <cmath>

int main() {
    // Simple configuration
    window::Config config;
    config.title = "Window Example";
    config.width = 800;
    config.height = 600;
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

    float time = 0.0f;

    while (!win->should_close()) {
        win->poll_events();

        // Begin frame
        if (!gfx->begin_frame()) {
            continue;
        }

        // Animate background color
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;

        // Begin render pass with clear
        window::RenderTargetDesc rt;
        rt.clear_color = window::Color(r * 0.3f, g * 0.3f, b * 0.3f);
        rt.color_load = window::LoadOp::Clear;
        gfx->begin_pass(rt);

        // Set viewport
        gfx->set_viewport(window::Rect(0, 0, win->get_width(), win->get_height()));

        // ... render geometry here ...

        gfx->end_pass();

        // End frame (presents to screen)
        gfx->end_frame();

        time += 0.016f;
    }

    printf("Closing...\n");
    win->destroy();

    return 0;
}
