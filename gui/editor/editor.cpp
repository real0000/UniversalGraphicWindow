/*
 * editor.cpp - GUI Editor Core Implementation
 */

#include "editor.hpp"
#include "window.hpp"
#include "gui/gui_context.hpp"
#include "gui/gui_panel.hpp"
#include "gui/gui_toolbar.hpp"
#include "gui/gui_menu.hpp"
#include "gui/gui_tree.hpp"
#include <cstdio>
#include <algorithm>

namespace window {
namespace gui {
namespace editor {

using math::x;
using math::y;
using math::box_min;
using math::box_max;
using math::box_width;
using math::box_height;
using math::make_box;

// ============================================================================
// MenuForwarder
// ============================================================================

void MenuForwarder::on_menu_item_clicked(int item_id) {
    if (editor) editor->handle_menu_item(offset + item_id);
}

void MenuForwarder::on_menu_closed() {
    // When the menu auto-closes (e.g. click outside), sync the menubar's
    // open_menu_ tracking variable so the next click opens correctly.
    if (editor) editor->sync_menubar_close();
}

void GuiEditor::sync_menubar_close() {
    if (m_menubar) m_menubar->close_all();
}

// ============================================================================
// GuiEditor lifecycle
// ============================================================================

GuiEditor::~GuiEditor() {
    if (m_initialized) shutdown();
}

bool GuiEditor::initialize(IGuiContext* ctx, Window* window) {
    if (m_initialized) return false;

    m_editor_ctx = ctx;
    m_window = window;

    // Init per-menu forwarders
    m_fwd_file.editor = this;    m_fwd_file.offset = MENU_FILE;
    m_fwd_edit.editor = this;    m_fwd_edit.offset = MENU_EDIT;
    m_fwd_view.editor = this;    m_fwd_view.offset = MENU_VIEW;
    m_fwd_widget.editor = this;  m_fwd_widget.offset = MENU_WIDGET;
    m_fwd_about.editor = this;   m_fwd_about.offset = MENU_ABOUT;
    m_fwd_context.editor = this; m_fwd_context.offset = MENU_CONTEXT;

    // Design context for the widgets being edited.
    // create_gui_context() already calls initialize() internally.
    m_design_ctx = gui::create_gui_context();
    if (!m_design_ctx) return false;

    // Make the design root transparent so only actual widgets are visible.
    // Bounds are set each frame in layout_editor().
    {
        GuiStyle transparent = GuiStyle::default_style();
        transparent.background_color = math::Vec4(0, 0, 0, 0);
        m_design_ctx->get_root()->set_style(transparent);
    }

    // Sub-components
    m_history.clear();
    m_palette.initialize(m_editor_ctx);
    m_inspector.initialize(m_editor_ctx, &m_history);
    m_canvas.initialize(m_editor_ctx, &m_history);
    m_canvas.set_design_root(m_design_ctx->get_root());

    build_editor_ui();

    m_initialized = true;
    set_status_message("Ready");
    return true;
}

void GuiEditor::shutdown() {
    if (!m_initialized) return;

    m_canvas.shutdown();
    m_inspector.shutdown();
    m_palette.shutdown();

    if (m_design_ctx) {
        m_design_ctx->shutdown();
        delete m_design_ctx;
        m_design_ctx = nullptr;
    }

    m_editor_root = nullptr;
    m_menubar = nullptr;
    m_toolbar = nullptr;
    m_hierarchy_container = nullptr;
    m_hierarchy_tree = nullptr;
    m_canvas_panel = nullptr;
    m_inspector_container = nullptr;
    m_status_bar = nullptr;
    m_tree_context_menu = nullptr;
    m_node_to_widget.clear();

    m_initialized = false;
}

// ============================================================================
// Build editor UI
// ============================================================================

void GuiEditor::build_editor_ui() {
    if (!m_editor_ctx) return;

    m_editor_root = m_editor_ctx->get_root();
    if (!m_editor_root) return;

    // Make the editor root transparent. Each child panel draws its own
    // opaque background. The center canvas area is drawn by draw_design_widgets
    // in main.cpp before draw_render_info — an opaque root would erase it.
    {
        GuiStyle rs = GuiStyle::default_style();
        rs.background_color = math::Vec4(0, 0, 0, 0);
        m_editor_root->set_style(rs);
    }

    build_menubar();
    build_toolbar();
    build_hierarchy_panel();

    // Canvas panel (center)
    m_canvas_panel = m_editor_ctx->create_widget(WidgetType::Panel);
    if (m_canvas_panel) {
        m_canvas_panel->set_name("canvas_panel");
        GuiStyle cs = GuiStyle::default_style();
        cs.background_color = math::Vec4(0, 0, 0, 0);  // transparent: canvas drawn separately
        cs.border_width = 0.0f;
        cs.padding = math::Vec4(0.0f);
        m_canvas_panel->set_style(cs);
        m_editor_root->add_child(m_canvas_panel);
    }

    // Inspector panel (right)
    m_inspector_container = m_inspector.get_root_widget();
    if (m_inspector_container) {
        m_editor_root->add_child(m_inspector_container);
    }

    build_status_bar();

    // Context menu for tree view right-click
    m_tree_context_menu = m_editor_ctx->create_menu();
    if (m_tree_context_menu) {
        m_tree_context_menu->set_bounds(make_box(0, 0, 200, 520));
        // Layout
        m_cm_add_container   = MENU_CONTEXT + m_tree_context_menu->add_item("Add Container");
        m_cm_add_panel       = MENU_CONTEXT + m_tree_context_menu->add_item("Add Panel");
        m_cm_add_scrollarea  = MENU_CONTEXT + m_tree_context_menu->add_item("Add Scroll Area");
        m_cm_add_tabcontrol  = MENU_CONTEXT + m_tree_context_menu->add_item("Add Tab Control");
        m_tree_context_menu->add_separator();
        // Basic controls
        m_cm_add_button      = MENU_CONTEXT + m_tree_context_menu->add_item("Add Button");
        m_cm_add_label       = MENU_CONTEXT + m_tree_context_menu->add_item("Add Label");
        m_cm_add_textinput   = MENU_CONTEXT + m_tree_context_menu->add_item("Add Text Input");
        m_cm_add_checkbox    = MENU_CONTEXT + m_tree_context_menu->add_item("Add Checkbox");
        m_cm_add_radio       = MENU_CONTEXT + m_tree_context_menu->add_item("Add Radio Button");
        m_tree_context_menu->add_separator();
        // Value controls
        m_cm_add_slider      = MENU_CONTEXT + m_tree_context_menu->add_item("Add Slider");
        m_cm_add_progress    = MENU_CONTEXT + m_tree_context_menu->add_item("Add Progress Bar");
        m_tree_context_menu->add_separator();
        // List/data controls
        m_cm_add_listbox     = MENU_CONTEXT + m_tree_context_menu->add_item("Add List Box");
        m_cm_add_combobox    = MENU_CONTEXT + m_tree_context_menu->add_item("Add Combo Box");
        m_cm_add_treeview    = MENU_CONTEXT + m_tree_context_menu->add_item("Add Tree View");
        m_tree_context_menu->add_separator();
        // Other
        m_cm_add_image       = MENU_CONTEXT + m_tree_context_menu->add_item("Add Image");
        m_cm_add_separator   = MENU_CONTEXT + m_tree_context_menu->add_item("Add Separator");
        m_cm_add_spacer      = MENU_CONTEXT + m_tree_context_menu->add_item("Add Spacer");
        m_tree_context_menu->add_separator();
        m_cm_rename = MENU_CONTEXT + m_tree_context_menu->add_item("Rename", nullptr, "F2");
        m_cm_delete = MENU_CONTEXT + m_tree_context_menu->add_item("Delete", nullptr, "Del");
        m_tree_context_menu->set_menu_event_handler(&m_fwd_context);
        m_editor_ctx->add_overlay(m_tree_context_menu);
    }
}

void GuiEditor::build_menubar() {
    m_menubar = m_editor_ctx->create_menu_bar();
    if (!m_menubar) return;

    m_menubar->set_name("editor_menubar");
    m_editor_root->add_child(m_menubar);

    // Helper lambda: build a menu, register it in the menubar and as an overlay.
    // Dropdowns are overlays so collect_recursive picks them up last (highest depth),
    // guaranteeing they render on top of all other widgets.
    auto register_menu = [&](IGuiMenu* menu, const char* label) {
        if (!menu) return;
        m_menubar->add_menu(label, menu);
        m_editor_ctx->add_overlay(menu); // rendered after root tree → always on top
    };

    // File menu
    IGuiMenu* file_menu = m_editor_ctx->create_menu();
    if (file_menu) {
        file_menu->set_bounds(make_box(0, 0, 200, 140));
        m_mi_new     = MENU_FILE + file_menu->add_item("New",        nullptr, "Ctrl+N");
        m_mi_open    = MENU_FILE + file_menu->add_item("Open...",    nullptr, "Ctrl+O");
        m_mi_save    = MENU_FILE + file_menu->add_item("Save",       nullptr, "Ctrl+S");
        m_mi_save_as = MENU_FILE + file_menu->add_item("Save As...", nullptr, "Ctrl+Shift+S");
        file_menu->set_menu_event_handler(&m_fwd_file);
        register_menu(file_menu, "File");
    }

    // Edit menu
    IGuiMenu* edit_menu = m_editor_ctx->create_menu();
    if (edit_menu) {
        edit_menu->set_bounds(make_box(0, 0, 200, 280));
        m_mi_undo       = MENU_EDIT + edit_menu->add_item("Undo",       nullptr, "Ctrl+Z");
        m_mi_redo       = MENU_EDIT + edit_menu->add_item("Redo",       nullptr, "Ctrl+Y");
        edit_menu->add_separator();
        m_mi_cut        = MENU_EDIT + edit_menu->add_item("Cut",        nullptr, "Ctrl+X");
        m_mi_copy       = MENU_EDIT + edit_menu->add_item("Copy",       nullptr, "Ctrl+C");
        m_mi_paste      = MENU_EDIT + edit_menu->add_item("Paste",      nullptr, "Ctrl+V");
        m_mi_delete     = MENU_EDIT + edit_menu->add_item("Delete",     nullptr, "Del");
        edit_menu->add_separator();
        m_mi_select_all = MENU_EDIT + edit_menu->add_item("Select All", nullptr, "Ctrl+A");
        edit_menu->set_menu_event_handler(&m_fwd_edit);
        register_menu(edit_menu, "Edit");
    }

    // View menu
    IGuiMenu* view_menu = m_editor_ctx->create_menu();
    if (view_menu) {
        view_menu->set_bounds(make_box(0, 0, 200, 160));
        m_mi_zoom_fit   = MENU_VIEW + view_menu->add_item("Zoom to Fit", nullptr, "Ctrl+0");
        m_mi_zoom_reset = MENU_VIEW + view_menu->add_item("Reset Zoom",  nullptr, "Ctrl+1");
        view_menu->add_separator();
        m_mi_grid = MENU_VIEW + view_menu->add_checkbox_item("Show Grid", true);
        m_mi_snap = MENU_VIEW + view_menu->add_checkbox_item("Snap to Grid", true);
        view_menu->set_menu_event_handler(&m_fwd_view);
        register_menu(view_menu, "View");
    }

    // Widget menu
    IGuiMenu* widget_menu = m_editor_ctx->create_menu();
    if (widget_menu) {
        widget_menu->set_bounds(make_box(0, 0, 200, 500));
        m_mi_add_container  = MENU_WIDGET + widget_menu->add_item("Container");
        m_mi_add_panel      = MENU_WIDGET + widget_menu->add_item("Panel");
        m_mi_add_scrollarea = MENU_WIDGET + widget_menu->add_item("Scroll Area");
        m_mi_add_tabcontrol = MENU_WIDGET + widget_menu->add_item("Tab Control");
        widget_menu->add_separator();
        m_mi_add_button     = MENU_WIDGET + widget_menu->add_item("Button");
        m_mi_add_label      = MENU_WIDGET + widget_menu->add_item("Label");
        m_mi_add_textinput  = MENU_WIDGET + widget_menu->add_item("Text Input");
        m_mi_add_checkbox   = MENU_WIDGET + widget_menu->add_item("Checkbox");
        m_mi_add_radio      = MENU_WIDGET + widget_menu->add_item("Radio Button");
        m_mi_add_slider     = MENU_WIDGET + widget_menu->add_item("Slider");
        m_mi_add_progress   = MENU_WIDGET + widget_menu->add_item("Progress Bar");
        widget_menu->add_separator();
        m_mi_add_image      = MENU_WIDGET + widget_menu->add_item("Image");
        m_mi_add_listbox    = MENU_WIDGET + widget_menu->add_item("List Box");
        m_mi_add_combobox   = MENU_WIDGET + widget_menu->add_item("Combo Box");
        m_mi_add_treeview   = MENU_WIDGET + widget_menu->add_item("Tree View");
        m_mi_add_separator  = MENU_WIDGET + widget_menu->add_item("Separator");
        m_mi_add_spacer     = MENU_WIDGET + widget_menu->add_item("Spacer");
        widget_menu->set_menu_event_handler(&m_fwd_widget);
        register_menu(widget_menu, "Widget");
    }

    // About menu
    IGuiMenu* about_menu = m_editor_ctx->create_menu();
    if (about_menu) {
        about_menu->set_bounds(make_box(0, 0, 220, 40));
        m_mi_about = MENU_ABOUT + about_menu->add_item("About GUI Editor");
        about_menu->set_menu_event_handler(&m_fwd_about);
        register_menu(about_menu, "About");
    }
}

void GuiEditor::build_toolbar() {
    m_toolbar = m_editor_ctx->create_toolbar(ToolbarOrientation::Horizontal);
    if (!m_toolbar) return;

    m_toolbar->set_name("editor_toolbar");

    m_tb_new  = m_toolbar->add_button("\xF0\x9F\x97\x8E",  "New (Ctrl+N)");      // U+1F5CE (document)
    m_tb_open = m_toolbar->add_button("\xF0\x9F\x93\x82", "Open (Ctrl+O)");     // U+1F4C2 (open folder)
    m_tb_save = m_toolbar->add_button("\xF0\x9F\x92\xBE", "Save (Ctrl+S)");     // U+1F4BE (floppy disk)
    m_toolbar->add_separator();
    m_tb_undo = m_toolbar->add_button("\xE2\x86\xA9", "Undo (Ctrl+Z)");         // U+21A9 (leftwards arrow with hook)
    m_tb_redo = m_toolbar->add_button("\xE2\x86\xAA", "Redo (Ctrl+Y)");         // U+21AA (rightwards arrow with hook)
    m_toolbar->add_separator();
    m_tb_delete   = m_toolbar->add_button("\xE2\x9C\x95", "Delete (Del)");      // U+2715 (multiplication X)
    m_toolbar->add_separator();
    m_tb_zoom_fit = m_toolbar->add_button("\xE2\x87\xB1", "Zoom to Fit");       // U+21F1 (north west arrow to corner)
    m_tb_grid     = m_toolbar->add_toggle_button("\xE2\x96\xA6", "Toggle Grid", true);  // U+25A6 (square with diagonal crosshatch)
    m_tb_snap     = m_toolbar->add_toggle_button("\xE2\x97\x88", "Toggle Snap", true);  // U+25C8 (white diamond containing black small diamond)

    m_toolbar->set_toolbar_event_handler(this);
    m_editor_root->add_child(m_toolbar);
}

void GuiEditor::build_hierarchy_panel() {
    m_hierarchy_container = m_editor_ctx->create_widget(WidgetType::Container);
    if (!m_hierarchy_container) return;

    m_hierarchy_container->set_name("hierarchy_container");
    m_hierarchy_container->set_layout_direction(LayoutDirection::Vertical);
    m_hierarchy_container->set_spacing(0.0f);

    GuiStyle cs = GuiStyle::default_style();
    cs.background_color = math::Vec4(0, 0, 0, 0);
    cs.padding = math::Vec4(0.0f);
    m_hierarchy_container->set_style(cs);

    // Header label
    IGuiLabel* header = m_editor_ctx->create_label("Hierarchy");
    if (header) {
        header->set_name("hierarchy_header");
        GuiStyle hs = GuiStyle::default_style();
        hs.background_color = color_rgba8(45, 45, 48);
        hs.padding = math::Vec4(8.0f, 4.0f, 8.0f, 4.0f);
        header->set_style(hs);
        m_hierarchy_container->add_child(header);
    }

    // Tree view
    m_hierarchy_tree = m_editor_ctx->create_tree_view();
    if (m_hierarchy_tree) {
        m_hierarchy_tree->set_name("hierarchy_tree");
        m_hierarchy_tree->set_drag_reorder_enabled(true);
        m_hierarchy_tree->set_tree_event_handler(this);
        m_hierarchy_container->add_child(m_hierarchy_tree);
    }

    m_editor_root->add_child(m_hierarchy_container);
}

void GuiEditor::build_status_bar() {
    m_status_bar = m_editor_ctx->create_status_bar();
    if (!m_status_bar) return;

    m_status_bar->set_name("editor_statusbar");
    m_status_bar->set_status_bar_event_handler(this);

    m_sb_message   = m_status_bar->add_panel("Ready", StatusBarPanelSizeMode::Fill);
    m_sb_zoom      = m_status_bar->add_panel("100%", StatusBarPanelSizeMode::Fixed);
    m_status_bar->set_panel_fixed_width(m_sb_zoom, 60.0f);
    m_sb_position  = m_status_bar->add_panel("0, 0", StatusBarPanelSizeMode::Fixed);
    m_status_bar->set_panel_fixed_width(m_sb_position, 100.0f);
    m_sb_selection = m_status_bar->add_panel("No selection", StatusBarPanelSizeMode::Fixed);
    m_status_bar->set_panel_fixed_width(m_sb_selection, 120.0f);

    m_editor_root->add_child(m_status_bar);
}

// ============================================================================
// Explicit layout pass
// ============================================================================

void GuiEditor::layout_editor(float w, float h) {
    if (!m_editor_root) return;

    m_editor_root->set_bounds(make_box(0, 0, w, h));

    float yc = 0;

    if (m_menubar) {
        m_menubar->set_bounds(make_box(0, yc, w, MENUBAR_H));
        yc += MENUBAR_H;
    }
    if (m_toolbar) {
        m_toolbar->set_bounds(make_box(0, yc, w, TOOLBAR_H));
        yc += TOOLBAR_H;
    }

    float sb_y = h - STATUSBAR_H;
    if (m_status_bar) {
        m_status_bar->set_bounds(make_box(0, sb_y, w, STATUSBAR_H));
    }

    float content_y = yc;
    float content_h = sb_y - content_y;
    if (content_h < 1.0f) content_h = 1.0f;

    // Left: hierarchy tree
    float tree_w = m_tree_panel_w;
    if (m_hierarchy_container) {
        m_hierarchy_container->set_bounds(make_box(0, content_y, tree_w, content_h));
        IGuiWidget* header = m_hierarchy_container->find_by_name("hierarchy_header");
        float hy = content_y;
        if (header) {
            header->set_bounds(make_box(0, hy, tree_w, 24.0f));
            hy += 24.0f;
        }
        if (m_hierarchy_tree) {
            m_hierarchy_tree->set_bounds(make_box(0, hy, tree_w, content_h - (hy - content_y)));
        }
    }

    // Right: inspector
    float insp_x = w - m_inspector_w;
    if (m_inspector_container) {
        m_inspector_container->set_bounds(make_box(insp_x, content_y, m_inspector_w, content_h));
        // Also set bounds on the property grid inside
        IGuiWidget* grid = m_inspector_container->find_by_name("inspector_grid");
        if (grid) {
            grid->set_bounds(make_box(insp_x, content_y, m_inspector_w, content_h));
        }
    }

    // Center: canvas
    float canvas_x = tree_w;
    float canvas_w = insp_x - tree_w;
    if (canvas_w < 1.0f) canvas_w = 1.0f;
    if (m_canvas_panel) {
        m_canvas_panel->set_bounds(make_box(canvas_x, content_y, canvas_w, content_h));
    }

    m_canvas.set_viewport_bounds(make_box(canvas_x, content_y, canvas_w, content_h));

    // Give the design context root a large non-empty bounds so collect_recursive
    // visits its children. Without this, all design widgets are invisible because
    // collect_recursive skips any widget with empty (zero-size) bounds.
    if (m_design_ctx) {
        m_design_ctx->get_root()->set_bounds(make_box(0, 0, 10000, 10000));
    }
}

// ============================================================================
// Frame update
// ============================================================================

void GuiEditor::update(float delta_time) {
    if (!m_initialized) return;

    // Layout in *logical* px so hardcoded sizes (MENUBAR_H, panel widths)
    // keep their physical size on Hi-DPI screens.
    int sw_p = 1440, sh_p = 900;
    if (m_window) m_window->get_size(&sw_p, &sh_p);
    float scale = m_window ? m_window->get_dpi_scale() : 1.0f;
    if (scale <= 0.0f) scale = 1.0f;
    int sw = static_cast<int>(sw_p / scale);
    int sh = static_cast<int>(sh_p / scale);

    layout_editor(static_cast<float>(sw), static_cast<float>(sh));

    m_editor_ctx->begin_frame(delta_time);

    if (m_toolbar) {
        m_toolbar->set_item_enabled(m_tb_undo, m_history.can_undo());
        m_toolbar->set_item_enabled(m_tb_redo, m_history.can_redo());
        m_toolbar->set_item_enabled(m_tb_delete, !m_canvas.get_selection().empty());
    }

    if (m_status_bar) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f%%", m_canvas.get_zoom() * 100.0f);
        m_status_bar->set_panel_text(m_sb_zoom, buf);

        int sc = m_canvas.get_selection().count();
        if (sc == 0)      snprintf(buf, sizeof(buf), "No selection");
        else if (sc == 1)  snprintf(buf, sizeof(buf), "1 widget");
        else               snprintf(buf, sizeof(buf), "%d widgets", sc);
        m_status_bar->set_panel_text(m_sb_selection, buf);
    }

