/*
 * gui_toolbar.hpp - Toolbar and StatusBar Interfaces
 *
 * Contains IGuiToolbar for tool button strips and IGuiStatusBar for status panels.
 */

#ifndef WINDOW_GUI_TOOLBAR_HPP
#define WINDOW_GUI_TOOLBAR_HPP

namespace window {
namespace gui {

// Forward declarations
class IGuiMenu;

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

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_TOOLBAR_HPP
