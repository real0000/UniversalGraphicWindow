/*
 * api_d3d12.cpp - Direct3D 12 graphics implementation
 */

#include "window.hpp"

#if defined(_WIN32) && !defined(WINDOW_NO_D3D12)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace window {

//=============================================================================
// Format Conversion
//=============================================================================

static DXGI_FORMAT get_dxgi_format(int color_bits) {
    // color_bits: 16 = R5G6B5, 24 = R8G8B8, 32 = R8G8B8A8
    if (color_bits >= 32) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if (color_bits >= 24) return DXGI_FORMAT_R8G8B8A8_UNORM;  // No 24-bit in DXGI
    return DXGI_FORMAT_B5G6R5_UNORM;  // 16-bit
}

//=============================================================================
// D3D12 Graphics Implementation
//=============================================================================

class GraphicsD3D12 : public Graphics {
public:
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* command_queue = nullptr;
    IDXGISwapChain4* swap_chain = nullptr;
    std::string device_name;
    bool owns_device = true;
    bool vsync = true;
    UINT buffer_count = 2;

    ~GraphicsD3D12() override {
        if (swap_chain) swap_chain->Release();
        if (command_queue) command_queue->Release();
        if (device && owns_device) device->Release();
    }

    Backend get_backend() const override { return Backend::D3D12; }
    const char* get_backend_name() const override { return "Direct3D 12"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        if (!swap_chain || width <= 0 || height <= 0) return false;

        // Note: Caller must ensure GPU is idle before resizing
        HRESULT hr = swap_chain->ResizeBuffers(buffer_count, static_cast<UINT>(width),
                                                static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
        return SUCCEEDED(hr);
    }

    void present() override {
        if (swap_chain) {
            swap_chain->Present(vsync ? 1 : 0, 0);
        }
    }

    void make_current() override {
        // D3D12 doesn't have a "make current" concept like OpenGL
    }

    void* native_device() const override { return device; }
    void* native_context() const override { return command_queue; }
    void* native_swapchain() const override { return swap_chain; }
};

//=============================================================================
// Creation for HWND (Win32)
//=============================================================================

Graphics* create_d3d12_graphics_hwnd(void* hwnd, const Config& config) {
    ID3D12Device* device = nullptr;
    bool owns_device = true;
    DXGI_ADAPTER_DESC1 adapter_desc = {};

    IDXGIFactory6* factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return nullptr;

    // Check for shared device
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::D3D12) {
        device = static_cast<ID3D12Device*>(config.shared_graphics->native_device());
        device->AddRef();
        owns_device = false;

        // Get adapter info from shared device
        LUID luid = device->GetAdapterLuid();
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (memcmp(&desc.AdapterLuid, &luid, sizeof(LUID)) == 0) {
                adapter_desc = desc;
                adapter->Release();
                break;
            }
            adapter->Release();
        }
    } else {
#ifdef _DEBUG
        ID3D12Debug* debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debug->Release();
        }
#endif

        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }
            if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                break;
            }
            adapter->Release();
            adapter = nullptr;
        }

        if (!adapter) {
            factory->Release();
            return nullptr;
        }

        adapter->GetDesc1(&adapter_desc);

        if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
            adapter->Release();
            factory->Release();
            return nullptr;
        }
        adapter->Release();
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ID3D12CommandQueue* command_queue;
    device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

    const WindowConfigEntry& win_cfg = config.windows[0];
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = win_cfg.width;
    sd.Height = win_cfg.height;
    sd.Format = get_dxgi_format(config.color_bits);
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    IDXGISwapChain1* swap_chain1;
    factory->CreateSwapChainForHwnd(command_queue, static_cast<HWND>(hwnd), &sd, nullptr, nullptr, &swap_chain1);
    factory->Release();

    IDXGISwapChain4* swap_chain;
    swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain));
    swap_chain1->Release();

    GraphicsD3D12* gfx = new GraphicsD3D12();
    gfx->device = device;
    gfx->command_queue = command_queue;
    gfx->swap_chain = swap_chain;
    gfx->owns_device = owns_device;
    gfx->vsync = config.vsync;
    gfx->buffer_count = static_cast<UINT>(config.back_buffers);

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gfx->device_name = name;

    return gfx;
}

//=============================================================================
// Creation for CoreWindow (UWP)
//=============================================================================

Graphics* create_d3d12_graphics_corewindow(void* core_window, int width, int height, const Config& config) {
#ifdef _DEBUG
    ID3D12Debug* debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        debug->Release();
    }
#endif

    IDXGIFactory4* factory;
    HRESULT hr = CreateDXGIFactory2(
#ifdef _DEBUG
        DXGI_CREATE_FACTORY_DEBUG,
#else
        0,
#endif
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(hr)) return nullptr;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }
        if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
            break;
        }
        adapter->Release();
        adapter = nullptr;
    }

    if (!adapter) {
        factory->Release();
        return nullptr;
    }

    DXGI_ADAPTER_DESC1 adapter_desc;
    adapter->GetDesc1(&adapter_desc);

    ID3D12Device* device;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        adapter->Release();
        factory->Release();
        return nullptr;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ID3D12CommandQueue* command_queue;
    device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = get_dxgi_format(config.color_bits);
    sd.Stereo = FALSE;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = 0;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1* swap_chain1;
    hr = factory->CreateSwapChainForCoreWindow(command_queue, static_cast<IUnknown*>(core_window),
                                                &sd, nullptr, &swap_chain1);

    factory->Release();
    adapter->Release();

    if (FAILED(hr)) {
        command_queue->Release();
        device->Release();
        return nullptr;
    }

    IDXGISwapChain4* swap_chain;
    swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain));
    swap_chain1->Release();

    GraphicsD3D12* gfx = new GraphicsD3D12();
    gfx->device = device;
    gfx->command_queue = command_queue;
    gfx->swap_chain = swap_chain;
    gfx->vsync = config.vsync;
    gfx->buffer_count = static_cast<UINT>(config.back_buffers);

    char name[256];
    WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name, sizeof(name), nullptr, nullptr);
    gfx->device_name = name;

    return gfx;
}

} // namespace window

#endif // _WIN32 && !WINDOW_NO_D3D12
