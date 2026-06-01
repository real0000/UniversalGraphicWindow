/*
 * font.cpp - Platform-Independent Font Utilities
 *
 * Contains string conversion, UTF-8 utilities, and common functionality.
 */

#include "font.hpp"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <climits>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

namespace font {

using Vec2 = window::math::Vec2;
using Vec4 = window::math::Vec4;
using Box = window::math::Box;

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* result_to_string(Result result) {
    switch (result) {
        case Result::Success:               return "Success";
        case Result::ErrorUnknown:          return "Unknown error";
        case Result::ErrorNotInitialized:   return "Not initialized";
        case Result::ErrorAlreadyInitialized: return "Already initialized";
        case Result::ErrorFileNotFound:     return "File not found";
        case Result::ErrorInvalidFont:      return "Invalid font";
        case Result::ErrorInvalidParameter: return "Invalid parameter";
        case Result::ErrorOutOfMemory:      return "Out of memory";
        case Result::ErrorGlyphNotFound:    return "Glyph not found";
        case Result::ErrorRenderFailed:     return "Render failed";
        case Result::ErrorBackendNotSupported: return "Backend not supported";
        default:                            return "Unknown";
    }
}

const char* font_backend_to_string(FontBackend backend) {
    switch (backend) {
        case FontBackend::Auto:     return "Auto";
        case FontBackend::Native:   return "Native";
        case FontBackend::FreeType: return "FreeType";
        default:                    return "Unknown";
    }
}

const char* font_weight_to_string(FontWeight weight) {
    switch (weight) {
        case FontWeight::Thin:       return "Thin";
        case FontWeight::ExtraLight: return "ExtraLight";
        case FontWeight::Light:      return "Light";
        case FontWeight::Regular:    return "Regular";
        case FontWeight::Medium:     return "Medium";
        case FontWeight::SemiBold:   return "SemiBold";
        case FontWeight::Bold:       return "Bold";
        case FontWeight::ExtraBold:  return "ExtraBold";
        case FontWeight::Black:      return "Black";
        default:                     return "Unknown";
    }
}

const char* font_style_to_string(FontStyle style) {
    switch (style) {
        case FontStyle::Normal:  return "Normal";
        case FontStyle::Italic:  return "Italic";
        case FontStyle::Oblique: return "Oblique";
        default:                 return "Unknown";
    }
}

// ============================================================================
// UTF-8 Utilities
// ============================================================================

int utf8_to_codepoint(const char* str, uint32_t* out_codepoint) {
    if (!str || !out_codepoint) return 0;

    const uint8_t* s = reinterpret_cast<const uint8_t*>(str);

    if (s[0] < 0x80) {
        // 1-byte sequence (ASCII)
        *out_codepoint = s[0];
        return 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        if ((s[1] & 0xC0) != 0x80) return 0;
        *out_codepoint = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        // 3-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80) return 0;
        *out_codepoint = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80) return 0;
        *out_codepoint = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                         ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }

    return 0; // Invalid UTF-8
}

