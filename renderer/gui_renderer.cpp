#include "gui_renderer.hpp"

#include "shader_compiler/shader_compiler.hpp"   // HLSL -> backend blob (cached) at runtime

#include <cmath>
#include <cstdint>
#include <cstring>

namespace window {
namespace gui {
namespace {

using gfx::ShaderCompiler;

const int FLOATS_PER_VERT = 9;  // pos2 + uvw3 + rgba4

// One HLSL language for every backend, compiled + disk-cached at runtime with NO per-backend
// flags. Resources share one set: projection UBO at register(b0) -> binding 0, the texture at
// register(t1) -> binding 1, the sampler at register(s2) -> binding 2 (distinct numbers so they
// don't collide in a Vulkan set; SPIRV-Cross folds texture+sampler into a combined sampler for
// OpenGL). Vertex inputs use TEXCOORD<n> so the RHI maps attribute n.
//
// Atlas source: vertex + the glyph/solid pixel shader (sampler2DArray as an alpha mask).
const char* kHLSL_ATLAS = R"(
cbuffer Proj : register(b0) { float4x4 uProjection; };

struct VSIn  { float2 pos : TEXCOORD0; float3 uvw : TEXCOORD1; float4 color : TEXCOORD2; };
struct VSOut { float4 pos : SV_Position; float3 uvw : TEXCOORD0; float4 color : TEXCOORD1; };

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos   = mul(uProjection, float4(i.pos, 0.0, 1.0));
    o.uvw   = i.uvw;
    o.color = i.color;
    return o;
}

Texture2DArray uAtlas : register(t1);
SamplerState   uSamp  : register(s2);
float4 ps_atlas(VSOut i) : SV_Target {
    if (i.uvw.z >= 0.0) {                                  // glyph: R8 atlas as an alpha mask
        float a = uAtlas.Sample(uSamp, i.uvw).r;
        return float4(i.color.rgb, i.color.a * a);
    }
    return i.color;                                        // solid colour
}
)";

// Image source: full RGBA sampler2D modulated by the vertex colour (tint).
const char* kHLSL_IMAGE = R"(
struct VSOut { float4 pos : SV_Position; float3 uvw : TEXCOORD0; float4 color : TEXCOORD1; };
Texture2D    uImage : register(t1);
SamplerState uSamp  : register(s2);
float4 ps_image(VSOut i) : SV_Target { return uImage.Sample(uSamp, i.uvw.xy) * i.color; }
)";

} // namespace

