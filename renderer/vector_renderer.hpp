#pragma once
// VectorRenderer — an immediate-mode line/triangle batch renderer over the
// backend-neutral graphics abstraction (GraphicDevice / GraphicCommander). It is
// NOT tied to the GUI: it takes a full 4x4 view-projection matrix, so the same
// object draws 2D overlays (feed it an orthographic matrix) and 3D geometry —
// debug gizmos, wireframes, grids, axes, bounding boxes — under a perspective
// camera.
//
// Geometry is accumulated into two streams between begin()/end(): solid-fill triangles
// and line quads. Every line — thin or thick — is a screen-space-expanded quad (two
// triangles), never a native LineList primitive, because the Vulkan/D3D backends only
// honour triangle topology; this also makes a line keep a constant *pixel* width at any
// depth and look identical on every backend. The vertex shader projects both endpoints,
// widens the segment along the screen-space normal by the viewport size, and projects
// back, so every line/wireframe helper inherits set_line_width() with no CPU-side
// geometry math. end() uploads both streams into one dynamic vertex buffer (fills first,
// then line quads) and replays them.
//
// The view-projection matrix and the viewport size travel together in one uniform
// buffer, bound the same way on every backend — a descriptor set on Vulkan/D3D12, a
// slot bind on OpenGL/D3D11 — so the renderer runs cross-API unchanged. Shaders ship
// prebuilt per backend: inline GLSL (OpenGL), embedded SPIR-V (Vulkan, glslc) and
// embedded DXBC (D3D11/D3D12, fxc). Metal is a follow-up (init() returns false).
//
// Vertex format (thin/fill): pos.xyz (3) | rgba (4). Solid colour, no texturing.

#include "../graphics_api.hpp"
#include "../math_util.hpp"

#include <vector>

namespace window {
namespace gfx {

struct VectorRendererDesc {
    // Depth state. Defaults suit 3D (occlude correctly). For a 2D/GUI overlay drawn
    // on top of a scene, set both false so the vectors are never depth-clipped.
    bool       depth_test  = true;
    bool       depth_write  = true;
    CompareFunc depth_func  = CompareFunc::LessEqual;
    BlendState blend        = BlendState::alpha_blend();
    // Pipeline target formats — must match the render pass the renderer draws into.
    TextureFormat color_format = TextureFormat::RGBA8_UNORM;
    TextureFormat depth_format  = TextureFormat::D24_UNORM_S8_UINT;
    int           samples       = 1;

    // Curve level-of-detail. Curves (and shapes with segments == 0) are tessellated
    // to roughly `curve_tolerance_px` screen pixels per segment, measured against the
    // current view-projection + viewport — so a curve gets more segments as the camera
    // moves closer and fewer as it recedes. Clamped to [min_,max_curve_segments].
    float curve_tolerance_px = 8.0f;
    int   min_curve_segments = 4;
    int   max_curve_segments = 256;
};

class VectorRenderer {
public:
    bool init(GraphicDevice* device, const VectorRendererDesc& desc = {});
    void shutdown();

    // Start a batch. `view_proj` is a 16-float column-major matrix mapping world
    // (or UI) space → clip space; `viewport_w/h` are the pixel dimensions the camera
    // renders into (used both for curve LOD and the flush-time scissor). Clears any
    // geometry from the previous frame.
    void begin(const float view_proj[16], int viewport_w, int viewport_h);

    // Upload the batched geometry and draw it (lines then triangles) into the
    // currently-bound render target. A full-viewport scissor is set so Vulkan/D3D12
    // dynamic-scissor draws aren't clipped away.
    void end(GraphicCommander* cmd);

    // Override the curve LOD parameters set at init (see VectorRendererDesc).
    void set_curve_lod(float tolerance_px, int min_segments, int max_segments);

    // Stroke width in pixels for every line / wireframe helper below. 1.0 (default)
    // emits native 1 px lines; > 1 routes through the screen-space thick-line path,
    // so the same scene code gets fat strokes just by raising this.
    void  set_line_width(float px) { line_width_ = px < 1.0f ? 1.0f : px; }
    float line_width() const { return line_width_; }

    bool empty() const { return tris_.empty() && thick_.empty(); }

    //--- Core primitives (world-space Vec3) ----------------------------------
    void line(const math::Vec3& a, const math::Vec3& b, const math::Vec4& color);
    void line(const math::Vec3& a, const math::Vec4& ca,
              const math::Vec3& b, const math::Vec4& cb);            // per-endpoint colour
    void polyline(const math::Vec3* pts, int count, const math::Vec4& color, bool closed = false);
    void triangle(const math::Vec3& a, const math::Vec3& b, const math::Vec3& c, const math::Vec4& color);
    void quad(const math::Vec3& a, const math::Vec3& b,
              const math::Vec3& c, const math::Vec3& d, const math::Vec4& color);  // filled, a-b-c-d CCW