    m_editor_ctx->end_frame();
}

const WidgetRenderInfo& GuiEditor::get_render_info(Window* window) {
    return m_editor_ctx->get_render_info();
}

IGuiWidget* GuiEditor::get_design_root() const {
    return m_design_ctx ? m_design_ctx->get_root() : nullptr;
}

// ============================================================================
// File operations
// ============================================================================

bool GuiEditor::new_file() {
    if (!m_design_ctx) return false;
    IGuiWidget* root = m_design_ctx->get_root();
    if (root) root->clear_children();
    m_canvas.clear_selection();
    m_history.clear();
    m_filepath.clear();
    refresh_hierarchy();
    m_inspector.set_selected_widget(nullptr);
    set_status_message("New layout created");
    update_title();
    return true;
}

bool GuiEditor::open_file(const char* filepath) {
    if (!m_design_ctx) return false;
    if (!filepath) { set_status_message("Open: no file dialog available"); return false; }

    GuiSerializeOptions opts;
    opts.clear_before_load = true;
    GuiSerializeResult result = gui_load(m_design_ctx, filepath, m_format, opts);
    if (result != GuiSerializeResult::Success) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to open: %s", gui_serialize_result_to_string(result));
        set_status_message(msg);
        return false;
    }

    m_filepath = filepath;
    m_canvas.clear_selection();
    m_canvas.set_design_root(m_design_ctx->get_root());
    m_history.clear();
    m_history.mark_saved();
    refresh_hierarchy();
    m_inspector.set_selected_widget(nullptr);
    m_canvas.zoom_to_fit();

