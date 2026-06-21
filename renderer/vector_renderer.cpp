#include "vector_renderer.hpp"
// Shaders are authored once in HLSL (kHLSL below) and compiled for the active backend by the
// built-in shader compiler, which caches the resulting blob on disk -- so there are no hand-
// baked per-backend artifacts. All entry points share one UBO { mat4 view_proj; vec4 viewport }.
#include "shader_compiler/shader_compiler.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace window {
namespace gfx {
namespace {

const int   FLOATS_PER_VERT = 7;          // thin/fill: pos3 + rgba4
const int   THICK_FLOATS_PER_VERT = 12;   // thick: posThis3 + posOther3 + (side,halfwidth) + rgba4
const float TWO_PI          = 6.28318530718f;

// Per-frame uniforms, std140-compatible. Matches `Uniforms` in every shader.
struct Uniforms { float view_proj[16]; float viewport[4]; };  // 80 bytes

// One HLSL source, three entry points (two vertex shaders + one pixel shader). The view-
// projection + viewport arrive through a std140 UBO at register(b0). mul(uViewProj, p) matches
// the column-major math::Mat4 the renderer uploads (same convention as pbr.hlsl). Vertex inputs
// use TEXCOORD<n> semantics so the RHI maps attribute location n -> the n-th input.
const char* kHLSL = R"(
cbuffer Uniforms : register(b0) { float4x4 uViewProj; float4 uViewport; };

struct VSOut { float4 pos : SV_Position; float4 color : TEXCOORD0; };

// basic: pos3 + rgba4
struct BasicIn { float3 pos : TEXCOORD0; float4 color : TEXCOORD1; };
VSOut vs_basic(BasicIn i) {
    VSOut o;
    o.pos   = mul(uViewProj, float4(i.pos, 1.0));
    o.color = i.color;
    return o;
}

// pass-through colour
float4 ps_color(VSOut i) : SV_Target { return i.color; }

// thick line: widen a segment to 2*half_width pixels in screen space
struct ThickIn {
    float3 posThis  : TEXCOORD0;
    float3 posOther : TEXCOORD1;
    float2 param    : TEXCOORD2;   // x = side(-1/+1), y = half_width(px)
    float4 color    : TEXCOORD3;
};
VSOut vs_thick(ThickIn i) {
    float4 cThis  = mul(uViewProj, float4(i.posThis,  1.0));
    float4 cOther = mul(uViewProj, float4(i.posOther, 1.0));
    float2 res    = uViewport.xy;
    float2 nThis  = cThis.xy  / cThis.w;
    float2 nOther = cOther.xy / cOther.w;
    float2 d      = (nOther - nThis) * res;
    float  len    = length(d);
    float2 dir    = (len > 1e-6) ? d / len : float2(1.0, 0.0);
    float2 nrm    = float2(-dir.y, dir.x);
    float2 offPx  = nrm * i.param.x * i.param.y;
    float2 offNdc = offPx / res * 2.0;
    cThis.xy += offNdc * cThis.w;
    VSOut o; o.pos = cThis; o.color = i.color;
    return o;
}
)";

// Build a pipeline sharing the renderer's render state. `thick` swaps in the
// expanded vertex layout (locations 0..3) used by the thick-line shader.
PipelineHandle make_pipeline(GraphicDevice* dev, const VectorRendererDesc& d,
                             ShaderHandle vs, ShaderHandle fs, PipelineLayoutHandle layout,
                             PrimitiveTopology topo, bool thick) {
    PipelineDesc pd;
    pd.vertex_shader   = vs;
    pd.fragment_shader = fs;
    pd.layout          = layout;            // invalid on GL/D3D11 → reflected/auto bindings
    pd.topology        = topo;
    pd.blend           = d.blend;
    pd.depth_stencil.depth_enable = d.depth_test;
    pd.depth_stencil.depth_write  = d.depth_write;
    pd.depth_stencil.depth_func   = d.depth_func;
    pd.rasterizer      = RasterizerState::no_cull();
    pd.rasterizer.scissor_enable = true;    // a full-size scissor is set in end()
    pd.color_formats[0]    = d.color_format;
    pd.color_format_count  = 1;
    pd.depth_format        = d.depth_format;
    pd.samples             = d.samples;
    VertexLayout& l = pd.vertex_layout;
    if (thick) {
        l.attributes[0] = { 0, VertexFormat::Float3, 0,  0 };   // posThis
        l.attributes[1] = { 1, VertexFormat::Float3, 12, 0 };   // posOther
        l.attributes[2] = { 2, VertexFormat::Float2, 24, 0 };   // (side, half_width)
        l.attributes[3] = { 3, VertexFormat::Float4, 32, 0 };   // rgba
        l.attribute_count = 4;
        l.strides[0]      = THICK_FLOATS_PER_VERT * sizeof(float);
    } else {
        l.attributes[0] = { 0, VertexFormat::Float3, 0,  0 };   // pos
        l.attributes[1] = { 1, VertexFormat::Float4, 12, 0 };   // rgba
        l.attribute_count = 2;
        l.strides[0]      = FLOATS_PER_VERT * sizeof(float);
    }
    l.buffer_count = 1;
    return dev->create_pipeline(pd);
}

} // namespace

