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
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <vector>

#ifdef FONT_SUPPORT_FRIBIDI
#include <fribidi.h>
#endif

namespace font {

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
        if (hb_font_) hb_font_destroy(hb_font_);
    }

    void shape_text(IFontFace* font, const char* text, int text_length,
                    std::vector<PositionedGlyph>& out_glyphs,
                    const TextLayoutOptions& options) override {
        out_glyphs.clear();
        if (!font || !text) return;
        if (text_length < 0) text_length = static_cast<int>(strlen(text));
        if (text_length == 0) return;

        ensure_hb_font(font);

#ifdef FONT_SUPPORT_FRIBIDI
        // BiDi reordering
        std::vector<FriBidiChar> unicode(text_length);
        int ulen = 0;
        {
            const char* ptr = text;
            const char* end = text + text_length;
            while (ptr < end) {
                uint32_t cp;
                int bytes = utf8_to_codepoint(ptr, &cp);
                if (bytes == 0) { ptr++; continue; }
                unicode[ulen++] = static_cast<FriBidiChar>(cp);
                ptr += bytes;
            }
        }
        unicode.resize(ulen);

        FriBidiParType base_dir = (options.direction == TextDirection::RightToLeft)
                                  ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_LTR;
        std::vector<FriBidiChar> visual(ulen);
        std::vector<FriBidiLevel> levels(ulen);
        fribidi_log2vis(unicode.data(), ulen, &base_dir, visual.data(), nullptr, nullptr, levels.data());

        // Convert visual order back to UTF-8
        std::vector<char> visual_utf8;
        visual_utf8.reserve(text_length + 4);
        for (int i = 0; i < ulen; i++) {
            char buf[4];
            int bytes = codepoint_to_utf8(static_cast<uint32_t>(visual[i]), buf);
            for (int j = 0; j < bytes; j++) visual_utf8.push_back(buf[j]);
        }

        shape_buffer(font, visual_utf8.data(), static_cast<int>(visual_utf8.size()),
                     options, out_glyphs);
#else
        shape_buffer(font, text, text_length, options, out_glyphs);
#endif
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

        result.bounds = window::math::make_box(0, 0, max_x, y + fm.descender);
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
    void ensure_hb_font(IFontFace* font) {
        if (current_font_ == font && hb_font_) return;
        if (hb_font_) { hb_font_destroy(hb_font_); hb_font_ = nullptr; }

        // Create HarfBuzz font from native handle
        void* native = font->get_native_handle();
        if (!native) {
            // Fallback: create a dummy font from face data
            hb_blob_t* blob = hb_blob_create_from_file("");
            hb_face_t* face = hb_face_create(blob, 0);
            hb_font_ = hb_font_create(face);
            hb_face_destroy(face);
            hb_blob_destroy(blob);
        } else {
            // Platform backends store different native types.
            // For FreeType: native is FT_Face, use hb_ft_font_create.
            // For others: create a basic HarfBuzz font.
#ifdef FONT_SUPPORT_FREETYPE
            // If FreeType is available, native handle is FT_Face
            hb_font_ = hb_ft_font_create(static_cast<FT_Face>(native), nullptr);
#else
            // Build from font tables if possible; otherwise use a nominal font
            hb_face_t* face = hb_face_create(hb_blob_get_empty(), 0);
            hb_font_ = hb_font_create(face);
            hb_face_destroy(face);
#endif
        }

        float size = font->get_size();
        hb_font_set_scale(hb_font_,
                          static_cast<int>(size * 64),
                          static_cast<int>(size * 64));
        current_font_ = font;
    }

    void shape_buffer(IFontFace* font, const char* text, int text_length,
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

        hb_shape(hb_font_, buffer_, nullptr, 0);

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
    hb_font_t* hb_font_ = nullptr;
    IFontFace* current_font_ = nullptr;
};

// Factory
ITextShaper* create_harfbuzz_text_shaper(IFontLibrary* library, Result* out_result) {
    if (out_result) *out_result = Result::Success;
    return new HarfBuzzTextShaper(library);
}

} // namespace font

#endif // FONT_SUPPORT_HARFBUZZ
