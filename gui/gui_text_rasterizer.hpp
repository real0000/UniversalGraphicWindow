#pragma once
// GpuTextRasterizer — IGuiTextRasterizer + ITextMeasurer backed by gui/font/
// (FreeType) with a SHARED texture-array glyph atlas. Every glyph across all text
// packs into the one atlas, so GpuGuiRenderer binds atlas() once and batches all
// glyph quads into few draws (rather than one draw per string). Atlas GPU storage
// is a GraphicDevice array texture; call sync_atlas() each frame before rendering
// to upload newly-rasterised glyphs.

#include "../graphics_api.hpp"
#include "gui.hpp"            // IGuiTextRasterizer, ITextMeasurer
#include "font/font.hpp"

#include <vector>

namespace window {
namespace gui {

class GpuTextRasterizer : public IGuiTextRasterizer, public ITextMeasurer {
public:
    bool init(GraphicDevice* device, const char* font_family = nullptr, float default_px = 14.0f);
    void shutdown();

    // Upload newly-rasterised glyph layers; returns the atlas texture to bind
    // before drawing text (invalid handle until the first glyph is packed).
    TextureHandle sync_atlas();
    TextureHandle atlas() const { return atlas_; }

    // ITextMeasurer
    math::Vec2 measure_text(const char* text, float font_size, const char* font_name = nullptr) override;
    float      get_line_height(float font_size, const char* font_name = nullptr) override;

    // IGuiTextRasterizer
    TextQuad rasterize(const char*, float, const char*) override { return TextQuad{}; }
    bool     rasterize_glyphs(const char* text, float font_size, const char* font_name,
                              std::vector<GlyphQuad>& out_quads, float* out_w, float* out_h) override;
    float    measure_advance(const char* text, int n, float font_size, const char* font_name = nullptr) override;
    float    get_time() const override;

private:
    void shape(const char* text, float size, std::vector<GlyphQuad>& out, float& out_w, float& out_h);

    GraphicDevice*            device_  = nullptr;
    font::IFontLibrary*       lib_     = nullptr;
    font::IGlyphAtlasManager* mgr_     = nullptr;
    font::ITextShaper*        shaper_  = nullptr;
    font::IFontFace*          font_    = nullptr;
    TextureHandle             atlas_;
    int                       atlas_layers_ = 0;
    long long                 start_ms_ = 0;
};

} // namespace gui
} // namespace window
