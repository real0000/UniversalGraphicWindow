#include "gui_renderer.hpp"
#include "gui_vk_vert.spv.h"        // Vulkan SPIR-V (glslangValidator): proj via push constant
#include "gui_vk_atlas_frag.spv.h"  // sampler2DArray atlas (alpha mask / solid)
#include "gui_vk_image_frag.spv.h"  // sampler2D image * tint

#include <cmath>
#include <cstdint>

namespace window {
namespace gui {
namespace {

const int FLOATS_PER_VERT = 9;  // pos2 + uvw3 + rgba4

// GLSL (OpenGL backend). Projection comes through a std140 UBO at binding 0
// (the abstraction's push_constants); the glyph atlas is a sampler2DArray.
const char* VS_GLSL = R"(#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aUVW;
layout(location=2) in vec4 aColor;
out vec3 vUVW;
out vec4 vColor;
layout(std140, binding=0) uniform Proj { mat4 uProjection; };
void main() { gl_Position = uProjection * vec4(aPos, 0.0, 1.0); vUVW = aUVW; vColor = aColor; }
)";

const char* FS_GLSL = R"(#version 460 core
in vec3 vUVW;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2DArray uTexture;
void main() {
    if (vUVW.z >= 0.0) { float a = texture(uTexture, vUVW).r; FragColor = vec4(vColor.rgb, vColor.a * a); }
    else               { FragColor = vColor; }
}
)";

// Image fragment shader: samples a full RGBA sampler2D and modulates by the vertex
// colour (tint). Shares VS_GLSL; uvw.z (the atlas layer) is unused here.
const char* FS_IMAGE_GLSL = R"(#version 460 core
in vec3 vUVW;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uImage;
void main() { FragColor = texture(uImage, vUVW.xy) * vColor; }
)";

} // namespace

bool GpuGuiRenderer::init(GraphicDevice* device) {
    device_ = device;
    if (!device_) return false;
    backend_ = device_->get_backend();

    // Shaders are per-backend: GL uses GLSL (proj via a UBO emulating push_constants,
    // atlas via bind_texture); Vulkan uses SPIR-V (proj via a real push constant,
    // atlas/image via a descriptor set). D3D/Metal renderer shaders are a follow-up.
    ShaderDesc vd; vd.stage = ShaderStage::Vertex;
    ShaderDesc fd; fd.stage = ShaderStage::Fragment;
    ShaderDesc fid; fid.stage = ShaderStage::Fragment;
    if (backend_ == Backend::Vulkan) {
        vd.language  = ShaderLanguage::SPIRV; vd.code  = gui_vk_vert_spv;        vd.code_size  = sizeof(gui_vk_vert_spv);
        fd.language  = ShaderLanguage::SPIRV; fd.code  = gui_vk_atlas_frag_spv;  fd.code_size  = sizeof(gui_vk_atlas_frag_spv);
        fid.language = ShaderLanguage::SPIRV; fid.code = gui_vk_image_frag_spv;  fid.code_size = sizeof(gui_vk_image_frag_spv);
    } else if (backend_ == Backend::OpenGL) {
        vd.language  = ShaderLanguage::GLSL; vd.code  = VS_GLSL;
        fd.language  = ShaderLanguage::GLSL; fd.code  = FS_GLSL;
        fid.language = ShaderLanguage::GLSL; fid.code = FS_IMAGE_GLSL;
    } else {
        return false;   // D3D/Metal GUI shaders not provided yet (follow-up)
    }
    vs_       = device_->create_shader(vd);
    fs_       = device_->create_shader(fd);
    fs_image_ = device_->create_shader(fid);
    if (!vs_.valid() || !fs_.valid() || !fs_image_.valid()) return false;

    // Modern backends: a pipeline layout (push range for proj + a 1-binding atlas/
    // image descriptor set) and a sampler. GL ignores the layout (UBO + bind_texture).
    if (uses_descriptor_sets()) {
        DescriptorSetLayoutDesc dl; dl.binding_count = 1;
        dl.bindings[0] = { 0, BindingType::CombinedImageSampler, 1, STAGE_FRAGMENT };
        set_layout_ = device_->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pll; pll.set_layout_count = 1; pll.set_layouts[0] = set_layout_;
        pll.push_constant_count = 1; pll.push_constants[0] = { 0, 16 * sizeof(float), STAGE_VERTEX };
        pipe_layout_ = device_->create_pipeline_layout(pll);
        SamplerState ss; sampler_ = device_->create_sampler(ss);
    }

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

// Per-texture descriptor set (cached by texture id) for the modern-binding path:
// one combined-image-sampler at binding 0 (atlas = sampler2DArray, image = sampler2D).
DescriptorSetHandle GpuGuiRenderer::desc_set_for(TextureHandle tex) {
    auto it = desc_sets_.find(tex.id);
    if (it != desc_sets_.end()) return it->second;
    DescriptorSetDesc d; d.layout = set_layout_; d.write_count = 1;
    d.writes[0].binding = 0; d.writes[0].type = BindingType::CombinedImageSampler;
    d.writes[0].texture = tex; d.writes[0].sampler = sampler_;
    DescriptorSetHandle set = device_->create_descriptor_set(d);
    desc_sets_.emplace(tex.id, set);
    return set;
}

void GpuGuiRenderer::shutdown() {
    if (!device_) return;
    if (vbo_.valid())            device_->destroy_buffer(vbo_);
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
                sy = uses_descriptor_sets() ? int(by * scale) : int(fb_h - (by + bh) * scale);
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
            if      (c.shape == DrawShape::Circle) { emit_circle(px + pw * 0.5f, py + ph * 0.5f, pw * 0.5f, c.color); vc += 24 * 3; cur.count += 24 * 3; }
            else if (c.shape == DrawShape::Line)   { emit_line(px, py, c.line_x1, c.line_y1, c.line_w, c.color);     vc += 6;      cur.count += 6; }
            else                                   { emit_quad(px, py, pw, ph, 0,0,0,0, -1.0f, c.color);             vc += 6;      cur.count += 6; }
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

    const uint32_t bytes = uint32_t(verts_.size() * sizeof(float));
    if (!vbo_.valid() || bytes > vbo_capacity_) {
        if (vbo_.valid()) device_->destroy_buffer(vbo_);
        BufferDesc bd; bd.size = bytes; bd.type = BufferType::Vertex; bd.usage = ResourceUsage::Dynamic; bd.initial_data = verts_.data();
        vbo_ = device_->create_buffer(bd);
        vbo_capacity_ = bytes;
    } else {
        device_->update_buffer(vbo_, verts_.data(), bytes, 0);
    }

    for (const auto& s : segs) {
        cmd->set_pipeline(s.kind == Kind::Image ? image_pipeline_ : pipeline_);
        cmd->push_constants(0, proj, 16 * sizeof(float));
        cmd->bind_vertex_buffer(0, vbo_);
        const TextureHandle tex = (s.kind == Kind::Image) ? TextureHandle{ s.tex } : atlas;
        if (uses_descriptor_sets()) {
            // Vulkan/D3D/Metal: bind the texture through a (cached) descriptor set.
            if (tex.valid()) cmd->bind_descriptor_set(0, desc_set_for(tex));
        } else {
            // OpenGL: direct texture binding (proj is the emulated UBO at binding 0).
            if (tex.valid()) cmd->bind_texture(0, tex);
        }
        cmd->set_scissor(ScissorRect{ s.sx, s.sy, s.sw, s.sh });
        cmd->draw(s.count, s.first);
    }
}

} // namespace gui
} // namespace window
