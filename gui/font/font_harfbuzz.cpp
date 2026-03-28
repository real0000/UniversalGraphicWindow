/*
 * font_harfbuzz.cpp - HarfBuzz text shaper with optional FriBidi BiDi support
 *
 * Provides proper text shaping (ligatures, complex scripts, combining characters)
 * via HarfBuzz and optional bidirectional text reordering via FriBidi.
 */

#include "font.hpp"

#ifdef FONT_SUPPORT_HARFBUZZ

#include <hb.h>
#ifdef FONT_SUPPORT_FREETYPE
#include <hb-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#endif
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>

#ifdef FONT_SUPPORT_FRIBIDI
#include <fribidi.h>
#endif

namespace font {

// ============================================================================
// HarfBuzz ↔ IFontFace bridge (used when FreeType is not available)
// ============================================================================

#ifndef FONT_SUPPORT_FREETYPE

// Callback: map Unicode codepoint → glyph index via IFontFace
static hb_bool_t hb_nominal_glyph_func(hb_font_t*, void* font_data,
                                         hb_codepoint_t unicode,
                                         hb_codepoint_t* glyph,
                                         void*) {
    IFontFace* face = static_cast<IFontFace*>(font_data);
    *glyph = face->get_glyph_index(unicode);
    return *glyph != 0;
}

// Callback: horizontal advance for a glyph
static hb_position_t hb_h_advance_func(hb_font_t*, void* font_data,
                                         hb_codepoint_t glyph, void*) {
    IFontFace* face = static_cast<IFontFace*>(font_data);
    GlyphMetrics m;
    if (face->get_glyph_metrics(glyph, &m))
        return static_cast<hb_position_t>(m.advance_x * 64.0f);
    return 0;
}

// Cached font-funcs singleton
static hb_font_funcs_t* get_bridge_font_funcs() {
    static hb_font_funcs_t* funcs = nullptr;
    if (!funcs) {
        funcs = hb_font_funcs_create();
        hb_font_funcs_set_nominal_glyph_func(funcs, hb_nominal_glyph_func, nullptr, nullptr);
        hb_font_funcs_set_glyph_h_advance_func(funcs, hb_h_advance_func, nullptr, nullptr);
        hb_font_funcs_make_immutable(funcs);
    }
    return funcs;
}

#ifdef _WIN32
// Create hb_face from DirectWrite IDWriteFontFace via font-table callback
static hb_blob_t* dwrite_reference_table(hb_face_t*, hb_tag_t tag, void* user_data) {
    IDWriteFontFace* dw = static_cast<IDWriteFontFace*>(user_data);
    const void* data = nullptr;
    UINT32 size = 0;
    void* ctx = nullptr;
    BOOL exists = FALSE;
    if (FAILED(dw->TryGetFontTable(tag, &data, &size, &ctx, &exists)) || !exists || !data)
        return nullptr;
    // Copy so we can release the table context immediately
    char* copy = static_cast<char*>(malloc(size));
    if (!copy) { dw->ReleaseFontTable(ctx); return nullptr; }
    memcpy(copy, data, size);
    dw->ReleaseFontTable(ctx);
    return hb_blob_create(copy, size, HB_MEMORY_MODE_WRITABLE, copy, free);
}
#endif // _WIN32

#endif // !FONT_SUPPORT_FREETYPE

// ============================================================================
// HarfBuzz Text Shaper
// ============================================================================

class HarfBuzzTextShaper : public ITextShaper {
public:
    explicit HarfBuzzTextShaper(IFontLibrary* library) : library_(library) {
        buffer_ = hb_buffer_create();
    }

    ~HarfBuzzTextShaper() override {
        if (buffer_) hb_buffer_destroy(buffer_);
        clear_hb_font_cache();
    }

    // Set a fallback chain for multi-language support.
    // When set, shape_text will split runs by font coverage.
    void set_fallback_chain(IFontFallbackChain* chain) {
        fallback_chain_ = chain;
    }

