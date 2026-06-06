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

#include <vector>

namespace window {
namespace gui {

class GpuGuiRenderer {
public:
    bool init(GraphicDevice* device);
    void shutdown();

    // The rasterizer turns TextCmd strings into atlas glyph quads during flatten().
    void set_text_rasterizer(IGuiTextRasterizer* r) { rasterizer_ = r; }

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

    GraphicDevice*      device_ = nullptr;
    IGuiTextRasterizer* rasterizer_ = nullptr;
    ShaderHandle        vs_, fs_;
    PipelineHandle      pipeline_;
    BufferHandle        vbo_;
    uint32_t            vbo_capacity_ = 0;  // bytes
    std::vector<float>  verts_;
};

} // namespace gui
} // namespace window