    char msg[256];
    snprintf(msg, sizeof(msg), "Opened: %s", filepath);
    set_status_message(msg);
    update_title();
    return true;
}

bool GuiEditor::save_file() {
    if (m_filepath.empty()) return save_file_as();
    return save_file_as(m_filepath.c_str());
}

bool GuiEditor::save_file_as(const char* filepath) {
    if (!m_design_ctx) return false;
    if (!filepath) { set_status_message("Save: no file dialog available"); return false; }

    std::string p = filepath;
    m_format = (p.size() > 4 && p.substr(p.size() - 4) == ".bin")
        ? GuiSerializeFormat::Binary : GuiSerializeFormat::Json;

    GuiSerializeOptions opts;
    opts.pretty_print = true;
    GuiSerializeResult result = gui_save(m_design_ctx, filepath, m_format, opts);
    if (result != GuiSerializeResult::Success) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to save: %s", gui_serialize_result_to_string(result));
        set_status_message(msg);
        return false;
    }

    m_filepath = filepath;
    m_history.mark_saved();
    char msg[256];
    snprintf(msg, sizeof(msg), "Saved: %s", filepath);
    set_status_message(msg);
    update_title();
    return true;
}

// ============================================================================
// Undo/Redo
// ============================================================================

void GuiEditor::undo() {
    m_history.undo();
    refresh_hierarchy();
    on_selection_changed();
    set_status_message(m_history.get_undo_description() ? m_history.get_undo_description() : "Undo");
}

