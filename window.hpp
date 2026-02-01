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
#include <functional>
#include <vector>

//=============================================================================
// Platform Detection (Internal)
// These may already be defined by the build system (CMake), so check first
//=============================================================================

#if defined(__EMSCRIPTEN__)
    #ifndef WINDOW_PLATFORM_WASM
    #define WINDOW_PLATFORM_WASM
    #endif
#elif defined(_WIN32)
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

// Standard cursor types supported across platforms
enum class CursorType : uint8_t {
    Arrow = 0,          // Default arrow cursor
    IBeam,              // Text input cursor (I-beam)
    Crosshair,          // Precise selection crosshair
    Hand,               // Pointing hand (for links)
    ResizeH,            // Horizontal resize (left-right)
    ResizeV,            // Vertical resize (up-down)
    ResizeNESW,         // Diagonal resize (northeast-southwest)
    ResizeNWSE,         // Diagonal resize (northwest-southeast)
    ResizeAll,          // Move/resize in all directions
    NotAllowed,         // Operation not allowed
    Wait,               // Busy/wait cursor
    WaitArrow,          // Busy but still interactive
    Help,               // Help cursor (arrow with question mark)
    Hidden,             // No cursor visible
    Custom,             // Custom cursor (platform-specific)
    Count               // Number of cursor types
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
// Input Enumerations
//-----------------------------------------------------------------------------

// Mouse buttons
enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    X1 = 3,         // Extra button 1 (back)
    X2 = 4,         // Extra button 2 (forward)
    Unknown = 255
};

// Key codes (platform-independent virtual key codes)
enum class Key : uint16_t {
    Unknown = 0,

    // Letters
    A = 'A', B = 'B', C = 'C', D = 'D', E = 'E', F = 'F', G = 'G', H = 'H',
    I = 'I', J = 'J', K = 'K', L = 'L', M = 'M', N = 'N', O = 'O', P = 'P',
    Q = 'Q', R = 'R', S = 'S', T = 'T', U = 'U', V = 'V', W = 'W', X = 'X',
    Y = 'Y', Z = 'Z',

    // Numbers
    Num0 = '0', Num1 = '1', Num2 = '2', Num3 = '3', Num4 = '4',
    Num5 = '5', Num6 = '6', Num7 = '7', Num8 = '8', Num9 = '9',

    // Function keys
    F1 = 256, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

    // Navigation
    Escape = 300, Tab, CapsLock, Shift, Control, Alt, Super,   // Super = Win/Cmd
    Space, Enter, Backspace, Delete, Insert,
    Home, End, PageUp, PageDown,
    Left, Right, Up, Down,

    // Modifiers (left/right variants)
    LeftShift = 350, RightShift, LeftControl, RightControl,
    LeftAlt, RightAlt, LeftSuper, RightSuper,

    // Punctuation and symbols
    Grave = 400,        // `~
    Minus,              // -_
    Equal,              // =+
    LeftBracket,        // [{
    RightBracket,       // ]}
    Backslash,          // \|
    Semicolon,          // ;:
    Apostrophe,         // '"
    Comma,              // ,<
    Period,             // .>
    Slash,              // /?

    // Numpad
    Numpad0 = 450, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadDecimal, NumpadEnter, NumpadAdd, NumpadSubtract,
    NumpadMultiply, NumpadDivide, NumLock,

    // Other
    PrintScreen = 500, ScrollLock, Pause,
    Menu,               // Context menu key
};

// Key modifiers (can be combined with |)
enum class KeyMod : uint8_t {
    None    = 0,
    Shift   = 1 << 0,
    Control = 1 << 1,
    Alt     = 1 << 2,
    Super   = 1 << 3,   // Win/Cmd key
    CapsLock = 1 << 4,
    NumLock = 1 << 5
};