    void shape_text(IFontFace* font, const char* text, int text_length,
                    std::vector<PositionedGlyph>& out_glyphs,
                    const TextLayoutOptions& options) override {
        out_glyphs.clear();
        if (!font || !text) return;
        if (text_length < 0) text_length = static_cast<int>(strlen(text));
        if (text_length == 0) return;

        // Decode UTF-8 to codepoints for fallback analysis and BiDi
        std::vector<uint32_t> codepoints;
        std::vector<int> byte_offsets; // byte offset of each codepoint in text
        {
            const char* ptr = text;
            const char* end = text + text_length;
            while (ptr < end) {
                byte_offsets.push_back(static_cast<int>(ptr - text));
                uint32_t cp;
                int bytes = utf8_to_codepoint(ptr, &cp);
                if (bytes == 0) { ptr++; continue; }
                codepoints.push_back(cp);
                ptr += bytes;
            }
        }
        int ulen = static_cast<int>(codepoints.size());
        if (ulen == 0) return;

#ifdef FONT_SUPPORT_FRIBIDI
        // BiDi reordering
        FriBidiParType base_dir = (options.direction == TextDirection::RightToLeft)
                                  ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
        std::vector<FriBidiChar> fribidi_input(ulen);
        for (int i = 0; i < ulen; i++)
            fribidi_input[i] = static_cast<FriBidiChar>(codepoints[i]);

        std::vector<FriBidiChar> visual(ulen);
        std::vector<FriBidiStrIndex> visual_to_logical(ulen);
        std::vector<FriBidiLevel> levels(ulen);
        fribidi_log2vis(fribidi_input.data(), ulen, &base_dir,
                        visual.data(), nullptr, visual_to_logical.data(), levels.data());

        // Use visual-order codepoints for shaping
        std::vector<uint32_t> ordered_cps(ulen);
        for (int i = 0; i < ulen; i++)
            ordered_cps[i] = static_cast<uint32_t>(visual[i]);
#else
        const std::vector<uint32_t>& ordered_cps = codepoints;
#endif

        // If we have a fallback chain, split into runs by font
        if (fallback_chain_ && fallback_chain_->get_font_count() > 1) {
            shape_with_fallback(ordered_cps, options, out_glyphs);
        } else {
            // Single font path — shape the whole text at once
            std::vector<char> utf8_buf;
            codepoints_to_utf8(ordered_cps, utf8_buf);
            ensure_hb_font(font);
            shape_buffer(font, utf8_buf.data(), static_cast<int>(utf8_buf.size()),
                         options, out_glyphs, 0);
        }
    }

    TextLayoutResult layout_text(IFontFace* font, const char* text, int text_length,
                                  std::vector<PositionedGlyph>& out_glyphs,
                                  const TextLayoutOptions& options) override {
        TextLayoutResult result;
        out_glyphs.clear();
        if (!font || !text) return result;
        if (text_length < 0) text_length = static_cast<int>(strlen(text));
        if (text_length == 0) return result;

        const FontMetrics& fm = font->get_metrics();
        float line_h = fm.line_height * options.line_spacing;
        float y = fm.ascender;
        float max_x = 0;
        int line_count = 1;

        // Split into lines and shape each
        const char* ptr = text;
        const char* end = text + text_length;

        while (ptr < end) {
            // Find next newline
            const char* nl = ptr;
            while (nl < end && *nl != '\n') nl++;
            int line_len = static_cast<int>(nl - ptr);

            // Shape this line
            std::vector<PositionedGlyph> line_glyphs;
            shape_text(font, ptr, line_len, line_glyphs, options);

            // Word wrapping
            if (options.max_width > 0 && options.wrap_words) {
                float x = 0;
                int last_space = -1;
                float last_space_x = 0;

                for (int i = 0; i < static_cast<int>(line_glyphs.size()); i++) {
                    if (line_glyphs[i].codepoint == ' ') {
                        last_space = i;
                        last_space_x = x;
                    }
                    x += line_glyphs[i].advance;
                    if (x > options.max_width && last_space >= 0) {
                        // Emit glyphs up to last_space
                        for (int j = 0; j <= last_space; j++) {
                            PositionedGlyph pg = line_glyphs[j];
                            pg.y = y;
                            out_glyphs.push_back(pg);
                        }
                        if (last_space_x > max_x) max_x = last_space_x;
                        y += line_h;
                        line_count++;

                        // Re-layout remaining glyphs
                        float offset = line_glyphs[last_space + 1].x;
                        for (int j = last_space + 1; j < static_cast<int>(line_glyphs.size()); j++) {
                            line_glyphs[j].x -= offset;
                        }
                        line_glyphs.erase(line_glyphs.begin(), line_glyphs.begin() + last_space + 1);
                        i = -1;
                        x = 0;
                        last_space = -1;
                    }
                }
            }

            // Add remaining glyphs with y offset
            for (auto& pg : line_glyphs) {
                pg.y = y;
                out_glyphs.push_back(pg);
                float end_x = pg.x + pg.advance;
                if (end_x > max_x) max_x = end_x;
            }

            if (nl < end) {
                y += line_h;
                line_count++;
                ptr = nl + 1;
            } else {
                ptr = end;
            }

            // Height limit check
            if (options.max_height > 0 && y + line_h > options.max_height) {
                result.truncated = true;
                break;
            }
        }

        result.bounds = window::math::make_box(0, 0, max_x, y - fm.descender);
        result.line_count = line_count;
        result.char_count = static_cast<int>(out_glyphs.size());
        return result;
    }

