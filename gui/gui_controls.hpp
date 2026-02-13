/*
 * gui_controls.hpp - Button, Slider, ProgressBar, ColorPicker, and Image Interfaces
 *
 * Contains control widgets for value editing and display.
 */

#ifndef WINDOW_GUI_CONTROLS_HPP
#define WINDOW_GUI_CONTROLS_HPP

namespace window {
namespace gui {

// ============================================================================
// Button Interface - Clickable button control
// ============================================================================

enum class ButtonType : uint8_t {
    Normal = 0,     // Standard push button
    Toggle,         // Toggle on/off button
    Radio,          // Radio button (mutually exclusive)
    Checkbox        // Checkbox button
};

struct ButtonStyle {
    math::Vec4 background_color;
    math::Vec4 hover_color;
    math::Vec4 pressed_color;
    math::Vec4 disabled_color;
    math::Vec4 checked_color;           // For toggle/checkbox/radio when checked
    math::Vec4 text_color;
    math::Vec4 text_disabled_color;
    math::Vec4 border_color;
    math::Vec4 focus_border_color;
    float border_width = 1.0f;
    float corner_radius = 4.0f;
    float padding = 8.0f;
    float icon_size = 16.0f;
    float icon_text_spacing = 6.0f;
    float font_size = 13.0f;

    static ButtonStyle default_style() {
        ButtonStyle s;
        s.background_color = color_rgba8(60, 60, 60);
        s.hover_color = color_rgba8(70, 70, 70);
        s.pressed_color = color_rgba8(50, 50, 50);
        s.disabled_color = color_rgba8(45, 45, 48);
        s.checked_color = color_rgba8(0, 122, 204);
        s.text_color = color_rgba8(241, 241, 241);
        s.text_disabled_color = color_rgba8(110, 110, 110);
        s.border_color = color_rgba8(80, 80, 80);
        s.focus_border_color = color_rgba8(0, 122, 204);
        return s;
    }
};

class IButtonEventHandler {
public:
    virtual ~IButtonEventHandler() = default;
    virtual void on_clicked() = 0;
    virtual void on_toggled(bool checked) = 0;
};

class IGuiButton : public IGuiWidget {
public:
    virtual ~IGuiButton() = default;

    // Button type
    virtual ButtonType get_button_type() const = 0;
    virtual void set_button_type(ButtonType type) = 0;

    // Text
    virtual const char* get_text() const = 0;
    virtual void set_text(const char* text) = 0;

    // Icon
    virtual const char* get_icon() const = 0;
    virtual void set_icon(const char* icon_name) = 0;

    // Checked state (for Toggle, Radio, Checkbox types)
    virtual bool is_checked() const = 0;
    virtual void set_checked(bool checked) = 0;

    // Radio group (for Radio type)
    virtual int get_radio_group() const = 0;
    virtual void set_radio_group(int group_id) = 0;

    // Style
    virtual const ButtonStyle& get_button_style() const = 0;
    virtual void set_button_style(const ButtonStyle& style) = 0;

    // Event handler
    virtual void set_button_event_handler(IButtonEventHandler* handler) = 0;
};

// ============================================================================
// Slider Interface - Value slider control
// ============================================================================

enum class SliderOrientation : uint8_t {
    Horizontal = 0,
    Vertical
};

struct SliderStyle {
    math::Vec4 track_color;
    math::Vec4 track_fill_color;
    math::Vec4 thumb_color;
    math::Vec4 thumb_hover_color;
    math::Vec4 thumb_pressed_color;
    math::Vec4 tick_color;
    float track_height = 4.0f;
    float thumb_radius = 7.0f;
    float tick_length = 6.0f;
    float tick_width = 1.0f;
    float track_corner_radius = 2.0f;

    static SliderStyle default_style() {
        SliderStyle s;
        s.track_color = color_rgba8(63, 63, 70);
        s.track_fill_color = color_rgba8(0, 122, 204);
        s.thumb_color = color_rgba8(200, 200, 200);
        s.thumb_hover_color = color_rgba8(0, 122, 204);
        s.thumb_pressed_color = color_rgba8(0, 100, 180);
        s.tick_color = color_rgba8(110, 110, 110);
        return s;
    }
};

struct SliderRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box track_rect;
    math::Box track_fill_rect;      // Filled portion of track
    math::Vec2 thumb_center;
    float thumb_radius = 7.0f;

    SliderStyle style;
    SliderOrientation orientation = SliderOrientation::Horizontal;
    WidgetState thumb_state = WidgetState::Normal;
    float value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    float normalized = 0.0f;        // 0.0 - 1.0
    bool show_ticks = false;
    int tick_count = 0;
};

class ISliderEventHandler {
public:
    virtual ~ISliderEventHandler() = default;
    virtual void on_value_changed(float value) = 0;
    virtual void on_drag_started() = 0;
    virtual void on_drag_ended() = 0;
};

class IGuiSlider : public IGuiWidget {
public:
    virtual ~IGuiSlider() = default;

