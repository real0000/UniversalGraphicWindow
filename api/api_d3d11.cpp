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
#include "../internal/utf8_util.hpp"

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

    void get_capabilities(GraphicsCapabilities* out_caps) const override {
        if (!out_caps || !device) return;
        GraphicsCapabilities& c = *out_caps;

        D3D_FEATURE_LEVEL fl = device->GetFeatureLevel();

        // API version
        c.api_version_major = 11;
        c.api_version_minor = (fl >= D3D_FEATURE_LEVEL_11_1) ? 1 : 0;
        c.shader_model      = (fl >= D3D_FEATURE_LEVEL_11_0) ? 5.0f :
                              (fl >= D3D_FEATURE_LEVEL_10_1) ? 4.1f : 4.0f;

        // Texture limits (D3D11 hardware spec constants)
        c.max_texture_size         = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        c.max_texture_3d_size      = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        c.max_texture_cube_size    = D3D11_REQ_TEXTURECUBE_DIMENSION;
        c.max_texture_array_layers = D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        c.max_mip_levels           = D3D11_REQ_MIP_LEVELS;
        c.max_framebuffer_width    = c.max_texture_size;
        c.max_framebuffer_height   = c.max_texture_size;

        // Framebuffer
        c.max_color_attachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

        // MSAA: probe each power-of-two down from 8
        c.max_samples = 1;
        for (UINT s : {8u, 4u, 2u}) {
            UINT quality = 0;
            if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, s, &quality)) && quality > 0) {
                c.max_samples = static_cast<int>(s);
                break;
            }
        }

        // Sampling
        c.max_texture_bindings = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        c.max_anisotropy       = D3D11_MAX_MAXANISOTROPY;

        // Vertex / buffer limits
        c.max_vertex_attributes   = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        c.max_vertex_buffers      = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        c.max_uniform_bindings    = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        c.max_uniform_buffer_size = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        c.max_storage_bindings    = D3D11_PS_CS_UAV_REGISTER_COUNT;

        // Viewports / scissors
        c.max_viewports     = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        c.max_scissor_rects = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

        // Compute (CS 5.0 on FL 11.0+)
        if (fl >= D3D_FEATURE_LEVEL_11_0) {
            c.max_compute_group_size_x = D3D11_CS_THREAD_GROUP_MAX_X;
            c.max_compute_group_size_y = D3D11_CS_THREAD_GROUP_MAX_Y;
            c.max_compute_group_size_z = D3D11_CS_THREAD_GROUP_MAX_Z;
            c.max_compute_group_total  = D3D11_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
            c.max_compute_dispatch_x   = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
            c.max_compute_dispatch_y   = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
            c.max_compute_dispatch_z   = D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        }

        // Shader / pipeline features
        c.compute_shaders     = (fl >= D3D_FEATURE_LEVEL_11_0);
        c.geometry_shaders    = (fl >= D3D_FEATURE_LEVEL_10_0);
        c.tessellation        = (fl >= D3D_FEATURE_LEVEL_11_0);
        c.instancing          = true;
        c.indirect_draw       = (fl >= D3D_FEATURE_LEVEL_11_0);
        c.base_vertex_draw    = true;
        c.occlusion_query     = true;
        c.timestamp_query     = true;
        c.depth_clamp         = true;
        c.fill_mode_wireframe = true;

        // Texture features
        c.texture_arrays          = true;
        c.texture_3d              = true;
        c.cube_maps               = true;
        c.cube_map_arrays         = (fl >= D3D_FEATURE_LEVEL_10_1);
        c.render_to_texture       = true;
        c.read_write_textures     = c.compute_shaders;
        c.floating_point_textures = true;
        c.integer_textures        = (fl >= D3D_FEATURE_LEVEL_10_0);
        c.texture_compression_bc  = true;
        c.srgb_framebuffer        = true;
        c.srgb_textures           = true;
        c.depth32f                = true;
        c.stencil8                = true;

        // Blend
        c.independent_blend  = true;
        c.dual_source_blend  = (fl >= D3D_FEATURE_LEVEL_10_0);

        // Check D3D11.1 logic ops
        if (c.api_version_minor >= 1) {
            D3D11_FEATURE_DATA_D3D11_OPTIONS opts = {};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &opts, sizeof(opts))))
                c.logic_ops = (opts.OutputMergerLogicOp != FALSE);
        }

        // DXGI: tearing support + VRAM
        IDXGIDevice* dxgi_dev = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_dev))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgi_dev->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC desc = {};
                if (SUCCEEDED(adapter->GetDesc(&desc))) {
                    c.vram_dedicated_bytes = desc.DedicatedVideoMemory;
                    c.vram_shared_bytes    = desc.SharedSystemMemory;
                }
                IDXGIFactory2* fac2 = nullptr;
                if (SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&fac2))) {
                    IDXGIFactory5* fac5 = nullptr;
                    if (SUCCEEDED(fac2->QueryInterface(__uuidof(IDXGIFactory5), (void**)&fac5))) {
                        BOOL tearing = FALSE;
                        if (SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
                            c.tearing_support = (tearing != FALSE);
                        fac5->Release();
                    }
                    fac2->Release();
                }
                adapter->Release();
            }
            dxgi_dev->Release();
        }
    }
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

    gfx->device_name = internal::wide_to_utf8(adapter_desc.Description);

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

    gfx->device_name = internal::wide_to_utf8(adapter_desc.Description);

    return gfx;
}

#endif // WINDOW_PLATFORM_UWP

} // namespace window

#endif // _WIN32 && !WINDOW_NO_D3D11
