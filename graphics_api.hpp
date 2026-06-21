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
int         texture_format_bytes_per_pixel(TextureFormat format);  // bytes per pixel (uncompressed) or per block (compressed)
int         texture_format_block_size(TextureFormat format);     // Compressed block WIDTH in pixels (1 for non-compressed)
bool        texture_format_is_compressed(TextureFormat format);

// Memory layout of a region, correct for both uncompressed and block-compressed
// (BC/ETC/EAC = 4x4, ASTC = its footprint) formats. Used by every backend's texture
// upload/readback so compressed data is laid out by blocks, not by 4-byte pixels.
void        texture_format_block_dims(TextureFormat format, int* out_w, int* out_h);  // 1x1 if uncompressed
size_t      texture_format_row_pitch(TextureFormat format, int width);   // bytes per row of (blocks of) `width` px
int         texture_format_row_count(TextureFormat format, int height);  // texel rows, or block-rows if compressed
size_t      texture_format_image_size(TextureFormat format, int width, int height);  // = row_pitch * row_count
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
    // Binding / shader-data alignment  (what offsets must be multiples of)
    //-------------------------------------------------------------------------

    int min_uniform_buffer_offset_alignment = 0; // bind_uniform_buffer offset granularity (bytes)
    int min_storage_buffer_offset_alignment = 0; // bind_storage_buffer offset granularity (bytes)
    int min_texel_buffer_offset_alignment   = 0; // texel-buffer binding granularity (bytes)
    int max_uniform_buffer_range            = 0; // Max bytes bindable as one uniform buffer
    int max_storage_buffer_range            = 0; // Max bytes bindable as one storage buffer
    int max_push_constant_size              = 0; // Bytes available to push_constants() / root constants
    int max_bound_descriptor_sets           = 0; // Max descriptor sets bound at once (root-signature width)

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

// Vulkan handles from a Backend::Vulkan Graphics, enough to build a Vulkan
// GraphicDevice on top. Opaque void* (cast to the Vulkan types in the backend) so
// graphics_api.hpp need not include vulkan.h. Only populated when get_vulkan_info()
// returns true (i.e. the context is Vulkan).
struct VulkanGraphicsInfo {
    void*    instance = nullptr;          // VkInstance
    void*    physical_device = nullptr;   // VkPhysicalDevice
    void*    device = nullptr;            // VkDevice
    void*    graphics_queue = nullptr;    // VkQueue
    uint32_t graphics_queue_family = 0;
    void*    surface = nullptr;           // VkSurfaceKHR (may be null)
    void*    swapchain = nullptr;         // VkSwapchainKHR (may be null)
    uint32_t swapchain_format = 0;        // VkFormat of the swapchain images (0 = VK_FORMAT_UNDEFINED)
};

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

    // Vulkan-only: fill the full handle set for a Vulkan render device. Returns
    // false on non-Vulkan backends (default). Overridden by the Vulkan Graphics.
    virtual bool get_vulkan_info(VulkanGraphicsInfo* /*out*/) const { return false; }

    // Actual swapchain configuration after creation -- the driver may clamp or substitute
    // the requested Config values (back_buffers / swap_mode). get_backbuffer_count() returns
    // the real number of swapchain images/buffers (so an app can size per-frame resource
    // rings to the true swapchain length); 0 means not applicable / unknown. get_swap_mode()
    // returns the present mode actually in effect (never SwapMode::Auto once resolved; on
    // Vulkan this reflects any fallback when the requested mode is unsupported). Default
    // implementations return 0 / Auto for backends that don't expose this.
    virtual int      get_backbuffer_count() const { return 0; }
    virtual SwapMode get_swap_mode()        const { return SwapMode::Auto; }

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

//=============================================================================
// Render Device & Command Abstraction
//
// Handle-based resource device + command recorder, modeled on REngine's
// RGDeviceWrapper (GraphicDevice + GraphicCommander) and adapted to this
// library's per-window `Graphics` context (which plays RGDeviceWrapper's
// GraphicCanvas role: swapchain + present).
//
// Design notes:
//   - These are SEPARATE interfaces from `Graphics` (added, not mixed in), so
//     existing backend Graphics implementations keep compiling unchanged. A
//     GraphicDevice is obtained for a live `Graphics` context via create_device().
//   - Resources are referenced by small typed, int-backed handles (-1 = invalid),
//     matching the reference's id scheme while staying type-safe.
//   - The interface is backend-NEUTRAL: no GL/D3D/Vulkan/Metal types leak here.
//     Each backend implements it in its own api_*.cpp/.mm (platform-isolated),
//     exactly like the existing context backends.
//=============================================================================

