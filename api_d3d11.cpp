/*
 * api_d3d11.cpp - Direct3D 11 graphics implementation
 */

#include "window.hpp"

#if defined(_WIN32) && !defined(WINDOW_NO_D3D11)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace window {

//=============================================================================
// Format Conversion
//=============================================================================

static DXGI_FORMAT get_dxgi_format(int r, int g, int b, int a) {
    if (r == 8 && g == 8 && b == 8 && a == 8) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if (r == 10 && g == 10 && b == 10 && a == 2) return DXGI_FORMAT_R10G10B10A2_UNORM;
    if (r == 16 && g == 16 && b == 16 && a == 16) return DXGI_FORMAT_R16G16B16A16_FLOAT;
    return DXGI_FORMAT_R8G8B8A8_UNORM;
}

//=============================================================================
// D3D11 Graphics Implementation
//=============================================================================

class GraphicsD3D11 : public Graphics {
public:
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain1* swap_chain = nullptr;
    std::string device_name;
    bool owns_device = true;

    ~GraphicsD3D11() override {
        if (swap_chain) swap_chain->Release();
        if (context) context->Release();
        if (device && owns_device) device->Release();
    }

    Backend get_backend() const override { return Backend::D3D11; }
    const char* get_backend_name() const override { return "Direct3D 11"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    void* native_device() const override { return device; }
    void* native_context() const override { return context; }
    void* native_swapchain() const override { return swap_chain; }
};

//=============================================================================
// Creation for HWND (Win32)
//=============================================================================

Graphics* create_d3d11_graphics_hwnd(void* hwnd, const Config& config) {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool owns_device = true;

    // Check for shared device
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::D3D11) {
        device = static_cast<ID3D11Device*>(config.shared_graphics->native_device());
        device->AddRef();
        device->GetImmediateContext(&context);
        owns_device = false;
    } else {
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                        feature_levels, _countof(feature_levels), D3D11_SDK_VERSION,
                                        &device, nullptr, &context);
        if (FAILED(result)) return nullptr;
    }

    IDXGIDevice* dxgi_device;
    device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
    IDXGIAdapter* adapter;
    dxgi_device->GetAdapter(&adapter);
    IDXGIFactory2* factory;
    adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&factory);

    DXGI_ADAPTER_DESC adapter_desc;
    adapter->GetDesc(&adapter_desc);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = config.width;
    sd.Height = config.height;
    sd.Format = get_dxgi_format(config.red_bits, config.green_bits, config.blue_bits, config.alpha_bits);
    sd.SampleDesc.Count = config.samples;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* swap_chain;
    HRESULT hr = factory->CreateSwapChainForHwnd(device, static_cast<HWND>(hwnd), &sd, nullptr, nullptr, &swap_chain);

    factory->Release();
    adapter->Release();
    dxgi_device->Release();

    if (FAILED(hr)) {
        context->Release();
        device->Release();
        return nullptr;
    }

    GraphicsD3D11* gfx = new GraphicsD3D11();
    gfx->device = device;
    gfx->context = context;
    gfx->swap_chain = swap_chain;
    gfx->owns_device = owns_device;

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gfx->device_name = name;

    return gfx;
}

//=============================================================================
// Creation for CoreWindow (UWP)
//=============================================================================

#if defined(WINDOW_PLATFORM_UWP)

#include <dxgi1_4.h>

Graphics* create_d3d11_graphics_corewindow(void* core_window, int width, int height, const Config& config) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

    ID3D11Device* device;
    ID3D11DeviceContext* context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                    feature_levels, _countof(feature_levels), D3D11_SDK_VERSION,
                                    &device, nullptr, &context);
    if (FAILED(hr)) return nullptr;

    IDXGIDevice3* dxgi_device;
    device->QueryInterface(__uuidof(IDXGIDevice3), (void**)&dxgi_device);
    IDXGIAdapter* adapter;
    dxgi_device->GetAdapter(&adapter);
    IDXGIFactory4* factory;
    adapter->GetParent(__uuidof(IDXGIFactory4), (void**)&factory);

    DXGI_ADAPTER_DESC adapter_desc;
    adapter->GetDesc(&adapter_desc);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = get_dxgi_format(config.red_bits, config.green_bits, config.blue_bits, config.alpha_bits);
    sd.Stereo = FALSE;
    sd.SampleDesc.Count = config.samples;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.Flags = 0;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1* swap_chain;
    hr = factory->CreateSwapChainForCoreWindow(device, static_cast<IUnknown*>(core_window),
                                                &sd, nullptr, &swap_chain);

    factory->Release();
    adapter->Release();
    dxgi_device->Release();

    if (FAILED(hr)) {
        context->Release();
        device->Release();
        return nullptr;
    }

    GraphicsD3D11* gfx = new GraphicsD3D11();
    gfx->device = device;
    gfx->context = context;
    gfx->swap_chain = swap_chain;

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gfx->device_name = name;

    return gfx;
}

#endif // WINDOW_PLATFORM_UWP

} // namespace window

#endif // _WIN32 && !WINDOW_NO_D3D11
