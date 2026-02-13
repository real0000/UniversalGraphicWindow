/*
 * gui_menu.hpp - Menu and MenuBar Interfaces
 *
 * Contains IGuiMenu for context menus and IGuiMenuBar for horizontal menu bars.
 */

#ifndef WINDOW_GUI_MENU_HPP
#define WINDOW_GUI_MENU_HPP

// Note: Requires gui_dialog.hpp to be included before this header (for PopupPlacement)

namespace window {
namespace gui {

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

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_MENU_HPP
