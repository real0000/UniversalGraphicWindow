/*
 * virtual_keyboard.hpp - Cross-Platform Native Virtual Keyboard Interface
 *
 * This is an independent module for interacting with the system's native
 * on-screen/virtual keyboard. It can be used standalone without the window system.
 *
 * Supported platforms:
 *   - Windows: Touch Keyboard (TabTip.exe), IFrameworkInputPane (Win8+)
 *   - macOS: Accessibility Keyboard, Input Sources
 *   - iOS: UIKit keyboard integration
 *   - Android: InputMethodManager soft keyboard
 *   - Linux/Wayland: zwp_text_input_v3 protocol
 *   - Linux/X11: IBus/Fcitx integration (limited)
 */

#ifndef VIRTUAL_KEYBOARD_HPP
#define VIRTUAL_KEYBOARD_HPP

#include <cstdint>
#include <cstddef>

namespace vkeyboard {

// ============================================================================
// Constants
// ============================================================================

static constexpr int MAX_TEXT_LENGTH = 4096;
static constexpr int MAX_LANGUAGE_CODE_LENGTH = 16;
static constexpr int MAX_KEYBOARD_LAYOUTS = 32;

// ============================================================================
// Enums
// ============================================================================

enum class Result : uint8_t {
    Success = 0,
    ErrorUnknown,
    ErrorNotSupported,          // Platform doesn't support virtual keyboards
    ErrorNotInitialized,
    ErrorAlreadyInitialized,
    ErrorNoKeyboardAvailable,   // No virtual keyboard installed/available
    ErrorPermissionDenied,      // Missing permissions (Android, iOS)
    ErrorInvalidParameter,
    ErrorNotFocused             // No text input context is focused
};

enum class KeyboardType : uint8_t {
    Default = 0,        // Standard alphanumeric keyboard
    Text,               // Optimized for general text input
    Number,             // Numeric keypad
    Phone,              // Phone number input (digits, +, -, etc.)
    Email,              // Email address (includes @ and .com)
    URL,                // URL input (includes /, .com, etc.)
    Password,           // Secure text entry
    Search,             // Search input (may show search button)
    Decimal,            // Decimal number input
    NamePhone,          // Name or phone number
    Twitter,            // Twitter-style (includes @ and #)
    WebSearch,          // Web search
    ASCII,              // ASCII capable only
    NumberPunctuation   // Numbers and punctuation
};

enum class KeyboardAppearance : uint8_t {
    Default = 0,
    Light,
    Dark
};

enum class ReturnKeyType : uint8_t {
    Default = 0,
    Done,
    Go,
    Next,
    Search,
    Send,
    Join,
    Route,
    Continue,
    EmergencyCall
};

enum class AutoCapitalization : uint8_t {
    None = 0,
    Words,              // Capitalize first letter of each word
    Sentences,          // Capitalize first letter of each sentence
    AllCharacters       // All uppercase
};

enum class AutoCorrection : uint8_t {
    Default = 0,
    Enabled,
    Disabled
};

enum class KeyboardState : uint8_t {
    Hidden = 0,
    Showing,            // Animation in progress (showing)
    Visible,
    Hiding              // Animation in progress (hiding)
};

enum class TextInputAction : uint8_t {
    None = 0,
    InsertText,
    DeleteBackward,
    DeleteForward,
    MoveCursor,
    SetSelection,
    Replace,
    Commit              // IME commit
};

// ============================================================================
// Basic Types
// ============================================================================

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool is_empty() const { return width <= 0.0f || height <= 0.0f; }
};

struct TextRange {
    int start = 0;
    int length = 0;

    int end() const { return start + length; }
    bool is_empty() const { return length == 0; }
    static TextRange empty() { return {0, 0}; }
};

// ============================================================================
// Keyboard Configuration
// ============================================================================

struct KeyboardConfig {
    KeyboardType type = KeyboardType::Default;
    KeyboardAppearance appearance = KeyboardAppearance::Default;
    ReturnKeyType return_key = ReturnKeyType::Default;
    AutoCapitalization auto_capitalization = AutoCapitalization::Sentences;
    AutoCorrection auto_correction = AutoCorrection::Default;

    bool spell_checking = true;
    bool smart_quotes = true;
    bool smart_dashes = true;
    bool secure_entry = false;          // Hide input (passwords)
    bool enable_predictions = true;     // Predictive text
    bool enable_dictation = true;       // Voice input button