    window::math::Vec2 measure_text(IFontFace* font, const char* text, int text_length,
                                     const TextLayoutOptions& options) override {
        std::vector<PositionedGlyph> glyphs;
        if (options.max_width > 0) {
            TextLayoutResult r = layout_text(font, text, text_length, glyphs, options);
            return window::math::vec2(
                window::math::box_width(r.bounds),
                window::math::box_height(r.bounds));
        }
        shape_text(font, text, text_length, glyphs, options);
        if (glyphs.empty()) return window::math::vec2(0, 0);
        float w = glyphs.back().x + glyphs.back().advance;
        float h = font->get_metrics().line_height;
        return window::math::vec2(w, h);
    }

    void get_caret_positions(IFontFace* font, const char* text, int text_length,
                              std::vector<float>& out_positions) override {
        out_positions.clear();
        std::vector<PositionedGlyph> glyphs;
        shape_text(font, text, text_length, glyphs, TextLayoutOptions());
        out_positions.push_back(0);
        for (auto& g : glyphs) {
            out_positions.push_back(g.x + g.advance);
        }
    }

    int hit_test(IFontFace* font, const char* text, int text_length,
                  float hx, float, const TextLayoutOptions& options) override {
        std::vector<PositionedGlyph> glyphs;
        shape_text(font, text, text_length, glyphs, options);
        for (int i = 0; i < static_cast<int>(glyphs.size()); i++) {
            float mid = glyphs[i].x + glyphs[i].advance * 0.5f;
            if (hx < mid) return i;
        }
        return static_cast<int>(glyphs.size());
    }

private:
    // --- HarfBuzz font cache (supports multiple fonts for fallback) ---

    struct HbFontEntry {
        IFontFace* face = nullptr;
        hb_font_t* hb_font = nullptr;
    };

    void clear_hb_font_cache() {
        for (auto& e : hb_font_cache_) {
            if (e.hb_font) hb_font_destroy(e.hb_font);
        }
        hb_font_cache_.clear();
        // Legacy single-font pointer
        if (hb_font_) { hb_font_destroy(hb_font_); hb_font_ = nullptr; }
        current_font_ = nullptr;
    }

    hb_font_t* get_or_create_hb_font(IFontFace* font) {
        // Check cache
        for (auto& e : hb_font_cache_) {
            if (e.face == font) return e.hb_font;
        }
        // Create new
        hb_font_t* hf = create_hb_font_for(font);
        hb_font_cache_.push_back({font, hf});
        return hf;
    }

