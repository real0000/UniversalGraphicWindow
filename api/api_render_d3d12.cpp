// api_render_d3d12.cpp — Direct3D 12 implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Windows only. Also covers
// "D3D12 Ultimate" (mesh shaders, DXR) under WINDOW_SUPPORT_D3D12_ULTIMATE.
//
// Built on UGW's GraphicsD3D12: native_device() = ID3D12Device*, native_context() =
// ID3D12CommandQueue*, native_swapchain() = IDXGISwapChain*. Wired into the
// dispatcher in api_render.cpp.
//
// NOTE: written against the D3D12 API but COMPILED/VERIFIED ONLY ON WINDOWS. Core
// paths are implemented (resources + committed heaps, root signature from the
// pipeline layout, graphics/compute PSOs from DXIL, a command-allocator/list
// commander, native ID3D12Fence for fences AND timelines, descriptor-heap-backed
// descriptor sets, copy/readback, queries); the present path, sparse (reserved
// resources) and DXR/mesh under D3D12 Ultimate are scaffolded with clear TODOs.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

#if defined(WINDOW_SUPPORT_D3D12) && defined(_WIN32)

#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>   // D3DReflect — auto root signature from DXBC for slot binding
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#pragma comment(lib, "d3dcompiler.lib")

namespace window {
namespace {

void d12_unsupported(const char* what) { static std::vector<const char*> seen; for (auto s : seen) if (s == what) return; seen.push_back(what); std::fprintf(stderr, "[UGW/D3D12] %s not supported (no-op)\n", what); }

template <class T> struct Pool {
    std::vector<T> items; std::vector<int> free_list;
    int alloc(T v) { if (!free_list.empty()) { int i = free_list.back(); free_list.pop_back(); items[i] = std::move(v); return i; } items.push_back(std::move(v)); return int(items.size()) - 1; }
    T* get(int id) { return (id >= 0 && id < int(items.size())) ? &items[id] : nullptr; }
    void release(int id) { if (id >= 0 && id < int(items.size())) free_list.push_back(id); }
};

DXGI_FORMAT tex_format(TextureFormat f) {
    switch (f) {
        case TextureFormat::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case TextureFormat::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        // Block-compressed (BCn). ETC2/ASTC have no D3D equivalent (CPU-decode them).
        case TextureFormat::BC1_UNORM: return DXGI_FORMAT_BC1_UNORM;
        case TextureFormat::BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case TextureFormat::BC2_UNORM: return DXGI_FORMAT_BC2_UNORM;
        case TextureFormat::BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM_SRGB;
        case TextureFormat::BC3_UNORM: return DXGI_FORMAT_BC3_UNORM;
        case TextureFormat::BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case TextureFormat::BC4_UNORM: return DXGI_FORMAT_BC4_UNORM;
        case TextureFormat::BC4_SNORM: return DXGI_FORMAT_BC4_SNORM;
        case TextureFormat::BC5_UNORM: return DXGI_FORMAT_BC5_UNORM;
        case TextureFormat::BC5_SNORM: return DXGI_FORMAT_BC5_SNORM;
        case TextureFormat::BC6H_UF16: return DXGI_FORMAT_BC6H_UF16;
        case TextureFormat::BC6H_SF16: return DXGI_FORMAT_BC6H_SF16;
        case TextureFormat::BC7_UNORM: return DXGI_FORMAT_BC7_UNORM;
        case TextureFormat::BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM_SRGB;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}
DXGI_FORMAT vertex_format(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float1: return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UByte4N: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Half2:  return DXGI_FORMAT_R16G16_FLOAT;
        case VertexFormat::Half4:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
}
D3D12_BLEND d12_blend_factor(BlendFactor f, bool alpha) {
    switch (f) {
        case BlendFactor::Zero:           return D3D12_BLEND_ZERO;
        case BlendFactor::One:            return D3D12_BLEND_ONE;
        case BlendFactor::SrcColor:       return alpha ? D3D12_BLEND_SRC_ALPHA      : D3D12_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:    return alpha ? D3D12_BLEND_INV_SRC_ALPHA  : D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:       return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:    return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstColor:       return alpha ? D3D12_BLEND_DEST_ALPHA     : D3D12_BLEND_DEST_COLOR;
        case BlendFactor::InvDstColor:    return alpha ? D3D12_BLEND_INV_DEST_ALPHA : D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::DstAlpha:       return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha:    return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::SrcAlphaSat:    return D3D12_BLEND_SRC_ALPHA_SAT;
        case BlendFactor::BlendFactor:    return D3D12_BLEND_BLEND_FACTOR;
        case BlendFactor::InvBlendFactor: return D3D12_BLEND_INV_BLEND_FACTOR;
    }
    return D3D12_BLEND_ONE;
}
D3D12_BLEND_OP d12_blend_op(BlendOp o) {
    switch (o) {
        case BlendOp::Add: return D3D12_BLEND_OP_ADD; case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT; case BlendOp::Min: return D3D12_BLEND_OP_MIN; case BlendOp::Max: return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}
D3D12_COMPARISON_FUNC d12_compare(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never: return D3D12_COMPARISON_FUNC_NEVER; case CompareFunc::Less: return D3D12_COMPARISON_FUNC_LESS;
        case CompareFunc::Equal: return D3D12_COMPARISON_FUNC_EQUAL; case CompareFunc::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareFunc::Greater: return D3D12_COMPARISON_FUNC_GREATER; case CompareFunc::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL; case CompareFunc::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS;
}

struct D12Buffer  { ID3D12Resource* res = nullptr; UINT64 size = 0; UINT stride = 0; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; ID3D12Resource* map_upload = nullptr; };
struct D12Texture { ID3D12Resource* res = nullptr; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; TextureFormat tf = TextureFormat::RGBA8_UNORM; int w = 0, h = 0; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
                   bool owns_res = true; };   // false for imported resources we must not Release
struct D12Sampler { D3D12_SAMPLER_DESC desc{}; };
// Reflected resource binding from DXBC: (register, kind). kind: 0=CBV(b#), 1=UAV(u#).
// (SRV/sampler slot binding on D3D12's auto path is a follow-up; explicit layouts handle them.)
struct D12Shader  { std::vector<uint8_t> bytecode; ShaderStage stage = ShaderStage::Vertex;
                    std::vector<std::pair<UINT, int>> binds; };
// Per descriptor set, the root-parameter index of its CBV/SRV/UAV table and (separate,
// as D3D12 requires) its SAMPLER table. -1 = that set has no table of that kind.
struct SetParams { int srv_param = -1; int samp_param = -1; };
struct D12Pipeline{ ID3D12PipelineState* pso = nullptr; ID3D12RootSignature* root = nullptr; bool compute = false;
                    D3D_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; VertexLayout vl;
                    std::vector<SetParams> set_params; };
struct D12RenderTarget { int color_tex = -1; int depth_tex = -1; D3D12_CPU_DESCRIPTOR_HANDLE rtv{}; D3D12_CPU_DESCRIPTOR_HANDLE dsv{}; };
struct D12Fence   { ID3D12Fence* fence = nullptr; UINT64 value = 0; };
struct D12Query   { ID3D12QueryHeap* heap = nullptr; ID3D12Resource* readback = nullptr; QueryType type = QueryType::Timestamp; };
struct D12DescSetLayout { DescriptorSetLayoutDesc desc; };
struct D12PipelineLayout{ ID3D12RootSignature* root = nullptr; PipelineLayoutDesc desc; std::vector<SetParams> set_params; };
struct D12DescriptorSet { std::vector<DescriptorWrite> writes; };

class D12Device : public GraphicDevice {
public:
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12Fence* imm_fence = nullptr;     // for immediate (upload/readback) submissions
    UINT64 imm_value = 0;
    HANDLE imm_event = nullptr;

    // CPU-only (non-shader-visible) heaps for render-target / depth views. Offscreen
    // targets allocate one descriptor each; a rolling index is plenty for a frame's RTs.
    ID3D12DescriptorHeap* rtv_heap = nullptr; UINT rtv_size = 0, rtv_next = 0;
    ID3D12DescriptorHeap* dsv_heap = nullptr; UINT dsv_size = 0, dsv_next = 0;
    // GPU-visible (shader-visible) heaps for descriptor-table binding: one CBV/SRV/UAV
    // heap + one SAMPLER heap, allocated as small rings per bound descriptor set.
    ID3D12DescriptorHeap* gpu_heap = nullptr; UINT gpu_size = 0, gpu_next = 0;
    ID3D12DescriptorHeap* samp_heap = nullptr; UINT samp_size = 0, samp_next = 0;
    static const UINT kHeapCount = 256;
    static const UINT kGpuHeapCount = 4096;
    static const UINT kSampHeapCount = 256;

    // Swapchain backbuffers for the windowed present path (set_render_target_backbuffer).
    IDXGISwapChain*       swap_raw_ = nullptr;   // from native_swapchain()
    IDXGISwapChain3*      swap3_    = nullptr;   // QI'd lazily (GetCurrentBackBufferIndex)
    ID3D12DescriptorHeap* bb_rtv_heap_ = nullptr;
    ID3D12Resource*       backbuffers_[8] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE bb_rtv_[8] = {};
    D3D12_RESOURCE_STATES  bb_state_[8] = {};
    UINT                   bb_count_ = 0;

    // Lazily wrap the swapchain backbuffers as render targets (one RTV each, own heap).
    void ensure_backbuffers() {
        if (swap3_ || !swap_raw_) return;
        if (FAILED(swap_raw_->QueryInterface(IID_PPV_ARGS(&swap3_))) || !swap3_) return;
        DXGI_SWAP_CHAIN_DESC d{}; swap3_->GetDesc(&d);
        bb_count_ = d.BufferCount > 8 ? 8 : d.BufferCount;
        D3D12_DESCRIPTOR_HEAP_DESC rh{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, bb_count_, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&bb_rtv_heap_));
        auto base = bb_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < bb_count_; ++i) {
            swap3_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i]));
            bb_rtv_[i] = base; bb_rtv_[i].ptr += SIZE_T(i) * rtv_size;
            dev->CreateRenderTargetView(backbuffers_[i], nullptr, bb_rtv_[i]);
            bb_state_[i] = D3D12_RESOURCE_STATE_PRESENT;
        }
    }
    UINT current_bb_index() { return swap3_ ? swap3_->GetCurrentBackBufferIndex() : 0; }
    void transition_bb(ID3D12GraphicsCommandList* cl, UINT i, D3D12_RESOURCE_STATES to) {
        if (i >= bb_count_ || bb_state_[i] == to) return;
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { backbuffers_[i], D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bb_state_[i], to };
        cl->ResourceBarrier(1, &b); bb_state_[i] = to;
    }

    explicit D12Device(Graphics* g) {
        dev = (ID3D12Device*)g->native_device(); queue = (ID3D12CommandQueue*)g->native_context();
        swap_raw_ = (IDXGISwapChain*)g->native_swapchain();
        dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&imm_fence));
        imm_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        D3D12_DESCRIPTOR_HEAP_DESC rh{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kHeapCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtv_heap)); rtv_size = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_DESCRIPTOR_HEAP_DESC dh{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kHeapCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
        dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsv_heap)); dsv_size = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        D3D12_DESCRIPTOR_HEAP_DESC gh{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kGpuHeapCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        dev->CreateDescriptorHeap(&gh, IID_PPV_ARGS(&gpu_heap)); gpu_size = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_DESCRIPTOR_HEAP_DESC sh{ D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSampHeapCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
        dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&samp_heap)); samp_size = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    }
    ~D12Device() override { for (UINT i = 0; i < bb_count_; ++i) if (backbuffers_[i]) backbuffers_[i]->Release(); if (bb_rtv_heap_) bb_rtv_heap_->Release(); if (swap3_) swap3_->Release(); if (samp_heap) samp_heap->Release(); if (gpu_heap) gpu_heap->Release(); if (default_root_) default_root_->Release(); if (rtv_heap) rtv_heap->Release(); if (dsv_heap) dsv_heap->Release(); if (imm_fence) imm_fence->Release(); if (imm_event) CloseHandle(imm_event); }

    D3D12_CPU_DESCRIPTOR_HANDLE alloc_rtv() { auto h = rtv_heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(rtv_next++ % kHeapCount) * rtv_size; return h; }
    D3D12_CPU_DESCRIPTOR_HANDLE alloc_dsv() { auto h = dsv_heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(dsv_next++ % kHeapCount) * dsv_size; return h; }
    // Allocate `n` contiguous shader-visible CBV/SRV/UAV descriptors; returns both handles to the base.
    void alloc_gpu(UINT n, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu) {
        UINT base = gpu_next; gpu_next = (gpu_next + n) % kGpuHeapCount; if (gpu_next < base) base = 0, gpu_next = n;  // avoid wrap-split
        cpu = gpu_heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += SIZE_T(base) * gpu_size;
        gpu = gpu_heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += UINT64(base) * gpu_size;
    }
    void alloc_samp(UINT n, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu) {
        UINT base = samp_next; samp_next = (samp_next + n) % kSampHeapCount; if (samp_next < base) base = 0, samp_next = n;
        cpu = samp_heap->GetCPUDescriptorHandleForHeapStart(); cpu.ptr += SIZE_T(base) * samp_size;
        gpu = samp_heap->GetGPUDescriptorHandleForHeapStart(); gpu.ptr += UINT64(base) * samp_size;
    }

    // A graphics PSO requires a root signature even with no bound resources; share an
    // empty one (IA-input-layout allowed) for pipelines created without a layout.
    ID3D12RootSignature* default_root_ = nullptr;
    ID3D12RootSignature* default_root() {
        if (!default_root_) {
            D3D12_ROOT_SIGNATURE_DESC rsd = {}; rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
            D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
            if (blob) { dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&default_root_)); blob->Release(); }
            if (err) err->Release();
        }
        return default_root_;
    }

    Backend get_backend() const override { return Backend::D3D12; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (!out) return;
        out->max_texture_size = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        out->max_color_attachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        out->max_bound_descriptor_sets = 8; out->min_uniform_buffer_offset_alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        out->min_storage_buffer_offset_alignment = 16;    // UAV FirstElement: raw=4B, structured=stride; 16 is a safe floor
        out->max_push_constant_size = 256; out->compute_shaders = true; out->indirect_draw = true; out->mesh_shaders = true; out->timestamp_query = true;
        // Highest MSAA sample count the default colour format supports.
        for (UINT n = 8; n >= 2; n >>= 1) { D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{ DXGI_FORMAT_R8G8B8A8_UNORM, n, D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0 };
            if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof q)) && q.NumQualityLevels > 0) { out->max_samples = int(n); break; } }
    }

    // committed resource helper
    ID3D12Resource* commit(const D3D12_RESOURCE_DESC& rd, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE* cv = nullptr) {
        D3D12_HEAP_PROPERTIES hp = {}; hp.Type = heap;
        ID3D12Resource* r = nullptr; dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, cv, IID_PPV_ARGS(&r)); return r;
    }
    void flush() { const UINT64 v = ++imm_value; queue->Signal(imm_fence, v); if (imm_fence->GetCompletedValue() < v) { imm_fence->SetEventOnCompletion(v, imm_event); WaitForSingleObject(imm_event, INFINITE); } }

    BufferHandle create_buffer(const BufferDesc& d) override {
        D12Buffer b; b.size = d.size ? d.size : 16; b.stride = d.stride;
        D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (b.size + 255) & ~255ull; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (d.type == BufferType::Storage) rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        b.state = D3D12_RESOURCE_STATE_COMMON;
        b.res = commit(rd, D3D12_HEAP_TYPE_DEFAULT, b.state);
        if (d.initial_data) { int id = buffers_.alloc(b); update_buffer({ id }, d.initial_data, d.size, 0); return { id }; }
        return { buffers_.alloc(b) };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !data) return;
        D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* up = commit(rd, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p = nullptr; D3D12_RANGE rng{ 0, 0 }; up->Map(0, &rng, &p); std::memcpy(p, data, size); up->Unmap(0, nullptr);
        immediate([&](ID3D12GraphicsCommandList* cl) { cl->CopyBufferRegion(b->res, offset, up, 0, size); });
        up->Release();
    }
    void destroy_buffer(BufferHandle h) override { auto* b = buffers_.get(h.id); if (b) { if (b->map_upload) b->map_upload->Release(); if (b->res) b->res->Release(); } buffers_.release(h.id); }

    TextureHandle create_texture(const TextureDesc& d) override {
        D12Texture t; t.fmt = tex_format(d.format); t.tf = d.format; t.w = d.width; t.h = d.height;
        D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width = d.width; rd.Height = d.height;
        rd.DepthOrArraySize = d.array_layers > 1 ? d.array_layers : 1; rd.MipLevels = d.mip_levels ? d.mip_levels : 1; rd.Format = t.fmt; rd.SampleDesc.Count = d.samples > 1 ? d.samples : 1;
        if (d.usage & TEXTURE_USAGE_RENDER_TARGET) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (d.usage & TEXTURE_USAGE_DEPTH_STENCIL) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (d.usage & TEXTURE_USAGE_STORAGE) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        t.state = D3D12_RESOURCE_STATE_COMMON; t.res = commit(rd, D3D12_HEAP_TYPE_DEFAULT, t.state);
        int id = textures_.alloc(t);
        if (d.initial_data) { TextureRegion r; r.width = d.width; r.height = d.height; update_texture({ id }, r, d.initial_data); }
        return { id };
    }
    void update_texture(TextureHandle h, const TextureRegion& r, const void* data) override {
        auto* t = textures_.get(h.id); if (!t || !data) return;
        int bw = 1, bh = 1; texture_format_block_dims(t->tf, &bw, &bh);
        const UINT64 srcRowPitch = texture_format_row_pitch(t->tf, r.width);          // tight bytes per (block)row
        const int    rows        = texture_format_row_count(t->tf, r.height);         // texel- or block-rows
        const UINT64 rowPitch    = (srcRowPitch + 255) & ~255ull;                     // D3D12 256-byte row alignment
        const UINT64 upSize      = rowPitch * UINT64(rows);
        const UINT   fpW = UINT(((r.width  + bw - 1) / bw) * bw);                     // footprint dims block-aligned
        const UINT   fpH = UINT(((r.height + bh - 1) / bh) * bh);
        D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = upSize; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* up = commit(bd, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        uint8_t* p = nullptr; D3D12_RANGE rng{ 0, 0 }; up->Map(0, &rng, (void**)&p);
        for (int y = 0; y < rows; ++y) std::memcpy(p + y * rowPitch, (const uint8_t*)data + y * srcRowPitch, (size_t)srcRowPitch);
        up->Unmap(0, nullptr);
        immediate([&](ID3D12GraphicsCommandList* cl) {
            D3D12_TEXTURE_COPY_LOCATION dstL{}; dstL.pResource = t->res; dstL.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstL.SubresourceIndex = r.mip;
            D3D12_TEXTURE_COPY_LOCATION srcL{}; srcL.pResource = up; srcL.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            srcL.PlacedFootprint.Footprint = { t->fmt, fpW, fpH, 1, (UINT)rowPitch };
            transition(cl, t, D3D12_RESOURCE_STATE_COPY_DEST);
            cl->CopyTextureRegion(&dstL, r.x, r.y, 0, &srcL, nullptr);
        });
        up->Release();
    }
    void generate_mipmaps(TextureHandle) override { d12_unsupported("generate_mipmaps (run a downsample compute pass)"); }
    void destroy_texture(TextureHandle h) override { auto* t = textures_.get(h.id); if (t && t->res && t->owns_res) t->res->Release(); textures_.release(h.id); }

    SamplerHandle create_sampler(const SamplerState& s) override { D12Sampler smp; smp.desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; smp.desc.AddressU = smp.desc.AddressV = smp.desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; smp.desc.MaxLOD = D3D12_FLOAT32_MAX; (void)s; return { samplers_.alloc(smp) }; }
    void destroy_sampler(SamplerHandle h) override { samplers_.release(h.id); }

    ShaderHandle create_shader(const ShaderDesc& d) override {
        if (d.language != ShaderLanguage::DXIL && d.language != ShaderLanguage::DXBC) { d12_unsupported("non-DXIL shader (compile HLSL to DXIL)"); return { -1 }; }
        D12Shader sh; sh.stage = d.stage; sh.bytecode.assign((const uint8_t*)d.code, (const uint8_t*)d.code + d.code_size);
        // Reflect cbuffer/UAV registers so a pipeline built without an explicit layout can
        // synthesise a root signature (enables slot-based bind_uniform_buffer/bind_storage_buffer).
        ID3D12ShaderReflection* refl = nullptr;
        if (SUCCEEDED(D3DReflect(d.code, d.code_size, IID_PPV_ARGS(&refl))) && refl) {
            D3D12_SHADER_DESC sd{}; refl->GetDesc(&sd);
            for (UINT i = 0; i < sd.BoundResources; ++i) {
                D3D12_SHADER_INPUT_BIND_DESC bd{}; refl->GetResourceBindingDesc(i, &bd);
                if (bd.Type == D3D_SIT_CBUFFER) sh.binds.push_back({ bd.BindPoint, 0 });
                else if (bd.Type == D3D_SIT_UAV_RWTYPED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED ||
                         bd.Type == D3D_SIT_UAV_RWBYTEADDRESS || bd.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                         bd.Type == D3D_SIT_UAV_CONSUME_STRUCTURED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER)
                    sh.binds.push_back({ bd.BindPoint, 1 });
            }
            refl->Release();
        }
        return { shaders_.alloc(std::move(sh)) };
    }
    void destroy_shader(ShaderHandle h) override { shaders_.release(h.id); }

    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override { return { dsls_.alloc(D12DescSetLayout{ d }) }; }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { dsls_.release(h.id); }
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& d) override {
        // Per set, build up to two descriptor tables: a CBV/SRV/UAV table and (since D3D12
        // forbids mixing) a separate SAMPLER table. CombinedImageSampler contributes to both.
        std::vector<D3D12_ROOT_PARAMETER> params; std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges;
        std::vector<SetParams> set_params(d.set_layout_count);
        const UINT APPEND = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        auto push_table = [&](std::vector<D3D12_DESCRIPTOR_RANGE>&& rs) -> int {
            ranges.push_back(std::move(rs));
            D3D12_ROOT_PARAMETER p = {}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            p.DescriptorTable.NumDescriptorRanges = (UINT)ranges.back().size(); p.DescriptorTable.pDescriptorRanges = ranges.back().data();
            int idx = (int)params.size(); params.push_back(p); return idx;
        };
        for (int i = 0; i < d.set_layout_count; ++i) {
            auto* l = dsls_.get(d.set_layouts[i].id); if (!l) continue;
            std::vector<D3D12_DESCRIPTOR_RANGE> res, samp;   // CBV/SRV/UAV ranges, SAMPLER ranges
            for (int b = 0; b < l->desc.binding_count; ++b) {
                const auto& bd = l->desc.bindings[b];
                switch (bd.type) {
                    case BindingType::UniformBuffer:  res.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_CBV, bd.count, bd.binding, 0, APPEND }); break;
                    case BindingType::StorageBuffer:
                    case BindingType::StorageTexture: res.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_UAV, bd.count, bd.binding, 0, APPEND }); break;
                    case BindingType::SampledTexture: res.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bd.count, bd.binding, 0, APPEND }); break;
                    case BindingType::CombinedImageSampler:
                        res.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bd.count, bd.binding, 0, APPEND });
                        samp.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, bd.count, bd.binding, 0, APPEND }); break;
                    case BindingType::Sampler:        samp.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, bd.count, bd.binding, 0, APPEND }); break;
                }
            }
            if (!res.empty())  set_params[i].srv_param  = push_table(std::move(res));
            if (!samp.empty()) set_params[i].samp_param = push_table(std::move(samp));
        }
        for (int i = 0; i < d.push_constant_count; ++i) { D3D12_ROOT_PARAMETER p = {}; p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; p.Constants.Num32BitValues = d.push_constants[i].size / 4; p.Constants.ShaderRegister = 0; params.push_back(p); }
        D3D12_ROOT_SIGNATURE_DESC rsd = {}; rsd.NumParameters = (UINT)params.size(); rsd.pParameters = params.data(); rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr; D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        D12PipelineLayout pl; pl.desc = d; pl.set_params = std::move(set_params);
        if (blob) { dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&pl.root)); blob->Release(); }
        if (err) err->Release();
        return { plls_.alloc(pl) };
    }
    void destroy_pipeline_layout(PipelineLayoutHandle h) override { auto* l = plls_.get(h.id); if (l && l->root) l->root->Release(); plls_.release(h.id); }
    DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& d) override { D12DescriptorSet s; s.writes.assign(d.writes, d.writes + d.write_count); return { dsets_.alloc(std::move(s)) }; }
    void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& d) override { if (auto* s = dsets_.get(h.id)) s->writes.assign(d.writes, d.writes + d.write_count); }
    void destroy_descriptor_set(DescriptorSetHandle h) override { dsets_.release(h.id); }

    // Synthesize a root signature from reflected cbuffer/UAV registers: root param i is a root
    // CBV (or UAV) at register i, so bind_uniform_buffer(i)/bind_storage_buffer(i) -- which call
    // SetGraphicsRoot{ConstantBufferView,UnorderedAccessView}(i) -- hit the matching param.
    // Returns null when there are no such resources (caller falls back to the empty default).
    ID3D12RootSignature* build_auto_root(std::initializer_list<ShaderHandle> shs) {
        std::vector<std::pair<UINT, int>> all; UINT maxreg = 0; bool any = false;
        for (ShaderHandle h : shs) { auto* s = shaders_.get(h.id); if (!s) continue;
            for (auto& b : s->binds) { all.push_back(b); if (b.first > maxreg) maxreg = b.first; any = true; } }
        if (!any) return nullptr;
        std::vector<D3D12_ROOT_PARAMETER> params(maxreg + 1);
        for (UINT i = 0; i <= maxreg; ++i) {
            int kind = 0; for (auto& b : all) if (b.first == i) { kind = b.second; break; }   // gaps -> dummy CBV
            D3D12_ROOT_PARAMETER& rp = params[i]; rp = {};
            rp.ParameterType = (kind == 1) ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_CBV;
            rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            rp.Descriptor.ShaderRegister = i; rp.Descriptor.RegisterSpace = 0;
        }
        D3D12_ROOT_SIGNATURE_DESC rsd{}; rsd.NumParameters = (UINT)params.size(); rsd.pParameters = params.data();
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr;
        D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        ID3D12RootSignature* root = nullptr;
        if (blob) { dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)); blob->Release(); }
        if (err) err->Release();
        return root;
    }

    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        D12Pipeline p; p.vl = d.vertex_layout; auto* pl = plls_.get(d.layout.id); if (pl) { p.root = pl->root; if (p.root) p.root->AddRef(); p.set_params = pl->set_params; }
        if (!p.root) {   // no explicit layout: synthesize one from reflection (slot binding)
            p.root = d.compute_shader.valid() ? build_auto_root({ d.compute_shader })
                   : build_auto_root({ d.vertex_shader, d.fragment_shader, d.geometry_shader, d.tess_control_shader, d.tess_eval_shader });
            if (!p.root) { p.root = default_root(); if (p.root) p.root->AddRef(); }   // PSOs require a root signature
        }
        auto blob = [&](ShaderHandle h) -> D3D12_SHADER_BYTECODE { auto* s = shaders_.get(h.id); return s ? D3D12_SHADER_BYTECODE{ s->bytecode.data(), s->bytecode.size() } : D3D12_SHADER_BYTECODE{ nullptr, 0 }; };
        if (d.compute_shader.valid()) {
            p.compute = true; D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {}; cd.pRootSignature = p.root; cd.CS = blob(d.compute_shader);
            dev->CreateComputePipelineState(&cd, IID_PPV_ARGS(&p.pso)); return { pipelines_.alloc(p) };
        }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC gd = {}; gd.pRootSignature = p.root;
        gd.VS = blob(d.vertex_shader); gd.PS = blob(d.fragment_shader);
        std::vector<D3D12_INPUT_ELEMENT_DESC> ie;
        for (int i = 0; i < d.vertex_layout.attribute_count; ++i) { const auto& a = d.vertex_layout.attributes[i]; ie.push_back({ "TEXCOORD", a.location, vertex_format(a.format), a.buffer_slot, a.offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }); }
        gd.InputLayout = { ie.data(), (UINT)ie.size() };
        gd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        gd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; gd.RasterizerState.CullMode = d.rasterizer.cull_mode == CullMode::None ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
        gd.RasterizerState.FrontCounterClockwise = d.rasterizer.front_face == FrontFace::CounterClockwise; gd.RasterizerState.DepthClipEnable = TRUE;
        // Honour the requested BlendState (factors / ops / write mask), not a hardcode.
        auto& rt0 = gd.BlendState.RenderTarget[0];
        rt0.RenderTargetWriteMask = d.blend.write_mask & 0xF; rt0.BlendEnable = d.blend.enabled;
        rt0.SrcBlend  = d12_blend_factor(d.blend.src_color, false); rt0.DestBlend  = d12_blend_factor(d.blend.dst_color, false); rt0.BlendOp      = d12_blend_op(d.blend.color_op);
        rt0.SrcBlendAlpha = d12_blend_factor(d.blend.src_alpha, true); rt0.DestBlendAlpha = d12_blend_factor(d.blend.dst_alpha, true); rt0.BlendOpAlpha = d12_blend_op(d.blend.alpha_op);
        rt0.LogicOp = D3D12_LOGIC_OP_NOOP;
        gd.DepthStencilState.DepthEnable = d.depth_stencil.depth_enable;
        gd.DepthStencilState.DepthWriteMask = d.depth_stencil.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        gd.DepthStencilState.DepthFunc = d12_compare(d.depth_stencil.depth_func);
        gd.DSVFormat = d.depth_stencil.depth_enable ? tex_format(d.depth_format) : DXGI_FORMAT_UNKNOWN;
        gd.SampleMask = UINT_MAX; gd.SampleDesc.Count = d.samples > 1 ? d.samples : 1;
        gd.NumRenderTargets = d.color_format_count; for (int i = 0; i < d.color_format_count; ++i) gd.RTVFormats[i] = tex_format(d.color_formats[i]);
        dev->CreateGraphicsPipelineState(&gd, IID_PPV_ARGS(&p.pso));
        return { pipelines_.alloc(p) };
    }
    void destroy_pipeline(PipelineHandle h) override { auto* p = pipelines_.get(h.id); if (!p) return; if (p->pso) p->pso->Release(); if (p->root) p->root->Release(); pipelines_.release(h.id); }

    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples; td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET;
        D12RenderTarget rt; rt.color_tex = create_texture(td).id;
        if (auto* t = textures_.get(rt.color_tex)) { rt.rtv = alloc_rtv(); dev->CreateRenderTargetView(t->res, nullptr, rt.rtv); }
        return { rts_.alloc(rt) };
    }
    RenderTargetHandle create_depth_target(const DepthStencilDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.usage = TEXTURE_USAGE_DEPTH_STENCIL;
        D12RenderTarget rt; rt.depth_tex = create_texture(td).id;
        if (auto* t = textures_.get(rt.depth_tex)) { rt.dsv = alloc_dsv(); dev->CreateDepthStencilView(t->res, nullptr, rt.dsv); }
        return { rts_.alloc(rt) };
    }
    TextureHandle render_target_texture(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return { -1 }; return { rt->color_tex >= 0 ? rt->color_tex : rt->depth_tex }; }
    void destroy_render_target(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return; if (rt->color_tex >= 0) destroy_texture({ rt->color_tex }); if (rt->depth_tex >= 0) destroy_texture({ rt->depth_tex }); rts_.release(h.id); }
    TextureHandle create_texture_view(const TextureViewDesc& d) override { d12_unsupported("create_texture_view (create another SRV/UAV descriptor)"); return d.texture; }

    // Zero-copy interop: wrap an existing ID3D12Resource* (e.g. from a GStreamer d3d12
    // decoder sharing this device) as a sampled RHI texture. SRVs are created on demand
    // at bind time from t->res, so we only record the resource + its dimensions/format.
    // It starts in COMMON state so the binding-time transition() promotes it correctly.
    TextureHandle import_texture(const NativeTextureDesc& d) override {
        auto* native = (ID3D12Resource*)d.d3d_resource;
        if (!native) { d12_unsupported("import_texture (null d3d_resource)"); return { -1 }; }
        D12Texture t; t.res = native; t.owns_res = d.take_ownership;
        t.tf = d.format; t.fmt = tex_format(d.format); t.w = d.width; t.h = d.height;
        const D3D12_RESOURCE_DESC rd = native->GetDesc();
        if (t.w <= 0) t.w = (int)rd.Width;
        if (t.h <= 0) t.h = (int)rd.Height;
        if (t.fmt == DXGI_FORMAT_UNKNOWN) t.fmt = rd.Format;
        t.state = D3D12_RESOURCE_STATE_COMMON;
        return { textures_.alloc(t) };
    }

    // native ID3D12Fence serves both fences and timelines
    FenceHandle create_fence(bool signaled) override { D12Fence f; dev->CreateFence(signaled ? 1 : 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f.fence)); f.value = signaled ? 1 : 0; return { fences_.alloc(f) }; }
    void destroy_fence(FenceHandle h) override { auto* f = fences_.get(h.id); if (f && f->fence) f->fence->Release(); fences_.release(h.id); }
    bool wait_fence(FenceHandle h, uint64_t) override { auto* f = fences_.get(h.id); if (!f) return false; if (f->fence->GetCompletedValue() < f->value) { HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr); f->fence->SetEventOnCompletion(f->value, e); WaitForSingleObject(e, INFINITE); CloseHandle(e); } return true; }
    bool get_fence_status(FenceHandle h) override { auto* f = fences_.get(h.id); return f && f->fence->GetCompletedValue() >= f->value; }
    void reset_fence(FenceHandle) override {}
    SemaphoreHandle create_semaphore() override { return { sems_.alloc(0) }; }
    void destroy_semaphore(SemaphoreHandle h) override { sems_.release(h.id); }
    void wait_idle() override { flush(); }
    TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial) override { D12Fence f; dev->CreateFence(initial, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f.fence)); f.value = initial; return { timelines_.alloc(f) }; }
    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override { auto* t = timelines_.get(h.id); if (t && t->fence) t->fence->Release(); timelines_.release(h.id); }
    void signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v) override { if (auto* t = timelines_.get(h.id)) t->fence->Signal(v); }
    bool wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v, uint64_t) override { auto* t = timelines_.get(h.id); if (!t) return false; if (t->fence->GetCompletedValue() < v) { HANDLE e = CreateEventA(nullptr, FALSE, FALSE, nullptr); t->fence->SetEventOnCompletion(v, e); WaitForSingleObject(e, INFINITE); CloseHandle(e); } return true; }
    uint64_t get_timeline_value(TimelineSemaphoreHandle h) override { auto* t = timelines_.get(h.id); return t ? t->fence->GetCompletedValue() : 0; }

    QueryHandle create_query(QueryType type) override { D12Query q; q.type = type; D3D12_QUERY_HEAP_DESC hd{ type == QueryType::Occlusion ? D3D12_QUERY_HEAP_TYPE_OCCLUSION : D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 1 }; dev->CreateQueryHeap(&hd, IID_PPV_ARGS(&q.heap)); D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 8; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; q.readback = commit(rd, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); return { queries_.alloc(q) }; }
    void destroy_query(QueryHandle h) override { auto* q = queries_.get(h.id); if (q) { if (q->heap) q->heap->Release(); if (q->readback) q->readback->Release(); } queries_.release(h.id); }
    bool get_query_result(QueryHandle h, uint64_t* out, bool) override { auto* q = queries_.get(h.id); if (!q) return false; UINT64* p = nullptr; D3D12_RANGE rng{ 0, 8 }; if (q->readback->Map(0, &rng, (void**)&p) != S_OK) return false; if (out) *out = *p; q->readback->Unmap(0, nullptr); return true; }

    void* map_buffer(BufferHandle h, uint32_t offset, uint32_t) override {
        // DEFAULT-heap buffers aren't CPU-visible; back the map with an UPLOAD buffer and
        // copy it into the resource on unmap. (Write-back: the caller is expected to fill
        // the mapped range, matching the test and the common upload pattern.)
        auto* b = buffers_.get(h.id); if (!b) return nullptr;
        if (b->map_upload) { b->map_upload->Release(); b->map_upload = nullptr; }
        D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = (b->size + 255) & ~255ull; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        b->map_upload = commit(rd, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* p = nullptr; D3D12_RANGE rng{ 0, 0 }; if (!b->map_upload || b->map_upload->Map(0, &rng, &p) != S_OK) return nullptr;
        return (uint8_t*)p + offset;
    }
    void unmap_buffer(BufferHandle h) override {
        auto* b = buffers_.get(h.id); if (!b || !b->map_upload) return;
        b->map_upload->Unmap(0, nullptr);
        immediate([&](ID3D12GraphicsCommandList* cl) { transition(cl, b, D3D12_RESOURCE_STATE_COPY_DEST); cl->CopyBufferRegion(b->res, 0, b->map_upload, 0, b->size); });
        b->map_upload->Release(); b->map_upload = nullptr;
    }
    void read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !dst) return;
        D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* rb = commit(rd, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        immediate([&](ID3D12GraphicsCommandList* cl) { transition(cl, b, D3D12_RESOURCE_STATE_COPY_SOURCE); cl->CopyBufferRegion(rb, 0, b->res, offset, size); });
        void* p = nullptr; D3D12_RANGE rng{ 0, size }; if (rb->Map(0, &rng, &p) == S_OK) { std::memcpy(dst, p, size); rb->Unmap(0, nullptr); }
        rb->Release();
    }
    void read_texture(TextureHandle h, const TextureRegion& r, void* dst) override {
        auto* t = textures_.get(h.id); if (!t || !dst) return;
        int bw = 1, bh = 1; texture_format_block_dims(t->tf, &bw, &bh);
        const UINT64 srcRowPitch = texture_format_row_pitch(t->tf, r.width);
        const int    rows        = texture_format_row_count(t->tf, r.height);
        const UINT64 rowPitch    = (srcRowPitch + 255) & ~255ull;
        const UINT   fpW = UINT(((r.width  + bw - 1) / bw) * bw);
        const UINT   fpH = UINT(((r.height + bh - 1) / bh) * bh);
        D3D12_RESOURCE_DESC bd = {}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = rowPitch * UINT64(rows); bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* rb = commit(bd, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        immediate([&](ID3D12GraphicsCommandList* cl) {
            transition(cl, t, D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = t->res; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = r.mip;
            D3D12_TEXTURE_COPY_LOCATION dl{}; dl.pResource = rb; dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dl.PlacedFootprint.Footprint = { t->fmt, fpW, fpH, 1, (UINT)rowPitch };
            D3D12_BOX box{ (UINT)r.x, (UINT)r.y, 0, UINT(r.x + fpW), UINT(r.y + fpH), 1 };
            cl->CopyTextureRegion(&dl, 0, 0, 0, &src, &box);
        });
        uint8_t* p = nullptr; D3D12_RANGE rng{ 0, (SIZE_T)(rowPitch * UINT64(rows)) }; if (rb->Map(0, &rng, (void**)&p) == S_OK) { for (int y = 0; y < rows; ++y) std::memcpy((uint8_t*)dst + y * (size_t)srcRowPitch, p + y * rowPitch, (size_t)srcRowPitch); rb->Unmap(0, nullptr); }
        rb->Release();
    }

    void set_debug_name(ObjectType, uint32_t, const char*) override {}
    PipelineCacheHandle create_pipeline_cache(const void*, size_t) override { return { pcaches_.alloc(0) }; }   // TODO: ID3D12PipelineLibrary
    size_t get_pipeline_cache_data(PipelineCacheHandle, void*, size_t) override { return 0; }
    void destroy_pipeline_cache(PipelineCacheHandle h) override { pcaches_.release(h.id); }
    void update_texture_residency(TextureHandle, const TextureRegion&, bool) override { d12_unsupported("sparse residency (reserved resources + UpdateTileMappings)"); }
    AccelStructHandle create_acceleration_structure(const AccelStructDesc&) override { d12_unsupported("acceleration structures (DXR; D3D12 Ultimate)"); return { accels_.alloc(0) }; }
    void destroy_acceleration_structure(AccelStructHandle h) override { accels_.release(h.id); }

    // ---- internal: immediate command-list submit + barriers -----------------
    void transition(ID3D12GraphicsCommandList* cl, D12Buffer* b, D3D12_RESOURCE_STATES to) { if (b->state == to) return; D3D12_RESOURCE_BARRIER br = {}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = { b->res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, b->state, to }; cl->ResourceBarrier(1, &br); b->state = to; }
    void transition(ID3D12GraphicsCommandList* cl, D12Texture* t, D3D12_RESOURCE_STATES to) { if (t->state == to) return; D3D12_RESOURCE_BARRIER br = {}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = { t->res, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, t->state, to }; cl->ResourceBarrier(1, &br); t->state = to; }
    template <class Fn> void immediate(Fn&& fn) {
        ID3D12CommandAllocator* alloc = nullptr; dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
        ID3D12GraphicsCommandList* cl = nullptr; dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl));
        fn(cl); cl->Close(); ID3D12CommandList* lists[] = { cl }; queue->ExecuteCommandLists(1, lists); flush();
        cl->Release(); alloc->Release();
    }

    D12Buffer* buffer(int id) { return buffers_.get(id); }
    D12Texture* texture(int id) { return textures_.get(id); }
    D12Pipeline* pipeline(int id) { return pipelines_.get(id); }
    D12RenderTarget* rt(int id) { return rts_.get(id); }
    D12Query* query(int id) { return queries_.get(id); }
    D12DescriptorSet* descriptor_set(int id) { return dsets_.get(id); }
    D12Sampler* sampler(int id) { return samplers_.get(id); }
    D12Fence* fence(int id) { return fences_.get(id); }
    D12Fence* timeline(int id) { return timelines_.get(id); }

    Pool<D12Buffer> buffers_; Pool<D12Texture> textures_; Pool<D12Sampler> samplers_; Pool<D12Shader> shaders_;
    Pool<D12Pipeline> pipelines_; Pool<D12RenderTarget> rts_; Pool<D12Fence> fences_; Pool<int> sems_;
    Pool<D12Fence> timelines_; Pool<D12Query> queries_; Pool<D12DescSetLayout> dsls_; Pool<D12PipelineLayout> plls_;
    Pool<D12DescriptorSet> dsets_; Pool<int> pcaches_; Pool<int> accels_;
};

