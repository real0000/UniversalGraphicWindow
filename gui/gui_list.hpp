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

// ListBoxStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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

// Declarative row model for IGuiListBox::set_items — the app hands the widget a
// whole list and the widget diffs/rebuilds/self-renders (no app-side row widgets).
// selected is DATA (the app owns which id is selected); swatch/has_action decorate.
struct ListItemModel {
    int        id = -1;
    std::string text;
    math::Vec4 swatch = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);  // leading dot; alpha 0 = none
    bool       editable = false;      // reserved: inline rename
    bool       has_action = false;    // trailing "×" (delete) affordance
    bool       selected = false;
    bool       enabled = true;
    math::Vec4 text_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);  // per-row override; alpha 0 = style default
};

class IListBoxEventHandler {
public:
    virtual ~IListBoxEventHandler() = default;
    virtual void on_item_selected(int item_id) = 0;
    virtual void on_item_double_clicked(int item_id) = 0;
    // Trailing action ("×") on a row was clicked (model has_action == true). Default
    // no-op so existing handlers keep compiling.
    virtual void on_item_action(int item_id) { (void)item_id; }
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

    // Model-driven population: replace the whole list from a declarative model, in
    // one idempotent call (an unchanged model schedules no repaint — the app may
    // rebuild + push on every event, mirroring IGuiCanvasView::set_nodes). The
    // widget owns row rendering; the app never positions a row. `selected` is taken
    // from the model, so the app owns selection as data.
    virtual void set_items(const std::vector<ListItemModel>& items) = 0;

    // Bind a data provider (set ONCE): the list re-reads it before each render
    // (via refresh_bindings) and set_items() the result — so the app mutates its
    // own data + asks for a repaint, and the list reacts. Pass {} to clear.
    virtual void bind(std::function<std::vector<ListItemModel>()> provider) = 0;

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
    virtual float get_scroll_offset() const = 0;
    virtual void set_scroll_offset(float offset) = 0;
    virtual float get_total_content_height() const = 0;
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

// ComboBoxStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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