    // Hint text shown when input is empty
    char placeholder[256] = {};

    // Language/locale hint (e.g., "en-US", "ja-JP")
    char language_hint[MAX_LANGUAGE_CODE_LENGTH] = {};

    // For number inputs: allowed character set
    char allowed_characters[128] = {};

    // Maximum text length (0 = unlimited)
    int max_length = 0;

    // Create default configuration
    static KeyboardConfig default_config() {
        return KeyboardConfig{};
    }

    // Create configuration for specific input types
    static KeyboardConfig for_email() {
        KeyboardConfig config;
        config.type = KeyboardType::Email;
        config.auto_capitalization = AutoCapitalization::None;
        config.auto_correction = AutoCorrection::Disabled;
        return config;
    }

    static KeyboardConfig for_password() {
        KeyboardConfig config;
        config.type = KeyboardType::Password;
        config.secure_entry = true;
        config.auto_correction = AutoCorrection::Disabled;
        config.enable_predictions = false;
        config.spell_checking = false;
        return config;
    }

    static KeyboardConfig for_number() {
        KeyboardConfig config;
        config.type = KeyboardType::Number;
        config.auto_correction = AutoCorrection::Disabled;
        config.enable_predictions = false;
        return config;
    }

    static KeyboardConfig for_phone() {
        KeyboardConfig config;
        config.type = KeyboardType::Phone;
        config.auto_correction = AutoCorrection::Disabled;
        config.enable_predictions = false;
        return config;
    }

    static KeyboardConfig for_url() {
        KeyboardConfig config;
        config.type = KeyboardType::URL;
        config.auto_capitalization = AutoCapitalization::None;
        config.auto_correction = AutoCorrection::Disabled;
        return config;
    }

    static KeyboardConfig for_search() {
        KeyboardConfig config;
        config.type = KeyboardType::Search;
        config.return_key = ReturnKeyType::Search;
        return config;
    }
};

// ============================================================================
// Text Input Context
// ============================================================================

// Represents the current state of a text input field
struct TextInputContext {
    // Current text content (UTF-8)
    char text[MAX_TEXT_LENGTH] = {};
    int text_length = 0;

    // Selection/cursor position
    TextRange selection;            // If length=0, this is cursor position

    // Composition (IME) state
    TextRange composition;          // Currently composing text range
    bool has_composition = false;

    // Hint for keyboard about the text field's position (for keyboard avoidance)
    Rect text_field_frame;

    // Context around cursor (for better predictions)
    char text_before_cursor[256] = {};
    char text_after_cursor[256] = {};
};

// ============================================================================
// Keyboard Event Data
// ============================================================================

struct KeyboardEventData {
    KeyboardState state = KeyboardState::Hidden;
    Rect frame;                     // Keyboard frame in screen coordinates
    float animation_duration = 0.0f; // Animation duration in seconds

    // For state transitions
    KeyboardState previous_state = KeyboardState::Hidden;
};

struct TextInputEventData {
    TextInputAction action = TextInputAction::None;

    // For InsertText, Replace
    char text[MAX_TEXT_LENGTH] = {};
    int text_length = 0;

    // For Replace
    TextRange replace_range;

    // For MoveCursor, SetSelection
    TextRange new_selection;

    // For DeleteBackward, DeleteForward
    int delete_count = 0;
};

// ============================================================================
// Event Handler Interface
// ============================================================================

class IVirtualKeyboardEventHandler {
public:
    virtual ~IVirtualKeyboardEventHandler() = default;

    // Keyboard visibility events
    virtual void on_keyboard_will_show(const KeyboardEventData& data) { (void)data; }
    virtual void on_keyboard_did_show(const KeyboardEventData& data) { (void)data; }
    virtual void on_keyboard_will_hide(const KeyboardEventData& data) { (void)data; }
    virtual void on_keyboard_did_hide(const KeyboardEventData& data) { (void)data; }
    virtual void on_keyboard_frame_changed(const KeyboardEventData& data) { (void)data; }

    // Text input events (from keyboard to application)
    virtual void on_text_input(const TextInputEventData& data) { (void)data; }
    virtual void on_text_committed(const char* text) { (void)text; }
    virtual void on_return_pressed() {}

    // IME composition events
    virtual void on_composition_started() {}
    virtual void on_composition_updated(const char* composition_text, const TextRange& selection) {
        (void)composition_text; (void)selection;
    }
    virtual void on_composition_ended() {}
};

