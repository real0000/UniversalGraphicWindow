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
#include <algorithm>
#include <string>
#include <unordered_map>
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
// The TYPELESS family a format can be cast within, or UNKNOWN when it has none.
// See create_texture(): sampled textures are allocated typeless so create_texture_view()
// can reinterpret their format. Depth formats are excluded — their casts need per-view
// depth/stencil aspect formats, and depth textures here are attachments, not view sources.
DXGI_FORMAT d12_typeless_family(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8_UNORM:              return DXGI_FORMAT_R8_TYPELESS;
        case DXGI_FORMAT_R8G8_UNORM:            return DXGI_FORMAT_R8G8_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM:        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        case DXGI_FORMAT_R16_FLOAT:             return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:    return DXGI_FORMAT_R16G16B16A16_TYPELESS;
        case DXGI_FORMAT_R32_FLOAT:             return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:    return DXGI_FORMAT_R32G32B32A32_TYPELESS;
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:        return DXGI_FORMAT_BC1_TYPELESS;
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:        return DXGI_FORMAT_BC2_TYPELESS;
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:        return DXGI_FORMAT_BC3_TYPELESS;
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:        return DXGI_FORMAT_BC7_TYPELESS;
        default:                                return DXGI_FORMAT_UNKNOWN;
    }
}

D3D12_STENCIL_OP d12_stencil_op(StencilOp o) {
    switch (o) {
        case StencilOp::Keep: return D3D12_STENCIL_OP_KEEP;         case StencilOp::Zero:     return D3D12_STENCIL_OP_ZERO;
        case StencilOp::Replace: return D3D12_STENCIL_OP_REPLACE;   case StencilOp::IncrSat:  return D3D12_STENCIL_OP_INCR_SAT;
        case StencilOp::DecrSat: return D3D12_STENCIL_OP_DECR_SAT;  case StencilOp::Invert:   return D3D12_STENCIL_OP_INVERT;
        case StencilOp::IncrWrap: return D3D12_STENCIL_OP_INCR;     case StencilOp::DecrWrap: return D3D12_STENCIL_OP_DECR;
    }
    return D3D12_STENCIL_OP_KEEP;
}

struct D12Buffer  { ID3D12Resource* res = nullptr; UINT64 size = 0; UINT stride = 0; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON; ID3D12Resource* map_upload = nullptr; };
struct D12Texture { ID3D12Resource* res = nullptr; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; TextureFormat tf = TextureFormat::RGBA8_UNORM; int w = 0, h = 0; D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
                   bool owns_res = true;      // false for imported resources we must not Release
                   bool typeless = false;     // resource allocated in its TYPELESS family (format-castable views)
                   // Sparse (reserved) textures own no memory until tiles are mapped; the heap
                   // below backs whatever update_texture_residency() has committed so far.
                   bool sparse = false; ID3D12Heap* tile_heap = nullptr; UINT tiles_committed = 0;
                   // A texture VIEW (create_texture_view) shares another texture's resource but
                   // reinterprets it. SRVs are otherwise created on demand with a null desc
                   // (= whole resource, native format); has_srv_desc makes that explicit instead.
                   bool has_srv_desc = false; D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{}; };
struct D12Sampler { D3D12_SAMPLER_DESC desc{}; };
// Reflected resource binding from DXBC: (register, kind). kind: 0=CBV(b#), 1=UAV(u#).
// (SRV/sampler slot binding on D3D12's auto path is a follow-up; explicit layouts handle them.)
struct D12Shader  { std::vector<uint8_t> bytecode; ShaderStage stage = ShaderStage::Vertex;
                    std::vector<std::pair<UINT, int>> binds; };
// Per descriptor set, the root-parameter index of its CBV/SRV/UAV table and (separate,
// as D3D12 requires) its SAMPLER table. -1 = that set has no table of that kind.
struct SetParams { int srv_param = -1; int samp_param = -1; };
struct D12Pipeline{ ID3D12PipelineState* pso = nullptr; ID3D12RootSignature* root = nullptr; bool compute = false; bool mesh = false;
                    D3D_PRIMITIVE_TOPOLOGY topo = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; VertexLayout vl;
                    std::vector<SetParams> set_params; };
struct D12RenderTarget { int color_tex = -1; int depth_tex = -1; D3D12_CPU_DESCRIPTOR_HANDLE rtv{}; D3D12_CPU_DESCRIPTOR_HANDLE dsv{}; };
struct D12Fence   { ID3D12Fence* fence = nullptr; UINT64 value = 0; };
struct D12Query   { ID3D12QueryHeap* heap = nullptr; ID3D12Resource* readback = nullptr; QueryType type = QueryType::Timestamp; };
struct D12DescSetLayout { DescriptorSetLayoutDesc desc; };
struct D12PipelineLayout{ ID3D12RootSignature* root = nullptr; PipelineLayoutDesc desc; std::vector<SetParams> set_params; };
struct D12DescriptorSet { std::vector<DescriptorWrite> writes; };
// A DXR acceleration structure: the built result plus the scratch space its builds need.
struct D12Accel { ID3D12Resource* result = nullptr; ID3D12Resource* scratch = nullptr;
                  UINT64 result_size = 0, scratch_size = 0; AccelStructDesc desc; };
