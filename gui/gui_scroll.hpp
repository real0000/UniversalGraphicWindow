/*
 * gui_scroll.hpp - ScrollView and ScrollBar Interfaces
 *
 * Contains IGuiScrollView for scrollable content areas and
 * IGuiScrollBar for standalone scrollbar widgets.
 */

#ifndef WINDOW_GUI_SCROLL_HPP
#define WINDOW_GUI_SCROLL_HPP

namespace window {
namespace gui {

// ============================================================================
// ScrollView Interface - For scrollable content areas
// ============================================================================

enum class ScrollBarVisibility : uint8_t {
    Auto = 0,       // Show when content exceeds viewport
    Always,         // Always visible
    Never           // Never visible
};

enum class ScrollViewSize : uint8_t {
    Compact = 0,    // Minimal scrollbars (6px), small padding
    Small,          // Small scrollbars (8px), compact spacing
    Medium,         // Standard size (12px scrollbars)
    Large,          // Large scrollbars (16px), touch-friendly
    Custom          // User-defined dimensions
};

struct ScrollViewSizeParams {
    float scrollbar_width = 12.0f;      // Width of scrollbar track
    float scrollbar_min_thumb = 20.0f;  // Minimum thumb length
    float content_padding = 4.0f;       // Padding inside content area
    float corner_radius = 6.0f;         // Scrollbar corner radius

    static ScrollViewSizeParams from_size(ScrollViewSize size) {
        ScrollViewSizeParams params;
        switch (size) {
            case ScrollViewSize::Compact:
                params.scrollbar_width = 6.0f;
                params.scrollbar_min_thumb = 16.0f;
                params.content_padding = 2.0f;
                params.corner_radius = 3.0f;
                break;
            case ScrollViewSize::Small:
                params.scrollbar_width = 8.0f;
                params.scrollbar_min_thumb = 18.0f;
                params.content_padding = 3.0f;
                params.corner_radius = 4.0f;
                break;
            case ScrollViewSize::Medium:
                params.scrollbar_width = 12.0f;
                params.scrollbar_min_thumb = 20.0f;
                params.content_padding = 4.0f;
                params.corner_radius = 6.0f;
                break;
            case ScrollViewSize::Large:
                params.scrollbar_width = 16.0f;
                params.scrollbar_min_thumb = 24.0f;
                params.content_padding = 6.0f;
                params.corner_radius = 8.0f;
                break;
            case ScrollViewSize::Custom:
            default:
                // Use default values
                break;
        }
        return params;
    }
};

struct ScrollViewRenderInfo {
    const IGuiWidget* widget = nullptr;

    // Geometry
    math::Box bounds;
    math::Box clip_rect;
    math::Box content_bounds;        // Full content area (may be larger than bounds)

    // Scroll state
    math::Vec2 scroll_offset;
    math::Vec2 content_size;         // Total size of scrollable content
    math::Vec2 viewport_size;        // Visible area size

    // Scrollbar geometry (empty if not visible)
    math::Box h_scrollbar_track;
    math::Box h_scrollbar_thumb;
    math::Box v_scrollbar_track;
    math::Box v_scrollbar_thumb;

    // Scrollbar visibility
    bool h_scrollbar_visible = false;
    bool v_scrollbar_visible = false;

    // Size preset and parameters
    ScrollViewSize size_preset = ScrollViewSize::Medium;
    ScrollViewSizeParams size_params;
};

class IGuiScrollBar;  // Forward declaration

class IGuiScrollView : public IGuiWidget {
public:
    virtual ~IGuiScrollView() = default;

    // Scroll offset
    virtual math::Vec2 get_scroll_offset() const = 0;
    virtual void set_scroll_offset(const math::Vec2& offset) = 0;

    // Content size (total scrollable area)
    virtual math::Vec2 get_content_size() const = 0;
    virtual void set_content_size(const math::Vec2& size) = 0;

    // Viewport size (visible area)
    virtual math::Vec2 get_viewport_size() const = 0;

    // Scroll limits
    virtual math::Vec2 get_max_scroll_offset() const = 0;

    // Scrollbar visibility
    virtual ScrollBarVisibility get_h_scrollbar_visibility() const = 0;
    virtual void set_h_scrollbar_visibility(ScrollBarVisibility visibility) = 0;
    virtual ScrollBarVisibility get_v_scrollbar_visibility() const = 0;
    virtual void set_v_scrollbar_visibility(ScrollBarVisibility visibility) = 0;

