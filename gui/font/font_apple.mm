/*
 * font_apple.mm - macOS/iOS Native Font Implementation
 *
 * Uses Core Text for font rendering on Apple platforms.
 */

#if defined(__APPLE__)

#include "font.hpp"
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#else
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#endif

#include <cstring>
#include <vector>

namespace font {

// ============================================================================
// Core Text Font Face Implementation
// ============================================================================

class CoreTextFontFace : public IFontFace {
public:
    CoreTextFontFace(CTFontRef font, const FontDescriptor& desc);
    ~CoreTextFontFace();

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

    void* get_native_handle() const override { return const_cast<void*>(static_cast<const void*>(ct_font_)); }

private:
    void update_metrics();

    CTFontRef ct_font_ = nullptr;
    FontDescriptor descriptor_;
    FontMetrics metrics_;
    std::string family_name_;
    std::string style_name_;
    std::vector<uint8_t> glyph_buffer_;
};

CoreTextFontFace::CoreTextFontFace(CTFontRef font, const FontDescriptor& desc)
    : ct_font_(font), descriptor_(desc) {
    if (ct_font_) {
        CFRetain(ct_font_);

        // Get family name
        CFStringRef family = CTFontCopyFamilyName(ct_font_);
        if (family) {
            char buf[256];
            if (CFStringGetCString(family, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                family_name_ = buf;
            }
            CFRelease(family);
        }

        // Get style name
        CFStringRef style = CTFontCopyName(ct_font_, kCTFontStyleNameKey);
        if (style) {
            char buf[128];
            if (CFStringGetCString(style, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                style_name_ = buf;
            }
            CFRelease(style);
        }

        update_metrics();
    }
}

CoreTextFontFace::~CoreTextFontFace() {
    if (ct_font_) {
        CFRelease(ct_font_);
    }
}

void CoreTextFontFace::update_metrics() {
    if (!ct_font_) return;

    metrics_.ascender = CTFontGetAscent(ct_font_);
    metrics_.descender = -CTFontGetDescent(ct_font_);
    metrics_.line_height = CTFontGetAscent(ct_font_) + CTFontGetDescent(ct_font_) + CTFontGetLeading(ct_font_);
    metrics_.underline_position = CTFontGetUnderlinePosition(ct_font_);
    metrics_.underline_thickness = CTFontGetUnderlineThickness(ct_font_);
    metrics_.units_per_em = CTFontGetUnitsPerEm(ct_font_);

    // Estimate max advance from 'M' glyph
    UniChar ch = 'M';
    CGGlyph glyph;
    CTFontGetGlyphsForCharacters(ct_font_, &ch, &glyph, 1);
    CGSize advance;
    CTFontGetAdvancesForGlyphs(ct_font_, kCTFontOrientationHorizontal, &glyph, &advance, 1);
    metrics_.max_advance = advance.width;
}

uint32_t CoreTextFontFace::get_glyph_index(uint32_t codepoint) const {
    if (!ct_font_) return 0;

    UniChar chars[2];
    int char_count = 0;

    if (codepoint <= 0xFFFF) {
        chars[0] = static_cast<UniChar>(codepoint);
        char_count = 1;
    } else {
        // Surrogate pair for characters > 0xFFFF
        codepoint -= 0x10000;
        chars[0] = static_cast<UniChar>(0xD800 + (codepoint >> 10));
        chars[1] = static_cast<UniChar>(0xDC00 + (codepoint & 0x3FF));
        char_count = 2;
    }

    CGGlyph glyphs[2];
    if (CTFontGetGlyphsForCharacters(ct_font_, chars, glyphs, char_count)) {
        return glyphs[0];
    }

    return 0;
}

bool CoreTextFontFace::get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const {
    if (!ct_font_ || !out_metrics) return false;

    CGGlyph glyph = static_cast<CGGlyph>(glyph_index);

    CGRect bounds;
    CTFontGetBoundingRectsForGlyphs(ct_font_, kCTFontOrientationHorizontal, &glyph, &bounds, 1);

    CGSize advance;
    CTFontGetAdvancesForGlyphs(ct_font_, kCTFontOrientationHorizontal, &glyph, &advance, 1);

    out_metrics->width = bounds.size.width;
    out_metrics->height = bounds.size.height;
    out_metrics->bearing_x = bounds.origin.x;
    out_metrics->bearing_y = bounds.origin.y + bounds.size.height;
    out_metrics->advance_x = advance.width;
    out_metrics->advance_y = advance.height;

    return true;
}

float CoreTextFontFace::get_kerning(uint32_t left_glyph, uint32_t right_glyph) const {
    // Core Text handles kerning automatically in layout
    // Direct kerning query would require accessing the 'kern' table
    (void)left_glyph;
    (void)right_glyph;
    return 0.0f;
}

Result CoreTextFontFace::render_glyph(uint32_t glyph_index, const RenderOptions& options,
                                       GlyphBitmap* out_bitmap) {
    if (!ct_font_ || !out_bitmap) return Result::ErrorInvalidParameter;

    CGGlyph glyph = static_cast<CGGlyph>(glyph_index);

    // Get glyph bounds
    CGRect bounds;
    CTFontGetBoundingRectsForGlyphs(ct_font_, kCTFontOrientationHorizontal, &glyph, &bounds, 1);

    int width = static_cast<int>(std::ceil(bounds.size.width)) + 4;
    int height = static_cast<int>(std::ceil(bounds.size.height)) + 4;

    if (width <= 0 || height <= 0) {
        out_bitmap->width = 0;
        out_bitmap->height = 0;
        out_bitmap->pixels = nullptr;
        return Result::Success;
    }

    // Create bitmap context
    size_t bytes_per_row;
    CGColorSpaceRef color_space;
    CGBitmapInfo bitmap_info;

    if (options.output_format == PixelFormat::A8 || options.antialias == AntiAliasMode::Grayscale) {
        bytes_per_row = width;
        color_space = CGColorSpaceCreateDeviceGray();
        bitmap_info = kCGImageAlphaOnly;
        glyph_buffer_.resize(bytes_per_row * height);
    } else {
        bytes_per_row = width * 4;
        color_space = CGColorSpaceCreateDeviceRGB();
        bitmap_info = kCGImageAlphaPremultipliedLast;
        glyph_buffer_.resize(bytes_per_row * height);
    }

    memset(glyph_buffer_.data(), 0, glyph_buffer_.size());

    CGContextRef context = CGBitmapContextCreate(
        glyph_buffer_.data(),
        width, height,
        8, bytes_per_row,
        color_space,
        bitmap_info
    );

    CGColorSpaceRelease(color_space);

    if (!context) {
        return Result::ErrorRenderFailed;
    }

    // Set antialiasing
    bool antialias = (options.antialias != AntiAliasMode::None);
    CGContextSetAllowsAntialiasing(context, antialias);
    CGContextSetShouldAntialias(context, antialias);

    if (options.antialias == AntiAliasMode::Subpixel || options.antialias == AntiAliasMode::SubpixelBGR) {
        CGContextSetAllowsFontSubpixelQuantization(context, true);
        CGContextSetShouldSubpixelQuantizeFonts(context, true);
        CGContextSetAllowsFontSubpixelPositioning(context, true);
        CGContextSetShouldSubpixelPositionFonts(context, true);
    }

    // Set text color to white
    CGContextSetGrayFillColor(context, 1.0, 1.0);

    // Position and draw glyph
    CGPoint position = CGPointMake(-bounds.origin.x + 2, -bounds.origin.y + 2);

    // Flip coordinate system
    CGContextTranslateCTM(context, 0, height);
    CGContextScaleCTM(context, 1.0, -1.0);

    position.y = height - position.y - bounds.size.height;

    CTFontDrawGlyphs(ct_font_, &glyph, &position, 1, context);

    CGContextRelease(context);

    // Fill output
    out_bitmap->pixels = glyph_buffer_.data();
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->pitch = static_cast<int>(bytes_per_row);
    out_bitmap->format = (bytes_per_row == static_cast<size_t>(width)) ? PixelFormat::A8 : PixelFormat::RGBA8;

    // Fill metrics
    CGSize advance;
    CTFontGetAdvancesForGlyphs(ct_font_, kCTFontOrientationHorizontal, &glyph, &advance, 1);

    out_bitmap->metrics.width = bounds.size.width;
    out_bitmap->metrics.height = bounds.size.height;
    out_bitmap->metrics.bearing_x = bounds.origin.x;
    out_bitmap->metrics.bearing_y = bounds.origin.y + bounds.size.height;
    out_bitmap->metrics.advance_x = advance.width;
    out_bitmap->metrics.advance_y = advance.height;

    return Result::Success;
}

bool CoreTextFontFace::has_glyph(uint32_t codepoint) const {
    return get_glyph_index(codepoint) != 0;
}

int CoreTextFontFace::get_glyph_count() const {
    if (!ct_font_) return 0;
    return static_cast<int>(CTFontGetGlyphCount(ct_font_));
}

Result CoreTextFontFace::set_size(float size) {
    if (size <= 0.0f) return Result::ErrorInvalidParameter;
    if (!ct_font_) return Result::ErrorNotInitialized;

    // Create new font with different size
    CTFontRef new_font = CTFontCreateCopyWithAttributes(ct_font_, size, nullptr, nullptr);
    if (!new_font) {
        return Result::ErrorUnknown;
    }

    CFRelease(ct_font_);
    ct_font_ = new_font;
    descriptor_.size = size;
    update_metrics();

    return Result::Success;
}

// ============================================================================
// Core Text Font Library Implementation
// ============================================================================

class CoreTextFontLibrary : public IFontLibrary {
public:
    CoreTextFontLibrary();
    ~CoreTextFontLibrary();

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

    void enumerate_system_fonts(std::vector<FontDescriptor>& out_fonts) const override;
    bool find_system_font(const FontDescriptor& descriptor,
                           std::string& out_path) const override;

    IFontFace* get_default_font(float size, Result* out_result) override;

    void* get_native_handle() const override { return nullptr; }

private:
    CTFontSymbolicTraits traits_from_descriptor(const FontDescriptor& desc) const;

    bool initialized_ = false;
};

CoreTextFontLibrary::CoreTextFontLibrary() {}

CoreTextFontLibrary::~CoreTextFontLibrary() {
    shutdown();
}

Result CoreTextFontLibrary::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    initialized_ = true;
    return Result::Success;
}

void CoreTextFontLibrary::shutdown() {
    initialized_ = false;
}

IFontFace* CoreTextFontLibrary::load_font_file(const char* filepath, int face_index,
                                                Result* out_result) {
    if (!initialized_ || !filepath) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    (void)face_index; // Core Text typically handles this automatically

    // Create URL from path
    CFStringRef path_str = CFStringCreateWithCString(kCFAllocatorDefault, filepath, kCFStringEncodingUTF8);
    if (!path_str) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_str, kCFURLPOSIXPathStyle, false);
    CFRelease(path_str);

    if (!url) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    // Create font descriptor from URL
    CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);

