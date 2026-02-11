/*
 * gui.hpp - Cross-Platform GUI Interface
 *
 * This is the public header for GUI functionality.
 * Provides a cross-window UI system where renderers query widget info directly.
 *
 * Key design:
 *   - No draw lists - renderer gets widget info and draws
 *   - Cross-window - widgets can appear on any window via viewports
 *   - Text is always a Label widget, not embedded in other widgets
 *   - Widgets provide all visual info via getters
 */

#ifndef WINDOW_GUI_HPP
#define WINDOW_GUI_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

#include "../math_util.hpp"

namespace window {
namespace gui {

// Forward declarations
class IGuiContext;
class IGuiWidget;

// ============================================================================
// Enums
// ============================================================================

enum class GuiResult : uint8_t {
    Success = 0,
    ErrorUnknown,
    ErrorNotInitialized,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorWidgetNotFound,
    ErrorLayoutFailed,
    ErrorViewportNotFound
};

enum class Alignment : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum class LayoutDirection : uint8_t {
    Horizontal = 0,
    Vertical
};

enum class SizeMode : uint8_t {
    Fixed = 0,      // Fixed pixel size
    Relative,       // Relative to parent (0.0-1.0)
    Auto,           // Size to content
    Fill            // Fill remaining space
};

enum class WidgetState : uint8_t {
    Normal = 0,
    Hovered,
    Pressed,
    Focused,
    Disabled
};

enum class WidgetType : uint8_t {
    Custom = 0,
    Container,
    Panel,
    Button,
    Label,          // Text display
    TextInput,      // Editable text
    Checkbox,
    RadioButton,
    Slider,
    ProgressBar,
    ScrollArea,
    ListBox,
    ComboBox,
    TabControl,
    TreeView,
    Image,
    Separator,
    Spacer
};

enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    X1,
    X2
};

// ============================================================================
// Color helpers (math::Vec4: x=r, y=g, z=b, w=a, floats 0-1)
// ============================================================================

inline math::Vec4 color_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return math::Vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

inline uint32_t color_to_rgba8(const math::Vec4& c) {
    uint8_t rb = static_cast<uint8_t>(c.x * 255.0f);
    uint8_t gb = static_cast<uint8_t>(c.y * 255.0f);
    uint8_t bb = static_cast<uint8_t>(c.z * 255.0f);
    uint8_t ab = static_cast<uint8_t>(c.w * 255.0f);
    return (ab << 24) | (bb << 16) | (gb << 8) | rb;
}

// ============================================================================
// Input State
// ============================================================================

struct GuiInputState {
    math::Vec2 mouse_position;
    math::Vec2 mouse_delta;
    float scroll_delta_x = 0.0f;
    float scroll_delta_y = 0.0f;
    bool mouse_buttons[5] = {};         // Current state
    bool mouse_buttons_pressed[5] = {}; // Just pressed this frame
    bool mouse_buttons_released[5] = {};// Just released this frame
    bool keys[512] = {};                // Current key state
    bool keys_pressed[512] = {};        // Just pressed this frame
    std::string text_input;             // Text input this frame (UTF-8)
    bool ctrl_held = false;
    bool shift_held = false;
    bool alt_held = false;
};

// ============================================================================
// Style - Visual appearance (no text properties)
// ============================================================================

struct GuiStyle {
    // Colors (math::Vec4: x=r, y=g, z=b, w=a)
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 hover_color;
    math::Vec4 pressed_color;
    math::Vec4 disabled_color;
    math::Vec4 focus_color;

    // Sizing
    float border_width = 1.0f;
    float corner_radius = 0.0f;
    math::Vec4 padding;       // x=left, y=top, z=right, w=bottom
    math::Vec4 margin;        // x=left, y=top, z=right, w=bottom

    // Create default style
    static GuiStyle default_style() {
        GuiStyle style;
        style.background_color = color_rgba8(45, 45, 48);
        style.border_color = color_rgba8(63, 63, 70);
        style.hover_color = color_rgba8(62, 62, 66);
        style.pressed_color = color_rgba8(27, 27, 28);
        style.disabled_color = color_rgba8(78, 78, 80);
        style.focus_color = color_rgba8(0, 122, 204);
        style.padding = math::Vec4(8.0f, 4.0f, 8.0f, 4.0f);
        style.margin = math::Vec4(2.0f);
        return style;
    }
};

// ============================================================================
// Label Style - Text-specific properties (only for Label widgets)
// ============================================================================

struct LabelStyle {
    math::Vec4 text_color;
    math::Vec4 selection_color;       // For TextInput
    float font_size = 14.0f;
    const char* font_name = nullptr;
    Alignment alignment = Alignment::CenterLeft;
    bool wrap = false;          // Word wrap
    bool ellipsis = false;      // Truncate with "..."

    static LabelStyle default_style() {
        LabelStyle style;
        style.text_color = color_rgba8(241, 241, 241);
        style.selection_color = color_rgba8(51, 153, 255, 128);
        return style;
    }
};

// ============================================================================
// Event Types
// ============================================================================

enum class GuiEventType : uint8_t {
    None = 0,
    Click,
    DoubleClick,
    Hover,
    Focus,
    Blur,
    ValueChanged,
    TextChanged,
    SelectionChanged,
    DragStart,
    DragMove,
    DragEnd,
    Scroll,
    KeyPress,
    KeyRelease,
    Resize,
    Close
};

struct GuiEvent {
    GuiEventType type = GuiEventType::None;
    IGuiWidget* source = nullptr;
    math::Vec2 position;
    int key_code = 0;
    int modifiers = 0;
    union {
        float float_value;
        int int_value;
        bool bool_value;
    };
};

// ============================================================================
// Event Handler Interface
// ============================================================================

class IGuiEventHandler {
public:
    virtual ~IGuiEventHandler() = default;
    virtual void on_gui_event(const GuiEvent& event) = 0;
};

// ============================================================================
// Text Measurement Interface (user provides implementation)
// ============================================================================

class ITextMeasurer {
public:
    virtual ~ITextMeasurer() = default;
    virtual math::Vec2 measure_text(const char* text, float font_size, const char* font_name = nullptr) = 0;
    virtual float get_line_height(float font_size, const char* font_name = nullptr) = 0;
};

// ============================================================================
// Viewport - Defines a view into the UI for a specific window
// ============================================================================

struct Viewport {
    int id = 0;                     // Viewport ID (user-defined, e.g., window ID)
    math::Box bounds;                     // Viewport bounds in UI coordinate space
    float scale = 1.0f;             // UI scale factor
    math::Vec2 offset;                    // Scroll/pan offset

    // Transform point from viewport to UI coordinates
    math::Vec2 to_ui(const math::Vec2& p) const {
        return math::Vec2((math::x(p) / scale) + math::x(bounds.min_corner()) - math::x(offset),
                     (math::y(p) / scale) + math::y(bounds.min_corner()) - math::y(offset));
    }

    // Transform point from UI to viewport coordinates
    math::Vec2 to_viewport(const math::Vec2& p) const {
        return math::Vec2((math::x(p) - math::x(bounds.min_corner()) + math::x(offset)) * scale,
                     (math::y(p) - math::y(bounds.min_corner()) + math::y(offset)) * scale);
    }

    // Get visible box in UI coordinates
    math::Box get_visible_box() const {
        float bx = math::x(bounds.min_corner()) - math::x(offset);
        float by = math::y(bounds.min_corner()) - math::y(offset);
        return math::make_box(bx, by, math::box_width(bounds) / scale, math::box_height(bounds) / scale);
    }
};

// ============================================================================
// Widget Render Info - Base info for all widgets
// ============================================================================

struct WidgetRenderInfo {
    // Identity
    const IGuiWidget* widget = nullptr;
    WidgetType type = WidgetType::Custom;
    WidgetState state = WidgetState::Normal;

    // Geometry (in UI coordinates)
    math::Box bounds;
    math::Box content_bounds;     // Bounds minus padding
    math::Box clip_rect;          // Clipping rectangle

    // Style
    math::Vec4 background_color;
    math::Vec4 border_color;
    float border_width = 1.0f;
    float corner_radius = 0.0f;

    // Widget-specific values
    float value = 0.0f;             // For slider, progress bar
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool checked = false;           // For checkbox, radio button
};

// ============================================================================
// Label Render Info - For Label and TextInput widgets only
// ============================================================================

struct LabelRenderInfo {
    const IGuiWidget* widget = nullptr;

    // Geometry
    math::Box bounds;
    math::Box clip_rect;

    // Text content
    const char* text = nullptr;

    // Style
    math::Vec4 text_color;
    float font_size = 14.0f;
    const char* font_name = nullptr;
    Alignment alignment = Alignment::CenterLeft;

    // For TextInput only
    bool is_editable = false;
    int cursor_position = 0;
    int selection_start = 0;
    int selection_length = 0;
    math::Vec4 selection_color;
};

// ============================================================================
// Image Render Info - For Image widgets only
// ============================================================================

struct ImageRenderInfo {
    const IGuiWidget* widget = nullptr;

    // Geometry
    math::Box bounds;
    math::Box clip_rect;

    // Image name (renderer resolves this to actual texture)
    std::string image_name;
    math::Vec4 tint = math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
};

// ============================================================================
// Widget Iterator - For iterating visible widgets in a viewport
// ============================================================================

class IWidgetIterator {
public:
    virtual ~IWidgetIterator() = default;

    // Check if there are more widgets
    virtual bool has_next() const = 0;

    // Get next widget's render info (base info)
    virtual bool next(WidgetRenderInfo* out_info) = 0;

    // Reset to beginning
    virtual void reset() = 0;
};

// ============================================================================
// Widget Interface (Abstract) - Base for all widgets
// ============================================================================

class IGuiWidget {
public:
    virtual ~IGuiWidget() = default;

