/*
 * input_gamepad.hpp - Cross-platform game controller/gamepad input
 *
 * This file contains platform-independent gamepad handling code including:
 * - GamepadButton and GamepadAxis enums
 * - Event structures for gamepad input
 * - IGamepadHandler interface for event handling
 * - GamepadManager for polling and state management
 *
 * Unlike mouse/keyboard which are window-specific, game controllers are
 * system-wide devices. GamepadManager is a standalone component that can
 * be used independently of windows.
 *
 * Platform implementations:
 * - Windows: XInput (gamepad_xinput.cpp)
 * - macOS/iOS: GCController (gamepad_gccontroller.mm)
 * - Linux: evdev (gamepad_evdev.cpp)
 * - Android: InputDevice (gamepad_android.cpp)
 */

#ifndef INPUT_GAMEPAD_HPP
#define INPUT_GAMEPAD_HPP

#include <cstdint>

namespace window {
namespace input {

//=============================================================================
// Constants
//=============================================================================

static const int MAX_GAMEPAD_BUTTONS = 16;
static const int MAX_GAMEPAD_AXES = 6;
static const int MAX_GAMEPADS = 8;
static const int MAX_GAMEPAD_HANDLERS = 16;
static const int MAX_GAMEPAD_NAME_LENGTH = 64;

//=============================================================================
// Gamepad Button Enumeration
//=============================================================================

// Standard gamepad button layout (Xbox-style naming with PlayStation equivalents)
enum class GamepadButton : uint8_t {
    A = 0,              // Cross (PlayStation)
    B,                  // Circle
    X,                  // Square
    Y,                  // Triangle
    LeftBumper,         // L1
    RightBumper,        // R1
    Back,               // Select/Share
    Start,              // Options
    Guide,              // Xbox/PS button
    LeftStick,          // L3 (left stick click)
    RightStick,         // R3 (right stick click)
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Count,
    Unknown = 255
};

//=============================================================================
// Gamepad Axis Enumeration
//=============================================================================

enum class GamepadAxis : uint8_t {
    LeftX = 0,          // -1.0 (left) to 1.0 (right)
    LeftY,              // -1.0 (up) to 1.0 (down)
    RightX,
    RightY,
    LeftTrigger,        // 0.0 to 1.0
    RightTrigger,       // 0.0 to 1.0
    Count,
    Unknown = 255
};

//=============================================================================
// Gamepad Event Types (add to EventType in window.hpp conceptually)
//=============================================================================

enum class GamepadEventType : uint8_t {
    Connected = 0,
    Disconnected,
    ButtonDown,
    ButtonUp,
    AxisMotion
};

//=============================================================================
// Gamepad Event Structures
//=============================================================================

// Base gamepad event
struct GamepadEvent {
    GamepadEventType type;
    int gamepad_index;          // Which controller (0 to MAX_GAMEPADS-1)
    double timestamp;           // Event timestamp in seconds
};

// Connection/disconnection event
struct GamepadConnectionEvent : GamepadEvent {
    const char* name;           // Controller name (may be null on disconnect)
    bool connected;             // true = connected, false = disconnected
};

// Button press/release event
struct GamepadButtonEvent : GamepadEvent {
    GamepadButton button;
};

// Axis motion event
struct GamepadAxisEvent : GamepadEvent {
    GamepadAxis axis;
    float value;                // Current normalized value
    float delta;                // Change from previous value
};

//=============================================================================
// Gamepad State
//=============================================================================

struct GamepadState {
    bool buttons[MAX_GAMEPAD_BUTTONS] = {};
    float axes[MAX_GAMEPAD_AXES] = {};
    bool connected = false;
    char name[MAX_GAMEPAD_NAME_LENGTH] = {};

    // Check if any button is pressed
    bool any_button_down() const;

    // Reset all state
    void reset();
};

//=============================================================================
// IGamepadHandler - Event handler interface
//=============================================================================

// Interface for objects that handle gamepad events.
// Handlers are called in priority order (highest first).
// Return true from button/axis handlers to consume the event and stop propagation.
class IGamepadHandler {
public:
    virtual ~IGamepadHandler() = default;