bool VectorRenderer::init(GraphicDevice* device, const VectorRendererDesc& desc) {
    device_ = device;
    if (!device_) return false;
    backend_ = device_->get_backend();
    lod_px_  = (desc.curve_tolerance_px > 0.5f) ? desc.curve_tolerance_px : 0.5f;
    min_seg_ = (desc.min_curve_segments > 1) ? desc.min_curve_segments : 1;
    max_seg_ = (desc.max_curve_segments > min_seg_) ? desc.max_curve_segments : min_seg_;

#ifdef WINDOW_SUPPORT_SHADER_COMPILER
    // One HLSL source -> the active backend's blob (cached on disk by the compiler).
    const size_t hlsl_len = std::strlen(kHLSL);
    vs_basic_ = ShaderCompiler::compile_and_create_cached(device_, kHLSL, hlsl_len, ShaderStage::Vertex,   "vs_basic");
    fs_color_ = ShaderCompiler::compile_and_create_cached(device_, kHLSL, hlsl_len, ShaderStage::Fragment, "ps_color");
    vs_thick_ = ShaderCompiler::compile_and_create_cached(device_, kHLSL, hlsl_len, ShaderStage::Vertex,   "vs_thick");
    if (!vs_basic_.valid() || !fs_color_.valid() || !vs_thick_.valid()) return false;
#else
    return false;   // the renderer's GPU shaders now require the built-in shader compiler
#endif

    // The matrix + viewport live in a RING of small uniform buffers (one per slot). Each end()
    // writes a fresh slot fully and binds it, so multiple batches per frame are safe on deferred
    // backends. No explicit pipeline layout: the backend auto-builds the descriptor set from
    // shader reflection and binds it per draw (slot binding works on all 4 backends now).
    for (uint32_t i = 0; i < kUboSlots; ++i) {
        BufferDesc ud; ud.size = sizeof(Uniforms); ud.type = BufferType::Uniform; ud.usage = ResourceUsage::Dynamic;
        ubo_[i] = device_->create_buffer(ud);
        if (!ubo_[i].valid()) return false;
    }

    // Two pipelines: solid fills (basic VS) and screen-space line quads (thick VS). No layout
    // (pipe_layout_ invalid) -> auto reflected layout. Both use TriangleList (uniform support).
    tri_pipeline_   = make_pipeline(device_, desc, vs_basic_, fs_color_, pipe_layout_, PrimitiveTopology::TriangleList, false);
    thick_pipeline_ = make_pipeline(device_, desc, vs_thick_, fs_color_, pipe_layout_, PrimitiveTopology::TriangleList, true);
    return tri_pipeline_.valid() && thick_pipeline_.valid();
}

void VectorRenderer::shutdown() {
    if (!device_) return;
    if (vbo_.valid())            device_->destroy_buffer(vbo_);
    for (uint32_t i = 0; i < kUboSlots; ++i) if (ubo_[i].valid()) device_->destroy_buffer(ubo_[i]);
    if (tri_pipeline_.valid())   device_->destroy_pipeline(tri_pipeline_);
    if (thick_pipeline_.valid()) device_->destroy_pipeline(thick_pipeline_);
    if (vs_basic_.valid())       device_->destroy_shader(vs_basic_);
    if (fs_color_.valid())       device_->destroy_shader(fs_color_);
    if (vs_thick_.valid())       device_->destroy_shader(vs_thick_);
    if (desc_set_.valid())       device_->destroy_descriptor_set(desc_set_);
    if (set_layout_.valid())     device_->destroy_descriptor_set_layout(set_layout_);
    if (pipe_layout_.valid())    device_->destroy_pipeline_layout(pipe_layout_);
    tris_.clear(); thick_.clear(); scratch_.clear();
    device_ = nullptr;
}