// ---- Typed resource handles (int-backed; id < 0 = invalid) ------------------
#define WINDOW_GFX_HANDLE(Name)                                               \
    struct Name { int32_t id = -1; bool valid() const noexcept { return id >= 0; } \
                  bool operator==(const Name& o) const noexcept { return id == o.id; } }
WINDOW_GFX_HANDLE(BufferHandle);
WINDOW_GFX_HANDLE(TextureHandle);
WINDOW_GFX_HANDLE(SamplerHandle);
WINDOW_GFX_HANDLE(ShaderHandle);
WINDOW_GFX_HANDLE(PipelineHandle);
WINDOW_GFX_HANDLE(RenderTargetHandle);
WINDOW_GFX_HANDLE(FenceHandle);      // CPU waits for GPU completion
WINDOW_GFX_HANDLE(SemaphoreHandle);  // GPU↔GPU / queue↔queue ordering (no-op on single-queue backends)
WINDOW_GFX_HANDLE(QueryHandle);      // timestamp / occlusion / pipeline-statistics
WINDOW_GFX_HANDLE(DescriptorSetLayoutHandle); // one descriptor set's binding layout (≈ VkDescriptorSetLayout)
WINDOW_GFX_HANDLE(PipelineLayoutHandle);      // the full binding layout (≈ D3D12 root signature / VkPipelineLayout)
WINDOW_GFX_HANDLE(DescriptorSetHandle);       // a written set of bound resources (≈ descriptor table / bind group)
WINDOW_GFX_HANDLE(PipelineCacheHandle);       // compiled-pipeline cache (PSO cache / VkPipelineCache)
WINDOW_GFX_HANDLE(TimelineSemaphoreHandle);   // monotonic 64-bit timeline (VkTimelineSemaphore / D3D12 fence-value)
WINDOW_GFX_HANDLE(AccelStructHandle);         // ray-tracing acceleration structure (BLAS/TLAS)
#undef WINDOW_GFX_HANDLE

// ---- Resource enums ---------------------------------------------------------
enum class BufferType : uint8_t { Vertex, Index, Uniform, Storage, Indirect };
// Submission queue. Backends with a single queue (e.g. OpenGL) treat all the same.
enum class QueueType : uint8_t { Graphics, Compute, Transfer };
// GPU query kinds. Caps advertise support via occlusion_query / timestamp_query.
enum class QueryType : uint8_t { Timestamp, Occlusion, PipelineStatistics };
enum class ResourceUsage : uint8_t { Immutable, Default, Dynamic, Staging };
enum class IndexFormat : uint8_t { UInt16, UInt32 };
// Programmable stages: classic graphics + tessellation + compute + the mesh-shader
// pipeline (Task = amplification/Mesh). Backends without a stage log + no-op.
enum class ShaderStage : uint8_t {
    Vertex, Fragment, Geometry, TessControl, TessEval, Compute, Task, Mesh,
    // Ray-tracing stages (exposed for RT-capable backends; OpenGL logs + no-ops).
    RayGen, Miss, ClosestHit, AnyHit, Intersection, Callable
};
enum class ShaderLanguage : uint8_t { Auto, GLSL, ESSL, SPIRV, HLSL, DXBC, DXIL, MSL, WGSL };
// Per-vertex vs per-instance stepping for a vertex buffer slot (instancing).
enum class VertexInputRate : uint8_t { PerVertex, PerInstance };
// Access for storage (UAV) image bindings in compute / fragment.
enum class StorageAccess : uint8_t { Read, Write, ReadWrite };
// Memory-barrier scopes between passes (compute↔graphics, indirect args, etc.).
enum GpuBarrier : uint32_t {
    GPU_BARRIER_VERTEX_BUFFER  = 1u << 0,
    GPU_BARRIER_INDEX_BUFFER   = 1u << 1,
    GPU_BARRIER_UNIFORM_BUFFER = 1u << 2,
    GPU_BARRIER_STORAGE_BUFFER = 1u << 3,
    GPU_BARRIER_TEXTURE        = 1u << 4,
    GPU_BARRIER_STORAGE_IMAGE  = 1u << 5,
    GPU_BARRIER_INDIRECT_ARGS  = 1u << 6,
    GPU_BARRIER_RENDER_TARGET  = 1u << 7,
    GPU_BARRIER_ALL            = 0xFFFFFFFFu
};

