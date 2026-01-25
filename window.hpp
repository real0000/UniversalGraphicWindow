/*
 * window.hpp - Cross-platform window and graphics library
 *
 * Supported platforms:
 *   - Windows (Win32)
 *   - UWP (Windows Runtime)
 *   - X11 (Linux)
 *   - Wayland (Linux)
 *   - iOS (UIKit)
 *   - Android (NativeActivity)
 *   - macOS (Cocoa)
 *
 * Graphics backends (auto-selected per platform):
 *   - OpenGL/OpenGL ES
 *   - Vulkan
 *   - Direct3D 11/12
 *   - Metal
 */

#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <cstdint>

//=============================================================================
// Platform Detection (Internal)
// These may already be defined by the build system (CMake), so check first
//=============================================================================

#if defined(_WIN32)
    #if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
        #ifndef WINDOW_PLATFORM_UWP
        #define WINDOW_PLATFORM_UWP
        #endif
    #else
        #ifndef WINDOW_PLATFORM_WIN32
        #define WINDOW_PLATFORM_WIN32
        #endif
    #endif
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #ifndef WINDOW_PLATFORM_IOS
        #define WINDOW_PLATFORM_IOS
        #endif
    #else
        #ifndef WINDOW_PLATFORM_MACOS
        #define WINDOW_PLATFORM_MACOS
        #endif
    #endif
#elif defined(__ANDROID__)
    #ifndef WINDOW_PLATFORM_ANDROID
    #define WINDOW_PLATFORM_ANDROID
    #endif
#elif defined(__linux__)
    #if defined(WINDOW_USE_WAYLAND)
        #ifndef WINDOW_PLATFORM_WAYLAND
        #define WINDOW_PLATFORM_WAYLAND
        #endif
    #else
        #ifndef WINDOW_PLATFORM_X11
        #define WINDOW_PLATFORM_X11
        #endif
    #endif
#else
    #error "Unsupported platform"
#endif

//=============================================================================
// Public API
//=============================================================================

namespace window {

//-----------------------------------------------------------------------------
// Enumerations
//-----------------------------------------------------------------------------

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

enum class Backend {
    Auto = 0,   // Auto-select best backend for platform
    OpenGL,
    Vulkan,
    D3D11,
    D3D12,
    Metal
};

// Window style flags (can be combined with |)
enum class WindowStyle : uint32_t {
    None            = 0,
    TitleBar        = 1 << 0,   // Has title bar
    Border          = 1 << 1,   // Has border/frame
    CloseButton     = 1 << 2,   // Has close button
    MinimizeButton  = 1 << 3,   // Has minimize button
    MaximizeButton  = 1 << 4,   // Has maximize button
    Resizable       = 1 << 5,   // Can be resized by dragging edges
    Fullscreen      = 1 << 6,   // Fullscreen mode
    AlwaysOnTop     = 1 << 7,   // Always on top of other windows
    ToolWindow      = 1 << 8,   // Tool window (smaller title bar, not in taskbar)

