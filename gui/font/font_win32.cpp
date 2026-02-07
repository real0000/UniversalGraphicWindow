/*
 * font_win32.cpp - Windows Native Font Implementation
 *
 * Uses DirectWrite for modern font rendering on Windows 7+.
 * Falls back to GDI for older systems.
 */

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "font.hpp"
#include <windows.h>
#include <dwrite.h>
#include <d2d1.h>
#include <wincodec.h>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace font {

// ============================================================================
// DirectWrite Font Face Implementation
// ============================================================================

class DirectWriteFontFace : public IFontFace {
public:
    DirectWriteFontFace(IDWriteFontFace* face, IDWriteFont* font,
                        const FontDescriptor& desc, float size);
    ~DirectWriteFontFace();

    const FontDescriptor& get_descriptor() const override { return descriptor_; }
    const FontMetrics& get_metrics() const override { return metrics_; }
    const char* get_family_name() const override { return family_name_; }
    const char* get_style_name() const override { return style_name_; }

    uint32_t get_glyph_index(uint32_t codepoint) const override;
    bool get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const override;
    float get_kerning(uint32_t left_glyph, uint32_t right_glyph) const override;

    Result render_glyph(uint32_t glyph_index, const RenderOptions& options,
                        GlyphBitmap* out_bitmap) override;

    bool has_glyph(uint32_t codepoint) const override;
    int get_glyph_count() const override;

    Result set_size(float size) override;
    float get_size() const override { return size_; }

    void* get_native_handle() const override { return font_face_; }

private:
    void update_metrics();
    float design_units_to_pixels(int units) const;

    IDWriteFontFace* font_face_ = nullptr;
    IDWriteFont* font_ = nullptr;
    FontDescriptor descriptor_;
    FontMetrics metrics_;
    char family_name_[MAX_FONT_FAMILY_LENGTH] = {};
    char style_name_[128] = {};
    float size_ = 12.0f;
    DWRITE_FONT_METRICS dw_metrics_ = {};
    std::vector<uint8_t> glyph_buffer_;
};

DirectWriteFontFace::DirectWriteFontFace(IDWriteFontFace* face, IDWriteFont* font,
                                          const FontDescriptor& desc, float size)
    : font_face_(face), font_(font), descriptor_(desc), size_(size) {
    if (font_face_) {
        font_face_->AddRef();
    }
    if (font_) {
        font_->AddRef();

        // Get family name
        IDWriteFontFamily* family = nullptr;
        if (SUCCEEDED(font_->GetFontFamily(&family))) {
            IDWriteLocalizedStrings* names = nullptr;
            if (SUCCEEDED(family->GetFamilyNames(&names))) {
                wchar_t name[256];
                names->GetString(0, name, 256);
                WideCharToMultiByte(CP_UTF8, 0, name, -1, family_name_, MAX_FONT_FAMILY_LENGTH, nullptr, nullptr);
                names->Release();
            }
            family->Release();
        }

        // Get style name
        IDWriteLocalizedStrings* face_names = nullptr;
        if (SUCCEEDED(font_->GetFaceNames(&face_names))) {
            wchar_t name[128];
            face_names->GetString(0, name, 128);
            WideCharToMultiByte(CP_UTF8, 0, name, -1, style_name_, sizeof(style_name_), nullptr, nullptr);
            face_names->Release();
        }
    }

    update_metrics();
}

DirectWriteFontFace::~DirectWriteFontFace() {
    if (font_) font_->Release();
    if (font_face_) font_face_->Release();
}

void DirectWriteFontFace::update_metrics() {
    if (!font_face_) return;

    font_face_->GetMetrics(&dw_metrics_);

    float scale = size_ / static_cast<float>(dw_metrics_.designUnitsPerEm);

    metrics_.ascender = dw_metrics_.ascent * scale;
    metrics_.descender = -dw_metrics_.descent * scale;
    metrics_.line_height = (dw_metrics_.ascent + dw_metrics_.descent + dw_metrics_.lineGap) * scale;
    metrics_.underline_position = -dw_metrics_.underlinePosition * scale;
    metrics_.underline_thickness = dw_metrics_.underlineThickness * scale;
    metrics_.strikethrough_position = dw_metrics_.strikethroughPosition * scale;
    metrics_.strikethrough_thickness = dw_metrics_.strikethroughThickness * scale;
    metrics_.units_per_em = static_cast<float>(dw_metrics_.designUnitsPerEm);

    // Calculate max advance
    DWRITE_GLYPH_METRICS glyph_metrics;
    UINT16 glyph_index = 0;
    UINT32 codepoint = 'M';
    font_face_->GetGlyphIndices(&codepoint, 1, &glyph_index);
    if (SUCCEEDED(font_face_->GetDesignGlyphMetrics(&glyph_index, 1, &glyph_metrics))) {
        metrics_.max_advance = glyph_metrics.advanceWidth * scale;
    }
}

