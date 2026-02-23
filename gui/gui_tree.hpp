/*
 * gui_tree.hpp - TreeView Interface
 *
 * Contains IGuiTreeView for hierarchical node display.
 */

#ifndef WINDOW_GUI_TREE_HPP
#define WINDOW_GUI_TREE_HPP

namespace window {
namespace gui {

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
    virtual float get_scroll_offset() const = 0;
    virtual void set_scroll_offset(float offset) = 0;
    virtual float get_total_content_height() const = 0;

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

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_TREE_HPP