    if (!descs || CFArrayGetCount(descs) == 0) {
        if (descs) CFRelease(descs);
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    CTFontDescriptorRef desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descs, 0);
    CTFontRef font = CTFontCreateWithFontDescriptor(desc, 12.0, nullptr);
    CFRelease(descs);

    if (!font) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    FontDescriptor font_desc;
    font_desc.size = 12.0f;

    if (out_result) *out_result = Result::Success;
    return new CoreTextFontFace(font, font_desc);
}

IFontFace* CoreTextFontLibrary::load_font_memory(const void* data, size_t size,
                                                  int face_index, Result* out_result) {
    if (!initialized_ || !data || size == 0) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    (void)face_index;

    // Create data provider from memory
    CFDataRef cf_data = CFDataCreate(kCFAllocatorDefault, static_cast<const UInt8*>(data), size);
    if (!cf_data) {
        if (out_result) *out_result = Result::ErrorOutOfMemory;
        return nullptr;
    }

    CGDataProviderRef provider = CGDataProviderCreateWithCFData(cf_data);
    CFRelease(cf_data);

    if (!provider) {
        if (out_result) *out_result = Result::ErrorOutOfMemory;
        return nullptr;
    }

    // Create font from data
    CGFontRef cg_font = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);

    if (!cg_font) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    CTFontRef font = CTFontCreateWithGraphicsFont(cg_font, 12.0, nullptr, nullptr);
    CGFontRelease(cg_font);

    if (!font) {
        if (out_result) *out_result = Result::ErrorInvalidFont;
        return nullptr;
    }

    FontDescriptor font_desc;
    font_desc.size = 12.0f;

    if (out_result) *out_result = Result::Success;
    return new CoreTextFontFace(font, font_desc);
}