bool GpuGuiRenderer::init(GraphicDevice* device) {
    device_ = device;
    if (!device_) return false;
    backend_ = device_->get_backend();

    // One HLSL source compiled (and disk-cached) for the active backend, with no per-backend
    // flags. Works on every backend the RHI supports (the projection is a uniform buffer, not a
    // push constant, so there is no D3D11 restriction).
#ifdef WINDOW_SUPPORT_SHADER_COMPILER
    const size_t atlas_len = std::strlen(kHLSL_ATLAS);
    const size_t image_len = std::strlen(kHLSL_IMAGE);
    vs_       = ShaderCompiler::compile_and_create_cached(device_, kHLSL_ATLAS, atlas_len, ShaderStage::Vertex,   "vs_main");
    fs_       = ShaderCompiler::compile_and_create_cached(device_, kHLSL_ATLAS, atlas_len, ShaderStage::Fragment, "ps_atlas");
    fs_image_ = ShaderCompiler::compile_and_create_cached(device_, kHLSL_IMAGE, image_len, ShaderStage::Fragment, "ps_image");
    if (!vs_.valid() || !fs_.valid() || !fs_image_.valid()) return false;
#else
    return false;   // the GUI renderer's shaders now require the built-in shader compiler
#endif

    // Projection uniform buffer ring (a fresh slot per render() so multiple GUI passes a frame
    // don't clobber each other on deferred backends; separate buffers => full updates for D3D11).
    float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    for (uint32_t i = 0; i < kUboSlots; ++i) {
        BufferDesc ud; ud.size = 16 * sizeof(float); ud.type = BufferType::Uniform;
        ud.usage = ResourceUsage::Dynamic; ud.initial_data = identity;
        proj_ubo_[i] = device_->create_buffer(ud);
        if (!proj_ubo_[i].valid()) return false;
    }

    // 1x1 white atlas so solid-only draws (no glyph atlas supplied) still bind a complete set.
    uint8_t white = 255;
    TextureDesc dt; dt.width = 1; dt.height = 1; dt.array_layers = 1; dt.array_texture = true;
    dt.format = TextureFormat::R8_UNORM; dt.usage = TEXTURE_USAGE_SAMPLED; dt.initial_data = &white;
    dummy_atlas_ = device_->create_texture(dt);

    // One descriptor set per draw: UBO (binding 0, vertex) + texture (1) + sampler (2, fragment).
    // The RHI emulates this on GL/D3D11 as slot binds, so it works on every backend uniformly.
    SamplerState ss; sampler_ = device_->create_sampler(ss);
    DescriptorSetLayoutDesc dl; dl.binding_count = 3;
    dl.bindings[0] = { 0, BindingType::UniformBuffer,  1, STAGE_VERTEX };
    dl.bindings[1] = { 1, BindingType::SampledTexture, 1, STAGE_FRAGMENT };
    dl.bindings[2] = { 2, BindingType::Sampler,        1, STAGE_FRAGMENT };
    set_layout_ = device_->create_descriptor_set_layout(dl);
    PipelineLayoutDesc pll; pll.set_layout_count = 1; pll.set_layouts[0] = set_layout_;
    pipe_layout_ = device_->create_pipeline_layout(pll);

    // Both pipelines share everything but the fragment shader (atlas vs image).
    PipelineDesc pd;
    pd.vertex_shader   = vs_;
    pd.layout          = pipe_layout_;     // invalid on GL → reflected/auto bindings
    pd.topology        = PrimitiveTopology::TriangleList;
    pd.blend           = BlendState::alpha_blend();
    pd.depth_stencil   = DepthStencilState::disabled();
    pd.rasterizer      = RasterizerState::no_cull();
    pd.rasterizer.scissor_enable = true;   // we always scissor (full-screen rect = no clip)
    VertexLayout& l = pd.vertex_layout;
    l.attributes[0] = { 0, VertexFormat::Float2, 0,  0 };   // pos
    l.attributes[1] = { 1, VertexFormat::Float3, 8,  0 };   // uvw
    l.attributes[2] = { 2, VertexFormat::Float4, 20, 0 };   // rgba
    l.attribute_count = 3;
    l.strides[0]      = FLOATS_PER_VERT * sizeof(float);
    l.buffer_count    = 1;

    pd.fragment_shader = fs_;        pipeline_       = device_->create_pipeline(pd);
    pd.fragment_shader = fs_image_;  image_pipeline_ = device_->create_pipeline(pd);
    return pipeline_.valid() && image_pipeline_.valid();
}

// Per-draw descriptor set (cached by UBO slot + texture id): the projection UBO of `ubo_slot` at
// binding 0, the texture at 1, the sampler at 2 (atlas = sampler2DArray, image = sampler2D). Caching
// by (slot,texture) keeps each pair created once -- the slot ring cycles deterministically and the
// texture set is small, so the cache stays bounded with no per-frame churn.
DescriptorSetHandle GpuGuiRenderer::desc_set_for(TextureHandle tex, uint32_t ubo_slot) {
    const uint64_t key = (uint64_t(ubo_slot) << 32) | uint32_t(tex.id);
    auto it = desc_sets_.find(key);
    if (it != desc_sets_.end()) return it->second;
    DescriptorSetDesc d; d.layout = set_layout_; d.write_count = 3;
    d.writes[0].binding = 0; d.writes[0].type = BindingType::UniformBuffer;  d.writes[0].buffer = proj_ubo_[ubo_slot]; d.writes[0].buffer_size = 16 * sizeof(float);
    d.writes[1].binding = 1; d.writes[1].type = BindingType::SampledTexture; d.writes[1].texture = tex;
    d.writes[2].binding = 2; d.writes[2].type = BindingType::Sampler;        d.writes[2].sampler = sampler_;
    DescriptorSetHandle set = device_->create_descriptor_set(d);
    desc_sets_.emplace(key, set);
    return set;
}