    // Get unique identifier for this handler
    virtual const char* get_handler_id() const = 0;

    // Get priority (higher values = called first, default = 0)
    virtual int get_priority() const { return 0; }

    // Button event handler (both press and release)
    // Return true to consume the event (stop propagation to lower priority handlers)
    virtual bool on_button(const GamepadButtonEvent& event) { (void)event; return false; }

    // Axis motion event handler
    // Return true to consume the event
    virtual bool on_axis(const GamepadAxisEvent& event) { (void)event; return false; }

    // Connection event handler (does not consume)
    virtual void on_connection(const GamepadConnectionEvent& event) { (void)event; }
};

//=============================================================================
// GamepadEventDispatcher - Manages handlers and dispatches events
//=============================================================================

class GamepadEventDispatcher {
public:
    GamepadEventDispatcher();
    ~GamepadEventDispatcher();

    // Handler management
    bool add_handler(IGamepadHandler* handler);
    bool remove_handler(IGamepadHandler* handler);
    bool remove_handler(const char* handler_id);

    int get_handler_count() const;
    IGamepadHandler* get_handler(int index) const;
    IGamepadHandler* find_handler(const char* handler_id) const;

    // Event dispatch - calls handlers in priority order
    bool dispatch_button(const GamepadButtonEvent& event);
    bool dispatch_axis(const GamepadAxisEvent& event);
    void dispatch_connection(const GamepadConnectionEvent& event);

private:
    void sort_handlers();

    IGamepadHandler* handlers_[MAX_GAMEPAD_HANDLERS];
    int handler_count_;
    bool needs_sort_;
};

//=============================================================================
// GamepadManager - Standalone gamepad management
//=============================================================================

// GamepadManager is a standalone component that manages game controller input.
// Unlike mouse/keyboard which are window-specific, gamepads are system-wide.
//
// Usage:
//   auto* mgr = GamepadManager::create();
//   mgr->add_handler(&my_handler);
//   while (running) {
//       mgr->update();  // Poll controllers, dispatch events
//       // ... or query state directly
//       if (mgr->is_button_down(0, GamepadButton::A)) { ... }
//   }
//   mgr->destroy();
class GamepadManager {
public:
    // Create a new GamepadManager instance
    // Returns nullptr on failure
    static GamepadManager* create();

    // Destroy this manager instance
    void destroy();

    // Poll controllers and dispatch events (call once per frame)
    void update();

    // Handler management
    bool add_handler(IGamepadHandler* handler);
    bool remove_handler(IGamepadHandler* handler);
    bool remove_handler(const char* handler_id);

    // Get the event dispatcher
    GamepadEventDispatcher* get_dispatcher();

    // State queries
    int get_gamepad_count() const;
    bool is_connected(int index) const;
    const GamepadState* get_state(int index) const;
    bool is_button_down(int index, GamepadButton button) const;
    float get_axis(int index, GamepadAxis axis) const;

    // Configuration
    void set_deadzone(float deadzone);
    float get_deadzone() const;

    // Platform-specific implementation
    struct Impl;

private:
    GamepadManager();
    ~GamepadManager();
    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    Impl* impl_;
};

//=============================================================================
// Utility Functions
//=============================================================================

// Convert GamepadButton to human-readable string
const char* gamepad_button_to_string(GamepadButton button);

// Convert string to GamepadButton (case-insensitive)
GamepadButton string_to_gamepad_button(const char* str);

// Get button index for array indexing, -1 if invalid
int gamepad_button_to_index(GamepadButton button);

// Get GamepadButton from index
GamepadButton index_to_gamepad_button(int index);

// Convert GamepadAxis to human-readable string
const char* gamepad_axis_to_string(GamepadAxis axis);

// Convert string to GamepadAxis (case-insensitive)
GamepadAxis string_to_gamepad_axis(const char* str);

// Get axis index for array indexing, -1 if invalid
int gamepad_axis_to_index(GamepadAxis axis);

// Get GamepadAxis from index
GamepadAxis index_to_gamepad_axis(int index);

// Convert GamepadEventType to string
const char* gamepad_event_type_to_string(GamepadEventType type);

} // namespace input
} // namespace window

#endif // INPUT_GAMEPAD_HPP
