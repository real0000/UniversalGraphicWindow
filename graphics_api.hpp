/*
 * graphics_api.hpp - Graphics API Types and Interfaces
 *
 * This header contains graphics-related enums, structs, and interfaces
 * that are independent of the windowing system. Can be used standalone
 * for graphics context management on external windows.
 *
 * Graphics backends:
 *   - OpenGL/OpenGL ES
 *   - Vulkan
 *   - Direct3D 11/12
 *   - Metal
 */

#ifndef GRAPHICS_API_HPP
#define GRAPHICS_API_HPP

#include <cstdint>
#include <cstddef>
#include <string>

namespace window {

//=============================================================================
// Constants
//=============================================================================
static const int MAX_DEVICES = 16;
static const int MAX_MONITORS = 16;
static const int MAX_DISPLAY_MODES = 256;

//=============================================================================
// Result Codes
//=============================================================================

enum class Result {
    Success = 0,
    ErrorUnknown,
    ErrorPlatformInit,
    ErrorWindowCreation,
    ErrorGraphicsInit,
    ErrorNotSupported,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorDeviceLost
};

const char* result_to_string(Result result);

//=============================================================================
// Graphics Backend
//=============================================================================

enum class Backend {
    Auto = 0,   // Auto-select best backend for platform
    OpenGL,
    Vulkan,
    D3D11,
    D3D12,
    Metal
};

const char* backend_to_string(Backend backend);
bool is_backend_supported(Backend backend);
Backend get_default_backend();

//=============================================================================
// Swap Chain Mode
//=============================================================================

// Swap chain presentation mode
enum class SwapMode : uint8_t {
    Fifo = 0,           // VSync ON - wait for vertical blank (default, no tearing)
    FifoRelaxed,        // Adaptive VSync - like Fifo but may tear if frame is late
    Mailbox,            // Triple buffering - low latency, no tearing (if supported)
    Immediate,          // VSync OFF - no waiting, lowest latency, may tear
    Auto                // Auto-select based on vsync preference
};

const char* swap_mode_to_string(SwapMode mode);
bool parse_swap_mode(const char* value, SwapMode* out);

//=============================================================================
// Texture Format
//=============================================================================

// Texture/pixel formats (cross-API compatible)
enum class TextureFormat : uint16_t {
    Unknown = 0,

    // 8-bit formats
    R8_UNORM,               // Single channel, normalized [0,1]
    R8_SNORM,               // Single channel, signed normalized [-1,1]
    R8_UINT,                // Single channel, unsigned integer
    R8_SINT,                // Single channel, signed integer

    // 16-bit formats (single channel)
    R16_UNORM,
    R16_SNORM,
    R16_UINT,
    R16_SINT,
    R16_FLOAT,              // Half float

    // 16-bit formats (two channels)
    RG8_UNORM,
    RG8_SNORM,
    RG8_UINT,
    RG8_SINT,

    // 32-bit formats (single channel)
    R32_UINT,
    R32_SINT,
    R32_FLOAT,

    // 32-bit formats (two channels)
    RG16_UNORM,
    RG16_SNORM,
    RG16_UINT,
    RG16_SINT,
    RG16_FLOAT,

    // 32-bit formats (four channels)
    RGBA8_UNORM,            // Standard 32-bit RGBA
    RGBA8_UNORM_SRGB,       // sRGB color space
    RGBA8_SNORM,
    RGBA8_UINT,
    RGBA8_SINT,
    BGRA8_UNORM,            // Common on Windows/D3D
    BGRA8_UNORM_SRGB,

    // 32-bit packed formats
    RGB10A2_UNORM,          // 10-bit RGB, 2-bit alpha
    RGB10A2_UINT,
    RG11B10_FLOAT,          // HDR format (no alpha)
    RGB9E5_FLOAT,           // Shared exponent HDR

    // 64-bit formats (two channels)
    RG32_UINT,
    RG32_SINT,
    RG32_FLOAT,

    // 64-bit formats (four channels)
    RGBA16_UNORM,
    RGBA16_SNORM,
    RGBA16_UINT,
    RGBA16_SINT,
    RGBA16_FLOAT,           // Half float RGBA

    // 128-bit formats
    RGBA32_UINT,
    RGBA32_SINT,
    RGBA32_FLOAT,           // Full float RGBA