void VectorRenderer::push(std::vector<float>& dst, const math::Vec3& p, const math::Vec4& c) {
    dst.push_back(p.x); dst.push_back(p.y); dst.push_back(p.z);
    dst.push_back(c.x); dst.push_back(c.y); dst.push_back(c.z); dst.push_back(c.w);
}

void VectorRenderer::begin(const float view_proj[16], int viewport_w, int viewport_h) {
    for (int i = 0; i < 16; ++i) view_proj_[i] = view_proj[i];
    viewport_w_ = viewport_w;
    viewport_h_ = viewport_h;
    tris_.clear();
    thick_.clear();
}

void VectorRenderer::set_curve_lod(float tolerance_px, int min_segments, int max_segments) {
    lod_px_  = (tolerance_px > 0.5f) ? tolerance_px : 0.5f;
    min_seg_ = (min_segments > 1) ? min_segments : 1;
    max_seg_ = (max_segments > min_seg_) ? max_segments : min_seg_;
}

//--- Camera-aware LOD --------------------------------------------------------

bool VectorRenderer::project_px(const math::Vec3& p, float out[2]) const {
    const float* m = view_proj_;   // column-major: clip = M * (p,1)
    const float cx = m[0]*p.x + m[4]*p.y + m[8] *p.z + m[12];
    const float cy = m[1]*p.x + m[5]*p.y + m[9] *p.z + m[13];
    const float cw = m[3]*p.x + m[7]*p.y + m[11]*p.z + m[15];
    if (cw <= 1e-6f) return false;   // at/behind the camera — projection undefined
    out[0] = (cx / cw * 0.5f + 0.5f) * float(viewport_w_);
    out[1] = (cy / cw * 0.5f + 0.5f) * float(viewport_h_);
    return true;
}

int VectorRenderer::lod_for_polyline(const math::Vec3* ctrl, int n) const {
    if (n < 2 || viewport_w_ <= 0 || viewport_h_ <= 0) return min_seg_;
    float prev[2], cur[2];
    if (!project_px(ctrl[0], prev)) return max_seg_;   // crosses the camera plane → finest
    float len_px = 0.0f;
    for (int i = 1; i < n; ++i) {
        if (!project_px(ctrl[i], cur)) return max_seg_;
        const float dx = cur[0] - prev[0], dy = cur[1] - prev[1];
        len_px += std::sqrt(dx*dx + dy*dy);
        prev[0] = cur[0]; prev[1] = cur[1];
    }
    int seg = int(std::ceil(len_px / lod_px_));
    if (seg < min_seg_) seg = min_seg_;
    if (seg > max_seg_) seg = max_seg_;
    return seg;
}

int VectorRenderer::lod_for_arc(const math::Vec3& center, const math::Vec3& u, const math::Vec3& v,
                                float r, float sweep) const {
    if (viewport_w_ <= 0 || viewport_h_ <= 0) return min_seg_;
    float pc[2], pu[2], pv[2];
    if (!project_px(center, pc) || !project_px(center + u * r, pu) || !project_px(center + v * r, pv))
        return max_seg_;
    const float du = std::sqrt((pu[0]-pc[0])*(pu[0]-pc[0]) + (pu[1]-pc[1])*(pu[1]-pc[1]));
    const float dv = std::sqrt((pv[0]-pc[0])*(pv[0]-pc[0]) + (pv[1]-pc[1])*(pv[1]-pc[1]));
    const float pr = (du > dv) ? du : dv;                 // pixel radius (worst case)
    int seg = int(std::ceil(pr * std::fabs(sweep) / lod_px_));
    if (seg < min_seg_) seg = min_seg_;
    if (seg > max_seg_) seg = max_seg_;
    return seg;
}

