#pragma once
// GpuTextRasterizer — IGuiTextRasterizer + ITextMeasurer backed by gui/font/
// (FreeType) with a SHARED texture-array glyph atlas. Every glyph across all text
// packs into the one atlas, so GpuGuiRenderer binds atlas() once and batches all
// glyph quads into few draws (rather than one draw per string). Atlas GPU storage
// is a GraphicDevice array texture; call sync_atlas() each frame before rendering
// to upload newly-rasterised glyphs.

#include "../graphics_api.hpp"
#include "../gui/gui.hpp"            // IGuiTextRasterizer, ITextMeasurer
#include "../gui/font/font.hpp"

#include <vector>
#include <unordered_map>

namespace window {
namespace gui {

class GpuTextRasterizer : public IGuiTextRasterizer, public ITextMeasurer {
public:
    // Colour-emoji glyph quads flag themselves by adding this to their atlas layer,
    // so the GUI shader knows to sample the RGBA colour atlas (and the real layer is
    // (atlas_layer - kColorLayerBase)). Well above the R8 atlas's max layer count.
    static constexpr int kColorLayerBase = 4096;

    bool init(GraphicDevice* device, const char* font_family = nullptr, float default_px = 14.0f);
    void shutdown();

    // Upload newly-rasterised glyph layers; returns the (R8 SDF) atlas texture. Also
    // syncs the RGBA colour-emoji atlas — bind color_atlas() alongside it.
    TextureHandle sync_atlas();
    TextureHandle atlas() const { return atlas_; }
    TextureHandle color_atlas() const { return color_atlas_; }   // RGBA emoji atlas (may be invalid)

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
    // Upload one atlas manager's dirty layers into a GPU array texture.
    void sync_layers(font::IGlyphAtlasManager* mgr, TextureHandle& tex, int& layers, TextureFormat fmt);

    GraphicDevice*            device_  = nullptr;
    font::IFontLibrary*       lib_     = nullptr;
    font::IGlyphAtlasManager* mgr_     = nullptr;   // R8 SDF coverage atlas (text)
    font::IGlyphAtlasManager* mgr_color_ = nullptr; // RGBA colour atlas (emoji)
    font::ITextShaper*        shaper_  = nullptr;
    font::IFontFace*          font_    = nullptr;
    TextureHandle             atlas_;
    TextureHandle             color_atlas_;
    int                       atlas_layers_ = 0;
    int                       color_atlas_layers_ = 0;
    // Maps a coverage-atlas font index (shaper output) → the colour atlas's mirrored
    // index for the same face, so colour glyphs resolve to the right emoji face.
    std::unordered_map<int,int> color_font_idx_;
    long long                 start_ms_ = 0;
};

} // namespace gui
} // namespace window