// Texture usage flags (bitwise OR).
enum TextureUsage : uint32_t {
    TEXTURE_USAGE_SAMPLED       = 1u << 0,
    TEXTURE_USAGE_RENDER_TARGET = 1u << 1,
    TEXTURE_USAGE_DEPTH_STENCIL = 1u << 2,
    TEXTURE_USAGE_STORAGE       = 1u << 3,
    TEXTURE_USAGE_COPY_SRC      = 1u << 4,
    TEXTURE_USAGE_COPY_DST      = 1u << 5
};

// ---- Resource descriptors ---------------------------------------------------
struct BufferDesc {
    uint32_t      size = 0;                       // bytes
    BufferType    type = BufferType::Vertex;
    ResourceUsage usage = ResourceUsage::Default;
    uint32_t      stride = 0;                      // structured/vertex element stride
    bool          sparse = false;                  // partially-resident (tiled) buffer
    const void*   initial_data = nullptr;          // optional upload at creation
    const char*   debug_name = nullptr;
};

struct TextureDesc {
    int           width = 0;
    int           height = 0;
    int           depth = 1;                       // 3D textures
    int           array_layers = 1;
    int           mip_levels = 1;
    int           samples = 1;                     // MSAA
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    uint32_t      usage = TEXTURE_USAGE_SAMPLED;   // TextureUsage bits OR'd
    bool          cube = false;
    bool          array_texture = false;           // force an array view even at array_layers==1
                                                   // (e.g. a sampler2DArray glyph atlas)
    bool          sparse = false;                  // partially-resident (tiled) texture; commit tiles
                                                   // with update_texture_residency()
    const void*   initial_data = nullptr;          // mip 0 / layer 0, tightly packed
    const char*   debug_name = nullptr;
};

// Sub-region for texture updates (RGDeviceWrapper updateTexture equivalent).
struct TextureRegion {
    int x = 0, y = 0, z = 0;
    int width = 0, height = 0, depth = 1;
    int mip = 0;
    int layer = 0;
};

// A reinterpreting view onto an existing texture (different format and/or a mip /
// layer sub-range). format = Unknown keeps the source format. The source must use
// immutable storage (it does). Free the view with destroy_texture().
struct TextureViewDesc {
    TextureHandle texture;
    TextureFormat format = TextureFormat::Unknown;  // Unknown = same as source
    int  base_mip = 0;
    int  mip_count = 0;        // 0 = all remaining levels
    int  base_layer = 0;
    int  layer_count = 0;      // 0 = all remaining layers
    bool cube = false;         // view as a cubemap
};

// Wrap an EXISTING native GPU texture (created by another API user — e.g. a
// GStreamer decoder/uploader sharing this device) as an RHI TextureHandle, with
// no copy. Set the field matching the device's backend; the others stay null/0.
// The wrapped handle is bindable as a sampled texture like any other; free it with
// destroy_texture(). By default the RHI does NOT own the native object (the
// producer keeps it alive) — set take_ownership to have destroy_texture() release
// it. The caller must keep the native resource alive while the handle is in use.
struct NativeTextureDesc {
    int           width = 0;
    int           height = 0;
    TextureFormat format = TextureFormat::RGBA8_UNORM;  // how the RHI samples it (SRV/view format)
    uint32_t      usage = TEXTURE_USAGE_SAMPLED;        // TextureUsage bits (usually just SAMPLED)
    bool          take_ownership = false;               // destroy_texture() frees the native object if true

    // --- Backend-specific native handle (set exactly the one for this backend) ---
    void*       d3d_resource = nullptr;   // D3D11: ID3D11Texture2D*   | D3D12: ID3D12Resource*
    uint64_t    gl_texture   = 0;         // OpenGL: GLuint texture name (0 = none)
    uint32_t    gl_target    = 0;         // OpenGL: GL_TEXTURE_2D etc. (0 = GL_TEXTURE_2D)
    void*       vk_image     = nullptr;   // Vulkan: VkImage
    uint32_t    vk_format    = 0;         // Vulkan: VkFormat the image was created with (0 = derive from `format`)
    uint32_t    vk_layout    = 0;         // Vulkan: current VkImageLayout for sampling (0 = SHADER_READ_ONLY_OPTIMAL)
    void*       metal_texture = nullptr;  // Metal: id<MTLTexture> (bridged pointer)
    const char* debug_name = nullptr;
};

struct ShaderDesc {
    ShaderStage    stage = ShaderStage::Vertex;
    ShaderLanguage language = ShaderLanguage::Auto; // Auto = pick the backend's native
    const void*    code = nullptr;                  // source text or compiled bytecode
    size_t         code_size = 0;                   // 0 → treat `code` as a NUL-terminated string
    const char*    entry_point = "main";
    const char*    debug_name = nullptr;
};