    //--- Curves (camera-LOD tessellated; segments == 0 → auto, else forced) --
    void bezier(const math::Vec3& p0, const math::Vec3& c0,
                const math::Vec3& c1, const math::Vec3& p1, const math::Vec4& color, int segments = 0);  // cubic
    void quad_bezier(const math::Vec3& p0, const math::Vec3& c,
                     const math::Vec3& p1, const math::Vec4& color, int segments = 0);                    // quadratic
    void catmull_rom(const math::Vec3* pts, int count, const math::Vec4& color,
                     bool closed = false, int segments_per_span = 0);                                     // spline through pts
    void arc(const math::Vec3& center, const math::Vec3& axis, float radius,
             float start_rad, float end_rad, const math::Vec4& color, int segments = 0);

    //--- 3D helpers (segments == 0 → camera-LOD) -----------------------------
    void aabb(const math::Vec3& mn, const math::Vec3& mx, const math::Vec4& color);        // wireframe
    void solid_aabb(const math::Vec3& mn, const math::Vec3& mx, const math::Vec4& color);  // filled
    void ring(const math::Vec3& center, const math::Vec3& axis, float radius,
              const math::Vec4& color, int segments = 0);                                   // wire circle in a plane
    void sphere(const math::Vec3& center, float radius, const math::Vec4& color, int segments = 0);  // 3 rings
    void grid(const math::Vec3& center, const math::Vec3& right, const math::Vec3& forward,
              float extent, int divisions, const math::Vec4& color);
    void axes(const math::Vec3& origin, float scale);   // X=red, Y=green, Z=blue
    void axes(const float model[16], float scale);      // oriented by a 4x4 model matrix

    //--- 2D convenience (a constant-z plane; default z = 0) ------------------
    void line2d(float x0, float y0, float x1, float y1, const math::Vec4& color, float z = 0.0f);
    void rect(float x, float y, float w, float h, const math::Vec4& color, float z = 0.0f);       // wire
    void fill_rect(float x, float y, float w, float h, const math::Vec4& color, float z = 0.0f);
    void circle2d(float cx, float cy, float r, const math::Vec4& color, int segments = 0, float z = 0.0f);  // wire
    void fill_circle(float cx, float cy, float r, const math::Vec4& color, int segments = 0, float z = 0.0f);

private:
    void push(std::vector<float>& dst, const math::Vec3& p, const math::Vec4& c);
    // One line segment: expanded into two screen-space quad triangles in thick_, widened
    // by line_width_ pixels in the vertex shader.
    void seg(const math::Vec3& a, const math::Vec4& ca, const math::Vec3& b, const math::Vec4& cb);
    void thick_corner(const math::Vec3& self, const math::Vec3& other, float side, float hw,
                      const math::Vec4& c);
    // Vulkan/D3D12 bind the UBO through a descriptor set; OpenGL/D3D11 bind it by slot.
    bool uses_descriptor_sets() const { return backend_ == Backend::Vulkan || backend_ == Backend::D3D12; }

    // Camera-LOD: project a world point to viewport pixels (false = at/behind camera).
    bool project_px(const math::Vec3& world, float out_xy[2]) const;
    // Segment count for a curve whose control polygon is `ctrl[0..n-1]`.
    int  lod_for_polyline(const math::Vec3* ctrl, int n) const;
    // Segment count for an arc of `sweep` radians, radius `r`, plane basis u/v at center.
    int  lod_for_arc(const math::Vec3& center, const math::Vec3& u, const math::Vec3& v,
                     float r, float sweep) const;
    // Orthonormal basis (u,v) of the plane perpendicular to `axis`.
    static void plane_basis(const math::Vec3& axis, math::Vec3* u, math::Vec3* v);

    GraphicDevice*       device_ = nullptr;
    Backend              backend_ = Backend::OpenGL;
    ShaderHandle         vs_basic_, fs_color_, vs_thick_;
    PipelineHandle       tri_pipeline_, thick_pipeline_;   // solid fills, line quads

    // Per-frame constants (view-projection + viewport). Bound by slot (works on every backend
    // now) from a RING of small buffers so multiple begin()/end() batches per frame don't clobber
    // each other on deferred backends (Vulkan/D3D12). Separate buffers (not offsets into one) so
    // each gets a full update -- D3D11 can't partial-update a constant buffer. No explicit pipeline
    // layout: the backend reflects the binding and auto-builds the descriptor set per draw.
    static const uint32_t kUboSlots = 64;
    BufferHandle              ubo_[kUboSlots];   // each sizeof(Uniforms)
    uint32_t                  ubo_slot_ = 0;
    PipelineLayoutHandle      pipe_layout_;    // left invalid (auto layout from reflection)
    DescriptorSetLayoutHandle set_layout_;     // unused (kept for ABI)
    DescriptorSetHandle       desc_set_;       // unused (kept for ABI)

    BufferHandle         vbo_;
    uint32_t             vbo_capacity_ = 0;   // bytes
    uint32_t             vbo_off_ = 0;        // ring write cursor
    std::vector<float>   tris_, thick_, scratch_;
    float                view_proj_[16] = {};
    int                  viewport_w_ = 0, viewport_h_ = 0;
    float                line_width_ = 1.0f;
    float                lod_px_  = 8.0f;
    int                  min_seg_ = 4, max_seg_ = 256;
};

} // namespace gfx
} // namespace window