    // Depth/stencil formats
    D16_UNORM,              // 16-bit depth
    D24_UNORM_S8_UINT,      // 24-bit depth, 8-bit stencil
    D32_FLOAT,              // 32-bit float depth
    D32_FLOAT_S8_UINT,      // 32-bit float depth, 8-bit stencil

    // Compressed formats - BC (DirectX) / DXT
    BC1_UNORM,              // DXT1 - RGB (1-bit alpha)
    BC1_UNORM_SRGB,
    BC2_UNORM,              // DXT3 - RGBA (explicit alpha)
    BC2_UNORM_SRGB,
    BC3_UNORM,              // DXT5 - RGBA (interpolated alpha)
    BC3_UNORM_SRGB,
    BC4_UNORM,              // Single channel
    BC4_SNORM,
    BC5_UNORM,              // Two channels (normal maps)
    BC5_SNORM,
    BC6H_UF16,              // HDR (unsigned float)
    BC6H_SF16,              // HDR (signed float)
    BC7_UNORM,              // High quality RGBA
    BC7_UNORM_SRGB,

    // Compressed formats - ETC/EAC (OpenGL ES / Mobile)
    ETC1_RGB8,              // ETC1
    ETC2_RGB8,              // ETC2 RGB
    ETC2_RGB8_SRGB,
    ETC2_RGBA8,             // ETC2 RGBA
    ETC2_RGBA8_SRGB,
    ETC2_RGB8A1,            // ETC2 RGB with 1-bit alpha
    ETC2_RGB8A1_SRGB,
    EAC_R11_UNORM,          // EAC single channel
    EAC_R11_SNORM,
    EAC_RG11_UNORM,         // EAC two channels
    EAC_RG11_SNORM,

    // Compressed formats - ASTC (Adaptive Scalable)
    ASTC_4x4_UNORM,
    ASTC_4x4_SRGB,
    ASTC_5x4_UNORM,
    ASTC_5x4_SRGB,
    ASTC_5x5_UNORM,
    ASTC_5x5_SRGB,
    ASTC_6x5_UNORM,
    ASTC_6x5_SRGB,
    ASTC_6x6_UNORM,
    ASTC_6x6_SRGB,
    ASTC_8x5_UNORM,
    ASTC_8x5_SRGB,
    ASTC_8x6_UNORM,
    ASTC_8x6_SRGB,
    ASTC_8x8_UNORM,
    ASTC_8x8_SRGB,
    ASTC_10x5_UNORM,
    ASTC_10x5_SRGB,
    ASTC_10x6_UNORM,
    ASTC_10x6_SRGB,
    ASTC_10x8_UNORM,
    ASTC_10x8_SRGB,
    ASTC_10x10_UNORM,
    ASTC_10x10_SRGB,
    ASTC_12x10_UNORM,
    ASTC_12x10_SRGB,
    ASTC_12x12_UNORM,
    ASTC_12x12_SRGB,

    // Legacy/compatibility formats
    A8_UNORM,               // Alpha only (legacy)
    L8_UNORM,               // Luminance (legacy)
    LA8_UNORM,              // Luminance + Alpha (legacy)

    Count                   // Number of formats
};

// TextureFormat utilities
const char* texture_format_to_string(TextureFormat format);
bool        parse_texture_format(const char* s, TextureFormat* out);
int         texture_format_bytes_per_pixel(TextureFormat format);
int         texture_format_block_size(TextureFormat format);     // Compressed block size in pixels (1 for non-compressed)
bool        texture_format_is_compressed(TextureFormat format);
bool        texture_format_is_depth_stencil(TextureFormat format);
bool        texture_format_is_srgb(TextureFormat format);
bool        texture_format_has_alpha(TextureFormat format);

//=============================================================================
// Graphics Device Information
//=============================================================================

// Information about a graphics device (GPU)
struct GraphicsDeviceInfo {
    std::string name;                           // Device name (e.g., "NVIDIA GeForce RTX 3080")
    std::string vendor;                         // Vendor name (e.g., "NVIDIA")
    uint32_t device_id = 0;                     // Unique device identifier
    uint32_t vendor_id = 0;                     // Vendor identifier
    uint64_t dedicated_video_memory = 0;        // Dedicated VRAM in bytes
    uint64_t dedicated_system_memory = 0;       // Dedicated system memory in bytes
    uint64_t shared_system_memory = 0;          // Shared system memory in bytes
    Backend backend = Backend::Auto;            // Which backend this device is for
    int device_index = 0;                       // Index for selection
    bool is_default = false;                    // True if this is the system default device
};

// Enumeration results
struct DeviceEnumeration {
    GraphicsDeviceInfo devices[MAX_DEVICES] = {};
    int device_count = 0;
};

// Enumerate available graphics devices for a specific backend (or all backends if Auto)
// Returns the number of devices found
int enumerate_devices(Backend backend, DeviceEnumeration* out_devices);

//=============================================================================
// Display/Monitor Information
//=============================================================================

// Display mode (resolution + refresh rate)
struct DisplayMode {
    int width = 0;
    int height = 0;
    int refresh_rate = 0;       // In Hz (e.g., 60, 120, 144)
    int bits_per_pixel = 32;    // Color depth
    bool is_native = false;     // True if this is the monitor's native resolution
};

// Information about a monitor/display
struct MonitorInfo {
    std::string name;                           // Monitor name
    int x = 0;                                  // Position X
    int y = 0;                                  // Position Y
    int width = 0;                              // Current width
    int height = 0;                             // Current height
    int refresh_rate = 0;                       // Current refresh rate
    bool is_primary = false;                    // True if primary monitor
    int monitor_index = 0;                      // Index for selection