void GpuGuiRenderer::shutdown() {
    if (!device_) return;
    if (vbo_.valid())            device_->destroy_buffer(vbo_);
    for (uint32_t i = 0; i < kUboSlots; ++i)
        if (proj_ubo_[i].valid()) device_->destroy_buffer(proj_ubo_[i]);
    if (dummy_atlas_.valid())    device_->destroy_texture(dummy_atlas_);
    if (pipeline_.valid())       device_->destroy_pipeline(pipeline_);
    if (image_pipeline_.valid()) device_->destroy_pipeline(image_pipeline_);
    if (vs_.valid())             device_->destroy_shader(vs_);
    if (fs_.valid())             device_->destroy_shader(fs_);
    if (fs_image_.valid())       device_->destroy_shader(fs_image_);
    for (auto& kv : desc_sets_) device_->destroy_descriptor_set(kv.second);
    desc_sets_.clear();
    if (set_layout_.valid())  device_->destroy_descriptor_set_layout(set_layout_);
    if (pipe_layout_.valid()) device_->destroy_pipeline_layout(pipe_layout_);
    if (sampler_.valid())     device_->destroy_sampler(sampler_);
    tex_cache_.clear();   // handles only; the provider owns the textures
    device_ = nullptr;
}

void GpuGuiRenderer::emit_quad(float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1,
                               float layer, const math::Vec4& c) {
    auto v = [&](float px, float py, float u, float vv) {
        verts_.push_back(px); verts_.push_back(py);
        verts_.push_back(u);  verts_.push_back(vv); verts_.push_back(layer);
        verts_.push_back(c.x); verts_.push_back(c.y); verts_.push_back(c.z); verts_.push_back(c.w);
    };
    v(x,     y,     u0, v0); v(x + w, y,     u1, v0); v(x + w, y + h, u1, v1);
    v(x,     y,     u0, v0); v(x + w, y + h, u1, v1); v(x,     y + h, u0, v1);
}

void GpuGuiRenderer::emit_circle(float cx, float cy, float radius, const math::Vec4& c) {
    const int N = 24;
    auto v = [&](float px, float py) {
        verts_.push_back(px); verts_.push_back(py);
        verts_.push_back(0);  verts_.push_back(0); verts_.push_back(-1.0f);
        verts_.push_back(c.x); verts_.push_back(c.y); verts_.push_back(c.z); verts_.push_back(c.w);
    };
    for (int i = 0; i < N; ++i) {
        const float a0 = (float(i)     / N) * 6.2831853f;
        const float a1 = (float(i + 1) / N) * 6.2831853f;
        v(cx, cy);
        v(cx + std::cos(a0) * radius, cy + std::sin(a0) * radius);
        v(cx + std::cos(a1) * radius, cy + std::sin(a1) * radius);
    }
}

// Filled rounded rectangle: 3 cross bands + 4 quarter-circle corner fans. Always
// emits kRoundRectVerts vertices (degenerate corners when radius≈0) so the draw
// bookkeeping stays a fixed count. Solid colour (layer = -1).
void GpuGuiRenderer::emit_round_rect(float x, float y, float w, float h, float radius, const math::Vec4& c) {
    const float r = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    // 3 bands covering the rect minus the 4 corner squares (18 verts).
    emit_quad(x,     y + r, w,         h - 2 * r, 0,0,0,0, -1.0f, c);   // middle (full width)
    emit_quad(x + r, y,     w - 2 * r, r,         0,0,0,0, -1.0f, c);   // top
    emit_quad(x + r, y + h - r, w - 2 * r, r,     0,0,0,0, -1.0f, c);   // bottom
    auto v = [&](float px, float py) {
        verts_.push_back(px); verts_.push_back(py);
        verts_.push_back(0);  verts_.push_back(0); verts_.push_back(-1.0f);
        verts_.push_back(c.x); verts_.push_back(c.y); verts_.push_back(c.z); verts_.push_back(c.w);
    };
    const float cx[4] = { x + r,     x + w - r, x + w - r, x + r     };  // TL, TR, BR, BL
    const float cy[4] = { y + r,     y + r,     y + h - r, y + h - r };
    const float a0[4] = { 3.14159265f, 4.71238898f, 0.0f,        1.57079633f };  // start angles
    for (int k = 0; k < 4; ++k) {
        for (int i = 0; i < kRoundRectCornerSegs; ++i) {
            const float t0 = a0[k] + (float(i)     / kRoundRectCornerSegs) * 1.57079633f;
            const float t1 = a0[k] + (float(i + 1) / kRoundRectCornerSegs) * 1.57079633f;
            v(cx[k], cy[k]);
            v(cx[k] + std::cos(t0) * r, cy[k] + std::sin(t0) * r);
            v(cx[k] + std::cos(t1) * r, cy[k] + std::sin(t1) * r);
        }
    }
}

