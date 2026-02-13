/*
 * gui_list.hpp - ListBox and ComboBox Interfaces
 *
 * Contains IGuiListBox for selectable item lists and
 * IGuiComboBox for dropdown selection.
 */

#ifndef WINDOW_GUI_LIST_HPP
#define WINDOW_GUI_LIST_HPP

namespace window {
namespace gui {

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

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_LIST_HPP