    static hb_font_t* create_hb_font_for(IFontFace* font) {
        void* native = font->get_native_handle();
        hb_font_t* hf = nullptr;

#ifdef FONT_SUPPORT_FREETYPE
        if (native) {
            hf = hb_ft_font_create(static_cast<FT_Face>(native), nullptr);
        } else {
            hb_face_t* face = hb_face_create(hb_blob_get_empty(), 0);
            hf = hb_font_create(face);
            hb_face_destroy(face);
        }
#else
        hb_face_t* face = nullptr;
#ifdef _WIN32
        if (native) {
            face = hb_face_create_for_tables(
                dwrite_reference_table, native, nullptr);
        }
#endif
        if (!face)
            face = hb_face_create(hb_blob_get_empty(), 0);

        hf = hb_font_create(face);
        hb_face_destroy(face);

        hb_font_set_funcs(hf, get_bridge_font_funcs(),
                          static_cast<void*>(font), nullptr);
#endif

        float size = font->get_size();
        hb_font_set_scale(hf,
                          static_cast<int>(size * 64),
                          static_cast<int>(size * 64));
        return hf;
    }

    // Legacy single-font ensure (for backward compat)
    void ensure_hb_font(IFontFace* font) {
        if (current_font_ == font && hb_font_) return;
        if (hb_font_) { hb_font_destroy(hb_font_); hb_font_ = nullptr; }
        hb_font_ = create_hb_font_for(font);
        current_font_ = font;
    }

    // --- Codepoint-to-UTF8 helper ---

    static void codepoints_to_utf8(const std::vector<uint32_t>& cps, std::vector<char>& out) {
        out.clear();
        out.reserve(cps.size() * 2);
        for (uint32_t cp : cps) {
            char buf[4];
            int bytes = codepoint_to_utf8(cp, buf);
            for (int j = 0; j < bytes; j++) out.push_back(buf[j]);
        }
    }

    // --- Fallback run splitting and shaping ---

    // A run is a contiguous sequence of codepoints that should be shaped
    // with the same font.
    struct FontRun {
        int font_index;           // index into fallback chain
        int start;                // start index in codepoint array
        int count;                // number of codepoints
    };

    // Split codepoints into runs by font coverage
    void compute_font_runs(const std::vector<uint32_t>& cps,
                           std::vector<FontRun>& runs) {
        runs.clear();
        if (cps.empty() || !fallback_chain_) return;

        int cur_font = -1;
        int run_start = 0;

        for (int i = 0; i < static_cast<int>(cps.size()); ++i) {
            uint32_t cp = cps[i];
            int fi = 0;

            // Whitespace/control chars stay with whatever font is current
            if (cp == ' ' || cp == '\t' || cp == '\n') {
                fi = (cur_font >= 0) ? cur_font : 0;
            } else {
                uint32_t gi;
                fi = fallback_chain_->resolve(cp, &gi);
            }

            if (fi != cur_font) {
                if (cur_font >= 0 && i > run_start) {
                    runs.push_back({cur_font, run_start, i - run_start});
                }
                cur_font = fi;
                run_start = i;
            }
        }
        // Final run
        if (cur_font >= 0 && run_start < static_cast<int>(cps.size())) {
            runs.push_back({cur_font, run_start,
                            static_cast<int>(cps.size()) - run_start});
        }
    }

    void shape_with_fallback(const std::vector<uint32_t>& cps,
                             const TextLayoutOptions& options,
                             std::vector<PositionedGlyph>& out_glyphs) {
        std::vector<FontRun> runs;
        compute_font_runs(cps, runs);

        float x_offset = 0;
        for (auto& run : runs) {
            IFontFace* run_font = fallback_chain_->get_font(run.font_index);
            if (!run_font) run_font = fallback_chain_->get_primary_font();
            if (!run_font) continue;

            // Extract sub-codepoints and convert to UTF-8
            std::vector<uint32_t> sub_cps(cps.begin() + run.start,
                                          cps.begin() + run.start + run.count);
            std::vector<char> utf8_buf;
            codepoints_to_utf8(sub_cps, utf8_buf);

            // Shape this run
            hb_font_t* hf = get_or_create_hb_font(run_font);
            std::vector<PositionedGlyph> run_glyphs;
            shape_buffer_with_hb(hf, utf8_buf.data(),
                                 static_cast<int>(utf8_buf.size()),
                                 options, run_glyphs);

            // Offset positions and set font_index
            float run_width = 0;
            for (auto& pg : run_glyphs) {
                pg.x += x_offset;
                pg.font_index = static_cast<uint16_t>(run.font_index);
                pg.cluster += run.start;
                float end_x = pg.x + pg.advance;
                if (end_x - x_offset > run_width)
                    run_width = end_x - x_offset;
                out_glyphs.push_back(pg);
            }

            x_offset += run_width;
        }

        // Apply letter spacing across all glyphs
        if (options.letter_spacing != 0) {
            float offset = 0;
            for (auto& g : out_glyphs) {
                g.x += offset;
                offset += options.letter_spacing;
            }
        }
    }

