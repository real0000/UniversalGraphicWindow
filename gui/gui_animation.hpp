/*
 * gui_animation.hpp - GUI Animation Interface
 *
 * Contains animation system for animating widget properties.
 */

#ifndef WINDOW_GUI_ANIMATION_HPP
#define WINDOW_GUI_ANIMATION_HPP

namespace window {
namespace gui {

// ============================================================================
// Animation Enums
// ============================================================================

enum class AnimationEasing : uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInElastic,
    EaseOutElastic,
    EaseInOutElastic,
    EaseInBounce,
    EaseOutBounce,
    EaseInOutBounce
};

enum class AnimationState : uint8_t {
    Idle = 0,
    Playing,
    Paused,
    Completed
};

enum class AnimationTarget : uint8_t {
    PositionX = 0,
    PositionY,
    Position,       // Both X and Y
    Width,
    Height,
    Size,           // Both width and height
    Opacity,
    ColorR,
    ColorG,
    ColorB,
    ColorA,
    Color,          // All RGBA
    Rotation,
    ScaleX,
    ScaleY,
    Scale           // Both X and Y
};

enum class AnimationLoop : uint8_t {
    None = 0,       // Play once
    Loop,           // Repeat from start
    PingPong        // Reverse direction each cycle
};

// ============================================================================
// Animation Keyframe
// ============================================================================

struct AnimationKeyframe {
    float time = 0.0f;              // Time in seconds from animation start
    math::Vec4 value;               // Value at this keyframe (use x for single values)
    AnimationEasing easing = AnimationEasing::Linear;
};

// ============================================================================
// Animation Event Handler
// ============================================================================

class IAnimationEventHandler {
public:
    virtual ~IAnimationEventHandler() = default;
    virtual void on_animation_started(int animation_id) = 0;
    virtual void on_animation_completed(int animation_id) = 0;
    virtual void on_animation_looped(int animation_id, int loop_count) = 0;
    virtual void on_animation_paused(int animation_id) = 0;
    virtual void on_animation_resumed(int animation_id) = 0;
};

// ============================================================================
// Animation Interface
// ============================================================================

class IGuiAnimation {
public:
    virtual ~IGuiAnimation() = default;

    // Identification
    virtual int get_id() const = 0;
    virtual const char* get_name() const = 0;
    virtual void set_name(const char* name) = 0;

    // Target widget
    virtual IGuiWidget* get_target() const = 0;
    virtual void set_target(IGuiWidget* widget) = 0;

    // What property to animate
    virtual AnimationTarget get_target_property() const = 0;
    virtual void set_target_property(AnimationTarget target) = 0;

    // Simple animation (from current value to end value)
    virtual void animate_to(const math::Vec4& end_value, float duration) = 0;
    virtual void animate_from_to(const math::Vec4& start_value, const math::Vec4& end_value, float duration) = 0;

    // Keyframe animation
    virtual void clear_keyframes() = 0;
    virtual void add_keyframe(const AnimationKeyframe& keyframe) = 0;
    virtual int get_keyframe_count() const = 0;
    virtual const AnimationKeyframe* get_keyframe(int index) const = 0;

    // Timing
    virtual float get_duration() const = 0;
    virtual void set_duration(float duration) = 0;
    virtual float get_delay() const = 0;
    virtual void set_delay(float delay) = 0;

    // Easing (for simple animations without keyframes)
    virtual AnimationEasing get_easing() const = 0;
    virtual void set_easing(AnimationEasing easing) = 0;

    // Looping
    virtual AnimationLoop get_loop_mode() const = 0;
    virtual void set_loop_mode(AnimationLoop mode) = 0;
    virtual int get_loop_count() const = 0;         // 0 = infinite
    virtual void set_loop_count(int count) = 0;
    virtual int get_current_loop() const = 0;

    // Playback control
    virtual void start() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    // State
    virtual AnimationState get_state() const = 0;
    virtual float get_current_time() const = 0;
    virtual float get_progress() const = 0;         // 0.0 - 1.0
    virtual math::Vec4 get_current_value() const = 0;

    // Speed
    virtual float get_speed() const = 0;
    virtual void set_speed(float speed) = 0;        // 1.0 = normal, 2.0 = double speed

    // Auto-destroy when completed
    virtual bool is_auto_destroy() const = 0;
    virtual void set_auto_destroy(bool auto_destroy) = 0;

    // Event handler
    virtual void set_animation_event_handler(IAnimationEventHandler* handler) = 0;
};

// ============================================================================
// Animation Manager Interface
// ============================================================================

class IGuiAnimationManager {
public:
    virtual ~IGuiAnimationManager() = default;

    // Update all animations (call each frame with delta time)
    virtual void update(float delta_time) = 0;

    // Create animations
    virtual IGuiAnimation* create_animation() = 0;
    virtual void destroy_animation(IGuiAnimation* animation) = 0;
    virtual void destroy_animation(int animation_id) = 0;

    // Find animations
    virtual IGuiAnimation* get_animation(int animation_id) const = 0;
    virtual IGuiAnimation* get_animation(const char* name) const = 0;
    virtual int get_animations_for_widget(IGuiWidget* widget, IGuiAnimation** out_animations, int max_count) const = 0;

    // Bulk operations
    virtual void pause_all() = 0;
    virtual void resume_all() = 0;
    virtual void stop_all() = 0;
    virtual void stop_animations_for_widget(IGuiWidget* widget) = 0;

    // Animation count
    virtual int get_animation_count() const = 0;
    virtual int get_active_animation_count() const = 0;

    // Global speed multiplier
    virtual float get_global_speed() const = 0;
    virtual void set_global_speed(float speed) = 0;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* animation_easing_to_string(AnimationEasing easing);
const char* animation_state_to_string(AnimationState state);
const char* animation_target_to_string(AnimationTarget target);
const char* animation_loop_to_string(AnimationLoop loop);

// ============================================================================
// Easing Functions (for implementation use)
// ============================================================================

float apply_easing(AnimationEasing easing, float t);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_ANIMATION_HPP
