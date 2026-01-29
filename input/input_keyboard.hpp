/*
 * input_keyboard.hpp - Cross-platform keyboard input utilities
 *
 * This file contains platform-independent keyboard handling code including:
 * - Key enum to string conversions
 * - Event type to string conversions
 * - KeyMod utilities
 *
 * For mouse utilities, see input_mouse.hpp
 *
 * Platform-specific key translation (from native keycodes to Key enum) remains
 * in each platform's window implementation file.
 */

#ifndef INPUT_KEYBOARD_HPP
#define INPUT_KEYBOARD_HPP

#include "../window.hpp"

namespace window {
namespace input {

//=============================================================================
// Key Code Utilities
//=============================================================================

// Convert Key enum to human-readable string
// Returns "Unknown" for unrecognized keys
const char* key_to_string(Key key);

// Convert string to Key enum (case-insensitive)
// Returns Key::Unknown if not found
Key string_to_key(const char* str);

// Get the printable character for a key (if any)
// Returns 0 if the key doesn't have a printable representation
// Note: This returns the unshifted character (e.g., 'a' not 'A')
char key_to_char(Key key);

// Check if a key is a modifier key
bool is_modifier_key(Key key);

// Check if a key is a function key (F1-F24)
bool is_function_key(Key key);

// Check if a key is a numpad key
bool is_numpad_key(Key key);

// Check if a key is a navigation key (arrows, home, end, etc.)
bool is_navigation_key(Key key);

// Check if a key is a letter key (A-Z)
bool is_letter_key(Key key);

// Check if a key is a number key (0-9, not numpad)
bool is_number_key(Key key);

// Mouse button utilities are in input_mouse.hpp

//=============================================================================
// Event Type Utilities
//=============================================================================

// Convert EventType enum to human-readable string
const char* event_type_to_string(EventType type);

// Convert string to EventType enum (case-insensitive)
EventType string_to_event_type(const char* str);

// Check if an event type is a keyboard event
bool is_keyboard_event(EventType type);

// Check if an event type is a mouse event
bool is_mouse_event(EventType type);

// Check if an event type is a touch event
bool is_touch_event(EventType type);

// Check if an event type is a window event
bool is_window_event(EventType type);

//=============================================================================
// Key Modifier Utilities
//=============================================================================

// Convert KeyMod flags to human-readable string (e.g., "Ctrl+Shift")
// buffer must be at least 64 bytes
void keymod_to_string(KeyMod mods, char* buffer, size_t buffer_size);

// Parse modifier string (e.g., "Ctrl+Shift") to KeyMod flags
KeyMod string_to_keymod(const char* str);

// Get the generic modifier key for a specific left/right key
// e.g., Key::LeftShift -> Key::Shift, Key::RightControl -> Key::Control
Key get_generic_modifier(Key key);

// Get corresponding KeyMod flag for a modifier Key
// Returns KeyMod::None if not a modifier key
KeyMod key_to_keymod(Key key);

//=============================================================================
// Constants
//=============================================================================

static const int MAX_KEYBOARD_HANDLERS = 16;
static const int MAX_KEY_STATES = 512;

//=============================================================================
// IKeyboardHandler - Keyboard event handler interface
//=============================================================================

// Interface for objects that handle keyboard events.
// Handlers are called in priority order (highest first).
// Return true from an event handler to consume the event and stop propagation.
class IKeyboardHandler {
public:
    virtual ~IKeyboardHandler() = default;

    // Get unique identifier for this handler
    virtual const char* get_handler_id() const = 0;

    // Get priority (higher values = called first, default = 0)
    virtual int get_priority() const { return 0; }

    // Key event handler (key down, up, repeat)
    // Return true to consume the event (stop propagation to lower priority handlers)
    virtual bool on_key(const KeyEvent& event) { (void)event; return false; }

    // Character input handler (for text input)
    // Return true to consume the event
    virtual bool on_char(const CharEvent& event) { (void)event; return false; }
};

//=============================================================================
// KeyboardEventDispatcher - Manages keyboard handlers and dispatches events
//=============================================================================

class KeyboardEventDispatcher {
public:
    KeyboardEventDispatcher();
    ~KeyboardEventDispatcher();

    // Handler management
    // Returns true on success, false if handler limit reached or handler already registered
    bool add_handler(IKeyboardHandler* handler);

    // Remove a handler by pointer
    // Returns true if handler was found and removed
    bool remove_handler(IKeyboardHandler* handler);

    // Remove a handler by ID
    // Returns true if handler was found and removed
    bool remove_handler(const char* handler_id);

    // Get handler count
    int get_handler_count() const;

    // Get handler by index (for iteration)
    IKeyboardHandler* get_handler(int index) const;

    // Find handler by ID
    IKeyboardHandler* find_handler(const char* handler_id) const;

    // Event dispatch - calls handlers in priority order
    // Returns true if event was consumed by any handler
    bool dispatch_key(const KeyEvent& event);
    bool dispatch_char(const CharEvent& event);

private:
    void sort_handlers();

    IKeyboardHandler* handlers_[MAX_KEYBOARD_HANDLERS];
    int handler_count_;
    bool needs_sort_;
};

//=============================================================================
// DefaultKeyboardDevice - Standard keyboard input from platform
//=============================================================================

// Default keyboard device that receives events from the platform layer.
// Each Window has one of these that receives events from the platform's
// message/event handling code.
class DefaultKeyboardDevice {
public:
    DefaultKeyboardDevice();
    ~DefaultKeyboardDevice();

    // Get unique identifier for this device
    const char* get_device_id() const;

    // Check if this device is currently active
    bool is_active() const;

    // Set the dispatcher to forward events to
    void set_dispatcher(KeyboardEventDispatcher* dispatcher);

    // Set the owning window (for event construction)
    void set_window(Window* window);

    // Platform layer calls these to inject events
    // These update internal state and dispatch to the registered dispatcher
    void inject_key_down(Key key, KeyMod modifiers, int scancode, bool repeat, double timestamp);
    void inject_key_up(Key key, KeyMod modifiers, int scancode, double timestamp);
    void inject_char(uint32_t codepoint, KeyMod modifiers, double timestamp);

    // State queries
    bool is_key_down(Key key) const;

    // Reset state (e.g., on focus loss)
    void reset();

private:
    bool key_states_[MAX_KEY_STATES];
    KeyboardEventDispatcher* dispatcher_;
    Window* window_;
    bool active_;
};

} // namespace input
} // namespace window

#endif // INPUT_KEYBOARD_HPP