    DisplayMode modes[MAX_DISPLAY_MODES] = {};  // Available display modes
    int mode_count = 0;                         // Number of available modes
};

struct MonitorEnumeration {
    MonitorInfo monitors[MAX_MONITORS] = {};
    int monitor_count = 0;
};

// Enumerate available monitors and their display modes
// Returns the number of monitors found
int enumerate_monitors(MonitorEnumeration* out_monitors);

// Find the best matching display mode for a monitor
// Returns true if a matching mode was found
bool find_display_mode(const MonitorInfo& monitor, int width, int height, int refresh_rate, DisplayMode* out_mode);

// Get the primary monitor info
bool get_primary_monitor(MonitorInfo* out_monitor);

//=============================================================================
// GraphicsCapabilities
//=============================================================================

struct GraphicsCapabilities {

    //-------------------------------------------------------------------------
    // Texture size limits
    //-------------------------------------------------------------------------

    int max_texture_size        = 0;  // Max width/height of a 2D texture (e.g. 16384)
    int max_texture_3d_size     = 0;  // Max dimension of a 3D texture
    int max_texture_cube_size   = 0;  // Max face dimension of a cubemap texture
    int max_texture_array_layers = 0; // Max number of layers in a texture array
    int max_mip_levels          = 0;  // Max mipmap chain depth (typically log2(max_texture_size)+1)

    //-------------------------------------------------------------------------
    // Render-target / framebuffer limits
    //-------------------------------------------------------------------------

    int max_color_attachments   = 1;  // Max simultaneous render targets (MRT)
    int max_framebuffer_width   = 0;  // Max renderable width  (0 = same as max_texture_size)
    int max_framebuffer_height  = 0;  // Max renderable height (0 = same as max_texture_size)
    int max_samples             = 1;  // Max MSAA sample count (1 = no MSAA)

    //-------------------------------------------------------------------------
    // Sampling limits
    //-------------------------------------------------------------------------

    int   max_anisotropy        = 1;  // Max anisotropic filtering level (1 = disabled)
    int   max_texture_bindings  = 0;  // Max simultaneous textures per shader stage
    float max_texture_lod_bias  = 0.0f; // Max absolute LOD bias value

    //-------------------------------------------------------------------------
    // Vertex / buffer limits
    //-------------------------------------------------------------------------

    int max_vertex_attributes   = 0;  // Max vertex input attributes (VS input slots)
    int max_vertex_buffers      = 0;  // Max simultaneously-bound vertex buffers
    int max_uniform_buffer_size = 0;  // Max size of a single constant/uniform buffer (bytes)
    int max_uniform_bindings    = 0;  // Max constant/uniform buffer binding slots per stage
    int max_storage_bindings    = 0;  // Max storage/UAV buffer slots per stage (0 = unsupported)

    //-------------------------------------------------------------------------
    // Draw call limits
    //-------------------------------------------------------------------------

    int max_draw_indirect_count = 0;  // Max count for indirect/multi-draw  (0 = unsupported)
    int max_viewports           = 1;  // Max simultaneous viewports (D3D11/D3D12/Vulkan >= 1)
    int max_scissor_rects       = 1;  // Max simultaneous scissor rectangles