void GuiEditor::redo() {
    m_history.redo();
    refresh_hierarchy();
    on_selection_changed();
    set_status_message(m_history.get_redo_description() ? m_history.get_redo_description() : "Redo");
}

// ============================================================================
// Widget creation
// ============================================================================

void GuiEditor::create_widget(WidgetType type, const math::Vec2& position) {
    IGuiWidget* parent = m_design_ctx ? m_design_ctx->get_root() : nullptr;
    IGuiWidget* sel = m_canvas.get_selection().primary();
    if (sel) {
        WidgetType st = sel->get_type();
        if (st == WidgetType::Container || st == WidgetType::Panel ||
            st == WidgetType::ScrollArea || st == WidgetType::TabControl)
            parent = sel;
    }
    if (!parent) return;

    math::Vec2 snapped = m_canvas.get_grid().snap(position);
    float sx = x(snapped), sy = y(snapped);
    math::Box bounds = make_box(sx, sy, 120.0f, 40.0f);

    switch (type) {
        case WidgetType::Container: case WidgetType::Panel: case WidgetType::ScrollArea:
            bounds = make_box(sx, sy, 200.0f, 150.0f); break;
        case WidgetType::Separator:
            bounds = make_box(sx, sy, 200.0f, 4.0f); break;
        case WidgetType::Spacer:
            bounds = make_box(sx, sy, 50.0f, 50.0f); break;
        case WidgetType::Image:
            bounds = make_box(sx, sy, 100.0f, 100.0f); break;
        case WidgetType::ListBox: case WidgetType::TreeView:
            bounds = make_box(sx, sy, 180.0f, 200.0f); break;
        case WidgetType::TabControl:
            bounds = make_box(sx, sy, 250.0f, 180.0f); break;
        default: break;
    }

    auto cmd = std::make_unique<CreateWidgetCommand>(m_design_ctx, parent, type, bounds);
    auto* cmd_raw = cmd.get();           // keep raw pointer; object lives in history stack
    m_history.execute(std::move(cmd));
    IGuiWidget* created = cmd_raw->get_created_widget(); // valid after execute()

    if (created) {
        m_canvas.select_widget(created, false);
        on_selection_changed();
    }
    refresh_hierarchy();
    set_status_message("Widget created");
}