    // Type
    virtual WidgetType get_type() const = 0;

    // Name (optional, user-defined)
    virtual const char* get_name() const = 0;
    virtual void set_name(const char* name) = 0;

    // Find widget by name (searches self and all descendants)
    // Returns first match, or nullptr if not found
    virtual IGuiWidget* find_by_name(const char* name) = 0;

    // Find all widgets with specified name (searches self and all descendants)
    virtual void find_all_by_name(const char* name, std::vector<IGuiWidget*>& out_widgets) = 0;

    // Hierarchy
    virtual IGuiWidget* get_parent() const = 0;
    virtual void set_parent(IGuiWidget* parent) = 0;

    // Geometry
    virtual math::Box get_bounds() const = 0;
    virtual void set_bounds(const math::Box& bounds) = 0;
    virtual math::Vec2 get_preferred_size() const = 0;
    virtual math::Vec2 get_min_size() const = 0;
    virtual math::Vec2 get_max_size() const = 0;
    virtual void set_min_size(const math::Vec2& size) = 0;
    virtual void set_max_size(const math::Vec2& size) = 0;

    // Clipping
    virtual bool is_clip_enabled() const = 0;
    virtual void set_clip_enabled(bool enabled) = 0;
    virtual math::Box get_clip_rect() const = 0;
    virtual void set_clip_rect(const math::Box& rect) = 0;

    // Visibility and state
    virtual bool is_visible() const = 0;
    virtual void set_visible(bool visible) = 0;
    virtual bool is_enabled() const = 0;
    virtual void set_enabled(bool enabled) = 0;
    virtual WidgetState get_state() const = 0;

    // Style
    virtual const GuiStyle& get_style() const = 0;
    virtual void set_style(const GuiStyle& style) = 0;

    // Layout
    virtual SizeMode get_width_mode() const = 0;
    virtual SizeMode get_height_mode() const = 0;
    virtual void set_size_mode(SizeMode width_mode, SizeMode height_mode) = 0;
    virtual Alignment get_alignment() const = 0;
    virtual void set_alignment(Alignment alignment) = 0;

    // Events
    virtual void set_event_handler(IGuiEventHandler* handler) = 0;

    // Update
    virtual void update(float delta_time) = 0;

    // Get render info for this widget (renderer calls this)
    virtual void get_render_info(WidgetRenderInfo* out_info) const = 0;

    // Input handling
    virtual bool handle_mouse_move(const math::Vec2& position) = 0;
    virtual bool handle_mouse_button(MouseButton button, bool pressed, const math::Vec2& position) = 0;
    virtual bool handle_mouse_scroll(float delta_x, float delta_y) = 0;
    virtual bool handle_key(int key_code, bool pressed, int modifiers) = 0;
    virtual bool handle_text_input(const char* text) = 0;

    // Focus
    virtual bool is_focusable() const = 0;
    virtual bool has_focus() const = 0;
    virtual void set_focus(bool focus) = 0;

    // Hit testing
    virtual bool hit_test(const math::Vec2& position) const = 0;
    virtual IGuiWidget* find_widget_at(const math::Vec2& position) = 0;

    // Widget-specific value (slider, progress bar, etc.)
    virtual float get_value() const { return 0.0f; }
    virtual void set_value(float value) { (void)value; }
    virtual bool is_checked() const { return false; }
    virtual void set_checked(bool checked) { (void)checked; }

    // Children management (tree structure)
    virtual int get_child_count() const = 0;
    virtual IGuiWidget* get_child(int index) const = 0;
    virtual bool add_child(IGuiWidget* child) = 0;
    virtual bool remove_child(IGuiWidget* child) = 0;
    virtual bool remove_child_at(int index) = 0;
    virtual void clear_children() = 0;

    // Layout for children
    virtual LayoutDirection get_layout_direction() const = 0;
    virtual void set_layout_direction(LayoutDirection direction) = 0;
    virtual float get_spacing() const = 0;
    virtual void set_spacing(float spacing) = 0;
    virtual void layout_children() = 0;
};

// ============================================================================
// Label Interface - For text display
// ============================================================================

class IGuiLabel : public IGuiWidget {
public:
    virtual ~IGuiLabel() = default;

    // Text content
    virtual const char* get_text() const = 0;
    virtual void set_text(const char* text) = 0;

    // Label style
    virtual const LabelStyle& get_label_style() const = 0;
    virtual void set_label_style(const LabelStyle& style) = 0;

    // Get label-specific render info
    virtual void get_label_render_info(LabelRenderInfo* out_info) const = 0;
};

// ============================================================================
// TextInput Interface - For editable text
// ============================================================================

class IGuiTextInput : public IGuiLabel {
public:
    virtual ~IGuiTextInput() = default;

    // Cursor and selection
    virtual int get_cursor_position() const = 0;
    virtual void set_cursor_position(int position) = 0;
    virtual int get_selection_start() const = 0;
    virtual int get_selection_length() const = 0;
    virtual void set_selection(int start, int length) = 0;
    virtual void select_all() = 0;
    virtual void clear_selection() = 0;

    // Editing
    virtual void insert_text(const char* text) = 0;
    virtual void delete_selection() = 0;
    virtual void delete_backward(int count = 1) = 0;
    virtual void delete_forward(int count = 1) = 0;

    // Placeholder
    virtual const char* get_placeholder() const = 0;
    virtual void set_placeholder(const char* placeholder) = 0;

    // Input mode
    virtual bool is_password_mode() const = 0;
    virtual void set_password_mode(bool enabled) = 0;
    virtual bool is_read_only() const = 0;
    virtual void set_read_only(bool read_only) = 0;
    virtual int get_max_length() const = 0;
    virtual void set_max_length(int max_length) = 0;
};

// ============================================================================
// ScrollView Interface - For scrollable content areas
// ============================================================================

enum class ScrollBarVisibility : uint8_t {
    Auto = 0,       // Show when content exceeds viewport
    Always,         // Always visible
    Never           // Never visible
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

// ============================================================================
// PropertyGrid Interface - Editable name/value property list
// ============================================================================

enum class PropertyType : uint8_t {
    String = 0,
    Int,
    Float,
    Bool,
    Color,          // Vec4 RGBA
    Vec2,
    Vec4,
    Enum,           // Dropdown selection from options
    Range,          // Float with min/max (rendered as slider)
    Category        // Group header, no value
};

struct PropertyGridStyle {
    math::Vec4 category_background;
    math::Vec4 category_text_color;
    math::Vec4 name_text_color;
    math::Vec4 value_text_color;
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 separator_color;
    float row_height = 24.0f;
    float name_column_width = 150.0f;
    float indent_width = 16.0f;
    float font_size = 13.0f;

    static PropertyGridStyle default_style() {
        PropertyGridStyle s;
        s.category_background = color_rgba8(37, 37, 38);
        s.category_text_color = color_rgba8(220, 220, 220);
        s.name_text_color = color_rgba8(200, 200, 200);
        s.value_text_color = color_rgba8(241, 241, 241);
        s.row_background = color_rgba8(45, 45, 48);
        s.row_alt_background = color_rgba8(50, 50, 53);
        s.selected_background = color_rgba8(0, 122, 204);
        s.separator_color = color_rgba8(63, 63, 70);
        return s;
    }
};

struct PropertyRenderItem {
    int property_id = -1;
    const char* name = nullptr;
    const char* category = nullptr;
    PropertyType type = PropertyType::String;
    int depth = 0;
    bool is_category_header = false;
    bool expanded = true;
    bool read_only = false;
    bool selected = false;
    math::Box row_rect;
    math::Box name_rect;
    math::Box value_rect;
};

struct PropertyGridRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    PropertyGridStyle style;
    int total_row_count = 0;
    int visible_row_count = 0;
    int selected_property = -1;
    float scroll_offset_y = 0.0f;
};

class IPropertyGridEventHandler {
public:
    virtual ~IPropertyGridEventHandler() = default;
    virtual void on_property_changed(int property_id) = 0;
};

class IGuiPropertyGrid : public IGuiWidget {
public:
    virtual ~IGuiPropertyGrid() = default;

    // Property management
    virtual int add_property(const char* category, const char* name, PropertyType type) = 0;
    virtual bool remove_property(int property_id) = 0;
    virtual void clear_properties() = 0;
    virtual int get_property_count() const = 0;

    // Property info
    virtual const char* get_property_name(int property_id) const = 0;
    virtual const char* get_property_category(int property_id) const = 0;
    virtual PropertyType get_property_type(int property_id) const = 0;

    // Value access - String
    virtual const char* get_string_value(int property_id) const = 0;
    virtual void set_string_value(int property_id, const char* value) = 0;

    // Value access - Int
    virtual int get_int_value(int property_id) const = 0;
    virtual void set_int_value(int property_id, int value) = 0;

    // Value access - Float
    virtual float get_float_value(int property_id) const = 0;
    virtual void set_float_value(int property_id, float value) = 0;

    // Value access - Bool
    virtual bool get_bool_value(int property_id) const = 0;
    virtual void set_bool_value(int property_id, bool value) = 0;

    // Value access - Vec2
    virtual math::Vec2 get_vec2_value(int property_id) const = 0;
    virtual void set_vec2_value(int property_id, const math::Vec2& value) = 0;

    // Value access - Vec4 / Color
    virtual math::Vec4 get_vec4_value(int property_id) const = 0;
    virtual void set_vec4_value(int property_id, const math::Vec4& value) = 0;

    // Enum options
    virtual void set_enum_options(int property_id, const std::vector<std::string>& options) = 0;
    virtual const std::vector<std::string>& get_enum_options(int property_id) const = 0;
    virtual int get_enum_index(int property_id) const = 0;
    virtual void set_enum_index(int property_id, int index) = 0;

