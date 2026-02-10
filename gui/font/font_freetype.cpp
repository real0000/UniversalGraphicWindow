/*
 * font_freetype.cpp - FreeType2 Font Implementation
 *
 * Cross-platform font rendering using FreeType2 library.
 * Supports TrueType, OpenType, and many other font formats.
 */

#ifdef FONT_SUPPORT_FREETYPE

#include "font.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_LCD_FILTER_H
#include FT_TRUETYPE_TABLES_H

#include <cstring>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <cmath>

namespace font {

// ============================================================================
// FreeType Font Face Implementation
// ============================================================================

class FreeTypeFontFace : public IFontFace {
public:
    FreeTypeFontFace(FT_Library library, FT_Face face, const FontDescriptor& desc,
                     std::vector<uint8_t>&& font_data);
    ~FreeTypeFontFace();

    const FontDescriptor& get_descriptor() const override { return descriptor_; }
    const FontMetrics& get_metrics() const override { return metrics_; }
    const char* get_family_name() const override { return family_name_.c_str(); }
    const char* get_style_name() const override { return style_name_.c_str(); }

    uint32_t get_glyph_index(uint32_t codepoint) const override;
    bool get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const override;
    float get_kerning(uint32_t left_glyph, uint32_t right_glyph) const override;

    Result render_glyph(uint32_t glyph_index, const RenderOptions& options,
                        GlyphBitmap* out_bitmap) override;

    bool has_glyph(uint32_t codepoint) const override;
    int get_glyph_count() const override;

    Result set_size(float size) override;
    float get_size() const override { return descriptor_.size; }

    void* get_native_handle() const override { return face_; }

private:
    void update_metrics();
    int get_load_flags(const RenderOptions& options) const;
    int get_render_mode(const RenderOptions& options) const;

    FT_Library library_ = nullptr;
    FT_Face face_ = nullptr;
    FontDescriptor descriptor_;
    FontMetrics metrics_;
    std::string family_name_;
    std::string style_name_;
    std::vector<uint8_t> font_data_;  // Keep font data alive
    std::vector<uint8_t> glyph_buffer_;
    bool has_kerning_ = false;
};

FreeTypeFontFace::FreeTypeFontFace(FT_Library library, FT_Face face,
                                    const FontDescriptor& desc,
                                    std::vector<uint8_t>&& font_data)
    : library_(library), face_(face), descriptor_(desc), font_data_(std::move(font_data)) {

    if (face_) {
        if (face_->family_name) {
            family_name_ = face_->family_name;
        }
        if (face_->style_name) {
            style_name_ = face_->style_name;
        }

        has_kerning_ = FT_HAS_KERNING(face_);

        // Set initial size
        set_size(desc.size);
    }
}

FreeTypeFontFace::~FreeTypeFontFace() {
    if (face_) {
        FT_Done_Face(face_);
    }
}

void FreeTypeFontFace::update_metrics() {
    if (!face_) return;

    // FreeType metrics are in 26.6 fixed-point format (1/64 pixels)
    float scale = 1.0f / 64.0f;

    metrics_.ascender = face_->size->metrics.ascender * scale;
    metrics_.descender = face_->size->metrics.descender * scale;
    metrics_.line_height = face_->size->metrics.height * scale;
    metrics_.max_advance = face_->size->metrics.max_advance * scale;
    metrics_.units_per_em = static_cast<float>(face_->units_per_EM);

    // Get underline info from OS/2 table if available
    TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2));
    if (os2) {
        float units_scale = descriptor_.size / metrics_.units_per_em;
        metrics_.underline_position = os2->ySubscriptYOffset * units_scale;
        metrics_.underline_thickness = os2->yStrikeoutSize * units_scale;
        metrics_.strikethrough_position = os2->yStrikeoutPosition * units_scale;
        metrics_.strikethrough_thickness = os2->yStrikeoutSize * units_scale;
    } else {
        // Reasonable defaults
        metrics_.underline_position = -metrics_.descender * 0.5f;
        metrics_.underline_thickness = descriptor_.size / 14.0f;
        metrics_.strikethrough_position = metrics_.ascender * 0.3f;
        metrics_.strikethrough_thickness = metrics_.underline_thickness;
    }
}