    // Orientation
    virtual SliderOrientation get_orientation() const = 0;
    virtual void set_orientation(SliderOrientation orientation) = 0;

    // Value
    virtual float get_value() const = 0;
    virtual void set_value(float value) = 0;

    // Range
    virtual float get_min_value() const = 0;
    virtual float get_max_value() const = 0;
    virtual void set_range(float min_value, float max_value) = 0;

    // Step (0 = continuous)
    virtual float get_step() const = 0;
    virtual void set_step(float step) = 0;

    // Tick marks
    virtual bool is_ticks_visible() const = 0;
    virtual void set_ticks_visible(bool visible) = 0;
    virtual float get_tick_interval() const = 0;
    virtual void set_tick_interval(float interval) = 0;

    // Thumb state
    virtual bool is_thumb_hovered() const = 0;
    virtual bool is_thumb_pressed() const = 0;

    // Style
    virtual const SliderStyle& get_slider_style() const = 0;
    virtual void set_slider_style(const SliderStyle& style) = 0;

    // Event handler
    virtual void set_slider_event_handler(ISliderEventHandler* handler) = 0;

    // Render info
    virtual void get_slider_render_info(SliderRenderInfo* out_info) const = 0;
};

// ============================================================================
// ProgressBar Interface - Progress indicator
// ============================================================================

enum class ProgressBarMode : uint8_t {
    Determinate = 0,    // Known progress 0.0 - 1.0
    Indeterminate       // Unknown progress (animated)
};

struct ProgressBarStyle {
    math::Vec4 track_color;
    math::Vec4 fill_color;
    math::Vec4 indeterminate_color;
    math::Vec4 text_color;
    float height = 20.0f;
    float corner_radius = 4.0f;
    float indeterminate_width = 0.3f;   // Width of indeterminate bar as ratio

    static ProgressBarStyle default_style() {
        ProgressBarStyle s;
        s.track_color = color_rgba8(63, 63, 70);
        s.fill_color = color_rgba8(0, 122, 204);
        s.indeterminate_color = color_rgba8(0, 122, 204);
        s.text_color = color_rgba8(241, 241, 241);
        return s;
    }
};

struct ProgressBarRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box track_rect;
    math::Box fill_rect;

    ProgressBarStyle style;
    ProgressBarMode mode = ProgressBarMode::Determinate;
    float value = 0.0f;            // 0.0 - 1.0
    float animation_phase = 0.0f;  // 0.0 - 1.0 for indeterminate animation
    bool show_text = false;
    const char* text = nullptr;    // Display text (e.g. "45%")
};

class IGuiProgressBar : public IGuiWidget {
public:
    virtual ~IGuiProgressBar() = default;

    // Mode
    virtual ProgressBarMode get_mode() const = 0;
    virtual void set_mode(ProgressBarMode mode) = 0;

    // Value (0.0 - 1.0 for determinate mode)
    virtual float get_value() const = 0;
    virtual void set_value(float value) = 0;

    // Text display
    virtual bool is_text_visible() const = 0;
    virtual void set_text_visible(bool visible) = 0;
    virtual const char* get_text() const = 0;
    virtual void set_text(const char* text) = 0;

    // Style
    virtual const ProgressBarStyle& get_progress_bar_style() const = 0;
    virtual void set_progress_bar_style(const ProgressBarStyle& style) = 0;

    // Render info
    virtual void get_progress_bar_render_info(ProgressBarRenderInfo* out_info) const = 0;
};

// ============================================================================
// ColorPicker Interface - Color selection control
// ============================================================================

enum class ColorPickerMode : uint8_t {
    HSVSquare = 0,      // Hue ring + SV square
    HSVWheel,           // Hue ring + SV triangle
    RGBSliders,         // R, G, B sliders
    HSLSliders,         // H, S, L sliders
    Palette             // Predefined color swatches
};

struct ColorPickerStyle {
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 label_color;
    math::Vec4 input_background;
    math::Vec4 input_text_color;
    math::Vec4 swatch_border_color;
    math::Vec4 selector_color;          // Ring/crosshair on color area
    float wheel_outer_radius = 100.0f;
    float wheel_inner_radius = 80.0f;
    float sv_square_size = 140.0f;
    float slider_height = 18.0f;
    float swatch_size = 20.0f;
    float swatch_spacing = 4.0f;
    float alpha_checker_size = 6.0f;
    float selector_radius = 5.0f;
    float preview_height = 30.0f;
    float font_size = 12.0f;
    float padding = 8.0f;