    // Range limits (for PropertyType::Range)
    virtual void set_range_limits(int property_id, float min_val, float max_val) = 0;
    virtual float get_range_min(int property_id) const = 0;
    virtual float get_range_max(int property_id) const = 0;

    // Read-only
    virtual bool is_property_read_only(int property_id) const = 0;
    virtual void set_property_read_only(int property_id, bool read_only) = 0;

    // Category management
    virtual bool is_category_expanded(const char* category) const = 0;
    virtual void set_category_expanded(const char* category, bool expanded) = 0;
    virtual void expand_all() = 0;
    virtual void collapse_all() = 0;

    // Selection
    virtual int get_selected_property() const = 0;
    virtual void set_selected_property(int property_id) = 0;

    // Layout
    virtual float get_name_column_width() const = 0;
    virtual void set_name_column_width(float width) = 0;
    virtual float get_row_height() const = 0;
    virtual void set_row_height(float height) = 0;

    // Style
    virtual const PropertyGridStyle& get_property_grid_style() const = 0;
    virtual void set_property_grid_style(const PropertyGridStyle& style) = 0;

    // Event handler
    virtual void set_property_event_handler(IPropertyGridEventHandler* handler) = 0;

    // Render info
    virtual void get_property_grid_render_info(PropertyGridRenderInfo* out_info) const = 0;
    virtual int get_visible_property_items(PropertyRenderItem* out_items, int max_items) const = 0;
};

// ============================================================================
// TreeView Interface - Hierarchical node display
// ============================================================================

enum class TreeViewSelectionMode : uint8_t {
    Single = 0,
    Multi,
    None
};

struct TreeViewStyle {
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 hover_background;
    math::Vec4 text_color;
    math::Vec4 icon_color;
    math::Vec4 line_color;          // Indent guide lines
    float row_height = 22.0f;
    float indent_width = 18.0f;
    float icon_size = 16.0f;
    float font_size = 13.0f;
    bool show_lines = true;         // Draw indent guide lines
    bool show_root_lines = false;   // Draw lines from root nodes

    static TreeViewStyle default_style() {
        TreeViewStyle s;
        s.row_background = color_rgba8(45, 45, 48);
        s.row_alt_background = color_rgba8(50, 50, 53);
        s.selected_background = color_rgba8(0, 122, 204);
        s.hover_background = color_rgba8(62, 62, 66);
        s.text_color = color_rgba8(241, 241, 241);
        s.icon_color = color_rgba8(200, 200, 200);
        s.line_color = color_rgba8(80, 80, 80);
        return s;
    }
};

struct TreeNodeRenderItem {
    int node_id = -1;
    const char* text = nullptr;
    const char* icon_name = nullptr;    // Renderer resolves to actual icon
    int depth = 0;
    bool has_children = false;
    bool expanded = false;
    bool selected = false;
    bool hovered = false;
    math::Box row_rect;
    math::Box expand_rect;              // Toggle expand/collapse area
    math::Box icon_rect;
    math::Box text_rect;
};

struct TreeViewRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    TreeViewStyle style;
    int total_node_count = 0;
    int visible_node_count = 0;
    float scroll_offset_y = 0.0f;
};

class ITreeViewEventHandler {
public:
    virtual ~ITreeViewEventHandler() = default;
    virtual void on_node_selected(int node_id) = 0;
    virtual void on_node_expanded(int node_id, bool expanded) = 0;
    virtual void on_node_double_clicked(int node_id) = 0;
};

class IGuiTreeView : public IGuiWidget {
public:
    virtual ~IGuiTreeView() = default;

    // Node management
    virtual int add_node(int parent_id, const char* text, const char* icon_name = nullptr) = 0;
    virtual bool remove_node(int node_id) = 0;
    virtual void clear_nodes() = 0;
    virtual int get_node_count() const = 0;

    // Node info
    virtual const char* get_node_text(int node_id) const = 0;
    virtual void set_node_text(int node_id, const char* text) = 0;
    virtual const char* get_node_icon(int node_id) const = 0;
    virtual void set_node_icon(int node_id, const char* icon_name) = 0;

    // Hierarchy
    virtual int get_node_parent(int node_id) const = 0;
    virtual int get_node_child_count(int node_id) const = 0;
    virtual int get_node_child(int node_id, int index) const = 0;
    virtual int get_root_node_count() const = 0;
    virtual int get_root_node(int index) const = 0;

    // Expand/collapse
    virtual bool is_node_expanded(int node_id) const = 0;
    virtual void set_node_expanded(int node_id, bool expanded) = 0;
    virtual void expand_all() = 0;
    virtual void collapse_all() = 0;
    virtual void expand_to_node(int node_id) = 0;

    // Selection
    virtual TreeViewSelectionMode get_selection_mode() const = 0;
    virtual void set_selection_mode(TreeViewSelectionMode mode) = 0;
    virtual int get_selected_node() const = 0;
    virtual void set_selected_node(int node_id) = 0;
    virtual void get_selected_nodes(std::vector<int>& out_nodes) const = 0;
    virtual void set_selected_nodes(const std::vector<int>& node_ids) = 0;
    virtual void clear_selection() = 0;

    // Scrolling
    virtual void scroll_to_node(int node_id) = 0;
    virtual void ensure_node_visible(int node_id) = 0;

    // User data
    virtual void set_node_user_data(int node_id, void* data) = 0;
    virtual void* get_node_user_data(int node_id) const = 0;

    // Node enable/disable
    virtual bool is_node_enabled(int node_id) const = 0;
    virtual void set_node_enabled(int node_id, bool enabled) = 0;

    // Drag and drop reordering
    virtual bool is_drag_reorder_enabled() const = 0;
    virtual void set_drag_reorder_enabled(bool enabled) = 0;

    // Style
    virtual const TreeViewStyle& get_tree_view_style() const = 0;
    virtual void set_tree_view_style(const TreeViewStyle& style) = 0;

    // Event handler
    virtual void set_tree_event_handler(ITreeViewEventHandler* handler) = 0;

    // Render info
    virtual void get_tree_view_render_info(TreeViewRenderInfo* out_info) const = 0;
    virtual int get_visible_tree_items(TreeNodeRenderItem* out_items, int max_items) const = 0;
};

// ============================================================================
// TabControl Interface - Tabbed container
// ============================================================================

enum class TabPosition : uint8_t {
    Top = 0,
    Bottom,
    Left,
    Right
};

enum class TabSizeMode : uint8_t {
    Fixed = 0,      // All tabs same width
    FitContent,     // Size to text + icon
    Fill            // Stretch to fill available space
};

struct TabStyle {
    math::Vec4 tab_background;
    math::Vec4 tab_hover_background;
    math::Vec4 tab_active_background;
    math::Vec4 tab_text_color;
    math::Vec4 tab_active_text_color;
    math::Vec4 tab_bar_background;
    math::Vec4 indicator_color;         // Active tab indicator line
    math::Vec4 close_button_color;
    math::Vec4 close_button_hover_color;
    float tab_height = 30.0f;
    float tab_min_width = 60.0f;
    float tab_max_width = 200.0f;
    float tab_padding = 12.0f;
    float indicator_height = 2.0f;
    float icon_size = 16.0f;
    float close_button_size = 14.0f;
    float corner_radius = 0.0f;
    float font_size = 13.0f;

    static TabStyle default_style() {
        TabStyle s;
        s.tab_background = color_rgba8(45, 45, 48);
        s.tab_hover_background = color_rgba8(62, 62, 66);
        s.tab_active_background = color_rgba8(37, 37, 38);
        s.tab_text_color = color_rgba8(160, 160, 160);
        s.tab_active_text_color = color_rgba8(241, 241, 241);
        s.tab_bar_background = color_rgba8(30, 30, 30);
        s.indicator_color = color_rgba8(0, 122, 204);
        s.close_button_color = color_rgba8(160, 160, 160);
        s.close_button_hover_color = color_rgba8(241, 241, 241);
        return s;
    }
};

struct TabRenderItem {
    int tab_id = -1;
    const char* text = nullptr;
    const char* icon_name = nullptr;
    bool active = false;
    bool hovered = false;
    bool closable = false;
    bool enabled = true;
    math::Box tab_rect;
    math::Box icon_rect;
    math::Box text_rect;
    math::Box close_rect;
};

struct TabControlRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box tab_bar_rect;
    math::Box content_rect;

    TabStyle style;
    TabPosition position = TabPosition::Top;
    int tab_count = 0;
    int active_tab = -1;
    float scroll_offset = 0.0f;        // Tab bar scroll when tabs overflow
    bool can_scroll_left = false;
    bool can_scroll_right = false;
};

class ITabControlEventHandler {
public:
    virtual ~ITabControlEventHandler() = default;
    virtual void on_tab_selected(int tab_id) = 0;
    virtual void on_tab_closed(int tab_id) = 0;
    virtual void on_tab_reordered(int tab_id, int new_index) = 0;
};

class IGuiTabControl : public IGuiWidget {
public:
    virtual ~IGuiTabControl() = default;

    // Tab management
    virtual int add_tab(const char* text, const char* icon_name = nullptr) = 0;
    virtual int insert_tab(int index, const char* text, const char* icon_name = nullptr) = 0;
    virtual bool remove_tab(int tab_id) = 0;
    virtual void clear_tabs() = 0;
    virtual int get_tab_count() const = 0;

    // Tab info
    virtual const char* get_tab_text(int tab_id) const = 0;
    virtual void set_tab_text(int tab_id, const char* text) = 0;
    virtual const char* get_tab_icon(int tab_id) const = 0;
    virtual void set_tab_icon(int tab_id, const char* icon_name) = 0;