    // Scrollbar widgets
    virtual IGuiScrollBar* get_h_scrollbar() const = 0;
    virtual void set_h_scrollbar(IGuiScrollBar* scrollbar) = 0;
    virtual IGuiScrollBar* get_v_scrollbar() const = 0;
    virtual void set_v_scrollbar(IGuiScrollBar* scrollbar) = 0;

    // Scroll behavior
    virtual float get_scroll_speed() const = 0;
    virtual void set_scroll_speed(float speed) = 0;
    virtual bool is_scroll_inertia_enabled() const = 0;
    virtual void set_scroll_inertia_enabled(bool enabled) = 0;

    // Programmatic scrolling
    virtual void scroll_to(const math::Vec2& offset, bool animated = false) = 0;
    virtual void scroll_to_widget(const IGuiWidget* widget, bool animated = false) = 0;
    virtual void scroll_to_top(bool animated = false) = 0;
    virtual void scroll_to_bottom(bool animated = false) = 0;

    // Query
    virtual bool is_scrolling() const = 0;
    virtual bool can_scroll_horizontal() const = 0;
    virtual bool can_scroll_vertical() const = 0;

    // Size presets
    virtual ScrollViewSize get_size() const = 0;
    virtual void set_size(ScrollViewSize size) = 0;
    virtual ScrollViewSizeParams get_size_params() const = 0;
    virtual void set_size_params(const ScrollViewSizeParams& params) = 0;

    // Get scroll-specific render info
    virtual void get_scroll_render_info(ScrollViewRenderInfo* out_info) const = 0;
};

// ============================================================================
// ScrollBar Interface - Standalone scrollbar widget
// ============================================================================

enum class ScrollBarOrientation : uint8_t {
    Horizontal = 0,
    Vertical
};

struct ScrollBarStyle {
    math::Vec4 track_color;
    math::Vec4 thumb_color;
    math::Vec4 thumb_hover_color;
    math::Vec4 thumb_pressed_color;
    float track_width = 12.0f;
    float thumb_min_length = 20.0f;
    float corner_radius = 6.0f;

    static ScrollBarStyle default_style() {
        ScrollBarStyle s;
        s.track_color = math::Vec4(30 / 255.0f, 30 / 255.0f, 30 / 255.0f, 1.0f);
        s.thumb_color = math::Vec4(80 / 255.0f, 80 / 255.0f, 80 / 255.0f, 1.0f);
        s.thumb_hover_color = math::Vec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f);
        s.thumb_pressed_color = math::Vec4(160 / 255.0f, 160 / 255.0f, 160 / 255.0f, 1.0f);
        return s;
    }
};

struct ScrollBarRenderInfo {
    const IGuiWidget* widget = nullptr;

    ScrollBarOrientation orientation = ScrollBarOrientation::Vertical;

    // Geometry
    math::Box bounds;
    math::Box track_rect;
    math::Box thumb_rect;

    // Style
    ScrollBarStyle style;

    // State
    WidgetState thumb_state = WidgetState::Normal;
    float value = 0.0f;
    float page_size = 0.0f;
};

class IGuiScrollBar : public IGuiWidget {
public:
    virtual ~IGuiScrollBar() = default;

    // Orientation
    virtual ScrollBarOrientation get_orientation() const = 0;
    virtual void set_orientation(ScrollBarOrientation orientation) = 0;

    // Value (scroll position, 0.0 to max)
    virtual float get_value() const = 0;
    virtual void set_value(float value) = 0;

    // Range
    virtual float get_min_value() const = 0;
    virtual float get_max_value() const = 0;
    virtual void set_range(float min_value, float max_value) = 0;

    // Page size (visible portion, determines thumb size)
    virtual float get_page_size() const = 0;
    virtual void set_page_size(float size) = 0;

    // Step sizes
    virtual float get_line_step() const = 0;
    virtual void set_line_step(float step) = 0;
    virtual float get_page_step() const = 0;
    virtual void set_page_step(float step) = 0;

    // Scrollbar style
    virtual const ScrollBarStyle& get_scrollbar_style() const = 0;
    virtual void set_scrollbar_style(const ScrollBarStyle& style) = 0;

    // Thumb state
    virtual bool is_thumb_hovered() const = 0;
    virtual bool is_thumb_pressed() const = 0;

    // Get scrollbar-specific render info
    virtual void get_scrollbar_render_info(ScrollBarRenderInfo* out_info) const = 0;
};

// String conversion
const char* scroll_view_size_to_string(ScrollViewSize size);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_SCROLL_HPP
