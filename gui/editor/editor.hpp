/*
 * editor.hpp - GUI Editor Public API
 *
 * The GUI Editor allows visual design of GUI layouts built on the existing
 * widget system. It provides:
 *   - Menu bar: File, Edit, View, Widget, About
 *   - Widget hierarchy tree with context menu (add/delete/rename)
 *   - Property grid inspector for editing widget properties
 *   - Canvas view window to preview/select widgets with zoom/pan
 *   - Undo/redo for all editing operations
 *   - Save/load via the GUI serialization system (JSON and binary)
 */

#ifndef GUI_EDITOR_HPP
#define GUI_EDITOR_HPP

#include "gui/gui.hpp"
#include "gui/gui_serialization.hpp"
#include "editor_commands.hpp"
#include "editor_canvas.hpp"
#include "editor_palette.hpp"
#include "editor_inspector.hpp"
#include <string>
#include <functional>

namespace window {

class Window;

namespace gui {
namespace editor {

using EditorSelectionCallback = std::function<void(IGuiWidget* selected)>;
using EditorDirtyCallback = std::function<void(bool dirty)>;

// ============================================================================
// MenuForwarder -- forwards menu item clicks with offset to disambiguate menus
// ============================================================================

class GuiEditor; // forward

class MenuForwarder : public IMenuEventHandler {
public:
    GuiEditor* editor = nullptr;
    int offset = 0;
    void on_menu_item_clicked(int item_id) override;
    void on_menu_opened() override {}
    void on_menu_closed() override;
};

// ============================================================================
// GuiEditor -- main editor class
// ============================================================================

class GuiEditor : public ITreeViewEventHandler,
                  public IToolbarEventHandler,
                  public IStatusBarEventHandler {
public:
    GuiEditor() = default;
    ~GuiEditor();

    bool initialize(IGuiContext* ctx, Window* window);
    void shutdown();
    bool is_initialized() const { return m_initialized; }

    void update(float delta_time);
    const WidgetRenderInfo& get_render_info(Window* window);

    IGuiContext* get_editor_context() const { return m_editor_ctx; }
    IGuiContext* get_design_context() const { return m_design_ctx; }
    IGuiWidget* get_design_root() const;

    EditorCanvas& get_canvas() { return m_canvas; }
    EditorPalette& get_palette() { return m_palette; }
    EditorInspector& get_inspector() { return m_inspector; }
    CommandHistory& get_command_history() { return m_history; }

    bool new_file();
    bool open_file(const char* filepath = nullptr);
    bool save_file();
    bool save_file_as(const char* filepath = nullptr);
    const char* get_current_filepath() const { return m_filepath.empty() ? nullptr : m_filepath.c_str(); }
    bool is_dirty() const { return m_history.is_dirty(); }

    void undo();
    void redo();

    void set_selection_callback(EditorSelectionCallback cb) { m_on_selection = std::move(cb); }
    void set_dirty_callback(EditorDirtyCallback cb) { m_on_dirty = std::move(cb); }

    void delete_selected();
    void create_widget(WidgetType type, const math::Vec2& position);
    void set_status_message(const char* msg);

    // Called after canvas mouse interactions (select/move/resize) to sync
    // the inspector and hierarchy with the current canvas state.
    void on_canvas_interaction();

    // Called by MenuForwarder with offset+item_id
    void handle_menu_item(int global_id);
    // Called by MenuForwarder::on_menu_closed to keep menubar state in sync
    void sync_menubar_close();

    // ITreeViewEventHandler
    void on_node_selected(int node_id) override;
    void on_node_expanded(int node_id, bool expanded) override;
    void on_node_double_clicked(int node_id) override;
    void on_right_click(const math::Vec2& pos) override;
    void on_node_moved(int dragged_id, int new_parent_id, int before_id) override;

    // IToolbarEventHandler
    void on_toolbar_item_clicked(int item_id) override;
    void on_toolbar_item_toggled(int item_id, bool toggled) override;

    // IStatusBarEventHandler
    void on_panel_clicked(int panel_id) override {}
    void on_panel_double_clicked(int panel_id) override {}

    // Panel width access for splitter drag in host
    float get_tree_panel_w()   const { return m_tree_panel_w; }
    float get_inspector_w()    const { return m_inspector_w; }
    void  set_tree_panel_w(float w)  { m_tree_panel_w = std::max(100.0f, std::min(w, 500.0f)); }
    void  set_inspector_w(float w)   { m_inspector_w  = std::max(160.0f, std::min(w, 600.0f)); }

    // Exposed layout heights for host rendering
    static constexpr float MENUBAR_H    = 26.0f;
    static constexpr float TOOLBAR_H    = 32.0f;
    static constexpr float STATUSBAR_H  = 24.0f;

private:
    void build_editor_ui();
    void build_menubar();
    void build_toolbar();
    void build_hierarchy_panel();
    void build_status_bar();
    void layout_editor(float w, float h);
    void refresh_hierarchy();
    void refresh_hierarchy_recursive(IGuiWidget* widget, int parent_node_id);
    void on_selection_changed();
    void update_title();
    void create_widget_from_menu(WidgetType type);

    bool m_initialized = false;

    IGuiContext* m_editor_ctx = nullptr;
    IGuiContext* m_design_ctx = nullptr;
    Window* m_window = nullptr;

    EditorCanvas m_canvas;
    EditorPalette m_palette;
    EditorInspector m_inspector;
    CommandHistory m_history;

    // Editor UI widgets
    IGuiWidget* m_editor_root = nullptr;
    IGuiMenuBar* m_menubar = nullptr;
    IGuiToolbar* m_toolbar = nullptr;
    IGuiWidget* m_hierarchy_container = nullptr;
    IGuiTreeView* m_hierarchy_tree = nullptr;
    IGuiWidget* m_canvas_panel = nullptr;
    IGuiWidget* m_inspector_container = nullptr;
    IGuiStatusBar* m_status_bar = nullptr;
    IGuiMenu* m_tree_context_menu = nullptr;

    std::string m_filepath;
    GuiSerializeFormat m_format = GuiSerializeFormat::Json;

    EditorSelectionCallback m_on_selection;
    EditorDirtyCallback m_on_dirty;

    // Per-menu forwarding handlers (each adds offset to item IDs)
    static constexpr int MENU_FILE    = 1000;
    static constexpr int MENU_EDIT    = 2000;
    static constexpr int MENU_VIEW    = 3000;
    static constexpr int MENU_WIDGET  = 4000;
    static constexpr int MENU_ABOUT   = 5000;
    static constexpr int MENU_CONTEXT = 6000;

    MenuForwarder m_fwd_file, m_fwd_edit, m_fwd_view, m_fwd_widget, m_fwd_about, m_fwd_context;

    // Panel widths — mutable for splitter dragging
    float m_tree_panel_w = 220.0f;
    float m_inspector_w  = 280.0f;

    // Toolbar item IDs
    int m_tb_new = -1, m_tb_open = -1, m_tb_save = -1;
    int m_tb_undo = -1, m_tb_redo = -1;
    int m_tb_delete = -1;
    int m_tb_zoom_fit = -1;
    int m_tb_grid = -1, m_tb_snap = -1;

    // Menu item IDs (stored as offset + local_id)
    // File
    int m_mi_new = -1, m_mi_open = -1, m_mi_save = -1, m_mi_save_as = -1;
    // Edit
    int m_mi_undo = -1, m_mi_redo = -1;
    int m_mi_cut = -1, m_mi_copy = -1, m_mi_paste = -1, m_mi_delete = -1;
    int m_mi_select_all = -1;
    // View
    int m_mi_zoom_fit = -1, m_mi_zoom_reset = -1;
    int m_mi_grid = -1, m_mi_snap = -1;
    // Widget
    int m_mi_add_container = -1, m_mi_add_panel = -1, m_mi_add_button = -1;
    int m_mi_add_label = -1, m_mi_add_textinput = -1, m_mi_add_checkbox = -1;
    int m_mi_add_radio = -1, m_mi_add_slider = -1, m_mi_add_progress = -1;
    int m_mi_add_image = -1, m_mi_add_listbox = -1, m_mi_add_combobox = -1;
    int m_mi_add_scrollarea = -1, m_mi_add_tabcontrol = -1, m_mi_add_treeview = -1;
    int m_mi_add_separator = -1, m_mi_add_spacer = -1;
    // About
    int m_mi_about = -1;
    // Context menu
    int m_cm_add_container = -1, m_cm_add_panel = -1, m_cm_add_scrollarea = -1;
    int m_cm_add_tabcontrol = -1;
    int m_cm_add_button = -1, m_cm_add_label = -1, m_cm_add_textinput = -1;
    int m_cm_add_checkbox = -1, m_cm_add_radio = -1;
    int m_cm_add_slider = -1, m_cm_add_progress = -1;
    int m_cm_add_image = -1, m_cm_add_listbox = -1, m_cm_add_combobox = -1;
    int m_cm_add_treeview = -1, m_cm_add_separator = -1, m_cm_add_spacer = -1;
    int m_cm_delete = -1, m_cm_rename = -1;

    // Status bar panel IDs
    int m_sb_message = -1, m_sb_zoom = -1, m_sb_position = -1, m_sb_selection = -1;

    std::vector<IGuiWidget*> m_node_to_widget;
};

} // namespace editor
} // namespace gui
} // namespace window

#endif // GUI_EDITOR_HPP