    // Tab enable/disable
    virtual bool is_tab_enabled(int tab_id) const = 0;
    virtual void set_tab_enabled(int tab_id, bool enabled) = 0;

    // Tab closable
    virtual bool is_tab_closable(int tab_id) const = 0;
    virtual void set_tab_closable(int tab_id, bool closable) = 0;

    // Tab content widget
    virtual IGuiWidget* get_tab_content(int tab_id) const = 0;
    virtual void set_tab_content(int tab_id, IGuiWidget* content) = 0;

    // Active tab
    virtual int get_active_tab() const = 0;
    virtual void set_active_tab(int tab_id) = 0;

    // Tab position
    virtual TabPosition get_tab_position() const = 0;
    virtual void set_tab_position(TabPosition position) = 0;

    // Tab sizing
    virtual TabSizeMode get_tab_size_mode() const = 0;
    virtual void set_tab_size_mode(TabSizeMode mode) = 0;
    virtual float get_fixed_tab_width() const = 0;
    virtual void set_fixed_tab_width(float width) = 0;

    // Drag reorder
    virtual bool is_drag_reorder_enabled() const = 0;
    virtual void set_drag_reorder_enabled(bool enabled) = 0;

    // Tab user data
    virtual void set_tab_user_data(int tab_id, void* data) = 0;
    virtual void* get_tab_user_data(int tab_id) const = 0;

    // Style
    virtual const TabStyle& get_tab_style() const = 0;
    virtual void set_tab_style(const TabStyle& style) = 0;

    // Event handler
    virtual void set_tab_event_handler(ITabControlEventHandler* handler) = 0;

    // Render info
    virtual void get_tab_control_render_info(TabControlRenderInfo* out_info) const = 0;
    virtual int get_visible_tab_items(TabRenderItem* out_items, int max_items) const = 0;
};

// ============================================================================
// ListBox Interface - Selectable item list
// ============================================================================

enum class ListBoxSelectionMode : uint8_t {
    Single = 0,
    Multi,
    None
};

struct ListBoxStyle {
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 hover_background;
    math::Vec4 text_color;
    math::Vec4 selected_text_color;
    math::Vec4 icon_color;
    math::Vec4 separator_color;
    float row_height = 24.0f;
    float icon_size = 16.0f;
    float item_padding = 8.0f;
    float font_size = 13.0f;
    bool show_separator = false;

    static ListBoxStyle default_style() {
        ListBoxStyle s;
        s.row_background = color_rgba8(45, 45, 48);
        s.row_alt_background = color_rgba8(50, 50, 53);
        s.selected_background = color_rgba8(0, 122, 204);
        s.hover_background = color_rgba8(62, 62, 66);
        s.text_color = color_rgba8(241, 241, 241);
        s.selected_text_color = color_rgba8(255, 255, 255);
        s.icon_color = color_rgba8(200, 200, 200);
        s.separator_color = color_rgba8(63, 63, 70);
        return s;
    }
};

struct ListBoxItemRenderInfo {
    int item_id = -1;
    const char* text = nullptr;
    const char* icon_name = nullptr;
    bool selected = false;
    bool hovered = false;
    bool enabled = true;
    math::Box row_rect;
    math::Box icon_rect;
    math::Box text_rect;
};

struct ListBoxRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    ListBoxStyle style;
    int total_item_count = 0;
    int visible_item_count = 0;
    float scroll_offset_y = 0.0f;
};

class IListBoxEventHandler {
public:
    virtual ~IListBoxEventHandler() = default;
    virtual void on_item_selected(int item_id) = 0;
    virtual void on_item_double_clicked(int item_id) = 0;
};

class IGuiListBox : public IGuiWidget {
public:
    virtual ~IGuiListBox() = default;

    // Item management
    virtual int add_item(const char* text, const char* icon_name = nullptr) = 0;
    virtual int insert_item(int index, const char* text, const char* icon_name = nullptr) = 0;
    virtual bool remove_item(int item_id) = 0;
    virtual void clear_items() = 0;
    virtual int get_item_count() const = 0;

    // Item info
    virtual const char* get_item_text(int item_id) const = 0;
    virtual void set_item_text(int item_id, const char* text) = 0;
    virtual const char* get_item_icon(int item_id) const = 0;
    virtual void set_item_icon(int item_id, const char* icon_name) = 0;

    // Item enable/disable
    virtual bool is_item_enabled(int item_id) const = 0;
    virtual void set_item_enabled(int item_id, bool enabled) = 0;

    // Selection
    virtual ListBoxSelectionMode get_selection_mode() const = 0;
    virtual void set_selection_mode(ListBoxSelectionMode mode) = 0;
    virtual int get_selected_item() const = 0;
    virtual void set_selected_item(int item_id) = 0;
    virtual void get_selected_items(std::vector<int>& out_items) const = 0;
    virtual void set_selected_items(const std::vector<int>& item_ids) = 0;
    virtual void clear_selection() = 0;

    // Scrolling
    virtual void scroll_to_item(int item_id) = 0;
    virtual void ensure_item_visible(int item_id) = 0;

    // Item user data
    virtual void set_item_user_data(int item_id, void* data) = 0;
    virtual void* get_item_user_data(int item_id) const = 0;

    // Sorting
    virtual void sort_items(bool ascending = true) = 0;

    // Style
    virtual const ListBoxStyle& get_list_box_style() const = 0;
    virtual void set_list_box_style(const ListBoxStyle& style) = 0;

    // Event handler
    virtual void set_list_event_handler(IListBoxEventHandler* handler) = 0;

    // Render info
    virtual void get_list_box_render_info(ListBoxRenderInfo* out_info) const = 0;
    virtual int get_visible_list_items(ListBoxItemRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// ComboBox Interface - Dropdown selection
// ============================================================================

struct ComboBoxStyle {
    math::Vec4 background_color;
    math::Vec4 hover_background;
    math::Vec4 open_background;
    math::Vec4 text_color;
    math::Vec4 placeholder_color;
    math::Vec4 arrow_color;
    math::Vec4 dropdown_background;
    math::Vec4 dropdown_border_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_selected_background;
    math::Vec4 item_text_color;
    math::Vec4 item_selected_text_color;
    float height = 28.0f;
    float dropdown_max_height = 200.0f;
    float item_height = 24.0f;
    float item_padding = 8.0f;
    float arrow_size = 10.0f;
    float icon_size = 16.0f;
    float corner_radius = 4.0f;
    float dropdown_corner_radius = 4.0f;
    float border_width = 1.0f;
    float font_size = 13.0f;

    static ComboBoxStyle default_style() {
        ComboBoxStyle s;
        s.background_color = color_rgba8(45, 45, 48);
        s.hover_background = color_rgba8(62, 62, 66);
        s.open_background = color_rgba8(37, 37, 38);
        s.text_color = color_rgba8(241, 241, 241);
        s.placeholder_color = color_rgba8(130, 130, 130);
        s.arrow_color = color_rgba8(160, 160, 160);
        s.dropdown_background = color_rgba8(37, 37, 38);
        s.dropdown_border_color = color_rgba8(63, 63, 70);
        s.item_hover_background = color_rgba8(62, 62, 66);
        s.item_selected_background = color_rgba8(0, 122, 204);
        s.item_text_color = color_rgba8(241, 241, 241);
        s.item_selected_text_color = color_rgba8(255, 255, 255);
        return s;
    }
};

struct ComboBoxItemRenderInfo {
    int item_id = -1;
    const char* text = nullptr;
    const char* icon_name = nullptr;
    bool selected = false;
    bool hovered = false;
    bool enabled = true;
    math::Box row_rect;
    math::Box icon_rect;
    math::Box text_rect;
};

struct ComboBoxRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box arrow_rect;
    math::Box dropdown_rect;

    ComboBoxStyle style;
    const char* display_text = nullptr;     // Current selected text or placeholder
    bool is_open = false;
    bool is_placeholder = false;
    int item_count = 0;
    int visible_item_count = 0;
    float dropdown_scroll_offset = 0.0f;
};

class IComboBoxEventHandler {
public:
    virtual ~IComboBoxEventHandler() = default;
    virtual void on_selection_changed(int item_id) = 0;
    virtual void on_dropdown_opened() = 0;
    virtual void on_dropdown_closed() = 0;
};

class IGuiComboBox : public IGuiWidget {
public:
    virtual ~IGuiComboBox() = default;

    // Item management
    virtual int add_item(const char* text, const char* icon_name = nullptr) = 0;
    virtual int insert_item(int index, const char* text, const char* icon_name = nullptr) = 0;
    virtual bool remove_item(int item_id) = 0;
    virtual void clear_items() = 0;
    virtual int get_item_count() const = 0;

    // Item info
    virtual const char* get_item_text(int item_id) const = 0;
    virtual void set_item_text(int item_id, const char* text) = 0;
    virtual const char* get_item_icon(int item_id) const = 0;
    virtual void set_item_icon(int item_id, const char* icon_name) = 0;

    // Item enable/disable
    virtual bool is_item_enabled(int item_id) const = 0;
    virtual void set_item_enabled(int item_id, bool enabled) = 0;

    // Selection
    virtual int get_selected_item() const = 0;
    virtual void set_selected_item(int item_id) = 0;

    // Placeholder
    virtual const char* get_placeholder() const = 0;
    virtual void set_placeholder(const char* text) = 0;

    // Dropdown state
    virtual bool is_open() const = 0;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual void toggle() = 0;

    // Item user data
    virtual void set_item_user_data(int item_id, void* data) = 0;
    virtual void* get_item_user_data(int item_id) const = 0;

