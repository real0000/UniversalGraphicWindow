#pragma once
// stencil_clip — stencil-buffer clipping for the GUI and vector renderers.
//
// Clipping used to be a scissor rect (and, before that, a viewport rect). Both are
// axis-aligned integer rectangles baked into fixed-function state, so neither can
// express a clip that is rotated, rounded, skewed or scaled off-axis. This replaces
// them with a stencil mask: the clip is drawn as geometry, and the stencil test
// decides which pixels the content may touch.
//
//   1. Draw the clip's shape into the stencil buffer, colour writes off, stamping a
//      reference value (StencilOp::Replace).
//   2. Draw the content with a stencil test of Equal(that value).
//
// Because step 1 is a real draw, the shape is whatever geometry we emit. ClipShape
// carries a rounded box in LOCAL space plus a 2x3 affine that places it on screen:
// the shader evaluates the rounded-box SDF in local space while the vertex shader
// applies the transform, so rotation/scale/skew cost nothing and stay exact at any
// angle (see ps_clip_mask in renderer/shaders/gui.hlsl).
//
// Usage is two-phase, matching how both renderers already work — all geometry is
// built and uploaded first, then the draws are replayed in order:
//
//   clipper.begin_pass(proj, fb_w, fb_h);        // reset per-pass geometry
//   uint32_t m = clipper.add(shape);             // CPU: append the mask quad
//   ...                                           // build content geometry
//   clipper.upload();                             // one buffer update for the pass
//   clipper.draw_mask(cmd, m);                    // record: stamp the stencil
//   cmd->set_pipeline(content_pipeline_with_stencil_test);
//   cmd->set_stencil_reference(clipper.ref_of(m));
//   cmd->draw(...);                               // clipped to the shape
//
// The clipper is self-contained (own mask pipeline, projection UBO ring, descriptor
// sets and vertex buffer) so one instance can be shared by every renderer drawing
// into the same stencil buffer; sharing keeps their reference values from colliding.

#include "../graphics_api.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

// A clip region: a rounded box in local space, positioned by a 2x3 affine transform.
//
// Local space is centred on the origin and spans [-half_w, half_w] x [-half_h, half_h];
// `corner_radius` rounds the corners (clamped to half the smaller side). The affine maps
// local → screen pixels:  screen.x = a*lx + c*ly + tx,  screen.y = b*lx + d*ly + ty.
//
// The identity transform with half-extents from a rect reproduces exactly what the old
// scissor rect did. Non-identity is the point of the exercise: `rotated()` gives a clip
// at any angle, which no scissor rect can represent.
struct ClipShape {
    float half_w = 0.0f, half_h = 0.0f;
    float corner_radius = 0.0f;
    float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;   // linear part, column-major
    float tx = 0.0f, ty = 0.0f;                      // translation (shape centre, screen px)

    // Axis-aligned rect given by its top-left corner and size — the drop-in for a
    // scissor rect. `radius` optionally rounds the corners.
    static ClipShape from_rect(float x, float y, float w, float h, float radius = 0.0f);
    // Rect centred at (cx, cy), rotated by `radians` in screen space.
    static ClipShape rotated(float cx, float cy, float w, float h, float radians, float radius = 0.0f);
    // The shape that covers the whole viewport, expressed in the space `proj` maps FROM.
    //
    // A mask is drawn through the same projection as the content it clips, so "everything"
    // is not the framebuffer rect — it is whatever region of the content's own space fills
    // clip space. That differs from the framebuffer rect whenever the projection carries a
    // DPI, zoom or pan factor, as the GUI editor's canvas projection does. Assumes the
    // axis-aligned orthographic form the GUI always uses; falls back to a large rect if
    // `proj` has no invertible x/y scale.
    static ClipShape covering(const float proj[16]);

    // A degenerate shape clips everything away; callers treat it as "draw nothing".
    bool is_degenerate() const { return half_w <= 0.0f || half_h <= 0.0f; }
    // True when the transform is a pure translation — the shape is then a plain
    // axis-aligned rect and its screen bounds are exact rather than conservative.
    bool is_axis_aligned() const { return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f; }

    // Screen-space axis-aligned bounds of the transformed shape (conservative when
    // rotated: the bounds of the rotated quad, corners included).
    void screen_bounds(float* out_x0, float* out_y0, float* out_x1, float* out_y1) const;
};

// Owns the mask pipeline, its geometry buffer and the stencil reference allocator.
class StencilClipper {
public:
    // `vs` is gui.hlsl's vs_main, `fs` its ps_clip_mask; the clipper does not own them.
    bool init(GraphicDevice* device, ShaderHandle vs, ShaderHandle fs);
    void shutdown();
    bool valid() const { return mask_pipeline_.valid(); }

