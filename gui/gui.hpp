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
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include "../math_util.hpp"

namespace window {

// Forward declaration for Window class
class Window;

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
    math::Box bounds;               // Viewport bounds in UI coordinate space
    float scale = 1.0f;             // UI scale factor
    math::Vec2 offset;              // Scroll/pan offset

    // Transform point from viewport to UI coordinates
    math::Vec2 to_ui(const math::Vec2& p) const;

    // Transform point from UI to viewport coordinates
    math::Vec2 to_viewport(const math::Vec2& p) const;

    // Get visible box in UI coordinates
    math::Box get_visible_box() const;
};

// ============================================================================
// Widget Render Info - Texture list for rendering
// ============================================================================

// Texture source type
enum class TextureSourceType : uint8_t {
    File = 0,       // File path
    Memory,         // Memory data pointer
    Generated       // Runtime generated (solid color, gradient, etc.)
};

// 9-slice (9-patch) center behavior
enum class SliceCenterMode : uint8_t {
    Stretch = 0,    // Stretch center region
    Tile,           // Tile center region
    Hidden          // Don't draw center (for frames)
};

// 9-slice border definition (in pixels or UV units depending on use)
struct SliceBorder {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool is_zero() const;
    static SliceBorder uniform(float size);
};

// Single texture entry for rendering
struct TextureEntry {
    // Texture source
    TextureSourceType source_type = TextureSourceType::File;
    const char* file_path = nullptr;        // For File type
    const void* memory_data = nullptr;      // For Memory type
    size_t memory_size = 0;                 // For Memory type

    // Depth/z-order for sorting (lower = further back, higher = closer to front)
    int32_t depth = 0;

    // Destination rectangle (screen coordinates)
    math::Box dest_rect;

    // UV coordinates (0.0-1.0)
    math::Box uv_rect = math::make_box(0.0f, 0.0f, 1.0f, 1.0f);

    // Tint color (RGBA 0.0-1.0)
    math::Vec4 tint = math::Vec4(1.0f, 1.0f, 1.0f, 1.0f);

    // Clipping rectangle
    math::Box clip_rect;

    // For generated textures (solid color fill)
    math::Vec4 solid_color;

    // 9-slice rendering
    bool use_slice9 = false;                // Enable 9-slice rendering
    SliceBorder slice_border;               // Border sizes in pixels (source texture)
    SliceCenterMode slice_center_mode = SliceCenterMode::Stretch;

    // Check if two entries use the same texture source
    bool same_texture(const TextureEntry& other) const;
};

// Render batch - group of entries with same texture for efficient rendering
struct RenderBatch {
    TextureSourceType source_type = TextureSourceType::File;
    const char* file_path = nullptr;        // For File type
    const void* memory_data = nullptr;      // For Memory type
    size_t memory_size = 0;                 // For Memory type
    int32_t start_index = 0;                // Start index in sorted entries
    int32_t count = 0;                      // Number of entries in this batch
};

struct WidgetRenderInfo {
    // List of texture entries (unsorted, call sort_and_batch() before rendering)
    std::vector<TextureEntry> textures;

    // Clipping rectangle for the entire widget
    math::Box clip_rect;

    // Sort entries by depth (back to front), then batch by texture source
    // After calling this, textures are sorted and batches are populated
    void sort_and_batch();

    // Get batches (valid after sort_and_batch())
    const std::vector<RenderBatch>& get_batches() const { return batches; }

    // Clear all data
    void clear() {
        textures.clear();
        batches.clear();
    }

private:
    std::vector<RenderBatch> batches;
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
    virtual void get_render_info(Window* window, WidgetRenderInfo* out_info) const = 0;

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

} // namespace gui
} // namespace window

// Include all GUI component headers (after base types are defined)
#include "gui_label.hpp"        // IGuiLabel, IGuiTextInput, IGuiEditBox
#include "gui_scroll.hpp"       // IGuiScrollView, IGuiScrollBar
#include "gui_property.hpp"     // IGuiPropertyGrid
#include "gui_tree.hpp"         // IGuiTreeView
#include "gui_tab.hpp"          // IGuiTabControl
#include "gui_list.hpp"         // IGuiListBox, IGuiComboBox
#include "gui_dialog.hpp"       // IGuiDialog, IGuiPopup
#include "gui_menu.hpp"         // IGuiMenu, IGuiMenuBar
#include "gui_toolbar.hpp"      // IGuiToolbar, IGuiStatusBar
#include "gui_panel.hpp"        // IGuiSplitPanel, IGuiDockPanel
#include "gui_controls.hpp"     // IGuiButton, IGuiSlider, IGuiProgressBar, IGuiColorPicker, IGuiImage
#include "gui_animation.hpp"    // IGuiAnimation, IGuiAnimationManager
#include "gui_page.hpp"         // IGuiPage, IGuiPageView
#include "gui_context.hpp"      // IGuiContext, IGuiTheme, IGuiLayout, factory functions

#endif // WINDOW_GUI_HPP