// One vertex attribute fed to a pipeline's input layout.
struct VertexAttribute {
    uint32_t     location = 0;                      // shader input slot / semantic index
    VertexFormat format = VertexFormat::Float3;
    uint32_t     offset = 0;                        // byte offset within its buffer slot
    uint32_t     buffer_slot = 0;                   // which bound vertex buffer it reads from
};

struct VertexLayout {
    static const int MAX_ATTRIBUTES = 16;
    static const int MAX_BUFFER_SLOTS = 8;
    VertexAttribute attributes[MAX_ATTRIBUTES] = {};
    int             attribute_count = 0;
    uint32_t        strides[MAX_BUFFER_SLOTS] = {};       // byte stride per vertex buffer slot
    VertexInputRate input_rates[MAX_BUFFER_SLOTS] = {};   // per-vertex (default) or per-instance, per slot
    int             buffer_count = 0;
};

// Pipeline state object (immutable once created), mirroring modern explicit APIs.
// Pipeline kind is inferred from which shaders are valid:
//   compute_shader            → compute pipeline
//   mesh_shader (+ task?)      → mesh-shader pipeline (no vertex input)
//   else vertex (+ tess/geom)  → classic graphics pipeline
struct PipelineDesc {
    ShaderHandle      vertex_shader;
    ShaderHandle      fragment_shader;
    ShaderHandle      geometry_shader;              // optional
    ShaderHandle      tess_control_shader;          // optional (tessellation)
    ShaderHandle      tess_eval_shader;             // optional (tessellation)
    ShaderHandle      compute_shader;               // valid → a compute pipeline
    ShaderHandle      task_shader;                  // optional amplification (mesh pipeline)
    ShaderHandle      mesh_shader;                  // valid → a mesh-shader pipeline
    VertexLayout      vertex_layout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    uint32_t          patch_control_points = 0;     // >0 → tessellation patches (overrides topology)
    BlendState        blend;
    DepthStencilState depth_stencil;
    RasterizerState   rasterizer;
    PipelineLayoutHandle layout;                    // optional root signature; invalid → reflected/auto bindings
    PipelineCacheHandle  cache;                      // optional PSO cache to reuse/populate compiled state
    TextureFormat     color_formats[8] = { TextureFormat::RGBA8_UNORM };
    int               color_format_count = 1;
    TextureFormat     depth_format = TextureFormat::D24_UNORM_S8_UINT;
    int               samples = 1;
    bool              alpha_to_coverage = false;     // MSAA alpha-to-coverage
    const char*       debug_name = nullptr;
};

// ---- Resource binding model (descriptor sets / root signature) --------------
// Modern explicit binding: a PipelineLayout (≈ D3D12 root signature) is built from
// per-set DescriptorSetLayouts + push-constant ranges; resources are written into
// DescriptorSets (≈ descriptor tables / bind groups) and bound as a unit. Backends
// without native descriptor sets (OpenGL) emulate a set as a list of slot binds.

// Shader-stage visibility bits (bitmask form of ShaderStage).
enum ShaderStageBits : uint32_t {
    STAGE_VERTEX       = 1u << 0,
    STAGE_FRAGMENT     = 1u << 1,
    STAGE_GEOMETRY     = 1u << 2,
    STAGE_TESS_CONTROL = 1u << 3,
    STAGE_TESS_EVAL    = 1u << 4,
    STAGE_COMPUTE      = 1u << 5,
    STAGE_TASK         = 1u << 6,
    STAGE_MESH         = 1u << 7,
    STAGE_ALL          = 0xFFu,
};

enum class BindingType : uint8_t {
    UniformBuffer,        // constant buffer (UBO / cbuffer)
    StorageBuffer,        // read/write structured buffer (SSBO / UAV buffer)
    SampledTexture,       // sampled image (SRV)
    StorageTexture,       // read/write image (image2D / UAV texture)
    Sampler,              // standalone sampler
    CombinedImageSampler, // texture+sampler in one binding (GLSL sampler2D)
};

// One binding within a descriptor set layout.
struct DescriptorBinding {
    uint32_t    binding = 0;                 // binding index within the set
    BindingType type = BindingType::UniformBuffer;
    uint32_t    count = 1;                   // array size (>1 for descriptor arrays)
    uint32_t    stages = STAGE_ALL;          // ShaderStageBits visibility
};