IFontFace* CoreTextFontLibrary::load_system_font(const FontDescriptor& descriptor,
                                                  Result* out_result) {
    if (!initialized_) {
        if (out_result) *out_result = Result::ErrorNotInitialized;
        return nullptr;
    }

    CFStringRef family_name = CFStringCreateWithCString(kCFAllocatorDefault, descriptor.family, kCFStringEncodingUTF8);
    if (!family_name) {
        if (out_result) *out_result = Result::ErrorInvalidParameter;
        return nullptr;
    }

    // Create font descriptor with traits
    CTFontSymbolicTraits traits = traits_from_descriptor(descriptor);

    CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontFamilyNameAttribute, family_name);
    CFRelease(family_name);

    if (traits != 0) {
        CFMutableDictionaryRef trait_dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFNumberRef traits_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &traits);
        CFDictionarySetValue(trait_dict, kCTFontSymbolicTrait, traits_num);
        CFRelease(traits_num);
        CFDictionarySetValue(attrs, kCTFontTraitsAttribute, trait_dict);
        CFRelease(trait_dict);
    }

    CTFontDescriptorRef font_desc = CTFontDescriptorCreateWithAttributes(attrs);
    CFRelease(attrs);

    if (!font_desc) {
        if (out_result) *out_result = Result::ErrorUnknown;
        return nullptr;
    }

    CTFontRef font = CTFontCreateWithFontDescriptor(font_desc, descriptor.size, nullptr);
    CFRelease(font_desc);

    if (!font) {
        if (out_result) *out_result = Result::ErrorFileNotFound;
        return nullptr;
    }

    if (out_result) *out_result = Result::Success;
    return new CoreTextFontFace(font, descriptor);
}