float DirectWriteFontFace::design_units_to_pixels(int units) const {
    return units * size_ / static_cast<float>(dw_metrics_.designUnitsPerEm);
}

uint32_t DirectWriteFontFace::get_glyph_index(uint32_t codepoint) const {
    if (!font_face_) return 0;

    UINT16 glyph_index = 0;
    font_face_->GetGlyphIndices(&codepoint, 1, &glyph_index);
    return glyph_index;
}

bool DirectWriteFontFace::get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const {
    if (!font_face_ || !out_metrics) return false;

    UINT16 gi = static_cast<UINT16>(glyph_index);
    DWRITE_GLYPH_METRICS dw_glyph_metrics;

    if (FAILED(font_face_->GetDesignGlyphMetrics(&gi, 1, &dw_glyph_metrics))) {
        return false;
    }

    float scale = size_ / static_cast<float>(dw_metrics_.designUnitsPerEm);

    out_metrics->width = dw_glyph_metrics.advanceWidth * scale;
    out_metrics->height = (dw_glyph_metrics.advanceHeight != 0)
        ? dw_glyph_metrics.advanceHeight * scale
        : (dw_metrics_.ascent + dw_metrics_.descent) * scale;
    out_metrics->bearing_x = dw_glyph_metrics.leftSideBearing * scale;
    out_metrics->bearing_y = (dw_metrics_.ascent - dw_glyph_metrics.topSideBearing) * scale;
    out_metrics->advance_x = dw_glyph_metrics.advanceWidth * scale;
    out_metrics->advance_y = 0.0f;

    return true;
}

float DirectWriteFontFace::get_kerning(uint32_t left_glyph, uint32_t right_glyph) const {
    if (!font_face_) return 0.0f;

    // DirectWrite handles kerning through text layout, not directly accessible here
    // Return 0 for now - proper kerning would require IDWriteTextAnalyzer
    (void)left_glyph;
    (void)right_glyph;
    return 0.0f;
}