uint32_t FreeTypeFontFace::get_glyph_index(uint32_t codepoint) const {
    if (!face_) return 0;
    return FT_Get_Char_Index(face_, codepoint);
}

bool FreeTypeFontFace::get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const {
    if (!face_ || !out_metrics) return false;

    // Load glyph without rendering
    FT_Error error = FT_Load_Glyph(face_, glyph_index, FT_LOAD_NO_BITMAP);
    if (error) return false;

    float scale = 1.0f / 64.0f;
    FT_GlyphSlot slot = face_->glyph;

    out_metrics->width = slot->metrics.width * scale;
    out_metrics->height = slot->metrics.height * scale;
    out_metrics->bearing_x = slot->metrics.horiBearingX * scale;
    out_metrics->bearing_y = slot->metrics.horiBearingY * scale;
    out_metrics->advance_x = slot->metrics.horiAdvance * scale;
    out_metrics->advance_y = slot->metrics.vertAdvance * scale;

    return true;
}

float FreeTypeFontFace::get_kerning(uint32_t left_glyph, uint32_t right_glyph) const {
    if (!face_ || !has_kerning_) return 0.0f;

    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(face_, left_glyph, right_glyph,
                                     FT_KERNING_DEFAULT, &kerning);
    if (error) return 0.0f;

    return kerning.x / 64.0f;
}

int FreeTypeFontFace::get_load_flags(const RenderOptions& options) const {
    int flags = FT_LOAD_DEFAULT;

    switch (options.hinting) {
        case HintingMode::None:
            flags |= FT_LOAD_NO_HINTING;
            break;
        case HintingMode::Light:
            flags |= FT_LOAD_TARGET_LIGHT;
            break;
        case HintingMode::Normal:
            flags |= FT_LOAD_TARGET_NORMAL;
            break;
        case HintingMode::Full:
            flags |= FT_LOAD_TARGET_MONO;
            break;
    }

    if (options.antialias == AntiAliasMode::None) {
        flags |= FT_LOAD_TARGET_MONO;
    } else if (options.antialias == AntiAliasMode::Subpixel ||
               options.antialias == AntiAliasMode::SubpixelBGR) {
        flags |= FT_LOAD_TARGET_LCD;
    }

    return flags;
}

int FreeTypeFontFace::get_render_mode(const RenderOptions& options) const {
    switch (options.antialias) {
        case AntiAliasMode::None:
            return FT_RENDER_MODE_MONO;
        case AntiAliasMode::Subpixel:
        case AntiAliasMode::SubpixelBGR:
            return FT_RENDER_MODE_LCD;
        default:
            return FT_RENDER_MODE_NORMAL;
    }
}

