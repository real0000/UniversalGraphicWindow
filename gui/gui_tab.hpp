/*
 * gui_tab.hpp - TabControl Interface
 *
 * Contains IGuiTabControl for tabbed containers.
 */

#ifndef WINDOW_GUI_TAB_HPP
#define WINDOW_GUI_TAB_HPP

namespace window {
namespace gui {

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

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_TAB_HPP