void VectorRenderer::plane_basis(const math::Vec3& axis, math::Vec3* u, math::Vec3* v) {
    math::Vec3 n = math::normalize(axis);
    math::Vec3 ref = (std::fabs(n.y) < 0.99f) ? math::Vec3::up() : math::Vec3::right();
    *u = math::normalize(math::cross(ref, n));
    *v = math::cross(n, *u);
}

//--- Core primitives ---------------------------------------------------------

void VectorRenderer::thick_corner(const math::Vec3& self, const math::Vec3& other, float side,
                                  float hw, const math::Vec4& c) {
    thick_.push_back(self.x);  thick_.push_back(self.y);  thick_.push_back(self.z);
    thick_.push_back(other.x); thick_.push_back(other.y); thick_.push_back(other.z);
    thick_.push_back(side);    thick_.push_back(hw);
    thick_.push_back(c.x); thick_.push_back(c.y); thick_.push_back(c.z); thick_.push_back(c.w);
}

void VectorRenderer::seg(const math::Vec3& a, const math::Vec4& ca,
                         const math::Vec3& b, const math::Vec4& cb) {
    // Every line is a screen-space-expanded quad (two triangles), never a native
    // LineList primitive: the Vulkan/D3D backends don't honour non-triangle topology,
    // and routing through the quad path also gives identical pixel-width lines on every
    // backend. width 1.0 (the default) yields a crisp ~1 px line.
    // The vertex shader offsets each corner along the segment's screen normal by ±hw px;
    // the sign is mirrored at endpoint b (its "other" is a, so its computed normal
    // flips) to keep both edges parallel.
    const float hw = line_width_ * 0.5f;
    const math::Vec3& aL = a; const math::Vec3& bL = b;
    // rectangle corners A_left(a,+1) B_left(b,-1) B_right(b,+1) A_right(a,-1)
    thick_corner(aL, bL, +1.0f, hw, ca);  // tri 1: A_left, B_left, B_right
    thick_corner(bL, aL, -1.0f, hw, cb);
    thick_corner(bL, aL, +1.0f, hw, cb);
    thick_corner(aL, bL, +1.0f, hw, ca);  // tri 2: A_left, B_right, A_right
    thick_corner(bL, aL, +1.0f, hw, cb);
    thick_corner(aL, bL, -1.0f, hw, ca);
}

void VectorRenderer::line(const math::Vec3& a, const math::Vec3& b, const math::Vec4& color) {
    seg(a, color, b, color);
}

void VectorRenderer::line(const math::Vec3& a, const math::Vec4& ca,
                          const math::Vec3& b, const math::Vec4& cb) {
    seg(a, ca, b, cb);
}

void VectorRenderer::polyline(const math::Vec3* pts, int count, const math::Vec4& color, bool closed) {
    if (!pts || count < 2) return;
    for (int i = 0; i + 1 < count; ++i) line(pts[i], pts[i + 1], color);
    if (closed) line(pts[count - 1], pts[0], color);
}

void VectorRenderer::triangle(const math::Vec3& a, const math::Vec3& b, const math::Vec3& c,
                              const math::Vec4& color) {
    push(tris_, a, color); push(tris_, b, color); push(tris_, c, color);
}

void VectorRenderer::quad(const math::Vec3& a, const math::Vec3& b,
                          const math::Vec3& c, const math::Vec3& d, const math::Vec4& color) {
    triangle(a, b, c, color); triangle(a, c, d, color);
}

//--- Curves (camera-LOD tessellated) -----------------------------------------

void VectorRenderer::bezier(const math::Vec3& p0, const math::Vec3& c0,
                            const math::Vec3& c1, const math::Vec3& p1,
                            const math::Vec4& color, int segments) {
    if (segments <= 0) { const math::Vec3 hull[4] = { p0, c0, c1, p1 }; segments = lod_for_polyline(hull, 4); }
    math::Vec3 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        const float t = float(i) / segments, it = 1.0f - t;
        const math::Vec3 cur = p0 * (it*it*it) + c0 * (3*it*it*t) + c1 * (3*it*t*t) + p1 * (t*t*t);
        line(prev, cur, color);
        prev = cur;
    }
}