void CoreTextFontLibrary::destroy_font(IFontFace* face) {
    delete face;
}

void CoreTextFontLibrary::enumerate_system_fonts(std::vector<FontDescriptor>& out_fonts) const {
    out_fonts.clear();
    if (!initialized_) return;

    // Get all font descriptors
    CTFontCollectionRef collection = CTFontCollectionCreateFromAvailableFonts(nullptr);
    if (!collection) return;

    CFArrayRef descriptors = CTFontCollectionCreateMatchingFontDescriptors(collection);
    CFRelease(collection);

    if (!descriptors) return;

    CFIndex count = CFArrayGetCount(descriptors);

    for (CFIndex i = 0; i < count; ++i) {
        CTFontDescriptorRef desc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(descriptors, i);

        CFStringRef family = (CFStringRef)CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute);
        if (family) {
            FontDescriptor font_desc;
            char buf[256];
            if (CFStringGetCString(family, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                font_desc.family = buf;
            }
            font_desc.size = 12.0f;
            font_desc.weight = FontWeight::Regular;
            font_desc.style = FontStyle::Normal;
            CFRelease(family);
            out_fonts.push_back(font_desc);
        }
    }

    CFRelease(descriptors);
}

bool CoreTextFontLibrary::find_system_font(const FontDescriptor& descriptor,
                                            std::string& out_path) const {
    if (!initialized_) return false;

    // Create font to get its URL
    IFontFace* face = const_cast<CoreTextFontLibrary*>(this)->load_system_font(descriptor, nullptr);
    if (!face) return false;

    CTFontRef font = static_cast<CTFontRef>(face->get_native_handle());
    if (!font) {
        delete face;
        return false;
    }

    CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute);
    delete face;

    if (!url) return false;

    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    CFRelease(url);

    if (!path) return false;

    char buf[1024];
    bool success = CFStringGetCString(path, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(path);

    if (success) {
        out_path = buf;
    }
    return success;
}

IFontFace* CoreTextFontLibrary::get_default_font(float size, Result* out_result) {
#if TARGET_OS_IOS
    FontDescriptor desc = FontDescriptor::create("Helvetica Neue", size);
#else
    FontDescriptor desc = FontDescriptor::create("Helvetica", size);
#endif

    IFontFace* face = load_system_font(desc, out_result);

    if (!face) {
        // Fallback
        desc = FontDescriptor::create("Arial", size);
        face = load_system_font(desc, out_result);
    }

    return face;
}

CTFontSymbolicTraits CoreTextFontLibrary::traits_from_descriptor(const FontDescriptor& desc) const {
    CTFontSymbolicTraits traits = 0;

    if (static_cast<int>(desc.weight) >= static_cast<int>(FontWeight::Bold)) {
        traits |= kCTFontTraitBold;
    }

    if (desc.style == FontStyle::Italic || desc.style == FontStyle::Oblique) {
        traits |= kCTFontTraitItalic;
    }

    return traits;
}

// ============================================================================
// Factory Functions
// ============================================================================

IFontLibrary* create_font_library(FontBackend backend, Result* out_result) {
    if (backend != FontBackend::Auto && backend != FontBackend::Native) {
        if (out_result) *out_result = Result::ErrorBackendNotSupported;
        return nullptr;
    }

    CoreTextFontLibrary* library = new CoreTextFontLibrary();
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

#endif // __APPLE__
