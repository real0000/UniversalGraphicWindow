#include "gui_text_rasterizer.hpp"

#include <chrono>
#include <cstring>
#include <set>
#include <string>

namespace window {
namespace gui {
namespace {
long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

bool GpuTextRasterizer::init(GraphicDevice* device, const char* family, float default_px) {
    device_ = device;
    start_ms_ = now_ms();
    if (!device_) return false;

    font::Result fr;
    lib_ = font::create_font_library(font::FontBackend::Auto, &fr);
    if (!lib_) return false;

    font::GlyphAtlasConfig acfg;
    acfg.layer_width = 2048; acfg.layer_height = 2048; acfg.max_layers = 256;
    acfg.sdf = true;            // text glyphs as signed distance fields (shader thresholds them)
    mgr_    = font::create_glyph_atlas_manager(lib_, acfg);

    // Separate RGBA atlas for colour-emoji glyphs (sampled directly by the shader).
    font::GlyphAtlasConfig ccfg;
    ccfg.layer_width = 2048; ccfg.layer_height = 2048; ccfg.max_layers = 16;
    ccfg.color = true;
    mgr_color_ = font::create_glyph_atlas_manager(lib_, ccfg);

    shaper_ = font::create_text_shaper(lib_, &fr);

    if (family) font_ = lib_->load_system_font(font::FontDescriptor::create(family, default_px), nullptr);
    if (!font_) font_ = lib_->load_system_font(font::FontDescriptor::create("DejaVu Sans", default_px), nullptr);
    if (!font_) font_ = lib_->load_system_font(font::FontDescriptor::create("Arial", default_px), nullptr);
    if (!font_) font_ = lib_->get_default_font(default_px, nullptr);
    if (!font_ || !mgr_ || !shaper_) return false;

    mgr_->add_font(font_, /*take_ownership=*/true);
    shaper_->set_fallback_chain(mgr_->fallback_chain());
    return true;
}

void GpuTextRasterizer::shutdown() {
    if (atlas_.valid() && device_)       device_->destroy_texture(atlas_);
    if (color_atlas_.valid() && device_) device_->destroy_texture(color_atlas_);
    if (shaper_)     font::destroy_text_shaper(shaper_);
    if (mgr_color_)  font::destroy_glyph_atlas_manager(mgr_color_);   // mirrors faces (not owned)
    if (mgr_)        font::destroy_glyph_atlas_manager(mgr_);         // owns font_
    if (lib_)        font::destroy_font_library(lib_);
    device_ = nullptr; lib_ = nullptr; mgr_ = nullptr; mgr_color_ = nullptr; shaper_ = nullptr; font_ = nullptr;
    atlas_ = TextureHandle{}; color_atlas_ = TextureHandle{};
    atlas_layers_ = 0; color_atlas_layers_ = 0;
    color_font_idx_.clear();
}

void GpuTextRasterizer::shape(const char* text, float size, std::vector<GlyphQuad>& out, float& out_w, float& out_h) {
    out.clear(); out_w = 0; out_h = 0;
    if (!text || !text[0] || !shaper_ || !mgr_) return;
    font::IFontFace* primary = mgr_->get_font(0);
    if (!primary) return;
    primary->set_size(size);

    std::vector<font::PositionedGlyph> glyphs;
    shaper_->shape_text(primary, text, -1, glyphs, font::TextLayoutOptions());
    const font::FontMetrics& fm = primary->get_metrics();
    const float ascent = fm.ascender;
    float max_x = 0;
    for (const auto& pg : glyphs) {
        const float end_x = pg.x + pg.advance;
        if (end_x > max_x) max_x = end_x;

        // Colour-emoji faces (CBDT/COLR) go to the RGBA atlas; everything else to the
        // SDF atlas. The layout advance is already display-size (the face scales a
        // fixed strike's metrics), so only the bitmap quad needs the strike→size scale.
        font::IFontFace* gf = mgr_->get_font(pg.font_index);
        const font::GlyphSlot* s = nullptr;
        int   layer_base = 0;
        float qscale     = 1.0f;
        if (gf && gf->has_color() && mgr_color_) {
            int ci;
            auto it = color_font_idx_.find(pg.font_index);
            if (it != color_font_idx_.end()) ci = it->second;
            else { ci = mgr_color_->add_font(gf, /*take_ownership=*/false); color_font_idx_[pg.font_index] = ci; }
            s = mgr_color_->acquire(ci, pg.glyph_index, size);
            if (s) { layer_base = kColorLayerBase; qscale = s->scale; }
        }
        if (!s) s = mgr_->acquire(pg.font_index, pg.glyph_index, size);   // SDF coverage
        if (!s || s->pw <= 0 || s->ph <= 0) continue;                     // whitespace / missing

        GlyphQuad q;
        q.atlas_layer = s->layer + layer_base;
        q.x = pg.x + s->bearing_x;            // bearing already in display space
        q.y = ascent - s->bearing_y;
        q.w = float(s->pw) * qscale; q.h = float(s->ph) * qscale;
        q.u0 = s->u0; q.v0 = s->v0; q.u1 = s->u1; q.v1 = s->v1;
        out.push_back(q);
    }
    out_w = max_x;
    out_h = fm.ascender - fm.descender;
}

math::Vec2 GpuTextRasterizer::measure_text(const char* text, float size, const char*) {
    std::vector<GlyphQuad> q; float w = 0, h = 0; shape(text, size, q, w, h);
    return math::Vec2(w, h);
}

float GpuTextRasterizer::get_line_height(float size, const char*) {
    if (font_) { font_->set_size(size); const font::FontMetrics& fm = font_->get_metrics(); return fm.ascender - fm.descender; }
    return size * 1.2f;
}

bool GpuTextRasterizer::rasterize_glyphs(const char* text, float size, const char*,
                                         std::vector<GlyphQuad>& out, float* out_w, float* out_h) {
    float w = 0, h = 0;
    shape(text, size, out, w, h);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return !out.empty();
}

float GpuTextRasterizer::measure_advance(const char* text, int n, float size, const char*) {
    if (!text || n <= 0) return 0.0f;
    const int len = int(std::strlen(text));
    if (n > len) n = len;
    std::vector<GlyphQuad> q; float w = 0, h = 0;
    shape(std::string(text, n).c_str(), size, q, w, h);
    return w;
}

float GpuTextRasterizer::get_time() const { return float(now_ms() - start_ms_) / 1000.0f; }

// Upload one atlas manager's layers into its GPU array texture. The R8 SDF atlas and
// the RGBA colour atlas share this path (only the texture format differs).
void GpuTextRasterizer::sync_layers(font::IGlyphAtlasManager* mgr, TextureHandle& tex,
                                    int& layers_cached, TextureFormat fmt) {
    if (!mgr || !device_) return;
    const int W = mgr->width(), H = mgr->height(), layers = mgr->layer_count();
    if (layers < 1) return;

    // (Re)create the array texture on first use or when the atlas grew layers.
    if (!tex.valid() || layers > layers_cached) {
        if (tex.valid()) device_->destroy_texture(tex);
        TextureDesc td;
        td.width = W; td.height = H; td.array_layers = layers;
        td.array_texture = true;   // always a sampler2DArray, even at 1 layer
        td.format = fmt; td.usage = TEXTURE_USAGE_SAMPLED;
        tex = device_->create_texture(td);
        layers_cached = layers;
        for (int l = 0; l < layers; ++l) {
            if (const uint8_t* d = mgr->layer_data(l)) {
                TextureRegion r; r.x = 0; r.y = 0; r.layer = l; r.width = W; r.height = H;
                device_->update_texture(tex, r, d);
            }
        }
        std::vector<font::GlyphDirtyRegion> drop; mgr->take_dirty_regions(drop);
        return;
    }

    // Incremental: re-upload whichever layers changed (full layer; row-aligned to W).
    std::vector<font::GlyphDirtyRegion> dirty;
    mgr->take_dirty_regions(dirty);
    if (dirty.empty()) return;
    std::set<int> dirty_layers;
    for (const auto& r : dirty) dirty_layers.insert(r.layer);
    for (int l : dirty_layers) {
        if (const uint8_t* d = mgr->layer_data(l)) {
            TextureRegion r; r.x = 0; r.y = 0; r.layer = l; r.width = W; r.height = H;
            device_->update_texture(tex, r, d);
        }
    }
}

TextureHandle GpuTextRasterizer::sync_atlas() {
    sync_layers(mgr_,       atlas_,       atlas_layers_,       TextureFormat::R8_UNORM);
    sync_layers(mgr_color_, color_atlas_, color_atlas_layers_, TextureFormat::RGBA8_UNORM);
    return atlas_;
}

} // namespace gui
} // namespace window