void VectorRenderer::quad_bezier(const math::Vec3& p0, const math::Vec3& c,
                                 const math::Vec3& p1, const math::Vec4& color, int segments) {
    if (segments <= 0) { const math::Vec3 hull[3] = { p0, c, p1 }; segments = lod_for_polyline(hull, 3); }
    math::Vec3 prev = p0;
    for (int i = 1; i <= segments; ++i) {
        const float t = float(i) / segments, it = 1.0f - t;
        const math::Vec3 cur = p0 * (it*it) + c * (2*it*t) + p1 * (t*t);
        line(prev, cur, color);
        prev = cur;
    }
}

// Uniform Catmull-Rom spline through pts[]. Each span i interpolates pts[i]→pts[i+1]
// with tangents from the neighbours; endpoints are duplicated (or wrapped if closed).
void VectorRenderer::catmull_rom(const math::Vec3* pts, int count, const math::Vec4& color,
                                 bool closed, int segments_per_span) {
    if (!pts || count < 2) return;
    const int spans = closed ? count : count - 1;
    auto at = [&](int i) -> const math::Vec3& {
        if (closed) { i %= count; if (i < 0) i += count; return pts[i]; }
        if (i < 0) i = 0; if (i > count - 1) i = count - 1; return pts[i];
    };
    for (int s = 0; s < spans; ++s) {
        const math::Vec3& P0 = at(s - 1);
        const math::Vec3& P1 = at(s);
        const math::Vec3& P2 = at(s + 1);
        const math::Vec3& P3 = at(s + 2);
        int seg = segments_per_span;
        if (seg <= 0) { const math::Vec3 hull[4] = { P0, P1, P2, P3 }; seg = lod_for_polyline(hull, 4); }
        math::Vec3 prev = P1;
        for (int i = 1; i <= seg; ++i) {
            const float t = float(i) / seg, t2 = t * t, t3 = t2 * t;
            const math::Vec3 cur = (P1 * 2.0f
                                  + (P2 - P0) * t
                                  + (P0 * 2.0f - P1 * 5.0f + P2 * 4.0f - P3) * t2
                                  + (P1 * 3.0f - P0 - P2 * 3.0f + P3) * t3) * 0.5f;
            line(prev, cur, color);
            prev = cur;
        }
    }
}

void VectorRenderer::arc(const math::Vec3& center, const math::Vec3& axis, float radius,
                         float start_rad, float end_rad, const math::Vec4& color, int segments) {
    math::Vec3 u, v;
    plane_basis(axis, &u, &v);
    const float sweep = end_rad - start_rad;
    if (segments <= 0) segments = lod_for_arc(center, u, v, radius, sweep);
    if (segments < 1) segments = 1;
    math::Vec3 prev = center + (u * std::cos(start_rad) + v * std::sin(start_rad)) * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = start_rad + sweep * (float(i) / segments);
        math::Vec3 cur = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        line(prev, cur, color);
        prev = cur;
    }
}

//--- 3D helpers --------------------------------------------------------------

void VectorRenderer::aabb(const math::Vec3& mn, const math::Vec3& mx, const math::Vec4& color) {
    const math::Vec3 c[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
    };
    static const int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0},   // bottom (z = mn.z)
        {4,5},{5,6},{6,7},{7,4},   // top    (z = mx.z)
        {0,4},{1,5},{2,6},{3,7},   // verticals
    };
    for (auto& ei : e) line(c[ei[0]], c[ei[1]], color);
}

void VectorRenderer::solid_aabb(const math::Vec3& mn, const math::Vec3& mx, const math::Vec4& color) {
    const math::Vec3 c[8] = {
        { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mx.y, mn.z }, { mn.x, mx.y, mn.z },
        { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z },
    };
    // 6 faces, CCW seen from outside; quad() expands each to two triangles.
    quad(c[0], c[3], c[2], c[1], color);   // -Z
    quad(c[4], c[5], c[6], c[7], color);   // +Z
    quad(c[0], c[1], c[5], c[4], color);   // -Y
    quad(c[3], c[7], c[6], c[2], color);   // +Y
    quad(c[0], c[4], c[7], c[3], color);   // -X
    quad(c[1], c[2], c[6], c[5], color);   // +X
}