Result FreeTypeFontFace::render_glyph(uint32_t glyph_index, const RenderOptions& options,
                                       GlyphBitmap* out_bitmap) {
    if (!face_ || !out_bitmap) return Result::ErrorInvalidParameter;

    int load_flags = get_load_flags(options);
    FT_Error error = FT_Load_Glyph(face_, glyph_index, load_flags);
    if (error) return Result::ErrorGlyphNotFound;

    // Set LCD filter if using subpixel
    if (options.antialias == AntiAliasMode::Subpixel ||
        options.antialias == AntiAliasMode::SubpixelBGR) {
        FT_Library_SetLcdFilter(library_, FT_LCD_FILTER_DEFAULT);
    }

    int render_mode = get_render_mode(options);
    error = FT_Render_Glyph(face_->glyph, static_cast<FT_Render_Mode>(render_mode));
    if (error) return Result::ErrorRenderFailed;

    FT_GlyphSlot slot = face_->glyph;
    FT_Bitmap* bitmap = &slot->bitmap;

    // Handle empty glyphs (spaces, etc.)
    if (bitmap->width == 0 || bitmap->rows == 0) {
        out_bitmap->width = 0;
        out_bitmap->height = 0;
        out_bitmap->pixels = nullptr;
        out_bitmap->pitch = 0;

        // Still fill metrics
        float scale = 1.0f / 64.0f;
        out_bitmap->metrics.width = slot->metrics.width * scale;
        out_bitmap->metrics.height = slot->metrics.height * scale;
        out_bitmap->metrics.bearing_x = slot->metrics.horiBearingX * scale;
        out_bitmap->metrics.bearing_y = slot->metrics.horiBearingY * scale;
        out_bitmap->metrics.advance_x = slot->metrics.horiAdvance * scale;
        out_bitmap->metrics.advance_y = 0.0f;

        return Result::Success;
    }

    int width = bitmap->width;
    int height = bitmap->rows;
    int src_pitch = bitmap->pitch;

    // Determine output format and pitch
    PixelFormat out_format;
    int out_pitch;

    switch (bitmap->pixel_mode) {
        case FT_PIXEL_MODE_MONO: {
            // Convert 1-bit to 8-bit
            out_format = PixelFormat::A8;
            out_pitch = width;
            glyph_buffer_.resize(width * height);

            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int byte_idx = x / 8;
                    int bit_idx = 7 - (x % 8);
                    uint8_t bit = (bitmap->buffer[y * src_pitch + byte_idx] >> bit_idx) & 1;
                    glyph_buffer_[y * out_pitch + x] = bit ? 255 : 0;
                }
            }
            break;
        }

        case FT_PIXEL_MODE_GRAY: {
            out_format = PixelFormat::A8;
            out_pitch = width;
            glyph_buffer_.resize(width * height);

            for (int y = 0; y < height; ++y) {
                memcpy(glyph_buffer_.data() + y * out_pitch,
                       bitmap->buffer + y * src_pitch, width);
            }
            break;
        }

        case FT_PIXEL_MODE_LCD: {
            // RGB subpixel - width is 3x actual width
            width /= 3;
            if (options.output_format == PixelFormat::A8) {
                // Convert to grayscale
                out_format = PixelFormat::A8;
                out_pitch = width;
                glyph_buffer_.resize(width * height);

                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        int r = bitmap->buffer[y * src_pitch + x * 3];
                        int g = bitmap->buffer[y * src_pitch + x * 3 + 1];
                        int b = bitmap->buffer[y * src_pitch + x * 3 + 2];
                        // Luminance
                        glyph_buffer_[y * out_pitch + x] =
                            static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
                    }
                }
            } else {
                out_format = (options.antialias == AntiAliasMode::SubpixelBGR)
                    ? PixelFormat::BGR8 : PixelFormat::RGB8;
                out_pitch = width * 3;
                glyph_buffer_.resize(out_pitch * height);

                for (int y = 0; y < height; ++y) {
                    memcpy(glyph_buffer_.data() + y * out_pitch,
                           bitmap->buffer + y * src_pitch, out_pitch);
                }
            }
            break;
        }

        default:
            return Result::ErrorRenderFailed;
    }

    out_bitmap->pixels = glyph_buffer_.data();
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->pitch = out_pitch;
    out_bitmap->format = out_format;

    // Fill metrics
    float scale = 1.0f / 64.0f;
    out_bitmap->metrics.width = slot->metrics.width * scale;
    out_bitmap->metrics.height = slot->metrics.height * scale;
    out_bitmap->metrics.bearing_x = static_cast<float>(slot->bitmap_left);
    out_bitmap->metrics.bearing_y = static_cast<float>(slot->bitmap_top);
    out_bitmap->metrics.advance_x = slot->metrics.horiAdvance * scale;
    out_bitmap->metrics.advance_y = 0.0f;

    return Result::Success;
}