void GuiEditor::create_widget_from_menu(WidgetType type) {
    math::Box vp = m_canvas.get_viewport_bounds();
    float cx = x(box_min(vp)) + box_width(vp) * 0.5f;
    float cy = y(box_min(vp)) + box_height(vp) * 0.5f;
    math::Vec2 canvas_pos = m_canvas.screen_to_canvas(math::Vec2(cx, cy));
    create_widget(type, canvas_pos);
}

// ============================================================================
// Status bar
// ============================================================================

void GuiEditor::delete_selected() {
    m_canvas.delete_selection();
    refresh_hierarchy();
    on_selection_changed();
}

void GuiEditor::on_canvas_interaction() {
    // After any canvas mouse action (click-select, move, resize) sync the
    // inspector and hierarchy to reflect the current selection / new bounds.
    on_selection_changed();
    m_inspector.refresh();   // pick up geometry changes for same-widget moves
}

void GuiEditor::set_status_message(const char* msg) {
    if (m_status_bar && m_sb_message >= 0)
        m_status_bar->set_panel_text(m_sb_message, msg);
}

void GuiEditor::update_title() {
    if (m_on_dirty) m_on_dirty(m_history.is_dirty());
}

// ============================================================================
// Hierarchy tree
// ============================================================================

void GuiEditor::refresh_hierarchy() {
    if (!m_hierarchy_tree || !m_design_ctx) return;
    m_hierarchy_tree->clear_nodes();
    m_node_to_widget.clear();

    IGuiWidget* root = m_design_ctx->get_root();
    if (!root) return;

    int root_node = m_hierarchy_tree->add_node(-1, "Design Root", "icon_root");
    m_node_to_widget.push_back(root);
    refresh_hierarchy_recursive(root, root_node);
    m_hierarchy_tree->expand_all();
}

