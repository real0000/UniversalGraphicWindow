/*
 * font.cpp - Platform-Independent Font Utilities
 *
 * Contains string conversion, UTF-8 utilities, and common functionality.
 */

#include "font.hpp"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <unordered_map>

namespace font {

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

            uint32_t glyph_index = font->get_glyph_index(codepoint);

            // Apply kerning
            if (prev_glyph != 0) {
                x += font->get_kerning(prev_glyph, glyph_index);
            }
            x += options.letter_spacing;

            GlyphMetrics metrics;
            font->get_glyph_metrics(glyph_index, &metrics);

            PositionedGlyph pg;
            pg.codepoint = codepoint;
            pg.glyph_index = glyph_index;
            pg.x = x;
            pg.y = y;
            pg.advance = metrics.advance_x;
            pg.cluster = cluster;
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

            uint32_t glyph_index = font->get_glyph_index(codepoint);
            GlyphMetrics glyph_metrics;
            font->get_glyph_metrics(glyph_index, &glyph_metrics);

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
};

ITextShaper* create_text_shaper(IFontLibrary* library, Result* out_result) {
    if (!library) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return new SimpleTextShaper(library);
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

        const FontMetrics& metrics = font->get_metrics();

        for (int i = 0; i < glyph_count; ++i) {
            const PositionedGlyph& pg = glyphs[i];

            // Skip whitespace
            if (pg.codepoint == ' ' || pg.codepoint == '\t' || pg.codepoint == '\n') {
                continue;
            }

            // Render glyph
            GlyphBitmap glyph_bitmap;
            Result result = font->render_glyph(pg.glyph_index, render_opts, &glyph_bitmap);
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

            GlyphMetrics glyph_metrics;
            if (!font->get_glyph_metrics(pg.glyph_index, &glyph_metrics)) {
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

} // namespace font
