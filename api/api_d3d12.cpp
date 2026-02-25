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
#include "../internal/utf8_util.hpp"

#pragma comment(lib, "d3d12.lib")
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
// D3D12 Graphics Implementation
//=============================================================================

class GraphicsD3D12 : public Graphics {
public:
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* command_queue = nullptr;
    IDXGISwapChain4* swap_chain = nullptr;
    std::string device_name;
    bool owns_device = true;
    SwapMode swap_mode = SwapMode::Fifo;
    bool allow_tearing = false;
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
        UINT flags = allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = swap_chain->ResizeBuffers(buffer_count, static_cast<UINT>(width),
                                                static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, flags);
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
                    sync_interval = 1;
                    break;
                case SwapMode::FifoRelaxed:
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
        // D3D12 doesn't have a "make current" concept like OpenGL
    }

    void* native_device() const override { return device; }
    void* native_context() const override { return command_queue; }
    void* native_swapchain() const override { return swap_chain; }

    void get_capabilities(GraphicsCapabilities* out_caps) const override {
        if (!out_caps || !device) return;
        GraphicsCapabilities& c = *out_caps;

        // API version
        c.api_version_major = 12;
        c.api_version_minor = 0;

        // Shader model
        static const D3D_SHADER_MODEL sm_list[] = {
            D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4,
            D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1,
            D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1
        };
        D3D12_FEATURE_DATA_SHADER_MODEL sm = { D3D_SHADER_MODEL_6_6 };
        for (auto m : sm_list) {
            sm.HighestShaderModel = m;
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) break;
        }
        c.shader_model = static_cast<float>((sm.HighestShaderModel >> 4) & 0xF)
                       + ((sm.HighestShaderModel & 0xF) * 0.1f);

        // Feature level
        static const D3D_FEATURE_LEVEL fl_list[] = {
            D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS fl_data = {};
        fl_data.NumFeatureLevels         = _countof(fl_list);
        fl_data.pFeatureLevelsRequested  = fl_list;
        device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl_data, sizeof(fl_data));

        // Texture limits
        c.max_texture_size         = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        c.max_texture_3d_size      = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        c.max_texture_cube_size    = D3D12_REQ_TEXTURECUBE_DIMENSION;
        c.max_texture_array_layers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        c.max_mip_levels           = D3D12_REQ_MIP_LEVELS;
        c.max_framebuffer_width    = c.max_texture_size;
        c.max_framebuffer_height   = c.max_texture_size;

        // Framebuffer / MSAA
        c.max_color_attachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        c.max_samples = 1;
        for (UINT s : {8u, 4u, 2u}) {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS mql = {};
            mql.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
            mql.SampleCount = s;
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &mql, sizeof(mql))) && mql.NumQualityLevels > 0) {
                c.max_samples = static_cast<int>(s);
                break;
            }
        }

        // Sampling
        c.max_texture_bindings = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
        c.max_anisotropy       = D3D12_MAX_MAXANISOTROPY;

        // Vertex / buffer limits
        c.max_vertex_attributes   = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        c.max_vertex_buffers      = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        c.max_uniform_bindings    = D3D12_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        c.max_uniform_buffer_size = D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        c.max_storage_bindings    = D3D12_UAV_SLOT_COUNT;
        c.max_viewports           = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        c.max_scissor_rects       = D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;

        // Compute
        c.max_compute_group_size_x = D3D12_CS_THREAD_GROUP_MAX_X;
        c.max_compute_group_size_y = D3D12_CS_THREAD_GROUP_MAX_Y;
        c.max_compute_group_size_z = D3D12_CS_THREAD_GROUP_MAX_Z;
        c.max_compute_group_total  = D3D12_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP;
        c.max_compute_dispatch_x   = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        c.max_compute_dispatch_y   = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
        c.max_compute_dispatch_z   = D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;

        // Pipeline features
        c.compute_shaders     = true;
        c.geometry_shaders    = true;
        c.tessellation        = true;
        c.instancing          = true;
        c.indirect_draw       = true;
        c.base_vertex_draw    = true;
        c.occlusion_query     = true;
        c.timestamp_query     = true;
        c.depth_clamp         = true;
        c.fill_mode_wireframe = true;

        // Options query for conservative rasterisation and other features
        D3D12_FEATURE_DATA_D3D12_OPTIONS opts = {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts))))
            c.conservative_raster = (opts.ConservativeRasterizationTier != D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED);

        // Mesh shaders (SM 6.5+, Options7)
        if (c.shader_model >= 6.5f) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 opts7 = {};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &opts7, sizeof(opts7))))
                c.mesh_shaders = (opts7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED);
        }

        // Texture features
        c.texture_arrays = c.texture_3d = c.cube_maps = c.cube_map_arrays = true;
        c.render_to_texture = c.read_write_textures   = true;
        c.floating_point_textures = c.integer_textures = true;
        c.texture_compression_bc  = true;
        c.srgb_framebuffer = c.srgb_textures = true;
        c.depth32f = c.stencil8              = true;

        // Blend
        c.independent_blend = c.dual_source_blend = c.logic_ops = true;

        // DXGI: VRAM + tearing
        IDXGIFactory4* fac4 = nullptr;
        IDXGIAdapter*  adapter = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&fac4))) {
            LUID luid = device->GetAdapterLuid();
            if (SUCCEEDED(fac4->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter), (void**)&adapter))) {
                DXGI_ADAPTER_DESC desc = {};
                if (SUCCEEDED(adapter->GetDesc(&desc))) {
                    c.vram_dedicated_bytes = desc.DedicatedVideoMemory;
                    c.vram_shared_bytes    = desc.SharedSystemMemory;
                }
                adapter->Release();
            }
            IDXGIFactory5* fac5 = nullptr;
            if (SUCCEEDED(fac4->QueryInterface(__uuidof(IDXGIFactory5), (void**)&fac5))) {
                BOOL tearing = FALSE;
                if (SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
                    c.tearing_support = (tearing != FALSE);
                fac5->Release();
            }
            fac4->Release();
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

Graphics* create_d3d12_graphics_hwnd(void* hwnd, const Config& config) {
    ID3D12Device* device = nullptr;
    bool owns_device = true;
    DXGI_ADAPTER_DESC1 adapter_desc = {};

    IDXGIFactory6* factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return nullptr;

    // Check for tearing support
    bool allow_tearing = false;
    {
        BOOL tearing_support = FALSE;
        if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_support, sizeof(tearing_support)))) {
            allow_tearing = (tearing_support == TRUE);
        }
    }

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

    SwapMode swap_mode = resolve_swap_mode(config);

    const WindowConfigEntry& win_cfg = config.windows[0];
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = win_cfg.width;
    sd.Height = win_cfg.height;
    sd.Format = get_dxgi_format(config.color_bits);
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = config.back_buffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = (allow_tearing && swap_mode == SwapMode::Immediate) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

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
    gfx->swap_mode = swap_mode;
    gfx->allow_tearing = allow_tearing && (swap_mode == SwapMode::Immediate);
    gfx->buffer_count = static_cast<UINT>(config.back_buffers);

    gfx->device_name = internal::wide_to_utf8(adapter_desc.Description);

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

    SwapMode swap_mode = resolve_swap_mode(config);

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
    gfx->swap_mode = swap_mode;
    gfx->allow_tearing = false;  // UWP doesn't support tearing
    gfx->buffer_count = static_cast<UINT>(config.back_buffers);

    gfx->device_name = internal::wide_to_utf8(adapter_desc.Description);

    return gfx;
}

} // namespace window

#endif // _WIN32 && !WINDOW_NO_D3D12
