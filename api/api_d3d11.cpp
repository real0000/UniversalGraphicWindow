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

static DXGI_FORMAT get_dxgi_format(int color_bits) {
    // color_bits: 16 = R5G6B5, 24/32 = R8G8B8A8, 64 = R16G16B16A16 (HDR)
    if (color_bits >= 64) return DXGI_FORMAT_R16G16B16A16_FLOAT;  // 64-bit HDR
    if (color_bits >= 32) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if (color_bits >= 24) return DXGI_FORMAT_R8G8B8A8_UNORM;  // No 24-bit in DXGI
    return DXGI_FORMAT_B5G6R5_UNORM;  // 16-bit
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
    SwapMode swap_mode = SwapMode::Fifo;
    bool allow_tearing = false;

    ~GraphicsD3D11() override {
        if (swap_chain) swap_chain->Release();
        if (context) context->Release();
        if (device && owns_device) device->Release();
    }

    Backend get_backend() const override { return Backend::D3D11; }
    const char* get_backend_name() const override { return "Direct3D 11"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        if (!swap_chain || width <= 0 || height <= 0) return false;

        // Release all references to back buffer before resizing
        context->ClearState();
        context->Flush();

        HRESULT hr = swap_chain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height),
                                                DXGI_FORMAT_UNKNOWN, 0);
        return SUCCEEDED(hr);
    }

    void present() override {
        if (swap_chain) {
            UINT sync_interval = 1;
            UINT flags = 0;
            switch (swap_mode) {
                case SwapMode::Immediate:
                    sync_interval = 0;
                    if (allow_tearing) flags = DXGI_PRESENT_ALLOW_TEARING;
                    break;
                case SwapMode::Mailbox:
                    // Mailbox-like behavior: vsync but drop frames if too fast
                    sync_interval = 1;
                    break;
                case SwapMode::FifoRelaxed:
                    // Present immediately if we missed vsync
                    sync_interval = 1;
                    break;
                case SwapMode::Fifo:
                case SwapMode::Auto:
                default:
                    sync_interval = 1;
                    break;
            }
            swap_chain->Present(sync_interval, flags);
        }
    }

    void make_current() override {
        // D3D11 doesn't have a "make current" concept like OpenGL
        // Context is always bound to the device
    }

    void* native_device() const override { return device; }
    void* native_context() const override { return context; }
    void* native_swapchain() const override { return swap_chain; }
};

//=============================================================================
// Creation for HWND (Win32)
//=============================================================================

// Helper to resolve swap mode from config
static SwapMode resolve_swap_mode(const Config& config) {
    if (config.swap_mode != SwapMode::Auto) {
        return config.swap_mode;
    }
    return config.vsync ? SwapMode::Fifo : SwapMode::Immediate;
}

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

    // Check for tearing support (required for true immediate mode)
    bool allow_tearing = false;
    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&factory5))) {
        BOOL tearing_support = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_support, sizeof(tearing_support)))) {
            allow_tearing = (tearing_support == TRUE);
        }
        factory5->Release();
    }

    SwapMode swap_mode = resolve_swap_mode(config);

    const WindowConfigEntry& win_cfg = config.windows[0];
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = win_cfg.width;
    sd.Height = win_cfg.height;
    sd.Format = get_dxgi_format(config.color_bits);
    sd.SampleDesc.Count = config.samples;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = (allow_tearing && swap_mode == SwapMode::Immediate) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

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
    gfx->swap_mode = swap_mode;
    gfx->allow_tearing = allow_tearing && (swap_mode == SwapMode::Immediate);

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

    SwapMode swap_mode = resolve_swap_mode(config);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = get_dxgi_format(config.color_bits);
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
    gfx->swap_mode = swap_mode;
    gfx->allow_tearing = false;  // UWP doesn't support tearing

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gfx->device_name = name;

    return gfx;
}

#endif // WINDOW_PLATFORM_UWP

} // namespace window

#endif // _WIN32 && !WINDOW_NO_D3D11
