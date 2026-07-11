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

// MenuStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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

    // Semantic driving (automation / scripting): fire on_menu_item_clicked exactly
    // like clicking the item (enabled items only). False if the id isn't present.
    virtual bool activate_item(int item_id) = 0;

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

// MenuBarStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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

    // Close any currently open dropdown (call from on_menu_closed to sync state)
    virtual void close_all() = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_MENU_HPP