// A ray-tracing pipeline: the DXR state object plus the shader binding table built from it.
// The table's three regions live in one upload buffer; DispatchRays addresses them by
// (start, size, stride), so the ranges are kept alongside it.
struct D12RayPipeline {
    ID3D12StateObject* state = nullptr;
    ID3D12Resource*    sbt = nullptr;
    ID3D12RootSignature* global_root = nullptr;
    std::vector<ID3D12RootSignature*> local_roots;   // rebuilt with the LOCAL flag; owned here
    D3D12_GPU_VIRTUAL_ADDRESS raygen_addr = 0; UINT64 raygen_size = 0;
    D3D12_GPU_VIRTUAL_ADDRESS miss_addr = 0;   UINT64 miss_size = 0, miss_stride = 0;
    D3D12_GPU_VIRTUAL_ADDRESS hit_addr = 0;    UINT64 hit_size = 0,  hit_stride = 0;
    std::vector<SetParams> set_params;   // global-layout tables, for bind_descriptor_set
};

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
    std::unordered_map<uint64_t, ID3D12CommandSignature*> cmd_sigs_;   // (arg type, stride) -> signature
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
    // ---- indirect-draw command signatures -----------------------------------
    // D3D12 has no bare "draw from buffer" call: ExecuteIndirect needs a command
    // signature describing the argument layout. These carry a single argument and no
    // root-argument changes, so the root signature is null and one signature serves
    // every pipeline. The stride is part of the signature, so they are cached per
    // (argument type, stride) — callers may pack argument structs with padding.
    ID3D12CommandSignature* command_signature(D3D12_INDIRECT_ARGUMENT_TYPE type, UINT stride) {
        UINT natural = sizeof(D3D12_DRAW_ARGUMENTS);
        if (type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED) natural = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH) natural = sizeof(D3D12_DISPATCH_ARGUMENTS);
        else if (type == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH) natural = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
        if (stride == 0) stride = natural;
        const uint64_t key = (uint64_t(type) << 32) | stride;
        auto it = cmd_sigs_.find(key);
        if (it != cmd_sigs_.end()) return it->second;
        D3D12_INDIRECT_ARGUMENT_DESC arg{}; arg.Type = type;
        D3D12_COMMAND_SIGNATURE_DESC sd{}; sd.ByteStride = stride; sd.NumArgumentDescs = 1; sd.pArgumentDescs = &arg;
        ID3D12CommandSignature* sig = nullptr;
        if (FAILED(dev->CreateCommandSignature(&sd, nullptr, IID_PPV_ARGS(&sig)))) return nullptr;
        cmd_sigs_.emplace(key, sig);
        return sig;
    }

    // Write a texture UAV descriptor at `dst`. A UAV must name a typed format and the
    // matching view dimension: a null desc is only valid for a typed resource, and sampled
    // textures here are allocated TYPELESS (see create_texture). Multisampled textures
    // cannot be UAVs at all, so they are rejected rather than silently mis-viewed.
    bool write_texture_uav(D12Texture* t, int mip, D3D12_CPU_DESCRIPTOR_HANDLE dst) {
        if (!t || !t->res) return false;
        const D3D12_RESOURCE_DESC rd = t->res->GetDesc();
        if (rd.SampleDesc.Count > 1) { d12_unsupported("storage texture (multisampled UAV)"); return false; }
        if (!(rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
            d12_unsupported("storage texture (missing TEXTURE_USAGE_STORAGE)"); return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = t->fmt;
        if (rd.DepthOrArraySize > 1) {
            uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            uv.Texture2DArray.MipSlice = (UINT)mip; uv.Texture2DArray.ArraySize = rd.DepthOrArraySize;
        } else {
            uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uv.Texture2D.MipSlice = (UINT)mip;
        }
        dev->CreateUnorderedAccessView(t->res, nullptr, &uv, dst);
        return true;
    }

    // ---- scaled-blit pass ---------------------------------------------------
    // D3D12's CopyResource/CopyTextureRegion are 1:1, so a stretched blit is a
    // fullscreen-triangle draw that samples the source. The PSO is cached per render-target
    // format (a PSO bakes its RTV format in); the root signature carries the source rect as
    // root constants and both filters as static samplers, so no CB or sampler heap is needed.
    ID3D12RootSignature* blit_root_ = nullptr;
    std::unordered_map<uint32_t, ID3D12PipelineState*> blit_psos_;
    ID3DBlob* blit_vs_ = nullptr;
    ID3DBlob* blit_ps_ = nullptr;
    bool ensure_blit_shaders() {
        if (blit_root_) return true;
        static const char kHLSL[] =
            "cbuffer B : register(b0) { float4 uSrc; uint uLinear; }\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut vs_main(uint id : SV_VertexID) {\n"
            "  float2 p = float2((id << 1) & 2, id & 2);\n"
            "  VSOut o; o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1);\n"
            "  o.uv = lerp(uSrc.xy, uSrc.zw, p);\n"
            "  return o;\n"
            "}\n"
            "Texture2D uTex : register(t0);\n"
            "SamplerState sPoint : register(s0);\n"
            "SamplerState sLinear : register(s1);\n"
            "float4 ps_main(VSOut i) : SV_Target {\n"
            "  return uLinear ? uTex.Sample(sLinear, i.uv) : uTex.Sample(sPoint, i.uv);\n"
            "}\n";
        ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(kHLSL, sizeof(kHLSL) - 1, nullptr, nullptr, nullptr, "vs_main", "vs_5_0", 0, 0, &blit_vs_, &err)) ||
            FAILED(D3DCompile(kHLSL, sizeof(kHLSL) - 1, nullptr, nullptr, nullptr, "ps_main", "ps_5_0", 0, 0, &blit_ps_, &err))) {
            if (err) err->Release();
            if (blit_vs_) { blit_vs_->Release(); blit_vs_ = nullptr; }
            if (blit_ps_) { blit_ps_->Release(); blit_ps_ = nullptr; }
            return false;
        }
        if (err) err->Release();
        D3D12_DESCRIPTOR_RANGE srv{}; srv.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; srv.NumDescriptors = 1; srv.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[0].Constants = { 0, 0, 5 };                       // float4 uSrc + uint uLinear
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[1].DescriptorTable = { 1, &srv };
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC ss[2] = {};
        for (int i = 0; i < 2; ++i) {
            ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss[i].ShaderRegister = i; ss[i].MaxLOD = D3D12_FLOAT32_MAX;
            ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        ss[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 2; rd.pParameters = rp;
        rd.NumStaticSamplers = 2; rd.pStaticSamplers = ss;
        rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* sig = nullptr;
        D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr);
        if (sig) { dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&blit_root_)); sig->Release(); }
        return blit_root_ != nullptr;
    }
    ID3D12PipelineState* blit_pso(DXGI_FORMAT rtv_format) {
        if (!ensure_blit_shaders()) return nullptr;
        auto it = blit_psos_.find((uint32_t)rtv_format);
        if (it != blit_psos_.end()) return it->second;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = blit_root_;
        pd.VS = { blit_vs_->GetBufferPointer(), blit_vs_->GetBufferSize() };
        pd.PS = { blit_ps_->GetBufferPointer(), blit_ps_->GetBufferSize() };
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.DepthStencilState.DepthEnable = FALSE; pd.DepthStencilState.StencilEnable = FALSE;
        pd.SampleMask = UINT_MAX; pd.SampleDesc.Count = 1;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1; pd.RTVFormats[0] = rtv_format;
        ID3D12PipelineState* pso = nullptr;
        dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso));
        if (pso) blit_psos_.emplace((uint32_t)rtv_format, pso);
        return pso;
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
    ~D12Device() override { if (mip_pso_) mip_pso_->Release(); if (mip_root_) mip_root_->Release();
                            for (auto& kv : blit_psos_) if (kv.second) kv.second->Release(); blit_psos_.clear();
                            if (blit_root_) blit_root_->Release(); if (blit_vs_) blit_vs_->Release(); if (blit_ps_) blit_ps_->Release();
                            for (auto& kv : cmd_sigs_) if (kv.second) kv.second->Release(); cmd_sigs_.clear();
                            for (UINT i = 0; i < bb_count_; ++i) if (backbuffers_[i]) backbuffers_[i]->Release(); if (bb_rtv_heap_) bb_rtv_heap_->Release(); if (swap3_) swap3_->Release(); if (samp_heap) samp_heap->Release(); if (gpu_heap) gpu_heap->Release(); if (default_root_) default_root_->Release(); if (rtv_heap) rtv_heap->Release(); if (dsv_heap) dsv_heap->Release(); if (imm_fence) imm_fence->Release(); if (imm_event) CloseHandle(imm_event); }

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
        out->max_push_constant_size = 256; out->compute_shaders = true; out->indirect_draw = true; out->timestamp_query = true;
        out->read_write_textures = true;   // UAV textures (see bind_storage_texture)
        // Query the optional features rather than asserting them: mesh_shaders was hardcoded
        // true even though a mesh PSO could not be created, so callers had no way to tell.
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7{};
        out->mesh_shaders = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof o7)) &&
                            o7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED;
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
        out->ray_tracing = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof o5)) &&
                           o5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        D3D12_FEATURE_DATA_D3D12_OPTIONS o0{};
        out->sparse_textures = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o0, sizeof o0)) &&
                               o0.TiledResourcesTier != D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
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
        // Allocate a sampled texture in its TYPELESS family so create_texture_view() can
        // reinterpret the format (an sRGB view of a UNORM texture, say). D3D12 only permits
        // that cast when the RESOURCE is typeless; on a typed one the reinterpreting SRV is
        // invalid at bind time. The default SRV names the typed format explicitly (see the
        // descriptor-write path), so a typeless resource stays transparent to callers.
        if (d.usage & TEXTURE_USAGE_SAMPLED) {
            const DXGI_FORMAT typeless = d12_typeless_family(t.fmt);
            if (typeless != DXGI_FORMAT_UNKNOWN) { rd.Format = typeless; t.typeless = true; }
        }
        t.state = D3D12_RESOURCE_STATE_COMMON;
        if (d.sparse) {
            // A reserved resource has an address range but no memory: tiles are mapped later
            // by update_texture_residency(). CreateCommittedResource would defeat the point.
            // A reserved texture must declare a TILED layout: D3D12 rejects the usual
            // LAYOUT_UNKNOWN here with E_INVALIDARG (verified against the debug layer),
            // because tiles only have meaning under the 64KB swizzle.
            rd.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
            const HRESULT hr = dev->CreateReservedResource(&rd, t.state, nullptr, IID_PPV_ARGS(&t.res));
            if (!t.res) {
                // Do NOT fall back to a committed resource: the caller asked for a sparse
                // texture and would go on calling update_texture_residency() against
                // something that is fully resident, with no way to notice.
                static char msg[96];
                std::snprintf(msg, sizeof msg, "sparse texture (CreateReservedResource failed, hr=0x%08lX)", (unsigned long)hr);
                d12_unsupported(msg);
                return { -1 };
            }
            t.sparse = true;
        }
        if (!t.res) t.res = commit(rd, D3D12_HEAP_TYPE_DEFAULT, t.state);
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
    // D3D12 has no GenerateMips (that is a D3D11 context service), so each level is produced
    // by a compute pass that box-filters the level above it. Without this, a texture created
    // with mips keeps whatever was in levels 1..N (nothing), which is what the asset loader
    // was getting.
    //
    // The PSO is built once and cached. Its shader is compiled with D3DCompile, already
    // linked here for reflection, so no offline fxc step or embedded blob is needed.
    ID3D12RootSignature* mip_root_ = nullptr;
    ID3D12PipelineState* mip_pso_  = nullptr;
    bool ensure_mip_pso() {
        if (mip_pso_) return true;
        static const char kHLSL[] =
            "Texture2D<float4> uSrc : register(t0);\n"
            "RWTexture2D<float4> uDst : register(u0);\n"
            "[numthreads(8,8,1)]\n"
            "void cs_main(uint3 id : SV_DispatchThreadID) {\n"
            "  uint2 d = id.xy; uint2 s = d * 2;\n"
            "  float4 c = uSrc.Load(int3(s,0)) + uSrc.Load(int3(s + uint2(1,0),0))\n"
            "           + uSrc.Load(int3(s + uint2(0,1),0)) + uSrc.Load(int3(s + uint2(1,1),0));\n"
            "  uDst[d] = c * 0.25;\n"
            "}\n";
        ID3DBlob* code = nullptr; ID3DBlob* err = nullptr;
        if (FAILED(D3DCompile(kHLSL, sizeof(kHLSL) - 1, nullptr, nullptr, nullptr, "cs_main", "cs_5_0", 0, 0, &code, &err))) {
            if (err) err->Release();
            d12_unsupported("generate_mipmaps (downsample shader compile failed)");
            return false;
        }
        if (err) err->Release();
        // Two single-entry tables: SRV (the level being read) and UAV (the level written).
        D3D12_DESCRIPTOR_RANGE rs{}; rs.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; rs.NumDescriptors = 1; rs.BaseShaderRegister = 0;
        D3D12_DESCRIPTOR_RANGE ru{}; ru.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ru.NumDescriptors = 1; ru.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[0].DescriptorTable = { 1, &rs };
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[1].DescriptorTable = { 1, &ru };
        D3D12_ROOT_SIGNATURE_DESC rd{}; rd.NumParameters = 2; rd.pParameters = rp;
        ID3DBlob* sig = nullptr;
        D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, nullptr);
        if (sig) { dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&mip_root_)); sig->Release(); }
        if (!mip_root_) { code->Release(); d12_unsupported("generate_mipmaps (root signature)"); return false; }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = mip_root_;
        pd.CS = { code->GetBufferPointer(), code->GetBufferSize() };
        dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&mip_pso_));
        code->Release();
        if (!mip_pso_) { d12_unsupported("generate_mipmaps (compute PSO)"); return false; }
        return true;
    }
    void generate_mipmaps(TextureHandle h) override {
        auto* t = textures_.get(h.id); if (!t || !t->res) return;
        const D3D12_RESOURCE_DESC rd = t->res->GetDesc();
        if (rd.MipLevels <= 1) return;
        if (!(rd.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) {
            // Writing a mip needs a UAV; the texture has to have been created for it.
            d12_unsupported("generate_mipmaps (texture lacks TEXTURE_USAGE_STORAGE)");
            return;
        }
        if (rd.DepthOrArraySize > 1) {
            // Each array slice would need its own dispatch chain and subresource indexing.
            d12_unsupported("generate_mipmaps (array textures)");
            return;
        }
        if (!ensure_mip_pso()) return;
        immediate([&](ID3D12GraphicsCommandList* cl) {
            ID3D12DescriptorHeap* heaps[] = { gpu_heap };
            cl->SetDescriptorHeaps(1, heaps);
            cl->SetComputeRootSignature(mip_root_);
            cl->SetPipelineState(mip_pso_);
            transition(cl, t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // Levels need DIFFERENT states at the same time — the one being read must be a
            // shader resource while the one being written is a UAV — so these barriers are
            // per-subresource rather than the whole-resource transition() used elsewhere.
            auto sub_barrier = [&](UINT sub, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
                D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition = { t->res, sub, from, to };
                cl->ResourceBarrier(1, &b);
            };
            UINT w = (UINT)rd.Width, hgt = rd.Height;
            for (UINT level = 1; level < rd.MipLevels; ++level) {
                const UINT nw = w > 1 ? w / 2 : 1, nh = hgt > 1 ? hgt / 2 : 1;
                sub_barrier(level - 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
                alloc_gpu(2, cpu, gpu);   // [0] = SRV of level-1, [1] = UAV of level
                D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
                sd.Format = t->fmt; sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip = level - 1; sd.Texture2D.MipLevels = 1;
                dev->CreateShaderResourceView(t->res, &sd, cpu);
                D3D12_CPU_DESCRIPTOR_HANDLE ucpu = cpu; ucpu.ptr += SIZE_T(gpu_size);
                D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
                ud.Format = t->fmt; ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; ud.Texture2D.MipSlice = level;
                dev->CreateUnorderedAccessView(t->res, nullptr, &ud, ucpu);
                D3D12_GPU_DESCRIPTOR_HANDLE ugpu = gpu; ugpu.ptr += UINT64(gpu_size);
                cl->SetComputeRootDescriptorTable(0, gpu);
                cl->SetComputeRootDescriptorTable(1, ugpu);
                cl->Dispatch((nw + 7) / 8, (nh + 7) / 8, 1);
                // The write must land before the next iteration reads this level.
                D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = t->res;
                cl->ResourceBarrier(1, &uav);
                w = nw; hgt = nh;
            }
            // Levels 0..N-2 are shader resources by now; bring the last one to match so the
            // whole resource is in one state again and the tracker stays truthful.
            sub_barrier(rd.MipLevels - 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            t->state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        });
    }
    void destroy_texture(TextureHandle h) override {
        auto* t = textures_.get(h.id); if (!t) return;
        if (t->tile_heap) t->tile_heap->Release();     // sparse backing store
        if (t->res && t->owns_res) t->res->Release();
        textures_.release(h.id);
    }

    SamplerHandle create_sampler(const SamplerState& s) override { D12Sampler smp; smp.desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; smp.desc.AddressU = smp.desc.AddressV = smp.desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; smp.desc.MaxLOD = D3D12_FLOAT32_MAX; (void)s; return { samplers_.alloc(smp) }; }
    void destroy_sampler(SamplerHandle h) override { samplers_.release(h.id); }

    ShaderHandle create_shader(const ShaderDesc& d) override {
        // Auto means "this backend's native bytecode", which here is DXBC or DXIL — both are
        // handed to D3D12 verbatim. Rejecting Auto made the documented default unusable.
        if (d.language != ShaderLanguage::Auto && d.language != ShaderLanguage::DXIL && d.language != ShaderLanguage::DXBC) {
            d12_unsupported("non-DXIL shader (compile HLSL to DXIL)"); return { -1 };
        }
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
                         bd.Type == D3D_SIT_UAV_CONSUME_STRUCTURED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER) {
                    // Buffer UAVs can be root descriptors; TEXTURE UAVs cannot — a root
                    // descriptor is a raw GPU address, and a texture needs a real descriptor
                    // in a heap. Kind 2 tells build_auto_root to give this register a
                    // descriptor table instead (see bind_storage_texture).
                    const bool tex_uav = bd.Dimension != D3D_SRV_DIMENSION_BUFFER &&
                                         bd.Dimension != D3D_SRV_DIMENSION_UNKNOWN;
                    sh.binds.push_back({ bd.BindPoint, tex_uav ? 2 : 1 });
                }
            }
            refl->Release();
        }
        return { shaders_.alloc(std::move(sh)) };
    }
    void destroy_shader(ShaderHandle h) override { shaders_.release(h.id); }

    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override { return { dsls_.alloc(D12DescSetLayout{ d }) }; }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { dsls_.release(h.id); }
    // Build a root signature from a layout description. `local` produces a LOCAL root
    // signature — the kind a ray-tracing shader record carries its own arguments through.
    // D3D12 requires that flag at serialization time and rejects a state object built with
    // a global signature in a local slot, so the same layout has to be serialized twice
    // when it is used both ways.
    ID3D12RootSignature* build_root_signature(const PipelineLayoutDesc& d, bool local,
                                              std::vector<SetParams>* out_set_params) {
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
                    // A ray-tracing scene is read as an SRV, so it shares the SRV table.
                    case BindingType::AccelerationStructure: res.push_back({ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, bd.count, bd.binding, 0, APPEND }); break;
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
        D3D12_ROOT_SIGNATURE_DESC rsd = {}; rsd.NumParameters = (UINT)params.size(); rsd.pParameters = params.data();
        rsd.Flags = local ? D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE
                          : D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* blob = nullptr; ID3DBlob* err = nullptr; D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
        ID3D12RootSignature* root = nullptr;
        if (blob) { dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)); blob->Release(); }
        if (err) err->Release();
        if (out_set_params) *out_set_params = std::move(set_params);
        return root;
    }

    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& d) override {
        D12PipelineLayout pl; pl.desc = d;
        pl.root = build_root_signature(d, /*local=*/false, &pl.set_params);
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
        // Ranges must outlive serialization (the descriptor tables point at them).
        std::vector<D3D12_DESCRIPTOR_RANGE> tex_ranges(maxreg + 1);
        for (UINT i = 0; i <= maxreg; ++i) {
            int kind = 0; for (auto& b : all) if (b.first == i) { kind = b.second; break; }   // gaps -> dummy CBV
            D3D12_ROOT_PARAMETER& rp = params[i]; rp = {};
            rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            if (kind == 2) {   // texture UAV: descriptor table (a root UAV cannot hold a texture)
                D3D12_DESCRIPTOR_RANGE& r = tex_ranges[i]; r = {};
                r.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; r.NumDescriptors = 1;
                r.BaseShaderRegister = i; r.RegisterSpace = 0;
                r.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
                rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rp.DescriptorTable.NumDescriptorRanges = 1;
                rp.DescriptorTable.pDescriptorRanges = &r;
                continue;
            }
            rp.ParameterType = (kind == 1) ? D3D12_ROOT_PARAMETER_TYPE_UAV : D3D12_ROOT_PARAMETER_TYPE_CBV;
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
            // Reflection is DXBC-only (D3DReflect cannot read DXIL), so a DXIL pipeline —
            // which mesh shaders always are — yields nothing here and falls back to the empty
            // default root signature. That is correct for shaders with no resources; DXIL
            // shaders that bind resources need an explicit PipelineLayout.
            p.root = d.compute_shader.valid() ? build_auto_root({ d.compute_shader })
                   : (d.mesh_shader.valid()
                        ? build_auto_root({ d.task_shader, d.mesh_shader, d.fragment_shader })
                        : build_auto_root({ d.vertex_shader, d.fragment_shader, d.geometry_shader, d.tess_control_shader, d.tess_eval_shader }));
            if (!p.root) { p.root = default_root(); if (p.root) p.root->AddRef(); }   // PSOs require a root signature
        }
        auto blob = [&](ShaderHandle h) -> D3D12_SHADER_BYTECODE { auto* s = shaders_.get(h.id); return s ? D3D12_SHADER_BYTECODE{ s->bytecode.data(), s->bytecode.size() } : D3D12_SHADER_BYTECODE{ nullptr, 0 }; };
        if (d.compute_shader.valid()) {
            p.compute = true; D3D12_COMPUTE_PIPELINE_STATE_DESC cd = {}; cd.pRootSignature = p.root; cd.CS = blob(d.compute_shader);
            dev->CreateComputePipelineState(&cd, IID_PPV_ARGS(&p.pso)); return { pipelines_.alloc(p) };
        }
        if (d.mesh_shader.valid()) {
            // A mesh pipeline has no input assembler, so it cannot be described by
            // D3D12_GRAPHICS_PIPELINE_STATE_DESC at all — it is built from a subobject
            // STREAM via ID3D12Device2::CreatePipelineState. Each subobject is a
            // {type, value} pair, aligned to a pointer boundary.
            p.mesh = true;
            ID3D12Device2* dev2 = nullptr;
            if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dev2))) || !dev2) {
                d12_unsupported("mesh pipeline (device has no CreatePipelineState)");
                return { pipelines_.alloc(p) };
            }
            // Each subobject is {type, value}; the stream is parsed by reading a type and
            // advancing by THAT type's size, so entries must be laid out packed with each
            // one starting on a void* boundary — not as a uniform-stride array. A fixed
            // struct with alignas(void*) on every type tag gives exactly that layout.
            // Optional stages are still present with a null bytecode, which D3D12 reads as
            // "no such stage", keeping the layout constant.
            struct MeshStream {
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_root = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
                ID3D12RootSignature* root = nullptr;
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_as = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
                D3D12_SHADER_BYTECODE as{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_ms = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
                D3D12_SHADER_BYTECODE ms{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_ps = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
                D3D12_SHADER_BYTECODE ps{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_rast = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
                D3D12_RASTERIZER_DESC rast{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_blend = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
                D3D12_BLEND_DESC blend{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_ds = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
                D3D12_DEPTH_STENCIL_DESC ds{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_rtv = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
                D3D12_RT_FORMAT_ARRAY rtvs{};
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_dsv = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
                DXGI_FORMAT dsv = DXGI_FORMAT_UNKNOWN;
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_sample = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
                DXGI_SAMPLE_DESC sample{ 1, 0 };
                alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE t_mask = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
                UINT mask = UINT_MAX;
            } ms{};
            ms.root = p.root;
            if (d.task_shader.valid()) ms.as = blob(d.task_shader);
            ms.ms = blob(d.mesh_shader);
            if (d.fragment_shader.valid()) ms.ps = blob(d.fragment_shader);
            ms.rast.FillMode = d.rasterizer.fill_mode == FillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
            ms.rast.CullMode = d.rasterizer.cull_mode == CullMode::None ? D3D12_CULL_MODE_NONE
                             : (d.rasterizer.cull_mode == CullMode::Front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK);
            ms.rast.FrontCounterClockwise = d.rasterizer.front_face == FrontFace::CounterClockwise;
            ms.rast.DepthClipEnable = d.rasterizer.depth_clip_enable;
            ms.blend.RenderTarget[0].RenderTargetWriteMask = d.blend.write_mask & 0xF;
            ms.blend.RenderTarget[0].BlendEnable = d.blend.enabled;
            ms.blend.RenderTarget[0].SrcBlend  = d12_blend_factor(d.blend.src_color, false);
            ms.blend.RenderTarget[0].DestBlend = d12_blend_factor(d.blend.dst_color, false);
            ms.blend.RenderTarget[0].BlendOp   = d12_blend_op(d.blend.color_op);
            ms.blend.RenderTarget[0].SrcBlendAlpha  = d12_blend_factor(d.blend.src_alpha, true);
            ms.blend.RenderTarget[0].DestBlendAlpha = d12_blend_factor(d.blend.dst_alpha, true);
            ms.blend.RenderTarget[0].BlendOpAlpha   = d12_blend_op(d.blend.alpha_op);
            ms.blend.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
            ms.ds.DepthEnable = d.depth_stencil.depth_enable;
            ms.ds.DepthWriteMask = d.depth_stencil.depth_write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
            ms.ds.DepthFunc = d12_compare(d.depth_stencil.depth_func);
            ms.ds.StencilEnable = d.depth_stencil.stencil_enable;
            ms.rtvs.NumRenderTargets = d.color_format_count;
            for (int i = 0; i < d.color_format_count && i < 8; ++i) ms.rtvs.RTFormats[i] = tex_format(d.color_formats[i]);
            ms.dsv = (d.depth_stencil.depth_enable || d.depth_stencil.stencil_enable) ? tex_format(d.depth_format) : DXGI_FORMAT_UNKNOWN;
            ms.sample.Count = d.samples > 1 ? d.samples : 1;

            D3D12_PIPELINE_STATE_STREAM_DESC sd{};
            sd.SizeInBytes = sizeof(ms);
            sd.pPipelineStateSubobjectStream = &ms;
            dev2->CreatePipelineState(&sd, IID_PPV_ARGS(&p.pso));
            dev2->Release();
            if (!p.pso) d12_unsupported("mesh pipeline (CreatePipelineState failed)");
            return { pipelines_.alloc(p) };
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
        // Stencil (used by the GUI's clip masks); the reference value is dynamic (OMSetStencilRef).
        gd.DepthStencilState.StencilEnable    = d.depth_stencil.stencil_enable;
        gd.DepthStencilState.StencilReadMask  = d.depth_stencil.stencil_read_mask;
        gd.DepthStencilState.StencilWriteMask = d.depth_stencil.stencil_write_mask;
        auto face = [](const StencilOpDesc& s) {
            D3D12_DEPTH_STENCILOP_DESC o{};
            o.StencilFailOp      = d12_stencil_op(s.stencil_fail);
            o.StencilDepthFailOp = d12_stencil_op(s.depth_fail);
            o.StencilPassOp      = d12_stencil_op(s.pass);
            o.StencilFunc        = d12_compare(s.func);
            return o;
        };
        gd.DepthStencilState.FrontFace = face(d.depth_stencil.front_face);
        gd.DepthStencilState.BackFace  = face(d.depth_stencil.back_face);
        // A stencil-only pipeline (GUI clip masks: depth off, stencil on) still needs a DSV
        // format, or the PSO won't match a bound depth-stencil and the stencil test is dropped.
        const bool uses_ds = d.depth_stencil.depth_enable || d.depth_stencil.stencil_enable;
        gd.DSVFormat = uses_ds ? tex_format(d.depth_format) : DXGI_FORMAT_UNKNOWN;
        gd.SampleMask = UINT_MAX; gd.SampleDesc.Count = d.samples > 1 ? d.samples : 1;
        gd.NumRenderTargets = d.color_format_count; for (int i = 0; i < d.color_format_count; ++i) gd.RTVFormats[i] = tex_format(d.color_formats[i]);
        dev->CreateGraphicsPipelineState(&gd, IID_PPV_ARGS(&p.pso));
        return { pipelines_.alloc(p) };
    }
    void destroy_pipeline(PipelineHandle h) override { auto* p = pipelines_.get(h.id); if (!p) return; if (p->pso) p->pso->Release(); if (p->root) p->root->Release(); pipelines_.release(h.id); }

    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples; td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET;
        D12RenderTarget rt; rt.color_tex = create_texture(td).id;
        if (auto* t = textures_.get(rt.color_tex)) {
            rt.rtv = alloc_rtv();
            // A render target is also SAMPLED, so its resource is typeless (format-castable
            // views); a typeless resource has no implied view format and a null RTV desc is
            // rejected, leaving no render target at all. Name the typed format explicitly.
            D3D12_RENDER_TARGET_VIEW_DESC rv{}; rv.Format = t->fmt;
            rv.ViewDimension = (d.samples > 1) ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                                               : D3D12_RTV_DIMENSION_TEXTURE2D;
            dev->CreateRenderTargetView(t->res, t->typeless ? &rv : nullptr, rt.rtv);
        }
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
    // A reinterpreting view over the SAME ID3D12Resource: different format and/or a
    // mip/array sub-range. D3D12 has no view object to create up front — descriptors are
    // written into the GPU heap at bind time — so the view is recorded as an SRV desc the
    // bind path uses instead of the null (whole-resource) default. Returning the source
    // handle, as this used to, silently ignored every field of the desc.
    TextureHandle create_texture_view(const TextureViewDesc& d) override {
        auto* src = textures_.get(d.texture.id);
        if (!src || !src->res) return { -1 };
        const D3D12_RESOURCE_DESC rd = src->res->GetDesc();
        D12Texture v = *src;
        v.tf  = (d.format == TextureFormat::Unknown) ? src->tf : d.format;
        v.fmt = (d.format == TextureFormat::Unknown) ? src->fmt : tex_format(d.format);
        // Shares the resource, holding its own reference: owns_res stays true so
        // destroy_texture() releases exactly the AddRef below and the source is unaffected.
        v.res->AddRef(); v.owns_res = true;
        const UINT mips   = d.mip_count   ? (UINT)d.mip_count   : rd.MipLevels - d.base_mip;
        const UINT layers = d.layer_count ? (UINT)d.layer_count : rd.DepthOrArraySize - d.base_layer;
        D3D12_SHADER_RESOURCE_VIEW_DESC& sd = v.srv_desc;
        sd = {};
        sd.Format = v.fmt;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (d.cube) {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            sd.TextureCube.MostDetailedMip = d.base_mip; sd.TextureCube.MipLevels = mips;
        } else if (rd.DepthOrArraySize > 1) {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Texture2DArray.MostDetailedMip = d.base_mip; sd.Texture2DArray.MipLevels = mips;
            sd.Texture2DArray.FirstArraySlice = d.base_layer; sd.Texture2DArray.ArraySize = layers;
        } else {
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Texture2D.MostDetailedMip = d.base_mip; sd.Texture2D.MipLevels = mips;
        }
        v.has_srv_desc = true;
        return { textures_.alloc(v) };
    }

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
    // Commit or release backing memory for a tile region of a reserved (sparse) texture.
    // The region is given in texels and snapped outward to whole tiles, since mapping is
    // only expressible per tile — a sub-tile request would otherwise silently under-commit.
    void update_texture_residency(TextureHandle h, const TextureRegion& region, bool resident) override {
        auto* t = textures_.get(h.id);
        if (!t || !t->res) return;
        if (!t->sparse) { d12_unsupported("sparse residency (texture was not created with TextureDesc::sparse)"); return; }

        UINT num_tiles = 0;
        D3D12_PACKED_MIP_INFO packed{};
        D3D12_TILE_SHAPE shape{};
        UINT sub_count = 1;
        D3D12_SUBRESOURCE_TILING tiling{};
        dev->GetResourceTiling(t->res, &num_tiles, &packed, &shape, &sub_count, 0, &tiling);
        if (num_tiles == 0 || shape.WidthInTexels == 0 || shape.HeightInTexels == 0) return;

        const UINT mip = (UINT)(region.mip < 0 ? 0 : region.mip);
        // Snap the texel rect outward to tile granularity.
        const UINT x0 = (UINT)region.x / shape.WidthInTexels;
        const UINT y0 = (UINT)region.y / shape.HeightInTexels;
        const UINT w  = region.width  ? (UINT)region.width  : shape.WidthInTexels;
        const UINT hh = region.height ? (UINT)region.height : shape.HeightInTexels;
        UINT x1 = ((UINT)region.x + w  + shape.WidthInTexels  - 1) / shape.WidthInTexels;
        UINT y1 = ((UINT)region.y + hh + shape.HeightInTexels - 1) / shape.HeightInTexels;
        if (x1 > tiling.WidthInTiles)  x1 = tiling.WidthInTiles;
        if (y1 > tiling.HeightInTiles) y1 = tiling.HeightInTiles;
        if (x1 <= x0 || y1 <= y0) return;
        const UINT tw = x1 - x0, th = y1 - y0;
        const UINT count = tw * th;

        // One heap per texture, grown to cover every tile the resource could need. Tiles are
        // a fixed 64KB, so the whole-resource heap is simple and avoids per-region bookkeeping.
        if (resident && !t->tile_heap) {
            D3D12_HEAP_DESC hd{};
            hd.SizeInBytes = UINT64(num_tiles) * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
            hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            hd.Alignment = 0;
            hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            dev->CreateHeap(&hd, IID_PPV_ARGS(&t->tile_heap));
            if (!t->tile_heap) { d12_unsupported("sparse residency (tile heap allocation failed)"); return; }
        }
        if (!resident && !t->tile_heap) return;   // nothing mapped to release

        D3D12_TILED_RESOURCE_COORDINATE start{ x0, y0, 0, mip };
        D3D12_TILE_REGION_SIZE size{};
        size.NumTiles = count; size.UseBox = TRUE;
        size.Width = tw; size.Height = (UINT16)th; size.Depth = 1;
        const D3D12_TILE_RANGE_FLAGS flags = resident ? D3D12_TILE_RANGE_FLAG_NONE
                                                      : D3D12_TILE_RANGE_FLAG_NULL;
        // Heap offsets are per-tile; map this region to a contiguous run. The run starts at
        // the resource's own tile index for the region so repeated commits stay stable.
        const UINT heap_start = tiling.StartTileIndexInOverallResource + y0 * tiling.WidthInTiles + x0;
        UINT range_count = count;
        queue->UpdateTileMappings(t->res, 1, &start, &size,
                                  resident ? t->tile_heap : nullptr,
                                  1, &flags, resident ? &heap_start : nullptr,
                                  resident ? &range_count : nullptr,
                                  D3D12_TILE_MAPPING_FLAG_NONE);
        flush();   // mappings are queue operations; make them visible before any use
        if (resident) t->tiles_committed += count;
    }
    // ---- ray tracing: acceleration structures -------------------------------
    // Translate an AccelStructDesc into DXR build inputs. Shared by create (which sizes the
    // result/scratch buffers from the prebuild info) and build (which records the build), so
    // the two cannot disagree about what is being built.
    bool accel_inputs(const AccelStructDesc& d, D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* out,
                      D3D12_RAYTRACING_GEOMETRY_DESC* geom) const {
        *out = {};
        out->DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        UINT f = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
        if (d.flags & ACCEL_ALLOW_UPDATE)      f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
        if (d.flags & ACCEL_ALLOW_COMPACTION)  f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
        if (d.flags & ACCEL_PREFER_FAST_TRACE) f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        if (d.flags & ACCEL_PREFER_FAST_BUILD) f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
        if (d.flags & ACCEL_LOW_MEMORY)        f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
        if (d.update)                          f |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        out->Flags = (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)f;

        if (d.type == AccelStructType::TopLevel) {
            out->Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            out->NumDescs = d.instance_count;
            auto* ib = const_cast<D12Device*>(this)->buffers_.get(d.instance_buffer.id);
            out->InstanceDescs = ib ? ib->res->GetGPUVirtualAddress() : 0;
            return true;
        }
        auto* vb = const_cast<D12Device*>(this)->buffers_.get(d.vertex_buffer.id);
        if (!vb) return false;
        *geom = {};
        geom->Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geom->Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geom->Triangles.VertexBuffer.StartAddress = vb->res->GetGPUVirtualAddress();
        geom->Triangles.VertexBuffer.StrideInBytes = d.vertex_stride ? d.vertex_stride : 12;
        geom->Triangles.VertexCount = d.vertex_count;
        geom->Triangles.VertexFormat = (d.vertex_format == VertexFormat::Float2) ? DXGI_FORMAT_R32G32_FLOAT
                                                                                 : DXGI_FORMAT_R32G32B32_FLOAT;
        if (auto* ixb = const_cast<D12Device*>(this)->buffers_.get(d.index_buffer.id)) {
            geom->Triangles.IndexBuffer = ixb->res->GetGPUVirtualAddress();
            geom->Triangles.IndexCount  = d.index_count;
            geom->Triangles.IndexFormat = (d.index_format == IndexFormat::UInt16) ? DXGI_FORMAT_R16_UINT
                                                                                  : DXGI_FORMAT_R32_UINT;
        }
        if (auto* tb = const_cast<D12Device*>(this)->buffers_.get(d.transform_buffer.id))
            geom->Triangles.Transform3x4 = tb->res->GetGPUVirtualAddress();
        out->Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        out->NumDescs = 1;
        out->pGeometryDescs = geom;
        return true;
    }

    AccelStructHandle create_acceleration_structure(const AccelStructDesc& d) override {
        ID3D12Device5* dev5 = nullptr;
        if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dev5))) || !dev5) {
            d12_unsupported("acceleration structures (device has no DXR support)"); return { -1 };
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        D3D12_RAYTRACING_GEOMETRY_DESC geom{};
        if (!accel_inputs(d, &in, &geom)) { dev5->Release(); d12_unsupported("acceleration structures (invalid geometry)"); return { -1 }; }
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO pi{};
        dev5->GetRaytracingAccelerationStructurePrebuildInfo(&in, &pi);
        dev5->Release();
        if (pi.ResultDataMaxSizeInBytes == 0) { d12_unsupported("acceleration structures (prebuild returned 0)"); return { -1 }; }

        // The result lives in a UAV buffer in the dedicated ACCELERATION_STRUCTURE state;
        // scratch is a plain UAV buffer reused for every build of this structure.
        D12Accel a;
        a.desc = d;
        a.result_size = pi.ResultDataMaxSizeInBytes;
        a.scratch_size = pi.ScratchDataSizeInBytes > pi.UpdateScratchDataSizeInBytes
                       ? pi.ScratchDataSizeInBytes : pi.UpdateScratchDataSizeInBytes;
        auto make_uav_buffer = [&](UINT64 size, D3D12_RESOURCE_STATES state) {
            D3D12_RESOURCE_DESC bd{};
            bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = size; bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
            bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return commit(bd, D3D12_HEAP_TYPE_DEFAULT, state);
        };
        a.result  = make_uav_buffer(a.result_size,  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
        a.scratch = make_uav_buffer(a.scratch_size, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!a.result || !a.scratch) {
            if (a.result) a.result->Release(); if (a.scratch) a.scratch->Release();
            d12_unsupported("acceleration structures (buffer allocation failed)"); return { -1 };
        }
        return { accels_.alloc(a) };
    }
    void destroy_acceleration_structure(AccelStructHandle h) override {
        if (auto* a = accels_.get(h.id)) { if (a->result) a->result->Release(); if (a->scratch) a->scratch->Release(); }
        accels_.release(h.id);
    }

    // ---- ray-tracing pipeline + shader binding table ------------------------
    // Encode one record's local arguments after its shader identifier. The encoding mirrors
    // how D3D12 reads local root arguments: constants inline (4-byte units), root
    // descriptors as an 8-byte GPU address, tables as an 8-byte GPU descriptor handle —
    // in the order the local root signature declares them.
    UINT encode_local_args(uint8_t* dst, const RayLocalArg* args, int count) {
        UINT off = 0;
        for (int i = 0; i < count; ++i) {
            const RayLocalArg& a = args[i];
            switch (a.kind) {
                case RayArgKind::Constants: {
                    const UINT n = (a.constants_size + 3u) & ~3u;   // 4-byte units
                    if (dst && a.constants) std::memcpy(dst + off, a.constants, a.constants_size);
                    off += n;
                    break;
                }
                case RayArgKind::BufferAddress: {
                    D3D12_GPU_VIRTUAL_ADDRESS va = 0;
                    if (auto* b = buffers_.get(a.buffer.id)) va = b->res->GetGPUVirtualAddress() + a.buffer_offset;
                    if (dst) std::memcpy(dst + off, &va, sizeof va);
                    off += sizeof va;
                    break;
                }
                case RayArgKind::DescriptorTable: {
                    // Would need the set's descriptors written into the shader-visible heap,
                    // and the heap is a per-frame ring owned by the commander — a handle
                    // baked in at pipeline-creation time would dangle once it wrapped.
                    // Reported rather than encoded as zero, which would fault at trace time.
                    if (dst) d12_unsupported("ray-tracing local arg (DescriptorTable; use Constants/BufferAddress)");
                    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
                    if (dst) std::memcpy(dst + off, &gpu.ptr, sizeof gpu.ptr);
                    off += sizeof gpu.ptr;
                    break;
                }
            }
        }
        return off;
    }

    RayTracingPipelineHandle create_ray_tracing_pipeline(const RayTracingPipelineDesc& d) override {
        ID3D12Device5* dev5 = nullptr;
        if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&dev5))) || !dev5) {
            d12_unsupported("ray-tracing pipeline (device has no DXR support)"); return { -1 };
        }
        auto* lib = shaders_.get(d.library.id);
        if (!lib || !d.ray_gen.entry_point) { dev5->Release(); d12_unsupported("ray-tracing pipeline (no library / ray-gen)"); return { -1 }; }

        // Export names must outlive the state-object build, and D3D12 wants wide strings.
        std::vector<std::wstring> names;
        auto wide = [&](const char* s) -> const wchar_t* {
            if (!s) return nullptr;
            names.emplace_back(s, s + std::strlen(s));
            return names.back().c_str();
        };
        names.reserve(64);   // no reallocation: earlier pointers must stay valid

        std::vector<D3D12_EXPORT_DESC> exports;
        auto add_export = [&](const char* s) { if (s) exports.push_back({ wide(s), nullptr, D3D12_EXPORT_FLAG_NONE }); };
        add_export(d.ray_gen.entry_point);
        for (int i = 0; i < d.miss_count; ++i) add_export(d.miss[i].entry_point);
        for (int i = 0; i < d.hit_group_count; ++i) {
            add_export(d.hit_groups[i].closest_hit);
            add_export(d.hit_groups[i].any_hit);
            add_export(d.hit_groups[i].intersection);
        }

        std::vector<D3D12_STATE_SUBOBJECT> subs;
        subs.reserve(16 + d.hit_group_count * 2);

        D3D12_DXIL_LIBRARY_DESC libd{};
        libd.DXILLibrary = { lib->bytecode.data(), lib->bytecode.size() };
        libd.NumExports = (UINT)exports.size(); libd.pExports = exports.data();
        subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libd });

        std::vector<D3D12_HIT_GROUP_DESC> groups(d.hit_group_count);
        for (int i = 0; i < d.hit_group_count; ++i) {
            const RayHitGroup& g = d.hit_groups[i];
            groups[i].HitGroupExport = wide(g.name);
            groups[i].Type = g.intersection ? D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                                            : D3D12_HIT_GROUP_TYPE_TRIANGLES;
            groups[i].ClosestHitShaderImport  = wide(g.closest_hit);
            groups[i].AnyHitShaderImport      = wide(g.any_hit);
            groups[i].IntersectionShaderImport = wide(g.intersection);
            subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &groups[i] });
        }

        D3D12_RAYTRACING_SHADER_CONFIG scfg{};
        scfg.MaxPayloadSizeInBytes = d.max_payload_size;
        scfg.MaxAttributeSizeInBytes = d.max_attribute_size ? d.max_attribute_size : 8;
        subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &scfg });

        D3D12_RAYTRACING_PIPELINE_CONFIG pcfg{};
        pcfg.MaxTraceRecursionDepth = d.max_recursion ? d.max_recursion : 1;
        subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pcfg });

        auto* gpl = plls_.get(d.layout.id);
        D3D12_GLOBAL_ROOT_SIGNATURE grs{};
        grs.pGlobalRootSignature = gpl && gpl->root ? gpl->root : default_root();
        subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &grs });

        // Local root signatures, each associated with the exports/groups that use them.
        // Storage must outlive the build, hence the vectors rather than locals in the loop.
        std::vector<D3D12_LOCAL_ROOT_SIGNATURE> locals;
        std::vector<D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION> assocs;
        std::vector<const wchar_t*> assoc_names;
        locals.reserve(1 + d.miss_count + d.hit_group_count);
        assocs.reserve(1 + d.miss_count + d.hit_group_count);
        assoc_names.reserve(1 + d.miss_count + d.hit_group_count);
        // Recorded as (index into subs of the local RS, name) so associations can be pushed
        // only after every subobject address is final.
        std::vector<std::pair<size_t, const wchar_t*>> pending;
        std::vector<ID3D12RootSignature*> rp_locals;   // local root signatures owned by this pipeline
        // The caller's PipelineLayout was serialized as a GLOBAL root signature; a shader
        // record needs the same layout serialized with the LOCAL flag, so it is rebuilt here
        // from the stored description. The rebuilt signatures are owned by this pipeline.
        auto add_local = [&](PipelineLayoutHandle h, const char* export_name) {
            auto* pl = plls_.get(h.id);
            if (!pl || !export_name) return;
            ID3D12RootSignature* lr = build_root_signature(pl->desc, /*local=*/true, nullptr);
            if (!lr) { d12_unsupported("ray-tracing local root signature"); return; }
            rp_locals.push_back(lr);
            locals.push_back({ lr });
            subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE, &locals.back() });
            pending.emplace_back(subs.size() - 1, wide(export_name));
        };
        add_local(d.ray_gen.local_layout, d.ray_gen.entry_point);
        for (int i = 0; i < d.miss_count; ++i) add_local(d.miss[i].local_layout, d.miss[i].entry_point);
        for (int i = 0; i < d.hit_group_count; ++i) add_local(d.hit_groups[i].local_layout, d.hit_groups[i].name);
        for (auto& p : pending) {
            assoc_names.push_back(p.second);
            D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION a{};
            a.pSubobjectToAssociate = &subs[p.first];
            a.NumExports = 1; a.pExports = &assoc_names.back();
            assocs.push_back(a);
        }
        for (auto& a : assocs) subs.push_back({ D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &a });

        D3D12_STATE_OBJECT_DESC so{};
        so.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        so.NumSubobjects = (UINT)subs.size(); so.pSubobjects = subs.data();

        D12RayPipeline rp;
        rp.global_root = grs.pGlobalRootSignature;
        rp.local_roots = rp_locals;
        if (gpl) rp.set_params = gpl->set_params;
        const HRESULT hr = dev5->CreateStateObject(&so, IID_PPV_ARGS(&rp.state));
        dev5->Release();
        if (!rp.state) {
            static char msg[80];
            std::snprintf(msg, sizeof msg, "ray-tracing pipeline (CreateStateObject hr=0x%08lX)", (unsigned long)hr);
            d12_unsupported(msg);
            return { -1 };
        }

        ID3D12StateObjectProperties* props = nullptr;
        if (FAILED(rp.state->QueryInterface(IID_PPV_ARGS(&props))) || !props) {
            rp.state->Release(); d12_unsupported("ray-tracing pipeline (no state-object properties)"); return { -1 };
        }

        // Table layout: each record is the shader identifier plus its local arguments,
        // padded up to the record alignment; each REGION additionally starts on the larger
        // table alignment. Strides must be uniform per region, so size to the largest record.
        const UINT id_size = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        auto record_size = [&](const RayLocalArg* args, int n) {
            const UINT sz = id_size + encode_local_args(nullptr, args, n);
            return (sz + (D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1)) & ~(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1);
        };
        const UINT raygen_rec = record_size(d.ray_gen.args, d.ray_gen.arg_count);
        UINT miss_rec = id_size, hit_rec = id_size;
        for (int i = 0; i < d.miss_count; ++i)      miss_rec = (std::max)(miss_rec, record_size(d.miss[i].args, d.miss[i].arg_count));
        for (int i = 0; i < d.hit_group_count; ++i) hit_rec  = (std::max)(hit_rec,  record_size(d.hit_groups[i].args, d.hit_groups[i].arg_count));
        miss_rec = (std::max)(miss_rec, (UINT)D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        hit_rec  = (std::max)(hit_rec,  (UINT)D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

        const UINT64 align = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        auto round_up = [&](UINT64 v) { return (v + align - 1) & ~(align - 1); };
        const UINT64 raygen_off = 0;
        const UINT64 miss_off   = round_up(raygen_off + raygen_rec);
        const UINT64 hit_off    = round_up(miss_off + UINT64(miss_rec) * (d.miss_count ? d.miss_count : 0));
        const UINT64 total      = round_up(hit_off + UINT64(hit_rec) * (d.hit_group_count ? d.hit_group_count : 0));

        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total ? total : align; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rp.sbt = commit(bd, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!rp.sbt) { props->Release(); rp.state->Release(); d12_unsupported("ray-tracing pipeline (shader table allocation)"); return { -1 }; }

        uint8_t* map = nullptr;
        D3D12_RANGE none{ 0, 0 };
        rp.sbt->Map(0, &none, (void**)&map);
        std::memset(map, 0, size_t(bd.Width));
        auto write_record = [&](uint8_t* dst, const wchar_t* export_name, const RayLocalArg* args, int n) {
            void* id = props->GetShaderIdentifier(export_name);
            if (!id) return false;
            std::memcpy(dst, id, id_size);
            encode_local_args(dst + id_size, args, n);
            return true;
        };
        bool ok = write_record(map + raygen_off, wide(d.ray_gen.entry_point), d.ray_gen.args, d.ray_gen.arg_count);
        for (int i = 0; i < d.miss_count; ++i)
            ok &= write_record(map + miss_off + UINT64(miss_rec) * i, wide(d.miss[i].entry_point), d.miss[i].args, d.miss[i].arg_count);
        for (int i = 0; i < d.hit_group_count; ++i)
            ok &= write_record(map + hit_off + UINT64(hit_rec) * i, wide(d.hit_groups[i].name), d.hit_groups[i].args, d.hit_groups[i].arg_count);
        rp.sbt->Unmap(0, nullptr);
        props->Release();
        if (!ok) { rp.sbt->Release(); rp.state->Release(); d12_unsupported("ray-tracing pipeline (unknown export in shader table)"); return { -1 }; }

        const D3D12_GPU_VIRTUAL_ADDRESS base = rp.sbt->GetGPUVirtualAddress();
        rp.raygen_addr = base + raygen_off; rp.raygen_size = raygen_rec;
        rp.miss_addr = base + miss_off; rp.miss_stride = miss_rec; rp.miss_size = UINT64(miss_rec) * d.miss_count;
        rp.hit_addr  = base + hit_off;  rp.hit_stride  = hit_rec;  rp.hit_size  = UINT64(hit_rec) * d.hit_group_count;
        return { rt_pipes_.alloc(rp) };
    }
    void destroy_ray_tracing_pipeline(RayTracingPipelineHandle h) override {
        if (auto* p = rt_pipes_.get(h.id)) {
            for (auto* r : p->local_roots) if (r) r->Release();
            if (p->sbt) p->sbt->Release(); if (p->state) p->state->Release();
        }
        rt_pipes_.release(h.id);
    }
    D12RayPipeline* rt_pipeline(int id) { return rt_pipes_.get(id); }
    uint64_t acceleration_structure_address(AccelStructHandle h) override {
        auto* a = accels_.get(h.id);
        return (a && a->result) ? a->result->GetGPUVirtualAddress() : 0;
    }

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
    D12Accel* accel(int id) { return accels_.get(id); }

    Pool<D12Buffer> buffers_; Pool<D12Texture> textures_; Pool<D12Sampler> samplers_; Pool<D12Shader> shaders_;
    Pool<D12Pipeline> pipelines_; Pool<D12RenderTarget> rts_; Pool<D12Fence> fences_; Pool<int> sems_;
    Pool<D12Fence> timelines_; Pool<D12Query> queries_; Pool<D12DescSetLayout> dsls_; Pool<D12PipelineLayout> plls_;
    Pool<D12DescriptorSet> dsets_; Pool<int> pcaches_; Pool<D12Accel> accels_; Pool<D12RayPipeline> rt_pipes_;
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
    ~D12Commander() override { if (cl6_) cl6_->Release(); if (cl_) cl_->Release(); if (alloc_) alloc_->Release(); if (frame_fence_) frame_fence_->Release(); if (frame_event_) CloseHandle(frame_event_); }
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
    // `depth_stencil` (optional) is attached alongside — a swapchain buffer is colour-only, so
    // depth testing and stencil clipping against the backbuffer need one supplied here.
    void set_render_target_backbuffer(RenderTargetHandle depth_stencil) override {
        dev_->ensure_backbuffers();
        if (dev_->bb_count_ == 0) { has_rtv_ = false; return; }
        bb_idx_ = dev_->current_bb_index();
        dev_->transition_bb(cl_, bb_idx_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cur_rtv_ = dev_->bb_rtv_[bb_idx_]; has_rtv_ = true; has_dsv_ = false; bound_bb_ = true;
        if (depth_stencil.valid()) if (auto* rt = dev_->rt(depth_stencil.id)) if (auto* t = dev_->texture(rt->depth_tex)) {
            dev_->transition(cl_, t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            cur_dsv_ = rt->dsv; has_dsv_ = true;
        }
        cl_->OMSetRenderTargets(1, &cur_rtv_, FALSE, has_dsv_ ? &cur_dsv_ : nullptr);
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
    void set_pipeline(PipelineHandle h) override { auto* p = dev_->pipeline(h.id); if (!p) return; cur_ = p; rt_ = nullptr; cl_->SetPipelineState(p->pso); if (p->root) { if (p->compute) cl_->SetComputeRootSignature(p->root); else cl_->SetGraphicsRootSignature(p->root); } if (!p->compute) cl_->IASetPrimitiveTopology(p->topo); }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; UINT stride = (cur_ && slot < VertexLayout::MAX_BUFFER_SLOTS) ? cur_->vl.strides[slot] : 0; D3D12_VERTEX_BUFFER_VIEW v{ b->res->GetGPUVirtualAddress() + offset, (UINT)(b->size - offset), stride }; cl_->IASetVertexBuffers(slot, 1, &v); }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; D3D12_INDEX_BUFFER_VIEW v{ b->res->GetGPUVirtualAddress() + offset, (UINT)(b->size - offset), fmt == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT }; cl_->IASetIndexBuffer(&v); }
    void bind_texture(uint32_t, TextureHandle) override {}        // via descriptor tables
    void bind_sampler(uint32_t, SamplerHandle) override {}
    // Root arguments live in separate graphics and compute slots, so each of these has to
    // target the one matching the bound pipeline — always calling the graphics setter left
    // a compute pipeline's roots unbound.
    bool compute_bound() const { return cur_ && cur_->compute; }
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override {
        auto* b = dev_->buffer(h.id); if (!b) return;
        const D3D12_GPU_VIRTUAL_ADDRESS va = b->res->GetGPUVirtualAddress() + offset;
        if (compute_bound()) cl_->SetComputeRootConstantBufferView(slot, va);
        else                 cl_->SetGraphicsRootConstantBufferView(slot, va);
    }
    void push_constants(uint32_t, const void* data, uint32_t size) override {
        if (compute_bound()) cl_->SetComputeRoot32BitConstants(0, size / 4, data, 0);
        else                 cl_->SetGraphicsRoot32BitConstants(0, size / 4, data, 0);
    }
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override {
        auto* b = dev_->buffer(h.id); if (!b) return;
        const D3D12_GPU_VIRTUAL_ADDRESS va = b->res->GetGPUVirtualAddress() + offset;
        if (compute_bound()) cl_->SetComputeRootUnorderedAccessView(slot, va);
        else                 cl_->SetGraphicsRootUnorderedAccessView(slot, va);
    }
    // Slot bind for a storage (UAV) texture. Unlike a storage BUFFER, this cannot be a root
    // descriptor — a texture needs a real descriptor in the shader-visible heap — so one is
    // allocated from the per-frame heap and bound as a single-entry descriptor table.
    // build_auto_root() gives texture-UAV registers a table parameter to match (kind 2).
    void bind_storage_texture(uint32_t slot, TextureHandle h, int mip, StorageAccess access) override {
        (void)access;   // D3D12 has no read-only UAV distinction; the shader's declaration decides
        auto* t = dev_->texture(h.id); if (!t) return;
        dev_->transition(cl_, t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        dev_->alloc_gpu(1, cpu, gpu);
        if (!dev_->write_texture_uav(t, mip, cpu)) return;
        if (cur_ && cur_->compute) cl_->SetComputeRootDescriptorTable(slot, gpu);
        else                       cl_->SetGraphicsRootDescriptorTable(slot, gpu);
    }
    void bind_descriptor_set(uint32_t set_index, DescriptorSetHandle h, const uint32_t*, int) override {
        auto* s = dev_->descriptor_set(h.id); if (!s) return;
        // A ray-tracing pipeline has no D12Pipeline behind it, so its tables come from the
        // bound RT pipeline instead. Ray tracing reads the COMPUTE root slots.
        const std::vector<SetParams>* sp_list = nullptr; bool compute = false;
        if (rt_)       { sp_list = &rt_->set_params;  compute = true; }
        else if (cur_) { sp_list = &cur_->set_params; compute = cur_->compute; }
        if (!sp_list || set_index >= sp_list->size()) return;
        const SetParams sp = (*sp_list)[set_index];
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
                } else if (w.type == BindingType::AccelerationStructure) {
                    // An acceleration-structure SRV is addressed by GPU location, not by a
                    // resource pointer — the only SRV type created with a null resource.
                    D3D12_SHADER_RESOURCE_VIEW_DESC av{};
                    av.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
                    av.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    av.RaytracingAccelerationStructure.Location = dev_->acceleration_structure_address(w.accel);
                    dev_->dev->CreateShaderResourceView(nullptr, &av, dst);
                } else if (w.type == BindingType::StorageTexture) {
                    // A UAV, not an SRV: the layout reserved a UAV range for this binding, so
                    // writing an SRV descriptor here would mismatch what the shader reads.
                    if (auto* t = dev_->texture(w.texture.id)) {
                        dev_->transition(cl_, t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        dev_->write_texture_uav(t, w.texture_mip, dst);
                    }
                } else { // SampledTexture / CombinedImageSampler -> SRV (sampled in the shader stages)
                    if (auto* t = dev_->texture(w.texture.id)) {
                        dev_->transition(cl_, t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                        // A typeless resource has no implied view format, so the default SRV
                        // has to name the typed one; a null desc would be rejected there.
                        D3D12_SHADER_RESOURCE_VIEW_DESC def{};
                        const D3D12_SHADER_RESOURCE_VIEW_DESC* sdp = t->has_srv_desc ? &t->srv_desc : nullptr;
                        if (!sdp && t->typeless) {
                            const D3D12_RESOURCE_DESC rd = t->res->GetDesc();
                            def.Format = t->fmt;
                            def.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                            const bool ms = rd.SampleDesc.Count > 1;   // multisampled: the *MS dimensions
                            if (ms && rd.DepthOrArraySize > 1) {
                                def.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
                                def.Texture2DMSArray.ArraySize = rd.DepthOrArraySize;
                            } else if (ms) {
                                def.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
                            } else if (rd.DepthOrArraySize > 1) {
                                def.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                                def.Texture2DArray.MipLevels = rd.MipLevels; def.Texture2DArray.ArraySize = rd.DepthOrArraySize;
                            } else {
                                def.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                def.Texture2D.MipLevels = rd.MipLevels;
                            }
                            sdp = &def;
                        }
                        dev_->dev->CreateShaderResourceView(t->res, sdp, dst);
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
    // Indirect (GPU-driven) draws. D3D12 routes them all through ExecuteIndirect with a
    // command signature describing the argument layout (cached on the device per stride).
    // The args buffer must be readable as an indirect argument, so it is transitioned into
    // INDIRECT_ARGUMENT state first — the state tracker leaves it wherever the last write
    // put it (typically UNORDERED_ACCESS after a compute pass that generated the args).
    void execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE type, BufferHandle args, uint32_t offset,
                          uint32_t count, uint32_t stride, BufferHandle count_buf, uint32_t count_off) {
        if (count == 0) return;
        auto* b = dev_->buffer(args.id); if (!b) return;
        ID3D12CommandSignature* sig = dev_->command_signature(type, stride);
        if (!sig) { d12_unsupported("indirect draw (command signature creation failed)"); return; }
        dev_->transition(cl_, b, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        ID3D12Resource* cres = nullptr;
        if (count_buf.valid()) {
            if (auto* c = dev_->buffer(count_buf.id)) {
                dev_->transition(cl_, c, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                cres = c->res;
            }
        }
        cl_->ExecuteIndirect(sig, count, b->res, offset, cres, count_off);
    }
    void draw_indirect(BufferHandle args, uint32_t offset, uint32_t count, uint32_t stride) override {
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, args, offset, count, stride, {}, 0);
    }
    void draw_indexed_indirect(BufferHandle args, uint32_t offset, uint32_t count, uint32_t stride) override {
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, args, offset, count, stride, {}, 0);
    }
    void dispatch_indirect(BufferHandle args, uint32_t offset) override {
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH, args, offset, 1, 0, {}, 0);
    }
    // DispatchMesh lives on ID3D12GraphicsCommandList6; the interface is QI'd once and kept
    // for the commander's lifetime rather than per draw.
    ID3D12GraphicsCommandList6* mesh_list() {
        if (!cl6_checked_) { cl6_checked_ = true; if (FAILED(cl_->QueryInterface(IID_PPV_ARGS(&cl6_)))) cl6_ = nullptr; }
        return cl6_;
    }
    void draw_mesh_tasks(uint32_t x, uint32_t y, uint32_t z) override {
        ID3D12GraphicsCommandList6* cl6 = mesh_list();
        if (!cl6) { d12_unsupported("draw_mesh_tasks (no ID3D12GraphicsCommandList6)"); return; }
        cl6->DispatchMesh(x, y ? y : 1, z ? z : 1);
    }
    void draw_mesh_tasks_indirect(BufferHandle args, uint32_t offset, uint32_t count, uint32_t stride) override {
        if (!mesh_list()) { d12_unsupported("draw_mesh_tasks_indirect (no ID3D12GraphicsCommandList6)"); return; }
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH, args, offset, count, stride, {}, 0);
    }
    void memory_barrier(uint32_t) override { D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; cl_->ResourceBarrier(1, &b); }
    void copy_buffer(BufferHandle dst, uint32_t doff, BufferHandle src, uint32_t soff, uint32_t size) override { auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (s && d) cl_->CopyBufferRegion(d->res, doff, s->res, soff, size); }
    void copy_texture(TextureHandle dst, const TextureRegion&, TextureHandle src, const TextureRegion&) override { auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (s && d) cl_->CopyResource(d->res, s->res); }
    void blit_render_target(RenderTargetHandle dh, RenderTargetHandle sh,
                            int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1, bool linear) override {
        auto* d = dev_->rt(dh.id); auto* s = dev_->rt(sh.id); if (!d || !s) return;
        auto* dt = dev_->texture(d->color_tex); auto* st = dev_->texture(s->color_tex); if (!dt || !st) return;
        // 1:1, same-extent, same-origin blit == a resource copy.
        if (sx0 == dx0 && sy0 == dy0 && (sx1 - sx0) == (dx1 - dx0) && (sy1 - sy0) == (dy1 - dy0)) {
            dev_->transition(cl_, st, D3D12_RESOURCE_STATE_COPY_SOURCE);
            dev_->transition(cl_, dt, D3D12_RESOURCE_STATE_COPY_DEST);
            cl_->CopyResource(dt->res, st->res);
            return;
        }
        // Otherwise stretch through a fullscreen-triangle pass: the destination rect becomes
        // the viewport and the source rect the sampled UV range, so scale and flip both fall
        // out of the interpolation.
        ID3D12PipelineState* pso = dev_->blit_pso(dt->fmt);
        if (!pso) { d12_unsupported("blit_render_target scaling"); return; }
        const D3D12_RESOURCE_DESC srd = st->res->GetDesc();
        dev_->transition(cl_, st, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dev_->transition(cl_, dt, D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu;
        dev_->alloc_gpu(1, cpu, gpu);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = st->fmt;   // the resource may be TYPELESS; the view must name a type
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = srd.MipLevels;
        dev_->dev->CreateShaderResourceView(st->res, &sd, cpu);

        struct { float u0, v0, u1, v1; uint32_t lin; } cb{
            float(sx0) / float(srd.Width), float(sy0) / float(srd.Height),
            float(sx1) / float(srd.Width), float(sy1) / float(srd.Height), linear ? 1u : 0u };
        cl_->OMSetRenderTargets(1, &d->rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp{ float(dx0 < dx1 ? dx0 : dx1), float(dy0 < dy1 ? dy0 : dy1),
                           float(dx1 > dx0 ? dx1 - dx0 : dx0 - dx1),
                           float(dy1 > dy0 ? dy1 - dy0 : dy0 - dy1), 0.0f, 1.0f };
        cl_->RSSetViewports(1, &vp);
        // Scissor is dynamic state with no default: without it the draw is clipped away.
        D3D12_RECT sc{ (LONG)vp.TopLeftX, (LONG)vp.TopLeftY,
                       (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
        cl_->RSSetScissorRects(1, &sc);
        cl_->SetGraphicsRootSignature(dev_->blit_root_);
        cl_->SetPipelineState(pso);
        cl_->SetGraphicsRoot32BitConstants(0, 5, &cb, 0);
        cl_->SetGraphicsRootDescriptorTable(1, gpu);
        cl_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cl_->DrawInstanced(3, 1, 0, 0);
        // This pass replaced the pipeline/root state wholesale; the next set_pipeline
        // reapplies it in full, so drop the cached pointer rather than restoring it.
        cur_ = nullptr;
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
    // ExecuteIndirect takes the count buffer natively: `max_draws` caps it, and the GPU
    // reads the actual draw count from the buffer.
    void draw_indirect_count(BufferHandle args, uint32_t args_off, BufferHandle count_buf, uint32_t count_off,
                             uint32_t max_draws, uint32_t stride) override {
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, args, args_off, max_draws, stride, count_buf, count_off);
    }
    void draw_indexed_indirect_count(BufferHandle args, uint32_t args_off, BufferHandle count_buf, uint32_t count_off,
                                     uint32_t max_draws, uint32_t stride) override {
        execute_indirect(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, args, args_off, max_draws, stride, count_buf, count_off);
    }
    void build_acceleration_structure(AccelStructHandle h, const AccelStructDesc& d) override {
        auto* a = dev_->accel(h.id);
        if (!a || !a->result || !a->scratch) return;
        ID3D12GraphicsCommandList4* cl4 = nullptr;
        if (FAILED(cl_->QueryInterface(IID_PPV_ARGS(&cl4))) || !cl4) {
            d12_unsupported("build_acceleration_structure (command list has no DXR support)"); return;
        }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
        D3D12_RAYTRACING_GEOMETRY_DESC geom{};
        if (!dev_->accel_inputs(d, &in, &geom)) { cl4->Release(); return; }
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bd{};
        bd.Inputs = in;
        bd.DestAccelerationStructureData    = a->result->GetGPUVirtualAddress();
        bd.ScratchAccelerationStructureData = a->scratch->GetGPUVirtualAddress();
        // A refit reads and writes the same structure in place.
        if (d.update) bd.SourceAccelerationStructureData = a->result->GetGPUVirtualAddress();
        cl4->BuildRaytracingAccelerationStructure(&bd, 0, nullptr);
        // Anything reading the structure (a trace, or a TLAS build over this BLAS) must see
        // the build's writes; a UAV barrier is the documented synchronisation for that.
        D3D12_RESOURCE_BARRIER uav{}; uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource = a->result;
        cl_->ResourceBarrier(1, &uav);
        cl4->Release();
    }
    // Binding an RT pipeline also binds its global root signature — root arguments live in
    // a separate space from graphics/compute, so anything bound before this is not visible
    // to the trace.
    void set_ray_tracing_pipeline(RayTracingPipelineHandle h) override {
        auto* p = dev_->rt_pipeline(h.id);
        if (!p || !p->state) { d12_unsupported("set_ray_tracing_pipeline (invalid pipeline)"); return; }
        ID3D12GraphicsCommandList4* cl4 = nullptr;
        if (FAILED(cl_->QueryInterface(IID_PPV_ARGS(&cl4))) || !cl4) {
            d12_unsupported("set_ray_tracing_pipeline (command list has no DXR support)"); return;
        }
        if (p->global_root) cl_->SetComputeRootSignature(p->global_root);   // RT uses the compute root slots
        cl4->SetPipelineState1(p->state);
        cl4->Release();
        rt_ = p;
        cur_ = nullptr;   // the graphics/compute PSO binding is gone; make the next set_pipeline reapply
    }
    void trace_rays(uint32_t width, uint32_t height, uint32_t depth) override {
        if (!rt_) { d12_unsupported("trace_rays (no ray-tracing pipeline bound)"); return; }
        ID3D12GraphicsCommandList4* cl4 = nullptr;
        if (FAILED(cl_->QueryInterface(IID_PPV_ARGS(&cl4))) || !cl4) {
            d12_unsupported("trace_rays (command list has no DXR support)"); return;
        }
        D3D12_DISPATCH_RAYS_DESC dr{};
        dr.RayGenerationShaderRecord = { rt_->raygen_addr, rt_->raygen_size };
        dr.MissShaderTable   = { rt_->miss_addr, rt_->miss_size, rt_->miss_stride };
        dr.HitGroupTable     = { rt_->hit_addr,  rt_->hit_size,  rt_->hit_stride };
        dr.Width = width; dr.Height = height ? height : 1; dr.Depth = depth ? depth : 1;
        cl4->DispatchRays(&dr);
        cl4->Release();
    }

private:
    D12Device* dev_ = nullptr; ID3D12CommandAllocator* alloc_ = nullptr; ID3D12GraphicsCommandList* cl_ = nullptr;
    D12Pipeline* cur_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cur_rtv_{}, cur_dsv_{}; bool has_rtv_ = false, has_dsv_ = false;
    ID3D12Fence* frame_fence_ = nullptr; UINT64 frame_value_ = 0; HANDLE frame_event_ = nullptr;  // per-frame GPU sync
    bool bound_bb_ = false; UINT bb_idx_ = 0;   // current backbuffer bound this frame
    ID3D12GraphicsCommandList6* cl6_ = nullptr; bool cl6_checked_ = false;   // DispatchMesh (QI once)
    D12RayPipeline* rt_ = nullptr;   // bound ray-tracing pipeline (its shader tables feed trace_rays)
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