    // Style
    virtual const ComboBoxStyle& get_combo_box_style() const = 0;
    virtual void set_combo_box_style(const ComboBoxStyle& style) = 0;

    // Event handler
    virtual void set_combo_event_handler(IComboBoxEventHandler* handler) = 0;

    // Render info
    virtual void get_combo_box_render_info(ComboBoxRenderInfo* out_info) const = 0;
    virtual int get_visible_combo_items(ComboBoxItemRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// Dialog/Popup Interface - Modal and non-modal overlays
// ============================================================================

enum class DialogResult : uint8_t {
    None = 0,
    OK,
    Cancel,
    Yes,
    No,
    Retry,
    Abort,
    Custom
};

enum class DialogButtons : uint8_t {
    None = 0,
    OK,
    OKCancel,
    YesNo,
    YesNoCancel,
    RetryCancel,
    AbortRetryIgnore,
    Custom
};

enum class PopupPlacement : uint8_t {
    Center = 0,         // Center of parent / screen
    AtCursor,           // At current mouse position
    Below,              // Below anchor widget
    Above,              // Above anchor widget
    Right,              // Right of anchor widget
    Left,               // Left of anchor widget
    Manual              // Use explicit position
};

struct DialogStyle {
    math::Vec4 overlay_color;           // Dimmed background behind modal
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_text_color;
    math::Vec4 shadow_color;
    float border_width = 1.0f;
    float corner_radius = 6.0f;
    float title_bar_height = 32.0f;
    float button_area_height = 44.0f;
    float padding = 16.0f;
    float shadow_offset = 4.0f;
    float shadow_blur = 8.0f;
    float min_width = 300.0f;
    float min_height = 150.0f;
    float font_size = 13.0f;
    float title_font_size = 14.0f;

    static DialogStyle default_style() {
        DialogStyle s;
        s.overlay_color = color_rgba8(0, 0, 0, 128);
        s.background_color = color_rgba8(45, 45, 48);
        s.border_color = color_rgba8(63, 63, 70);
        s.title_bar_color = color_rgba8(37, 37, 38);
        s.title_text_color = color_rgba8(241, 241, 241);
        s.shadow_color = color_rgba8(0, 0, 0, 100);
        return s;
    }
};

struct DialogRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box overlay_rect;         // Full-screen dim overlay (modal only)
    math::Box title_bar_rect;
    math::Box content_rect;
    math::Box button_area_rect;
    math::Box close_button_rect;

    DialogStyle style;
    const char* title = nullptr;
    bool is_modal = false;
    bool is_draggable = false;
    bool is_resizable = false;
    bool show_close_button = true;
    bool close_button_hovered = false;
};

struct PopupRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 shadow_color;
    float border_width = 1.0f;
    float corner_radius = 4.0f;
    float shadow_offset = 2.0f;
    float shadow_blur = 6.0f;
    bool is_open = false;
};

class IDialogEventHandler {
public:
    virtual ~IDialogEventHandler() = default;
    virtual void on_dialog_closed(DialogResult result) = 0;
    virtual void on_dialog_button_clicked(DialogResult button) = 0;
};

class IGuiDialog : public IGuiWidget {
public:
    virtual ~IGuiDialog() = default;

    // Title
    virtual const char* get_title() const = 0;
    virtual void set_title(const char* title) = 0;

    // Modal
    virtual bool is_modal() const = 0;
    virtual void set_modal(bool modal) = 0;

    // Show/hide
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool is_open() const = 0;

    // Result
    virtual DialogResult get_result() const = 0;

    // Buttons
    virtual void set_buttons(DialogButtons buttons) = 0;
    virtual DialogButtons get_buttons() const = 0;
    virtual void set_custom_button(int index, const char* text, DialogResult result) = 0;
    virtual int get_button_count() const = 0;

    // Content widget
    virtual IGuiWidget* get_content() const = 0;
    virtual void set_content(IGuiWidget* content) = 0;

    // Behavior
    virtual bool is_draggable() const = 0;
    virtual void set_draggable(bool draggable) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_resizable(bool resizable) = 0;
    virtual bool has_close_button() const = 0;
    virtual void set_close_button(bool show) = 0;
    virtual bool is_close_on_overlay_click() const = 0;
    virtual void set_close_on_overlay_click(bool enabled) = 0;

    // Style
    virtual const DialogStyle& get_dialog_style() const = 0;
    virtual void set_dialog_style(const DialogStyle& style) = 0;

    // Event handler
    virtual void set_dialog_event_handler(IDialogEventHandler* handler) = 0;

    // Render info
    virtual void get_dialog_render_info(DialogRenderInfo* out_info) const = 0;
};

class IPopupEventHandler {
public:
    virtual ~IPopupEventHandler() = default;
    virtual void on_popup_opened() = 0;
    virtual void on_popup_closed() = 0;
};

class IGuiPopup : public IGuiWidget {
public:
    virtual ~IGuiPopup() = default;

    // Show/hide
    virtual void show(PopupPlacement placement = PopupPlacement::AtCursor) = 0;
    virtual void show_at(const math::Vec2& position) = 0;
    virtual void show_relative_to(const IGuiWidget* anchor, PopupPlacement placement) = 0;
    virtual void hide() = 0;
    virtual bool is_open() const = 0;

    // Content widget
    virtual IGuiWidget* get_content() const = 0;
    virtual void set_content(IGuiWidget* content) = 0;

    // Auto-close behavior
    virtual bool is_close_on_click_outside() const = 0;
    virtual void set_close_on_click_outside(bool enabled) = 0;
    virtual bool is_close_on_escape() const = 0;
    virtual void set_close_on_escape(bool enabled) = 0;

    // Style
    virtual math::Vec4 get_background_color() const = 0;
    virtual void set_background_color(const math::Vec4& color) = 0;
    virtual math::Vec4 get_border_color() const = 0;
    virtual void set_border_color(const math::Vec4& color) = 0;
    virtual float get_corner_radius() const = 0;
    virtual void set_corner_radius(float radius) = 0;

    // Event handler
    virtual void set_popup_event_handler(IPopupEventHandler* handler) = 0;

    // Render info
    virtual void get_popup_render_info(PopupRenderInfo* out_info) const = 0;
};

// ============================================================================
// Menu / Context Menu Interface
// ============================================================================

enum class MenuItemType : uint8_t {
    Normal = 0,
    Checkbox,
    Radio,
    Separator,
    Submenu
};

struct MenuStyle {
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 item_text_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_hover_text_color;
    math::Vec4 item_disabled_text_color;
    math::Vec4 separator_color;
    math::Vec4 shortcut_text_color;
    math::Vec4 check_color;
    math::Vec4 submenu_arrow_color;
    math::Vec4 shadow_color;
    float item_height = 26.0f;
    float separator_height = 7.0f;
    float item_padding = 24.0f;
    float icon_size = 16.0f;
    float icon_column_width = 28.0f;
    float shortcut_margin = 40.0f;
    float submenu_arrow_size = 8.0f;
    float corner_radius = 4.0f;
    float border_width = 1.0f;
    float shadow_offset = 2.0f;
    float shadow_blur = 6.0f;
    float min_width = 140.0f;
    float font_size = 13.0f;

    static MenuStyle default_style() {
        MenuStyle s;
        s.background_color = color_rgba8(37, 37, 38);
        s.border_color = color_rgba8(63, 63, 70);
        s.item_text_color = color_rgba8(241, 241, 241);
        s.item_hover_background = color_rgba8(0, 122, 204);
        s.item_hover_text_color = color_rgba8(255, 255, 255);
        s.item_disabled_text_color = color_rgba8(110, 110, 110);
        s.separator_color = color_rgba8(63, 63, 70);
        s.shortcut_text_color = color_rgba8(160, 160, 160);
        s.check_color = color_rgba8(0, 122, 204);
        s.submenu_arrow_color = color_rgba8(160, 160, 160);
        s.shadow_color = color_rgba8(0, 0, 0, 100);
        return s;
    }
};

struct MenuItemRenderInfo {
    int item_id = -1;
    MenuItemType type = MenuItemType::Normal;
    const char* text = nullptr;
    const char* icon_name = nullptr;
    const char* shortcut_text = nullptr;
    bool enabled = true;
    bool checked = false;
    bool hovered = false;
    bool has_submenu = false;
    bool submenu_open = false;
    math::Box row_rect;
    math::Box icon_rect;
    math::Box text_rect;
    math::Box shortcut_rect;
    math::Box submenu_arrow_rect;
};

struct MenuRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    MenuStyle style;
    int item_count = 0;
    bool is_open = false;
};

class IMenuEventHandler {
public:
    virtual ~IMenuEventHandler() = default;
    virtual void on_menu_item_clicked(int item_id) = 0;
    virtual void on_menu_opened() = 0;
    virtual void on_menu_closed() = 0;
};

class IGuiMenu : public IGuiWidget {
public:
    virtual ~IGuiMenu() = default;

    // Item management
    virtual int add_item(const char* text, const char* icon_name = nullptr, const char* shortcut = nullptr) = 0;
    virtual int add_checkbox_item(const char* text, bool checked = false) = 0;
    virtual int add_radio_item(const char* text, int group_id, bool checked = false) = 0;
    virtual int add_separator() = 0;
    virtual int add_submenu(const char* text, IGuiMenu* submenu) = 0;
    virtual int insert_item(int index, const char* text, const char* icon_name = nullptr, const char* shortcut = nullptr) = 0;
    virtual bool remove_item(int item_id) = 0;
    virtual void clear_items() = 0;
    virtual int get_item_count() const = 0;

