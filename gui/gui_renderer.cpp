#include "gui_renderer.hpp"

#include <cmath>

namespace window {
namespace gui {
namespace {

const int FLOATS_PER_VERT = 9;  // pos2 + uvw3 + rgba4

// GLSL (OpenGL backend). Projection comes through a std140 UBO at binding 0
// (the abstraction's push_constants); the glyph atlas is a sampler2DArray.
const char* VS_GLSL = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aUVW;
layout(location=2) in vec4 aColor;
out vec3 vUVW;
out vec4 vColor;
layout(std140, binding=0) uniform Proj { mat4 uProjection; };
void main() { gl_Position = uProjection * vec4(aPos, 0.0, 1.0); vUVW = aUVW; vColor = aColor; }
)";

const char* FS_GLSL = R"(#version 330 core
in vec3 vUVW;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2DArray uTexture;
void main() {
    if (vUVW.z >= 0.0) { float a = texture(uTexture, vUVW).r; FragColor = vec4(vColor.rgb, vColor.a * a); }
    else               { FragColor = vColor; }
}
)";

} // namespace

bool GpuGuiRenderer::init(GraphicDevice* device) {
    device_ = device;
    if (!device_) return false;

    ShaderDesc vd; vd.stage = ShaderStage::Vertex;   vd.language = ShaderLanguage::GLSL; vd.code = VS_GLSL;
    ShaderDesc fd; fd.stage = ShaderStage::Fragment; fd.language = ShaderLanguage::GLSL; fd.code = FS_GLSL;
    vs_ = device_->create_shader(vd);
    fs_ = device_->create_shader(fd);
    if (!vs_.valid() || !fs_.valid()) return false;

    PipelineDesc pd;
    pd.vertex_shader   = vs_;
    pd.fragment_shader = fs_;
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
    pipeline_ = device_->create_pipeline(pd);
    return pipeline_.valid();
}

void GpuGuiRenderer::shutdown() {
    if (!device_) return;
    if (vbo_.valid())      device_->destroy_buffer(vbo_);
    if (pipeline_.valid()) device_->destroy_pipeline(pipeline_);
    if (vs_.valid())       device_->destroy_shader(vs_);
    if (fs_.valid())       device_->destroy_shader(fs_);
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

void GpuGuiRenderer::render(GraphicCommander* cmd, WidgetRenderInfo& info,
                            TextureHandle atlas, const float proj[16],
                            int fb_w, int fb_h, float scale) {
    if (!device_ || !cmd) return;

    // Expand text + 9-slice → Color + Texture quads (idempotent).
    info.flatten(rasterizer_);

    verts_.clear();

    struct Range { uint32_t first, count; int sx, sy, sw, sh; };
    std::vector<Range> ranges;
    Range cur{ 0, 0, 0, 0, fb_w, fb_h };   // default: full-screen scissor (= no clip)
    uint32_t vert_count = 0;

    using Pool = WidgetRenderInfo::DrawRef::Pool;
    for (const auto& ref : info.get_draw_order()) {
        if (ref.clip_changed) {
            if (cur.count > 0) ranges.push_back(cur);
            cur = Range{ vert_count, 0, 0, 0, fb_w, fb_h };
            const float bw = math::box_width(ref.clip), bh = math::box_height(ref.clip);
            if (bw > 0.0f && bh > 0.0f) {
                const float bx = math::x(math::box_min(ref.clip));
                const float by = math::y(math::box_min(ref.clip));
                cur.sx = int(bx * scale);
                cur.sw = int(bw * scale);
                cur.sh = int(bh * scale);
                cur.sy = int(fb_h - (by + bh) * scale);   // GL scissor origin = bottom-left
            }
        }
        if (ref.pool == Pool::Color) {
            const auto& c = info.colors[ref.index];
            const float px = math::x(math::box_min(c.dest)), py = math::y(math::box_min(c.dest));
            const float pw = math::box_width(c.dest),        ph = math::box_height(c.dest);
            if (c.shape == DrawShape::Circle) { emit_circle(px + pw * 0.5f, py + ph * 0.5f, pw * 0.5f, c.color); vert_count += 24 * 3; cur.count += 24 * 3; }
            else                              { emit_quad(px, py, pw, ph, 0,0,0,0, -1.0f, c.color);              vert_count += 6;      cur.count += 6; }
        } else if (ref.pool == Pool::Texture) {
            const auto& t = info.textures[ref.index];
            if (t.atlas_layer < 0) continue;   // file/memory image textures: TODO (separate sampler/pass)
            const float px = math::x(math::box_min(t.dest)), py = math::y(math::box_min(t.dest));
            const float pw = math::box_width(t.dest),        ph = math::box_height(t.dest);
            const float u0 = math::x(math::box_min(t.uv)),   v0 = math::y(math::box_min(t.uv));
            const float u1 = math::x(math::box_max(t.uv)),   v1 = math::y(math::box_max(t.uv));
            emit_quad(px, py, pw, ph, u0, v0, u1, v1, float(t.atlas_layer), t.tint);
            vert_count += 6; cur.count += 6;
        }
        // Slice9 / Text refs do not survive flatten(); ignored if present.
    }
    if (cur.count > 0) ranges.push_back(cur);
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

    cmd->set_pipeline(pipeline_);
    cmd->push_constants(0, proj, 16 * sizeof(float));
    cmd->bind_vertex_buffer(0, vbo_);
    if (atlas.valid()) cmd->bind_texture(0, atlas);
    for (const auto& r : ranges) {
        cmd->set_scissor(ScissorRect{ r.sx, r.sy, r.sw, r.sh });
        cmd->draw(r.count, r.first);
    }
}

} // namespace gui
} // namespace window