    //-------------------------------------------------------------------------
    // Compute limits  (zeros indicate compute is unsupported)
    //-------------------------------------------------------------------------

    int max_compute_group_size_x    = 0; // Max thread group X dimension
    int max_compute_group_size_y    = 0; // Max thread group Y dimension
    int max_compute_group_size_z    = 0; // Max thread group Z dimension
    int max_compute_group_total     = 0; // Max total threads per group (x*y*z)
    int max_compute_dispatch_x      = 0; // Max dispatch X group count
    int max_compute_dispatch_y      = 0; // Max dispatch Y group count
    int max_compute_dispatch_z      = 0; // Max dispatch Z group count

    //-------------------------------------------------------------------------
    // Shader / pipeline feature support
    //-------------------------------------------------------------------------

    float shader_model          = 0.0f; // Shader model version (4.0, 5.0, 6.0, ...)
    bool  compute_shaders       = false;
    bool  geometry_shaders      = false;
    bool  tessellation          = false;  // Hull + Domain shaders / Tessellation stages
    bool  mesh_shaders          = false;  // Mesh + Amplification shaders (DX12 / Vulkan 1.2+)

    //-------------------------------------------------------------------------
    // Draw feature support
    //-------------------------------------------------------------------------

    bool instancing             = true;   // Instanced drawing
    bool indirect_draw          = false;  // DrawIndirect / DrawIndexedIndirect
    bool multi_draw_indirect    = false;  // MultiDrawIndirect (OpenGL / Vulkan)
    bool base_vertex_draw       = false;  // glDrawElementsBaseVertex / equivalent
    bool occlusion_query        = false;  // GPU occlusion query
    bool timestamp_query        = false;  // GPU timestamp / pipeline statistics query

    //-------------------------------------------------------------------------
    // Rasteriser feature support
    //-------------------------------------------------------------------------

    bool depth_clamp            = false;  // Clamp depth instead of clip
    bool fill_mode_wireframe    = false;  // Wireframe fill mode
    bool conservative_raster    = false;  // Conservative rasterisation
    bool line_smooth            = false;  // Anti-aliased line rasterisation

    //-------------------------------------------------------------------------
    // Texture feature support
    //-------------------------------------------------------------------------

    bool texture_compression_bc   = false; // BC1-BC7  (DXT/S3TC, ubiquitous on desktop)
    bool texture_compression_etc2 = false; // ETC2/EAC (OpenGL ES, Android, many desktop GL)
    bool texture_compression_astc = false; // ASTC     (mobile, Apple Silicon, some desktop)
    bool floating_point_textures  = false; // R16F/R32F/RGBA16F/RGBA32F sampling
    bool integer_textures         = false; // R8UI/R16I/... integer fetch
    bool texture_arrays           = false; // 2D texture arrays
    bool texture_3d               = true;  // 3D textures
    bool cube_maps                = true;  // Cubemap textures
    bool cube_map_arrays          = false; // Cubemap array textures
    bool render_to_texture        = true;  // Render to a texture (FBO / RTV)
    bool read_write_textures      = false; // Read/Write (UAV / image) texture access
    bool sparse_textures          = false; // Partially-resident / tiled textures

    //-------------------------------------------------------------------------
    // Format support
    //-------------------------------------------------------------------------

    bool srgb_framebuffer       = false;  // sRGB-capable swap chain / FBO
    bool srgb_textures          = false;  // sRGB texture formats
    bool hdr_output             = false;  // 10-bit/16-bit HDR swap chain output
    bool depth32f               = false;  // 32-bit float depth buffer
    bool stencil8               = false;  // 8-bit stencil buffer

    //-------------------------------------------------------------------------
    // Blend state support
    //-------------------------------------------------------------------------

    bool dual_source_blend      = false;  // SRC1_COLOR / SRC1_ALPHA blend factors
    bool independent_blend      = false;  // Per-render-target blend states
    bool logic_ops              = false;  // Logical blend operations

    //-------------------------------------------------------------------------
    // Sync / presentation
    //-------------------------------------------------------------------------

    bool tearing_support        = false;  // Variable refresh / tearing (DXGI_PRESENT_ALLOW_TEARING)
    bool multi_gpu              = false;  // Linked / explicit multi-GPU

    //-------------------------------------------------------------------------
    // API version
    //-------------------------------------------------------------------------

    int api_version_major       = 0;  // e.g. OpenGL 4, D3D 11, Vulkan 1, Metal 3
    int api_version_minor       = 0;  // e.g. OpenGL .6, Vulkan .3

