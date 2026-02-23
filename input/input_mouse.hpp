/*
 * input_mouse.hpp - Cross-platform mouse input utilities
 *
 * This file contains platform-independent mouse handling code including:
 * - Mouse button to string conversions
 * - Mouse state utilities
 *
 * Platform-specific mouse handling (button translation, cursor changes)
 * remains in each platform's window implementation file.
 */

#ifndef INPUT_MOUSE_HPP
#define INPUT_MOUSE_HPP

#include "../window.hpp"

namespace window {
namespace input {

//=============================================================================
// Mouse Button Utilities
//=============================================================================

// Convert MouseButton enum to human-readable string
const char* mouse_button_to_string(MouseButton button);

// Convert string to MouseButton enum (case-insensitive)
MouseButton string_to_mouse_button(const char* str);

// Get button index (0-4) for array indexing, -1 if invalid
int mouse_button_to_index(MouseButton button);

// Get MouseButton from index (0-4)
MouseButton index_to_mouse_button(int index);

// Check if button is a primary button (left/right)
bool is_primary_button(MouseButton button);

// Check if button is an extra button (X1/X2)
bool is_extra_button(MouseButton button);

//=============================================================================
// Mouse State Tracking
//=============================================================================

// Maximum number of mouse buttons to track
static const int MAX_MOUSE_BUTTONS = 5;

// Mouse state structure for tracking button states and position
struct MouseState {
    bool buttons[MAX_MOUSE_BUTTONS] = {};  // Button states (indexed by MouseButton)
    int x = 0;                              // Current X position
    int y = 0;                              // Current Y position

    // Check if any button is pressed
    bool any_button_down() const;

    // Update position
    void set_position(int new_x, int new_y);

    // Reset all state
    void reset();
};

//=============================================================================
// Constants
//=============================================================================

static const int MAX_MOUSE_HANDLERS = 16;

//=============================================================================
// IMouseHandler - Event handler interface
//=============================================================================

// Interface for objects that handle mouse events.
// Handlers are called in priority order (highest first).
// Return true from an event handler to consume the event and stop propagation.
class IMouseHandler {
public:
    virtual ~IMouseHandler() = default;

    // Get unique identifier for this handler
    virtual const char* get_handler_id() const = 0;

    // Get priority (higher values = called first, default = 0)
    virtual int get_priority() const { return 0; }

    // Mouse move event handler
    // Return true to consume the event (stop propagation to lower priority handlers)
    virtual bool on_mouse_move(const MouseMoveEvent& event) { (void)event; return false; }

    // Mouse button event handler (both press and release)
    // Return true to consume the event
    virtual bool on_mouse_button(const MouseButtonEvent& event) { (void)event; return false; }

    // Mouse wheel event handler
    // Return true to consume the event
    virtual bool on_mouse_wheel(const MouseWheelEvent& event) { (void)event; return false; }
};

//=============================================================================
// IMouseEventSource - Input source interface
//=============================================================================

// Interface for mouse input sources (real hardware, virtual devices, etc.)
class IMouseEventSource {
public:
    virtual ~IMouseEventSource() = default;

    // Get unique identifier for this source
    virtual const char* get_source_id() const = 0;

    // Check if this source is currently active/connected
    virtual bool is_active() const = 0;

    // Get current mouse state
    virtual const MouseState& get_state() const = 0;
};

//=============================================================================
// MouseEventDispatcher - Manages handlers and dispatches events
//=============================================================================

class MouseEventDispatcher {
public:
    MouseEventDispatcher();
    ~MouseEventDispatcher();

    // Handler management
    // Returns true on success, false if handler limit reached or handler already registered
    bool add_handler(IMouseHandler* handler);

    // Remove a handler by pointer
    // Returns true if handler was found and removed
    bool remove_handler(IMouseHandler* handler);

    // Remove a handler by ID
    // Returns true if handler was found and removed
    bool remove_handler(const char* handler_id);

    // Get handler count
    int get_handler_count() const;

    // Get handler by index (for iteration)
    IMouseHandler* get_handler(int index) const;

    // Find handler by ID
    IMouseHandler* find_handler(const char* handler_id) const;

    // Event dispatch - calls handlers in priority order
    // Returns true if event was consumed by any handler
    bool dispatch_move(const MouseMoveEvent& event);
    bool dispatch_button(const MouseButtonEvent& event);
    bool dispatch_wheel(const MouseWheelEvent& event);

private:
    void sort_handlers();

    IMouseHandler* handlers_[MAX_MOUSE_HANDLERS];
    int handler_count_;
    bool needs_sort_;
};

//=============================================================================
// DefaultMouseDevice - Standard mouse input from platform
//=============================================================================

// Default mouse device that receives events from the platform layer.
// Each Window has one of these that receives events from the platform's
// message/event handling code.
class DefaultMouseDevice : public IMouseEventSource {
public:
    DefaultMouseDevice();
    ~DefaultMouseDevice() override;

    // IMouseEventSource interface
    const char* get_source_id() const override;
    bool is_active() const override;
    const MouseState& get_state() const override;

    // Set the dispatcher to forward events to
    void set_dispatcher(MouseEventDispatcher* dispatcher);

    // Set the owning window (for event construction)
    void set_window(Window* window);

    // Platform layer calls these to inject events
    // These update internal state and dispatch to the registered dispatcher
    void inject_move(int x, int y, KeyMod modifiers, double timestamp);
    void inject_button_down(MouseButton button, int x, int y, int clicks, KeyMod modifiers, double timestamp);
    void inject_button_up(MouseButton button, int x, int y, KeyMod modifiers, double timestamp);
    void inject_wheel(float dx, float dy, int x, int y, KeyMod modifiers, double timestamp);

    // State queries
    bool is_button_down(MouseButton button) const;
    void get_position(int* x, int* y) const;

    // Reset state (e.g., on focus loss)
    void reset();

private:
    MouseState state_;
    MouseEventDispatcher* dispatcher_;
    Window* window_;
    bool active_;
};

} // namespace input
} // namespace window

#endif // INPUT_MOUSE_HPP
