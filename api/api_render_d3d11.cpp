// api_render_d3d11.cpp — Direct3D 11 implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Windows only.
//
// Built on UGW's GraphicsD3D11, which exposes the device/context/swapchain via
// Graphics::native_device() (ID3D11Device*), native_context() (ID3D11DeviceContext*)
// and native_swapchain() (IDXGISwapChain*). Wired into the dispatcher in
// api_render.cpp.
//
// NOTE: written against the D3D11 API but COMPILED/VERIFIED ONLY ON WINDOWS — this
// project's CI on Linux cannot build it. Core paths (resources, state, clears,
// draws, copy, map/readback, event-fence) are implemented; descriptor sets are
// emulated as per-stage bind calls (D3D11 has no set objects); timeline semaphores
// and queries are emulated/best-effort; mesh shaders / ray tracing / sparse log
// "unsupported" (parity with the GL backend); D3D11 has no PSO cache (no-op).

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

#if defined(WINDOW_SUPPORT_D3D11) && defined(_WIN32)

#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace window {
namespace {

void d11_unsupported(const char* what) {
    static bool seen[64]; static const char* names[64]; static int n = 0;
    for (int i = 0; i < n; ++i) if (names[i] == what) return;
    if (n < 64) { names[n++] = what; }
    std::fprintf(stderr, "[UGW/D3D11] %s not supported (no-op)\n", what);
}

template <class T> struct Pool {
    std::vector<T> items; std::vector<int> free_list;
    int alloc(T v) { if (!free_list.empty()) { int i = free_list.back(); free_list.pop_back(); items[i] = std::move(v); return i; } items.push_back(std::move(v)); return int(items.size()) - 1; }
    T* get(int id) { return (id >= 0 && id < int(items.size())) ? &items[id] : nullptr; }
    void release(int id) { if (id >= 0 && id < int(items.size())) free_list.push_back(id); }
};

DXGI_FORMAT tex_format(TextureFormat f) {
    switch (f) {
        case TextureFormat::R8_UNORM:          return DXGI_FORMAT_R8_UNORM;
        case TextureFormat::RG8_UNORM:         return DXGI_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8_UNORM:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB:  return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case TextureFormat::BGRA8_UNORM:       return DXGI_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R16_FLOAT:         return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::RGBA16_FLOAT:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::R32_FLOAT:         return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RGBA32_FLOAT:      return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::D32_FLOAT:         return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:                               return DXGI_FORMAT_R8G8B8A8_UNORM;
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
UINT bind_flags(BufferType t) {
    switch (t) {
        case BufferType::Vertex:  return D3D11_BIND_VERTEX_BUFFER;
        case BufferType::Index:   return D3D11_BIND_INDEX_BUFFER;
        case BufferType::Uniform: return D3D11_BIND_CONSTANT_BUFFER;
        case BufferType::Storage: return D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        default:                  return D3D11_BIND_VERTEX_BUFFER;
    }
}

// State mapping (honour the requested PipelineDesc, not a hardcode).
D3D11_BLEND d11_blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:           return D3D11_BLEND_ZERO;
        case BlendFactor::One:            return D3D11_BLEND_ONE;
        case BlendFactor::SrcColor:       return D3D11_BLEND_SRC_COLOR;
        case BlendFactor::InvSrcColor:    return D3D11_BLEND_INV_SRC_COLOR;
        case BlendFactor::SrcAlpha:       return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:    return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstColor:       return D3D11_BLEND_DEST_COLOR;
        case BlendFactor::InvDstColor:    return D3D11_BLEND_INV_DEST_COLOR;
        case BlendFactor::DstAlpha:       return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstAlpha:    return D3D11_BLEND_INV_DEST_ALPHA;
        case BlendFactor::SrcAlphaSat:    return D3D11_BLEND_SRC_ALPHA_SAT;
        case BlendFactor::BlendFactor:    return D3D11_BLEND_BLEND_FACTOR;
        case BlendFactor::InvBlendFactor: return D3D11_BLEND_INV_BLEND_FACTOR;
    }
    return D3D11_BLEND_ONE;
}
// D3D11 forbids colour-channel factors on the alpha blend; fold them to the alpha form.
D3D11_BLEND d11_blend_factor_alpha(BlendFactor f) {
    switch (f) {
        case BlendFactor::SrcColor:    return D3D11_BLEND_SRC_ALPHA;
        case BlendFactor::InvSrcColor: return D3D11_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DstColor:    return D3D11_BLEND_DEST_ALPHA;
        case BlendFactor::InvDstColor: return D3D11_BLEND_INV_DEST_ALPHA;
        default:                       return d11_blend_factor(f);
    }
}
D3D11_BLEND_OP d11_blend_op(BlendOp o) {
    switch (o) {
        case BlendOp::Add:         return D3D11_BLEND_OP_ADD;
        case BlendOp::Subtract:    return D3D11_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min:         return D3D11_BLEND_OP_MIN;
        case BlendOp::Max:         return D3D11_BLEND_OP_MAX;
    }
    return D3D11_BLEND_OP_ADD;
}
D3D11_COMPARISON_FUNC d11_compare(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never:        return D3D11_COMPARISON_NEVER;
        case CompareFunc::Less:         return D3D11_COMPARISON_LESS;
        case CompareFunc::Equal:        return D3D11_COMPARISON_EQUAL;
        case CompareFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
        case CompareFunc::Greater:      return D3D11_COMPARISON_GREATER;
        case CompareFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
        case CompareFunc::Always:       return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_LESS;
}

struct D11Buffer  { ID3D11Buffer* buf = nullptr; UINT size = 0; UINT byte_width = 0; BufferType type = BufferType::Vertex; ID3D11UnorderedAccessView* uav = nullptr; ID3D11Buffer* map_staging = nullptr; };
struct D11Texture { ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr; ID3D11RenderTargetView* rtv = nullptr;
                    ID3D11DepthStencilView* dsv = nullptr; ID3D11UnorderedAccessView* uav = nullptr; DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN; int w = 0, h = 0; };
struct D11Sampler { ID3D11SamplerState* s = nullptr; };
struct D11Shader  { ID3D11DeviceChild* shader = nullptr; ShaderStage stage = ShaderStage::Vertex; std::vector<uint8_t> bytecode; };
struct D11Pipeline{ ID3D11VertexShader* vs = nullptr; ID3D11PixelShader* ps = nullptr; ID3D11ComputeShader* cs = nullptr;
                    ID3D11GeometryShader* gs = nullptr; ID3D11InputLayout* layout = nullptr; ID3D11RasterizerState* raster = nullptr;
                    ID3D11BlendState* blend = nullptr; ID3D11DepthStencilState* depth = nullptr;
                    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; VertexLayout vl; };
struct D11RenderTarget { int color_tex = -1; int depth_tex = -1; };
struct D11Fence   { ID3D11Query* q = nullptr; };
struct D11Query   { ID3D11Query* q = nullptr; QueryType type = QueryType::Timestamp; };
struct D11DescSetLayout { DescriptorSetLayoutDesc desc; };
struct D11PipelineLayout{ PipelineLayoutDesc desc; };
struct D11DescriptorSet { std::vector<DescriptorWrite> writes; };
struct D11Timeline { uint64_t value = 0; };

class D11Device : public GraphicDevice {
public:
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    explicit D11Device(Graphics* g) { dev = (ID3D11Device*)g->native_device(); ctx = (ID3D11DeviceContext*)g->native_context(); }
    ~D11Device() override {}