    //-------------------------------------------------------------------------
    // Memory information  (0 = unknown)
    //-------------------------------------------------------------------------

    uint64_t vram_dedicated_bytes   = 0; // Dedicated VRAM (GPU-local)
    uint64_t vram_shared_bytes      = 0; // Shared system memory usable by GPU

    //-------------------------------------------------------------------------
    // Convenience helpers
    //-------------------------------------------------------------------------

    bool supports_msaa(int samples) const {
        return samples >= 1 && samples <= max_samples && (samples & (samples - 1)) == 0;
    }

    bool supports_texture_size(int w, int h) const {
        return max_texture_size > 0 && w <= max_texture_size && h <= max_texture_size;
    }

    bool supports_texture_format_compressed() const {
        return texture_compression_bc || texture_compression_etc2 || texture_compression_astc;
    }
};

//=============================================================================
// Forward Declarations
//=============================================================================

class Graphics;  // Defined below

//=============================================================================
// External Window Configuration
//=============================================================================

// Configuration for attaching graphics to an existing external window
struct ExternalWindowConfig {
    // Native window handle (required)
    // Win32: HWND
    // X11: Window (unsigned long)
    // Wayland: wl_surface*
    // macOS: NSView*
    // iOS: UIView*
    // Android: ANativeWindow*
    void* native_handle = nullptr;

    // Native display handle (required for X11/Wayland, optional for others)
    // X11: Display*
    // Wayland: wl_display*
    // Others: nullptr
    void* native_display = nullptr;

    // Window dimensions (required - used for swapchain/viewport setup)
    int width = 0;
    int height = 0;

    // Graphics settings
    SwapMode swap_mode = SwapMode::Auto;  // Swap chain presentation mode
    bool vsync = true;      // Used when swap_mode is Auto
    int samples = 1;        // MSAA samples (1 = disabled)
    int red_bits = 8;
    int green_bits = 8;
    int blue_bits = 8;
    int alpha_bits = 8;
    int depth_bits = 24;
    int stencil_bits = 8;
    int back_buffers = 2;

    // Graphics backend selection
    Backend backend = Backend::Auto;

    // Shared context for resource sharing
    Graphics* shared_graphics = nullptr;
};

//=============================================================================
// Graphics Context Interface
//=============================================================================

class Graphics {
public:
    virtual ~Graphics() = default;

    // Create graphics context for an existing external window
    // Use this when you have your own window (e.g., from Qt, wxWidgets, SDL, GLFW, etc.)
    // The caller is responsible for:
    //   - Managing the window lifetime (don't destroy window while Graphics exists)
    //   - Calling resize() when the window size changes
    //   - Presenting/swapping buffers using native APIs or present()
    static Graphics* create(const ExternalWindowConfig& config, Result* out_result = nullptr);

    // Destroy this graphics context (call instead of delete)
    void destroy();

    // Backend info
    virtual Backend get_backend() const = 0;
    virtual const char* get_backend_name() const = 0;
    virtual const char* get_device_name() const = 0;

    // Resize swapchain (call when external window is resized)
    virtual bool resize(int width, int height) = 0;

    // Present/swap buffers (convenience method, can also use native APIs directly)
    virtual void present() = 0;

    // Make this context current (for OpenGL)
    virtual void make_current() = 0;

    // Native handles
    virtual void* native_device() const = 0;
    virtual void* native_context() const = 0;
    virtual void* native_swapchain() const = 0;

    // Query backend capabilities and hardware limits.
    // Fills *out_caps completely; any field that cannot be determined is left
    // at its default (zero / false).  Always safe to call after creation.
    virtual void get_capabilities(GraphicsCapabilities* out_caps) const = 0;

protected:
    Graphics() = default;
};

//=============================================================================
// Render Target / Framebuffer Types
//=============================================================================

// Render target description
struct RenderTargetDesc {
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    int samples = 1;                    // MSAA samples
    bool generate_mipmaps = false;
};

// Depth-stencil target description
struct DepthStencilDesc {
    int width = 0;
    int height = 0;
    TextureFormat format = TextureFormat::D24_UNORM_S8_UINT;
    int samples = 1;
};

//=============================================================================
// Viewport and Scissor
//=============================================================================

struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float min_depth = 0.0f;
    float max_depth = 1.0f;
};

struct ScissorRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