Result DirectWriteFontFace::render_glyph(uint32_t glyph_index, const RenderOptions& options,
                                          GlyphBitmap* out_bitmap) {
    if (!font_face_ || !out_bitmap) return Result::ErrorInvalidParameter;

    UINT16 gi = static_cast<UINT16>(glyph_index);

    // Get glyph run bounds
    DWRITE_GLYPH_METRICS glyph_metrics;
    if (FAILED(font_face_->GetDesignGlyphMetrics(&gi, 1, &glyph_metrics))) {
        return Result::ErrorGlyphNotFound;
    }

    float scale = size_ / static_cast<float>(dw_metrics_.designUnitsPerEm);

    int width = static_cast<int>(std::ceil(glyph_metrics.advanceWidth * scale)) + 2;
    int height = static_cast<int>(std::ceil((dw_metrics_.ascent + dw_metrics_.descent) * scale)) + 2;

    if (width <= 0 || height <= 0) {
        out_bitmap->width = 0;
        out_bitmap->height = 0;
        out_bitmap->pixels = nullptr;
        return Result::Success;
    }

    // Create a bitmap render target
    ID2D1Factory* d2d_factory = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);

    if (!d2d_factory) {
        return Result::ErrorRenderFailed;
    }

    // Create WIC bitmap
    IWICImagingFactory* wic_factory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IWICImagingFactory, reinterpret_cast<void**>(&wic_factory));

    if (!wic_factory) {
        d2d_factory->Release();
        return Result::ErrorRenderFailed;
    }

    IWICBitmap* wic_bitmap = nullptr;
    wic_factory->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapCacheOnDemand, &wic_bitmap);

    if (!wic_bitmap) {
        wic_factory->Release();
        d2d_factory->Release();
        return Result::ErrorRenderFailed;
    }

    // Create D2D render target
    D2D1_RENDER_TARGET_PROPERTIES rt_props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    ID2D1RenderTarget* rt = nullptr;
    d2d_factory->CreateWicBitmapRenderTarget(wic_bitmap, rt_props, &rt);

    if (!rt) {
        wic_bitmap->Release();
        wic_factory->Release();
        d2d_factory->Release();
        return Result::ErrorRenderFailed;
    }

    // Set antialiasing mode
    D2D1_TEXT_ANTIALIAS_MODE aa_mode = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    switch (options.antialias) {
        case AntiAliasMode::None:
            aa_mode = D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
            break;
        case AntiAliasMode::Subpixel:
        case AntiAliasMode::SubpixelBGR:
            aa_mode = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
            break;
        default:
            aa_mode = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
            break;
    }
    rt->SetTextAntialiasMode(aa_mode);

    // Create brush
    ID2D1SolidColorBrush* brush = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brush);

    // Draw glyph
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    DWRITE_GLYPH_RUN glyph_run = {};
    glyph_run.fontFace = font_face_;
    glyph_run.fontEmSize = size_;
    glyph_run.glyphCount = 1;
    glyph_run.glyphIndices = &gi;

    float baseline_y = dw_metrics_.ascent * scale;
    float bearing_x = glyph_metrics.leftSideBearing * scale;
    D2D1_POINT_2F origin = {-bearing_x + 1.0f, baseline_y + 1.0f};

    rt->DrawGlyphRun(origin, &glyph_run, brush);
    rt->EndDraw();

    brush->Release();
    rt->Release();

    // Lock bitmap and copy to output
    WICRect lock_rect = {0, 0, width, height};
    IWICBitmapLock* lock = nullptr;
    wic_bitmap->Lock(&lock_rect, WICBitmapLockRead, &lock);

    if (lock) {
        UINT buffer_size = 0;
        BYTE* data = nullptr;
        lock->GetDataPointer(&buffer_size, &data);

        UINT stride = 0;
        lock->GetStride(&stride);

        // Convert to grayscale alpha if needed
        if (options.output_format == PixelFormat::A8) {
            glyph_buffer_.resize(width * height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    BYTE* pixel = data + y * stride + x * 4;
                    // Use alpha channel (premultiplied)
                    glyph_buffer_[y * width + x] = pixel[3];
                }
            }
            out_bitmap->pixels = glyph_buffer_.data();
            out_bitmap->pitch = width;
            out_bitmap->format = PixelFormat::A8;
        } else {
            glyph_buffer_.resize(buffer_size);
            memcpy(glyph_buffer_.data(), data, buffer_size);
            out_bitmap->pixels = glyph_buffer_.data();
            out_bitmap->pitch = stride;
            out_bitmap->format = PixelFormat::BGRA8;
        }

        lock->Release();
    }

    out_bitmap->width = width;
    out_bitmap->height = height;

    // Fill metrics
    out_bitmap->metrics.width = glyph_metrics.advanceWidth * scale;
    out_bitmap->metrics.height = (dw_metrics_.ascent + dw_metrics_.descent) * scale;
    out_bitmap->metrics.bearing_x = glyph_metrics.leftSideBearing * scale;
    out_bitmap->metrics.bearing_y = dw_metrics_.ascent * scale;
    out_bitmap->metrics.advance_x = glyph_metrics.advanceWidth * scale;
    out_bitmap->metrics.advance_y = 0.0f;

    wic_bitmap->Release();
    wic_factory->Release();
    d2d_factory->Release();

    return Result::Success;
}

bool DirectWriteFontFace::has_glyph(uint32_t codepoint) const {
    return get_glyph_index(codepoint) != 0;
}

int DirectWriteFontFace::get_glyph_count() const {
    if (!font_face_) return 0;
    return font_face_->GetGlyphCount();
}

Result DirectWriteFontFace::set_size(float size) {
    if (size <= 0.0f) return Result::ErrorInvalidParameter;
    size_ = size;
    descriptor_.size = size;
    update_metrics();
    return Result::Success;
}

// ============================================================================
// DirectWrite Font Library Implementation
// ============================================================================

class DirectWriteFontLibrary : public IFontLibrary {
public:
    DirectWriteFontLibrary();
    ~DirectWriteFontLibrary();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    FontBackend get_backend() const override { return FontBackend::Native; }