// Thick line p0→p1 as a rotated quad (two triangles), solid colour (layer = -1).
void GpuGuiRenderer::emit_line(float x0, float y0, float x1, float y1, float width, const math::Vec4& c) {
    auto v = [&](float px, float py) {
        verts_.push_back(px); verts_.push_back(py);
        verts_.push_back(0);  verts_.push_back(0); verts_.push_back(-1.0f);
        verts_.push_back(c.x); verts_.push_back(c.y); verts_.push_back(c.z); verts_.push_back(c.w);
    };
    float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) { emit_quad(x0 - width * 0.5f, y0 - width * 0.5f, width, width, 0,0,0,0, -1.0f, c); return; }
    const float hw = width * 0.5f;
    const float nx = -dy / len * hw, ny = dx / len * hw;   // half-width normal
    const float ax = x0 + nx, ay = y0 + ny, bx = x0 - nx, by = y0 - ny;
    const float cxp = x1 + nx, cyp = y1 + ny, dxp = x1 - nx, dyp = y1 - ny;
    v(ax, ay); v(bx, by); v(cxp, cyp);
    v(cxp, cyp); v(bx, by); v(dxp, dyp);
}

TextureHandle GpuGuiRenderer::resolve_texture(const WidgetRenderInfo::TextureCmd& t) {
    if (!tex_provider_) return {};
    // Key by source, matching flatten()'s own dedup (File→path, Memory→pointer).
    std::string key;
    if (t.source_type == TextureSourceType::File && t.file_path)
        key.assign("f:").append(t.file_path);
    else
        key = "m:" + std::to_string(reinterpret_cast<uintptr_t>(t.memory_data));
    auto it = tex_cache_.find(key);
    if (it != tex_cache_.end()) return it->second;
    const TextureHandle h = tex_provider_->resolve(t);   // may be invalid; cached so we don't retry
    tex_cache_.emplace(std::move(key), h);
    return h;
}

