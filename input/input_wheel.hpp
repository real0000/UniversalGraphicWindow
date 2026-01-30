/*
 * input_wheel.hpp - Cross-platform steering wheel input with force feedback
 *
 * Steering wheels are specialized game controllers used for racing games.
 * They have different characteristics from gamepads:
 * - Large rotation range (270° to 1080°)
 * - Pedal axes (throttle, brake, clutch)
 * - Force feedback is crucial for immersion
 * - Additional inputs (shifters, paddle shifters, buttons)
 *
 * Platform implementations:
 * - Windows: DirectInput (wheel_dinput.cpp) - required for proper FF
 * - Other platforms: Stubs for now
 */

#ifndef INPUT_WHEEL_HPP
#define INPUT_WHEEL_HPP

#include <cstdint>

namespace window {
namespace input {

//=============================================================================
// Constants
//=============================================================================

static const int MAX_WHEELS = 4;
static const int MAX_WHEEL_BUTTONS = 32;
static const int MAX_WHEEL_HANDLERS = 8;
static const int MAX_WHEEL_NAME_LENGTH = 128;
static const int MAX_WHEEL_FF_EFFECTS = 16;

//=============================================================================
// Wheel Axis Enumeration
//=============================================================================

enum class WheelAxis : uint8_t {
    Steering = 0,       // -1.0 (full left) to 1.0 (full right)
    Throttle,           // 0.0 (released) to 1.0 (fully pressed)
    Brake,              // 0.0 to 1.0
    Clutch,             // 0.0 to 1.0
    Handbrake,          // 0.0 to 1.0
    Count,
    Unknown = 255
};

//=============================================================================
// Wheel Button Enumeration
//=============================================================================

enum class WheelButton : uint8_t {
    // Paddle shifters
    PaddleShiftUp = 0,
    PaddleShiftDown,

    // D-Pad
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,

    // Common wheel buttons
    Button1,
    Button2,
    Button3,
    Button4,
    Button5,
    Button6,
    Button7,
    Button8,
    Button9,
    Button10,
    Button11,
    Button12,
    Button13,
    Button14,
    Button15,
    Button16,

    // Special buttons
    Start,
    Back,
    Xbox,       // Guide/Home button

    Count,
    Unknown = 255
};

//=============================================================================
// Gear Position (for H-pattern shifters)
//=============================================================================

enum class GearPosition : int8_t {
    Reverse = -1,
    Neutral = 0,
    Gear1 = 1,
    Gear2 = 2,
    Gear3 = 3,
    Gear4 = 4,
    Gear5 = 5,
    Gear6 = 6,
    Gear7 = 7,
    Unknown = 127
};

//=============================================================================
// Wheel Force Feedback Effect Types
//=============================================================================

enum class WheelFFType : uint8_t {
    None = 0,

    // Basic effects
    ConstantForce,      // Constant force in one direction (steering resistance)
    SpringForce,        // Self-centering spring
    DamperForce,        // Velocity-based resistance (hydraulic feel)
    FrictionForce,      // Static/kinetic friction
    InertiaForce,       // Mass/inertia simulation

    // Periodic effects
    SineWave,           // Smooth oscillation (engine vibration)
    SquareWave,         // Sharp oscillation
    TriangleWave,       // Linear oscillation
    SawtoothWave,       // Asymmetric oscillation

    // Game-specific effects
    RoadRumble,         // Road surface texture
    Collision,          // Impact/crash effect
    SlipperyRoad,       // Reduced grip feel
    DirtRoad,           // Loose surface rumble
    Kerb,               // Kerb/rumble strip effect

    Count
};

//=============================================================================
// Wheel Capabilities
//=============================================================================

struct WheelCaps {
    // Physical characteristics
    float rotation_degrees = 900.0f;    // Total rotation range (e.g., 900°)
    float min_rotation = -450.0f;       // Minimum angle in degrees
    float max_rotation = 450.0f;        // Maximum angle in degrees

    // Pedals
    bool has_throttle = true;
    bool has_brake = true;
    bool has_clutch = false;
    bool has_handbrake = false;
    bool combined_pedals = false;       // Throttle/brake on same axis

    // Shifter
    bool has_paddle_shifters = false;
    bool has_h_shifter = false;
    int h_shifter_gears = 0;            // Number of gears (e.g., 6)
    bool has_sequential_shifter = false;

    // Force feedback
    bool has_force_feedback = false;
    uint32_t supported_ff_effects = 0;  // Bitmask of WheelFFType
    int max_ff_effects = 0;
    float max_ff_torque_nm = 0.0f;      // Maximum torque in Newton-meters (if known)

