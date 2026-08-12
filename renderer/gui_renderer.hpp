#pragma once
// GpuGuiRenderer — draws the GUI's flattened WidgetRenderInfo through the
// backend-neutral render abstraction (GraphicDevice / GraphicCommander), instead
// of raw GL. Text + 9-slice are flattened to Color + Texture quads via a text
// rasterizer, so this only deals with two primitives.
//
// Vertex format: pos.xy (2) | uvw (3, z = glyph atlas layer; < 0 = solid color) |
// rgba (4). The fragment samples an R8 sampler2DArray atlas as an alpha mask for
// glyphs, or emits the solid color. Reusable across backends; shaders are GLSL
// for the OpenGL backend today (other backends add their own shader source).

#include "../graphics_api.hpp"
#include "../gui/gui.hpp"
#include "stencil_clip.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace window {
namespace gfx { class VectorRenderer; }   // renderer/vector_renderer.hpp
namespace gui {

class IGuiContext;        // gui/gui_context.hpp
class GpuTextRasterizer;  // renderer/gui_text_rasterizer.hpp

// Resolves a GUI image command (file path / in-memory blob / image name) to a GPU
// texture for sampling. UGW ships no image decoder, so the consumer implements this
// (decode however it likes, upload via the GraphicDevice, return the handle). The
// renderer caches the result per source, so resolve() runs once per unique image;
// the provider owns the returned textures' lifetime.
class IGuiTextureProvider {
public:
    virtual ~IGuiTextureProvider() = default;
    virtual TextureHandle resolve(const WidgetRenderInfo::TextureCmd& cmd) = 0;
};

class GpuGuiRenderer {
public:
    bool init(GraphicDevice* device);
    void shutdown();

    // The rasterizer turns TextCmd strings into atlas glyph quads during flatten().
    void set_text_rasterizer(IGuiTextRasterizer* r) { rasterizer_ = r; }

    // Optional: resolves image textures (atlas_layer < 0). Without one, image quads
    // are skipped. clear_texture_cache() drops cached handles (call when the provider
    // invalidates textures); it does not destroy them (the provider owns them).
    void set_texture_provider(IGuiTextureProvider* p) { tex_provider_ = p; }
    void clear_texture_cache() { tex_cache_.clear(); }

    // Render `info` to the currently-bound target via `cmd`. `atlas` is the glyph
    // texture array (pass an invalid handle when there is no text). `proj` is a
    // 16-float column-major orthographic matrix mapping the draw commands' own
    // coordinates → clip space. fb_w/fb_h are framebuffer pixels.
    // `color_atlas` is the RGBA colour-emoji glyph atlas (GpuTextRasterizer::color_atlas());
    // pass an invalid handle when there are no colour glyphs (a 1x1 dummy is bound instead).
    //
    // Clipping is done with stencil masks (see stencil_clip.hpp), so the target must have a
    // stencil buffer cleared to 0 — render_window_frame() arranges that; other callers use
    // depth_stencil_target() below. Against a target without one, clipped content draws
    // unclipped rather than vanishing.
    //
    // There is no UI-scale argument: it existed only to convert clip boxes into physical
    // scissor rectangles. A stencil mask is transformed by `proj` exactly like the content
    // it clips, so it is already in the right space whatever that space is — which is also
    // why the clip now follows a zoom/pan projection instead of ignoring it.
    void render(GraphicCommander* cmd, WidgetRenderInfo& info,
                TextureHandle atlas, const float proj[16],
                int fb_w, int fb_h, TextureHandle color_atlas = {});

    // The shared stencil clipper, so other renderers drawing into the same stencil
    // buffer this frame (e.g. the vector underlay) allocate references from it too and
    // cannot collide. Valid only after init().
    gfx::StencilClipper& clipper() { return clipper_; }

    // The depth-stencil target render() needs, sized to the framebuffer. Callers that
    // bind the backbuffer themselves (rather than using render_window_frame) must pass
    // this to set_render_target_backbuffer() and clear the stencil to 0 once per frame:
    //
    //   cmd->set_render_target_backbuffer(r.depth_stencil_target(w, h));
    //   cmd->clear_depth_stencil({ 1.0f, 0 });
    //   r.begin_frame();
    //
    // Returns an invalid handle on OpenGL, whose default framebuffer already owns a
    // stencil — passing it through is still correct there (the backend ignores it).
    RenderTargetHandle depth_stencil_target(int fb_w, int fb_h) { return clipper_.depth_target(fb_w, fb_h); }
    // Resets stencil-reference allocation; pair it with the per-frame stencil clear above.
    void begin_frame() { clipper_.begin_frame(); }