    // The depth-stencil target to attach to the backbuffer, sized to the framebuffer
    // (recreated on resize). OpenGL's default framebuffer already owns a stencil and
    // takes no attachments, so this returns an invalid handle there and allocates nothing.
    RenderTargetHandle depth_target(int fb_w, int fb_h);
    // True when the stencil buffer this clipper stamps into needs an explicit
    // depth-stencil attachment (every backend except OpenGL).
    bool needs_depth_attachment() const { return backend_ != Backend::OpenGL; }

    // Call once per frame, after the stencil buffer has been cleared to 0: resets the
    // reference allocator so a frame's first clip starts from 1 again.
    void begin_frame();
    // Call at the start of each pass sharing the frame's stencil buffer. `proj` is the
    // SAME matrix the pass's content is drawn with — masks are transformed by it exactly
    // like the content, which is what lets a clip be expressed in the content's own
    // coordinates rather than in framebuffer pixels. References keep incrementing across
    // passes within a frame, so a mask from an earlier pass is never mistaken for this one's.
    void begin_pass(const float proj[16], int fb_w, int fb_h);

    // The shape covering this pass's whole viewport, in the space begin_pass()'s `proj`
    // maps from — what "no clip" means for content that still has to go through the
    // stencil-tested pipeline.
    const ClipShape& cover_shape() const { return cover_; }

    // Append `shape`'s mask geometry (CPU side) and reserve a stencil reference for it.
    // Returns a mask id for draw_mask()/ref_of(), or kNoMask when the shape is degenerate
    // (nothing can pass the clip) — callers should skip the content entirely.
    static const uint32_t kNoMask = 0xFFFFFFFFu;
    uint32_t add(const ClipShape& shape);

    // Upload every mask added since begin_pass(). Call once, after all geometry is built
    // and before any draw_mask(); writing to the buffer between recorded draws would race
    // on the deferred backends.
    void upload();

    // Record the stamp for `mask`: draws its shape into the stencil buffer with colour
    // writes off. Emits a reset quad first when the 8-bit reference space wrapped.
    void draw_mask(GraphicCommander* cmd, uint32_t mask);
    // The stencil reference the content of `mask` must test Equal against.
    uint8_t ref_of(uint32_t mask) const;

    // Depth-stencil state for content drawn under a mask: test Equal(ref), write nothing.
    static DepthStencilState content_state();

private:
    void push_quad(const ClipShape& shape);

    struct Mask {
        uint32_t first = 0, count = 0;   // vertex range of the mask quad
        uint8_t  ref = 0;                // value it stamps / content tests against
        uint32_t generation = 0;         // reference-space generation (see generation_)
    };

    GraphicDevice*     device_ = nullptr;
    Backend            backend_ = Backend::OpenGL;
    PipelineHandle     mask_pipeline_;
    BufferHandle       vbo_;
    uint32_t           vbo_capacity_ = 0;   // bytes
    uint32_t           vbo_off_ = 0;        // ring write cursor (bytes)
    uint32_t           vbo_base_ = 0;       // this pass's byte offset into the ring
    std::vector<float> verts_;
    std::vector<Mask>  masks_;
    uint32_t           next_ref_ = 1;       // 0 = "outside every clip"; 255 is the last usable
    // The 8-bit stencil holds 255 distinct references at a time. Each exhaustion starts a
    // new "generation": references restart at 1, and the buffer must be wiped back to 0
    // first or a stale value from the previous generation would pass a content test.
    // Tracking it as a generation rather than a flag on the mask that triggered the wrap
    // is deliberate — a mask whose segment turns out to have no content is never drawn,
    // and the wipe still has to happen before the next mask that IS drawn.
    uint32_t           generation_ = 0;         // generation the next add() allocates into
    uint32_t           drawn_generation_ = 0;   // generation the stencil buffer currently holds
    uint32_t           reset_first_ = 0, reset_count_ = 0;   // per-pass full-target wipe quad
    ClipShape          cover_;                                // this pass's "everything" shape
    RenderTargetHandle depth_target_;
    int                depth_w_ = 0, depth_h_ = 0;
    int                fb_w_ = 0, fb_h_ = 0;

    // Projection UBO ring + descriptor sets, mirroring the GUI renderer's: a fresh slot
    // per pass so several passes a frame don't clobber each other on deferred backends.
    static const uint32_t kUboSlots = 64;
    BufferHandle              proj_ubo_[kUboSlots];
    DescriptorSetHandle       desc_set_[kUboSlots];
    uint32_t                  ubo_slot_ = 0;
    uint32_t                  cur_slot_ = 0;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
};

} // namespace gfx
} // namespace window