    // General
    int num_buttons = 0;
    int num_axes = 0;
};

//=============================================================================
// Wheel State
//=============================================================================

struct WheelState {
    // Axes (normalized)
    float steering = 0.0f;          // -1.0 (left) to 1.0 (right)
    float steering_degrees = 0.0f;  // Actual angle in degrees
    float throttle = 0.0f;          // 0.0 to 1.0
    float brake = 0.0f;             // 0.0 to 1.0
    float clutch = 0.0f;            // 0.0 to 1.0
    float handbrake = 0.0f;         // 0.0 to 1.0

    // Shifter
    GearPosition gear = GearPosition::Neutral;

    // Buttons
    bool buttons[MAX_WHEEL_BUTTONS] = {};

    // Connection status
    bool connected = false;
    char name[MAX_WHEEL_NAME_LENGTH] = {};

    void reset() {
        steering = 0.0f;
        steering_degrees = 0.0f;
        throttle = 0.0f;
        brake = 0.0f;
        clutch = 0.0f;
        handbrake = 0.0f;
        gear = GearPosition::Neutral;
        for (int i = 0; i < MAX_WHEEL_BUTTONS; i++) {
            buttons[i] = false;
        }
        connected = false;
        name[0] = '\0';
    }
};

//=============================================================================
// Wheel Force Feedback Effect Parameters
//=============================================================================

struct WheelFFEffect {
    WheelFFType type = WheelFFType::None;

    // Duration (0 = infinite)
    uint32_t duration_ms = 0;
    uint32_t start_delay_ms = 0;

    // Overall gain (0.0 to 1.0)
    float gain = 1.0f;

    //-------------------------------------------------------------------------
    // ConstantForce parameters
    //-------------------------------------------------------------------------
    float constant_force = 0.0f;    // -1.0 (full left) to 1.0 (full right)

    //-------------------------------------------------------------------------
    // Spring/Damper/Friction/Inertia parameters
    //-------------------------------------------------------------------------
    float coefficient = 0.5f;       // Effect strength (0.0 to 1.0)
    float saturation = 1.0f;        // Maximum force limit
    float deadband = 0.0f;          // Center deadband (0.0 to 1.0)
    float center_point = 0.0f;      // Spring center (-1.0 to 1.0)

    //-------------------------------------------------------------------------
    // Periodic effect parameters
    //-------------------------------------------------------------------------
    float magnitude = 0.5f;         // Wave amplitude (0.0 to 1.0)
    float frequency_hz = 20.0f;     // Wave frequency in Hz
    float phase = 0.0f;             // Starting phase (0.0 to 1.0)
    float offset = 0.0f;            // DC offset (-1.0 to 1.0)

    //-------------------------------------------------------------------------
    // Envelope (attack/sustain/fade)
    //-------------------------------------------------------------------------
    uint32_t attack_time_ms = 0;
    float attack_level = 0.0f;
    uint32_t fade_time_ms = 0;
    float fade_level = 0.0f;
};

// Handle for managing effects
typedef int WheelFFHandle;
static const WheelFFHandle INVALID_WHEEL_FF_HANDLE = -1;

//=============================================================================
// Wheel Event Types
//=============================================================================

enum class WheelEventType : uint8_t {
    Connected = 0,
    Disconnected,
    ButtonDown,
    ButtonUp,
    AxisChanged,
    GearChanged
};

//=============================================================================
// Wheel Event Structures
//=============================================================================

struct WheelEvent {
    WheelEventType type;
    int wheel_index;
    double timestamp;
};

struct WheelConnectionEvent : WheelEvent {
    const char* name;
    bool connected;
};

struct WheelButtonEvent : WheelEvent {
    WheelButton button;
};

struct WheelAxisEvent : WheelEvent {
    WheelAxis axis;
    float value;
    float delta;
};

struct WheelGearEvent : WheelEvent {
    GearPosition gear;
    GearPosition previous_gear;
};

//=============================================================================
// IWheelHandler - Event handler interface
//=============================================================================

class IWheelHandler {
public:
    virtual ~IWheelHandler() = default;

    virtual const char* get_handler_id() const = 0;
    virtual int get_priority() const { return 0; }