    static ColorPickerStyle default_style() {
        ColorPickerStyle s;
        s.background_color = color_rgba8(37, 37, 38);
        s.border_color = color_rgba8(63, 63, 70);
        s.label_color = color_rgba8(180, 180, 180);
        s.input_background = color_rgba8(30, 30, 30);
        s.input_text_color = color_rgba8(241, 241, 241);
        s.swatch_border_color = color_rgba8(80, 80, 80);
        s.selector_color = color_rgba8(255, 255, 255);
        return s;
    }
};

struct ColorPickerRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    ColorPickerStyle style;
    ColorPickerMode mode = ColorPickerMode::HSVSquare;

    // Current color
    math::Vec4 color;               // RGBA 0.0 - 1.0
    float hue = 0.0f;               // 0.0 - 360.0
    float saturation = 0.0f;        // 0.0 - 1.0
    float value_brightness = 1.0f;  // 0.0 - 1.0 (HSV value)
    float alpha = 1.0f;

    // Geometry
    math::Box color_area_rect;      // Main color area (square/wheel)
    math::Box hue_bar_rect;         // Hue slider bar
    math::Box alpha_bar_rect;       // Alpha slider bar
    math::Box preview_rect;         // Current/previous color preview
    math::Box hex_input_rect;       // Hex input field

    // Selector positions
    math::Vec2 color_selector_pos;  // Crosshair on color area
    float hue_selector_pos = 0.0f;  // Position on hue bar
    float alpha_selector_pos = 0.0f;

    bool show_alpha = true;
    bool show_hex_input = true;
    bool show_preview = true;
};

class IColorPickerEventHandler {
public:
    virtual ~IColorPickerEventHandler() = default;
    virtual void on_color_changed(const math::Vec4& color) = 0;
    virtual void on_color_confirmed(const math::Vec4& color) = 0;
};

class IGuiColorPicker : public IGuiWidget {
public:
    virtual ~IGuiColorPicker() = default;

    // Mode
    virtual ColorPickerMode get_mode() const = 0;
    virtual void set_mode(ColorPickerMode mode) = 0;

    // Color (RGBA 0.0 - 1.0)
    virtual math::Vec4 get_color() const = 0;
    virtual void set_color(const math::Vec4& color) = 0;

    // HSV access
    virtual float get_hue() const = 0;
    virtual void set_hue(float hue) = 0;
    virtual float get_saturation() const = 0;
    virtual void set_saturation(float saturation) = 0;
    virtual float get_brightness() const = 0;
    virtual void set_brightness(float brightness) = 0;

    // Alpha
    virtual float get_alpha() const = 0;
    virtual void set_alpha(float alpha) = 0;
    virtual bool is_alpha_enabled() const = 0;
    virtual void set_alpha_enabled(bool enabled) = 0;

    // Hex string (e.g. "#FF8040" or "#FF8040CC")
    virtual const char* get_hex_string() const = 0;
    virtual void set_hex_string(const char* hex) = 0;

    // Previous color (for comparison preview)
    virtual math::Vec4 get_previous_color() const = 0;
    virtual void set_previous_color(const math::Vec4& color) = 0;

    // Palette swatches
    virtual int get_swatch_count() const = 0;
    virtual math::Vec4 get_swatch_color(int index) const = 0;
    virtual void set_swatch_color(int index, const math::Vec4& color) = 0;
    virtual void add_swatch(const math::Vec4& color) = 0;
    virtual void remove_swatch(int index) = 0;
    virtual void clear_swatches() = 0;

    // Display options
    virtual bool is_hex_input_visible() const = 0;
    virtual void set_hex_input_visible(bool visible) = 0;
    virtual bool is_preview_visible() const = 0;
    virtual void set_preview_visible(bool visible) = 0;

    // Style
    virtual const ColorPickerStyle& get_color_picker_style() const = 0;
    virtual void set_color_picker_style(const ColorPickerStyle& style) = 0;

    // Event handler
    virtual void set_color_picker_event_handler(IColorPickerEventHandler* handler) = 0;

    // Render info
    virtual void get_color_picker_render_info(ColorPickerRenderInfo* out_info) const = 0;
};

// ============================================================================
// Image Interface - For image display
// ============================================================================

class IGuiImage : public IGuiWidget {
public:
    virtual ~IGuiImage() = default;

    // Image name (renderer resolves this to actual texture)
    virtual const std::string& get_image_name() const = 0;
    virtual void set_image_name(const std::string& name) = 0;

    // Tint
    virtual math::Vec4 get_tint() const = 0;
    virtual void set_tint(const math::Vec4& tint) = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_CONTROLS_HPP
