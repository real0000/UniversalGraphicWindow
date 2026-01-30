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
static const int MAX_FORCE_FEEDBACK_EFFECTS = 8;

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
// Force Feedback / Vibration
//=============================================================================

// Force feedback effect types (for advanced DirectInput-style effects)
enum class ForceFeedbackType : uint8_t {
    None = 0,
    Rumble,             // Basic dual-motor rumble (XInput style)
    Constant,           // Constant force in a direction
    Periodic,           // Periodic wave (sine, square, etc.)
    Ramp,               // Force that changes linearly over time
    Spring,             // Position-based resistance
    Damper,             // Velocity-based resistance
    Inertia,            // Acceleration-based resistance
    Friction,           // Movement resistance
    Custom,             // Custom/raw effect
    Count
};

// Periodic effect waveforms
enum class ForceFeedbackWaveform : uint8_t {
    Sine = 0,
    Square,
    Triangle,
    SawtoothUp,
    SawtoothDown,
    Count
};

// Force feedback capabilities for a gamepad
struct ForceFeedbackCaps {
    bool supported = false;             // Has any force feedback capability
    bool has_rumble = false;            // Has basic rumble motors
    bool has_left_motor = false;        // Has left (low frequency) motor
    bool has_right_motor = false;       // Has right (high frequency) motor
    bool has_trigger_rumble = false;    // Has trigger rumble (Xbox One+)
    bool has_advanced_effects = false;  // Has DirectInput-style effects
    uint32_t supported_effects = 0;     // Bitmask of ForceFeedbackType (1 << type)
    int max_simultaneous_effects = 0;   // Max effects that can play at once
};

// Parameters for a force feedback effect
struct ForceFeedbackEffect {
    ForceFeedbackType type = ForceFeedbackType::Rumble;

    // Duration in milliseconds (0 = infinite until stopped)
    uint32_t duration_ms = 0;

    // Start delay in milliseconds
    uint32_t start_delay_ms = 0;

    // Gain/intensity (0.0 to 1.0)
    float gain = 1.0f;

    // For Rumble type:
    float left_motor = 0.0f;        // Low frequency motor (0.0 to 1.0)
    float right_motor = 0.0f;       // High frequency motor (0.0 to 1.0)
    float left_trigger = 0.0f;      // Left trigger motor (0.0 to 1.0, Xbox One+)
    float right_trigger = 0.0f;     // Right trigger motor (0.0 to 1.0, Xbox One+)

    // For Constant/Ramp types:
    float magnitude = 0.0f;         // Force magnitude (-1.0 to 1.0)
    float end_magnitude = 0.0f;     // End magnitude for Ramp
    int direction = 0;              // Direction in degrees (0-359, 0=up/forward)

    // For Periodic type:
    ForceFeedbackWaveform waveform = ForceFeedbackWaveform::Sine;
    float period_ms = 100.0f;       // Wave period in milliseconds
    float phase = 0.0f;             // Starting phase (0.0 to 1.0)
    float offset = 0.0f;            // DC offset (-1.0 to 1.0)

    // For condition effects (Spring, Damper, Inertia, Friction):
    float positive_coefficient = 0.0f;  // Coefficient for positive displacement
    float negative_coefficient = 0.0f;  // Coefficient for negative displacement
    float positive_saturation = 1.0f;   // Maximum force in positive direction
    float negative_saturation = 1.0f;   // Maximum force in negative direction
    float dead_band = 0.0f;             // Center dead zone size

    // Envelope (attack/fade)
    uint32_t attack_time_ms = 0;    // Attack time in ms
    float attack_level = 0.0f;      // Attack level (0.0 to 1.0)
    uint32_t fade_time_ms = 0;      // Fade time in ms
    float fade_level = 0.0f;        // Fade level (0.0 to 1.0)
};

// Handle for a playing effect (used to stop/modify specific effects)
typedef int ForceFeedbackHandle;
static const ForceFeedbackHandle INVALID_FF_HANDLE = -1;

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

    //-------------------------------------------------------------------------
    // Force Feedback / Vibration
    //-------------------------------------------------------------------------

    // Query force feedback capabilities for a gamepad
    bool get_force_feedback_caps(int index, ForceFeedbackCaps* caps) const;

    // Check if force feedback is supported
    bool supports_force_feedback(int index) const;

    // Simple vibration API (most common use case)
    // left_motor: Low frequency rumble (0.0 to 1.0)
    // right_motor: High frequency rumble (0.0 to 1.0)
    // Returns true on success
    bool set_vibration(int index, float left_motor, float right_motor);

    // Trigger vibration (Xbox One controllers and later)
    // Returns true on success, false if not supported
    bool set_trigger_vibration(int index, float left_trigger, float right_trigger);

    // Stop all vibration on a gamepad
    bool stop_vibration(int index);

    // Advanced force feedback API
    // Play an effect and get a handle to control it
    // Returns INVALID_FF_HANDLE on failure
    ForceFeedbackHandle play_effect(int index, const ForceFeedbackEffect& effect);

    // Stop a specific effect
    bool stop_effect(int index, ForceFeedbackHandle handle);

    // Modify a playing effect
    bool update_effect(int index, ForceFeedbackHandle handle, const ForceFeedbackEffect& effect);

    // Stop all effects on a gamepad
    bool stop_all_effects(int index);

    // Pause/resume all effects on a gamepad
    bool pause_effects(int index);
    bool resume_effects(int index);

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