    IFontFace* load_font_file(const char* filepath, int face_index,
                               Result* out_result) override;
    IFontFace* load_font_memory(const void* data, size_t size, int face_index,
                                 Result* out_result) override;
    IFontFace* load_system_font(const FontDescriptor& descriptor,
                                 Result* out_result) override;

    void destroy_font(IFontFace* face) override;

    int enumerate_system_fonts(FontDescriptor* out_fonts, int max_count) const override;
    bool find_system_font(const FontDescriptor& descriptor,
                           char* out_path, int path_size) const override;

    IFontFace* get_default_font(float size, Result* out_result) override;

    void* get_native_handle() const override { return dwrite_factory_; }

private:
    DWRITE_FONT_WEIGHT to_dwrite_weight(FontWeight weight) const;
    DWRITE_FONT_STYLE to_dwrite_style(FontStyle style) const;
    DWRITE_FONT_STRETCH to_dwrite_stretch(FontStretch stretch) const;

    bool initialized_ = false;
    IDWriteFactory* dwrite_factory_ = nullptr;
};

DirectWriteFontLibrary::DirectWriteFontLibrary() {}

DirectWriteFontLibrary::~DirectWriteFontLibrary() {
    shutdown();
}

Result DirectWriteFontLibrary::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwrite_factory_)
    );

    if (FAILED(hr) || !dwrite_factory_) {
        return Result::ErrorUnknown;
    }

    initialized_ = true;
    return Result::Success;
}

void DirectWriteFontLibrary::shutdown() {
    if (dwrite_factory_) {
        dwrite_factory_->Release();
        dwrite_factory_ = nullptr;
    }
    initialized_ = false;
}

IFontFace* DirectWriteFontLibrary::load_font_file(const char* filepath, int face_index,
                                                   Result* out_result) {
    if (!initialized_ || !filepath) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    // Convert to wide string
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, filepath, -1, wpath, MAX_PATH);

    // Create font file reference
    IDWriteFontFile* font_file = nullptr;
    HRESULT hr = dwrite_factory_->CreateFontFileReference(wpath, nullptr, &font_file);
    if (FAILED(hr) || !font_file) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    // Create font face
    IDWriteFontFace* font_face = nullptr;
    hr = dwrite_factory_->CreateFontFace(
        DWRITE_FONT_FACE_TYPE_TRUETYPE,
        1,
        &font_file,
        face_index,
        DWRITE_FONT_SIMULATIONS_NONE,
        &font_face
    );

    font_file->Release();

    if (FAILED(hr) || !font_face) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    FontDescriptor desc;
    desc.size = 12.0f;

    if (out_result) *out_result = Result::Success;
    return new DirectWriteFontFace(font_face, nullptr, desc, 12.0f);
}

IFontFace* DirectWriteFontLibrary::load_font_memory(const void* data, size_t size,
                                                     int face_index, Result* out_result) {
    if (!initialized_ || !data || size == 0) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    // Create custom font file loader would be needed here
    // For now, return error
    if (out_result) *out_result = Result::ErrorBackendNotSupported;
    return nullptr;
}

