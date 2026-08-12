#include "stencil_clip.hpp"

#ifdef WINDOW_SUPPORT_SHADER_COMPILER
#include "shader_compiler/shader_compiler.hpp"   // self-contained init() compiles the mask shaders
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

// Directory the GUI shaders are read from at runtime, matching gui_renderer.cpp.
#ifndef WINDOW_SHADER_DIR
#define WINDOW_SHADER_DIR "renderer/shaders"
#endif

namespace window {
namespace gfx {
namespace {

// The mask reuses gui.hlsl's vs_main, so its vertices must use the GUI vertex layout:
// pos2 | uvw3 | rgba4 | sdf4. ps_clip_mask reads uvw.xy (local position) and sdf.xyz
// (half_w, half_h, corner_radius); the colour and the atlas layer go unused.
const int FLOATS_PER_VERT = 13;
const uint32_t QUAD_VERTS = 6;

} // namespace

//--- ClipShape ---------------------------------------------------------------

ClipShape ClipShape::from_rect(float x, float y, float w, float h, float radius) {
    ClipShape s;
    s.half_w = w * 0.5f;
    s.half_h = h * 0.5f;
    s.corner_radius = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    s.tx = x + s.half_w;
    s.ty = y + s.half_h;
    return s;
}

ClipShape ClipShape::rotated(float cx, float cy, float w, float h, float radians, float radius) {
    ClipShape s = from_rect(cx - w * 0.5f, cy - h * 0.5f, w, h, radius);
    const float co = std::cos(radians), si = std::sin(radians);
    s.a =  co; s.b = si;      // local X axis in screen space
    s.c = -si; s.d = co;      // local Y axis in screen space
    return s;
}

ClipShape ClipShape::covering(const float proj[16]) {
    // Column-major orthographic: clip.x = proj[0]*px + proj[12], clip.y = proj[5]*py + proj[13].
    // Invert at the clip-space corners (-1, 1) to get the covering rect in the source space.
    const float sx = proj ? proj[0] : 0.0f, sy = proj ? proj[5] : 0.0f;
    if (sx == 0.0f || sy == 0.0f) return from_rect(-1.0e6f, -1.0e6f, 2.0e6f, 2.0e6f);
    const float ax = (-1.0f - proj[12]) / sx, bx = (1.0f - proj[12]) / sx;
    const float ay = (-1.0f - proj[13]) / sy, by = (1.0f - proj[13]) / sy;
    const float x0 = std::min(ax, bx), x1 = std::max(ax, bx);
    const float y0 = std::min(ay, by), y1 = std::max(ay, by);
    return from_rect(x0, y0, x1 - x0, y1 - y0);
}

void ClipShape::screen_bounds(float* out_x0, float* out_y0, float* out_x1, float* out_y1) const {
    // |a|*hw + |c|*hh is the transformed shape's half-width along screen X (and likewise
    // for Y) — exact for a pure translation, the rotated quad's bounds otherwise.
    const float ex = std::fabs(a) * half_w + std::fabs(c) * half_h;
    const float ey = std::fabs(b) * half_w + std::fabs(d) * half_h;
    *out_x0 = tx - ex; *out_y0 = ty - ey;
    *out_x1 = tx + ex; *out_y1 = ty + ey;
}

//--- StencilClipper ----------------------------------------------------------

DepthStencilState StencilClipper::content_state() {
    // Content under a mask: pass only where the stamped reference matches, and never
    // modify the stencil (write mask 0) so one mask can gate many draws.
    DepthStencilState ds = DepthStencilState::disabled();
    ds.stencil_enable = true;
    ds.stencil_read_mask = 0xFF;
    ds.stencil_write_mask = 0x00;
    StencilOpDesc op;
    op.func = CompareFunc::Equal;
    op.stencil_fail = StencilOp::Keep;
    op.depth_fail   = StencilOp::Keep;
    op.pass         = StencilOp::Keep;
    ds.front_face = op;
    ds.back_face  = op;
    return ds;
}

// Self-contained init: compile the mask shaders from the shipped gui.hlsl. Reads the same
// file GpuGuiRenderer does, so there is one source of truth for the mask shape.
bool StencilClipper::init(GraphicDevice* device) {
#ifdef WINDOW_SUPPORT_SHADER_COMPILER
    if (!device) return false;
    const std::string path = std::string(WINDOW_SHADER_DIR) + "/gui.hlsl";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    if (n <= 0) return false;
    std::string src((size_t)n, '\0');
    f.seekg(0);
    f.read(&src[0], n);
    own_vs_ = ShaderCompiler::compile_and_create_cached(device, src.c_str(), src.size(),
                                                        ShaderStage::Vertex, "vs_main");
    own_fs_ = ShaderCompiler::compile_and_create_cached(device, src.c_str(), src.size(),
                                                        ShaderStage::Fragment, "ps_clip_mask");
    if (!own_vs_.valid() || !own_fs_.valid()) return false;
    return init(device, own_vs_, own_fs_);
#else
    (void)device;
    return false;   // the mask shaders need the built-in compiler
#endif
}

bool StencilClipper::init(GraphicDevice* device, ShaderHandle vs, ShaderHandle fs) {
    device_ = device;
    if (!device_ || !vs.valid() || !fs.valid()) return false;
    backend_ = device_->get_backend();

    // Projection UBO ring + one descriptor set per slot. vs_main's only resource is the
    // projection at binding 0, so the mask needs a one-binding layout of its own rather
    // than the GUI's (which also carries the glyph atlases).
    float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    DescriptorSetLayoutDesc dl; dl.binding_count = 1;
    dl.bindings[0] = { 0, BindingType::UniformBuffer, 1, STAGE_VERTEX };
    set_layout_ = device_->create_descriptor_set_layout(dl);
    PipelineLayoutDesc pll; pll.set_layout_count = 1; pll.set_layouts[0] = set_layout_;
    pipe_layout_ = device_->create_pipeline_layout(pll);
    for (uint32_t i = 0; i < kUboSlots; ++i) {
        BufferDesc ud; ud.size = 16 * sizeof(float); ud.type = BufferType::Uniform;
        ud.usage = ResourceUsage::Dynamic; ud.initial_data = identity;
        proj_ubo_[i] = device_->create_buffer(ud);
        if (!proj_ubo_[i].valid()) return false;
        DescriptorSetDesc d; d.layout = set_layout_; d.write_count = 1;
        d.writes[0].binding = 0; d.writes[0].type = BindingType::UniformBuffer;
        d.writes[0].buffer = proj_ubo_[i]; d.writes[0].buffer_size = 16 * sizeof(float);
        desc_set_[i] = device_->create_descriptor_set(d);
    }

    // The mask pipeline writes ONLY the stencil: colour write mask 0 (nothing reaches the
    // render target), depth off, stencil Always → Replace so every surviving fragment
    // stamps the dynamic reference. Scissor stays off — that is the state being replaced.
    PipelineDesc pd;
    pd.vertex_shader   = vs;
    pd.fragment_shader = fs;
    pd.layout          = pipe_layout_;
    pd.topology        = PrimitiveTopology::TriangleList;
    pd.blend           = BlendState::disabled();
    pd.blend.write_mask = 0x00;
    pd.rasterizer      = RasterizerState::no_cull();   // a mirrored transform must not cull
    pd.rasterizer.scissor_enable = false;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.depth_stencil.stencil_enable     = true;
    pd.depth_stencil.stencil_read_mask  = 0xFF;
    pd.depth_stencil.stencil_write_mask = 0xFF;
    StencilOpDesc op;
    op.func = CompareFunc::Always;
    op.stencil_fail = StencilOp::Keep;
    op.depth_fail   = StencilOp::Keep;
    op.pass         = StencilOp::Replace;
    pd.depth_stencil.front_face = op;
    pd.depth_stencil.back_face  = op;
    VertexLayout& l = pd.vertex_layout;
    l.attributes[0] = { 0, VertexFormat::Float2, 0,  0 };   // pos
    l.attributes[1] = { 1, VertexFormat::Float3, 8,  0 };   // uvw (xy = local position)
    l.attributes[2] = { 2, VertexFormat::Float4, 20, 0 };   // rgba (unused)
    l.attributes[3] = { 3, VertexFormat::Float4, 36, 0 };   // sdf (half_w, half_h, radius, -)
    l.attribute_count = 4;
    l.strides[0]      = FLOATS_PER_VERT * sizeof(float);
    l.buffer_count    = 1;
    mask_pipeline_ = device_->create_pipeline(pd);
    return mask_pipeline_.valid();
}

void StencilClipper::shutdown() {
    if (!device_) return;
    if (vbo_.valid()) device_->destroy_buffer(vbo_);
    for (uint32_t i = 0; i < kUboSlots; ++i) {
        if (desc_set_[i].valid()) device_->destroy_descriptor_set(desc_set_[i]);
        if (proj_ubo_[i].valid()) device_->destroy_buffer(proj_ubo_[i]);
    }
    if (mask_pipeline_.valid()) device_->destroy_pipeline(mask_pipeline_);
    // Only the self-contained init() owns shaders; the explicit overload borrows them.
    if (own_vs_.valid()) device_->destroy_shader(own_vs_);
    if (own_fs_.valid()) device_->destroy_shader(own_fs_);
    if (pipe_layout_.valid())   device_->destroy_pipeline_layout(pipe_layout_);
    if (set_layout_.valid())    device_->destroy_descriptor_set_layout(set_layout_);
    if (depth_target_.valid())  device_->destroy_render_target(depth_target_);
    device_ = nullptr;
}

RenderTargetHandle StencilClipper::depth_target(int fb_w, int fb_h) {
    // OpenGL's default framebuffer carries the stencil the context was created with and
    // takes no attachments, so allocating one would be pure waste.
    if (!device_ || !needs_depth_attachment() || fb_w <= 0 || fb_h <= 0) return {};
    if (depth_target_.valid() && depth_w_ == fb_w && depth_h_ == fb_h) return depth_target_;
    if (depth_target_.valid()) device_->destroy_render_target(depth_target_);
    DepthStencilDesc dd;
    dd.width = fb_w; dd.height = fb_h;
    dd.format = TextureFormat::D24_UNORM_S8_UINT;   // the stencil is what we are after
    depth_target_ = device_->create_depth_target(dd);
    depth_w_ = fb_w; depth_h_ = fb_h;
    return depth_target_;
}

// Called once the stencil buffer has been cleared to 0, so both the reference allocator
// and the generation bookkeeping start from the state the buffer is actually in.
void StencilClipper::begin_frame() {
    next_ref_ = 1;
    generation_ = drawn_generation_ = 0;
}

void StencilClipper::begin_pass(const float proj[16], int fb_w, int fb_h) {
    verts_.clear();
    masks_.clear();
    fb_w_ = fb_w; fb_h_ = fb_h;
    // The pass's "everything" shape, in the space `proj` maps from — not the framebuffer
    // rect, which only coincides when the projection carries no DPI/zoom/pan factor.
    cover_ = ClipShape::covering(proj);
    // Every pass keeps a copy of it at the head of its buffer, so draw_mask() can wipe the
    // stencil on a generation change no matter which mask turns out to need it.
    reset_first_ = 0;
    reset_count_ = QUAD_VERTS;
    push_quad(cover_);
    if (!device_ || !proj) return;
    cur_slot_ = ubo_slot_;
    ubo_slot_ = (ubo_slot_ + 1) % kUboSlots;
    device_->update_buffer(proj_ubo_[cur_slot_], proj, 16 * sizeof(float), 0);
}

// One mask quad (6 verts): the shape's local corners transformed to screen space, with
// the LOCAL corner kept in uvw.xy so ps_clip_mask evaluates the rounded box in local
// space. That split is what keeps the mask exact under rotation — the transform moves
// the geometry, never the distance field.
void StencilClipper::push_quad(const ClipShape& s) {
    auto v = [&](float lx, float ly) {
        verts_.push_back(s.a * lx + s.c * ly + s.tx);   // pos.x
        verts_.push_back(s.b * lx + s.d * ly + s.ty);   // pos.y
        verts_.push_back(lx); verts_.push_back(ly); verts_.push_back(-2.0f);  // uvw
        verts_.push_back(1.0f); verts_.push_back(1.0f); verts_.push_back(1.0f); verts_.push_back(1.0f);
        verts_.push_back(s.half_w); verts_.push_back(s.half_h);
        verts_.push_back(s.corner_radius); verts_.push_back(0.0f);
    };
    const float hw = s.half_w, hh = s.half_h;
    v(-hw, -hh); v(hw, -hh); v(hw, hh);
    v(-hw, -hh); v(hw, hh);  v(-hw, hh);
}

uint32_t StencilClipper::add(const ClipShape& shape) {
    if (!device_ || !mask_pipeline_.valid() || shape.is_degenerate()) return kNoMask;

    // The 8-bit stencil gives 255 usable references at a time (0 means "outside every
    // clip"). Exhausting them opens a new generation; draw_mask() wipes the buffer back
    // to 0 when it first draws into one. The wipe is a quad rather than a clear because
    // Vulkan cannot record a clear inside a render pass.
    if (next_ref_ > 255) {
        next_ref_ = 1;
        ++generation_;
    }
    Mask m;
    m.ref = uint8_t(next_ref_++);
    m.generation = generation_;
    m.first = uint32_t(verts_.size()) / FLOATS_PER_VERT;
    push_quad(shape);
    m.count = QUAD_VERTS;
    masks_.push_back(m);
    return uint32_t(masks_.size() - 1);
}

void StencilClipper::upload() {
    if (!device_ || verts_.empty()) return;
    // VBO ring: this pass writes at its own byte offset so an earlier pass this frame is
    // not overwritten on the deferred backends; wrap when full, grow if one pass exceeds
    // the capacity. Mirrors the ring in gui_renderer/vector_renderer.
    const uint32_t bytes = uint32_t(verts_.size() * sizeof(float));
    const uint32_t need  = (bytes + 15u) & ~15u;
    if (!vbo_.valid() || need > vbo_capacity_) {
        if (vbo_.valid()) device_->destroy_buffer(vbo_);
        uint32_t cap = vbo_capacity_ ? vbo_capacity_ : 8u * 1024u;
        while (cap < need) cap *= 2;
        BufferDesc bd; bd.size = cap; bd.type = BufferType::Vertex; bd.usage = ResourceUsage::Dynamic;
        vbo_ = device_->create_buffer(bd); vbo_capacity_ = cap; vbo_off_ = 0;
    } else if (vbo_off_ + need > vbo_capacity_) {
        vbo_off_ = 0;   // wrap (prior frames' passes are done via per-frame present sync)
    }
    vbo_base_ = vbo_off_;
    device_->update_buffer(vbo_, verts_.data(), bytes, vbo_base_);
    vbo_off_ += need;
}

void StencilClipper::draw_mask(GraphicCommander* cmd, uint32_t mask) {
    if (!cmd || !device_ || mask >= masks_.size() || !vbo_.valid()) return;
    const Mask& m = masks_[mask];
    cmd->set_pipeline(mask_pipeline_);
    cmd->bind_vertex_buffer(0, vbo_, vbo_base_);
    cmd->bind_descriptor_set(0, desc_set_[cur_slot_]);
    // Entering a new generation: wipe the whole target back to 0 so no reference value
    // survives from the previous one. Keyed on what the buffer actually holds, so a mask
    // that was allocated but never drawn cannot swallow the wipe.
    if (m.generation != drawn_generation_) {
        cmd->set_stencil_reference(0);
        cmd->draw(reset_count_, reset_first_);
        drawn_generation_ = m.generation;
    }
    cmd->set_stencil_reference(m.ref);
    cmd->draw(m.count, m.first);
}

uint8_t StencilClipper::ref_of(uint32_t mask) const {
    return mask < masks_.size() ? masks_[mask].ref : 0;
}

} // namespace gfx
} // namespace window