int codepoint_to_utf8(uint32_t codepoint, char* out_str) {
    if (!out_str) return 0;

    uint8_t* s = reinterpret_cast<uint8_t*>(out_str);

    if (codepoint < 0x80) {
        s[0] = static_cast<uint8_t>(codepoint);
        return 1;
    } else if (codepoint < 0x800) {
        s[0] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
        s[1] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        s[0] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
        s[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        s[2] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        return 3;
    } else if (codepoint < 0x110000) {
        s[0] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
        s[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
        s[2] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
        s[3] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
        return 4;
    }

    return 0; // Invalid codepoint
}

int utf8_strlen(const char* str) {
    if (!str) return 0;

    int count = 0;
    const uint8_t* s = reinterpret_cast<const uint8_t*>(str);

    while (*s) {
        if ((*s & 0xC0) != 0x80) {
            count++;
        }
        s++;
    }

    return count;
}

// ============================================================================
// Backend Detection
// ============================================================================

bool is_backend_available(FontBackend backend) {
    switch (backend) {
        case FontBackend::Auto:
            return true;

        case FontBackend::Native:
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
            return true;
#else
            return false;
#endif

        case FontBackend::FreeType:
#ifdef FONT_SUPPORT_FREETYPE
            return true;
#else
            return false;
#endif

        default:
            return false;
    }
}

FontBackend get_default_backend() {
#ifdef FONT_SUPPORT_FREETYPE
    // Prefer FreeType for consistent cross-platform rendering
    return FontBackend::FreeType;
#elif defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    return FontBackend::Native;
#else
    return FontBackend::Auto;
#endif
}

// ============================================================================
// Simple Glyph Cache Implementation
// ============================================================================

struct GlyphCacheKey {
    IFontFace* font;
    uint32_t glyph_index;
    float size;
    AntiAliasMode antialias;

    bool operator==(const GlyphCacheKey& other) const {
        return font == other.font &&
               glyph_index == other.glyph_index &&
               std::abs(size - other.size) < 0.01f &&
               antialias == other.antialias;
    }
};

struct GlyphCacheKeyHash {
    size_t operator()(const GlyphCacheKey& key) const {
        size_t h = reinterpret_cast<size_t>(key.font);
        h ^= std::hash<uint32_t>{}(key.glyph_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(key.antialias)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

class GlyphCacheImpl : public IGlyphCache {
public:
    GlyphCacheImpl(int max_glyphs) : max_glyphs_(max_glyphs), max_memory_(64 * 1024 * 1024) {}

    ~GlyphCacheImpl() {
        clear_all();
    }

    const CachedGlyph* get_glyph(IFontFace* font, uint32_t glyph_index,
                                  const RenderOptions& options) override {
        GlyphCacheKey key{font, glyph_index, font->get_size(), options.antialias};

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }

        // Render and cache
        CachedGlyph cached;
        cached.glyph_index = glyph_index;
        cached.size = font->get_size();
        cached.antialias = options.antialias;

        Result result = font->render_glyph(glyph_index, options, &cached.bitmap);
        if (result == Result::Success) {
            cached.valid = true;

            // Evict if needed
            while (cache_.size() >= static_cast<size_t>(max_glyphs_) ||
                   memory_usage_ + cached.bitmap.pitch * cached.bitmap.height > max_memory_) {
                if (cache_.empty()) break;
                evict_oldest();
            }

            // Copy bitmap data (font may invalidate it)
            if (cached.bitmap.pixels && cached.bitmap.pitch > 0 && cached.bitmap.height > 0) {
                size_t size = cached.bitmap.pitch * cached.bitmap.height;
                void* copy = malloc(size);
                if (copy) {
                    memcpy(copy, cached.bitmap.pixels, size);
                    cached.bitmap.pixels = copy;
                    memory_usage_ += size;
                }
            }

            cache_[key] = cached;
            return &cache_[key];
        }

        return nullptr;
    }

    void clear_font(IFontFace* font) override {
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first.font == font) {
                free_glyph(it->second);
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void clear_all() override {
        for (auto& pair : cache_) {
            free_glyph(pair.second);
        }
        cache_.clear();
        memory_usage_ = 0;
    }

    int get_cached_count() const override {
        return static_cast<int>(cache_.size());
    }

    size_t get_memory_usage() const override {
        return memory_usage_;
    }

    void set_max_glyphs(int max_glyphs) override {
        max_glyphs_ = max_glyphs;
    }

    void set_max_memory(size_t max_bytes) override {
        max_memory_ = max_bytes;
    }

private:
    void free_glyph(CachedGlyph& glyph) {
        if (glyph.bitmap.pixels) {
            size_t size = glyph.bitmap.pitch * glyph.bitmap.height;
            memory_usage_ -= size;
            free(glyph.bitmap.pixels);
            glyph.bitmap.pixels = nullptr;
        }
    }

    void evict_oldest() {
        if (cache_.empty()) return;
        auto it = cache_.begin();
        free_glyph(it->second);
        cache_.erase(it);
    }

    std::unordered_map<GlyphCacheKey, CachedGlyph, GlyphCacheKeyHash> cache_;
    int max_glyphs_;
    size_t max_memory_;
    size_t memory_usage_ = 0;
};

IGlyphCache* create_glyph_cache(int max_glyphs) {
    return new GlyphCacheImpl(max_glyphs);
}

void destroy_glyph_cache(IGlyphCache* cache) {
    delete cache;
}

// ============================================================================
// Simple Text Shaper Implementation
// ============================================================================

class SimpleTextShaper : public ITextShaper {
public:
    SimpleTextShaper(IFontLibrary* library) : library_(library) {}

    void set_fallback_chain(IFontFallbackChain* chain) {
        fallback_chain_ = chain;
    }

    void shape_text(IFontFace* font, const char* text, int text_length,
                    std::vector<PositionedGlyph>& out_glyphs,
                    const TextLayoutOptions& options) override {
        out_glyphs.clear();
        if (!font || !text) return;

        if (text_length < 0) text_length = static_cast<int>(strlen(text));

        float x = 0.0f;
        float y = 0.0f;
        uint32_t prev_glyph = 0;

        const char* ptr = text;
        const char* end = text + text_length;
        int cluster = 0;

        while (ptr < end) {
            uint32_t codepoint;
            int bytes = utf8_to_codepoint(ptr, &codepoint);
            if (bytes == 0) {
                ptr++;
                continue;
            }

            // Resolve which font has this glyph
            uint32_t glyph_index = 0;
            uint16_t font_index = 0;
            IFontFace* glyph_font = font;

            if (fallback_chain_) {
                int fi = fallback_chain_->resolve(codepoint, &glyph_index);
                font_index = static_cast<uint16_t>(fi);
                IFontFace* fb = fallback_chain_->get_font(fi);
                if (fb) glyph_font = fb;
            } else {
                glyph_index = font->get_glyph_index(codepoint);
            }

            // Apply kerning (only within same font)
            if (prev_glyph != 0 && font_index == 0) {
                x += glyph_font->get_kerning(prev_glyph, glyph_index);
            }
            x += options.letter_spacing;

            GlyphMetrics metrics;
            glyph_font->get_glyph_metrics(glyph_index, &metrics);

            PositionedGlyph pg;
            pg.codepoint = codepoint;
            pg.glyph_index = glyph_index;
            pg.x = x;
            pg.y = y;
            pg.advance = metrics.advance_x;
            pg.cluster = cluster;
            pg.font_index = font_index;
            out_glyphs.push_back(pg);

            x += metrics.advance_x;

            // Handle spaces for word spacing
            if (codepoint == ' ') {
                x += options.word_spacing;
            }

            prev_glyph = glyph_index;
            ptr += bytes;
            cluster++;
        }
    }

    TextLayoutResult layout_text(IFontFace* font, const char* text, int text_length,
                                  std::vector<PositionedGlyph>& out_glyphs,
                                  const TextLayoutOptions& options) override {
        TextLayoutResult result;
        out_glyphs.clear();

        if (!font || !text) return result;
        if (text_length < 0) text_length = static_cast<int>(strlen(text));

        const FontMetrics& metrics = font->get_metrics();
        float line_height = metrics.line_height * options.line_spacing;

        float x = 0.0f;
        float y = metrics.ascender;
        float max_x = 0.0f;
        int line_count = 1;

        const char* ptr = text;
        const char* end = text + text_length;
        const char* line_start = text;
        const char* word_start = text;
        int line_glyph_start = 0;
        int word_glyph_start = 0;

        while (ptr < end) {
            uint32_t codepoint;
            int bytes = utf8_to_codepoint(ptr, &codepoint);
            if (bytes == 0) {
                ptr++;
                continue;
            }

            // Handle newline
            if (codepoint == '\n') {
                if (x > max_x) max_x = x;
                x = 0.0f;
                y += line_height;
                line_count++;
                ptr += bytes;
                line_start = ptr;
                word_start = ptr;
                line_glyph_start = static_cast<int>(out_glyphs.size());
                word_glyph_start = static_cast<int>(out_glyphs.size());
                continue;
            }

            // Track word boundaries
            if (codepoint == ' ' || codepoint == '\t') {
                word_start = ptr + bytes;
                word_glyph_start = static_cast<int>(out_glyphs.size()) + 1;
            }

            // Resolve font for this codepoint
            uint32_t glyph_index = 0;
            uint16_t font_index = 0;
            IFontFace* glyph_font = font;
            if (fallback_chain_) {
                int fi = fallback_chain_->resolve(codepoint, &glyph_index);
                font_index = static_cast<uint16_t>(fi);
                IFontFace* fb = fallback_chain_->get_font(fi);
                if (fb) glyph_font = fb;
            } else {
                glyph_index = font->get_glyph_index(codepoint);
            }

            GlyphMetrics glyph_metrics;
            glyph_font->get_glyph_metrics(glyph_index, &glyph_metrics);

            float advance = glyph_metrics.advance_x + options.letter_spacing;
            if (codepoint == ' ') advance += options.word_spacing;

            int glyph_count = static_cast<int>(out_glyphs.size());

            // Check for wrap
            if (options.max_width > 0.0f && x + advance > options.max_width && x > 0.0f) {
                // Wrap at word boundary if possible
                if (options.wrap_words && word_glyph_start > line_glyph_start) {
                    // Reposition glyphs from word start to new line
                    float wrap_x = out_glyphs[word_glyph_start].x;
                    for (int i = word_glyph_start; i < glyph_count; ++i) {
                        out_glyphs[i].x -= wrap_x;
                        out_glyphs[i].y += line_height;
                    }
                    x -= wrap_x;
                } else {
                    x = 0.0f;
                }

                if (x > max_x) max_x = x;
                y += line_height;
                line_count++;
                line_glyph_start = glyph_count;
                word_glyph_start = glyph_count;

                // Check height limit
                if (options.max_height > 0.0f && y > options.max_height) {
                    result.truncated = true;
                    break;
                }
            }

            PositionedGlyph pg;
            pg.codepoint = codepoint;
            pg.glyph_index = glyph_index;
            pg.x = x;
            pg.y = y;
            pg.advance = glyph_metrics.advance_x;
            pg.cluster = result.char_count;
            pg.font_index = font_index;
            out_glyphs.push_back(pg);

            x += advance;
            ptr += bytes;
            result.char_count++;
        }

        if (x > max_x) max_x = x;

        result.bounds = window::math::make_box(0, 0, max_x, y + (metrics.line_height - metrics.ascender));
        result.line_count = line_count;

        return result;
    }

    Vec2 measure_text(IFontFace* font, const char* text, int text_length,
                      const TextLayoutOptions& options) override {
        std::vector<PositionedGlyph> glyphs;
        TextLayoutResult result = layout_text(font, text, text_length, glyphs, options);
        return Vec2(window::math::box_width(result.bounds), window::math::box_height(result.bounds));
    }

    void get_caret_positions(IFontFace* font, const char* text, int text_length,
                              std::vector<float>& out_positions) override {
        out_positions.clear();
        if (!font || !text) return;

        if (text_length < 0) text_length = static_cast<int>(strlen(text));

        float x = 0.0f;
        const char* ptr = text;
        const char* end = text + text_length;

        // First caret position at start
        out_positions.push_back(x);

        while (ptr < end) {
            uint32_t codepoint;
            int bytes = utf8_to_codepoint(ptr, &codepoint);
            if (bytes == 0) {
                ptr++;
                continue;
            }

            uint32_t glyph_index = font->get_glyph_index(codepoint);
            GlyphMetrics metrics;
            font->get_glyph_metrics(glyph_index, &metrics);

            x += metrics.advance_x;
            out_positions.push_back(x);

            ptr += bytes;
        }
    }

    int hit_test(IFontFace* font, const char* text, int text_length,
                 float x, float y, const TextLayoutOptions& options) override {
        (void)y; // TODO: multi-line support

        if (!font || !text) return -1;
        if (text_length < 0) text_length = static_cast<int>(strlen(text));

        float current_x = 0.0f;
        const char* ptr = text;
        const char* end = text + text_length;
        int index = 0;

        while (ptr < end) {
            uint32_t codepoint;
            int bytes = utf8_to_codepoint(ptr, &codepoint);
            if (bytes == 0) {
                ptr++;
                continue;
            }

            uint32_t glyph_index = font->get_glyph_index(codepoint);
            GlyphMetrics metrics;
            font->get_glyph_metrics(glyph_index, &metrics);

            float mid = current_x + metrics.advance_x / 2.0f;
            if (x < mid) {
                return index;
            }

            current_x += metrics.advance_x + options.letter_spacing;
            if (codepoint == ' ') current_x += options.word_spacing;

            ptr += bytes;
            index++;
        }

        return index;
    }

private:
    IFontLibrary* library_;
    IFontFallbackChain* fallback_chain_ = nullptr;
};

#ifdef FONT_SUPPORT_HARFBUZZ
// Defined in font_harfbuzz.cpp
ITextShaper* create_harfbuzz_text_shaper(IFontLibrary* library, Result* out_result);
#endif

ITextShaper* create_text_shaper(IFontLibrary* library, Result* out_result) {
    if (!library) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

#ifdef FONT_SUPPORT_HARFBUZZ
    return create_harfbuzz_text_shaper(library, out_result);
#else
    if (out_result) *out_result = Result::Success;
    return new SimpleTextShaper(library);
#endif
}

void destroy_text_shaper(ITextShaper* shaper) {
    delete shaper;
}

// ============================================================================
// Simple Font Renderer Implementation
// ============================================================================

class SimpleFontRenderer : public IFontRenderer {
public:
    SimpleFontRenderer(IFontLibrary* library) : library_(library) {
        shaper_ = create_text_shaper(library, nullptr);
    }

    ~SimpleFontRenderer() {
        if (shaper_) {
            destroy_text_shaper(shaper_);
        }
    }

    void set_fallback_chain(IFontFallbackChain* chain) override {
        fallback_chain_ = chain;
        if (shaper_) shaper_->set_fallback_chain(chain);
    }

    Result render_text(IFontFace* font, const char* text, int text_length,
                        const Vec4& color, const RenderOptions& render_opts,
                        const TextLayoutOptions& layout_opts,
                        void** out_pixels, int* out_width, int* out_height,
                        PixelFormat* out_format) override {
        if (!font || !text || !out_pixels || !out_width || !out_height) {
            return Result::ErrorInvalidParameter;
        }

        if (text_length < 0) text_length = static_cast<int>(strlen(text));
        if (text_length == 0) {
            *out_pixels = nullptr;
            *out_width = 0;
            *out_height = 0;
            if (out_format) *out_format = render_opts.output_format;
            return Result::Success;
        }

        // Layout text to get positioned glyphs
        std::vector<PositionedGlyph> glyphs;
        TextLayoutResult layout = shaper_->layout_text(font, text, text_length, glyphs, layout_opts);

        if (glyphs.empty()) {
            *out_pixels = nullptr;
            *out_width = 0;
            *out_height = 0;
            if (out_format) *out_format = render_opts.output_format;
            return Result::Success;
        }

        int glyph_count = static_cast<int>(glyphs.size());

        // Calculate bitmap dimensions
        int bitmap_width = static_cast<int>(std::ceil(window::math::box_width(layout.bounds))) + 2;
        int bitmap_height = static_cast<int>(std::ceil(window::math::box_height(layout.bounds))) + 2;

        if (bitmap_width <= 0 || bitmap_height <= 0) {
            *out_pixels = nullptr;
            *out_width = 0;
            *out_height = 0;
            if (out_format) *out_format = render_opts.output_format;
            return Result::Success;
        }

        // Determine bytes per pixel based on output format
        int bpp = get_bytes_per_pixel(render_opts.output_format);
        int pitch = bitmap_width * bpp;

        // Allocate bitmap
        void* pixels = calloc(1, pitch * bitmap_height);
        if (!pixels) {
            return Result::ErrorOutOfMemory;
        }

        // Render glyphs to bitmap
        Result result = render_glyphs(font, glyphs.data(), glyph_count,
                                       color, render_opts,
                                       pixels, bitmap_width, bitmap_height, pitch,
                                       render_opts.output_format, 0, 0);

        if (result != Result::Success) {
            free(pixels);
            return result;
        }

        // Convert premultiplied alpha to straight alpha so that the output
        // works correctly with standard GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA
        // blending.  blend_glyph() composites using premultiplied-over, which
        // is correct for layering glyphs, but consumers expect straight alpha.
        premultiplied_to_straight(pixels, bitmap_width, bitmap_height, pitch,
                                  render_opts.output_format);

        *out_pixels = pixels;
        *out_width = bitmap_width;
        *out_height = bitmap_height;
        if (out_format) *out_format = render_opts.output_format;

        return Result::Success;
    }

    Result render_text_to_bitmap(IFontFace* font, const char* text, int text_length,
                                  const Vec4& color, const RenderOptions& render_opts,
                                  const TextLayoutOptions& layout_opts,
                                  void* bitmap, int bitmap_width, int bitmap_height,
                                  int bitmap_pitch, PixelFormat bitmap_format,
                                  int x, int y) override {
        if (!font || !text || !bitmap) {
            return Result::ErrorInvalidParameter;
        }

        if (text_length < 0) text_length = static_cast<int>(strlen(text));
        if (text_length == 0) return Result::Success;

        // Layout text
        std::vector<PositionedGlyph> glyphs;
        TextLayoutResult layout = shaper_->layout_text(font, text, text_length,
                                                        glyphs, layout_opts);

        if (layout.char_count <= 0) return Result::Success;

        return render_glyphs(font, glyphs.data(), static_cast<int>(glyphs.size()),
                             color, render_opts,
                             bitmap, bitmap_width, bitmap_height, bitmap_pitch,
                             bitmap_format, x, y);
    }

    Result render_glyphs(IFontFace* font, const PositionedGlyph* glyphs, int glyph_count,
                          const Vec4& color, const RenderOptions& render_opts,
                          void* bitmap, int bitmap_width, int bitmap_height,
                          int bitmap_pitch, PixelFormat bitmap_format,
                          int x, int y) override {
        if (!font || !glyphs || !bitmap || glyph_count <= 0) {
            return Result::ErrorInvalidParameter;
        }

        for (int i = 0; i < glyph_count; ++i) {
            const PositionedGlyph& pg = glyphs[i];

            // Skip whitespace
            if (pg.codepoint == ' ' || pg.codepoint == '\t' || pg.codepoint == '\n') {
                continue;
            }

            // Select the correct font from fallback chain (if available)
            IFontFace* glyph_font = font;
            if (fallback_chain_ && pg.font_index > 0) {
                IFontFace* fb = fallback_chain_->get_font(pg.font_index);
                if (fb) glyph_font = fb;
            }

            // Render glyph
            GlyphBitmap glyph_bitmap;
            Result result = glyph_font->render_glyph(pg.glyph_index, render_opts, &glyph_bitmap);
            if (result != Result::Success) {
                continue; // Skip failed glyphs
            }

            if (!glyph_bitmap.pixels || glyph_bitmap.width <= 0 || glyph_bitmap.height <= 0) {
                continue;
            }

            // Calculate destination position
            int dst_x = x + static_cast<int>(pg.x + glyph_bitmap.metrics.bearing_x);
            int dst_y = y + static_cast<int>(pg.y - glyph_bitmap.metrics.bearing_y);

            // Blend glyph into destination bitmap
            blend_glyph(bitmap, bitmap_width, bitmap_height, bitmap_pitch, bitmap_format,
                        dst_x, dst_y, &glyph_bitmap, color);
        }

        return Result::Success;
    }

    void free_bitmap(void* pixels) override {
        free(pixels);
    }

    // ========================================================================
    // Texture Rendering Implementation
    // ========================================================================

    Result render_text_to_texture(IFontFace* font, const char* text, int text_length,
                                   const Vec4& color, const RenderOptions& render_opts,
                                   const TextLayoutOptions& layout_opts,
                                   window::TextureFormat texture_format,
                                   const TextureCreateCallback& create_callback,
                                   const TextureUploadCallback& upload_callback,
                                   TextureDesc* out_desc) override {
        if (!font || !text || !create_callback || !upload_callback || !out_desc) {
            return Result::ErrorInvalidParameter;
        }

        // Determine pixel format based on texture format
        PixelFormat pixel_format = texture_format_to_pixel_format(texture_format);
        RenderOptions opts = render_opts;
        opts.output_format = pixel_format;

        // Render text to CPU bitmap
        void* pixels = nullptr;
        int width = 0;
        int height = 0;
        PixelFormat out_pixel_format;

        Result result = render_text(font, text, text_length, color, opts, layout_opts,
                                     &pixels, &width, &height, &out_pixel_format);

        if (result != Result::Success) {
            return result;
        }

        // Handle empty text
        if (!pixels || width == 0 || height == 0) {
            out_desc->handle = nullptr;
            out_desc->view = nullptr;
            out_desc->width = 0;
            out_desc->height = 0;
            out_desc->format = texture_format;
            return Result::Success;
        }

        // Create texture via callback
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = texture_format;

        if (!create_callback(width, height, texture_format, &desc)) {
            free(pixels);
            return Result::ErrorRenderFailed;
        }

        // Upload pixels via callback
        int bpp = get_bytes_per_pixel(out_pixel_format);
        int pitch = width * bpp;

        if (!upload_callback(&desc, pixels, width, height, pitch)) {
            free(pixels);
            return Result::ErrorRenderFailed;
        }

        // Free CPU bitmap
        free(pixels);

        // Return texture descriptor
        *out_desc = desc;

        return Result::Success;
    }

    Result render_glyphs_to_texture(IFontFace* font,
                                     const PositionedGlyph* glyphs, int glyph_count,
                                     const Vec4& color, const RenderOptions& render_opts,
                                     window::TextureFormat texture_format,
                                     const TextureCreateCallback& create_callback,
                                     const TextureUploadCallback& upload_callback,
                                     TextureDesc* out_desc) override {
        if (!font || !glyphs || glyph_count <= 0 || !create_callback || !upload_callback || !out_desc) {
            return Result::ErrorInvalidParameter;
        }

        // Calculate bounds from positioned glyphs
        float min_x = 0.0f, min_y = 0.0f;
        float max_x = 0.0f, max_y = 0.0f;
        bool first = true;

        const FontMetrics& metrics = font->get_metrics();

        for (int i = 0; i < glyph_count; ++i) {
            const PositionedGlyph& pg = glyphs[i];

            // Skip whitespace
            if (pg.codepoint == ' ' || pg.codepoint == '\t' || pg.codepoint == '\n') {
                continue;
            }

            // Select the correct font from fallback chain
            IFontFace* glyph_font = font;
            if (fallback_chain_ && pg.font_index > 0) {
                IFontFace* fb = fallback_chain_->get_font(pg.font_index);
                if (fb) glyph_font = fb;
            }

            GlyphMetrics glyph_metrics;
            if (!glyph_font->get_glyph_metrics(pg.glyph_index, &glyph_metrics)) {
                continue;
            }

            float glyph_left = pg.x + glyph_metrics.bearing_x;
            float glyph_right = glyph_left + glyph_metrics.width;
            float glyph_top = pg.y - glyph_metrics.bearing_y;
            float glyph_bottom = glyph_top + glyph_metrics.height;

            if (first) {
                min_x = glyph_left;
                min_y = glyph_top;
                max_x = glyph_right;
                max_y = glyph_bottom;
                first = false;
            } else {
                if (glyph_left < min_x) min_x = glyph_left;
                if (glyph_top < min_y) min_y = glyph_top;
                if (glyph_right > max_x) max_x = glyph_right;
                if (glyph_bottom > max_y) max_y = glyph_bottom;
            }
        }

        // Calculate bitmap dimensions
        int width = static_cast<int>(std::ceil(max_x - min_x)) + 2;
        int height = static_cast<int>(std::ceil(max_y - min_y)) + 2;

        if (width <= 0 || height <= 0) {
            out_desc->handle = nullptr;
            out_desc->view = nullptr;
            out_desc->width = 0;
            out_desc->height = 0;
            out_desc->format = texture_format;
            return Result::Success;
        }

        // Determine pixel format
        PixelFormat pixel_format = texture_format_to_pixel_format(texture_format);
        int bpp = get_bytes_per_pixel(pixel_format);
        int pitch = width * bpp;

        // Allocate bitmap
        void* pixels = calloc(1, pitch * height);
        if (!pixels) {
            return Result::ErrorOutOfMemory;
        }

        // Adjust glyphs to local coordinates and render
        RenderOptions opts = render_opts;
        opts.output_format = pixel_format;

        // Create adjusted glyphs with local coordinates
        std::vector<PositionedGlyph> local_glyphs(glyph_count);
        for (int i = 0; i < glyph_count; ++i) {
            local_glyphs[i] = glyphs[i];
            local_glyphs[i].x -= min_x;
            local_glyphs[i].y -= min_y;
        }

        Result result = render_glyphs(font, local_glyphs.data(), glyph_count,
                                       color, opts,
                                       pixels, width, height, pitch, pixel_format,
                                       1, 1); // 1px padding

        if (result != Result::Success) {
            free(pixels);
            return result;
        }

        // Convert premultiplied alpha to straight alpha
        premultiplied_to_straight(pixels, width, height, pitch, pixel_format);

        // Create texture via callback
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = texture_format;

        if (!create_callback(width, height, texture_format, &desc)) {
            free(pixels);
            return Result::ErrorRenderFailed;
        }

        // Upload pixels via callback
        if (!upload_callback(&desc, pixels, width, height, pitch)) {
            free(pixels);
            return Result::ErrorRenderFailed;
        }

        // Free CPU bitmap
        free(pixels);

        // Return texture descriptor
        *out_desc = desc;

        return Result::Success;
    }

private:
    IFontLibrary* library_;
    ITextShaper* shaper_;
    IFontFallbackChain* fallback_chain_ = nullptr;

    // Convert a premultiplied-alpha bitmap to straight alpha in-place.
    static void premultiplied_to_straight(void* bitmap, int width, int height,
                                           int pitch, PixelFormat format) {
        if (!bitmap || width <= 0 || height <= 0) return;

        // Only RGBA8 and BGRA8 have separate color + alpha channels
        if (format != PixelFormat::RGBA8 && format != PixelFormat::BGRA8) return;

        uint8_t* data = static_cast<uint8_t*>(bitmap);
        for (int y = 0; y < height; ++y) {
            uint8_t* row = data + y * pitch;
            for (int x = 0; x < width; ++x) {
                uint8_t* px = row + x * 4;
                uint8_t a = px[3];
                if (a == 0) continue;       // fully transparent — leave as zeros
                if (a == 255) continue;      // fully opaque — no change needed
                // Un-premultiply: C_straight = C_premul * 255 / A
                px[0] = static_cast<uint8_t>(std::min(255, px[0] * 255 / a));
                px[1] = static_cast<uint8_t>(std::min(255, px[1] * 255 / a));
                px[2] = static_cast<uint8_t>(std::min(255, px[2] * 255 / a));
            }
        }
    }

    // Get bytes per pixel for a pixel format
    static int get_bytes_per_pixel(PixelFormat format) {
        switch (format) {
            case PixelFormat::A8:    return 1;
            case PixelFormat::RGB8:
            case PixelFormat::BGR8:  return 3;
            case PixelFormat::RGBA8:
            case PixelFormat::BGRA8: return 4;
            default:                 return 1;
        }
    }

    // Convert texture format to internal pixel format
    static PixelFormat texture_format_to_pixel_format(window::TextureFormat format) {
        switch (format) {
            case window::TextureFormat::R8_UNORM:
            case window::TextureFormat::A8_UNORM:
            case window::TextureFormat::L8_UNORM:
                return PixelFormat::A8;

            case window::TextureFormat::RGBA8_UNORM:
            case window::TextureFormat::RGBA8_UNORM_SRGB:
                return PixelFormat::RGBA8;

            case window::TextureFormat::BGRA8_UNORM:
            case window::TextureFormat::BGRA8_UNORM_SRGB:
                return PixelFormat::BGRA8;

            default:
                return PixelFormat::RGBA8;
        }
    }

    // Blend a glyph bitmap into the destination
    void blend_glyph(void* dst_bitmap, int dst_width, int dst_height, int dst_pitch,
                     PixelFormat dst_format, int dst_x, int dst_y,
                     const GlyphBitmap* glyph, const Vec4& color) {
        if (!glyph || !glyph->pixels) return;

        // Calculate clipping
        int src_x = 0, src_y = 0;
        int copy_width = glyph->width;
        int copy_height = glyph->height;

        // Clip left
        if (dst_x < 0) {
            src_x = -dst_x;
            copy_width += dst_x;
            dst_x = 0;
        }

        // Clip top
        if (dst_y < 0) {
            src_y = -dst_y;
            copy_height += dst_y;
            dst_y = 0;
        }

        // Clip right
        if (dst_x + copy_width > dst_width) {
            copy_width = dst_width - dst_x;
        }

        // Clip bottom
        if (dst_y + copy_height > dst_height) {
            copy_height = dst_height - dst_y;
        }

        if (copy_width <= 0 || copy_height <= 0) return;

        // Get destination bytes per pixel
        int dst_bpp = get_bytes_per_pixel(dst_format);
        int src_bpp = get_bytes_per_pixel(glyph->format);

        uint8_t* dst = static_cast<uint8_t*>(dst_bitmap);
        const uint8_t* src = static_cast<const uint8_t*>(glyph->pixels);

        for (int y = 0; y < copy_height; ++y) {
            uint8_t* dst_row = dst + (dst_y + y) * dst_pitch + dst_x * dst_bpp;
            const uint8_t* src_row = src + (src_y + y) * glyph->pitch + src_x * src_bpp;

            for (int x = 0; x < copy_width; ++x) {
                // Get source alpha
                uint8_t alpha = 0;
                if (glyph->format == PixelFormat::A8) {
                    alpha = src_row[x];
                } else if (glyph->format == PixelFormat::RGBA8) {
                    alpha = src_row[x * 4 + 3];
                } else if (glyph->format == PixelFormat::BGRA8) {
                    alpha = src_row[x * 4 + 3];
                } else if (glyph->format == PixelFormat::RGB8 || glyph->format == PixelFormat::BGR8) {
                    // Use luminance as alpha for subpixel
                    alpha = src_row[x * 3];
                }

                if (alpha == 0) continue;

                // Blend into destination
                uint8_t* dst_pixel = dst_row + x * dst_bpp;

                if (dst_format == PixelFormat::A8) {
                    // Alpha-only destination
                    uint8_t src_a = static_cast<uint8_t>(alpha * color.w);
                    uint8_t dst_a = dst_pixel[0];
                    dst_pixel[0] = dst_a + ((255 - dst_a) * src_a) / 255;
                }
                else if (dst_format == PixelFormat::RGBA8) {
                    blend_pixel_rgba(dst_pixel, color, alpha);
                }
                else if (dst_format == PixelFormat::BGRA8) {
                    blend_pixel_bgra(dst_pixel, color, alpha);
                }
                else if (dst_format == PixelFormat::RGB8) {
                    blend_pixel_rgb(dst_pixel, color, alpha);
                }
                else if (dst_format == PixelFormat::BGR8) {
                    blend_pixel_bgr(dst_pixel, color, alpha);
                }
            }
        }
    }

    // Blend RGBA pixel (color is Vec4 with floats 0-1: x=r, y=g, z=b, w=a)
    static void blend_pixel_rgba(uint8_t* dst, const Vec4& color, uint8_t alpha) {
        uint8_t cr = static_cast<uint8_t>(color.x * 255.0f);
        uint8_t cg = static_cast<uint8_t>(color.y * 255.0f);
        uint8_t cb = static_cast<uint8_t>(color.z * 255.0f);
        uint8_t src_a = static_cast<uint8_t>(alpha * color.w);
        if (src_a == 0) return;

        uint8_t inv_alpha = 255 - src_a;
        dst[0] = (cr * src_a + dst[0] * inv_alpha) / 255;
        dst[1] = (cg * src_a + dst[1] * inv_alpha) / 255;
        dst[2] = (cb * src_a + dst[2] * inv_alpha) / 255;
        dst[3] = src_a + (dst[3] * inv_alpha) / 255;
    }

    // Blend BGRA pixel
    static void blend_pixel_bgra(uint8_t* dst, const Vec4& color, uint8_t alpha) {
        uint8_t cr = static_cast<uint8_t>(color.x * 255.0f);
        uint8_t cg = static_cast<uint8_t>(color.y * 255.0f);
        uint8_t cb = static_cast<uint8_t>(color.z * 255.0f);
        uint8_t src_a = static_cast<uint8_t>(alpha * color.w);
        if (src_a == 0) return;

        uint8_t inv_alpha = 255 - src_a;
        dst[0] = (cb * src_a + dst[0] * inv_alpha) / 255;
        dst[1] = (cg * src_a + dst[1] * inv_alpha) / 255;
        dst[2] = (cr * src_a + dst[2] * inv_alpha) / 255;
        dst[3] = src_a + (dst[3] * inv_alpha) / 255;
    }

    // Blend RGB pixel (no alpha channel)
    static void blend_pixel_rgb(uint8_t* dst, const Vec4& color, uint8_t alpha) {
        uint8_t cr = static_cast<uint8_t>(color.x * 255.0f);
        uint8_t cg = static_cast<uint8_t>(color.y * 255.0f);
        uint8_t cb = static_cast<uint8_t>(color.z * 255.0f);
        uint8_t src_a = static_cast<uint8_t>(alpha * color.w);
        if (src_a == 0) return;

        uint8_t inv_alpha = 255 - src_a;
        dst[0] = (cr * src_a + dst[0] * inv_alpha) / 255;
        dst[1] = (cg * src_a + dst[1] * inv_alpha) / 255;
        dst[2] = (cb * src_a + dst[2] * inv_alpha) / 255;
    }

    // Blend BGR pixel (no alpha channel)
    static void blend_pixel_bgr(uint8_t* dst, const Vec4& color, uint8_t alpha) {
        uint8_t cr = static_cast<uint8_t>(color.x * 255.0f);
        uint8_t cg = static_cast<uint8_t>(color.y * 255.0f);
        uint8_t cb = static_cast<uint8_t>(color.z * 255.0f);
        uint8_t src_a = static_cast<uint8_t>(alpha * color.w);
        if (src_a == 0) return;

        uint8_t inv_alpha = 255 - src_a;
        dst[0] = (cb * src_a + dst[0] * inv_alpha) / 255;
        dst[1] = (cg * src_a + dst[1] * inv_alpha) / 255;
        dst[2] = (cr * src_a + dst[2] * inv_alpha) / 255;
    }
};

IFontRenderer* create_font_renderer(IFontLibrary* library, Result* out_result) {
    if (!library) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return new SimpleFontRenderer(library);
}

void destroy_font_renderer(IFontRenderer* renderer) {
    delete renderer;
}

// ============================================================================
// Font Fallback Chain Implementation
// ============================================================================

class FallbackFontChain : public IFontFallbackChain {
public:
    explicit FallbackFontChain(IFontFace* primary) {
        if (primary) fonts_.push_back(primary);
    }

    IFontFace* get_primary_font() const override {
        return fonts_.empty() ? nullptr : fonts_[0];
    }

    IFontFace* get_font(int index) const override {
        if (index < 0 || index >= static_cast<int>(fonts_.size())) return nullptr;
        return fonts_[index];
    }

    int get_font_count() const override {
        return static_cast<int>(fonts_.size());
    }

    void add_fallback(IFontFace* font) override {
        if (font) fonts_.push_back(font);
    }

    int resolve(uint32_t codepoint, uint32_t* out_glyph_index) const override {
        for (int i = 0; i < static_cast<int>(fonts_.size()); ++i) {
            uint32_t gi = fonts_[i]->get_glyph_index(codepoint);
            if (gi != 0) {
                if (out_glyph_index) *out_glyph_index = gi;
                return i;
            }
        }
        // No font has this glyph — return primary font's result (glyph 0)
        if (out_glyph_index) *out_glyph_index = 0;
        return 0;
    }

    bool has_glyph(uint32_t codepoint) const override {
        for (auto* f : fonts_) {
            if (f->has_glyph(codepoint)) return true;
        }
        return false;
    }

private:
    std::vector<IFontFace*> fonts_;
};

IFontFallbackChain* create_fallback_chain(IFontFace* primary) {
    return new FallbackFontChain(primary);
}

void destroy_fallback_chain(IFontFallbackChain* chain) {
    delete chain;
}

// Platform-specific fallback font family lists.
// Each list is ordered by priority — common UI fonts first, then script-specific.

// The list aims to cover every major Unicode script on each platform. Families
// that are not installed resolve (via the system font matcher) to an already
// loaded font and are de-duplicated, so listing many is cheap.
#ifdef _WIN32
static const char* const g_fallback_families[] = {
    // Latin / Cyrillic / Greek
    "Segoe UI", "Arial", "Tahoma", "Times New Roman",
    // CJK (Simplified, Traditional, Japanese, Korean)
    "Microsoft YaHei", "Microsoft JhengHei", "Yu Gothic", "Meiryo",
    "Malgun Gothic", "MingLiU", "SimSun",
    // Arabic / Hebrew
    "Arabic Typesetting", "Traditional Arabic", "David",
    // Indic (Nirmala UI covers all Indic scripts)
    "Nirmala UI", "Mangal", "Latha", "Vrinda",
    // South-East Asian
    "Leelawadee UI",    // Thai
    "Khmer UI",         // Khmer
    "Lao UI",           // Lao
    "Myanmar Text",     // Myanmar
    // Other scripts
    "Sylfaen",          // Armenian, Georgian
    "Nyala",            // Ethiopic
    "Gadugi",           // Cherokee, Canadian Aboriginal
    "Microsoft Himalaya", // Tibetan
    "Mongolian Baiti",  // Mongolian
    "MV Boli",          // Thaana
    "Ebrima",           // N'Ko, Tifinagh, Vai, Osmanya
    "Microsoft Yi Baiti", // Yi
    "Microsoft Tai Le", "Microsoft New Tai Lue",
    "Microsoft PhagsPa", "Javanese Text",
    // Emoji / symbols
    "Segoe UI Emoji", "Segoe UI Symbol", "Segoe UI Historic",
    nullptr
};
#elif defined(__APPLE__)
static const char* const g_fallback_families[] = {
    "Helvetica Neue", ".AppleSystemUIFont", "Arial",
    // CJK
    "PingFang SC", "PingFang TC", "Hiragino Sans",
    "Hiragino Kaku Gothic ProN", "Apple SD Gothic Neo",
    // Arabic / Hebrew
    "Geeza Pro", "Arial Hebrew",
    // Indic
    "Devanagari Sangam MN", "Kohinoor Devanagari", "Bangla Sangam MN",
    "Tamil Sangam MN", "Telugu Sangam MN", "Kannada Sangam MN",
    "Malayalam Sangam MN", "Gujarati Sangam MN", "Gurmukhi MN",
    "Oriya Sangam MN", "Sinhala Sangam MN",
    // South-East Asian
    "Thonburi",             // Thai
    "Khmer Sangam MN", "Lao Sangam MN", "Myanmar Sangam MN",
    // Other scripts
    "Mshtakan",             // Armenian
    "Kefa",                 // Ethiopic
    "Plantagenet Cherokee", // Cherokee
    "Euphemia UCAS",        // Canadian Aboriginal
    "Kailasa",              // Tibetan
    // Emoji / symbols
    "Apple Color Emoji", "Apple Symbols",
    nullptr
};
#elif defined(__linux__)
// The Noto family covers essentially every script; unavailable members are
// de-duplicated away after the matcher substitutes them.
static const char* const g_fallback_families[] = {
    "DejaVu Sans", "Noto Sans", "Liberation Sans", "FreeSans",
    // CJK
    "Noto Sans CJK SC", "Noto Sans CJK TC", "Noto Sans CJK JP",
    "Noto Sans CJK KR", "Noto Sans CJK HK", "WenQuanYi Micro Hei",
    // Middle Eastern
    "Noto Sans Arabic", "Noto Naskh Arabic", "Noto Sans Hebrew",
    "Noto Sans Syriac",
    // Indic
    "Noto Sans Devanagari", "Noto Sans Bengali", "Noto Sans Tamil",
    "Noto Sans Telugu", "Noto Sans Kannada", "Noto Sans Malayalam",
    "Noto Sans Gujarati", "Noto Sans Gurmukhi", "Noto Sans Oriya",
    "Noto Sans Sinhala",
    // South-East Asian
    "Noto Sans Thai", "Noto Sans Lao", "Noto Sans Khmer",
    "Noto Sans Myanmar", "Noto Sans Javanese",
    // Other scripts
    "Noto Sans Armenian", "Noto Sans Georgian", "Noto Sans Ethiopic",
    "Noto Sans Cherokee", "Noto Sans Canadian Aboriginal",
    "Noto Sans Tibetan", "Noto Sans Mongolian", "Noto Sans Thaana",
    "Noto Sans Tifinagh", "Noto Sans Vai", "Noto Sans Adlam", "Noto Sans Yi",
    // Emoji / symbols
    "Noto Color Emoji", "Noto Emoji", "Noto Sans Symbols", "Noto Sans Symbols2",
    nullptr
};
#elif defined(__ANDROID__)
static const char* const g_fallback_families[] = {
    "Roboto", "Noto Sans",
    // CJK
    "Noto Sans CJK", "Noto Sans CJK SC", "Noto Sans CJK JP", "Noto Sans CJK KR",
    // Middle Eastern
    "Noto Sans Arabic", "Noto Sans Hebrew",
    // Indic
    "Noto Sans Devanagari", "Noto Sans Bengali", "Noto Sans Tamil",
    "Noto Sans Telugu", "Noto Sans Kannada", "Noto Sans Malayalam",
    "Noto Sans Gujarati", "Noto Sans Gurmukhi", "Noto Sans Sinhala",
    // South-East Asian
    "Noto Sans Thai", "Noto Sans Lao", "Noto Sans Khmer", "Noto Sans Myanmar",
    // Other scripts
    "Noto Sans Armenian", "Noto Sans Georgian", "Noto Sans Ethiopic",
    // Emoji
    "Noto Color Emoji",
    nullptr
};
#else
static const char* const g_fallback_families[] = {
    "Arial", "DejaVu Sans", "Noto Sans",
    nullptr
};
#endif

IFontFallbackChain* create_fallback_chain_with_defaults(IFontLibrary* library,
                                                         IFontFace* primary,
                                                         float size) {
    FallbackFontChain* chain = new FallbackFontChain(primary);
    if (!library) return chain;

    // Track which families we've already loaded (including primary)
    std::vector<std::string> loaded;
    if (primary) {
        loaded.push_back(primary->get_family_name());
    }

    for (int i = 0; g_fallback_families[i] != nullptr; ++i) {
        const char* family = g_fallback_families[i];

        // Skip if the requested family was already loaded
        bool skip = false;
        for (auto& l : loaded) {
            if (l == family) { skip = true; break; }
        }
        if (skip) continue;

        FontDescriptor desc = FontDescriptor::create(family, size);
        Result r;
        IFontFace* face = library->load_system_font(desc, &r);
        if (!face) continue;

        // Also dedup by *actual* family name returned by the loaded face — on
        // Linux several requested families (Noto Sans CJK SC/TC/JP/KR) often
        // resolve to the same physical font (face 0 of NotoSansCJK-Regular.ttc).
        // Without this check we'd load the same face 4× and burn atlas slots.
        const char* loaded_name = face->get_family_name();
        bool dup = false;
        if (loaded_name && loaded_name[0]) {
            for (auto& l : loaded) {
                if (l == loaded_name) { dup = true; break; }
            }
        }
        if (dup) {
            library->destroy_font(face);
            continue;
        }

        chain->add_fallback(face);
        loaded.push_back(family);
        if (loaded_name && loaded_name[0] && std::string(loaded_name) != family) {
            loaded.push_back(loaded_name);
        }
    }

    return chain;
}

// ============================================================================
// FontAtlasImpl — CPU-side shelf-packing, GPU ops via callbacks
// ============================================================================

class FontAtlasImpl : public IFontAtlas {
public:
    int        m_tile_w     = 4096;
    int        m_tile_h     = 4096;
    int        m_max_layers = 2048;
    int        m_depth      = 0;
    uintptr_t  m_handle     = 0;

    InitCallback    m_init_cb;
    GrowCallback    m_grow_cb;
    UploadCallback  m_upload_cb;
    DestroyCallback m_destroy_cb;

    struct Shelf { int cur_x = 0, shelf_y = 0, shelf_h = 0; };
    std::vector<Shelf> m_shelves;

    void set_callbacks(InitCallback    init_cb,
                       GrowCallback    grow_cb,
                       UploadCallback  upload_cb,
                       DestroyCallback destroy_cb) override {
        m_init_cb    = std::move(init_cb);
        m_grow_cb    = std::move(grow_cb);
        m_upload_cb  = std::move(upload_cb);
        m_destroy_cb = std::move(destroy_cb);
    }

    void set_tile_size(int tile_w, int tile_h) override {
        m_tile_w = tile_w; m_tile_h = tile_h;
    }

    void set_max_layers(int max_layers) override {
        m_max_layers = max_layers;
    }

    AtlasEntry add(const void* rgba8, int w, int h) override {
        if (!rgba8 || w <= 0 || h <= 0 || w > m_tile_w || h > m_tile_h) return {};
        if (!m_init_cb || !m_grow_cb || !m_upload_cb) return {};

        if (m_depth == 0) grow_to(4);

        // Search existing layers for a shelf with room
        for (int li = 0; li < m_depth; li++) {
            Shelf& s = m_shelves[li];
            if (s.shelf_h > 0 && h <= s.shelf_h && s.cur_x + w <= m_tile_w) {
                AtlasEntry e = make_entry(li, s.cur_x, s.shelf_y, w, h);
                s.cur_x += w;
                do_upload(e, rgba8);
                return e;
            }
            // Try a new shelf below the current one
            int new_y = s.shelf_y + s.shelf_h;
            if (new_y + h <= m_tile_h) {
                s.shelf_y = new_y;
                s.shelf_h = h;
                s.cur_x   = w;
                AtlasEntry e = make_entry(li, 0, new_y, w, h);
                do_upload(e, rgba8);
                return e;
            }
        }

        // All layers full — grow
        if (m_depth >= m_max_layers) return {};
        int old_depth = m_depth;
        grow_to(std::min(m_depth * 2, m_max_layers));

        Shelf& s = m_shelves[old_depth];
        s.shelf_y = 0; s.shelf_h = h; s.cur_x = w;
        AtlasEntry e = make_entry(old_depth, 0, 0, w, h);
        do_upload(e, rgba8);
        return e;
    }

    void update(const AtlasEntry& entry, const void* rgba8) override {
        if (!entry.valid() || !rgba8 || !m_handle || !m_upload_cb) return;
        m_upload_cb(m_handle, rgba8, entry.px, entry.py, entry.layer, entry.pw, entry.ph);
    }

    uintptr_t get_gpu_handle() const override { return m_handle; }
    int get_tile_width()       const override { return m_tile_w; }
    int get_tile_height()      const override { return m_tile_h; }
    int get_layer_count()      const override { return m_depth;  }

    void clear() override {
        m_shelves.clear();
        m_shelves.resize(m_depth);
    }

    void destroy() override {
        if (m_handle && m_destroy_cb)
            m_destroy_cb(m_handle);
        m_handle = 0;
        m_depth  = 0;
        m_shelves.clear();
    }

private:
    AtlasEntry make_entry(int layer, int x, int y, int w, int h) const {
        AtlasEntry e;
        e.layer = layer;
        e.px = x; e.py = y; e.pw = w; e.ph = h;
        e.u0 = (float)x       / m_tile_w;
        e.v0 = (float)y       / m_tile_h;
        e.u1 = (float)(x + w) / m_tile_w;
        e.v1 = (float)(y + h) / m_tile_h;
        return e;
    }

    void do_upload(const AtlasEntry& e, const void* rgba8) {
        m_upload_cb(m_handle, rgba8, e.px, e.py, e.layer, e.pw, e.ph);
    }

    void grow_to(int new_depth) {
        if (new_depth <= m_depth) return;
        if (m_depth == 0) {
            m_handle = m_init_cb(m_tile_w, m_tile_h, new_depth);
        } else {
            m_handle = m_grow_cb(m_handle, m_tile_w, m_tile_h, m_depth, new_depth);
        }
        m_depth = new_depth;
        m_shelves.resize(new_depth);
    }
};

IFontAtlas* create_font_atlas() {
    return new FontAtlasImpl();
}

void destroy_font_atlas(IFontAtlas* atlas) {
    if (atlas) {
        atlas->destroy();
        delete atlas;
    }
}

// ============================================================================
// GlyphAtlasImpl — RAM-only, 8-bit, shelf-packed glyph atlas with GC + fallback
// ============================================================================

namespace {

// Quantise a point size for the cache key (1/4 px resolution).
inline uint32_t quantize_size(float size) {
    if (size < 0.0f) size = 0.0f;
    return static_cast<uint32_t>(std::lround(size * 4.0f));
}

// Faux-bold: horizontal max-dilation by `strength` px. Grows width by `strength`.
// `buf` holds an (w*h) A8 image with pitch == w; it is replaced in-place.
void synth_embolden(std::vector<uint8_t>& buf, int& w, int h, int strength) {
    if (strength <= 0 || w <= 0 || h <= 0) return;
    int nw = w + strength;
    std::vector<uint8_t> out(static_cast<size_t>(nw) * h, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* srow = buf.data() + static_cast<size_t>(y) * w;
        uint8_t* drow = out.data() + static_cast<size_t>(y) * nw;
        for (int x = 0; x < nw; ++x) {
            int v = 0;
            for (int k = 0; k <= strength; ++k) {
                int sx = x - k;
                if (sx >= 0 && sx < w) v = std::max(v, static_cast<int>(srow[sx]));
            }
            drow[x] = static_cast<uint8_t>(v);
        }
    }
    buf.swap(out);
    w = nw;
}

// Faux-italic: shear top rows to the right (lean). Grows width by ceil(shear*(h-1)).
void synth_oblique(std::vector<uint8_t>& buf, int& w, int h, float shear) {
    if (shear <= 0.0f || w <= 0 || h <= 0) return;
    int extra = static_cast<int>(std::ceil(shear * (h - 1)));
    if (extra <= 0) return;
    int nw = w + extra;
    std::vector<uint8_t> out(static_cast<size_t>(nw) * h, 0);
    for (int y = 0; y < h; ++y) {
        // Top row (y=0) shifts most; bottom row (y=h-1) not at all.
        int shift = static_cast<int>(std::lround(shear * (h - 1 - y)));
        const uint8_t* srow = buf.data() + static_cast<size_t>(y) * w;
        uint8_t* drow = out.data() + static_cast<size_t>(y) * nw;
        for (int x = 0; x < w; ++x) {
            int dx = x + shift;
            if (dx >= 0 && dx < nw) drow[dx] = srow[x];
        }
    }
    buf.swap(out);
    w = nw;
}

} // anonymous namespace

class GlyphAtlasImpl : public IGlyphAtlasManager {
public:
    GlyphAtlasImpl(IFontLibrary* library, const GlyphAtlasConfig& cfg)
        : library_(library), cfg_(cfg) {
        if (cfg_.layer_width  < 1) cfg_.layer_width  = 4096;
        if (cfg_.layer_height < 1) cfg_.layer_height = 4096;
        if (cfg_.max_layers   < 1) cfg_.max_layers   = 1;
        if (cfg_.initial_layers < 0) cfg_.initial_layers = 0;
        if (cfg_.padding < 0) cfg_.padding = 0;
        for (int i = 0; i < cfg_.initial_layers && i < cfg_.max_layers; ++i) add_layer();
    }

    ~GlyphAtlasImpl() override {
        delete chain_;
        for (auto& f : fonts_)
            if (f.owned && library_ && f.face) library_->destroy_font(f.face);
    }

    // ---- registry ----
    int add_font(IFontFace* face, bool take_ownership) override {
        if (!face) return -1;
        for (size_t i = 0; i < fonts_.size(); ++i)
            if (fonts_[i].face == face) return static_cast<int>(i);
        int idx = static_cast<int>(fonts_.size());
        FontEntry e;
        e.face = face;
        e.owned = take_ownership;
        const FontDescriptor& d = face->get_descriptor();
        e.intrinsic_bold   = static_cast<int>(d.weight) >= static_cast<int>(FontWeight::SemiBold);
        e.intrinsic_italic = d.style != FontStyle::Normal;
        fonts_.push_back(e);
        if (!chain_) chain_ = new FallbackFontChain(face);
        else         chain_->add_fallback(face);
        const char* fam = face->get_family_name();
        loaded_families_.push_back(fam ? fam : "");
        return idx;
    }

    int load_font_file(const char* path, int face_index) override {
        if (!library_ || !path) return -1;
        Result r;
        IFontFace* f = library_->load_font_file(path, face_index, &r);
        return f ? add_font(f, true) : -1;
    }

    int load_font_memory(const void* data, size_t size, int face_index) override {
        if (!library_ || !data) return -1;
        Result r;
        IFontFace* f = library_->load_font_memory(data, size, face_index, &r);
        return f ? add_font(f, true) : -1;
    }

    void add_system_font(const char* family, FontWeight weight, FontStyle style) override {
        if (!family || !family[0]) return;
        user_candidates_.push_back(Candidate{family, weight, style});
    }

    IFontFace* get_font(int font_idx) const override {
        if (font_idx < 0 || font_idx >= static_cast<int>(fonts_.size())) return nullptr;
        return fonts_[font_idx].face;
    }

    int font_count() const override { return static_cast<int>(fonts_.size()); }

    IFontFallbackChain* fallback_chain() override {
        populate_all_candidates();   // shaper needs a complete chain up front
        return chain_;
    }

    // ---- acquisition ----
    const GlyphSlot* acquire(int font_idx, uint32_t glyph_id, float size,
                             FontWeight weight, FontStyle style) override {
        if (font_idx < 0 || font_idx >= static_cast<int>(fonts_.size())) return nullptr;
        FontEntry& fe = fonts_[font_idx];

        uint8_t synth = 0;
        if (static_cast<int>(weight) >= static_cast<int>(FontWeight::Bold) && !fe.intrinsic_bold)
            synth |= kSynthBold;
        if (style != FontStyle::Normal && !fe.intrinsic_italic)
            synth |= kSynthItalic;

        Key key{static_cast<uint16_t>(font_idx), glyph_id, quantize_size(size), synth};
        auto it = glyphs_.find(key);
        if (it != glyphs_.end()) {
            it->second.last_frame = frame_;
            return &it->second.slot;
        }

        // Rasterise the glyph as 8-bit coverage.
        fe.face->set_size(size);
        RenderOptions ropts;
        ropts.antialias     = AntiAliasMode::Grayscale;
        ropts.hinting       = HintingMode::Normal;
        ropts.output_format = PixelFormat::A8;
        GlyphBitmap bmp;
        if (fe.face->render_glyph(glyph_id, ropts, &bmp) != Result::Success) return nullptr;

        Record rec;
        rec.last_frame      = frame_;
        rec.slot.bearing_x  = static_cast<int>(std::lround(bmp.metrics.bearing_x));
        rec.slot.bearing_y  = static_cast<int>(std::lround(bmp.metrics.bearing_y));
        rec.slot.advance_x  = bmp.metrics.advance_x;

        // Empty glyph (space, etc.): no pixels, advance only.
        if (!bmp.pixels || bmp.width <= 0 || bmp.height <= 0) {
            rec.slot.layer = 0;
            rec.cell_w = 0;
            auto ins = glyphs_.emplace(key, rec);
            return &ins.first->second.slot;
        }

        // Tight-copy the coverage immediately (the FreeType backend reuses its buffer).
        int cw = bmp.width, ch = bmp.height;
        std::vector<uint8_t> cov(static_cast<size_t>(cw) * ch);
        for (int y = 0; y < ch; ++y)
            memcpy(cov.data() + static_cast<size_t>(y) * cw,
                   static_cast<const uint8_t*>(bmp.pixels) + static_cast<size_t>(y) * bmp.pitch,
                   cw);

        // Synthetic bold / italic on the coverage bitmap.
        if (synth & kSynthBold) {
            int strength = std::max(1, static_cast<int>(std::lround(size / 24.0f)));
            synth_embolden(cov, cw, ch, strength);
            rec.slot.advance_x += strength;
        }
        if (synth & kSynthItalic) synth_oblique(cov, cw, ch, 0.2f);

        // Pack into a layer; one GC retry if the atlas is full.
        Placement pl;
        if (!pack(cw, ch, pl)) {
            collect_garbage();
            if (!pack(cw, ch, pl)) return nullptr;
        }
        blit(pl, cov, cw, ch);

        // Glyph content sits inset by `padding` inside the packed cell.
        const int cx = pl.x + cfg_.padding, cy = pl.y + cfg_.padding;
        rec.slot.layer = pl.layer;
        rec.slot.px = cx; rec.slot.py = cy;
        rec.slot.pw = cw; rec.slot.ph = ch;
        rec.slot.u0 = static_cast<float>(cx)      / cfg_.layer_width;
        rec.slot.v0 = static_cast<float>(cy)      / cfg_.layer_height;
        rec.slot.u1 = static_cast<float>(cx + cw) / cfg_.layer_width;
        rec.slot.v1 = static_cast<float>(cy + ch) / cfg_.layer_height;
        rec.layer = pl.layer; rec.shelf_idx = pl.shelf_idx;
        rec.cell_x = pl.x;    rec.cell_w = pl.cell_w;   // cell origin/width for freeing
        rec.bytes = static_cast<size_t>(cw) * ch;
        memory_usage_ += rec.bytes;

        auto ins = glyphs_.emplace(key, rec);
        return &ins.first->second.slot;
    }

    const GlyphSlot* acquire_codepoint(int font_idx, uint32_t codepoint, float size,
                                       int* out_resolved_font_idx,
                                       FontWeight weight, FontStyle style) override {
        int resolved = -1;
        uint32_t gi = 0;
        if (font_idx >= 0 && font_idx < static_cast<int>(fonts_.size())) {
            gi = fonts_[font_idx].face->get_glyph_index(codepoint);
            if (gi) resolved = font_idx;
        }
        if (resolved < 0) {
            for (int j = 0; j < static_cast<int>(fonts_.size()); ++j) {
                if (j == font_idx) continue;
                uint32_t g = fonts_[j].face->get_glyph_index(codepoint);
                if (g) { resolved = j; gi = g; break; }
            }
        }
        while (resolved < 0 && load_next_candidate()) {
            int j = static_cast<int>(fonts_.size()) - 1;
            uint32_t g = fonts_[j].face->get_glyph_index(codepoint);
            if (g) { resolved = j; gi = g; }
        }
        if (resolved < 0) { resolved = (font_idx >= 0 ? font_idx : 0); gi = 0; }
        if (out_resolved_font_idx) *out_resolved_font_idx = resolved;
        if (resolved < 0 || resolved >= static_cast<int>(fonts_.size())) return nullptr;
        return acquire(resolved, gi, size, weight, style);
    }

    // ---- texture access ----
    const uint8_t* layer_data(int layer) const override {
        if (layer < 0 || layer >= static_cast<int>(layers_.size())) return nullptr;
        return layers_[layer].data();
    }
    int width()  const override { return cfg_.layer_width; }
    int height() const override { return cfg_.layer_height; }
    int layer_count() const override { return static_cast<int>(layers_.size()); }

    void take_dirty_regions(std::vector<GlyphDirtyRegion>& out) override {
        out.insert(out.end(), dirty_.begin(), dirty_.end());
        dirty_.clear();
    }

    // ---- GC ----
    void begin_frame() override { ++frame_; }

    int collect_garbage() override {
        int freed = 0;
        // 1) idle eviction
        for (auto it = glyphs_.begin(); it != glyphs_.end(); ) {
            if (frame_ - it->second.last_frame > static_cast<uint64_t>(cfg_.gc_idle_frames)) {
                free_record(it->second);
                it = glyphs_.erase(it);
                ++freed;
            } else {
                ++it;
            }
        }
        // 2) budget eviction (global LRU)
        if (cfg_.gc_budget_bytes > 0 && memory_usage_ > cfg_.gc_budget_bytes) {
            std::vector<MapIt> its;
            its.reserve(glyphs_.size());
            for (auto it = glyphs_.begin(); it != glyphs_.end(); ++it) its.push_back(it);
            std::sort(its.begin(), its.end(),
                      [](const MapIt& a, const MapIt& b) {
                          return a->second.last_frame < b->second.last_frame;
                      });
            for (MapIt it : its) {
                if (memory_usage_ <= cfg_.gc_budget_bytes) break;
                free_record(it->second);
                glyphs_.erase(it);
                ++freed;
            }
        }
        return freed;
    }

    void clear() override {
        glyphs_.clear();
        for (auto& p : packs_) { p.shelves.clear(); p.frontier_y = 0; }
        memory_usage_ = 0;
        dirty_.clear();
    }

    size_t memory_usage() const override { return memory_usage_; }
    int    glyph_count()  const override { return static_cast<int>(glyphs_.size()); }

private:
    enum : uint8_t { kSynthBold = 1, kSynthItalic = 2 };

    struct FontEntry {
        IFontFace* face = nullptr;
        bool owned = false;
        bool intrinsic_bold = false;
        bool intrinsic_italic = false;
    };
    struct Candidate { std::string family; FontWeight weight; FontStyle style; };

    struct Key {
        uint16_t font_idx;
        uint32_t glyph_id;
        uint32_t size_q;
        uint8_t  synth;
        bool operator==(const Key& o) const {
            return font_idx == o.font_idx && glyph_id == o.glyph_id &&
                   size_q == o.size_q && synth == o.synth;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = k.font_idx;
            h ^= std::hash<uint32_t>{}(k.glyph_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(k.size_q)   + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.synth)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    struct Record {
        GlyphSlot slot;
        int layer = 0, shelf_idx = -1, cell_x = 0, cell_w = 0;
        size_t bytes = 0;
        uint64_t last_frame = 0;
    };
    using MapIt = std::unordered_map<Key, Record, KeyHash>::iterator;

    struct Seg   { int x, w; };
    struct Shelf { int y = 0, h = 0, cur_x = 0, live = 0; std::vector<Seg> free_list; };
    struct LayerPack { std::vector<Shelf> shelves; int frontier_y = 0; };
    struct Placement { int layer, x, y, shelf_idx, cell_w; };

    void add_layer() {
        layers_.emplace_back(static_cast<size_t>(cfg_.layer_width) * cfg_.layer_height, 0);
        packs_.emplace_back();
    }

    // Find room for a content cell of (cw x ch). Returns the content placement.
    bool pack(int cw, int ch, Placement& out) {
        // Reserve a `padding`-px cleared border on every side of the glyph so
        // bilinear sampling at a glyph edge can never reach a neighbour.
        int cell_w = cw + 2 * cfg_.padding;
        int cell_h = ch + 2 * cfg_.padding;
        if (cell_w > cfg_.layer_width || cell_h > cfg_.layer_height) return false;

        // 1) reuse space in an existing shelf (free segs first, then its frontier)
        for (int li = 0; li < static_cast<int>(packs_.size()); ++li) {
            LayerPack& lp = packs_[li];
            for (int si = 0; si < static_cast<int>(lp.shelves.size()); ++si) {
                Shelf& s = lp.shelves[si];
                if (s.h < cell_h) continue;
                int best = -1, bestw = INT_MAX;
                for (int k = 0; k < static_cast<int>(s.free_list.size()); ++k)
                    if (s.free_list[k].w >= cell_w && s.free_list[k].w < bestw) {
                        best = k; bestw = s.free_list[k].w;
                    }
                if (best >= 0) {
                    Seg seg = s.free_list[best];
                    if (seg.w > cell_w) s.free_list[best] = Seg{seg.x + cell_w, seg.w - cell_w};
                    else                s.free_list.erase(s.free_list.begin() + best);
                    ++s.live;
                    out = Placement{li, seg.x, s.y, si, cell_w};
                    return true;
                }
                if (s.cur_x + cell_w <= cfg_.layer_width) {
                    int x = s.cur_x; s.cur_x += cell_w; ++s.live;
                    out = Placement{li, x, s.y, si, cell_w};
                    return true;
                }
            }
        }
        // 2) open a new shelf at the frontier of an existing layer
        for (int li = 0; li < static_cast<int>(packs_.size()); ++li) {
            LayerPack& lp = packs_[li];
            if (lp.frontier_y + cell_h <= cfg_.layer_height) {
                Shelf s; s.y = lp.frontier_y; s.h = cell_h; s.cur_x = cell_w; s.live = 1;
                lp.frontier_y += cell_h;
                int si = static_cast<int>(lp.shelves.size());
                lp.shelves.push_back(std::move(s));
                out = Placement{li, 0, lp.shelves[si].y, si, cell_w};
                return true;
            }
        }
        // 3) grow a new layer
        if (static_cast<int>(layers_.size()) < cfg_.max_layers) {
            add_layer();
            int li = static_cast<int>(packs_.size()) - 1;
            LayerPack& lp = packs_[li];
            Shelf s; s.y = 0; s.h = cell_h; s.cur_x = cell_w; s.live = 1;
            lp.frontier_y = cell_h;
            lp.shelves.push_back(std::move(s));
            out = Placement{li, 0, 0, 0, cell_w};
            return true;
        }
        return false;
    }

    void blit(const Placement& pl, const std::vector<uint8_t>& cov, int cw, int ch) {
        std::vector<uint8_t>& layer = layers_[pl.layer];
        const int W   = cfg_.layer_width;
        const int pad = cfg_.padding;
        // Clear the whole cell (content + its border) so the gutter is zeroed.
        int clear_w = std::min(pl.cell_w, W - pl.x);
        int clear_h = std::min(ch + 2 * pad, cfg_.layer_height - pl.y);
        for (int y = 0; y < clear_h; ++y)
            memset(layer.data() + static_cast<size_t>(pl.y + y) * W + pl.x, 0, clear_w);
        // Copy the glyph inset by `pad` so it is surrounded by cleared pixels.
        const int cx = pl.x + pad, cy = pl.y + pad;
        for (int y = 0; y < ch; ++y)
            memcpy(layer.data() + static_cast<size_t>(cy + y) * W + cx,
                   cov.data() + static_cast<size_t>(y) * cw, cw);
        dirty_.push_back(GlyphDirtyRegion{pl.layer, pl.x, pl.y, clear_w, clear_h});
    }

    void free_record(Record& rec) {
        memory_usage_ -= rec.bytes;
        if (rec.cell_w <= 0) return;                         // empty glyph: nothing packed
        if (rec.layer < 0 || rec.layer >= static_cast<int>(packs_.size())) return;
        LayerPack& lp = packs_[rec.layer];
        if (rec.shelf_idx < 0 || rec.shelf_idx >= static_cast<int>(lp.shelves.size())) return;
        Shelf& s = lp.shelves[rec.shelf_idx];
        s.free_list.push_back(Seg{rec.cell_x, rec.cell_w});
        if (--s.live <= 0) {
            if (s.y + s.h == lp.frontier_y) {
                // Reclaim empty shelves from the top of the layer.
                while (!lp.shelves.empty()) {
                    Shelf& top = lp.shelves.back();
                    if (top.live <= 0 && top.y + top.h == lp.frontier_y) {
                        lp.frontier_y = top.y;
                        lp.shelves.pop_back();
                    } else break;
                }
            } else {
                // Interior empty shelf: make its whole row reusable.
                s.free_list.assign(1, Seg{0, cfg_.layer_width});
                s.cur_x = cfg_.layer_width;
            }
        }
    }

    // Lazily load the next fallback candidate (user families first, then platform
    // defaults). Returns true if one was newly registered.
    bool load_next_candidate() {
        while (user_tried_ < user_candidates_.size())
            if (try_load_candidate(user_candidates_[user_tried_++])) return true;
        while (g_fallback_families[plat_tried_] != nullptr) {
            Candidate c{g_fallback_families[plat_tried_++], FontWeight::Regular, FontStyle::Normal};
            if (try_load_candidate(c)) return true;
        }
        return false;
    }

    bool try_load_candidate(const Candidate& c) {
        if (!library_) return false;
        FontDescriptor d = FontDescriptor::create(c.family.c_str(), 12.0f);
        d.weight = c.weight;
        d.style  = c.style;
        Result r;
        IFontFace* face = library_->load_system_font(d, &r);
        if (!face) return false;
        const char* fam = face->get_family_name();
        for (auto& l : loaded_families_) {
            if ((fam && l == fam) || l == c.family) {   // already have this physical font
                library_->destroy_font(face);
                return false;
            }
        }
        add_font(face, true);
        return true;
    }

    void populate_all_candidates() { while (load_next_candidate()) {} }

    IFontLibrary*    library_;
    GlyphAtlasConfig cfg_;

    std::vector<FontEntry>   fonts_;
    std::vector<std::string> loaded_families_;
    FallbackFontChain*       chain_ = nullptr;

    std::vector<Candidate>   user_candidates_;
    size_t user_tried_ = 0;
    int    plat_tried_ = 0;

    std::vector<std::vector<uint8_t>> layers_;   // each cfg_.layer_width*height bytes
    std::vector<LayerPack>            packs_;
    std::vector<GlyphDirtyRegion>     dirty_;

    std::unordered_map<Key, Record, KeyHash> glyphs_;
    size_t   memory_usage_ = 0;
    uint64_t frame_ = 0;
};

IGlyphAtlasManager* create_glyph_atlas_manager(IFontLibrary* library, const GlyphAtlasConfig& cfg) {
    if (!library) return nullptr;
    return new GlyphAtlasImpl(library, cfg);
}

void destroy_glyph_atlas_manager(IGlyphAtlasManager* mgr) {
    delete mgr;
}

} // namespace font
