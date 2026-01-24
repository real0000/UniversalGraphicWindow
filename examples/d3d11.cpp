/*
 * d3d11.cpp - Direct3D 11 window example (Windows only)
 */

#include "window.hpp"

#ifdef WINDOW_PLATFORM_WIN32

#include <d3d11.h>
#include <cstdio>
#include <cmath>

int main() {
    window::Config config;
    config.title = "Direct3D 11 Example";
    config.width = 800;
    config.height = 600;
    config.graphics_api = window::GraphicsAPI::D3D11;
    config.d3d.debug_layer = true;

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    const window::GraphicsContext* ctx = win->get_graphics_context();
    ID3D11Device* device = static_cast<ID3D11Device*>(ctx->d3d11.device);
    ID3D11DeviceContext* context = static_cast<ID3D11DeviceContext*>(ctx->d3d11.device_context);
    ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(ctx->d3d11.render_target);

    printf("Direct3D 11 context created!\n");

    D3D_FEATURE_LEVEL feature_level = device->GetFeatureLevel();
    printf("Feature Level: %x\n", feature_level);

    float time = 0.0f;

    while (!win->should_close()) {
        win->poll_events();

        // Animate clear color
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;
        float clear_color[4] = { r * 0.3f, g * 0.3f, b * 0.3f, 1.0f };

        context->ClearRenderTargetView(rtv, clear_color);

        win->present();
        time += 0.016f;
    }

    win->destroy();
    return 0;
}

#else

int main() {
    printf("D3D11 example is only available on Windows.\n");
    return 0;
}

#endif