inline KeyMod operator|(KeyMod a, KeyMod b) {
    return static_cast<KeyMod>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline KeyMod operator&(KeyMod a, KeyMod b) {
    return static_cast<KeyMod>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool has_mod(KeyMod mods, KeyMod flag) {
    return (static_cast<uint8_t>(mods) & static_cast<uint8_t>(flag)) != 0;
}

//-----------------------------------------------------------------------------
// Event Types
//-----------------------------------------------------------------------------

enum class EventType : uint8_t {
    None = 0,

    // Window events
    WindowClose,        // Window close requested
    WindowResize,       // Window resized
    WindowMove,         // Window moved
    WindowFocus,        // Window gained focus
    WindowBlur,         // Window lost focus
    WindowMinimize,     // Window minimized
    WindowMaximize,     // Window maximized
    WindowRestore,      // Window restored from min/max

    // Keyboard events
    KeyDown,            // Key pressed
    KeyUp,              // Key released
    KeyRepeat,          // Key repeat (held down)
    CharInput,          // Character input (for text input)

    // Mouse events
    MouseDown,          // Mouse button pressed
    MouseMove,          // Mouse moved
    MouseUp,            // Mouse button released
    MouseWheel,         // Mouse wheel scrolled

    // Touch events (mobile/touchscreen)
    TouchDown,          // Touch started
    TouchUp,            // Touch ended
    TouchMove,          // Touch moved

    // System events
    DpiChange,          // DPI/scale factor changed
    DropFile,           // File dropped onto window
};

//-----------------------------------------------------------------------------
// Event Structures
//-----------------------------------------------------------------------------

class Window;  // Forward declaration

// Base event structure
struct Event {
    EventType type = EventType::None;
    Window* window = nullptr;       // Source window
    double timestamp = 0.0;         // Event timestamp in seconds
};

// Window events
struct WindowCloseEvent : Event {
    // No additional data - just the close request
};

struct WindowResizeEvent : Event {
    int width = 0;
    int height = 0;
    bool minimized = false;         // True if minimized (width/height may be 0)
};

struct WindowMoveEvent : Event {
    int x = 0;
    int y = 0;
};

struct WindowFocusEvent : Event {
    bool focused = false;           // True for focus, false for blur
};

struct WindowStateEvent : Event {
    bool minimized = false;
    bool maximized = false;
};

// Keyboard events
struct KeyEvent : Event {
    Key key = Key::Unknown;
    KeyMod modifiers = KeyMod::None;
    int scancode = 0;               // Platform-specific scancode
    bool repeat = false;            // True if this is a repeat event
};

struct CharEvent : Event {
    uint32_t codepoint = 0;         // Unicode codepoint
    KeyMod modifiers = KeyMod::None;
};

// Mouse events
struct MouseMoveEvent : Event {
    int x = 0;                      // Position relative to window
    int y = 0;
    int dx = 0;                     // Delta from last position
    int dy = 0;
    KeyMod modifiers = KeyMod::None;
};

struct MouseButtonEvent : Event {
    MouseButton button = MouseButton::Unknown;
    int x = 0;
    int y = 0;
    int clicks = 1;                 // 1 = single, 2 = double click, etc.
    KeyMod modifiers = KeyMod::None;
};

struct MouseWheelEvent : Event {
    float dx = 0.0f;                // Horizontal scroll
    float dy = 0.0f;                // Vertical scroll (positive = up/away)
    int x = 0;                      // Mouse position
    int y = 0;
    KeyMod modifiers = KeyMod::None;
};

// Touch events
struct TouchEvent : Event {
    int touch_id = 0;               // Unique ID for this touch point
    float x = 0.0f;                 // Position (0-1 normalized or pixels)
    float y = 0.0f;
    float pressure = 1.0f;          // Touch pressure (0-1)
};

// System events
struct DpiChangeEvent : Event {
    float scale = 1.0f;             // New DPI scale factor
    int dpi = 96;                   // New DPI value
};

struct DropFileEvent : Event {
    const char* const* paths = nullptr;  // Array of file paths
    int count = 0;                  // Number of files
};

//-----------------------------------------------------------------------------
// Event Callback Types (using std::function for flexibility)
//-----------------------------------------------------------------------------

// Generic event callback
using EventCallback = std::function<void(const Event& event)>;

// Typed callbacks for specific events
using WindowCloseCallback = std::function<void(const WindowCloseEvent& event)>;
using WindowResizeCallback = std::function<void(const WindowResizeEvent& event)>;
using WindowMoveCallback = std::function<void(const WindowMoveEvent& event)>;
using WindowFocusCallback = std::function<void(const WindowFocusEvent& event)>;
using WindowStateCallback = std::function<void(const WindowStateEvent& event)>;

using KeyCallback = std::function<void(const KeyEvent& event)>;
using CharCallback = std::function<void(const CharEvent& event)>;

using MouseButtonCallback = std::function<void(const MouseButtonEvent& event)>;
using MouseMoveCallback = std::function<void(const MouseMoveEvent& event)>;
using MouseWheelCallback = std::function<void(const MouseWheelEvent& event)>;

using TouchCallback = std::function<void(const TouchEvent& event)>;
using DpiChangeCallback = std::function<void(const DpiChangeEvent& event)>;
using DropFileCallback = std::function<void(const DropFileEvent& event)>;

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

namespace input {
    class IMouseHandler;
    class MouseEventDispatcher;
    class IKeyboardHandler;
    class KeyboardEventDispatcher;
} // namespace input

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------

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
// Configuration (saveable/loadable, supports multi-window)
//-----------------------------------------------------------------------------

static const int MAX_CONFIG_WINDOWS = 16;
static const int MAX_WINDOW_NAME_LENGTH = 64;

// Individual window configuration within Config
struct WindowConfigEntry {
    char name[MAX_WINDOW_NAME_LENGTH] = "main";     // Unique identifier
    char title[MAX_DEVICE_NAME_LENGTH] = "Window";
    int x = -1;                     // -1 = centered
    int y = -1;                     // -1 = centered
    int width = 800;
    int height = 600;
    int monitor_index = 0;          // Which monitor to use
    bool fullscreen = false;
    WindowStyle style = WindowStyle::Default;
    bool visible = true;
};

struct Config {
    //-------------------------------------------------------------------------
    // Graphics device settings (shared across all windows)
    //-------------------------------------------------------------------------
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

    //-------------------------------------------------------------------------
    // Window configurations
    //-------------------------------------------------------------------------
    WindowConfigEntry windows[MAX_CONFIG_WINDOWS];
    int window_count = 1;           // At least 1 window

    //-------------------------------------------------------------------------
    // Shared context (for multi-window resource sharing)
    //-------------------------------------------------------------------------
    // OpenGL: shares textures, buffers, shaders
    // D3D11/D3D12/Vulkan/Metal: shares device (creates new swapchain)
    Graphics* shared_graphics = nullptr;

    //-------------------------------------------------------------------------
    // Methods
    //-------------------------------------------------------------------------

    // Save configuration to file
    bool save(const char* filepath) const;

    // Load configuration from file
    static bool load(const char* filepath, Config* out_config);

    // Validate configuration against current system
    bool validate();

    // Find window by name (returns nullptr if not found)
    WindowConfigEntry* find_window(const char* name);
    const WindowConfigEntry* find_window(const char* name) const;

    // Add a window configuration (returns false if MAX_CONFIG_WINDOWS reached or name exists)
    bool add_window(const WindowConfigEntry& entry);

    // Remove a window by name (returns false if not found)
    bool remove_window(const char* name);
};

// Backward compatibility typedef
using GraphicsConfig = Config;

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
    static std::vector<Window*> create(const Config& config, Result* out_result = nullptr);

    // Create window from a saved configuration file
    // If the config file doesn't exist or is invalid, uses defaults
    // If specific settings (like device) are no longer valid, they are adjusted
    static std::vector<Window*> create_from_config(const char* config_filepath, Result* out_result = nullptr);

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

    //-------------------------------------------------------------------------
    // Event Callbacks
    //-------------------------------------------------------------------------
    // Set callbacks for specific event types. Pass empty std::function to remove callback.
    // Use lambda captures for context instead of user_data.

    // Window events
    void set_close_callback(WindowCloseCallback callback);
    void set_resize_callback(WindowResizeCallback callback);
    void set_move_callback(WindowMoveCallback callback);
    void set_focus_callback(WindowFocusCallback callback);
    void set_state_callback(WindowStateCallback callback);

    // Touch events
    void set_touch_callback(TouchCallback callback);

    // System events
    void set_dpi_change_callback(DpiChangeCallback callback);
    void set_drop_file_callback(DropFileCallback callback);

    //-------------------------------------------------------------------------
    // Input State Queries
    //-------------------------------------------------------------------------
    // Query current input state (useful for game-style input)

    bool is_key_down(Key key) const;
    bool is_mouse_button_down(MouseButton button) const;
    void get_mouse_position(int* x, int* y) const;
    KeyMod get_current_modifiers() const;

    //-------------------------------------------------------------------------
    // Mouse Handler API
    //-------------------------------------------------------------------------
    // Add/remove mouse event handlers. Handlers are called in priority order.
    // Returns true on success, false on failure (limit reached or already registered).

    bool add_mouse_handler(input::IMouseHandler* handler);
    bool remove_mouse_handler(input::IMouseHandler* handler);
    bool remove_mouse_handler(const char* handler_id);
    input::MouseEventDispatcher* get_mouse_dispatcher();

    //-------------------------------------------------------------------------
    // Keyboard Handler API
    //-------------------------------------------------------------------------
    // Add/remove keyboard event handlers. Handlers are called in priority order.
    // Returns true on success, false on failure (limit reached or already registered).

    bool add_keyboard_handler(input::IKeyboardHandler* handler);
    bool remove_keyboard_handler(input::IKeyboardHandler* handler);
    bool remove_keyboard_handler(const char* handler_id);
    input::KeyboardEventDispatcher* get_keyboard_dispatcher();

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

    // Friend function for internal window creation (implemented per-platform)
    friend Window* create_window_impl(const Config& config, Result* out_result);
};

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------

const char* result_to_string(Result result);
const char* backend_to_string(Backend backend);
bool is_backend_supported(Backend backend);
Backend get_default_backend();

// Cursor utilities
const char* cursor_type_to_string(CursorType type);
CursorType string_to_cursor_type(const char* str);

// Input utilities
const char* key_to_string(Key key);
const char* mouse_button_to_string(MouseButton button);
const char* event_type_to_string(EventType type);

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

//-----------------------------------------------------------------------------
// Internal Window Creation
//-----------------------------------------------------------------------------

// Internal function to create a single window - called by Window::create()
// Do not call directly; use Window::create() instead
Window* create_window_impl(const Config& config, Result* out_result = nullptr);

} // namespace window

#endif // WINDOW_HPP
