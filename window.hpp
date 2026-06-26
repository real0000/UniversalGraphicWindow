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
#include <string>

// Include graphics API types (Backend, SwapMode, TextureFormat, Graphics, etc.)
#include "graphics_api.hpp"

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

// Note: Result, Backend, SwapMode, TextureFormat are defined in graphics_api.hpp

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
// Message Box
//-----------------------------------------------------------------------------

// Button configuration for message boxes
enum class MessageBoxType : uint8_t {
    Ok = 0,             // Single "OK" button
    OkCancel,           // "OK" and "Cancel" buttons
    YesNo,              // "Yes" and "No" buttons
    YesNoCancel,        // "Yes", "No", and "Cancel" buttons
    RetryCancel,        // "Retry" and "Cancel" buttons
    AbortRetryIgnore    // "Abort", "Retry", and "Ignore" buttons
};

// Icon displayed in the message box
enum class MessageBoxIcon : uint8_t {
    None = 0,           // No icon
    Info,               // Informational
    Warning,            // Warning
    Error,              // Error
    Question            // Question
};

// Return value indicating which button was pressed
enum class MessageBoxButton : uint8_t {
    None = 0,           // No button pressed / dialog dismissed
    Ok,
    Cancel,
    Yes,
    No,
    Retry,
    Abort,
    Ignore
};

// Callback for asynchronous message box results
using MessageBoxCallback = std::function<void(MessageBoxButton result)>;

//-----------------------------------------------------------------------------
// Common Dialogs (file open/save, folder picker, color picker, font picker)
//-----------------------------------------------------------------------------
// A native OS dialog is used where one exists (Win32 IFileDialog/ChooseColor/
// ChooseFont, macOS NSOpenPanel/NSColorPanel/NSFontPanel, mobile/web pickers).
// On platforms without a native dialog the library falls back to a plain
// window it draws itself (see the X11 implementation), so the same API works
// everywhere. All blocking calls run their own modal loop and return when the
// user confirms or cancels; the *_async variants return immediately and invoke
// the callback with the result.

// A single file-type filter, e.g. {"Image Files", "png;jpg;jpeg;bmp"}.
// 'spec' is a semicolon-separated list of extensions WITHOUT a leading dot;
// the platform layer formats them into the OS-native pattern. Use "*" to match
// every file (an empty 'spec' is treated the same way).
struct DialogFilter {
    std::string name;   // Human-readable label shown in the type dropdown
    std::string spec;   // "png;jpg;jpeg"  or  "*"
};

// Options shared by the open / save / folder dialogs.
struct FileDialogOptions {
    std::string title;                  // Dialog title (empty = platform default)
    std::string initial_dir;            // Starting directory (empty = system default)
    std::string initial_name;           // Suggested file name (save dialog)
    std::vector<DialogFilter> filters;  // File-type filters (open/save dialogs)
    int default_filter = 0;             // Index into 'filters' to select initially
    bool allow_multiple = false;        // Open dialog: allow selecting several files
    bool can_create_dirs = true;        // Allow creating new folders from the dialog
    bool overwrite_prompt = true;       // Save dialog: confirm before overwriting
    Window* parent = nullptr;           // Owner window (nullptr = application-modal)
};

// Result of an open / save / folder dialog.
struct FileDialogResult {
    bool ok = false;                    // True only if the user confirmed a choice
    std::vector<std::string> paths;     // Selected absolute path(s), UTF-8
    int filter_index = 0;               // Filter active when the user confirmed

    // Convenience accessor for the first (often only) selected path.
    const std::string& path() const {
        static const std::string empty_path;
        return paths.empty() ? empty_path : paths.front();
    }
};

// 8-bit-per-channel RGBA color used by the color dialog.
struct DialogColor {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct ColorDialogOptions {
    std::string title;
    DialogColor initial{0, 0, 0, 255};  // Pre-selected color
    bool allow_alpha = false;           // Expose an alpha/opacity control
    Window* parent = nullptr;
};

struct ColorDialogResult {
    bool ok = false;
    DialogColor color{0, 0, 0, 255};
};

// Font description used by the font dialog.
struct DialogFont {
    std::string family = "";            // Family name, e.g. "Segoe UI" / "Helvetica"
    float size_pt = 12.0f;              // Size in points
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    DialogColor color{0, 0, 0, 255};    // Text color (only where the OS supports it)
};

struct FontDialogOptions {
    std::string title;
    DialogFont initial;                 // Pre-selected font
    bool allow_color = false;           // Expose a text-color control (where supported)
    Window* parent = nullptr;
};

struct FontDialogResult {
    bool ok = false;
    DialogFont font;
};

// Callbacks for the asynchronous dialog variants.
using FileDialogCallback  = std::function<void(const FileDialogResult& result)>;
using ColorDialogCallback = std::function<void(const ColorDialogResult& result)>;
using FontDialogCallback  = std::function<void(const FontDialogResult& result)>;

//-----------------------------------------------------------------------------
// Forward Declarations
//-----------------------------------------------------------------------------

// Note: Graphics class is defined in graphics_api.hpp

namespace input {
    class IMouseHandler;
    class MouseEventDispatcher;
    class IKeyboardHandler;
    class KeyboardEventDispatcher;
} // namespace input

//-----------------------------------------------------------------------------
// Structures
//-----------------------------------------------------------------------------

// Note: GraphicsDeviceInfo, DisplayMode, MonitorInfo, DeviceEnumeration,
//       MonitorEnumeration are defined in graphics_api.hpp

//-----------------------------------------------------------------------------
// Configuration (saveable/loadable, supports multi-window)
//-----------------------------------------------------------------------------

static const int MAX_CONFIG_WINDOWS = 16;

// Individual window configuration within Config.
// x/y/width/height are *logical* pixels (CSS-style). The platform layer
// multiplies them by the target monitor's DPI scale at creation time so the
// window has the same physical size across screens with different scaling.
struct WindowConfigEntry {
    std::string name = "main";      // Unique identifier
    std::string title = "Window";
    int x = -1;                     // -1 = centered (logical px)
    int y = -1;                     // -1 = centered (logical px)
    int width = 800;                // logical px
    int height = 600;               // logical px
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
    std::string device_name;        // For validation