    // Convenience combinations
    Borderless      = None,
    Default         = TitleBar | Border | CloseButton | MinimizeButton | MaximizeButton | Resizable,
    FixedSize       = TitleBar | Border | CloseButton | MinimizeButton,
    FullscreenBorderless = Fullscreen | Borderless
};

// Bitwise operators for WindowStyle
inline WindowStyle operator|(WindowStyle a, WindowStyle b) {
    return static_cast<WindowStyle>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline WindowStyle operator&(WindowStyle a, WindowStyle b) {
    return static_cast<WindowStyle>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline WindowStyle operator~(WindowStyle a) {
    return static_cast<WindowStyle>(~static_cast<uint32_t>(a));
}
inline WindowStyle& operator|=(WindowStyle& a, WindowStyle b) {
    return a = a | b;
}
inline WindowStyle& operator&=(WindowStyle& a, WindowStyle b) {
    return a = a & b;
}
inline bool has_style(WindowStyle styles, WindowStyle flag) {
    return (static_cast<uint32_t>(styles) & static_cast<uint32_t>(flag)) != 0;
}

// WindowStyle string conversion
bool parse_window_style(const char* value, WindowStyle* out);
void window_style_to_string(WindowStyle style, char* buffer, size_t buffer_size);
const char* window_style_flag_to_string(WindowStyle flag);

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

static const int MAX_DEVICE_NAME_LENGTH = 256;
static const int MAX_DEVICES = 16;
static const int MAX_MONITORS = 16;
static const int MAX_DISPLAY_MODES = 256;

//-----------------------------------------------------------------------------
// Forward Declarations
//-----------------------------------------------------------------------------

class Graphics;

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------

struct Config {
    const char* title = "Window";
    int width = 800;
    int height = 600;
    int x = -1;             // -1 = centered/default
    int y = -1;             // -1 = centered/default
    bool resizable = true;  // Deprecated: use style instead. Kept for backward compatibility.
    bool visible = true;
    bool vsync = true;
    int samples = 1;        // MSAA samples (1 = disabled)
    // Window style flags (default: titled, bordered, with all buttons, resizable)
    WindowStyle style = WindowStyle::Default;
    // Back buffer settings
    int red_bits = 8;
    int green_bits = 8;
    int blue_bits = 8;
    int alpha_bits = 8;
    int depth_bits = 24;
    int stencil_bits = 8;
    int back_buffers = 2;   // Swap chain buffer count (2 = double buffering, 3 = triple buffering)
    // Graphics backend selection
    // Auto = platform default, or specify OpenGL/Vulkan/D3D11/D3D12/Metal
    Backend backend = Backend::Auto;
    // Shared context - for resource sharing between windows
    // OpenGL: shares textures, buffers, shaders
    // D3D11/D3D12/Vulkan/Metal: shares device (creates new swapchain)
    Graphics* shared_graphics = nullptr;
};

//-----------------------------------------------------------------------------
// Graphics Device and Display Mode Enumeration
//-----------------------------------------------------------------------------

// Information about a graphics device (GPU)
struct GraphicsDeviceInfo {
    char name[MAX_DEVICE_NAME_LENGTH] = {};     // Device name (e.g., "NVIDIA GeForce RTX 3080")
    char vendor[MAX_DEVICE_NAME_LENGTH] = {};   // Vendor name (e.g., "NVIDIA")
    uint32_t device_id = 0;                     // Unique device identifier
    uint32_t vendor_id = 0;                     // Vendor identifier
    uint64_t dedicated_video_memory = 0;        // Dedicated VRAM in bytes
    uint64_t dedicated_system_memory = 0;       // Dedicated system memory in bytes
    uint64_t shared_system_memory = 0;          // Shared system memory in bytes
    Backend backend = Backend::Auto;            // Which backend this device is for
    int device_index = 0;                       // Index for selection
    bool is_default = false;                    // True if this is the system default device
};

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
    char name[MAX_DEVICE_NAME_LENGTH] = {};     // Monitor name
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

// Enumeration results
struct DeviceEnumeration {
    GraphicsDeviceInfo devices[MAX_DEVICES] = {};
    int device_count = 0;
};

struct MonitorEnumeration {
    MonitorInfo monitors[MAX_MONITORS] = {};
    int monitor_count = 0;
};

//-----------------------------------------------------------------------------
// Graphics Configuration (saveable/loadable)
//-----------------------------------------------------------------------------

struct GraphicsConfig {
    // Window settings
    char title[MAX_DEVICE_NAME_LENGTH] = "Window";
    int window_x = -1;              // -1 = centered
    int window_y = -1;              // -1 = centered
    int window_width = 800;
    int window_height = 600;
    WindowStyle style = WindowStyle::Default;

    // Display settings
    int monitor_index = 0;          // Which monitor to use (-1 = primary)
    bool fullscreen = false;
    int fullscreen_width = 0;       // 0 = use desktop resolution
    int fullscreen_height = 0;
    int refresh_rate = 0;           // 0 = highest available

    // Graphics device settings
    Backend backend = Backend::Auto;
    int device_index = -1;          // -1 = default device
    char device_name[MAX_DEVICE_NAME_LENGTH] = {};  // For validation

    // Rendering settings
    bool vsync = true;
    int samples = 1;                // MSAA (1, 2, 4, 8)
    int back_buffers = 2;           // 2 = double buffering, 3 = triple

    // Color/depth settings
    int color_bits = 32;            // 16, 24, or 32
    int depth_bits = 24;            // 0, 16, 24, or 32
    int stencil_bits = 8;           // 0 or 8

    // Save configuration to file
    // Returns true on success, false on failure
    bool save(const char* filepath) const;

    // Load configuration from file
    // Returns true on success, false on failure (file not found, parse error, etc.)
    // If validation fails (e.g., device no longer exists), affected fields are reset to defaults
    static bool load(const char* filepath, GraphicsConfig* out_config);

    // Validate configuration against current system
    // Returns true if all settings are valid, false if any were adjusted
    // Invalid settings are reset to safe defaults
    bool validate();

    // Convert to Config struct for window creation
    Config to_config() const;
};

//-----------------------------------------------------------------------------
// External Window Configuration
//-----------------------------------------------------------------------------

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
    bool vsync = true;
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

//-----------------------------------------------------------------------------
// Graphics Context
//-----------------------------------------------------------------------------

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

protected:
    Graphics() = default;
};

//-----------------------------------------------------------------------------
// Window
//-----------------------------------------------------------------------------

class Window {
public:
    // Creation/destruction
    static Window* create(const Config& config, Result* out_result = nullptr);

    // Create window from a saved configuration file
    // If the config file doesn't exist or is invalid, uses defaults
    // If specific settings (like device) are no longer valid, they are adjusted
    static Window* create_from_config(const char* config_filepath, Result* out_result = nullptr);

    void destroy();

    // Visibility
    void show();
    void hide();
    bool is_visible() const;

    // Title
    void set_title(const char* title);
    const char* get_title() const;

    // Size
    void set_size(int width, int height);
    void get_size(int* width, int* height) const;
    int get_width() const;
    int get_height() const;

    // Position (returns false if not supported on platform)
    bool set_position(int x, int y);
    bool get_position(int* x, int* y) const;
    bool supports_position() const;

    // Style
    void set_style(WindowStyle style);
    WindowStyle get_style() const;
    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const;
    void set_always_on_top(bool always_on_top);
    bool is_always_on_top() const;

    // State
    bool should_close() const;
    void set_should_close(bool close);
    void poll_events();

    // Graphics
    Graphics* graphics() const;

    // Native handles
    void* native_handle() const;    // HWND, Window, NSWindow*, etc.
    void* native_display() const;   // Display*, wl_display*, etc.

    // Implementation detail - public for platform callbacks
    struct Impl;

private:
    Window() = default;
    ~Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Impl* impl = nullptr;
};

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------

const char* result_to_string(Result result);
const char* backend_to_string(Backend backend);
bool is_backend_supported(Backend backend);
Backend get_default_backend();

//-----------------------------------------------------------------------------
// Device and Display Enumeration
//-----------------------------------------------------------------------------

// Enumerate available graphics devices for a specific backend (or all backends if Auto)
// Returns the number of devices found
int enumerate_devices(Backend backend, DeviceEnumeration* out_devices);

// Enumerate available monitors and their display modes
// Returns the number of monitors found
int enumerate_monitors(MonitorEnumeration* out_monitors);

// Find the best matching display mode for a monitor
// Returns true if a matching mode was found
bool find_display_mode(const MonitorInfo& monitor, int width, int height, int refresh_rate, DisplayMode* out_mode);

// Get the primary monitor info
bool get_primary_monitor(MonitorInfo* out_monitor);

} // namespace window

#endif // WINDOW_HPP