void VectorRenderer::ring(const math::Vec3& center, const math::Vec3& axis, float radius,
                          const math::Vec4& color, int segments) {
    math::Vec3 u, v;
    plane_basis(axis, &u, &v);   // unit vectors spanning the plane perpendicular to axis
    if (segments <= 0) segments = lod_for_arc(center, u, v, radius, TWO_PI);
    if (segments < 3) segments = 3;
    math::Vec3 prev = center + u * radius;
    for (int i = 1; i <= segments; ++i) {
        const float a = (float(i) / segments) * TWO_PI;
        math::Vec3 cur = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        line(prev, cur, color);
        prev = cur;
    }
}

void VectorRenderer::sphere(const math::Vec3& center, float radius, const math::Vec4& color, int segments) {
    ring(center, math::Vec3::unit_x(), radius, color, segments);
    ring(center, math::Vec3::unit_y(), radius, color, segments);
    ring(center, math::Vec3::unit_z(), radius, color, segments);
}

void VectorRenderer::grid(const math::Vec3& center, const math::Vec3& right, const math::Vec3& forward,
                          float extent, int divisions, const math::Vec4& color) {
    if (divisions < 1) divisions = 1;
    math::Vec3 r = math::normalize(right);
    math::Vec3 f = math::normalize(forward);
    const float half = extent * 0.5f;
    const float step = extent / divisions;
    for (int i = 0; i <= divisions; ++i) {
        const float o = -half + step * i;
        // line parallel to `forward`, offset along `right`
        line(center + r * o + f * (-half), center + r * o + f * (half), color);
        // line parallel to `right`, offset along `forward`
        line(center + f * o + r * (-half), center + f * o + r * (half), color);
    }
}

void VectorRenderer::axes(const math::Vec3& origin, float scale) {
    line(origin, origin + math::Vec3::unit_x() * scale, { 1, 0, 0, 1 });
    line(origin, origin + math::Vec3::unit_y() * scale, { 0, 1, 0, 1 });
    line(origin, origin + math::Vec3::unit_z() * scale, { 0, 0, 1, 1 });
}

void VectorRenderer::axes(const float m[16], float scale) {
    // Column-major: translation in m[12..14]; basis columns are (m0..2), (m4..6), (m8..10).
    const math::Vec3 o = { m[12], m[13], m[14] };
    const math::Vec3 x = { m[0], m[1], m[2] };
    const math::Vec3 y = { m[4], m[5], m[6] };
    const math::Vec3 z = { m[8], m[9], m[10] };
    line(o, o + x * scale, { 1, 0, 0, 1 });
    line(o, o + y * scale, { 0, 1, 0, 1 });
    line(o, o + z * scale, { 0, 0, 1, 1 });
}

//--- 2D convenience ----------------------------------------------------------

void VectorRenderer::line2d(float x0, float y0, float x1, float y1, const math::Vec4& color, float z) {
    line({ x0, y0, z }, { x1, y1, z }, color);
}

void VectorRenderer::rect(float x, float y, float w, float h, const math::Vec4& color, float z) {
    const math::Vec3 p[4] = { { x, y, z }, { x + w, y, z }, { x + w, y + h, z }, { x, y + h, z } };
    polyline(p, 4, color, true);
}

void VectorRenderer::fill_rect(float x, float y, float w, float h, const math::Vec4& color, float z) {
    quad({ x, y, z }, { x + w, y, z }, { x + w, y + h, z }, { x, y + h, z }, color);
}

void VectorRenderer::circle2d(float cx, float cy, float r, const math::Vec4& color, int segments, float z) {
    if (segments <= 0)
        segments = lod_for_arc({ cx, cy, z }, math::Vec3::unit_x(), math::Vec3::unit_y(), r, TWO_PI);
    if (segments < 3) segments = 3;
    math::Vec3 prev = { cx + r, cy, z };
    for (int i = 1; i <= segments; ++i) {
        const float a = (float(i) / segments) * TWO_PI;
        math::Vec3 cur = { cx + std::cos(a) * r, cy + std::sin(a) * r, z };
        line(prev, cur, color);
        prev = cur;
    }
}

