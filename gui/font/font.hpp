/*
 * font.hpp - Cross-Platform Font Rendering Interface
 *
 * This is an independent module for font loading and text rendering.
 * Provides abstract interfaces with multiple backend implementations:
 *
 * Native backends (use system font rendering):
 *   - Windows: DirectWrite / GDI
 *   - macOS/iOS: Core Text
 *   - Linux: Fontconfig + Pango/Cairo
 *   - Android: Skia (via NDK)
 *
 * FreeType2 backend (cross-platform):
 *   - Uses FreeType2 library for font parsing and rasterization
 *   - Works on all platforms
 *   - Supports TrueType, OpenType, and other font formats
 */

#ifndef FONT_HPP
#define FONT_HPP

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>

// Include graphics API types (TextureFormat, etc.)
#include "../../graphics_api.hpp"

namespace font {

// ============================================================================
// Constants
// ============================================================================

static constexpr int MAX_FONT_FAMILY_LENGTH = 256;
static constexpr int MAX_FONT_PATH_LENGTH = 1024;
static constexpr int MAX_FONT_FACES = 64;
static constexpr int MAX_GLYPH_CACHE_SIZE = 4096;

// ============================================================================
// Enums
// ============================================================================

enum class Result : uint8_t {
    Success = 0,
    ErrorUnknown,
    ErrorNotInitialized,
    ErrorAlreadyInitialized,
    ErrorFileNotFound,
    ErrorInvalidFont,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorGlyphNotFound,
    ErrorRenderFailed,
    ErrorBackendNotSupported
};

enum class FontBackend : uint8_t {
    Auto = 0,           // Use best available backend
    Native,             // Platform-specific (DirectWrite, CoreText, etc.)
    FreeType            // FreeType2 library
};

enum class FontWeight : uint16_t {
    Thin = 100,
    ExtraLight = 200,
    Light = 300,
    Regular = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900
};

enum class FontStyle : uint8_t {
    Normal = 0,
    Italic,
    Oblique
};

enum class FontStretch : uint8_t {
    UltraCondensed = 1,
    ExtraCondensed = 2,
    Condensed = 3,
    SemiCondensed = 4,
    Normal = 5,
    SemiExpanded = 6,
    Expanded = 7,
    ExtraExpanded = 8,
    UltraExpanded = 9
};

enum class TextAlignment : uint8_t {
    Left = 0,
    Center,
    Right,
    Justified
};

enum class TextDirection : uint8_t {
    LeftToRight = 0,
    RightToLeft,
    TopToBottom
};

enum class AntiAliasMode : uint8_t {
    None = 0,           // No anti-aliasing (1-bit)
    Grayscale,          // 8-bit grayscale
    Subpixel,           // LCD subpixel (RGB/BGR)
    SubpixelBGR         // LCD subpixel BGR order
};

enum class HintingMode : uint8_t {
    None = 0,           // No hinting
    Light,              // Light auto-hinting
    Normal,             // Normal hinting
    Full                // Full hinting (may look sharper but less smooth)
};

enum class PixelFormat : uint8_t {
    A8 = 0,             // 8-bit alpha (grayscale)
    RGBA8,              // 32-bit RGBA
    BGRA8,              // 32-bit BGRA
    RGB8,               // 24-bit RGB (subpixel)
    BGR8                // 24-bit BGR (subpixel)
};

// ============================================================================
// Basic Types
// ============================================================================

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Size {
    float width = 0.0f;
    float height = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Color {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    Color() = default;
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    static Color white() { return Color(255, 255, 255, 255); }
    static Color black() { return Color(0, 0, 0, 255); }
};

// ============================================================================
// Font Descriptor
// ============================================================================

struct FontDescriptor {
    char family[MAX_FONT_FAMILY_LENGTH] = {};
    float size = 12.0f;                     // Size in points
    FontWeight weight = FontWeight::Regular;
    FontStyle style = FontStyle::Normal;
    FontStretch stretch = FontStretch::Normal;

    // Create descriptor from family name and size
    static FontDescriptor create(const char* family, float size) {
        FontDescriptor desc;
        if (family) {
            strncpy(desc.family, family, MAX_FONT_FAMILY_LENGTH - 1);
        }
        desc.size = size;
        return desc;
    }

    // Create bold variant
    FontDescriptor bold() const {
        FontDescriptor desc = *this;
        desc.weight = FontWeight::Bold;
        return desc;
    }

    // Create italic variant
    FontDescriptor italic() const {
        FontDescriptor desc = *this;
        desc.style = FontStyle::Italic;
        return desc;
    }
};

// ============================================================================
// Glyph Information
// ============================================================================

struct GlyphMetrics {
    float width = 0.0f;             // Glyph width
    float height = 0.0f;            // Glyph height
    float bearing_x = 0.0f;         // Left side bearing
    float bearing_y = 0.0f;         // Top side bearing
    float advance_x = 0.0f;         // Horizontal advance
    float advance_y = 0.0f;         // Vertical advance (usually 0 for horizontal text)
};

struct GlyphBitmap {
    void* pixels = nullptr;         // Pixel data (owned by font system)
    int width = 0;                  // Bitmap width in pixels
    int height = 0;                 // Bitmap height in pixels
    int pitch = 0;                  // Bytes per row
    PixelFormat format = PixelFormat::A8;
    GlyphMetrics metrics;
};

// ============================================================================
// Font Metrics
// ============================================================================

struct FontMetrics {
    float ascender = 0.0f;          // Distance from baseline to top
    float descender = 0.0f;         // Distance from baseline to bottom (negative)
    float line_height = 0.0f;       // Recommended line height
    float max_advance = 0.0f;       // Maximum horizontal advance
    float underline_position = 0.0f;
    float underline_thickness = 0.0f;
    float strikethrough_position = 0.0f;
    float strikethrough_thickness = 0.0f;
    float units_per_em = 0.0f;      // Font design units per EM
};

// ============================================================================
// Text Layout
// ============================================================================

struct TextLayoutOptions {
    float max_width = 0.0f;         // 0 = no wrapping
    float max_height = 0.0f;        // 0 = no height limit
    float line_spacing = 1.0f;      // Line spacing multiplier
    float letter_spacing = 0.0f;    // Additional spacing between characters
    float word_spacing = 0.0f;      // Additional spacing between words
    TextAlignment alignment = TextAlignment::Left;
    TextDirection direction = TextDirection::LeftToRight;
    bool wrap_words = true;         // Wrap at word boundaries
    bool ellipsis = false;          // Add ellipsis when truncated
    int tab_width = 4;              // Tab width in spaces
};

struct TextLayoutResult {
    Rect bounds;                    // Bounding rectangle of laid out text
    int line_count = 0;             // Number of lines
    int char_count = 0;             // Number of characters laid out
    bool truncated = false;         // True if text was truncated
};

// Positioned glyph for rendering
struct PositionedGlyph {
    uint32_t codepoint = 0;
    uint32_t glyph_index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float advance = 0.0f;
    int cluster = 0;                // Character cluster index
};

// ============================================================================
// Render Options
// ============================================================================

struct RenderOptions {
    AntiAliasMode antialias = AntiAliasMode::Grayscale;
    HintingMode hinting = HintingMode::Normal;
    PixelFormat output_format = PixelFormat::A8;
    float gamma = 1.0f;             // Gamma correction (1.0 = none)
    bool lcd_filter = true;         // Apply LCD filter for subpixel
};

// ============================================================================
// Font Face Interface (Abstract)
// ============================================================================

class IFontFace {
public:
    virtual ~IFontFace() = default;

    // Get font information
    virtual const FontDescriptor& get_descriptor() const = 0;
    virtual const FontMetrics& get_metrics() const = 0;
    virtual const char* get_family_name() const = 0;
    virtual const char* get_style_name() const = 0;

    // Glyph operations
    virtual uint32_t get_glyph_index(uint32_t codepoint) const = 0;
    virtual bool get_glyph_metrics(uint32_t glyph_index, GlyphMetrics* out_metrics) const = 0;
    virtual float get_kerning(uint32_t left_glyph, uint32_t right_glyph) const = 0;

    // Render a single glyph
    virtual Result render_glyph(uint32_t glyph_index, const RenderOptions& options,
                                GlyphBitmap* out_bitmap) = 0;

    // Check if glyph exists
    virtual bool has_glyph(uint32_t codepoint) const = 0;

    // Get number of glyphs in font
    virtual int get_glyph_count() const = 0;

    // Set size (may be called to resize)
    virtual Result set_size(float size) = 0;
    virtual float get_size() const = 0;

    // Native handle (platform-specific)
    virtual void* get_native_handle() const = 0;
};

// ============================================================================
// Font Library Interface (Abstract)
// ============================================================================

class IFontLibrary {
public:
    virtual ~IFontLibrary() = default;

    // Lifecycle
    virtual Result initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Get backend type
    virtual FontBackend get_backend() const = 0;

    // Load font from file
    virtual IFontFace* load_font_file(const char* filepath, int face_index = 0,
                                       Result* out_result = nullptr) = 0;

    // Load font from memory
    virtual IFontFace* load_font_memory(const void* data, size_t size, int face_index = 0,
                                         Result* out_result = nullptr) = 0;

    // Load system font by family name
    virtual IFontFace* load_system_font(const FontDescriptor& descriptor,
                                         Result* out_result = nullptr) = 0;

    // Destroy a font face
    virtual void destroy_font(IFontFace* face) = 0;

    // Enumerate system fonts
    virtual void enumerate_system_fonts(std::vector<FontDescriptor>& out_fonts) const = 0;

    // Find best matching system font
    virtual bool find_system_font(const FontDescriptor& descriptor,
                                   char* out_path, int path_size) const = 0;

    // Get default system font
    virtual IFontFace* get_default_font(float size, Result* out_result = nullptr) = 0;

    // Native handle
    virtual void* get_native_handle() const = 0;
};

// ============================================================================
// Text Shaper Interface (Abstract)
// ============================================================================

// Handles complex text shaping (ligatures, combining characters, BiDi, etc.)
class ITextShaper {
public:
    virtual ~ITextShaper() = default;

    // Shape text and return positioned glyphs
    virtual void shape_text(IFontFace* font, const char* text, int text_length,
                            std::vector<PositionedGlyph>& out_glyphs,
                            const TextLayoutOptions& options = TextLayoutOptions()) = 0;

    // Layout text with wrapping
    virtual TextLayoutResult layout_text(IFontFace* font, const char* text, int text_length,
                                          std::vector<PositionedGlyph>& out_glyphs,
                                          const TextLayoutOptions& options) = 0;

    // Measure text without rendering
    virtual Size measure_text(IFontFace* font, const char* text, int text_length,
                              const TextLayoutOptions& options = TextLayoutOptions()) = 0;

    // Get caret positions for text
    virtual void get_caret_positions(IFontFace* font, const char* text, int text_length,
                                      std::vector<float>& out_positions) = 0;

    // Hit test - find character index at position
    virtual int hit_test(IFontFace* font, const char* text, int text_length,
                         float x, float y, const TextLayoutOptions& options = TextLayoutOptions()) = 0;
};

// ============================================================================
// Font Renderer Interface (Abstract)
// ============================================================================

// High-level text rendering to a bitmap
class IFontRenderer {
public:
    virtual ~IFontRenderer() = default;

    // Render text to a new bitmap
    // Caller must free the returned pixels with free_bitmap()
    virtual Result render_text(IFontFace* font, const char* text, int text_length,
                                const Color& color, const RenderOptions& render_opts,
                                const TextLayoutOptions& layout_opts,
                                void** out_pixels, int* out_width, int* out_height,
                                PixelFormat* out_format) = 0;

    // Render text to existing bitmap
    virtual Result render_text_to_bitmap(IFontFace* font, const char* text, int text_length,
                                          const Color& color, const RenderOptions& render_opts,
                                          const TextLayoutOptions& layout_opts,
                                          void* bitmap, int bitmap_width, int bitmap_height,
                                          int bitmap_pitch, PixelFormat bitmap_format,
                                          int x, int y) = 0;

    // Render pre-shaped glyphs
    virtual Result render_glyphs(IFontFace* font, const PositionedGlyph* glyphs, int glyph_count,
                                  const Color& color, const RenderOptions& render_opts,
                                  void* bitmap, int bitmap_width, int bitmap_height,
                                  int bitmap_pitch, PixelFormat bitmap_format,
                                  int x, int y) = 0;

    // Free bitmap allocated by render_text
    virtual void free_bitmap(void* pixels) = 0;

    // ========================================================================
    // Texture Rendering (Abstract - requires graphics backend integration)
    // ========================================================================

    // Texture descriptor for output
    // Uses window::TextureFormat from window.hpp for format specification
    struct TextureDesc {
        void* handle = nullptr;         // Native texture handle (ID3D11Texture2D*, GLuint, MTLTexture*, etc.)
        void* view = nullptr;           // Shader resource view if applicable (ID3D11ShaderResourceView*, etc.)
        int width = 0;
        int height = 0;
        window::TextureFormat format;   // Texture format (see window.hpp for all formats)
        void* user_data = nullptr;      // User-provided context
    };

    // Callback for texture creation (implement to integrate with your graphics API)
    // Return true on success, false on failure
    // The callback should:
    //   1. Create a texture with the specified dimensions and format
    //   2. Fill desc->handle with the native texture handle
    //   3. Optionally fill desc->view with a shader resource view
    // Note: Use lambda captures to pass context instead of user_data
    using TextureCreateCallback = std::function<bool(int width, int height,
                                                      window::TextureFormat format,
                                                      TextureDesc* desc)>;

    // Callback for texture upload (implement to upload pixel data to texture)
    // pixels: Source pixel data in the format specified during creation
    // Return true on success, false on failure
    using TextureUploadCallback = std::function<bool(TextureDesc* desc, const void* pixels,
                                                      int width, int height, int pitch)>;

    // Callback for texture destruction (implement to release texture resources)
    using TextureDestroyCallback = std::function<void(TextureDesc* desc)>;

    // Render text directly to a GPU texture
    // This method creates a texture, renders text to it, and returns the texture descriptor
    // The caller is responsible for managing the texture lifetime via the destroy callback
    //
    // Parameters:
    //   font: Font face to use
    //   text: UTF-8 encoded text string
    //   text_length: Length of text in bytes (-1 for null-terminated)
    //   color: Text color
    //   render_opts: Rendering options (antialiasing, hinting, etc.)
    //   layout_opts: Layout options (alignment, wrapping, etc.)
    //   texture_format: Desired texture format (from window::TextureFormat)
    //                   Common choices: R8_UNORM (grayscale), RGBA8_UNORM, BGRA8_UNORM
    //   create_callback: Function to create the texture (use lambda captures for context)
    //   upload_callback: Function to upload pixel data (use lambda captures for context)
    //   out_desc: Output texture descriptor
    //
    // Returns: Result::Success on success, error code on failure
    //
    // Example usage:
    //   ID3D11Device* device = ...;
    //   renderer->render_text_to_texture(font, "Hello", -1, color, render_opts, layout_opts,
    //       window::TextureFormat::RGBA8_UNORM,
    //       [device](int w, int h, window::TextureFormat fmt, TextureDesc* desc) {
    //           // Create D3D11 texture using captured device
    //           return create_d3d11_texture(device, w, h, fmt, desc);
    //       },
    //       [](TextureDesc* desc, const void* pixels, int w, int h, int pitch) {
    //           // Upload pixels to texture
    //           return upload_d3d11_texture(desc, pixels, w, h, pitch);
    //       },
    //       &out_desc);
    virtual Result render_text_to_texture(IFontFace* font, const char* text, int text_length,
                                           const Color& color, const RenderOptions& render_opts,
                                           const TextLayoutOptions& layout_opts,
                                           window::TextureFormat texture_format,
                                           const TextureCreateCallback& create_callback,
                                           const TextureUploadCallback& upload_callback,
                                           TextureDesc* out_desc) = 0;

    // Render pre-shaped glyphs directly to a GPU texture
    virtual Result render_glyphs_to_texture(IFontFace* font,
                                             const PositionedGlyph* glyphs, int glyph_count,
                                             const Color& color, const RenderOptions& render_opts,
                                             window::TextureFormat texture_format,
                                             const TextureCreateCallback& create_callback,
                                             const TextureUploadCallback& upload_callback,
                                             TextureDesc* out_desc) = 0;
};

// ============================================================================
// Glyph Cache Interface (Abstract)
// ============================================================================

struct CachedGlyph {
    GlyphBitmap bitmap;
    uint32_t glyph_index = 0;
    float size = 0.0f;
    AntiAliasMode antialias = AntiAliasMode::Grayscale;
    bool valid = false;
};

class IGlyphCache {
public:
    virtual ~IGlyphCache() = default;

    // Get or render a glyph (returns cached version if available)
    virtual const CachedGlyph* get_glyph(IFontFace* font, uint32_t glyph_index,
                                          const RenderOptions& options) = 0;

    // Clear cache for a specific font
    virtual void clear_font(IFontFace* font) = 0;

    // Clear entire cache
    virtual void clear_all() = 0;

    // Get cache statistics
    virtual int get_cached_count() const = 0;
    virtual size_t get_memory_usage() const = 0;

    // Set cache limits
    virtual void set_max_glyphs(int max_glyphs) = 0;
    virtual void set_max_memory(size_t max_bytes) = 0;
};

// ============================================================================
// Font System (Combines all interfaces)
// ============================================================================

class FontSystem {
public:
    virtual ~FontSystem() = default;

    // Get subsystem interfaces
    virtual IFontLibrary* get_library() = 0;
    virtual ITextShaper* get_shaper() = 0;
    virtual IFontRenderer* get_renderer() = 0;
    virtual IGlyphCache* get_cache() = 0;

    // Convenience methods
    virtual IFontFace* load_font(const char* filepath, float size, Result* out_result = nullptr) = 0;
    virtual IFontFace* load_system_font(const char* family, float size, Result* out_result = nullptr) = 0;
    virtual Size measure_text(IFontFace* font, const char* text) = 0;
    virtual Result render_text(IFontFace* font, const char* text, const Color& color,
                                void** out_pixels, int* out_width, int* out_height) = 0;
};

// ============================================================================
// Factory Functions
// ============================================================================

// Create font library with specified backend
IFontLibrary* create_font_library(FontBackend backend = FontBackend::Auto,
                                   Result* out_result = nullptr);

// Create text shaper
ITextShaper* create_text_shaper(IFontLibrary* library, Result* out_result = nullptr);

// Create font renderer
IFontRenderer* create_font_renderer(IFontLibrary* library, Result* out_result = nullptr);

// Create glyph cache
IGlyphCache* create_glyph_cache(int max_glyphs = MAX_GLYPH_CACHE_SIZE);

// Create complete font system
FontSystem* create_font_system(FontBackend backend = FontBackend::Auto,
                                Result* out_result = nullptr);

// Destroy instances
void destroy_font_library(IFontLibrary* library);
void destroy_text_shaper(ITextShaper* shaper);
void destroy_font_renderer(IFontRenderer* renderer);
void destroy_glyph_cache(IGlyphCache* cache);
void destroy_font_system(FontSystem* system);

// ============================================================================
// Utility Functions
// ============================================================================

const char* result_to_string(Result result);
const char* font_backend_to_string(FontBackend backend);
const char* font_weight_to_string(FontWeight weight);
const char* font_style_to_string(FontStyle style);

// Check if a backend is available
bool is_backend_available(FontBackend backend);

// Get default/recommended backend for current platform
FontBackend get_default_backend();

// UTF-8 utilities
int utf8_to_codepoint(const char* str, uint32_t* out_codepoint);
int codepoint_to_utf8(uint32_t codepoint, char* out_str);
int utf8_strlen(const char* str);

} // namespace font

#endif // FONT_HPP
