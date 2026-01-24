/*
 * external_window.cpp - Example of attaching graphics to an existing window
 *
 * This example demonstrates how to use the library with an external window
 * that you create yourself (e.g., from a GUI framework like Qt, wxWidgets, etc.)
 */

#include "window.hpp"
#include <cstdio>
#include <cmath>

#ifdef WINDOW_PLATFORM_WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Simple Win32 window creation for demonstration
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE: {
            // Get graphics context from window user data
            window::Graphics* gfx = reinterpret_cast<window::Graphics*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (gfx) {
                int width = LOWORD(lparam);
                int height = HIWORD(lparam);
                if (width > 0 && height > 0) {
                    gfx->resize(width, height);
                }
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

int main() {
    // Step 1: Create your own window (simulating external window from GUI framework)
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ExternalWindowClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        L"ExternalWindowClass",
        L"External Window Example",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        nullptr, nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!hwnd) {
        printf("Failed to create window\n");
        return 1;
    }

    // Get actual client area size
    RECT client_rect;
    GetClientRect(hwnd, &client_rect);
    int width = client_rect.right - client_rect.left;
    int height = client_rect.bottom - client_rect.top;

    // Step 2: Create graphics context for the external window
    window::ExternalWindowConfig config;
    config.native_handle = hwnd;
    config.width = width;
    config.height = height;
    config.vsync = true;
    config.backend = window::Backend::Auto;  // Or specify D3D11, OpenGL, etc.

    window::Result result;
    window::Graphics* gfx = window::Graphics::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create graphics context: %s\n", window::result_to_string(result));
        DestroyWindow(hwnd);
        return 1;
    }

    printf("External window graphics created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());

    // Store graphics pointer in window for resize handling
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gfx));

    // Show the window
    ShowWindow(hwnd, SW_SHOW);

    // Step 3: Main loop - handle your own events
    MSG msg;
    bool running = true;
    float time = 0.0f;

    while (running) {
        // Process Windows messages
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // Make context current (needed for OpenGL)
        gfx->make_current();

        // Your rendering code here...
        // For D3D11/D3D12, use native handles:
        //   ID3D11Device* device = (ID3D11Device*)gfx->native_device();
        //   IDXGISwapChain1* swapchain = (IDXGISwapChain1*)gfx->native_swapchain();
        //
        // For OpenGL:
        //   Just call OpenGL functions directly after make_current()

        // Present/swap buffers
        gfx->present();

        time += 0.016f;
    }

    // Step 4: Cleanup
    gfx->destroy();
    DestroyWindow(hwnd);
    UnregisterClassW(L"ExternalWindowClass", GetModuleHandleW(nullptr));

    printf("Cleaned up successfully.\n");
    return 0;
}

#else

int main() {
    printf("External window example is currently only implemented for Windows.\n");
    return 0;
}

#endif