    // Item info
    virtual const char* get_item_text(int item_id) const = 0;
    virtual void set_item_text(int item_id, const char* text) = 0;
    virtual const char* get_item_icon(int item_id) const = 0;
    virtual void set_item_icon(int item_id, const char* icon_name) = 0;
    virtual const char* get_item_shortcut(int item_id) const = 0;
    virtual void set_item_shortcut(int item_id, const char* shortcut) = 0;
    virtual MenuItemType get_item_type(int item_id) const = 0;

    // Item enable/disable
    virtual bool is_item_enabled(int item_id) const = 0;
    virtual void set_item_enabled(int item_id, bool enabled) = 0;

    // Checkbox / radio state
    virtual bool is_item_checked(int item_id) const = 0;
    virtual void set_item_checked(int item_id, bool checked) = 0;

    // Submenu access
    virtual IGuiMenu* get_submenu(int item_id) const = 0;

    // Show / hide (context menu usage)
    virtual void show_at(const math::Vec2& position) = 0;
    virtual void show_relative_to(const IGuiWidget* anchor, PopupPlacement placement) = 0;
    virtual void hide() = 0;
    virtual bool is_open() const = 0;

    // Item user data
    virtual void set_item_user_data(int item_id, void* data) = 0;
    virtual void* get_item_user_data(int item_id) const = 0;

    // Style
    virtual const MenuStyle& get_menu_style() const = 0;
    virtual void set_menu_style(const MenuStyle& style) = 0;

    // Event handler
    virtual void set_menu_event_handler(IMenuEventHandler* handler) = 0;

    // Render info
    virtual void get_menu_render_info(MenuRenderInfo* out_info) const = 0;
    virtual int get_visible_menu_items(MenuItemRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// MenuBar Interface - Horizontal menu bar
// ============================================================================

struct MenuBarStyle {
    math::Vec4 background_color;
    math::Vec4 item_text_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_hover_text_color;
    math::Vec4 item_open_background;
    float height = 28.0f;
    float item_padding = 10.0f;
    float font_size = 13.0f;

    static MenuBarStyle default_style() {
        MenuBarStyle s;
        s.background_color = color_rgba8(45, 45, 48);
        s.item_text_color = color_rgba8(241, 241, 241);
        s.item_hover_background = color_rgba8(62, 62, 66);
        s.item_hover_text_color = color_rgba8(255, 255, 255);
        s.item_open_background = color_rgba8(37, 37, 38);
        return s;
    }
};

struct MenuBarItemRenderInfo {
    int item_id = -1;
    const char* text = nullptr;
    bool hovered = false;
    bool open = false;
    bool enabled = true;
    math::Box item_rect;
    math::Box text_rect;
};

struct MenuBarRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    MenuBarStyle style;
    int item_count = 0;
};

class IGuiMenuBar : public IGuiWidget {
public:
    virtual ~IGuiMenuBar() = default;

    // Menu management
    virtual int add_menu(const char* text, IGuiMenu* menu) = 0;
    virtual int insert_menu(int index, const char* text, IGuiMenu* menu) = 0;
    virtual bool remove_menu(int item_id) = 0;
    virtual void clear_menus() = 0;
    virtual int get_menu_count() const = 0;

    // Menu info
    virtual const char* get_menu_text(int item_id) const = 0;
    virtual void set_menu_text(int item_id, const char* text) = 0;
    virtual IGuiMenu* get_menu(int item_id) const = 0;

    // Menu enable/disable
    virtual bool is_menu_enabled(int item_id) const = 0;
    virtual void set_menu_enabled(int item_id, bool enabled) = 0;

    // Style
    virtual const MenuBarStyle& get_menu_bar_style() const = 0;
    virtual void set_menu_bar_style(const MenuBarStyle& style) = 0;

    // Render info
    virtual void get_menu_bar_render_info(MenuBarRenderInfo* out_info) const = 0;
    virtual int get_visible_menu_bar_items(MenuBarItemRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// Toolbar Interface - Horizontal/vertical tool button strip
// ============================================================================

enum class ToolbarOrientation : uint8_t {
    Horizontal = 0,
    Vertical
};

enum class ToolbarItemType : uint8_t {
    Button = 0,
    ToggleButton,
    Separator,
    Widget          // Embedded custom widget
};

struct ToolbarStyle {
    math::Vec4 background_color;
    math::Vec4 button_color;
    math::Vec4 button_hover_color;
    math::Vec4 button_pressed_color;
    math::Vec4 button_toggled_color;
    math::Vec4 button_disabled_color;
    math::Vec4 icon_color;
    math::Vec4 icon_disabled_color;
    math::Vec4 separator_color;
    math::Vec4 overflow_button_color;
    float button_size = 28.0f;
    float icon_size = 16.0f;
    float separator_width = 1.0f;
    float separator_padding = 4.0f;
    float button_padding = 2.0f;
    float button_corner_radius = 4.0f;
    float toolbar_padding = 4.0f;

    static ToolbarStyle default_style() {
        ToolbarStyle s;
        s.background_color = color_rgba8(45, 45, 48);
        s.button_color = color_rgba8(45, 45, 48, 0);
        s.button_hover_color = color_rgba8(62, 62, 66);
        s.button_pressed_color = color_rgba8(27, 27, 28);
        s.button_toggled_color = color_rgba8(0, 122, 204, 80);
        s.button_disabled_color = color_rgba8(45, 45, 48, 0);
        s.icon_color = color_rgba8(241, 241, 241);
        s.icon_disabled_color = color_rgba8(110, 110, 110);
        s.separator_color = color_rgba8(63, 63, 70);
        s.overflow_button_color = color_rgba8(80, 80, 80);
        return s;
    }
};

struct ToolbarItemRenderInfo {
    int item_id = -1;
    ToolbarItemType type = ToolbarItemType::Button;
    const char* icon_name = nullptr;
    const char* tooltip_text = nullptr;
    bool enabled = true;
    bool toggled = false;
    bool hovered = false;
    bool pressed = false;
    math::Box item_rect;
    math::Box icon_rect;
};

struct ToolbarRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    ToolbarStyle style;
    ToolbarOrientation orientation = ToolbarOrientation::Horizontal;
    int item_count = 0;
    int visible_item_count = 0;
    bool has_overflow = false;
    math::Box overflow_button_rect;
};

class IToolbarEventHandler {
public:
    virtual ~IToolbarEventHandler() = default;
    virtual void on_toolbar_item_clicked(int item_id) = 0;
    virtual void on_toolbar_item_toggled(int item_id, bool toggled) = 0;
};

class IGuiToolbar : public IGuiWidget {
public:
    virtual ~IGuiToolbar() = default;

    // Item management
    virtual int add_button(const char* icon_name, const char* tooltip = nullptr) = 0;
    virtual int add_toggle_button(const char* icon_name, const char* tooltip = nullptr, bool toggled = false) = 0;
    virtual int add_separator() = 0;
    virtual int add_widget_item(IGuiWidget* widget) = 0;
    virtual int insert_button(int index, const char* icon_name, const char* tooltip = nullptr) = 0;
    virtual bool remove_item(int item_id) = 0;
    virtual void clear_items() = 0;
    virtual int get_item_count() const = 0;

    // Item info
    virtual ToolbarItemType get_item_type(int item_id) const = 0;
    virtual const char* get_item_icon(int item_id) const = 0;
    virtual void set_item_icon(int item_id, const char* icon_name) = 0;
    virtual const char* get_item_tooltip(int item_id) const = 0;
    virtual void set_item_tooltip(int item_id, const char* tooltip) = 0;

    // Item enable/disable
    virtual bool is_item_enabled(int item_id) const = 0;
    virtual void set_item_enabled(int item_id, bool enabled) = 0;

    // Toggle state
    virtual bool is_item_toggled(int item_id) const = 0;
    virtual void set_item_toggled(int item_id, bool toggled) = 0;

    // Embedded widget access
    virtual IGuiWidget* get_item_widget(int item_id) const = 0;

    // Orientation
    virtual ToolbarOrientation get_orientation() const = 0;
    virtual void set_orientation(ToolbarOrientation orientation) = 0;

    // Overflow (items that don't fit)
    virtual bool is_overflow_enabled() const = 0;
    virtual void set_overflow_enabled(bool enabled) = 0;
    virtual IGuiMenu* get_overflow_menu() const = 0;

    // Item user data
    virtual void set_item_user_data(int item_id, void* data) = 0;
    virtual void* get_item_user_data(int item_id) const = 0;

    // Style
    virtual const ToolbarStyle& get_toolbar_style() const = 0;
    virtual void set_toolbar_style(const ToolbarStyle& style) = 0;

    // Event handler
    virtual void set_toolbar_event_handler(IToolbarEventHandler* handler) = 0;

    // Render info
    virtual void get_toolbar_render_info(ToolbarRenderInfo* out_info) const = 0;
    virtual int get_visible_toolbar_items(ToolbarItemRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// StatusBar Interface - Bottom information strip
// ============================================================================

enum class StatusBarPanelSizeMode : uint8_t {
    Fixed = 0,      // Fixed pixel width
    Auto,           // Size to content
    Fill            // Fill remaining space
};

struct StatusBarStyle {
    math::Vec4 background_color;
    math::Vec4 text_color;
    math::Vec4 separator_color;
    math::Vec4 hover_background;
    math::Vec4 icon_color;
    float height = 24.0f;
    float panel_padding = 8.0f;
    float separator_width = 1.0f;
    float icon_size = 14.0f;
    float font_size = 12.0f;

    static StatusBarStyle default_style() {
        StatusBarStyle s;
        s.background_color = color_rgba8(0, 122, 204);
        s.text_color = color_rgba8(255, 255, 255);
        s.separator_color = color_rgba8(255, 255, 255, 60);
        s.hover_background = color_rgba8(255, 255, 255, 30);
        s.icon_color = color_rgba8(255, 255, 255);
        return s;
    }
};

struct StatusBarPanelRenderInfo {
    int panel_id = -1;
    const char* text = nullptr;
    const char* icon_name = nullptr;
    const char* tooltip_text = nullptr;
    bool clickable = false;
    bool hovered = false;
    math::Box panel_rect;
    math::Box icon_rect;
    math::Box text_rect;
};

struct StatusBarRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    StatusBarStyle style;
    int panel_count = 0;
};

class IStatusBarEventHandler {
public:
    virtual ~IStatusBarEventHandler() = default;
    virtual void on_panel_clicked(int panel_id) = 0;
    virtual void on_panel_double_clicked(int panel_id) = 0;
};

class IGuiStatusBar : public IGuiWidget {
public:
    virtual ~IGuiStatusBar() = default;