bool FreeTypeFontFace::has_glyph(uint32_t codepoint) const {
    return get_glyph_index(codepoint) != 0;
}

int FreeTypeFontFace::get_glyph_count() const {
    if (!face_) return 0;
    return static_cast<int>(face_->num_glyphs);
}

Result FreeTypeFontFace::set_size(float size) {
    if (size <= 0.0f) return Result::ErrorInvalidParameter;
    if (!face_) return Result::ErrorNotInitialized;

    // FreeType wants size in 26.6 fixed-point format
    FT_Error error = FT_Set_Char_Size(face_, 0, static_cast<FT_F26Dot6>(size * 64), 72, 72);
    if (error) return Result::ErrorUnknown;

    descriptor_.size = size;
    update_metrics();

    return Result::Success;
}

// ============================================================================
// FreeType Font Library Implementation
// ============================================================================

class FreeTypeFontLibrary : public IFontLibrary {
public:
    FreeTypeFontLibrary();
    ~FreeTypeFontLibrary();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    FontBackend get_backend() const override { return FontBackend::FreeType; }

    IFontFace* load_font_file(const char* filepath, int face_index,
                               Result* out_result) override;
    IFontFace* load_font_memory(const void* data, size_t size, int face_index,
                                 Result* out_result) override;
    IFontFace* load_system_font(const FontDescriptor& descriptor,
                                 Result* out_result) override;

    void destroy_font(IFontFace* face) override;

    void enumerate_system_fonts(std::vector<FontDescriptor>& out_fonts) const override;
    bool find_system_font(const FontDescriptor& descriptor,
                           std::string& out_path) const override;

    IFontFace* get_default_font(float size, Result* out_result) override;

    void* get_native_handle() const override { return library_; }

private:
    bool initialized_ = false;
    FT_Library library_ = nullptr;
};

FreeTypeFontLibrary::FreeTypeFontLibrary() {}

FreeTypeFontLibrary::~FreeTypeFontLibrary() {
    shutdown();
}

Result FreeTypeFontLibrary::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    FT_Error error = FT_Init_FreeType(&library_);
    if (error) {
        return Result::ErrorUnknown;
    }

    initialized_ = true;
    return Result::Success;
}

void FreeTypeFontLibrary::shutdown() {
    if (library_) {
        FT_Done_FreeType(library_);
        library_ = nullptr;
    }
    initialized_ = false;
}

IFontFace* FreeTypeFontLibrary::load_font_file(const char* filepath, int face_index,
                                                Result* out_result) {
    if (!initialized_ || !filepath) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    // Read file into memory (FreeType may need it to stay around)
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    FT_Face face = nullptr;
    FT_Error error = FT_New_Memory_Face(library_, data.data(), static_cast<FT_Long>(data.size()),
                                         face_index, &face);
    if (error || !face) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    FontDescriptor desc;
    if (face->family_name) {
        desc.family = face->family_name;
    }
    desc.size = 12.0f;

    if (out_result) *out_result = Result::Success;
    return new FreeTypeFontFace(library_, face, desc, std::move(data));
}

IFontFace* FreeTypeFontLibrary::load_font_memory(const void* data, size_t size,
                                                  int face_index, Result* out_result) {
    if (!initialized_ || !data || size == 0) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    // Copy data since we need to keep it alive
    std::vector<uint8_t> font_data(static_cast<const uint8_t*>(data),
                                    static_cast<const uint8_t*>(data) + size);

    FT_Face face = nullptr;
    FT_Error error = FT_New_Memory_Face(library_, font_data.data(),
                                         static_cast<FT_Long>(font_data.size()),
                                         face_index, &face);
    if (error || !face) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    FontDescriptor desc;
    if (face->family_name) {
        desc.family = face->family_name;
    }
    desc.size = 12.0f;

    if (out_result) *out_result = Result::Success;
    return new FreeTypeFontFace(library_, face, desc, std::move(font_data));
}