//=============================================================================
// Clear Values
//=============================================================================

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    ClearColor() = default;
    ClearColor(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    static ClearColor black() { return ClearColor(0.0f, 0.0f, 0.0f, 1.0f); }
    static ClearColor white() { return ClearColor(1.0f, 1.0f, 1.0f, 1.0f); }
    static ClearColor cornflower_blue() { return ClearColor(0.392f, 0.584f, 0.929f, 1.0f); }
};

struct ClearDepthStencil {
    float depth = 1.0f;
    uint8_t stencil = 0;
};

//=============================================================================
// Blend State
//=============================================================================

enum class BlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    InvSrcColor,
    SrcAlpha,
    InvSrcAlpha,
    DstColor,
    InvDstColor,
    DstAlpha,
    InvDstAlpha,
    SrcAlphaSat,
    BlendFactor,
    InvBlendFactor
};

const char* blend_factor_to_string(BlendFactor factor);
bool        parse_blend_factor(const char* s, BlendFactor* out);

enum class BlendOp : uint8_t {
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max
};

const char* blend_op_to_string(BlendOp op);
bool        parse_blend_op(const char* s, BlendOp* out);

struct BlendState {
    bool enabled = false;
    BlendFactor src_color = BlendFactor::One;
    BlendFactor dst_color = BlendFactor::Zero;
    BlendOp color_op = BlendOp::Add;
    BlendFactor src_alpha = BlendFactor::One;
    BlendFactor dst_alpha = BlendFactor::Zero;
    BlendOp alpha_op = BlendOp::Add;
    uint8_t write_mask = 0x0F;  // RGBA write mask

    static BlendState disabled() { return BlendState(); }
    static BlendState alpha_blend() {
        BlendState state;
        state.enabled = true;
        state.src_color = BlendFactor::SrcAlpha;
        state.dst_color = BlendFactor::InvSrcAlpha;
        state.src_alpha = BlendFactor::One;
        state.dst_alpha = BlendFactor::InvSrcAlpha;
        return state;
    }
    static BlendState additive() {
        BlendState state;
        state.enabled = true;
        state.src_color = BlendFactor::One;
        state.dst_color = BlendFactor::One;
        state.src_alpha = BlendFactor::One;
        state.dst_alpha = BlendFactor::One;
        return state;
    }
    static BlendState premultiplied_alpha() {
        BlendState state;
        state.enabled = true;
        state.src_color = BlendFactor::One;
        state.dst_color = BlendFactor::InvSrcAlpha;
        state.src_alpha = BlendFactor::One;
        state.dst_alpha = BlendFactor::InvSrcAlpha;
        return state;
    }
};

//=============================================================================
// Depth/Stencil State
//=============================================================================

enum class CompareFunc : uint8_t {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

const char* compare_func_to_string(CompareFunc func);
bool        parse_compare_func(const char* s, CompareFunc* out);

enum class StencilOp : uint8_t {
    Keep,
    Zero,
    Replace,
    IncrSat,
    DecrSat,
    Invert,
    IncrWrap,
    DecrWrap
};

const char* stencil_op_to_string(StencilOp op);
bool        parse_stencil_op(const char* s, StencilOp* out);

struct StencilOpDesc {
    StencilOp stencil_fail = StencilOp::Keep;
    StencilOp depth_fail = StencilOp::Keep;
    StencilOp pass = StencilOp::Keep;
    CompareFunc func = CompareFunc::Always;
};

struct DepthStencilState {
    bool depth_enable = true;
    bool depth_write = true;
    CompareFunc depth_func = CompareFunc::Less;
    bool stencil_enable = false;
    uint8_t stencil_read_mask = 0xFF;
    uint8_t stencil_write_mask = 0xFF;
    StencilOpDesc front_face;
    StencilOpDesc back_face;