    // Panel management
    virtual int add_panel(const char* text = nullptr, StatusBarPanelSizeMode size_mode = StatusBarPanelSizeMode::Auto) = 0;
    virtual int insert_panel(int index, const char* text = nullptr, StatusBarPanelSizeMode size_mode = StatusBarPanelSizeMode::Auto) = 0;
    virtual bool remove_panel(int panel_id) = 0;
    virtual void clear_panels() = 0;
    virtual int get_panel_count() const = 0;

    // Panel info
    virtual const char* get_panel_text(int panel_id) const = 0;
    virtual void set_panel_text(int panel_id, const char* text) = 0;
    virtual const char* get_panel_icon(int panel_id) const = 0;
    virtual void set_panel_icon(int panel_id, const char* icon_name) = 0;
    virtual const char* get_panel_tooltip(int panel_id) const = 0;
    virtual void set_panel_tooltip(int panel_id, const char* tooltip) = 0;

    // Panel sizing
    virtual StatusBarPanelSizeMode get_panel_size_mode(int panel_id) const = 0;
    virtual void set_panel_size_mode(int panel_id, StatusBarPanelSizeMode mode) = 0;
    virtual float get_panel_fixed_width(int panel_id) const = 0;
    virtual void set_panel_fixed_width(int panel_id, float width) = 0;
    virtual float get_panel_min_width(int panel_id) const = 0;
    virtual void set_panel_min_width(int panel_id, float width) = 0;

    // Panel clickable
    virtual bool is_panel_clickable(int panel_id) const = 0;
    virtual void set_panel_clickable(int panel_id, bool clickable) = 0;

    // Panel embedded widget
    virtual IGuiWidget* get_panel_widget(int panel_id) const = 0;
    virtual void set_panel_widget(int panel_id, IGuiWidget* widget) = 0;

    // Panel user data
    virtual void set_panel_user_data(int panel_id, void* data) = 0;
    virtual void* get_panel_user_data(int panel_id) const = 0;

    // Style
    virtual const StatusBarStyle& get_status_bar_style() const = 0;
    virtual void set_status_bar_style(const StatusBarStyle& style) = 0;

    // Event handler
    virtual void set_status_bar_event_handler(IStatusBarEventHandler* handler) = 0;

    // Render info
    virtual void get_status_bar_render_info(StatusBarRenderInfo* out_info) const = 0;
    virtual int get_visible_status_bar_panels(StatusBarPanelRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// SplitPanel Interface - Resizable split container
// ============================================================================

enum class SplitOrientation : uint8_t {
    Horizontal = 0,     // Left | Right
    Vertical            // Top / Bottom
};

enum class SplitSizeUnit : uint8_t {
    Pixels = 0,
    Ratio               // 0.0 - 1.0 of total size
};

struct SplitterStyle {
    math::Vec4 splitter_color;
    math::Vec4 splitter_hover_color;
    math::Vec4 splitter_drag_color;
    math::Vec4 grip_color;
    float splitter_thickness = 4.0f;
    float hit_area_thickness = 8.0f;    // Larger than visual for easier grabbing
    float grip_length = 30.0f;
    float grip_dot_size = 2.0f;
    int grip_dot_count = 3;

    static SplitterStyle default_style() {
        SplitterStyle s;
        s.splitter_color = color_rgba8(45, 45, 48);
        s.splitter_hover_color = color_rgba8(0, 122, 204);
        s.splitter_drag_color = color_rgba8(0, 122, 204);
        s.grip_color = color_rgba8(110, 110, 110);
        return s;
    }
};

struct SplitPanelRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box first_panel_rect;
    math::Box second_panel_rect;
    math::Box splitter_rect;

    SplitterStyle style;
    SplitOrientation orientation = SplitOrientation::Horizontal;
    bool splitter_hovered = false;
    bool splitter_dragging = false;
    float split_position = 0.0f;        // Current position in pixels
    float split_ratio = 0.5f;           // Current ratio 0.0-1.0
};

class ISplitPanelEventHandler {
public:
    virtual ~ISplitPanelEventHandler() = default;
    virtual void on_split_changed(float position, float ratio) = 0;
    virtual void on_split_drag_started() = 0;
    virtual void on_split_drag_ended() = 0;
};

class IGuiSplitPanel : public IGuiWidget {
public:
    virtual ~IGuiSplitPanel() = default;

    // Orientation
    virtual SplitOrientation get_orientation() const = 0;
    virtual void set_orientation(SplitOrientation orientation) = 0;

    // Panel content
    virtual IGuiWidget* get_first_panel() const = 0;
    virtual void set_first_panel(IGuiWidget* widget) = 0;
    virtual IGuiWidget* get_second_panel() const = 0;
    virtual void set_second_panel(IGuiWidget* widget) = 0;

    // Split position
    virtual float get_split_position() const = 0;
    virtual void set_split_position(float position) = 0;
    virtual float get_split_ratio() const = 0;
    virtual void set_split_ratio(float ratio) = 0;
    virtual SplitSizeUnit get_split_unit() const = 0;
    virtual void set_split_unit(SplitSizeUnit unit) = 0;

    // Constraints
    virtual float get_first_min_size() const = 0;
    virtual void set_first_min_size(float size) = 0;
    virtual float get_first_max_size() const = 0;
    virtual void set_first_max_size(float size) = 0;
    virtual float get_second_min_size() const = 0;
    virtual void set_second_min_size(float size) = 0;
    virtual float get_second_max_size() const = 0;
    virtual void set_second_max_size(float size) = 0;

    // Collapse
    virtual bool is_first_collapsed() const = 0;
    virtual void set_first_collapsed(bool collapsed) = 0;
    virtual bool is_second_collapsed() const = 0;
    virtual void set_second_collapsed(bool collapsed) = 0;
    virtual bool is_collapsible() const = 0;
    virtual void set_collapsible(bool collapsible) = 0;

    // Splitter interaction
    virtual bool is_splitter_fixed() const = 0;
    virtual void set_splitter_fixed(bool fixed) = 0;
    virtual bool is_splitter_hovered() const = 0;
    virtual bool is_splitter_dragging() const = 0;

    // Style
    virtual const SplitterStyle& get_splitter_style() const = 0;
    virtual void set_splitter_style(const SplitterStyle& style) = 0;

    // Event handler
    virtual void set_split_event_handler(ISplitPanelEventHandler* handler) = 0;

    // Render info
    virtual void get_split_panel_render_info(SplitPanelRenderInfo* out_info) const = 0;
};

// ============================================================================
// DockPanel Interface - Dockable panel layout system
// ============================================================================

enum class DockZone : uint8_t {
    Center = 0,
    Left,
    Right,
    Top,
    Bottom,
    Float           // Detached floating window
};

enum class DockPanelState : uint8_t {
    Docked = 0,
    Floating,
    AutoHide,       // Collapsed to edge, slides out on hover
    Hidden
};

struct DockPanelStyle {
    math::Vec4 background_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_bar_active_color;
    math::Vec4 title_text_color;
    math::Vec4 title_active_text_color;
    math::Vec4 tab_bar_color;
    math::Vec4 drop_indicator_color;
    math::Vec4 auto_hide_tab_color;
    float title_bar_height = 26.0f;
    float tab_height = 24.0f;
    float auto_hide_tab_width = 24.0f;
    float min_dock_width = 100.0f;
    float min_dock_height = 80.0f;
    float drop_indicator_thickness = 3.0f;
    float font_size = 12.0f;

    static DockPanelStyle default_style() {
        DockPanelStyle s;
        s.background_color = color_rgba8(37, 37, 38);
        s.title_bar_color = color_rgba8(45, 45, 48);
        s.title_bar_active_color = color_rgba8(0, 122, 204);
        s.title_text_color = color_rgba8(160, 160, 160);
        s.title_active_text_color = color_rgba8(255, 255, 255);
        s.tab_bar_color = color_rgba8(37, 37, 38);
        s.drop_indicator_color = color_rgba8(0, 122, 204, 180);
        s.auto_hide_tab_color = color_rgba8(45, 45, 48);
        return s;
    }
};

struct DockPanelRenderInfo {
    int panel_id = -1;
    const char* title = nullptr;
    const char* icon_name = nullptr;
    DockPanelState state = DockPanelState::Docked;
    DockZone zone = DockZone::Center;
    bool active = false;
    bool title_hovered = false;
    math::Box panel_rect;
    math::Box title_bar_rect;
    math::Box content_rect;
    math::Box close_button_rect;
};

struct DockDropIndicatorInfo {
    bool visible = false;
    DockZone target_zone = DockZone::Center;
    math::Box indicator_rect;
    math::Box preview_rect;         // Preview of where panel would dock
};

struct DockLayoutRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box center_rect;         // Center content area after docking

    DockPanelStyle style;
    int docked_panel_count = 0;
    int floating_panel_count = 0;
    int auto_hide_panel_count = 0;