// The D3D12 commander records into its own allocator/list; submit executes it on the
// queue. (Descriptor-heap binding for descriptor sets + the swapchain present path
// are scaffolded — see TODOs — as they need GPU-visible heaps and the backbuffer.)
class D12Commander : public GraphicCommander {
public:
    D12Commander(D12Device* d) : dev_(d) {
        dev_->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc_));
        dev_->dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc_, nullptr, IID_PPV_ARGS(&cl_)); cl_->Close();
        dev_->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&frame_fence_));
        frame_event_ = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }
    ~D12Commander() override { if (cl_) cl_->Release(); if (alloc_) alloc_->Release(); if (frame_fence_) frame_fence_->Release(); if (frame_event_) CloseHandle(frame_event_); }
    D12Device* device() const { return dev_; }
    ID3D12GraphicsCommandList* list() const { return cl_; }
    // Signalled on the queue after each submit so the next begin() can wait for the GPU to
    // finish before reusing this allocator (windowed begin->submit->present loops).
    void signal_frame() { if (frame_fence_) dev_->queue->Signal(frame_fence_, ++frame_value_); }

    void begin() override {
        if (frame_value_ > 0 && frame_fence_->GetCompletedValue() < frame_value_) {
            frame_fence_->SetEventOnCompletion(frame_value_, frame_event_);
            WaitForSingleObject(frame_event_, INFINITE);
        }
        alloc_->Reset(); cl_->Reset(alloc_, nullptr);
        ID3D12DescriptorHeap* heaps[] = { dev_->gpu_heap, dev_->samp_heap };
        cl_->SetDescriptorHeaps(2, heaps);
        cur_ = nullptr; has_rtv_ = has_dsv_ = false; bound_bb_ = false;
    }
    void end() override {
        if (bound_bb_) dev_->transition_bb(cl_, bb_idx_, D3D12_RESOURCE_STATE_PRESENT);
        cl_->Close();
    }
    // Bind the current swapchain backbuffer as the colour target (transition to RENDER_TARGET).
    void set_render_target_backbuffer() override {
        dev_->ensure_backbuffers();
        if (dev_->bb_count_ == 0) { has_rtv_ = false; return; }
        bb_idx_ = dev_->current_bb_index();
        dev_->transition_bb(cl_, bb_idx_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cur_rtv_ = dev_->bb_rtv_[bb_idx_]; has_rtv_ = true; has_dsv_ = false; bound_bb_ = true;
        cl_->OMSetRenderTargets(1, &cur_rtv_, FALSE, nullptr);
    }
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) override {
        has_rtv_ = false; has_dsv_ = false;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8]; int n = 0;
        for (int i = 0; i < count && i < 8 && colors; ++i) if (auto* rt = dev_->rt(colors[i].id)) if (auto* t = dev_->texture(rt->color_tex)) {
            dev_->transition(cl_, t, D3D12_RESOURCE_STATE_RENDER_TARGET);
            rtvs[n++] = rt->rtv; if (!has_rtv_) { cur_rtv_ = rt->rtv; has_rtv_ = true; }
        }
        if (depth.valid()) if (auto* rt = dev_->rt(depth.id)) if (auto* t = dev_->texture(rt->depth_tex)) {
            dev_->transition(cl_, t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cur_dsv_ = rt->dsv; has_dsv_ = true;
        }
        cl_->OMSetRenderTargets(n, n ? rtvs : nullptr, FALSE, has_dsv_ ? &cur_dsv_ : nullptr);
    }
    void set_viewport(const Viewport& v) override { D3D12_VIEWPORT vp{ v.x, v.y, v.width, v.height, v.min_depth, v.max_depth }; cl_->RSSetViewports(1, &vp); }
    void set_scissor(const ScissorRect& r) override { D3D12_RECT rc{ r.x, r.y, r.x + r.width, r.y + r.height }; cl_->RSSetScissorRects(1, &rc); }
    void clear_color(const ClearColor& c) override { if (has_rtv_) { float col[4]{ c.r, c.g, c.b, c.a }; cl_->ClearRenderTargetView(cur_rtv_, col, 0, nullptr); } }
    void clear_depth_stencil(const ClearDepthStencil& ds) override { if (has_dsv_) cl_->ClearDepthStencilView(cur_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, ds.depth, ds.stencil, 0, nullptr); }
    void set_pipeline(PipelineHandle h) override { auto* p = dev_->pipeline(h.id); if (!p) return; cur_ = p; cl_->SetPipelineState(p->pso); if (p->root) { if (p->compute) cl_->SetComputeRootSignature(p->root); else cl_->SetGraphicsRootSignature(p->root); } if (!p->compute) cl_->IASetPrimitiveTopology(p->topo); }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; UINT stride = (cur_ && slot < VertexLayout::MAX_BUFFER_SLOTS) ? cur_->vl.strides[slot] : 0; D3D12_VERTEX_BUFFER_VIEW v{ b->res->GetGPUVirtualAddress() + offset, (UINT)(b->size - offset), stride }; cl_->IASetVertexBuffers(slot, 1, &v); }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; D3D12_INDEX_BUFFER_VIEW v{ b->res->GetGPUVirtualAddress() + offset, (UINT)(b->size - offset), fmt == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT }; cl_->IASetIndexBuffer(&v); }
    void bind_texture(uint32_t, TextureHandle) override {}        // via descriptor tables
    void bind_sampler(uint32_t, SamplerHandle) override {}
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override { auto* b = dev_->buffer(h.id); if (b) cl_->SetGraphicsRootConstantBufferView(slot, b->res->GetGPUVirtualAddress() + offset); }
    void push_constants(uint32_t, const void* data, uint32_t size) override { cl_->SetGraphicsRoot32BitConstants(0, size / 4, data, 0); }
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override { auto* b = dev_->buffer(h.id); if (b) cl_->SetGraphicsRootUnorderedAccessView(slot, b->res->GetGPUVirtualAddress() + offset); }
    void bind_storage_texture(uint32_t, TextureHandle, int, StorageAccess) override { d12_unsupported("bind_storage_texture (descriptor table)"); }
    void bind_descriptor_set(uint32_t set_index, DescriptorSetHandle h, const uint32_t*, int) override {
        auto* s = dev_->descriptor_set(h.id); if (!s || !cur_ || set_index >= cur_->set_params.size()) return;
        const SetParams sp = cur_->set_params[set_index];
        const bool compute = cur_->compute;
        auto set_table = [&](int param, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
            if (param < 0) return;
            if (compute) cl_->SetComputeRootDescriptorTable(param, gpu); else cl_->SetGraphicsRootDescriptorTable(param, gpu);
        };
        // CBV/SRV/UAV table: one descriptor per non-sampler write, in append (binding) order.
        int n = 0; for (auto& w : s->writes) if (w.type != BindingType::Sampler) ++n;
        if (n > 0 && sp.srv_param >= 0) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu; dev_->alloc_gpu((UINT)n, cpu, gpu);
            int i = 0;
            for (auto& w : s->writes) {
                if (w.type == BindingType::Sampler) continue;
                D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += SIZE_T(i++) * dev_->gpu_size;
                if (w.type == BindingType::UniformBuffer) {
                    if (auto* b = dev_->buffer(w.buffer.id)) {
                        // BufferLocation (GPU VA + offset) must be 256-byte aligned; SizeInBytes
                        // describes only the bound range (rounded up to 256), not the whole
                        // resource — otherwise a sub-range bind over-reads past the buffer end.
                        const UINT64 range = w.buffer_size ? w.buffer_size : (b->size - w.buffer_offset);
                        D3D12_CONSTANT_BUFFER_VIEW_DESC cv{}; cv.BufferLocation = b->res->GetGPUVirtualAddress() + w.buffer_offset;
                        cv.SizeInBytes = (UINT)((range + 255) & ~255ull); dev_->dev->CreateConstantBufferView(&cv, dst);
                    }
                } else if (w.type == BindingType::StorageBuffer) {
                    if (auto* b = dev_->buffer(w.buffer.id)) {
                        // Structured UAV (RWStructuredBuffer<T>) when the buffer carries a stride,
                        // so element addressing matches the shader; raw UAV (RWByteAddressBuffer)
                        // otherwise. FirstElement/NumElements are in element units for structured.
                        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                        if (b->stride) {
                            uv.Format = DXGI_FORMAT_UNKNOWN;
                            uv.Buffer.FirstElement = w.buffer_offset / b->stride;
                            uv.Buffer.NumElements  = (UINT)((w.buffer_size ? w.buffer_size : b->size) / b->stride);
                            uv.Buffer.StructureByteStride = b->stride;
                        } else {
                            uv.Format = DXGI_FORMAT_R32_TYPELESS;
                            uv.Buffer.FirstElement = w.buffer_offset / 4;
                            uv.Buffer.NumElements  = (UINT)((w.buffer_size ? w.buffer_size : b->size) / 4);
                            uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                        }
                        dev_->dev->CreateUnorderedAccessView(b->res, nullptr, &uv, dst);
                    }
                } else { // SampledTexture / CombinedImageSampler -> SRV (sampled in the shader stages)
                    if (auto* t = dev_->texture(w.texture.id)) {
                        dev_->transition(cl_, t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        dev_->dev->CreateShaderResourceView(t->res, nullptr, dst);
                    }
                }
            }
            set_table(sp.srv_param, gpu);
        }
        // SAMPLER table: one descriptor per sampler / combined-image-sampler write.
        int ns = 0; for (auto& w : s->writes) if (w.type == BindingType::Sampler || w.type == BindingType::CombinedImageSampler) ++ns;
        if (ns > 0 && sp.samp_param >= 0) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu; dev_->alloc_samp((UINT)ns, cpu, gpu);
            int i = 0;
            for (auto& w : s->writes) {
                if (w.type != BindingType::Sampler && w.type != BindingType::CombinedImageSampler) continue;
                D3D12_CPU_DESCRIPTOR_HANDLE dst = cpu; dst.ptr += SIZE_T(i++) * dev_->samp_size;
                if (auto* smp = dev_->sampler(w.sampler.id)) dev_->dev->CreateSampler(&smp->desc, dst);
            }
            set_table(sp.samp_param, gpu);
        }
    }
    void draw(uint32_t vc, uint32_t first, uint32_t inst, uint32_t first_inst) override { cl_->DrawInstanced(vc, inst ? inst : 1, first, first_inst); }
    void draw_indexed(uint32_t ic, uint32_t first, int32_t base_v, uint32_t inst, uint32_t first_inst) override { cl_->DrawIndexedInstanced(ic, inst ? inst : 1, first, base_v, first_inst); }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { cl_->Dispatch(x, y, z); }
    void draw_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { d12_unsupported("draw_indirect (needs a command signature)"); }
    void draw_indexed_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { d12_unsupported("draw_indexed_indirect"); }
    void dispatch_indirect(BufferHandle, uint32_t) override { d12_unsupported("dispatch_indirect"); }
    void draw_mesh_tasks(uint32_t x, uint32_t y, uint32_t z) override {
#if defined(WINDOW_SUPPORT_D3D12_ULTIMATE)
        // ID3D12GraphicsCommandList6::DispatchMesh — needs the cast; scaffolded.
        d12_unsupported("draw_mesh_tasks (cast to ID3D12GraphicsCommandList6::DispatchMesh)");
        (void)x; (void)y; (void)z;
#else
        d12_unsupported("draw_mesh_tasks (build with D3D12 Ultimate)");
#endif
    }
    void draw_mesh_tasks_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { d12_unsupported("draw_mesh_tasks_indirect"); }
    void memory_barrier(uint32_t) override { D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; cl_->ResourceBarrier(1, &b); }
    void copy_buffer(BufferHandle dst, uint32_t doff, BufferHandle src, uint32_t soff, uint32_t size) override { auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (s && d) cl_->CopyBufferRegion(d->res, doff, s->res, soff, size); }
    void copy_texture(TextureHandle dst, const TextureRegion&, TextureHandle src, const TextureRegion&) override { auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (s && d) cl_->CopyResource(d->res, s->res); }
    void blit_render_target(RenderTargetHandle dh, RenderTargetHandle sh,
                            int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1, bool) override {
        auto* d = dev_->rt(dh.id); auto* s = dev_->rt(sh.id); if (!d || !s) return;
        auto* dt = dev_->texture(d->color_tex); auto* st = dev_->texture(s->color_tex); if (!dt || !st) return;
        // 1:1, same-extent, same-origin blit == a resource copy; scaled blits need a quad pass.
        if (sx0 == dx0 && sy0 == dy0 && (sx1 - sx0) == (dx1 - dx0) && (sy1 - sy0) == (dy1 - dy0)) {
            dev_->transition(cl_, st, D3D12_RESOURCE_STATE_COPY_SOURCE);
            dev_->transition(cl_, dt, D3D12_RESOURCE_STATE_COPY_DEST);
            cl_->CopyResource(dt->res, st->res);
        } else d12_unsupported("blit_render_target scaling (draw a fullscreen quad)");
    }
    void resolve_render_target(RenderTargetHandle dh, RenderTargetHandle sh) override {
        auto* d = dev_->rt(dh.id); auto* s = dev_->rt(sh.id); if (!d || !s) return;
        auto* dt = dev_->texture(d->color_tex); auto* st = dev_->texture(s->color_tex); if (!dt || !st) return;
        dev_->transition(cl_, st, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        dev_->transition(cl_, dt, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cl_->ResolveSubresource(dt->res, 0, st->res, 0, dt->fmt);
    }
    void write_timestamp(QueryHandle h) override { if (auto* q = dev_->query(h.id)) { cl_->EndQuery(q->heap, D3D12_QUERY_TYPE_TIMESTAMP, 0); cl_->ResolveQueryData(q->heap, D3D12_QUERY_TYPE_TIMESTAMP, 0, 1, q->readback, 0); } }
    void begin_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) cl_->BeginQuery(q->heap, D3D12_QUERY_TYPE_OCCLUSION, 0); }
    void end_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) { cl_->EndQuery(q->heap, D3D12_QUERY_TYPE_OCCLUSION, 0); cl_->ResolveQueryData(q->heap, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, q->readback, 0); } }
    void push_debug_group(const char*) override {}
    void pop_debug_group() override {}
    void insert_debug_marker(const char*) override {}
    void set_stencil_reference(uint32_t ref) override { cl_->OMSetStencilRef(ref); }
    void set_blend_constants(const float rgba[4]) override { cl_->OMSetBlendFactor(rgba); }
    void set_depth_bias(float, float, float) override { d12_unsupported("dynamic depth bias (set in the PSO)"); }
    void set_line_width(float) override {}
    void set_viewports(const Viewport* v, int n) override { std::vector<D3D12_VIEWPORT> vs(n); for (int i=0;i<n;++i) vs[i]={v[i].x,v[i].y,v[i].width,v[i].height,v[i].min_depth,v[i].max_depth}; cl_->RSSetViewports((UINT)n, vs.data()); }
    void set_scissors(const ScissorRect* r, int n) override { std::vector<D3D12_RECT> rs(n); for (int i=0;i<n;++i) rs[i]={r[i].x,r[i].y,r[i].x+r[i].width,r[i].y+r[i].height}; cl_->RSSetScissorRects((UINT)n, rs.data()); }
    void draw_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { d12_unsupported("draw_indirect_count (ExecuteIndirect with a count buffer)"); }
    void draw_indexed_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { d12_unsupported("draw_indexed_indirect_count"); }
    void build_acceleration_structure(AccelStructHandle, const AccelStructDesc&) override { d12_unsupported("build_acceleration_structure (BuildRaytracingAccelerationStructure)"); }
    void trace_rays(uint32_t, uint32_t, uint32_t) override { d12_unsupported("trace_rays (DispatchRays)"); }