    Backend get_backend() const override { return Backend::D3D11; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (!out) return;
        out->max_texture_size = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        out->max_color_attachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        out->max_viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        out->max_uniform_buffer_range = D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16;
        out->max_bound_descriptor_sets = 4;
        out->min_uniform_buffer_offset_alignment = 256;   // CB offset granularity (D3D11.1 VSSetConstantBuffers1)
        out->max_push_constant_size = 256;
        out->compute_shaders = true; out->instancing = true; out->indirect_draw = true; out->timestamp_query = true;
        // Highest MSAA sample count the default colour format supports.
        for (UINT n = 8; n >= 2; n >>= 1) { UINT q = 0; if (SUCCEEDED(dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, n, &q)) && q > 0) { out->max_samples = int(n); break; } }
    }

    // ---- buffers ------------------------------------------------------------
    BufferHandle create_buffer(const BufferDesc& d) override {
        D11Buffer b; b.size = d.size ? d.size : 16; b.type = d.type;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = (b.size + 15) & ~15u;   // CBs need 16-byte multiples
        bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = bind_flags(d.type);
        // Storage buffers are raw (ByteAddressBuffer) so one compute shader serves both
        // D3D11 and D3D12 (raw UAV); a raw view needs a 4-byte-multiple width.
        if (d.type == BufferType::Storage) { bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS; bd.ByteWidth = (b.size + 3) & ~3u; }
        b.byte_width = bd.ByteWidth;
        D3D11_SUBRESOURCE_DATA srd = {}; srd.pSysMem = d.initial_data;
        dev->CreateBuffer(&bd, d.initial_data ? &srd : nullptr, &b.buf);
        if (d.type == BufferType::Storage && b.buf) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {}; ud.Format = DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = bd.ByteWidth / 4; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            dev->CreateUnorderedAccessView(b.buf, &ud, &b.uav);
        }
        return { buffers_.alloc(b) };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !b->buf) return;
        D3D11_BOX box{ offset, 0, 0, offset + size, 1, 1 };
        ctx->UpdateSubresource(b->buf, 0, b->type == BufferType::Uniform ? nullptr : &box, data, 0, 0);
    }
    void destroy_buffer(BufferHandle h) override { auto* b = buffers_.get(h.id); if (b) { if (b->map_staging) b->map_staging->Release(); if (b->uav) b->uav->Release(); if (b->buf) b->buf->Release(); } buffers_.release(h.id); }

    // ---- textures -----------------------------------------------------------
    TextureHandle create_texture(const TextureDesc& d) override {
        D11Texture t; t.fmt = tex_format(d.format); t.w = d.width; t.h = d.height;
        const bool depth = (d.usage & TEXTURE_USAGE_DEPTH_STENCIL) != 0;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = d.width; td.Height = d.height; td.MipLevels = d.mip_levels ? d.mip_levels : 1;
        td.ArraySize = d.array_layers > 1 ? d.array_layers : 1; td.Format = t.fmt;
        td.SampleDesc.Count = d.samples > 1 ? d.samples : 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = 0;
        if (d.usage & TEXTURE_USAGE_SAMPLED)       td.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (d.usage & TEXTURE_USAGE_RENDER_TARGET) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        if (d.usage & TEXTURE_USAGE_DEPTH_STENCIL) td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        if (d.usage & TEXTURE_USAGE_STORAGE)       td.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        if (d.cube) { td.ArraySize = 6; td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE; }
        D3D11_SUBRESOURCE_DATA srd = {}; srd.pSysMem = d.initial_data; srd.SysMemPitch = d.width * 4;
        dev->CreateTexture2D(&td, d.initial_data ? &srd : nullptr, &t.tex);
        if (td.BindFlags & D3D11_BIND_SHADER_RESOURCE) dev->CreateShaderResourceView(t.tex, nullptr, &t.srv);
        if (td.BindFlags & D3D11_BIND_RENDER_TARGET)   dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv);
        if (td.BindFlags & D3D11_BIND_DEPTH_STENCIL)   dev->CreateDepthStencilView(t.tex, nullptr, &t.dsv);
        if (td.BindFlags & D3D11_BIND_UNORDERED_ACCESS) dev->CreateUnorderedAccessView(t.tex, nullptr, &t.uav);
        return { textures_.alloc(t) };
    }
    void update_texture(TextureHandle h, const TextureRegion& r, const void* data) override {
        auto* t = textures_.get(h.id); if (!t || !t->tex) return;
        D3D11_BOX box{ (UINT)r.x, (UINT)r.y, 0, UINT(r.x + r.width), UINT(r.y + r.height), 1 };
        ctx->UpdateSubresource(t->tex, r.mip, &box, data, r.width * 4, 0);
    }
    void generate_mipmaps(TextureHandle h) override { auto* t = textures_.get(h.id); if (t && t->srv) ctx->GenerateMips(t->srv); }
    void destroy_texture(TextureHandle h) override {
        auto* t = textures_.get(h.id); if (!t) return;
        if (t->srv) t->srv->Release(); if (t->rtv) t->rtv->Release(); if (t->dsv) t->dsv->Release(); if (t->uav) t->uav->Release(); if (t->tex) t->tex->Release();
        textures_.release(h.id);
    }

    SamplerHandle create_sampler(const SamplerState& s) override {
        D3D11_SAMPLER_DESC sd = {}; sd.Filter = s.min_filter == FilterMode::Point ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.MaxLOD = D3D11_FLOAT32_MAX;
        D11Sampler smp; dev->CreateSamplerState(&sd, &smp.s); return { samplers_.alloc(smp) };
    }
    void destroy_sampler(SamplerHandle h) override { auto* s = samplers_.get(h.id); if (s && s->s) s->s->Release(); samplers_.release(h.id); }

    // ---- shaders (DXBC) -----------------------------------------------------
    ShaderHandle create_shader(const ShaderDesc& d) override {
        if (d.language != ShaderLanguage::DXBC) { d11_unsupported("non-DXBC shader (compile HLSL to DXBC)"); return { -1 }; }
        D11Shader sh; sh.stage = d.stage; sh.bytecode.assign((const uint8_t*)d.code, (const uint8_t*)d.code + d.code_size);
        const void* bc = sh.bytecode.data(); SIZE_T n = sh.bytecode.size();
        switch (d.stage) {
            case ShaderStage::Vertex:   { ID3D11VertexShader* s = nullptr; dev->CreateVertexShader(bc, n, nullptr, &s); sh.shader = s; break; }
            case ShaderStage::Fragment: { ID3D11PixelShader* s = nullptr; dev->CreatePixelShader(bc, n, nullptr, &s); sh.shader = s; break; }
            case ShaderStage::Geometry: { ID3D11GeometryShader* s = nullptr; dev->CreateGeometryShader(bc, n, nullptr, &s); sh.shader = s; break; }
            case ShaderStage::Compute:  { ID3D11ComputeShader* s = nullptr; dev->CreateComputeShader(bc, n, nullptr, &s); sh.shader = s; break; }
            default: d11_unsupported("shader stage"); return { -1 };
        }
        return { shaders_.alloc(sh) };
    }
    void destroy_shader(ShaderHandle h) override { auto* s = shaders_.get(h.id); if (s && s->shader) s->shader->Release(); shaders_.release(h.id); }

    // ---- pipelines (state bundle; D3D11 has no PSO) -------------------------
    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        D11Pipeline p; p.vl = d.vertex_layout;
        auto* vs = shaders_.get(d.vertex_shader.id); auto* ps = shaders_.get(d.fragment_shader.id);
        auto* csh = shaders_.get(d.compute_shader.id);
        if (csh) { p.cs = (ID3D11ComputeShader*)csh->shader; p.cs->AddRef(); return { pipelines_.alloc(p) }; }
        if (vs) { p.vs = (ID3D11VertexShader*)vs->shader; p.vs->AddRef(); }
        if (ps) { p.ps = (ID3D11PixelShader*)ps->shader; p.ps->AddRef(); }
        if (auto* gs = shaders_.get(d.geometry_shader.id)) { p.gs = (ID3D11GeometryShader*)gs->shader; p.gs->AddRef(); }
        // Input layout from the vertex layout (needs the VS bytecode).
        if (vs && d.vertex_layout.attribute_count) {
            std::vector<D3D11_INPUT_ELEMENT_DESC> ie(d.vertex_layout.attribute_count);
            for (int i = 0; i < d.vertex_layout.attribute_count; ++i) {
                const auto& a = d.vertex_layout.attributes[i];
                ie[i] = { "TEXCOORD", a.location, vertex_format(a.format), a.buffer_slot, a.offset,
                          d.vertex_layout.input_rates[a.buffer_slot] == VertexInputRate::PerInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, 0 };
            }
            dev->CreateInputLayout(ie.data(), (UINT)ie.size(), vs->bytecode.data(), vs->bytecode.size(), &p.layout);
        }
        D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = d.rasterizer.fill_mode == FillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        rd.CullMode = d.rasterizer.cull_mode == CullMode::None ? D3D11_CULL_NONE : (d.rasterizer.cull_mode == CullMode::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
        rd.FrontCounterClockwise = d.rasterizer.front_face == FrontFace::CounterClockwise; rd.ScissorEnable = d.rasterizer.scissor_enable; rd.DepthClipEnable = TRUE;
        rd.MultisampleEnable = d.rasterizer.multisample_enable || d.samples > 1;
        dev->CreateRasterizerState(&rd, &p.raster);
        // Honour the requested BlendState (factors / ops / write mask), not a hardcode.
        D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = d.blend.write_mask & 0xF;
        bd.RenderTarget[0].BlendEnable = d.blend.enabled;
        bd.RenderTarget[0].SrcBlend  = d11_blend_factor(d.blend.src_color);  bd.RenderTarget[0].DestBlend  = d11_blend_factor(d.blend.dst_color);  bd.RenderTarget[0].BlendOp      = d11_blend_op(d.blend.color_op);
        bd.RenderTarget[0].SrcBlendAlpha = d11_blend_factor_alpha(d.blend.src_alpha); bd.RenderTarget[0].DestBlendAlpha = d11_blend_factor_alpha(d.blend.dst_alpha); bd.RenderTarget[0].BlendOpAlpha = d11_blend_op(d.blend.alpha_op);
        dev->CreateBlendState(&bd, &p.blend);
        D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = d.depth_stencil.depth_enable; dd.DepthWriteMask = d.depth_stencil.depth_write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; dd.DepthFunc = d11_compare(d.depth_stencil.depth_func);
        dev->CreateDepthStencilState(&dd, &p.depth);
        return { pipelines_.alloc(p) };
    }
    void destroy_pipeline(PipelineHandle h) override {
        auto* p = pipelines_.get(h.id); if (!p) return;
        if (p->vs) p->vs->Release(); if (p->ps) p->ps->Release(); if (p->cs) p->cs->Release(); if (p->gs) p->gs->Release();
        if (p->layout) p->layout->Release(); if (p->raster) p->raster->Release(); if (p->blend) p->blend->Release(); if (p->depth) p->depth->Release();
        pipelines_.release(h.id);
    }

    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples; td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET;
        D11RenderTarget rt; rt.color_tex = create_texture(td).id; return { rts_.alloc(rt) };
    }
    RenderTargetHandle create_depth_target(const DepthStencilDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.usage = TEXTURE_USAGE_DEPTH_STENCIL;
        D11RenderTarget rt; rt.depth_tex = create_texture(td).id; return { rts_.alloc(rt) };
    }
    TextureHandle render_target_texture(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return { -1 }; return { rt->color_tex >= 0 ? rt->color_tex : rt->depth_tex }; }
    void destroy_render_target(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return; if (rt->color_tex >= 0) destroy_texture({ rt->color_tex }); if (rt->depth_tex >= 0) destroy_texture({ rt->depth_tex }); rts_.release(h.id); }

    TextureHandle create_texture_view(const TextureViewDesc& d) override { d11_unsupported("create_texture_view (use the source texture's SRV)"); return d.texture; }

    // ---- sync (event query as fence; timeline emulated) ---------------------
    FenceHandle create_fence(bool) override { D11Fence f; D3D11_QUERY_DESC qd{ D3D11_QUERY_EVENT, 0 }; dev->CreateQuery(&qd, &f.q); return { fences_.alloc(f) }; }
    void destroy_fence(FenceHandle h) override { auto* f = fences_.get(h.id); if (f && f->q) f->q->Release(); fences_.release(h.id); }
    bool wait_fence(FenceHandle h, uint64_t) override { auto* f = fences_.get(h.id); if (!f) return false; BOOL done = FALSE; while (ctx->GetData(f->q, &done, sizeof(done), 0) != S_OK) {} return true; }
    bool get_fence_status(FenceHandle h) override { auto* f = fences_.get(h.id); if (!f) return false; BOOL d = FALSE; return ctx->GetData(f->q, &d, sizeof(d), 0) == S_OK; }
    void reset_fence(FenceHandle) override {}
    SemaphoreHandle create_semaphore() override { return { sems_.alloc(0) }; }
    void destroy_semaphore(SemaphoreHandle h) override { sems_.release(h.id); }
    void wait_idle() override { ctx->Flush(); }
    TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial) override { return { timelines_.alloc(D11Timeline{ initial }) }; }
    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override { timelines_.release(h.id); }
    void signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v) override { if (auto* t = timelines_.get(h.id)) if (v > t->value) t->value = v; }
    bool wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v, uint64_t) override { auto* t = timelines_.get(h.id); return t && t->value >= v; }
    uint64_t get_timeline_value(TimelineSemaphoreHandle h) override { auto* t = timelines_.get(h.id); return t ? t->value : 0; }

    QueryHandle create_query(QueryType type) override {
        D11Query q; q.type = type; D3D11_QUERY_DESC qd{ type == QueryType::Occlusion ? D3D11_QUERY_OCCLUSION : D3D11_QUERY_TIMESTAMP, 0 };
        dev->CreateQuery(&qd, &q.q); return { queries_.alloc(q) };
    }
    void destroy_query(QueryHandle h) override { auto* q = queries_.get(h.id); if (q && q->q) q->q->Release(); queries_.release(h.id); }
    bool get_query_result(QueryHandle h, uint64_t* out, bool wait) override {
        auto* q = queries_.get(h.id); if (!q) return false; UINT64 v = 0;
        HRESULT r; do { r = ctx->GetData(q->q, &v, sizeof(v), 0); } while (wait && r == S_FALSE);
        if (r != S_OK) return false; if (out) *out = v; return true;
    }

    void* map_buffer(BufferHandle h, uint32_t offset, uint32_t) override {
        // D3D11 DEFAULT buffers aren't CPU-mappable; back the map with a STAGING copy and
        // write the result back on unmap (CopyResource needs matching byte widths).
        auto* b = buffers_.get(h.id); if (!b || !b->buf) return nullptr;
        if (b->map_staging) { b->map_staging->Release(); b->map_staging = nullptr; }
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = b->byte_width; bd.Usage = D3D11_USAGE_STAGING; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &b->map_staging))) return nullptr;
        ctx->CopyResource(b->map_staging, b->buf);
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(ctx->Map(b->map_staging, 0, D3D11_MAP_READ_WRITE, 0, &ms))) { b->map_staging->Release(); b->map_staging = nullptr; return nullptr; }
        return (uint8_t*)ms.pData + offset;
    }
    void unmap_buffer(BufferHandle h) override {
        auto* b = buffers_.get(h.id); if (!b || !b->map_staging) return;
        ctx->Unmap(b->map_staging, 0); ctx->CopyResource(b->buf, b->map_staging);
        b->map_staging->Release(); b->map_staging = nullptr;
    }
    void read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !dst) return;
        D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = b->size; bd.Usage = D3D11_USAGE_STAGING; bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Buffer* stg = nullptr; dev->CreateBuffer(&bd, nullptr, &stg);
        ctx->CopyResource(stg, b->buf);
        D3D11_MAPPED_SUBRESOURCE m{}; if (ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m) == S_OK) { std::memcpy(dst, (const uint8_t*)m.pData + offset, size); ctx->Unmap(stg, 0); }
        stg->Release();
    }
    void read_texture(TextureHandle h, const TextureRegion& r, void* dst) override {
        auto* t = textures_.get(h.id); if (!t || !dst) return;
        D3D11_TEXTURE2D_DESC td = {}; td.Width = t->w; td.Height = t->h; td.MipLevels = 1; td.ArraySize = 1; td.Format = t->fmt;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* stg = nullptr; dev->CreateTexture2D(&td, nullptr, &stg);
        ctx->CopyResource(stg, t->tex);
        D3D11_MAPPED_SUBRESOURCE m{}; if (ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m) == S_OK) {
            const uint8_t* src = (const uint8_t*)m.pData + r.y * m.RowPitch + r.x * 4;
            for (int row = 0; row < r.height; ++row) std::memcpy((uint8_t*)dst + row * r.width * 4, src + row * m.RowPitch, r.width * 4);
            ctx->Unmap(stg, 0);
        }
        stg->Release();
    }

    // ---- binding model (emulated) -------------------------------------------
    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override { return { dsls_.alloc(D11DescSetLayout{ d }) }; }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { dsls_.release(h.id); }
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& d) override { return { plls_.alloc(D11PipelineLayout{ d }) }; }
    void destroy_pipeline_layout(PipelineLayoutHandle h) override { plls_.release(h.id); }
    DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& d) override { D11DescriptorSet s; s.writes.assign(d.writes, d.writes + d.write_count); return { dsets_.alloc(std::move(s)) }; }
    void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& d) override { if (auto* s = dsets_.get(h.id)) s->writes.assign(d.writes, d.writes + d.write_count); }
    void destroy_descriptor_set(DescriptorSetHandle h) override { dsets_.release(h.id); }
    void set_debug_name(ObjectType, uint32_t, const char*) override {}   // SetPrivateData(WKPDID_D3DDebugObjectName) — omitted

    // D3D11 has no pipeline cache; D3D compiles HLSL→DXBC offline.
    PipelineCacheHandle create_pipeline_cache(const void*, size_t) override { return { pcaches_.alloc(0) }; }
    size_t get_pipeline_cache_data(PipelineCacheHandle, void*, size_t) override { return 0; }
    void destroy_pipeline_cache(PipelineCacheHandle h) override { pcaches_.release(h.id); }

    void update_texture_residency(TextureHandle, const TextureRegion&, bool) override { d11_unsupported("sparse residency (tiled resources)"); }
    AccelStructHandle create_acceleration_structure(const AccelStructDesc&) override { d11_unsupported("acceleration structures (no D3D11 ray tracing)"); return { accels_.alloc(0) }; }
    void destroy_acceleration_structure(AccelStructHandle h) override { accels_.release(h.id); }

    // accessors for the commander
    D11Buffer* buffer(int id) { return buffers_.get(id); }
    D11Texture* texture(int id) { return textures_.get(id); }
    D11Sampler* sampler(int id) { return samplers_.get(id); }
    D11Pipeline* pipeline(int id) { return pipelines_.get(id); }
    D11RenderTarget* rt(int id) { return rts_.get(id); }
    D11Query* query(int id) { return queries_.get(id); }
    D11Fence* fence(int id) { return fences_.get(id); }
    D11DescriptorSet* descriptor_set(int id) { return dsets_.get(id); }

    Pool<D11Buffer> buffers_; Pool<D11Texture> textures_; Pool<D11Sampler> samplers_; Pool<D11Shader> shaders_;
    Pool<D11Pipeline> pipelines_; Pool<D11RenderTarget> rts_; Pool<D11Fence> fences_; Pool<int> sems_;
    Pool<D11Timeline> timelines_; Pool<D11Query> queries_; Pool<D11DescSetLayout> dsls_; Pool<D11PipelineLayout> plls_;
    Pool<D11DescriptorSet> dsets_; Pool<int> pcaches_; Pool<int> accels_;
};