struct DescriptorSetLayoutDesc {
    static const int MAX_BINDINGS = 16;
    DescriptorBinding bindings[MAX_BINDINGS] = {};
    int               binding_count = 0;
};

// A range of push constants (root constants) visible to some stages.
struct PushConstantRange {
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t stages = STAGE_ALL;
};

// The full binding layout for a pipeline — D3D12 calls this the root signature.
struct PipelineLayoutDesc {
    static const int MAX_SETS = 4;
    static const int MAX_PUSH = 4;
    DescriptorSetLayoutHandle set_layouts[MAX_SETS] = {};
    int                       set_layout_count = 0;
    PushConstantRange         push_constants[MAX_PUSH] = {};
    int                       push_constant_count = 0;
};

// One resource written into a descriptor set slot (tagged by the binding's type).
struct DescriptorWrite {
    uint32_t      binding = 0;
    BindingType   type = BindingType::UniformBuffer;
    BufferHandle  buffer;  uint32_t buffer_offset = 0; uint32_t buffer_size = 0;  // (Storage|Uniform)Buffer
    TextureHandle texture; int      texture_mip = 0;                              // (Storage|Sampled)Texture
    SamplerHandle sampler;                                                        // Sampler / CombinedImageSampler
    StorageAccess storage_access = StorageAccess::ReadWrite;                      // StorageTexture
};

struct DescriptorSetDesc {
    static const int MAX_WRITES = 16;
    DescriptorSetLayoutHandle layout;
    DescriptorWrite           writes[MAX_WRITES] = {};
    int                       write_count = 0;
};

// Object kind for set_debug_name().
enum class ObjectType : uint8_t { Buffer, Texture, Sampler, Shader, Pipeline, RenderTarget };

// ---- Ray tracing (acceleration structures) ---------------------------------
// Exposed for RT-capable backends (Vulkan KHR ray tracing / DXR). OpenGL has no
// ray tracing and logs + returns invalid; the interface keeps cross-backend parity.
enum class AccelStructType : uint8_t { BottomLevel, TopLevel };   // BLAS = geometry, TLAS = instances
enum AccelStructBuildFlags : uint32_t {
    ACCEL_ALLOW_UPDATE      = 1u << 0,
    ACCEL_ALLOW_COMPACTION  = 1u << 1,
    ACCEL_PREFER_FAST_TRACE = 1u << 2,
    ACCEL_PREFER_FAST_BUILD = 1u << 3,
    ACCEL_LOW_MEMORY        = 1u << 4,
};
struct AccelStructDesc {
    AccelStructType type = AccelStructType::BottomLevel;
    uint32_t        flags = ACCEL_PREFER_FAST_TRACE;     // AccelStructBuildFlags
    // Bottom-level (triangle geometry):
    BufferHandle    vertex_buffer;  uint32_t vertex_count = 0;  uint32_t vertex_stride = 0;
    VertexFormat    vertex_format = VertexFormat::Float3;
    BufferHandle    index_buffer;   uint32_t index_count = 0;   IndexFormat index_format = IndexFormat::UInt32;
    BufferHandle    transform_buffer;                            // optional 3x4 row-major transforms
    // Top-level (instances):
    BufferHandle    instance_buffer; uint32_t instance_count = 0;
    bool            update = false;                              // refit an existing structure in place
};

//-----------------------------------------------------------------------------
// GraphicDevice — resource creation/update/destruction (handle-based).
// Obtained for a live Graphics context via create_device(). Owns no commands;
// pair it with a GraphicCommander to render. (≈ RGDeviceWrapper GraphicDevice.)
//-----------------------------------------------------------------------------
class GraphicDevice {
public:
    virtual ~GraphicDevice() = default;

    virtual Backend     get_backend() const = 0;
    virtual void        get_capabilities(GraphicsCapabilities* out_caps) const = 0;