void GuiEditor::refresh_hierarchy_recursive(IGuiWidget* widget, int parent_node_id) {
    for (int i = 0; i < widget->get_child_count(); ++i) {
        IGuiWidget* child = widget->get_child(i);
        const char* name = child->get_name();
        if (!name || !name[0]) name = "(unnamed)";

        const char* icon = "icon_widget";
        switch (child->get_type()) {
            case WidgetType::Container:  icon = "icon_container"; break;
            case WidgetType::Panel:      icon = "icon_panel"; break;
            case WidgetType::Button:     icon = "icon_button"; break;
            case WidgetType::Label:      icon = "icon_label"; break;
            case WidgetType::TextInput:  icon = "icon_textinput"; break;
            case WidgetType::Checkbox:   icon = "icon_checkbox"; break;
            case WidgetType::Slider:     icon = "icon_slider"; break;
            case WidgetType::Image:      icon = "icon_image"; break;
            case WidgetType::ListBox:    icon = "icon_listbox"; break;
            case WidgetType::TreeView:   icon = "icon_treeview"; break;
            case WidgetType::TabControl: icon = "icon_tabs"; break;
            default: break;
        }

        int node_id = m_hierarchy_tree->add_node(parent_node_id, name, icon);
        while (static_cast<int>(m_node_to_widget.size()) <= node_id)
            m_node_to_widget.push_back(nullptr);
        m_node_to_widget[node_id] = child;

        if (child->get_child_count() > 0)
            refresh_hierarchy_recursive(child, node_id);
    }
}

void GuiEditor::on_node_selected(int node_id) {
    if (node_id >= 0 && node_id < static_cast<int>(m_node_to_widget.size())) {
        IGuiWidget* widget = m_node_to_widget[node_id];
        if (widget) {
            m_canvas.select_widget(widget, false);
            on_selection_changed();
        }
    }
}

void GuiEditor::on_node_expanded(int, bool) {}

void GuiEditor::on_node_double_clicked(int) {}