// ============================================================================
// Text Input Delegate Interface
// ============================================================================

// Implement this to provide text input context to the virtual keyboard
class ITextInputDelegate {
public:
    virtual ~ITextInputDelegate() = default;

    // Get current text input context
    virtual TextInputContext get_text_input_context() const = 0;

    // Text modification requests from keyboard
    virtual void insert_text(const char* text) = 0;
    virtual void delete_backward(int count = 1) = 0;
    virtual void delete_forward(int count = 1) = 0;
    virtual void replace_text(const TextRange& range, const char* text) = 0;
    virtual void set_selection(const TextRange& selection) = 0;

    // IME support
    virtual void set_marked_text(const char* text, const TextRange& selected_range) {
        (void)text; (void)selected_range;
    }
    virtual void unmark_text() {}

    // Query support
    virtual bool has_text() const = 0;
    virtual bool can_delete_backward() const { return has_text(); }
    virtual bool can_delete_forward() const { return has_text(); }
};

// ============================================================================
// Keyboard Layout Info
// ============================================================================

struct KeyboardLayoutInfo {
    char identifier[64] = {};           // e.g., "com.apple.keylayout.US"
    char display_name[128] = {};        // e.g., "U.S."
    char language_code[MAX_LANGUAGE_CODE_LENGTH] = {};  // e.g., "en"
    bool is_current = false;
};

struct KeyboardLayoutList {
    KeyboardLayoutInfo layouts[MAX_KEYBOARD_LAYOUTS];
    int count = 0;
};

// ============================================================================
// Virtual Keyboard Interface (Abstract)
// ============================================================================

class IVirtualKeyboard {
public:
    virtual ~IVirtualKeyboard() = default;

    // Lifecycle
    virtual Result initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Platform support query
    virtual bool is_supported() const = 0;
    virtual bool is_available() const = 0;  // Keyboard exists and can be shown

    // Show/hide keyboard
    virtual Result show() = 0;
    virtual Result show(const KeyboardConfig& config) = 0;
    virtual Result hide() = 0;
    virtual Result toggle() = 0;

    // State queries
    virtual KeyboardState get_state() const = 0;
    virtual bool is_visible() const = 0;
    virtual Rect get_frame() const = 0;     // Keyboard frame in screen coordinates
    virtual float get_height() const = 0;   // Convenience: keyboard height

    // Configuration
    virtual void set_config(const KeyboardConfig& config) = 0;
    virtual KeyboardConfig get_config() const = 0;

    // Text input context
    virtual void set_text_input_delegate(ITextInputDelegate* delegate) = 0;
    virtual ITextInputDelegate* get_text_input_delegate() const = 0;
    virtual void update_text_input_context(const TextInputContext& context) = 0;

    // Focus management
    virtual void begin_text_input() = 0;    // Indicate text input is starting
    virtual void end_text_input() = 0;      // Indicate text input is ending
    virtual bool is_text_input_active() const = 0;

    // Event handling
    virtual void set_event_handler(IVirtualKeyboardEventHandler* handler) = 0;

    // Keyboard layouts
    virtual Result get_available_layouts(KeyboardLayoutList* out_list) const = 0;
    virtual Result get_current_layout(KeyboardLayoutInfo* out_info) const = 0;
    virtual Result set_layout(const char* identifier) = 0;

    // Platform-specific
    virtual void* get_native_handle() const = 0;
    virtual void set_target_window(void* window_handle) { (void)window_handle; }

    // Utility
    virtual void update() = 0;  // Call periodically to process events
};

// ============================================================================
// Factory Function
// ============================================================================

// Create platform-specific virtual keyboard instance
IVirtualKeyboard* create_virtual_keyboard();

// Destroy virtual keyboard instance
void destroy_virtual_keyboard(IVirtualKeyboard* keyboard);

// ============================================================================
// Utility Functions
// ============================================================================

const char* result_to_string(Result result);
const char* keyboard_type_to_string(KeyboardType type);
const char* keyboard_state_to_string(KeyboardState state);
const char* return_key_type_to_string(ReturnKeyType type);

// Check if platform supports virtual keyboard
bool is_virtual_keyboard_supported();

// Get platform name
const char* get_platform_name();

} // namespace vkeyboard

#endif // VIRTUAL_KEYBOARD_HPP