    // Buffers (vertex / index / uniform / storage / indirect).
    virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;
    virtual void         update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset = 0) = 0;
    virtual void         destroy_buffer(BufferHandle h) = 0;

    // Textures + samplers.
    virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    virtual void          update_texture(TextureHandle h, const TextureRegion& region, const void* data) = 0;
    virtual void          generate_mipmaps(TextureHandle h) = 0;
    virtual void          destroy_texture(TextureHandle h) = 0;
    virtual SamplerHandle create_sampler(const SamplerState& desc) = 0;
    virtual void          destroy_sampler(SamplerHandle h) = 0;

    // Shaders + pipeline state objects.
    virtual ShaderHandle   create_shader(const ShaderDesc& desc) = 0;
    virtual void           destroy_shader(ShaderHandle h) = 0;
    virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
    virtual void           destroy_pipeline(PipelineHandle h) = 0;

    // Offscreen render targets (color or depth-stencil per RenderTargetDesc/DepthStencilDesc).
    virtual RenderTargetHandle create_render_target(const RenderTargetDesc& desc) = 0;
    virtual RenderTargetHandle create_depth_target(const DepthStencilDesc& desc) = 0;
    virtual TextureHandle      render_target_texture(RenderTargetHandle h) = 0;  // sample the result
    virtual void               destroy_render_target(RenderTargetHandle h) = 0;

    // ---- Synchronization ----------------------------------------------------
    // Fence: CPU waits for submitted GPU work to finish. submit_commander() with a
    // fence signals it on completion; create_fence(true) starts already-signaled.
    virtual FenceHandle create_fence(bool signaled = false) = 0;
    virtual void        destroy_fence(FenceHandle h) = 0;
    virtual bool        wait_fence(FenceHandle h, uint64_t timeout_ns = ~0ull) = 0;  // true if signaled in time
    virtual bool        get_fence_status(FenceHandle h) = 0;                          // signaled now?
    virtual void        reset_fence(FenceHandle h) = 0;
    // Semaphore: GPU-side ordering between queue submissions / swapchain. No-op on
    // single-queue backends (OpenGL), present for API parity with Vulkan/D3D12.
    virtual SemaphoreHandle create_semaphore() = 0;
    virtual void            destroy_semaphore(SemaphoreHandle h) = 0;
    // Block until all submitted GPU work on the device has completed.
    virtual void        wait_idle() = 0;

    // ---- Queries (timestamp / occlusion) ------------------------------------
    virtual QueryHandle create_query(QueryType type) = 0;
    virtual void        destroy_query(QueryHandle h) = 0;
    // Read a query's result. If wait, blocks until available; else returns false if
    // not yet ready. Timestamps are in nanoseconds; occlusion is a sample count.
    virtual bool        get_query_result(QueryHandle h, uint64_t* out_value, bool wait = true) = 0;

    // ---- CPU<->GPU data access ----------------------------------------------
    // Map a buffer's storage for CPU access (size 0 = to end). unmap when done.
    virtual void* map_buffer(BufferHandle h, uint32_t offset = 0, uint32_t size = 0) = 0;
    virtual void  unmap_buffer(BufferHandle h) = 0;
    // Read back GPU data to CPU memory (blocking).
    virtual void  read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset = 0) = 0;
    virtual void  read_texture(TextureHandle h, const TextureRegion& region, void* dst) = 0;

    // ---- Texture views ------------------------------------------------------
    // A reinterpreting view (format/mip/layer sub-range) onto a texture. Returns a
    // TextureHandle usable anywhere a texture is; free it with destroy_texture().
    virtual TextureHandle create_texture_view(const TextureViewDesc& desc) = 0;

    // Wrap an existing native GPU texture as an RHI TextureHandle without copying
    // (zero-copy interop — see NativeTextureDesc). Returns invalid on backends/handles
    // that can't be wrapped. Free with destroy_texture().
    virtual TextureHandle import_texture(const NativeTextureDesc& desc) = 0;

    // ---- Binding model: descriptor sets / pipeline layout (root signature) ---
    virtual DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& desc) = 0;
    virtual void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) = 0;
    virtual PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& desc) = 0;   // ≈ root signature
    virtual void destroy_pipeline_layout(PipelineLayoutHandle h) = 0;
    // Allocate + write a descriptor set (bind group). update_descriptor_set rewrites it.
    virtual DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& desc) = 0;
    virtual void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& desc) = 0;
    virtual void destroy_descriptor_set(DescriptorSetHandle h) = 0;

    // ---- Debug labels -------------------------------------------------------
    virtual void set_debug_name(ObjectType type, uint32_t handle_id, const char* name) = 0;

    // ---- Pipeline (PSO) cache ----------------------------------------------
    // Reuse compiled pipeline state across runs. Seed from previously saved data,
    // pass the handle in PipelineDesc.cache, then persist get_pipeline_cache_data().
    virtual PipelineCacheHandle create_pipeline_cache(const void* initial_data = nullptr, size_t size = 0) = 0;
    virtual size_t get_pipeline_cache_data(PipelineCacheHandle h, void* dst, size_t capacity) = 0; // returns bytes (dst null = query size)
    virtual void   destroy_pipeline_cache(PipelineCacheHandle h) = 0;

    // ---- Timeline semaphores (monotonic 64-bit value) -----------------------
    virtual TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial_value = 0) = 0;
    virtual void     destroy_timeline_semaphore(TimelineSemaphoreHandle h) = 0;
    virtual void     signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value) = 0;   // host signal
    virtual bool     wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value, uint64_t timeout_ns = ~0ull) = 0;
    virtual uint64_t get_timeline_value(TimelineSemaphoreHandle h) = 0;

    // ---- Sparse / tiled residency -------------------------------------------
    // Commit or release backing memory for a region of a sparse texture.
    virtual void update_texture_residency(TextureHandle h, const TextureRegion& region, bool resident) = 0;

    // ---- Ray tracing --------------------------------------------------------
    virtual AccelStructHandle create_acceleration_structure(const AccelStructDesc& desc) = 0;
    virtual void              destroy_acceleration_structure(AccelStructHandle h) = 0;

