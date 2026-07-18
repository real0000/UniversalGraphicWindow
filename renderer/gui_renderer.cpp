#include "gui_renderer.hpp"
#include "gui_text_rasterizer.hpp"
#include "vector_renderer.hpp"
#include "../gui/gui_context.hpp"

#include "shader_compiler/shader_compiler.hpp"   // HLSL -> backend blob (cached) at runtime

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

// Directory the GUI shaders (gui.hlsl / gui_image.hlsl) are read from at
// runtime. CMake sets this to the source tree's renderer/shaders; a deployed
// build must ship that folder alongside the binary.
#ifndef WINDOW_SHADER_DIR
#define WINDOW_SHADER_DIR "renderer/shaders"
#endif

namespace window {
namespace gui {
namespace {

using gfx::ShaderCompiler;

// pos2 + uvw3 + rgba4 + sdf4. The sdf attribute carries (half_w, half_h,
// corner_radius, border_width) for the procedural SDF shape path (see gui.hlsl).
const int FLOATS_PER_VERT = 13;

// Read a whole text file (shader source). Empty vector on failure.
std::string read_text_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize n = f.tellg();
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    f.seekg(0);
    f.read(&s[0], n);
    return s;
}

} // namespace

bool GpuGuiRenderer::init(GraphicDevice* device) {
    device_ = device;
    if (!device_) return false;
    backend_ = device_->get_backend();

    // One HLSL source compiled (and disk-cached) for the active backend, with no per-backend
    // flags. Works on every backend the RHI supports (the projection is a uniform buffer, not a
    // push constant, so there is no D3D11 restriction).
#ifdef WINDOW_SUPPORT_SHADER_COMPILER
    // Read the HLSL from renderer/shaders/ at runtime (source of truth; compiled
    // + disk-cached for the active backend). gui.hlsl holds vs_main + ps_atlas
    // (solid / SDF shape / glyph); gui_image.hlsl holds ps_image.
    const std::string dir     = WINDOW_SHADER_DIR;
    const std::string atlas   = read_text_file(dir + "/gui.hlsl");
    const std::string image   = read_text_file(dir + "/gui_image.hlsl");
    if (atlas.empty() || image.empty()) return false;   // shaders must ship with the binary
    vs_       = ShaderCompiler::compile_and_create_cached(device_, atlas.c_str(), atlas.size(), ShaderStage::Vertex,   "vs_main");
    fs_       = ShaderCompiler::compile_and_create_cached(device_, atlas.c_str(), atlas.size(), ShaderStage::Fragment, "ps_atlas");
    fs_image_ = ShaderCompiler::compile_and_create_cached(device_, image.c_str(), image.size(), ShaderStage::Fragment, "ps_image");
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
    // 1x1 transparent RGBA so the colour-emoji binding (3) is always complete, even when
    // a frame draws no colour glyphs.
    uint8_t clear4[4] = { 0, 0, 0, 0 };
    TextureDesc dtc; dtc.width = 1; dtc.height = 1; dtc.array_layers = 1; dtc.array_texture = true;
    dtc.format = TextureFormat::RGBA8_UNORM; dtc.usage = TEXTURE_USAGE_SAMPLED; dtc.initial_data = clear4;
    dummy_color_atlas_ = device_->create_texture(dtc);
    cur_color_atlas_ = dummy_color_atlas_;

    // One descriptor set per draw: UBO (binding 0, vertex) + glyph atlas (1) + sampler (2) +
    // colour-emoji atlas (3, fragment). The RHI emulates this on GL/D3D11 as slot binds.
    SamplerState ss; sampler_ = device_->create_sampler(ss);
    DescriptorSetLayoutDesc dl; dl.binding_count = 4;
    dl.bindings[0] = { 0, BindingType::UniformBuffer,  1, STAGE_VERTEX };
    dl.bindings[1] = { 1, BindingType::SampledTexture, 1, STAGE_FRAGMENT };
    dl.bindings[2] = { 2, BindingType::Sampler,        1, STAGE_FRAGMENT };
    dl.bindings[3] = { 3, BindingType::SampledTexture, 1, STAGE_FRAGMENT };
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
    l.attributes[1] = { 1, VertexFormat::Float3, 8,  0 };   // uvw (z selects shader path)
    l.attributes[2] = { 2, VertexFormat::Float4, 20, 0 };   // rgba
    l.attributes[3] = { 3, VertexFormat::Float4, 36, 0 };   // sdf (half_w, half_h, radius, border)
    l.attribute_count = 4;
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
    // Key on (slot, glyph atlas, colour atlas): the colour atlas handle changes when the
    // emoji atlas grows, which must mint a fresh set rather than reuse a stale binding.
    const uint64_t key = (uint64_t(ubo_slot) * 1000003ull)
                       ^ (uint64_t(tex.id) * 19349663ull)
                       ^ (uint64_t(cur_color_atlas_.id) * 83492791ull);
    auto it = desc_sets_.find(key);
    if (it != desc_sets_.end()) return it->second;
    DescriptorSetDesc d; d.layout = set_layout_; d.write_count = 4;
    d.writes[0].binding = 0; d.writes[0].type = BindingType::UniformBuffer;  d.writes[0].buffer = proj_ubo_[ubo_slot]; d.writes[0].buffer_size = 16 * sizeof(float);
    d.writes[1].binding = 1; d.writes[1].type = BindingType::SampledTexture; d.writes[1].texture = tex;
    d.writes[2].binding = 2; d.writes[2].type = BindingType::Sampler;        d.writes[2].sampler = sampler_;
    d.writes[3].binding = 3; d.writes[3].type = BindingType::SampledTexture; d.writes[3].texture = cur_color_atlas_;
    DescriptorSetHandle set = device_->create_descriptor_set(d);
    desc_sets_.emplace(key, set);
    return set;
}

void GpuGuiRenderer::shutdown() {
    if (!device_) return;
    if (vbo_.valid())            device_->destroy_buffer(vbo_);
    for (uint32_t i = 0; i < kUboSlots; ++i)
        if (proj_ubo_[i].valid()) device_->destroy_buffer(proj_ubo_[i]);
    if (dummy_atlas_.valid())       device_->destroy_texture(dummy_atlas_);
    if (dummy_color_atlas_.valid()) device_->destroy_texture(dummy_color_atlas_);
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

// One vertex: pos | uvw (z = shader path) | rgba | sdf (half_w,half_h,radius,border).
void GpuGuiRenderer::push_vert(float px, float py, float u, float vv, float layer,
                               const math::Vec4& c, float s0, float s1, float s2, float s3) {
    verts_.push_back(px); verts_.push_back(py);
    verts_.push_back(u);  verts_.push_back(vv); verts_.push_back(layer);
    verts_.push_back(c.x); verts_.push_back(c.y); verts_.push_back(c.z); verts_.push_back(c.w);
    verts_.push_back(s0); verts_.push_back(s1); verts_.push_back(s2); verts_.push_back(s3);
}

void GpuGuiRenderer::emit_quad(float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1,
                               float layer, const math::Vec4& c) {
    auto v = [&](float px, float py, float u, float vv) { push_vert(px, py, u, vv, layer, c, 0,0,0,0); };
    v(x,     y,     u0, v0); v(x + w, y,     u1, v0); v(x + w, y + h, u1, v1);
    v(x,     y,     u0, v0); v(x + w, y + h, u1, v1); v(x,     y + h, u0, v1);
}

// One SDF quad (6 verts): rounded box centred at (cx,cy), half-extent (hw,hh),
// corner radius `radius`, `border` > 0 draws a centred outline ring instead of a
// fill. Circle = hw==hh==radius. The fragment computes the distance field so the
// shape is smooth at any scale with free anti-aliasing (see gui.hlsl). Local
// coords (relative to centre) go in uvw.xy; the quad is grown by a 1px AA margin.
void GpuGuiRenderer::emit_sdf_box(float cx, float cy, float hw, float hh,
                                  float radius, float border, const math::Vec4& c) {
    const float m = 1.0f;                      // AA margin (so the edge isn't clipped)
    const float ex = hw + m, ey = hh + m;
    auto v = [&](float lx, float ly) {
        push_vert(cx + lx, cy + ly, lx, ly, -2.0f, c, hw, hh, radius, border);
    };
    v(-ex, -ey); v(ex, -ey); v(ex, ey);
    v(-ex, -ey); v(ex, ey);  v(-ex, ey);
}

void GpuGuiRenderer::emit_circle(float cx, float cy, float radius, const math::Vec4& c) {
    emit_sdf_box(cx, cy, radius, radius, radius, 0.0f, c);
}

void GpuGuiRenderer::emit_round_rect(float x, float y, float w, float h, float radius, const math::Vec4& c) {
    const float r = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    emit_sdf_box(x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, r, 0.0f, c);
}

// Thick line p0→p1 as a rotated quad (two triangles), solid colour (layer = -1).
void GpuGuiRenderer::emit_line(float x0, float y0, float x1, float y1, float width, const math::Vec4& c) {
    auto v = [&](float px, float py) { push_vert(px, py, 0, 0, -1.0f, c, 0,0,0,0); };
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
                            int fb_w, int fb_h, float scale, TextureHandle color_atlas) {
    if (!device_ || !cmd) return;

    // The colour-emoji atlas bound at binding 3 for every draw this pass.
    cur_color_atlas_ = color_atlas.valid() ? color_atlas : dummy_color_atlas_;

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
            if      (c.shape == DrawShape::Circle)    { emit_circle(px + pw * 0.5f, py + ph * 0.5f, pw * 0.5f, c.color); vc += kSdfBoxVerts; cur.count += kSdfBoxVerts; }
            else if (c.shape == DrawShape::Line)      { emit_line(px, py, c.line_x1, c.line_y1, c.line_w, c.color);     vc += 6;      cur.count += 6; }
            else if (c.shape == DrawShape::RoundRect) { emit_round_rect(px, py, pw, ph, c.corner_radius, c.color);      vc += kSdfBoxVerts; cur.count += kSdfBoxVerts; }
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

namespace {

// ---- CanvasView vector emission -------------------------------------------
// Backdrop, world grid and retained wires of every visible IGuiCanvasView in
// the context tree render through the vector underlay (drawn UNDER the widget
// batch, so wires sit above the grid and below the world-space children). The
// widget only holds data (gui/ has no renderer dependency); this is the one
// place that turns it into geometry, once per actually-rendered frame.

// Emit only the parts of a screen-space cubic that intersect the canvas rect
// [rx0,ry0..rx1,ry1]. A context can hold several canvases (e.g. a node editor
// next to a GPU-topology view); wires are drawn in the unclipped vector
// underlay, so without this a panned wire would bleed across into the
// neighbouring canvas. Conservative control-polygon bounds: fully outside →
// drop, fully inside (or max depth) → draw, else De Casteljau split + recurse.
static void emit_clipped_bezier(gfx::VectorRenderer& vr,
                                float p0x, float p0y, float c0x, float c0y,
                                float c1x, float c1y, float p1x, float p1y,
                                float rx0, float ry0, float rx1, float ry1,
                                const math::Vec4& color, int depth) {
    const float mnx = std::min(std::min(p0x, p1x), std::min(c0x, c1x));
    const float mxx = std::max(std::max(p0x, p1x), std::max(c0x, c1x));
    const float mny = std::min(std::min(p0y, p1y), std::min(c0y, c1y));
    const float mxy = std::max(std::max(p0y, p1y), std::max(c0y, c1y));
    if (mxx < rx0 || mnx > rx1 || mxy < ry0 || mny > ry1) return;          // fully outside
    const bool inside = mnx >= rx0 && mxx <= rx1 && mny >= ry0 && mxy <= ry1;
    if (inside || depth <= 0) {
        vr.bezier({p0x, p0y, 0.0f}, {c0x, c0y, 0.0f}, {c1x, c1y, 0.0f}, {p1x, p1y, 0.0f}, color);
        return;
    }
    // De Casteljau split at t = 0.5
    const float ax = (p0x + c0x) * 0.5f, ay = (p0y + c0y) * 0.5f;
    const float bx = (c0x + c1x) * 0.5f, by = (c0y + c1y) * 0.5f;
    const float cx = (c1x + p1x) * 0.5f, cy = (c1y + p1y) * 0.5f;
    const float dx = (ax + bx) * 0.5f,   dy = (ay + by) * 0.5f;
    const float ex = (bx + cx) * 0.5f,   ey = (by + cy) * 0.5f;
    const float mx = (dx + ex) * 0.5f,   my = (dy + ey) * 0.5f;
    emit_clipped_bezier(vr, p0x, p0y, ax, ay, dx, dy, mx, my, rx0, ry0, rx1, ry1, color, depth - 1);
    emit_clipped_bezier(vr, mx, my, ex, ey, cx, cy, p1x, p1y, rx0, ry0, rx1, ry1, color, depth - 1);
}

void emit_canvas_wire(gfx::VectorRenderer& vr, const IGuiCanvasView& cv,
                      const CanvasWire& wire, const CanvasStyle& cs, float ui) {
    const int last = (int)wire.points.size() - 1;
    if (last < 1) return;
    const CanvasWireStyle& ws = wire.style;
    // Clip rect = this canvas's own screen rect (physical px, same space as sp).
    const math::Box cb = cv.get_bounds();
    const float rx0 = math::x(math::box_min(cb)) * ui, ry0 = math::y(math::box_min(cb)) * ui;
    const float rx1 = rx0 + math::box_width(cb) * ui, ry1 = ry0 + math::box_height(cb) * ui;
    // Everything the canvas produces is LOGICAL; the vector layer draws in
    // physical px, so world-derived sizes carry the ui (DPI) factor and screen
    // points are lifted to physical. The *_px floors are already physical.
    const float s = cv.view_scale() * ui;
    std::vector<math::Vec2> sp;
    sp.reserve(wire.points.size());
    for (const auto& p : wire.points) { math::Vec2 q = cv.world_to_screen(p); sp.push_back(math::Vec2(math::x(q) * ui, math::y(q) * ui)); }
    // Smooth curve through the waypoints: cubic segments with Catmull-Rom
    // handles, first/last tangents forced horizontal (node-editor look). The
    // vector renderer tessellates each cubic to the camera LOD.
    vr.set_line_width(std::max(ws.min_width_px, ws.width * s));
    const float tangent_min = ws.end_tangent_min * s;
    for (int i = 0; i < last; ++i) {
        const math::Vec2& P0 = sp[(std::size_t)(i == 0 ? 0 : i - 1)];
        const math::Vec2& P1 = sp[(std::size_t)i];
        const math::Vec2& P2 = sp[(std::size_t)(i + 1)];
        const math::Vec2& P3 = sp[(std::size_t)(i + 2 <= last ? i + 2 : last)];
        float c1x = math::x(P1) + (math::x(P2) - math::x(P0)) / 6.0f;
        float c1y = math::y(P1) + (math::y(P2) - math::y(P0)) / 6.0f;
        float c2x = math::x(P2) - (math::x(P3) - math::x(P1)) / 6.0f;
        float c2y = math::y(P2) - (math::y(P3) - math::y(P1)) / 6.0f;
        if (i == 0) {
            const float k = std::max(std::fabs(math::x(P2) - math::x(P1)) * 0.5f, tangent_min);
            c1x = math::x(P1) + k; c1y = math::y(P1);
        }
        if (i == last - 1) {
            const float k = std::max(std::fabs(math::x(P2) - math::x(P1)) * 0.5f, tangent_min);
            c2x = math::x(P2) - k; c2y = math::y(P2);
        }
        emit_clipped_bezier(vr, math::x(P1), math::y(P1), c1x, c1y,
                            c2x, c2y, math::x(P2), math::y(P2),
                            rx0, ry0, rx1, ry1, ws.color, /*depth=*/10);
    }
    if (ws.handles && last >= 2) {
        // Interior waypoints get a grab ring: wire-coloured disc + backdrop hole.
        // 同樣裁切到本畫布（跨畫布不畫）。
        const math::Vec4 hole = ws.handle_hole_color.w > 0.0f ? ws.handle_hole_color
                                                              : cs.backdrop_color;
        for (int i = 1; i < last; ++i) {
            const float hx = math::x(sp[(std::size_t)i]), hy = math::y(sp[(std::size_t)i]);
            if (hx < rx0 || hx > rx1 || hy < ry0 || hy > ry1) continue;
            vr.fill_circle(hx, hy, std::max(ws.handle_min_px, ws.handle_radius * s), ws.color);
            vr.fill_circle(hx, hy, std::max(ws.handle_hole_min_px, ws.handle_hole_radius * s), hole);
        }
    }
}

// `ui` = the context's UI (DPI) scale: the canvas is laid out in logical px but
// the vector underlay draws in physical px, so all logical coords/extents are
// lifted by ui here. Grid line thickness stays physical (a crisp 1 px line).
void emit_canvas(gfx::VectorRenderer& vr, IGuiCanvasView* cv, float ui) {
    const CanvasStyle& cs = cv->get_canvas_style();
    const math::Box b = cv->get_bounds();
    const float x = math::x(math::box_min(b)) * ui, y = math::y(math::box_min(b)) * ui;
    const float w = math::box_width(b) * ui, h = math::box_height(b) * ui;
    if (w <= 0.0f || h <= 0.0f) return;
    if (cs.backdrop_color.w > 0.0f) vr.fill_rect(x, y, w, h, cs.backdrop_color);
    const float s = cv->view_scale();
    if (cs.grid_spacing > 0.0f && s >= cs.grid_min_scale) {
        const math::Vec2 o = cv->view_origin();
        const float wr = math::x(o) + math::box_width(b) / s, wb = math::y(o) + math::box_height(b) / s;   // world right/bottom
        for (float gw = std::ceil(math::x(o) / cs.grid_spacing) * cs.grid_spacing; gw < wr; gw += cs.grid_spacing)
            vr.fill_rect(math::x(cv->world_to_screen(math::Vec2(gw, 0.0f))) * ui, y,
                         cs.grid_line_px, h, cs.grid_color);
        for (float gh = std::ceil(math::y(o) / cs.grid_spacing) * cs.grid_spacing; gh < wb; gh += cs.grid_spacing)
            vr.fill_rect(x, math::y(cv->world_to_screen(math::Vec2(0.0f, gh))) * ui,
                         w, cs.grid_line_px, cs.grid_color);
    }
    for (int i = 0; i < cv->wire_count(); ++i)
        emit_canvas_wire(vr, *cv, cv->get_wire(i), cs, ui);
}

void emit_canvases(gfx::VectorRenderer& vr, IGuiWidget* w, float ui) {
    if (!w || !w->is_visible()) return;
    if (w->get_type() == WidgetType::CanvasView)
        emit_canvas(vr, static_cast<IGuiCanvasView*>(w), ui);
    for (int i = 0; i < w->get_child_count(); ++i)
        emit_canvases(vr, w->get_child(i), ui);
}

// Scale every draw command of an app-authored immediate layer (positions, sizes,
// clips, radii, line endpoints/width AND font sizes) by the UI scale, so the
// caller authors it in logical px like the retained tree. Text scales too, so
// glyphs rasterize at physical size and stay crisp.
void scale_render_info(WidgetRenderInfo& ri, float ui) {
    if (ui == 1.0f) return;
    auto sb = [ui](math::Box& box) {
        box = math::make_box(math::x(math::box_min(box)) * ui, math::y(math::box_min(box)) * ui,
                             math::box_width(box) * ui, math::box_height(box) * ui);
    };
    sb(ri.clip_rect);
    for (auto& c : ri.colors) {
        sb(c.dest); sb(c.clip); c.corner_radius *= ui;
        c.line_x1 *= ui; c.line_y1 *= ui; c.line_w *= ui;
    }
    for (auto& t : ri.textures) { sb(t.dest); sb(t.clip); }
    for (auto& s : ri.slices)   { sb(s.dest); sb(s.clip);
        s.border.left *= ui; s.border.top *= ui; s.border.right *= ui; s.border.bottom *= ui; }
    for (auto& t : ri.texts)    { sb(t.dest); sb(t.clip); t.font_size *= ui; }
}

} // namespace

void GpuGuiRenderer::render_window_frame(Graphics* gfx, GraphicCommander* cmd, GpuTextRasterizer* raster,
                                         int fb_w, int fb_h, const ClearColor& clear,
                                         WidgetRenderInfo* immediate, IGuiContext* ctx, float dt,
                                         window::gfx::VectorRenderer* underlay,
                                         WidgetRenderInfo* overlay) {
    if (!gfx || !cmd || !raster || fb_w <= 0 || fb_h <= 0) return;
    // The context's global UI scale: the retained tree is scaled inside
    // get_render_info(); the app-authored immediate/overlay layers and the
    // vector underlay are lifted to physical px here so the app authors every
    // layer in logical px.
    const float ui = ctx ? ctx->get_ui_scale() : 1.0f;
    // Collect every layer BEFORE sync_atlas() so glyphs rasterized this frame
    // (flatten + widget render-info) are uploaded before the draw.
    if (immediate) { scale_render_info(*immediate, ui); immediate->finalize(); immediate->flatten(raster); }
    if (overlay)   { scale_render_info(*overlay, ui);   overlay->finalize();   overlay->flatten(raster); }
    const WidgetRenderInfo* gri = nullptr;
    if (ctx) { ctx->begin_frame(dt); gri = &ctx->get_render_info(); }
    TextureHandle atlas = raster->sync_atlas();
    const float proj[16] = {
        2.0f / fb_w, 0.0f,          0.0f, 0.0f,
        0.0f,       -2.0f / fb_h,   0.0f, 0.0f,
        0.0f,        0.0f,         -1.0f, 0.0f,
       -1.0f,        1.0f,          0.0f, 1.0f,
    };
    // CanvasView widgets (world canvases) draw their backdrop/grid/wires through
    // the vector underlay. With a context present the facade OWNS the batch —
    // begin resets last frame's geometry, canvases emit, end() below draws it;
    // callers must not pre-fill in this mode. (Without a context the underlay is
    // passed through untouched: the caller built its own batch.)
    if (underlay && ctx) {
        underlay->begin(proj, fb_w, fb_h);
        emit_canvases(*underlay, ctx->get_root(), ui);
    }
    cmd->begin();
    cmd->set_render_target_backbuffer();
    window::Viewport vp; vp.x = 0; vp.y = 0; vp.width = float(fb_w); vp.height = float(fb_h);
    cmd->set_viewport(vp);
    cmd->clear_color(clear);
    if (underlay) underlay->end(cmd);
    if (immediate && immediate->is_valid())
        render(cmd, *immediate, atlas, proj, fb_w, fb_h, 1.0f, raster->color_atlas());
    if (gri && gri->is_valid())
        render(cmd, const_cast<WidgetRenderInfo&>(*gri), atlas, proj, fb_w, fb_h, 1.0f,
               raster->color_atlas());
    if (overlay && overlay->is_valid())   // popups/menus on top of the retained widgets
        render(cmd, *overlay, atlas, proj, fb_w, fb_h, 1.0f, raster->color_atlas());
    cmd->end();
    submit_commander(gfx, cmd);
}

} // namespace gui
} // namespace window