class D11Commander : public GraphicCommander {
public:
    D11Commander(D11Device* d) : dev_(d), ctx_(d->ctx) {}
    D11Device* device() const { return dev_; }

    void begin() override {}
    void end() override {}
    void set_render_target_backbuffer() override { /* present path: bind swapchain RTV (TODO) */ }
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) override {
        ID3D11RenderTargetView* rtvs[8] = {}; int n = 0; color0_ = nullptr; depth0_ = nullptr;
        for (int i = 0; i < count && i < 8 && colors; ++i) if (auto* rt = dev_->rt(colors[i].id)) if (auto* t = dev_->texture(rt->color_tex)) { rtvs[n++] = t->rtv; if (!color0_) color0_ = t; }
        if (depth.valid()) if (auto* rt = dev_->rt(depth.id)) if (auto* t = dev_->texture(rt->depth_tex)) depth0_ = t;
        ctx_->OMSetRenderTargets(n, rtvs, depth0_ ? depth0_->dsv : nullptr);
    }
    void set_viewport(const Viewport& v) override { D3D11_VIEWPORT vp{ v.x, v.y, v.width, v.height, v.min_depth, v.max_depth }; ctx_->RSSetViewports(1, &vp); }
    void set_scissor(const ScissorRect& r) override { D3D11_RECT rc{ r.x, r.y, r.x + r.width, r.y + r.height }; ctx_->RSSetScissorRects(1, &rc); }
    void clear_color(const ClearColor& c) override { if (color0_ && color0_->rtv) { float col[4]{ c.r, c.g, c.b, c.a }; ctx_->ClearRenderTargetView(color0_->rtv, col); } }
    void clear_depth_stencil(const ClearDepthStencil& ds) override { if (depth0_ && depth0_->dsv) ctx_->ClearDepthStencilView(depth0_->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, ds.depth, ds.stencil); }
    void set_pipeline(PipelineHandle h) override {
        auto* p = dev_->pipeline(h.id); if (!p) return; cur_ = p;
        if (p->cs) { ctx_->CSSetShader(p->cs, nullptr, 0); return; }
        ctx_->VSSetShader(p->vs, nullptr, 0); ctx_->PSSetShader(p->ps, nullptr, 0); ctx_->GSSetShader(p->gs, nullptr, 0);
        ctx_->IASetInputLayout(p->layout); ctx_->IASetPrimitiveTopology(p->topo);
        ctx_->RSSetState(p->raster); float bf[4]{ 1,1,1,1 }; ctx_->OMSetBlendState(p->blend, bf, 0xFFFFFFFF); ctx_->OMSetDepthStencilState(p->depth, 0);
    }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; UINT stride = cur_ && slot < VertexLayout::MAX_BUFFER_SLOTS ? cur_->vl.strides[slot] : 0; UINT off = offset; ID3D11Buffer* vb = b->buf; ctx_->IASetVertexBuffers(slot, 1, &vb, &stride, &off); }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (b) ctx_->IASetIndexBuffer(b->buf, fmt == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, offset); }
    void bind_texture(uint32_t slot, TextureHandle h) override { auto* t = dev_->texture(h.id); if (t) { ctx_->PSSetShaderResources(slot, 1, &t->srv); ctx_->VSSetShaderResources(slot, 1, &t->srv); } }
    void bind_sampler(uint32_t slot, SamplerHandle h) override { auto* s = dev_->sampler(h.id); if (s) ctx_->PSSetSamplers(slot, 1, &s->s); }
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t, uint32_t) override { auto* b = dev_->buffer(h.id); if (b) { ctx_->VSSetConstantBuffers(slot, 1, &b->buf); ctx_->PSSetConstantBuffers(slot, 1, &b->buf); } }
    void push_constants(uint32_t, const void*, uint32_t) override { d11_unsupported("push_constants (use a constant buffer)"); }
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t, uint32_t) override { auto* b = dev_->buffer(h.id); if (b && b->uav) ctx_->CSSetUnorderedAccessViews(slot, 1, &b->uav, nullptr); }
    void bind_storage_texture(uint32_t slot, TextureHandle h, int, StorageAccess) override { auto* t = dev_->texture(h.id); if (t && t->uav) ctx_->CSSetUnorderedAccessViews(slot, 1, &t->uav, nullptr); }
    void bind_descriptor_set(uint32_t, DescriptorSetHandle h, const uint32_t*, int) override {
        auto* s = dev_->descriptor_set(h.id); if (!s) return;
        for (const auto& w : s->writes) {
            if (w.type == BindingType::UniformBuffer) bind_uniform_buffer(w.binding, w.buffer, 0, 0);
            else if (w.type == BindingType::SampledTexture || w.type == BindingType::CombinedImageSampler) { bind_texture(w.binding, w.texture); if (w.type == BindingType::CombinedImageSampler) bind_sampler(w.binding, w.sampler); }
            else if (w.type == BindingType::Sampler) bind_sampler(w.binding, w.sampler);
            else if (w.type == BindingType::StorageTexture) bind_storage_texture(w.binding, w.texture, w.texture_mip, w.storage_access);
        }
    }
    void draw(uint32_t vc, uint32_t first, uint32_t inst, uint32_t first_inst) override { if (inst > 1) ctx_->DrawInstanced(vc, inst, first, first_inst); else ctx_->Draw(vc, first); }
    void draw_indexed(uint32_t ic, uint32_t first, int32_t base_v, uint32_t inst, uint32_t first_inst) override { if (inst > 1) ctx_->DrawIndexedInstanced(ic, inst, first, base_v, first_inst); else ctx_->DrawIndexed(ic, first, base_v); }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { ctx_->Dispatch(x, y, z); }
    void draw_indirect(BufferHandle a, uint32_t off, uint32_t, uint32_t) override { auto* b = dev_->buffer(a.id); if (b) ctx_->DrawInstancedIndirect(b->buf, off); }
    void draw_indexed_indirect(BufferHandle a, uint32_t off, uint32_t, uint32_t) override { auto* b = dev_->buffer(a.id); if (b) ctx_->DrawIndexedInstancedIndirect(b->buf, off); }
    void dispatch_indirect(BufferHandle a, uint32_t off) override { auto* b = dev_->buffer(a.id); if (b) ctx_->DispatchIndirect(b->buf, off); }
    void draw_mesh_tasks(uint32_t, uint32_t, uint32_t) override { d11_unsupported("draw_mesh_tasks"); }
    void draw_mesh_tasks_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { d11_unsupported("draw_mesh_tasks_indirect"); }
    void memory_barrier(uint32_t) override {}   // implicit in D3D11
    void copy_buffer(BufferHandle dst, uint32_t doff, BufferHandle src, uint32_t soff, uint32_t size) override {
        auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (!s || !d) return;
        D3D11_BOX box{ soff, 0, 0, soff + size, 1, 1 }; ctx_->CopySubresourceRegion(d->buf, 0, doff, 0, 0, s->buf, 0, &box);
    }
    void copy_texture(TextureHandle dst, const TextureRegion&, TextureHandle src, const TextureRegion&) override { auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (s && d) ctx_->CopyResource(d->tex, s->tex); }
    void blit_render_target(RenderTargetHandle dh, RenderTargetHandle sh,
                            int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1, bool) override {
        auto* d = dev_->rt(dh.id); auto* s = dev_->rt(sh.id); if (!d || !s) return;
        auto* dt = dev_->texture(d->color_tex); auto* st = dev_->texture(s->color_tex); if (!dt || !st) return;
        // 1:1, same-extent, same-origin blit == a resource copy; scaled/flipped blits
        // would need a fullscreen-quad pass (not wired here).
        if (sx0 == dx0 && sy0 == dy0 && (sx1 - sx0) == (dx1 - dx0) && (sy1 - sy0) == (dy1 - dy0))
            ctx_->CopyResource(dt->tex, st->tex);
        else
            d11_unsupported("blit_render_target scaling (draw a fullscreen quad)");
    }
    void resolve_render_target(RenderTargetHandle dh, RenderTargetHandle sh) override { auto* d = dev_->rt(dh.id); auto* s = dev_->rt(sh.id); if (d && s) { auto* dt = dev_->texture(d->color_tex); auto* st = dev_->texture(s->color_tex); if (dt && st) ctx_->ResolveSubresource(dt->tex, 0, st->tex, 0, dt->fmt); } }
    void write_timestamp(QueryHandle h) override { if (auto* q = dev_->query(h.id)) ctx_->End(q->q); }
    void begin_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) ctx_->Begin(q->q); }
    void end_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) ctx_->End(q->q); }
    void push_debug_group(const char*) override {}   // ID3DUserDefinedAnnotation — omitted
    void pop_debug_group() override {}
    void insert_debug_marker(const char*) override {}
    void set_stencil_reference(uint32_t ref) override { if (cur_) ctx_->OMSetDepthStencilState(cur_->depth, ref); }
    void set_blend_constants(const float rgba[4]) override { if (cur_) ctx_->OMSetBlendState(cur_->blend, rgba, 0xFFFFFFFF); }
    void set_depth_bias(float, float, float) override { d11_unsupported("dynamic depth bias (set in the rasterizer state)"); }
    void set_line_width(float) override {}
    void set_viewports(const Viewport* v, int n) override { std::vector<D3D11_VIEWPORT> vs(n); for (int i=0;i<n;++i) vs[i]={v[i].x,v[i].y,v[i].width,v[i].height,v[i].min_depth,v[i].max_depth}; ctx_->RSSetViewports((UINT)n, vs.data()); }
    void set_scissors(const ScissorRect* r, int n) override { std::vector<D3D11_RECT> rs(n); for (int i=0;i<n;++i) rs[i]={r[i].x,r[i].y,r[i].x+r[i].width,r[i].y+r[i].height}; ctx_->RSSetScissorRects((UINT)n, rs.data()); }
    void draw_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { d11_unsupported("draw_indirect_count (D3D11 has no count buffer)"); }
    void draw_indexed_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { d11_unsupported("draw_indexed_indirect_count"); }
    void build_acceleration_structure(AccelStructHandle, const AccelStructDesc&) override { d11_unsupported("build_acceleration_structure"); }
    void trace_rays(uint32_t, uint32_t, uint32_t) override { d11_unsupported("trace_rays"); }