    // Rendering settings
    SwapMode swap_mode = SwapMode::Auto;  // Swap chain presentation mode
    bool vsync = true;              // Used when swap_mode is Auto
    int samples = 1;                // MSAA (1, 2, 4, 8)
    int back_buffers = 2;           // 2 = double buffering, 3 = triple

    // Color/depth settings
    int color_bits = 32;            // 16, 24, 32, or 64 (64 = 16 bits per channel HDR)
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

// Note: ExternalWindowConfig and Graphics class are defined in graphics_api.hpp

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

    // Size — physical (framebuffer) pixels.
    // On Hi-DPI displays this is the scaled-up size; for the logical (CSS-style)
    // size used in layout code, divide by get_dpi_scale().
    void set_size(int width, int height);
    void get_size(int* width, int* height) const;
    int get_width() const;
    int get_height() const;

    // Position (returns false if not supported on platform)
    bool set_position(int x, int y);
    bool get_position(int* x, int* y) const;
    bool supports_position() const;

    // DPI / scale factor.
    // get_dpi_scale() returns physical_pixels / logical_pixels (1.0 at 96 DPI,
    // 1.5 at 144, 2.0 at 192). Driven by the current OS/screen scaling setting
    // and updated on DpiChange events.
    float get_dpi_scale() const;
    int   get_dpi() const;

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
    // Message Box
    //-------------------------------------------------------------------------
    // Show a native message box dialog.
    // If 'parent' is nullptr, the dialog is application-modal.
    // If 'parent' is a valid Window*, the dialog is window-modal.

    // Blocking - blocks calling thread until user responds
    static MessageBoxButton show_message_box(
        const char* title,
        const char* message,
        MessageBoxType type = MessageBoxType::Ok,
        MessageBoxIcon icon = MessageBoxIcon::None,
        Window* parent = nullptr);

    // Async - returns immediately, callback invoked when user responds
    static void show_message_box_async(
        const char* title,
        const char* message,
        MessageBoxType type,
        MessageBoxIcon icon,
        Window* parent,
        MessageBoxCallback callback);

    //-------------------------------------------------------------------------
    // Common Dialogs (file / folder / color / font)
    //-------------------------------------------------------------------------
    // Blocking calls run a modal loop and return when the user confirms or
    // cancels (result.ok == false on cancel). The *_async variants return
    // immediately and invoke the callback with the result.

    // File selection
    static FileDialogResult show_open_file_dialog(const FileDialogOptions& options = FileDialogOptions{});
    static FileDialogResult show_save_file_dialog(const FileDialogOptions& options = FileDialogOptions{});
    static FileDialogResult show_folder_dialog(const FileDialogOptions& options = FileDialogOptions{});

    // Color / font selection
    static ColorDialogResult show_color_dialog(const ColorDialogOptions& options = ColorDialogOptions{});
    static FontDialogResult  show_font_dialog(const FontDialogOptions& options = FontDialogOptions{});

    // Async variants
    static void show_open_file_dialog_async(const FileDialogOptions& options, FileDialogCallback callback);
    static void show_save_file_dialog_async(const FileDialogOptions& options, FileDialogCallback callback);
    static void show_folder_dialog_async(const FileDialogOptions& options, FileDialogCallback callback);
    static void show_color_dialog_async(const ColorDialogOptions& options, ColorDialogCallback callback);
    static void show_font_dialog_async(const FontDialogOptions& options, FontDialogCallback callback);

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

    // System clipboard (UTF-8 text). Implemented on X11; other platforms are
    // currently no-op / return empty until wired up.
    void        set_clipboard_text(const char* utf8);
    std::string get_clipboard_text();

    // Mouse cursor shape (e.g. IBeam over selectable text). Implemented on X11.
    void set_cursor(CursorType cursor);

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

    // Platform-internal accessor — used by Wayland event callbacks (and any
    // other free function in a platform .cpp) to reach the impl. Not part of
    // the public API; do not call from application code.
    friend Impl* internal_get_impl(Window* w);
};

inline Window::Impl* internal_get_impl(Window* w) { return w ? w->impl : nullptr; }

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------

// Note: result_to_string, backend_to_string, is_backend_supported,
//       get_default_backend are defined in graphics_api.hpp

// Cursor utilities
const char* cursor_type_to_string(CursorType type);
CursorType string_to_cursor_type(const char* str);

// Input utilities
const char* key_to_string(Key key);
const char* mouse_button_to_string(MouseButton button);
const char* event_type_to_string(EventType type);

// Message box utilities
const char* message_box_type_to_string(MessageBoxType type);
const char* message_box_icon_to_string(MessageBoxIcon icon);
const char* message_box_button_to_string(MessageBoxButton button);

// Note: Device and display enumeration functions are defined in graphics_api.hpp

//-----------------------------------------------------------------------------
// Internal Window Creation
//-----------------------------------------------------------------------------

// Internal function to create a single window - called by Window::create()
// Do not call directly; use Window::create() instead
Window* create_window_impl(const Config& config, Result* out_result = nullptr);

} // namespace window

#endif // WINDOW_HPP