    virtual bool on_button(const WheelButtonEvent& event) { (void)event; return false; }
    virtual bool on_axis(const WheelAxisEvent& event) { (void)event; return false; }
    virtual bool on_gear(const WheelGearEvent& event) { (void)event; return false; }
    virtual void on_connection(const WheelConnectionEvent& event) { (void)event; }
};

//=============================================================================
// WheelEventDispatcher
//=============================================================================

class WheelEventDispatcher {
public:
    WheelEventDispatcher();
    ~WheelEventDispatcher();

    bool add_handler(IWheelHandler* handler);
    bool remove_handler(IWheelHandler* handler);
    bool remove_handler(const char* handler_id);

    int get_handler_count() const;
    IWheelHandler* get_handler(int index) const;
    IWheelHandler* find_handler(const char* handler_id) const;

    bool dispatch_button(const WheelButtonEvent& event);
    bool dispatch_axis(const WheelAxisEvent& event);
    bool dispatch_gear(const WheelGearEvent& event);
    void dispatch_connection(const WheelConnectionEvent& event);

private:
    void sort_handlers();

    IWheelHandler* handlers_[MAX_WHEEL_HANDLERS];
    int handler_count_;
    bool needs_sort_;
};

//=============================================================================
// WheelManager - Steering wheel management
//=============================================================================

class WheelManager {
public:
    // Create/destroy
    static WheelManager* create();
    void destroy();

    // Update (call once per frame)
    void update();

    // Handler management
    bool add_handler(IWheelHandler* handler);
    bool remove_handler(IWheelHandler* handler);
    bool remove_handler(const char* handler_id);
    WheelEventDispatcher* get_dispatcher();

    // State queries
    int get_wheel_count() const;
    bool is_connected(int index) const;
    const WheelState* get_state(int index) const;
    bool get_capabilities(int index, WheelCaps* caps) const;

    // Axis queries
    float get_steering(int index) const;
    float get_steering_degrees(int index) const;
    float get_throttle(int index) const;
    float get_brake(int index) const;
    float get_clutch(int index) const;
    GearPosition get_gear(int index) const;
    bool is_button_down(int index, WheelButton button) const;

    // Configuration
    void set_rotation_range(int index, float degrees);  // Software rotation limit
    float get_rotation_range(int index) const;
    void set_deadzone(float deadzone);
    float get_deadzone() const;
    void set_linearity(int index, float linearity);     // 1.0 = linear, <1.0 = more sensitive center

    //-------------------------------------------------------------------------
    // Force Feedback
    //-------------------------------------------------------------------------

    // Query FF capabilities
    bool supports_force_feedback(int index) const;
    bool get_ff_capabilities(int index, WheelCaps* caps) const;

    // Simple force feedback
    // force: -1.0 (full left) to 1.0 (full right)
    bool set_constant_force(int index, float force);

    // Spring effect (self-centering)
    // strength: 0.0 to 1.0
    // center: -1.0 to 1.0 (center point of spring)
    bool set_spring_force(int index, float strength, float center = 0.0f);

    // Damper effect (resistance to movement)
    bool set_damper_force(int index, float strength);

    // Friction effect
    bool set_friction_force(int index, float strength);

    // Periodic effects (vibration)
    bool set_sine_effect(int index, float magnitude, float frequency_hz);

    // Stop all force feedback
    bool stop_all_forces(int index);

    // Advanced force feedback
    WheelFFHandle play_effect(int index, const WheelFFEffect& effect);
    bool stop_effect(int index, WheelFFHandle handle);
    bool update_effect(int index, WheelFFHandle handle, const WheelFFEffect& effect);

    // Global FF control
    bool set_ff_gain(int index, float gain);    // Master gain (0.0 to 1.0)
    float get_ff_gain(int index) const;
    bool set_ff_autocenter(int index, bool enabled, float strength = 0.5f);
    bool pause_ff(int index);
    bool resume_ff(int index);

    // Platform implementation
    struct Impl;

private:
    WheelManager();
    ~WheelManager();
    WheelManager(const WheelManager&) = delete;
    WheelManager& operator=(const WheelManager&) = delete;

    Impl* impl_;
};

//=============================================================================
// Utility Functions
//=============================================================================

const char* wheel_axis_to_string(WheelAxis axis);
WheelAxis string_to_wheel_axis(const char* str);

const char* wheel_button_to_string(WheelButton button);
WheelButton string_to_wheel_button(const char* str);

const char* gear_position_to_string(GearPosition gear);

const char* wheel_ff_type_to_string(WheelFFType type);

const char* wheel_event_type_to_string(WheelEventType type);

} // namespace input
} // namespace window

#endif // INPUT_WHEEL_HPP