private:
    D11Device* dev_ = nullptr; ID3D11DeviceContext* ctx_ = nullptr;
    D11Pipeline* cur_ = nullptr; D11Texture* color0_ = nullptr; D11Texture* depth0_ = nullptr;
};

} // namespace

GraphicDevice* create_device_d3d11(Graphics* context, Result* out_result) {
    if (!context || context->get_backend() != Backend::D3D11 || !context->native_device()) { if (out_result) *out_result = Result::ErrorNotSupported; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new D11Device(context);
}
GraphicCommander* create_commander_d3d11(Graphics* context, GraphicDevice* device, QueueType, Result* out_result) {
    if (!context || !device) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new D11Commander(static_cast<D11Device*>(device));
}
void submit_commander_d3d11(Graphics*, GraphicCommander* commander, FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    if (!commander) return;
    auto* c = static_cast<D11Commander*>(commander); auto* d = c->device();
    d->ctx->Flush();
    if (fence.valid()) if (auto* f = d->fence(fence.id)) d->ctx->End(f->q);          // event query signals on GPU completion
    if (timeline.valid()) d->signal_timeline_semaphore(timeline, value);
}

} // namespace window

#else  // not Windows / D3D11 disabled — not-supported stubs

namespace window {
GraphicDevice*    create_device_d3d11(Graphics*, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
GraphicCommander* create_commander_d3d11(Graphics*, GraphicDevice*, QueueType, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
void              submit_commander_d3d11(Graphics*, GraphicCommander*, FenceHandle, TimelineSemaphoreHandle, uint64_t) {}
} // namespace window

#endif