private:
    D12Device* dev_ = nullptr; ID3D12CommandAllocator* alloc_ = nullptr; ID3D12GraphicsCommandList* cl_ = nullptr;
    D12Pipeline* cur_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cur_rtv_{}, cur_dsv_{}; bool has_rtv_ = false, has_dsv_ = false;
    ID3D12Fence* frame_fence_ = nullptr; UINT64 frame_value_ = 0; HANDLE frame_event_ = nullptr;  // per-frame GPU sync
    bool bound_bb_ = false; UINT bb_idx_ = 0;   // current backbuffer bound this frame
};

} // namespace

GraphicDevice* create_device_d3d12(Graphics* context, Result* out_result) {
    if (!context || context->get_backend() != Backend::D3D12 || !context->native_device()) { if (out_result) *out_result = Result::ErrorNotSupported; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new D12Device(context);
}
GraphicCommander* create_commander_d3d12(Graphics* context, GraphicDevice* device, QueueType, Result* out_result) {
    if (!context || !device) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new D12Commander(static_cast<D12Device*>(device));
}
void submit_commander_d3d12(Graphics*, GraphicCommander* commander, FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    if (!commander) return;
    auto* c = static_cast<D12Commander*>(commander); auto* d = c->device();
    ID3D12CommandList* lists[] = { c->list() }; d->queue->ExecuteCommandLists(1, lists);
    if (fence.valid()) if (auto* f = d->fence(fence.id)) { f->value++; d->queue->Signal(f->fence, f->value); }
    if (timeline.valid()) if (auto* t = d->timeline(timeline.id)) d->queue->Signal(t->fence, value);
    c->signal_frame();   // let the next begin() wait for this frame's GPU work (windowed loops)
}

} // namespace window

#else  // not Windows / D3D12 disabled — not-supported stubs

namespace window {
GraphicDevice*    create_device_d3d12(Graphics*, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
GraphicCommander* create_commander_d3d12(Graphics*, GraphicDevice*, QueueType, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
void              submit_commander_d3d12(Graphics*, GraphicCommander*, FenceHandle, TimelineSemaphoreHandle, uint64_t) {}
} // namespace window

#endif