IFontFace* FreeTypeFontLibrary::load_system_font(const FontDescriptor& descriptor,
                                                  Result* out_result) {
    // Try to find the font file
    std::string path;
    if (find_system_font(descriptor, path)) {
        return load_font_file(path.c_str(), 0, out_result);
    }

    if (out_result) *out_result = Result::ErrorFileNotFound;
    return nullptr;
}

void FreeTypeFontLibrary::destroy_font(IFontFace* face) {
    delete face;
}

void FreeTypeFontLibrary::enumerate_system_fonts(std::vector<FontDescriptor>& out_fonts) const {
    // Platform-specific implementation would be needed
    // For now, just clear output
    out_fonts.clear();
}

bool FreeTypeFontLibrary::find_system_font(const FontDescriptor& descriptor,
                                            std::string& out_path) const {

    // Platform-specific font search paths
#ifdef _WIN32
    const char* search_paths[] = {
        "C:\\Windows\\Fonts\\",
        nullptr
    };
    const char* extensions[] = {".ttf", ".otf", ".ttc", nullptr};
#elif defined(__APPLE__)
    const char* search_paths[] = {
        "/System/Library/Fonts/",
        "/Library/Fonts/",
        "~/Library/Fonts/",
        nullptr
    };
    const char* extensions[] = {".ttf", ".otf", ".ttc", ".dfont", nullptr};
#else
    const char* search_paths[] = {
        "/usr/share/fonts/",
        "/usr/local/share/fonts/",
        "~/.fonts/",
        "~/.local/share/fonts/",
        nullptr
    };
    const char* extensions[] = {".ttf", ".otf", ".ttc", nullptr};
#endif

    for (int p = 0; search_paths[p]; ++p) {
        for (int e = 0; extensions[e]; ++e) {
            std::string test_path = std::string(search_paths[p]) + descriptor.family + extensions[e];

            // Check if file exists
            std::ifstream file(test_path);
            if (file.good()) {
                out_path = std::move(test_path);
                return true;
            }
        }
    }

    return false;
}

IFontFace* FreeTypeFontLibrary::get_default_font(float size, Result* out_result) {
    // Try common default fonts
#ifdef _WIN32
    const char* defaults[] = {"segoeui", "arial", "tahoma", nullptr};
#elif defined(__APPLE__)
    const char* defaults[] = {"Helvetica", "Arial", nullptr};
#else
    const char* defaults[] = {"DejaVuSans", "FreeSans", "Liberation Sans", nullptr};
#endif

    for (int i = 0; defaults[i]; ++i) {
        FontDescriptor desc = FontDescriptor::create(defaults[i], size);
        IFontFace* face = load_system_font(desc, nullptr);
        if (face) {
            if (out_result) *out_result = Result::Success;
            return face;
        }
    }

    if (out_result) *out_result = Result::ErrorFileNotFound;
    return nullptr;
}

// ============================================================================
// Factory Functions (FreeType backend)
// ============================================================================

#ifndef _WIN32
#ifndef __APPLE__

// For platforms without native backend, FreeType is the default
IFontLibrary* create_font_library(FontBackend backend, Result* out_result) {
    if (backend != FontBackend::Auto && backend != FontBackend::FreeType) {
        if (out_result) *out_result = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    FreeTypeFontLibrary* library = new FreeTypeFontLibrary();
    Result result = library->initialize();

    if (result != Result::Success) {
        delete library;
        if (out_result) *out_result = result;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return library;
}

void destroy_font_library(IFontLibrary* library) {
    delete library;
}

#endif // !__APPLE__
#endif // !_WIN32

// Allow creating FreeType backend explicitly on any platform
IFontLibrary* create_freetype_font_library(Result* out_result) {
    FreeTypeFontLibrary* library = new FreeTypeFontLibrary();
    Result result = library->initialize();

    if (result != Result::Success) {
        delete library;
        if (out_result) *out_result = result;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return library;
}

} // namespace font

#endif // FONT_SUPPORT_FREETYPE