    DockDropIndicatorInfo drop_indicator;
};

class IDockPanelEventHandler {
public:
    virtual ~IDockPanelEventHandler() = default;
    virtual void on_panel_docked(int panel_id, DockZone zone) = 0;
    virtual void on_panel_undocked(int panel_id) = 0;
    virtual void on_panel_floated(int panel_id) = 0;
    virtual void on_panel_closed(int panel_id) = 0;
    virtual void on_panel_activated(int panel_id) = 0;
    virtual void on_layout_changed() = 0;
};

class IGuiDockPanel : public IGuiWidget {
public:
    virtual ~IGuiDockPanel() = default;

    // Panel management
    virtual int add_panel(const char* title, IGuiWidget* content, const char* icon_name = nullptr) = 0;
    virtual bool remove_panel(int panel_id) = 0;
    virtual void clear_panels() = 0;
    virtual int get_panel_count() const = 0;

    // Panel info
    virtual const char* get_panel_title(int panel_id) const = 0;
    virtual void set_panel_title(int panel_id, const char* title) = 0;
    virtual const char* get_panel_icon(int panel_id) const = 0;
    virtual void set_panel_icon(int panel_id, const char* icon_name) = 0;
    virtual IGuiWidget* get_panel_content(int panel_id) const = 0;

    // Docking
    virtual DockZone get_panel_zone(int panel_id) const = 0;
    virtual void dock_panel(int panel_id, DockZone zone) = 0;
    virtual void dock_panel_relative(int panel_id, int target_panel_id, DockZone zone) = 0;
    virtual void dock_panel_as_tab(int panel_id, int target_panel_id) = 0;
    virtual void undock_panel(int panel_id) = 0;

    // Panel state
    virtual DockPanelState get_panel_state(int panel_id) const = 0;
    virtual void set_panel_state(int panel_id, DockPanelState state) = 0;

    // Floating
    virtual void float_panel(int panel_id, const math::Box& bounds) = 0;
    virtual math::Box get_floating_bounds(int panel_id) const = 0;
    virtual void set_floating_bounds(int panel_id, const math::Box& bounds) = 0;

    // Auto-hide
    virtual void auto_hide_panel(int panel_id) = 0;
    virtual bool is_auto_hide_expanded(int panel_id) const = 0;
    virtual void expand_auto_hide(int panel_id) = 0;
    virtual void collapse_auto_hide(int panel_id) = 0;

    // Active panel
    virtual int get_active_panel() const = 0;
    virtual void set_active_panel(int panel_id) = 0;

    // Panel visibility
    virtual bool is_panel_visible(int panel_id) const = 0;
    virtual void set_panel_visible(int panel_id, bool visible) = 0;
    virtual bool is_panel_closable(int panel_id) const = 0;
    virtual void set_panel_closable(int panel_id, bool closable) = 0;

    // Zone sizes
    virtual float get_zone_size(DockZone zone) const = 0;
    virtual void set_zone_size(DockZone zone, float size) = 0;

    // Drag and drop docking
    virtual bool is_drag_docking_enabled() const = 0;
    virtual void set_drag_docking_enabled(bool enabled) = 0;

    // Layout save/restore
    virtual std::string save_layout() const = 0;
    virtual bool load_layout(const char* layout_data) = 0;

    // Center content (the area not occupied by docked panels)
    virtual IGuiWidget* get_center_content() const = 0;
    virtual void set_center_content(IGuiWidget* widget) = 0;

    // Panel user data
    virtual void set_panel_user_data(int panel_id, void* data) = 0;
    virtual void* get_panel_user_data(int panel_id) const = 0;

    // Style
    virtual const DockPanelStyle& get_dock_panel_style() const = 0;
    virtual void set_dock_panel_style(const DockPanelStyle& style) = 0;

    // Event handler
    virtual void set_dock_event_handler(IDockPanelEventHandler* handler) = 0;

    // Render info
    virtual void get_dock_layout_render_info(DockLayoutRenderInfo* out_info) const = 0;
    virtual int get_visible_dock_panels(DockPanelRenderInfo* out_items, int max_items) const = 0;
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

    // Get image-specific render info
    virtual void get_image_render_info(ImageRenderInfo* out_info) const = 0;
};

// ============================================================================
// Context Interface (Abstract) - Cross-Window UI Management
// ============================================================================

class IGuiContext {
public:
    virtual ~IGuiContext() = default;

    // Lifecycle
    virtual GuiResult initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Frame management
    virtual void begin_frame(float delta_time) = 0;
    virtual void end_frame() = 0;

    // Viewport management (for multi-window support)
    virtual GuiResult add_viewport(const Viewport& viewport) = 0;
    virtual GuiResult remove_viewport(int viewport_id) = 0;
    virtual GuiResult update_viewport(const Viewport& viewport) = 0;
    virtual const Viewport* get_viewport(int viewport_id) const = 0;

    // Input (specify which viewport receives input)
    virtual void set_input_state(int viewport_id, const GuiInputState& state) = 0;
    virtual const GuiInputState& get_input_state() const = 0;

    // Root widget
    virtual IGuiWidget* get_root() = 0;

    // Focus management
    virtual IGuiWidget* get_focused_widget() const = 0;
    virtual void set_focused_widget(IGuiWidget* widget) = 0;
    virtual void clear_focus() = 0;

    // Get widgets visible in a viewport (renderer uses this)
    virtual IWidgetIterator* get_visible_widgets(int viewport_id) = 0;

    // Get all widgets that intersect a rect
    virtual void get_widgets_in_box(const math::Box& box, std::vector<IGuiWidget*>& out_widgets) = 0;

    // Text measurement
    virtual void set_text_measurer(ITextMeasurer* measurer) = 0;
    virtual ITextMeasurer* get_text_measurer() const = 0;

    // Style
    virtual const GuiStyle& get_default_style() const = 0;
    virtual void set_default_style(const GuiStyle& style) = 0;
    virtual const LabelStyle& get_default_label_style() const = 0;
    virtual void set_default_label_style(const LabelStyle& style) = 0;

    // Widget creation (factory methods)
    virtual IGuiWidget* create_widget(WidgetType type) = 0;
    virtual IGuiLabel* create_label(const char* text = nullptr) = 0;
    virtual IGuiTextInput* create_text_input(const char* placeholder = nullptr) = 0;
    virtual IGuiImage* create_image(const std::string& image_name = "") = 0;
    virtual IGuiScrollView* create_scroll_view() = 0;
    virtual IGuiScrollBar* create_scroll_bar(ScrollBarOrientation orientation = ScrollBarOrientation::Vertical) = 0;
    virtual IGuiPropertyGrid* create_property_grid() = 0;
    virtual IGuiTreeView* create_tree_view() = 0;
    virtual IGuiTabControl* create_tab_control(TabPosition position = TabPosition::Top) = 0;
    virtual IGuiListBox* create_list_box() = 0;
    virtual IGuiComboBox* create_combo_box() = 0;
    virtual IGuiDialog* create_dialog(const char* title = nullptr, DialogButtons buttons = DialogButtons::OK) = 0;
    virtual IGuiPopup* create_popup() = 0;
    virtual IGuiMenu* create_menu() = 0;
    virtual IGuiMenuBar* create_menu_bar() = 0;
    virtual IGuiToolbar* create_toolbar(ToolbarOrientation orientation = ToolbarOrientation::Horizontal) = 0;
    virtual IGuiStatusBar* create_status_bar() = 0;
    virtual IGuiSplitPanel* create_split_panel(SplitOrientation orientation = SplitOrientation::Horizontal) = 0;
    virtual IGuiDockPanel* create_dock_panel() = 0;
    virtual IGuiSlider* create_slider(SliderOrientation orientation = SliderOrientation::Horizontal) = 0;
    virtual IGuiProgressBar* create_progress_bar(ProgressBarMode mode = ProgressBarMode::Determinate) = 0;
    virtual IGuiColorPicker* create_color_picker(ColorPickerMode mode = ColorPickerMode::HSVSquare) = 0;
    virtual void destroy_widget(IGuiWidget* widget) = 0;

    // Modal handling
    virtual void push_modal(IGuiWidget* widget) = 0;
    virtual void pop_modal() = 0;
    virtual IGuiWidget* get_modal() const = 0;

    // Tooltip (creates a temporary label)
    virtual void show_tooltip(const char* text, const math::Vec2& position) = 0;
    virtual void hide_tooltip() = 0;

    // Debug
    virtual void set_debug_draw(bool enabled) = 0;
    virtual bool is_debug_draw_enabled() const = 0;
};

// ============================================================================
// Theme Interface (Abstract)
// ============================================================================

class IGuiTheme {
public:
    virtual ~IGuiTheme() = default;

    virtual const char* get_name() const = 0;
    virtual GuiStyle get_style_for(WidgetType type, WidgetState state) const = 0;
    virtual LabelStyle get_label_style_for(WidgetType type, WidgetState state) const = 0;
    virtual math::Vec4 get_color(const char* name) const = 0;
    virtual float get_metric(const char* name) const = 0;
};

// ============================================================================
// Layout Interface (Abstract)
// ============================================================================

class IGuiLayout {
public:
    virtual ~IGuiLayout() = default;

    virtual void apply(IGuiWidget* widget) = 0;
    virtual math::Vec2 calculate_size(IGuiWidget* widget) const = 0;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* gui_result_to_string(GuiResult result);
const char* widget_type_to_string(WidgetType type);
const char* widget_state_to_string(WidgetState state);
const char* gui_event_type_to_string(GuiEventType type);

// ============================================================================
// Factory Functions
// ============================================================================

IGuiContext* create_gui_context(GuiResult* out_result = nullptr);
void destroy_gui_context(IGuiContext* context);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_HPP