    // One COMPLETE window frame through the abstraction, so app frame code carries
    // no commander/viewport/clear plumbing: begin → backbuffer viewport/clear →
    // optional vector underlay (begun here when the caller didn't pre-fill it;
    // every visible IGuiCanvasView in `ctx` emits its backdrop/grid/wires into
    // it; end() draws it) → optional immediate layer (finalized + flattened
    // here) → optional retained-widget pass (ctx->begin_frame(dt) →
    // get_render_info) → optional overlay immediate layer (drawn LAST, above
    // the retained widgets — for popups/menus that must sit on top of retained
    // content) → end → submit. The glyph atlas syncs AFTER every layer is
    // collected so glyphs rasterized this frame upload before the draw.
    // Projection = UI pixels, origin top-left.
    void render_window_frame(Graphics* gfx, GraphicCommander* cmd, GpuTextRasterizer* raster,
                             int fb_w, int fb_h, const ClearColor& clear,
                             WidgetRenderInfo* immediate, IGuiContext* ctx, float dt,
                             window::gfx::VectorRenderer* underlay = nullptr,
                             WidgetRenderInfo* overlay = nullptr);

private:
    // One vertex (13 floats): pos2 | uvw3 | rgba4 | sdf4.
    void push_vert(float px, float py, float u, float vv, float layer,
                   const math::Vec4& c, float s0, float s1, float s2, float s3);
    void emit_quad(float x, float y, float w, float h,
                   float u0, float v0, float u1, float v1,
                   float layer, const math::Vec4& c);
    // Circle / rounded rect / ring all render as ONE SDF quad (6 verts) — no
    // tessellation, smooth at any scale, AA free (see gui.hlsl).
    static constexpr int kSdfBoxVerts = 6;
    void emit_sdf_box(float cx, float cy, float hw, float hh, float radius, float border, const math::Vec4& c);
    void emit_circle(float cx, float cy, float radius, const math::Vec4& c);
    void emit_round_rect(float x, float y, float w, float h, float radius, const math::Vec4& c);
    void emit_line(float x0, float y0, float x1, float y1, float width, const math::Vec4& c);
    TextureHandle resolve_texture(const WidgetRenderInfo::TextureCmd& t);

    // Per-draw bind group {projection UBO slot, texture, sampler}, cached by (UBO slot, texture id)
    // so each (slot,texture) pair is built once and reused. The slot is passed in because the
    // projection UBO is a ring (see proj_ubo_) -- multiple render() calls per frame must each point
    // their sets at the slot they wrote, or a later call clobbers an earlier one on Vulkan/D3D12.
    DescriptorSetHandle desc_set_for(TextureHandle tex, uint32_t ubo_slot);

    GraphicDevice*      device_ = nullptr;
    Backend             backend_ = Backend::OpenGL;
    IGuiTextRasterizer* rasterizer_ = nullptr;
    IGuiTextureProvider* tex_provider_ = nullptr;
    ShaderHandle        vs_, fs_, fs_image_, fs_clip_;
    // Both content pipelines are stencil-tested; a draw selects its clip by setting the
    // stencil reference, and content with no clip of its own gets a full-framebuffer
    // mask rather than a second, stencil-free pipeline (see init() for why).
    PipelineHandle      pipeline_;             // solid + glyph (sampler2DArray atlas)
    PipelineHandle      image_pipeline_;       // RGBA images (sampler2D)
    gfx::StencilClipper clipper_;
    BufferHandle        vbo_;
    uint32_t            vbo_capacity_ = 0;  // bytes
    uint32_t            vbo_off_ = 0;       // ring write cursor (bytes)
    std::vector<float>  verts_;
    std::unordered_map<std::string, TextureHandle> tex_cache_;

    // API-agnostic binding: one descriptor set per draw holds the projection UBO (binding 0),
    // the texture (binding 1) and the sampler (binding 2). The RHI emulates descriptor sets as
    // slot binds on GL/D3D11, so there is no per-backend path here. Clipping is stencil-based,
    // and the stencil buffer shares the render target's orientation, so the Y flip the scissor
    // path needed on OpenGL (bottom-left origin) is gone with it.
    // Projection UBO ring: each render() call writes the slot at ubo_slot_ and advances, so several
    // GUI passes per frame don't overwrite each other's projection before the GPU draws (deferred
    // backends). Separate buffers (not offsets into one) so each gets a full update -- D3D11 can't
    // partial-update a constant buffer.
    static const uint32_t     kUboSlots = 64;
    BufferHandle              proj_ubo_[kUboSlots];   // 16-float projection per slot
    uint32_t                  ubo_slot_ = 0;
    TextureHandle             dummy_atlas_;        // 1x1 R8 atlas so solid-only draws bind a set
    TextureHandle             dummy_color_atlas_;  // 1x1 RGBA so the colour binding is always complete
    TextureHandle             cur_color_atlas_;    // colour atlas bound this render() (or the dummy)
    SamplerHandle             sampler_;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
    std::unordered_map<uint64_t, DescriptorSetHandle> desc_sets_;  // (ubo_slot<<32 | texture id) → bind group
};

} // namespace gui
} // namespace window
