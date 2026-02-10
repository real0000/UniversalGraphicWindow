/*
 * font_linux.cpp - Linux Native Font Implementation
 *
 * Uses Fontconfig for font discovery and FreeType for rendering.
 * This is essentially a wrapper that provides native font enumeration
 * on Linux while using FreeType for actual font operations.
 */

#if defined(__linux__) && !defined(__ANDROID__)

#include "font.hpp"

// On Linux, we use FreeType for rendering but can use fontconfig for discovery
// If FreeType is available, delegate to it; otherwise provide stubs

#ifdef FONT_SUPPORT_FREETYPE

// FreeType implementation handles everything
// Just provide fontconfig-based font discovery

#include <fontconfig/fontconfig.h>
#include <cstring>
#include <cstdio>
#include <vector>

namespace font {

// Extended find_system_font that uses fontconfig
bool find_system_font_fontconfig(const FontDescriptor& descriptor,
                                  std::string& out_path) {

    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) return false;

    // Create pattern
    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        FcConfigDestroy(config);
        return false;
    }

    // Add family name
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(descriptor.family.c_str()));

    // Add weight
    int fc_weight = FC_WEIGHT_REGULAR;
    switch (descriptor.weight) {
        case FontWeight::Thin:       fc_weight = FC_WEIGHT_THIN; break;
        case FontWeight::ExtraLight: fc_weight = FC_WEIGHT_EXTRALIGHT; break;
        case FontWeight::Light:      fc_weight = FC_WEIGHT_LIGHT; break;
        case FontWeight::Regular:    fc_weight = FC_WEIGHT_REGULAR; break;
        case FontWeight::Medium:     fc_weight = FC_WEIGHT_MEDIUM; break;
        case FontWeight::SemiBold:   fc_weight = FC_WEIGHT_SEMIBOLD; break;
        case FontWeight::Bold:       fc_weight = FC_WEIGHT_BOLD; break;
        case FontWeight::ExtraBold:  fc_weight = FC_WEIGHT_EXTRABOLD; break;
        case FontWeight::Black:      fc_weight = FC_WEIGHT_BLACK; break;
    }
    FcPatternAddInteger(pattern, FC_WEIGHT, fc_weight);

    // Add slant
    int fc_slant = FC_SLANT_ROMAN;
    if (descriptor.style == FontStyle::Italic) fc_slant = FC_SLANT_ITALIC;
    else if (descriptor.style == FontStyle::Oblique) fc_slant = FC_SLANT_OBLIQUE;
    FcPatternAddInteger(pattern, FC_SLANT, fc_slant);

    // Configure pattern
    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    // Find match
    FcResult result;
    FcPattern* match = FcFontMatch(config, pattern, &result);
    FcPatternDestroy(pattern);

    if (!match) {
        FcConfigDestroy(config);
        return false;
    }

    // Get file path
    FcChar8* file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
        out_path = reinterpret_cast<const char*>(file);
        FcPatternDestroy(match);
        FcConfigDestroy(config);
        return true;
    }

    FcPatternDestroy(match);
    FcConfigDestroy(config);
    return false;
}

void enumerate_system_fonts_fontconfig(std::vector<FontDescriptor>& out_fonts) {
    out_fonts.clear();

    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) return;

    // List all fonts
    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* os = FcObjectSetBuild(FC_FAMILY, FC_STYLE, FC_FILE, nullptr);
    FcFontSet* fonts = FcFontList(config, pattern, os);

    FcPatternDestroy(pattern);
    FcObjectSetDestroy(os);

    if (!fonts) {
        FcConfigDestroy(config);
        return;
    }

    for (int i = 0; i < fonts->nfont; ++i) {
        FcChar8* family = nullptr;
        if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family) {
            // Check if we already have this family
            const char* family_str = reinterpret_cast<const char*>(family);
            bool duplicate = false;
            for (size_t j = 0; j < out_fonts.size(); ++j) {
                if (out_fonts[j].family == family_str) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                FontDescriptor desc;
                desc.family = family_str;
                desc.size = 12.0f;
                desc.weight = FontWeight::Regular;
                desc.style = FontStyle::Normal;
                out_fonts.push_back(desc);
            }
        }
    }

    FcFontSetDestroy(fonts);
    FcConfigDestroy(config);
}

} // namespace font

#else // !FONT_SUPPORT_FREETYPE

// Stub implementation when FreeType is not available

#include <cstring>

namespace font {

class StubFontFace : public IFontFace {
public:
    const FontDescriptor& get_descriptor() const override { return desc_; }
    const FontMetrics& get_metrics() const override { return metrics_; }
    const char* get_family_name() const override { return ""; }
    const char* get_style_name() const override { return ""; }

    uint32_t get_glyph_index(uint32_t) const override { return 0; }
    bool get_glyph_metrics(uint32_t, GlyphMetrics*) const override { return false; }
    float get_kerning(uint32_t, uint32_t) const override { return 0.0f; }

    Result render_glyph(uint32_t, const RenderOptions&, GlyphBitmap*) override {
        return Result::ErrorBackendNotSupported;
    }

    bool has_glyph(uint32_t) const override { return false; }
    int get_glyph_count() const override { return 0; }

    Result set_size(float size) override { desc_.size = size; return Result::Success; }
    float get_size() const override { return desc_.size; }

    void* get_native_handle() const override { return nullptr; }

private:
    FontDescriptor desc_;
    FontMetrics metrics_;
};

class StubFontLibrary : public IFontLibrary {
public:
    Result initialize() override { return Result::Success; }
    void shutdown() override {}
    bool is_initialized() const override { return true; }

    FontBackend get_backend() const override { return FontBackend::Native; }

    IFontFace* load_font_file(const char*, int, Result* r) override {
        if (r) *r = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    IFontFace* load_font_memory(const void*, size_t, int, Result* r) override {
        if (r) *r = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    IFontFace* load_system_font(const FontDescriptor&, Result* r) override {
        if (r) *r = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    void destroy_font(IFontFace* face) override { delete face; }

    void enumerate_system_fonts(std::vector<FontDescriptor>&) const override {}
    bool find_system_font(const FontDescriptor&, std::string&) const override { return false; }

    IFontFace* get_default_font(float, Result* r) override {
        if (r) *r = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    void* get_native_handle() const override { return nullptr; }
};

IFontLibrary* create_font_library(FontBackend backend, Result* out_result) {
    if (backend == FontBackend::FreeType) {
        if (out_result) *out_result = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return new StubFontLibrary();
}

void destroy_font_library(IFontLibrary* library) {
    delete library;
}

} // namespace font

#endif // FONT_SUPPORT_FREETYPE

#endif // __linux__ && !__ANDROID__