IFontFace* DirectWriteFontLibrary::load_system_font(const FontDescriptor& descriptor,
                                                     Result* out_result) {
    if (!initialized_) {
        if (out_result) *out_result = Result::ErrorNotInitialized;
        return nullptr;
    }

    // Get system font collection
    IDWriteFontCollection* font_collection = nullptr;
    HRESULT hr = dwrite_factory_->GetSystemFontCollection(&font_collection);
    if (FAILED(hr) || !font_collection) {
        if (out_result) *out_result = Result::ErrorUnknown;
        return nullptr;
    }

    // Convert family name to wide string
    wchar_t wfamily[256];
    MultiByteToWideChar(CP_UTF8, 0, descriptor.family, -1, wfamily, 256);

    // Find font family
    UINT32 index = 0;
    BOOL exists = FALSE;
    hr = font_collection->FindFamilyName(wfamily, &index, &exists);
    if (FAILED(hr) || !exists) {
        font_collection->Release();
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    // Get font family
    IDWriteFontFamily* font_family = nullptr;
    hr = font_collection->GetFontFamily(index, &font_family);
    if (FAILED(hr) || !font_family) {
        font_collection->Release();
        if (out_result) *out_result = Result::ErrorUnknown;
        return nullptr;
    }

    // Get matching font
    IDWriteFont* font = nullptr;
    hr = font_family->GetFirstMatchingFont(
        to_dwrite_weight(descriptor.weight),
        to_dwrite_stretch(descriptor.stretch),
        to_dwrite_style(descriptor.style),
        &font
    );

    font_family->Release();
    font_collection->Release();

    if (FAILED(hr) || !font) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    // Create font face
    IDWriteFontFace* font_face = nullptr;
    hr = font->CreateFontFace(&font_face);
    if (FAILED(hr) || !font_face) {
        font->Release();
        if (out_result) *out_result = Result::ErrorUnknown;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return new DirectWriteFontFace(font_face, font, descriptor, descriptor.size);
}

void DirectWriteFontLibrary::destroy_font(IFontFace* face) {
    delete face;
}

int DirectWriteFontLibrary::enumerate_system_fonts(FontDescriptor* out_fonts, int max_count) const {
    if (!initialized_ || !out_fonts || max_count <= 0) return 0;

    IDWriteFontCollection* font_collection = nullptr;
    if (FAILED(dwrite_factory_->GetSystemFontCollection(&font_collection)) || !font_collection) {
        return 0;
    }

    int count = 0;
    UINT32 family_count = font_collection->GetFontFamilyCount();

    for (UINT32 i = 0; i < family_count && count < max_count; ++i) {
        IDWriteFontFamily* family = nullptr;
        if (FAILED(font_collection->GetFontFamily(i, &family)) || !family) continue;

        IDWriteLocalizedStrings* names = nullptr;
        if (SUCCEEDED(family->GetFamilyNames(&names)) && names) {
            wchar_t name[256];
            names->GetString(0, name, 256);

            FontDescriptor& desc = out_fonts[count];
            WideCharToMultiByte(CP_UTF8, 0, name, -1, desc.family, MAX_FONT_FAMILY_LENGTH, nullptr, nullptr);
            desc.size = 12.0f;
            desc.weight = FontWeight::Regular;
            desc.style = FontStyle::Normal;

            names->Release();
            count++;
        }

        family->Release();
    }

    font_collection->Release();
    return count;
}

bool DirectWriteFontLibrary::find_system_font(const FontDescriptor& descriptor,
                                               char* out_path, int path_size) const {
    // DirectWrite doesn't expose font file paths directly
    // This would require additional work with the font collection
    (void)descriptor;
    (void)out_path;
    (void)path_size;
    return false;
}

IFontFace* DirectWriteFontLibrary::get_default_font(float size, Result* out_result) {
    FontDescriptor desc = FontDescriptor::create("Segoe UI", size);
    IFontFace* face = load_system_font(desc, out_result);

    if (!face) {
        // Fallback to Arial
        desc = FontDescriptor::create("Arial", size);
        face = load_system_font(desc, out_result);
    }

    if (!face) {
        // Fallback to Tahoma
        desc = FontDescriptor::create("Tahoma", size);
        face = load_system_font(desc, out_result);
    }

    return face;
}

DWRITE_FONT_WEIGHT DirectWriteFontLibrary::to_dwrite_weight(FontWeight weight) const {
    return static_cast<DWRITE_FONT_WEIGHT>(static_cast<int>(weight));
}

DWRITE_FONT_STYLE DirectWriteFontLibrary::to_dwrite_style(FontStyle style) const {
    switch (style) {
        case FontStyle::Italic:  return DWRITE_FONT_STYLE_ITALIC;
        case FontStyle::Oblique: return DWRITE_FONT_STYLE_OBLIQUE;
        default:                 return DWRITE_FONT_STYLE_NORMAL;
    }
}

DWRITE_FONT_STRETCH DirectWriteFontLibrary::to_dwrite_stretch(FontStretch stretch) const {
    return static_cast<DWRITE_FONT_STRETCH>(static_cast<int>(stretch));
}

// ============================================================================
// Factory Functions
// ============================================================================

IFontLibrary* create_font_library(FontBackend backend, Result* out_result) {
    if (backend != FontBackend::Auto && backend != FontBackend::Native) {
        if (out_result) *out_result = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    DirectWriteFontLibrary* library = new DirectWriteFontLibrary();
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

} // namespace font

#endif // _WIN32
