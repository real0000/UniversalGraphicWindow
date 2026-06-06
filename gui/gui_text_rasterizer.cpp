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
    mgr_    = font::create_glyph_atlas_manager(lib_, acfg);
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
    if (atlas_.valid() && device_) device_->destroy_texture(atlas_);
    if (shaper_) font::destroy_text_shaper(shaper_);
    if (mgr_)    font::destroy_glyph_atlas_manager(mgr_);   // owns font_
    if (lib_)    font::destroy_font_library(lib_);
    device_ = nullptr; lib_ = nullptr; mgr_ = nullptr; shaper_ = nullptr; font_ = nullptr;
    atlas_ = TextureHandle{}; atlas_layers_ = 0;
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
        const font::GlyphSlot* s = mgr_->acquire(pg.font_index, pg.glyph_index, size);
        if (!s || s->pw <= 0 || s->ph <= 0) continue;   // whitespace / missing
        GlyphQuad q;
        q.atlas_layer = s->layer;
        q.x = pg.x + s->bearing_x;
        q.y = ascent - s->bearing_y;
        q.w = float(s->pw); q.h = float(s->ph);
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

TextureHandle GpuTextRasterizer::sync_atlas() {
    if (!mgr_ || !device_) return atlas_;
    const int W = mgr_->width(), H = mgr_->height(), layers = mgr_->layer_count();
    if (layers < 1) return atlas_;

    // (Re)create the array texture on first use or when the atlas grew layers,
    // then upload all layers. The shared atlas is what makes text batchable.
    if (!atlas_.valid() || layers > atlas_layers_) {
        if (atlas_.valid()) device_->destroy_texture(atlas_);
        TextureDesc td;
        td.width = W; td.height = H; td.array_layers = layers;
        td.format = TextureFormat::R8_UNORM; td.usage = TEXTURE_USAGE_SAMPLED;
        atlas_ = device_->create_texture(td);
        atlas_layers_ = layers;
        for (int l = 0; l < layers; ++l) {
            if (const uint8_t* d = mgr_->layer_data(l)) {
                TextureRegion r; r.x = 0; r.y = 0; r.layer = l; r.width = W; r.height = H;
                device_->update_texture(atlas_, r, d);
            }
        }
        std::vector<font::GlyphDirtyRegion> drop; mgr_->take_dirty_regions(drop);
        return atlas_;
    }

    // Incremental: re-upload whichever layers changed (full layer; row-aligned to W).
    std::vector<font::GlyphDirtyRegion> dirty;
    mgr_->take_dirty_regions(dirty);
    if (dirty.empty()) return atlas_;
    std::set<int> dirty_layers;
    for (const auto& r : dirty) dirty_layers.insert(r.layer);
    for (int l : dirty_layers) {
        if (const uint8_t* d = mgr_->layer_data(l)) {
            TextureRegion r; r.x = 0; r.y = 0; r.layer = l; r.width = W; r.height = H;
            device_->update_texture(atlas_, r, d);
        }
    }
    return atlas_;
}

} // namespace gui
} // namespace window