    // Core buffer shaping — takes an explicit hb_font_t*
    void shape_buffer_with_hb(hb_font_t* hf, const char* text, int text_length,
                              const TextLayoutOptions& options,
                              std::vector<PositionedGlyph>& out_glyphs) {
        hb_buffer_reset(buffer_);
        hb_buffer_add_utf8(buffer_, text, text_length, 0, text_length);

        hb_direction_t dir = HB_DIRECTION_LTR;
        if (options.direction == TextDirection::RightToLeft) dir = HB_DIRECTION_RTL;
        else if (options.direction == TextDirection::TopToBottom) dir = HB_DIRECTION_TTB;
        hb_buffer_set_direction(buffer_, dir);

        hb_buffer_set_script(buffer_, hb_script_from_string("", -1));
        hb_buffer_guess_segment_properties(buffer_);

        hb_shape(hf, buffer_, nullptr, 0);

        unsigned int glyph_count = 0;
        hb_glyph_info_t* glyph_info = hb_buffer_get_glyph_infos(buffer_, &glyph_count);
        hb_glyph_position_t* glyph_pos = hb_buffer_get_glyph_positions(buffer_, &glyph_count);

        float x = 0, y = 0;
        for (unsigned int i = 0; i < glyph_count; i++) {
            PositionedGlyph pg;
            pg.codepoint = glyph_info[i].codepoint;
            pg.glyph_index = glyph_info[i].codepoint;
            pg.cluster = static_cast<int>(glyph_info[i].cluster);
            pg.x = x + glyph_pos[i].x_offset / 64.0f;
            pg.y = y + glyph_pos[i].y_offset / 64.0f;
            pg.advance = glyph_pos[i].x_advance / 64.0f;
            out_glyphs.push_back(pg);

            x += glyph_pos[i].x_advance / 64.0f;
            y += glyph_pos[i].y_advance / 64.0f;
        }
    }

    // Legacy shape_buffer (calls shape_buffer_with_hb using current hb_font_)
    void shape_buffer(IFontFace* font, const char* text, int text_length,
                      const TextLayoutOptions& options,
                      std::vector<PositionedGlyph>& out_glyphs,
                      uint16_t font_index = 0) {
        shape_buffer_with_hb(hb_font_, text, text_length, options, out_glyphs);

        // Set font_index on all glyphs
        for (auto& g : out_glyphs) g.font_index = font_index;

        // Apply letter spacing
        if (options.letter_spacing != 0) {
            float offset = 0;
            for (auto& g : out_glyphs) {
                g.x += offset;
                offset += options.letter_spacing;
            }
        }
    }

    IFontLibrary* library_;
    hb_buffer_t* buffer_ = nullptr;
    hb_font_t* hb_font_ = nullptr;           // legacy single-font
    IFontFace* current_font_ = nullptr;       // legacy single-font tracking
    IFontFallbackChain* fallback_chain_ = nullptr;
    std::vector<HbFontEntry> hb_font_cache_;  // multi-font cache
};

// Factory
ITextShaper* create_harfbuzz_text_shaper(IFontLibrary* library, Result* out_result) {
    if (out_result) *out_result = Result::Success;
    return new HarfBuzzTextShaper(library);
}

} // namespace font

#endif // FONT_SUPPORT_HARFBUZZ