protected:
    GraphicDevice() = default;
};

//-----------------------------------------------------------------------------
// GraphicCommander — records a frame's draw/compute work, then submit()ted to
// the owning Graphics context. (≈ RGDeviceWrapper GraphicCommander.)
//-----------------------------------------------------------------------------
class GraphicCommander {
public:
    virtual ~GraphicCommander() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    // Render targets. The "backbuffer" variant targets the owning context's swapchain.
    virtual void set_render_target_backbuffer() = 0;
    virtual void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) = 0;
    virtual void set_viewport(const Viewport& vp) = 0;
    virtual void set_scissor(const ScissorRect& rect) = 0;
    virtual void clear_color(const ClearColor& color) = 0;                              // clears the bound color target(s)
    virtual void clear_depth_stencil(const ClearDepthStencil& ds) = 0;

    // State + resource binding.
    virtual void set_pipeline(PipelineHandle h) = 0;
    virtual void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset = 0) = 0;
    virtual void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset = 0) = 0;
    virtual void bind_texture(uint32_t slot, TextureHandle h) = 0;
    virtual void bind_sampler(uint32_t slot, SamplerHandle h) = 0;
    virtual void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset = 0, uint32_t size = 0) = 0;
    virtual void push_constants(uint32_t offset, const void* data, uint32_t size) = 0;  // small per-draw constants
    // Storage (UAV) resources for compute / read-write shaders.
    virtual void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset = 0, uint32_t size = 0) = 0;
    virtual void bind_storage_texture(uint32_t slot, TextureHandle h, int mip = 0,
                                      StorageAccess access = StorageAccess::ReadWrite) = 0;

    // Direct draws / dispatch (instance_count + first_instance drive instancing;
    // per-instance vertex stepping comes from VertexLayout::input_rates).
    virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0,
                      uint32_t instance_count = 1, uint32_t first_instance = 0) = 0;
    virtual void draw_indexed(uint32_t index_count, uint32_t first_index = 0, int32_t base_vertex = 0,
                              uint32_t instance_count = 1, uint32_t first_instance = 0) = 0;
    virtual void dispatch(uint32_t group_x, uint32_t group_y = 1, uint32_t group_z = 1) = 0;

    // GPU-driven (indirect) draws / dispatch — args read from a BufferType::Indirect
    // buffer. `draw_count` > 1 = multi-draw indirect; `stride` 0 = tightly packed.
    virtual void draw_indirect(BufferHandle args, uint32_t offset = 0,
                               uint32_t draw_count = 1, uint32_t stride = 0) = 0;
    virtual void draw_indexed_indirect(BufferHandle args, uint32_t offset = 0,
                                       uint32_t draw_count = 1, uint32_t stride = 0) = 0;
    virtual void dispatch_indirect(BufferHandle args, uint32_t offset = 0) = 0;

    // Mesh-shader pipeline dispatch (task/mesh). Backends without mesh shaders
    // (e.g. core OpenGL) log once and no-op.
    virtual void draw_mesh_tasks(uint32_t group_x, uint32_t group_y = 1, uint32_t group_z = 1) = 0;
    virtual void draw_mesh_tasks_indirect(BufferHandle args, uint32_t offset = 0,
                                          uint32_t draw_count = 1, uint32_t stride = 0) = 0;

    // Memory/execution barrier between passes (GpuBarrier bits OR'd).
    virtual void memory_barrier(uint32_t barrier_bits) = 0;

    // ---- Copies / blit / resolve (GPU→GPU) ----------------------------------
    virtual void copy_buffer(BufferHandle dst, uint32_t dst_offset,
                             BufferHandle src, uint32_t src_offset, uint32_t size) = 0;
    virtual void copy_texture(TextureHandle dst, const TextureRegion& dst_region,
                              TextureHandle src, const TextureRegion& src_region) = 0;
    // Scaled blit between render targets; an invalid handle means the backbuffer.
    virtual void blit_render_target(RenderTargetHandle dst, RenderTargetHandle src,
                                    int src_x0, int src_y0, int src_x1, int src_y1,
                                    int dst_x0, int dst_y0, int dst_x1, int dst_y1,
                                    bool linear = true) = 0;
    // Resolve a multisampled color target into a single-sample one.
    virtual void resolve_render_target(RenderTargetHandle dst, RenderTargetHandle src) = 0;

    // ---- Queries / debug markers (recorded into the command stream) ----------
    virtual void write_timestamp(QueryHandle h) = 0;   // for QueryType::Timestamp
    virtual void begin_query(QueryHandle h) = 0;        // for Occlusion / PipelineStatistics
    virtual void end_query(QueryHandle h) = 0;
    virtual void push_debug_group(const char* name) = 0;   // RenderDoc/PIX capture grouping
    virtual void pop_debug_group() = 0;
    virtual void insert_debug_marker(const char* name) = 0;

    // ---- Descriptor sets ----------------------------------------------------
    // Bind a written descriptor set at a set index of the active pipeline's layout.
    // dynamic_offsets feed the set's dynamic uniform/storage buffer bindings in order.
    virtual void bind_descriptor_set(uint32_t set_index, DescriptorSetHandle set,
                                     const uint32_t* dynamic_offsets = nullptr,
                                     int dynamic_offset_count = 0) = 0;

    // ---- Dynamic pipeline state (override without rebuilding the pipeline) ----
    virtual void set_stencil_reference(uint32_t ref) = 0;
    virtual void set_blend_constants(const float rgba[4]) = 0;
    virtual void set_depth_bias(float constant, float clamp, float slope) = 0;
    virtual void set_line_width(float width) = 0;

    // ---- Multiple viewports / scissors (viewport-index in geometry/mesh) ------
    virtual void set_viewports(const Viewport* viewports, int count) = 0;
    virtual void set_scissors(const ScissorRect* rects, int count) = 0;

    // ---- GPU-driven draw with a count read from a buffer (≈ ExecuteIndirect) --
    virtual void draw_indirect_count(BufferHandle args, uint32_t args_offset,
                                     BufferHandle count_buffer, uint32_t count_offset,
                                     uint32_t max_draws, uint32_t stride) = 0;
    virtual void draw_indexed_indirect_count(BufferHandle args, uint32_t args_offset,
                                             BufferHandle count_buffer, uint32_t count_offset,
                                             uint32_t max_draws, uint32_t stride) = 0;

    // ---- Ray tracing --------------------------------------------------------
    // Build/refit an acceleration structure, then trace from the bound RT pipeline.
    virtual void build_acceleration_structure(AccelStructHandle dst, const AccelStructDesc& desc) = 0;
    virtual void trace_rays(uint32_t width, uint32_t height, uint32_t depth = 1) = 0;

