/*
 * d3d11.cpp - Direct3D 11 window example (Windows only)
 */

#include "window.hpp"

#ifdef WINDOW_PLATFORM_WIN32

#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cmath>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

int main() {
    window::Config config;
    config.windows[0].title = "Direct3D 11 Example";
    config.windows[0].width = 800;
    config.windows[0].height = 600;
    config.backend = window::Backend::D3D11;

    window::Result result;
    auto windows = window::Window::create(config, &result);

    if (result != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();

    printf("Direct3D 11 context created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());

    // Get native D3D11 handles
    ID3D11Device* device = static_cast<ID3D11Device*>(gfx->native_device());
    IDXGISwapChain1* swap_chain = static_cast<IDXGISwapChain1*>(gfx->native_swapchain());

    // Get device context
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);

    // Create render target view
    ID3D11Texture2D* back_buffer = nullptr;
    swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));

    ID3D11RenderTargetView* rtv = nullptr;
    device->CreateRenderTargetView(back_buffer, nullptr, &rtv);
    back_buffer->Release();

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

        // Present
        swap_chain->Present(1, 0);

        time += 0.016f;
    }

    // Cleanup
    if (rtv) rtv->Release();
    if (context) context->Release();

    win->destroy();
    return 0;
}

#else

int main() {
    printf("D3D11 example is only available on Windows.\n");
    return 0;
}

#endif