void VectorRenderer::fill_circle(float cx, float cy, float r, const math::Vec4& color, int segments, float z) {
    if (segments <= 0)
        segments = lod_for_arc({ cx, cy, z }, math::Vec3::unit_x(), math::Vec3::unit_y(), r, TWO_PI);
    if (segments < 3) segments = 3;
    const math::Vec3 center = { cx, cy, z };
    for (int i = 0; i < segments; ++i) {
        const float a0 = (float(i)     / segments) * TWO_PI;
        const float a1 = (float(i + 1) / segments) * TWO_PI;
        triangle(center,
                 { cx + std::cos(a0) * r, cy + std::sin(a0) * r, z },
                 { cx + std::cos(a1) * r, cy + std::sin(a1) * r, z }, color);
    }
}

//--- Flush -------------------------------------------------------------------

void VectorRenderer::end(GraphicCommander* cmd) {
    if (!device_ || !cmd) return;
    if (tris_.empty() && thick_.empty()) return;

    // One buffer holds both streams back to back: solid-fill triangles (7-float layout),
    // then line quads (12-float layout). Each stream is bound at its own byte offset and
    // drawn from first-vertex 0 (its stride comes from the bound pipeline).
    const uint32_t tri_floats   = uint32_t(tris_.size());
    const uint32_t thick_floats = uint32_t(thick_.size());
    scratch_.clear();
    scratch_.reserve(tri_floats + thick_floats);
    scratch_.insert(scratch_.end(), tris_.begin(),  tris_.end());
    scratch_.insert(scratch_.end(), thick_.begin(), thick_.end());

    // VBO ring: append this batch at its own byte offset (so an earlier batch this frame is not
    // overwritten on deferred backends); wrap when full, grow if one batch exceeds the capacity.
    const uint32_t bytes = uint32_t(scratch_.size() * sizeof(float));
    const uint32_t need  = (bytes + 15u) & ~15u;
    if (!vbo_.valid() || need > vbo_capacity_) {
        if (vbo_.valid()) device_->destroy_buffer(vbo_);
        uint32_t cap = vbo_capacity_ ? vbo_capacity_ : 64u * 1024u;
        while (cap < need) cap *= 2;
        BufferDesc bd; bd.size = cap; bd.type = BufferType::Vertex; bd.usage = ResourceUsage::Dynamic;
        vbo_ = device_->create_buffer(bd); vbo_capacity_ = cap; vbo_off_ = 0;
    } else if (vbo_off_ + need > vbo_capacity_) {
        vbo_off_ = 0;   // wrap (prior frames' batches are done via per-frame present sync)
    }
    const uint32_t vbo_base = vbo_off_;
    device_->update_buffer(vbo_, scratch_.data(), bytes, vbo_base);
    vbo_off_ += need;

    // UBO ring: a fresh 256-aligned slot per batch.
    Uniforms u;
    std::memcpy(u.view_proj, view_proj_, sizeof(u.view_proj));
    u.viewport[0] = float(viewport_w_); u.viewport[1] = float(viewport_h_);
    u.viewport[2] = 0.0f;               u.viewport[3] = 0.0f;
    const uint32_t slot = ubo_slot_;
    ubo_slot_ = (ubo_slot_ + 1) % kUboSlots;
    device_->update_buffer(ubo_[slot], &u, sizeof(u), 0);

    const ScissorRect full{ 0, 0, viewport_w_, viewport_h_ };
    const uint32_t tri_verts      = tri_floats   / FLOATS_PER_VERT;
    const uint32_t thick_verts    = thick_floats / THICK_FLOATS_PER_VERT;
    const uint32_t thick_byte_off = tri_floats * sizeof(float);

    auto bind_ubo = [&]() { cmd->bind_uniform_buffer(0, ubo_[slot], 0, sizeof(Uniforms)); };

    // Fills first, then line quads — wireframe reads on top of fills.
    if (tri_verts > 0) {
        cmd->set_pipeline(tri_pipeline_);
        bind_ubo();
        cmd->bind_vertex_buffer(0, vbo_, vbo_base);
        cmd->set_scissor(full);
        cmd->draw(tri_verts, 0);
    }
    if (thick_verts > 0) {
        cmd->set_pipeline(thick_pipeline_);
        bind_ubo();
        cmd->bind_vertex_buffer(0, vbo_, vbo_base + thick_byte_off);   // 12-float stream at its byte offset
        cmd->set_scissor(full);
        cmd->draw(thick_verts, 0);
    }
}

} // namespace gfx
} // namespace window