protected:
    GraphicCommander() = default;
};

// Create a render device for a live Graphics context (resources are owned by the
// context's backend device). Returns nullptr + sets *out_result when the context's
// backend has no GraphicDevice implementation yet. The caller owns the device and
// frees it with destroy_device(). A matching commander comes from create_commander().
GraphicDevice*    create_device(Graphics* context, Result* out_result = nullptr);
void              destroy_device(GraphicDevice* device);
GraphicCommander* create_commander(Graphics* context, GraphicDevice* device, Result* out_result = nullptr);
// Commander bound to a specific submission queue (single-queue backends ignore it).
GraphicCommander* create_commander(Graphics* context, GraphicDevice* device, QueueType queue,
                                   Result* out_result = nullptr);
void              destroy_commander(GraphicCommander* commander);
// Submit a finished commander's recorded work to its context. Present via Graphics::present().
void              submit_commander(Graphics* context, GraphicCommander* commander);
// Submit and signal `fence` (from create_fence) once the work completes; pass an
// invalid handle to skip signaling. Wait on it via GraphicDevice::wait_fence().
void              submit_commander(Graphics* context, GraphicCommander* commander, FenceHandle signal_fence);
// Submit and signal a timeline semaphore to `value` on completion (≈ D3D12 fence-value).
void              submit_commander(Graphics* context, GraphicCommander* commander,
                                   TimelineSemaphoreHandle signal_timeline, uint64_t value);

} // namespace window

#endif // GRAPHICS_API_HPP
