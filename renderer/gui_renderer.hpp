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

#include <string>
#include <unordered_map>
#include <vector>

namespace window {
namespace gui {

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
    // 16-float column-major orthographic matrix mapping UI pixels → clip space.
    // fb_w/fb_h are framebuffer pixels (for scissor); `scale` is UI→pixel.
    void render(GraphicCommander* cmd, WidgetRenderInfo& info,
                TextureHandle atlas, const float proj[16],
                int fb_w, int fb_h, float scale = 1.0f);

private:
    void emit_quad(float x, float y, float w, float h,
                   float u0, float v0, float u1, float v1,
                   float layer, const math::Vec4& c);
    void emit_circle(float cx, float cy, float radius, const math::Vec4& c);
    static constexpr int kRoundRectCornerSegs = 4;                  // arc segments per corner
    static constexpr int kRoundRectVerts = 18 + 4 * kRoundRectCornerSegs * 3;  // 3 bands + 4 arcs
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
    ShaderHandle        vs_, fs_, fs_image_;
    PipelineHandle      pipeline_;        // solid + glyph (sampler2DArray atlas)
    PipelineHandle      image_pipeline_;  // RGBA images (sampler2D)
    BufferHandle        vbo_;
    uint32_t            vbo_capacity_ = 0;  // bytes
    uint32_t            vbo_off_ = 0;       // ring write cursor (bytes)
    std::vector<float>  verts_;
    std::unordered_map<std::string, TextureHandle> tex_cache_;

    // API-agnostic binding: one descriptor set per draw holds the projection UBO (binding 0),
    // the texture (binding 1) and the sampler (binding 2). The RHI emulates descriptor sets as
    // slot binds on GL/D3D11, so there is no per-backend path here. OpenGL still needs the
    // scissor Y flipped (bottom-left origin), which is the only remaining backend check.
    bool flip_scissor_y() const { return backend_ == Backend::OpenGL; }
    // Projection UBO ring: each render() call writes the slot at ubo_slot_ and advances, so several
    // GUI passes per frame don't overwrite each other's projection before the GPU draws (deferred
    // backends). Separate buffers (not offsets into one) so each gets a full update -- D3D11 can't
    // partial-update a constant buffer.
    static const uint32_t     kUboSlots = 64;
    BufferHandle              proj_ubo_[kUboSlots];   // 16-float projection per slot
    uint32_t                  ubo_slot_ = 0;
    TextureHandle             dummy_atlas_;   // 1x1 atlas so solid-only draws still bind a set
    SamplerHandle             sampler_;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
    std::unordered_map<uint64_t, DescriptorSetHandle> desc_sets_;  // (ubo_slot<<32 | texture id) → bind group
};

} // namespace gui
} // namespace window