void GpuGuiRenderer::render(GraphicCommander* cmd, WidgetRenderInfo& info,
                            TextureHandle atlas, const float proj[16],
                            int fb_w, int fb_h, float scale) {
    if (!device_ || !cmd) return;

    // Expand text + 9-slice → Color + Texture quads (idempotent).
    info.flatten(rasterizer_);

    verts_.clear();

    // Solid + glyph quads use the atlas pipeline (one bound sampler2DArray, so they
    // batch freely); each image is its own sampler2D texture. To keep correct depth
    // order, walk the draw order once into segments — a run of primitives sharing a
    // pipeline (and, for images, the same texture) under one scissor — and replay
    // them in order. A run of glyphs/solids stays a single draw; a new image texture
    // (or a clip change) starts a new segment.
    enum class Kind { Atlas, Image };
    struct Segment { Kind kind; int tex; uint32_t first, count; int sx, sy, sw, sh; };
    std::vector<Segment> segs;

    int sx = 0, sy = 0, sw = fb_w, sh = fb_h;   // running scissor (full = no clip)
    Segment cur{ Kind::Atlas, -1, 0, 0, sx, sy, sw, sh };
    bool have_cur = false, force_break = false;
    uint32_t vc = 0;
    auto flush = [&]() { if (have_cur && cur.count > 0) segs.push_back(cur); };

    using Pool = WidgetRenderInfo::DrawRef::Pool;
    for (const auto& ref : info.get_draw_order()) {
        if (ref.clip_changed) {
            sx = 0; sy = 0; sw = fb_w; sh = fb_h;
            const float bw = math::box_width(ref.clip), bh = math::box_height(ref.clip);
            if (bw > 0.0f && bh > 0.0f) {
                const float bx = math::x(math::box_min(ref.clip));
                const float by = math::y(math::box_min(ref.clip));
                sx = int(bx * scale); sw = int(bw * scale); sh = int(bh * scale);
                // GL scissor origin is bottom-left (Y flip); Vulkan/D3D/Metal are top-left.
                sy = flip_scissor_y() ? int(fb_h - (by + bh) * scale) : int(by * scale);
            }
            force_break = true;   // applies even if the next primitive(s) are skipped
        }

        // Classify the primitive: which pipeline, and (for images) which texture.
        Kind kind = Kind::Atlas; int tex = -1;
        if (ref.pool == Pool::Texture) {
            const auto& t = info.textures[ref.index];
            if (t.atlas_layer < 0) {                 // file/memory image
                const TextureHandle h = resolve_texture(t);
                if (!h.valid()) continue;            // no provider / unresolved → skip
                kind = Kind::Image; tex = h.id;
            }
        }

        if (!have_cur || force_break || kind != cur.kind || (kind == Kind::Image && tex != cur.tex)) {
            flush();
            cur = Segment{ kind, tex, vc, 0, sx, sy, sw, sh };
            have_cur = true; force_break = false;
        }

        if (ref.pool == Pool::Color) {
            const auto& c = info.colors[ref.index];
            const float px = math::x(math::box_min(c.dest)), py = math::y(math::box_min(c.dest));
            const float pw = math::box_width(c.dest),        ph = math::box_height(c.dest);
            if      (c.shape == DrawShape::Circle)    { emit_circle(px + pw * 0.5f, py + ph * 0.5f, pw * 0.5f, c.color); vc += 24 * 3; cur.count += 24 * 3; }
            else if (c.shape == DrawShape::Line)      { emit_line(px, py, c.line_x1, c.line_y1, c.line_w, c.color);     vc += 6;      cur.count += 6; }
            else if (c.shape == DrawShape::RoundRect) { emit_round_rect(px, py, pw, ph, c.corner_radius, c.color);      vc += kRoundRectVerts; cur.count += kRoundRectVerts; }
            else                                      { emit_quad(px, py, pw, ph, 0,0,0,0, -1.0f, c.color);             vc += 6;      cur.count += 6; }
        } else if (ref.pool == Pool::Texture) {
            const auto& t = info.textures[ref.index];
            const float px = math::x(math::box_min(t.dest)), py = math::y(math::box_min(t.dest));
            const float pw = math::box_width(t.dest),        ph = math::box_height(t.dest);
            const float u0 = math::x(math::box_min(t.uv)),   v0 = math::y(math::box_min(t.uv));
            const float u1 = math::x(math::box_max(t.uv)),   v1 = math::y(math::box_max(t.uv));
            // Glyph → uvw.z = atlas layer (>=0); image → layer unused by its shader.
            const float layer = (t.atlas_layer >= 0) ? float(t.atlas_layer) : 0.0f;
            emit_quad(px, py, pw, ph, u0, v0, u1, v1, layer, t.tint);
            vc += 6; cur.count += 6;
        }
        // Slice9 / Text refs do not survive flatten(); ignored if present.
    }
    flush();
    if (verts_.empty()) return;

    // VBO ring: write this pass at its own byte offset so an earlier pass this frame is not
    // overwritten on deferred backends (Vulkan/D3D12); wrap when full, grow if one pass exceeds
    // the capacity. Every segment binds the buffer at vbo_base and indexes with its first-vertex.
    const uint32_t bytes = uint32_t(verts_.size() * sizeof(float));
    const uint32_t need  = (bytes + 15u) & ~15u;
    if (!vbo_.valid() || need > vbo_capacity_) {
        if (vbo_.valid()) device_->destroy_buffer(vbo_);
        uint32_t cap = vbo_capacity_ ? vbo_capacity_ : 64u * 1024u;
        while (cap < need) cap *= 2;
        BufferDesc bd; bd.size = cap; bd.type = BufferType::Vertex; bd.usage = ResourceUsage::Dynamic;
        vbo_ = device_->create_buffer(bd); vbo_capacity_ = cap; vbo_off_ = 0;
    } else if (vbo_off_ + need > vbo_capacity_) {
        vbo_off_ = 0;   // wrap (prior frames' passes are done via per-frame present sync)
    }
    const uint32_t vbo_base = vbo_off_;
    device_->update_buffer(vbo_, verts_.data(), bytes, vbo_base);
    vbo_off_ += need;

    // Projection -> a fresh UBO ring slot; every draw's descriptor set points at this slot.
    const uint32_t slot = ubo_slot_;
    ubo_slot_ = (ubo_slot_ + 1) % kUboSlots;
    device_->update_buffer(proj_ubo_[slot], proj, 16 * sizeof(float), 0);

    for (const auto& s : segs) {
        cmd->set_pipeline(s.kind == Kind::Image ? image_pipeline_ : pipeline_);
        cmd->bind_vertex_buffer(0, vbo_, vbo_base);
        // Atlas-kind draws fall back to the 1x1 dummy atlas when none was supplied, so the set
        // (which carries the projection UBO) is always complete and bindable.
        TextureHandle tex = (s.kind == Kind::Image) ? TextureHandle{ s.tex }
                                                    : (atlas.valid() ? atlas : dummy_atlas_);
        cmd->bind_descriptor_set(0, desc_set_for(tex, slot));   // UBO slot + texture + sampler; same on every backend
        cmd->set_scissor(ScissorRect{ s.sx, s.sy, s.sw, s.sh });
        cmd->draw(s.count, s.first);
    }
}

} // namespace gui
} // namespace window