void GuiEditor::on_node_moved(int dragged_id, int new_parent_id, int before_id) {
    // Resolve tree node IDs to widget pointers
    auto node_to_widget = [&](int nid) -> IGuiWidget* {
        if (nid < 0 || nid >= (int)m_node_to_widget.size()) return nullptr;
        return m_node_to_widget[nid];
    };

    IGuiWidget* widget     = node_to_widget(dragged_id);
    IGuiWidget* new_parent = node_to_widget(new_parent_id);
    IGuiWidget* before_w   = node_to_widget(before_id);
    IGuiWidget* old_parent = widget ? widget->get_parent() : nullptr;

    if (!widget || !new_parent || !old_parent) return;
    if (widget == new_parent) return;

    // Validate that new_parent can accept children
    {
        WidgetType nt = new_parent->get_type();
        bool ok = (nt == WidgetType::Container || nt == WidgetType::Panel ||
                   nt == WidgetType::ScrollArea || nt == WidgetType::TabControl);
        if (!ok) {
            set_status_message("Cannot drop here: target does not accept children");
            refresh_hierarchy();
            return;
        }
    }

    // Use ChangePropertyCommand for undoable reparent with insertion order
    auto apply = [widget, old_parent, new_parent, before_w]() {
        old_parent->remove_child(widget);
        new_parent->insert_child_before(widget, before_w);
    };
    // For undo: find where it was in old_parent before the move
    int old_index = -1;
    for (int i = 0; i < old_parent->get_child_count(); ++i) {
        if (old_parent->get_child(i) == widget) { old_index = i; break; }
    }
    // The sibling that was after the widget (to restore position on undo)
    IGuiWidget* old_before = (old_index + 1 < old_parent->get_child_count())
                             ? old_parent->get_child(old_index + 1) : nullptr;

    auto revert = [widget, old_parent, new_parent, old_before]() {
        new_parent->remove_child(widget);
        old_parent->insert_child_before(widget, old_before);
    };

    m_history.execute(std::make_unique<ChangePropertyCommand>("Reparent widget",
        std::move(apply), std::move(revert)));

    refresh_hierarchy();
    on_selection_changed();
    set_status_message("Widget moved");
}

void GuiEditor::on_right_click(const math::Vec2& pos) {
    if (!m_tree_context_menu) return;
    bool has_sel = !m_canvas.get_selection().empty();
    m_tree_context_menu->set_item_enabled(m_cm_delete - MENU_CONTEXT, has_sel);
    m_tree_context_menu->set_item_enabled(m_cm_rename - MENU_CONTEXT, has_sel);

    // Clamp so the popup stays within the window (logical px space).
    int sw_p = 1440, sh_p = 900;
    if (m_window) m_window->get_size(&sw_p, &sh_p);
    float scale = m_window ? m_window->get_dpi_scale() : 1.0f;
    if (scale <= 0.0f) scale = 1.0f;
    int sw = static_cast<int>(sw_p / scale);
    int sh = static_cast<int>(sh_p / scale);
    const MenuStyle& ms = m_tree_context_menu->get_menu_style();
    // Estimate menu height: items × item_height + separators × separator_height
    // Context menu has 19 normal items and 5 separators
    float est_h = 19.0f * ms.item_height + 5.0f * ms.separator_height;
    float est_w = std::max(math::box_width(m_tree_context_menu->get_bounds()), ms.min_width);
    float cx = math::x(pos);
    float cy = math::y(pos);
    if (cx + est_w > (float)sw) cx = (float)sw - est_w;
    if (cy + est_h > (float)sh) cy = (float)sh - est_h;
    if (cx < 0.0f) cx = 0.0f;
    if (cy < 0.0f) cy = 0.0f;
    m_tree_context_menu->show_at(math::Vec2(cx, cy));
}

void GuiEditor::on_selection_changed() {
    IGuiWidget* selected = m_canvas.get_selection().primary();
    m_inspector.set_selected_widget(selected);

    if (m_on_selection) m_on_selection(selected);

    if (m_hierarchy_tree && selected) {
        for (int i = 0; i < static_cast<int>(m_node_to_widget.size()); ++i) {
            if (m_node_to_widget[i] == selected) {
                m_hierarchy_tree->set_selected_node(i);
                m_hierarchy_tree->ensure_node_visible(i);
                break;
            }
        }
    }
}

// ============================================================================
// Toolbar callbacks
// ============================================================================

void GuiEditor::on_toolbar_item_clicked(int item_id) {
    if (item_id == m_tb_new)      { new_file(); return; }
    if (item_id == m_tb_open)     { open_file(); return; }
    if (item_id == m_tb_save)     { save_file(); return; }
    if (item_id == m_tb_undo)     { undo(); return; }
    if (item_id == m_tb_redo)     { redo(); return; }
    if (item_id == m_tb_delete)   { m_canvas.delete_selection(); refresh_hierarchy(); return; }
    if (item_id == m_tb_zoom_fit) { m_canvas.zoom_to_fit(); return; }
}

void GuiEditor::on_toolbar_item_toggled(int item_id, bool toggled) {
    if (item_id == m_tb_grid) { m_canvas.get_grid().visible = toggled; return; }
    if (item_id == m_tb_snap) { m_canvas.get_grid().snap_enabled = toggled; return; }
}

// ============================================================================
// Menu callbacks (all IDs are offset + local_id, disambiguated by offset)
// ============================================================================