    static DepthStencilState disabled() {
        DepthStencilState state;
        state.depth_enable = false;
        state.depth_write = false;
        return state;
    }
    static DepthStencilState depth_test() { return DepthStencilState(); }
    static DepthStencilState depth_read_only() {
        DepthStencilState state;
        state.depth_write = false;
        return state;
    }
};

//=============================================================================
// Rasterizer State
//=============================================================================

enum class FillMode : uint8_t {
    Solid,
    Wireframe
};

const char* fill_mode_to_string(FillMode mode);
bool        parse_fill_mode(const char* s, FillMode* out);

enum class CullMode : uint8_t {
    None,
    Front,
    Back
};

const char* cull_mode_to_string(CullMode mode);
bool        parse_cull_mode(const char* s, CullMode* out);

enum class FrontFace : uint8_t {
    CounterClockwise,
    Clockwise
};

const char* front_face_to_string(FrontFace face);
bool        parse_front_face(const char* s, FrontFace* out);

struct RasterizerState {
    FillMode fill_mode = FillMode::Solid;
    CullMode cull_mode = CullMode::Back;
    FrontFace front_face = FrontFace::CounterClockwise;
    int depth_bias = 0;
    float depth_bias_clamp = 0.0f;
    float slope_scaled_depth_bias = 0.0f;
    bool depth_clip_enable = true;
    bool scissor_enable = false;
    bool multisample_enable = false;
    bool antialiased_line_enable = false;

    static RasterizerState default_state() { return RasterizerState(); }
    static RasterizerState no_cull() {
        RasterizerState state;
        state.cull_mode = CullMode::None;
        return state;
    }
    static RasterizerState wireframe() {
        RasterizerState state;
        state.fill_mode = FillMode::Wireframe;
        state.cull_mode = CullMode::None;
        return state;
    }
};

//=============================================================================
// Sampler State
//=============================================================================

enum class FilterMode : uint8_t {
    Point,
    Linear,
    Anisotropic
};

const char* filter_mode_to_string(FilterMode mode);
bool        parse_filter_mode(const char* s, FilterMode* out);

enum class AddressMode : uint8_t {
    Wrap,
    Mirror,
    Clamp,
    Border,
    MirrorOnce
};

const char* address_mode_to_string(AddressMode mode);
bool        parse_address_mode(const char* s, AddressMode* out);

struct SamplerState {
    FilterMode min_filter = FilterMode::Linear;
    FilterMode mag_filter = FilterMode::Linear;
    FilterMode mip_filter = FilterMode::Linear;
    AddressMode address_u = AddressMode::Wrap;
    AddressMode address_v = AddressMode::Wrap;
    AddressMode address_w = AddressMode::Wrap;
    float mip_lod_bias = 0.0f;
    int max_anisotropy = 1;
    CompareFunc compare_func = CompareFunc::Never;
    float border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float min_lod = 0.0f;
    float max_lod = 1000.0f;

    static SamplerState point_clamp() {
        SamplerState state;
        state.min_filter = FilterMode::Point;
        state.mag_filter = FilterMode::Point;
        state.mip_filter = FilterMode::Point;
        state.address_u = AddressMode::Clamp;
        state.address_v = AddressMode::Clamp;
        state.address_w = AddressMode::Clamp;
        return state;
    }
    static SamplerState linear_clamp() {
        SamplerState state;
        state.address_u = AddressMode::Clamp;
        state.address_v = AddressMode::Clamp;
        state.address_w = AddressMode::Clamp;
        return state;
    }
    static SamplerState linear_wrap() {
        return SamplerState();
    }
    static SamplerState anisotropic(int max_aniso = 16) {
        SamplerState state;
        state.min_filter = FilterMode::Anisotropic;
        state.mag_filter = FilterMode::Anisotropic;
        state.max_anisotropy = max_aniso;
        return state;
    }
};

//=============================================================================
// Primitive Topology
//=============================================================================

enum class PrimitiveTopology : uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    LineListAdj,
    LineStripAdj,
    TriangleListAdj,
    TriangleStripAdj
};

const char* primitive_topology_to_string(PrimitiveTopology topology);
bool        parse_primitive_topology(const char* s, PrimitiveTopology* out);

//=============================================================================
// Vertex Format
//=============================================================================

enum class VertexFormat : uint8_t {
    Float1,
    Float2,
    Float3,
    Float4,
    Int1,
    Int2,
    Int3,
    Int4,
    UInt1,
    UInt2,
    UInt3,
    UInt4,
    Short2,
    Short4,
    Short2N,        // Normalized
    Short4N,
    UShort2,
    UShort4,
    UShort2N,
    UShort4N,
    Byte4,
    Byte4N,
    UByte4,
    UByte4N,
    Half2,
    Half4,
    RGB10A2
};

// Get byte size of a vertex format
int         vertex_format_size(VertexFormat format);
const char* vertex_format_to_string(VertexFormat format);
bool        parse_vertex_format(const char* s, VertexFormat* out);

} // namespace window

#endif // GRAPHICS_API_HPP