void GuiEditor::handle_menu_item(int id) {
    // File
    if (id == m_mi_new)        { new_file(); return; }
    if (id == m_mi_open)       { open_file(); return; }
    if (id == m_mi_save)       { save_file(); return; }
    if (id == m_mi_save_as)    { save_file_as(); return; }

    // Edit
    if (id == m_mi_undo)       { undo(); return; }
    if (id == m_mi_redo)       { redo(); return; }
    if (id == m_mi_cut)        { m_canvas.cut_selection(); refresh_hierarchy(); return; }
    if (id == m_mi_copy)       { m_canvas.copy_selection(); return; }
    if (id == m_mi_paste)      { m_canvas.paste(math::Vec2(100, 100)); refresh_hierarchy(); return; }
    if (id == m_mi_delete)     { m_canvas.delete_selection(); refresh_hierarchy(); return; }
    if (id == m_mi_select_all) { m_canvas.select_all(); on_selection_changed(); return; }

    // View
    if (id == m_mi_zoom_fit)   { m_canvas.zoom_to_fit(); return; }
    if (id == m_mi_zoom_reset) { m_canvas.reset_view(); return; }
    if (id == m_mi_grid) {
        m_canvas.get_grid().visible = !m_canvas.get_grid().visible;
        if (m_toolbar) m_toolbar->set_item_toggled(m_tb_grid, m_canvas.get_grid().visible);
        return;
    }
    if (id == m_mi_snap) {
        m_canvas.get_grid().snap_enabled = !m_canvas.get_grid().snap_enabled;
        if (m_toolbar) m_toolbar->set_item_toggled(m_tb_snap, m_canvas.get_grid().snap_enabled);
        return;
    }

    // Widget menu
    if (id == m_mi_add_container)  { create_widget_from_menu(WidgetType::Container); return; }
    if (id == m_mi_add_panel)      { create_widget_from_menu(WidgetType::Panel); return; }
    if (id == m_mi_add_scrollarea) { create_widget_from_menu(WidgetType::ScrollArea); return; }
    if (id == m_mi_add_tabcontrol) { create_widget_from_menu(WidgetType::TabControl); return; }
    if (id == m_mi_add_button)     { create_widget_from_menu(WidgetType::Button); return; }
    if (id == m_mi_add_label)      { create_widget_from_menu(WidgetType::Label); return; }
    if (id == m_mi_add_textinput)  { create_widget_from_menu(WidgetType::TextInput); return; }
    if (id == m_mi_add_checkbox)   { create_widget_from_menu(WidgetType::Checkbox); return; }
    if (id == m_mi_add_radio)      { create_widget_from_menu(WidgetType::RadioButton); return; }
    if (id == m_mi_add_slider)     { create_widget_from_menu(WidgetType::Slider); return; }
    if (id == m_mi_add_progress)   { create_widget_from_menu(WidgetType::ProgressBar); return; }
    if (id == m_mi_add_image)      { create_widget_from_menu(WidgetType::Image); return; }
    if (id == m_mi_add_listbox)    { create_widget_from_menu(WidgetType::ListBox); return; }
    if (id == m_mi_add_combobox)   { create_widget_from_menu(WidgetType::ComboBox); return; }
    if (id == m_mi_add_treeview)   { create_widget_from_menu(WidgetType::TreeView); return; }
    if (id == m_mi_add_separator)  { create_widget_from_menu(WidgetType::Separator); return; }
    if (id == m_mi_add_spacer)     { create_widget_from_menu(WidgetType::Spacer); return; }

    // About
    if (id == m_mi_about) {
        set_status_message("GUI Editor v1.0 - Visual designer for GUI layouts");
        return;
    }

    // Context menu (tree right-click)
    if (id == m_cm_add_container)  { create_widget_from_menu(WidgetType::Container); return; }
    if (id == m_cm_add_panel)      { create_widget_from_menu(WidgetType::Panel); return; }
    if (id == m_cm_add_scrollarea) { create_widget_from_menu(WidgetType::ScrollArea); return; }
    if (id == m_cm_add_tabcontrol) { create_widget_from_menu(WidgetType::TabControl); return; }
    if (id == m_cm_add_button)     { create_widget_from_menu(WidgetType::Button); return; }
    if (id == m_cm_add_label)      { create_widget_from_menu(WidgetType::Label); return; }
    if (id == m_cm_add_textinput)  { create_widget_from_menu(WidgetType::TextInput); return; }
    if (id == m_cm_add_checkbox)   { create_widget_from_menu(WidgetType::Checkbox); return; }
    if (id == m_cm_add_radio)      { create_widget_from_menu(WidgetType::RadioButton); return; }
    if (id == m_cm_add_slider)     { create_widget_from_menu(WidgetType::Slider); return; }
    if (id == m_cm_add_progress)   { create_widget_from_menu(WidgetType::ProgressBar); return; }
    if (id == m_cm_add_listbox)    { create_widget_from_menu(WidgetType::ListBox); return; }
    if (id == m_cm_add_combobox)   { create_widget_from_menu(WidgetType::ComboBox); return; }
    if (id == m_cm_add_treeview)   { create_widget_from_menu(WidgetType::TreeView); return; }
    if (id == m_cm_add_image)      { create_widget_from_menu(WidgetType::Image); return; }
    if (id == m_cm_add_separator)  { create_widget_from_menu(WidgetType::Separator); return; }
    if (id == m_cm_add_spacer)     { create_widget_from_menu(WidgetType::Spacer); return; }
    if (id == m_cm_delete) {
        m_canvas.delete_selection();
        refresh_hierarchy();
        return;
    }
    if (id == m_cm_rename) {
        set_status_message("Rename: use property grid to change widget name");
        return;
    }
}

} // namespace editor
} // namespace gui
} // namespace window
