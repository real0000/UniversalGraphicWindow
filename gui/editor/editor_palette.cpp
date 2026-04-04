/*
 * editor_palette.cpp - Widget Palette Panel Implementation
 */

#include "editor_palette.hpp"
#include "gui/gui_context.hpp"

namespace window {
namespace gui {
namespace editor {

void EditorPalette::initialize(IGuiContext* ctx) {
    m_ctx = ctx;
    m_root = ctx->create_widget(WidgetType::Container);
    if (m_root) {
        m_root->set_name("palette_root");
        m_root->set_layout_direction(LayoutDirection::Vertical);
        m_root->set_spacing(2.0f);

        GuiStyle style = GuiStyle::default_style();
        style.background_color = color_rgba8(37, 37, 38);
        style.padding = math::Vec4(4.0f, 4.0f, 4.0f, 4.0f);
        m_root->set_style(style);
    }
    populate_items();
}

void EditorPalette::shutdown() {
    m_items.clear();
    m_categories.clear();
    m_category_expanded.clear();
    m_dragging = nullptr;
    m_root = nullptr;
    m_ctx = nullptr;
}

void EditorPalette::populate_items() {
    m_items.clear();
    m_categories.clear();
    m_category_expanded.clear();

    // Containers
    m_items.push_back({WidgetType::Container, "Container", "icon_container", "Containers",
                       "Generic container for grouping child widgets"});
    m_items.push_back({WidgetType::Panel, "Panel", "icon_panel", "Containers",
                       "Visual panel with border and background"});
    m_items.push_back({WidgetType::ScrollArea, "Scroll Area", "icon_scroll", "Containers",
                       "Scrollable content area"});
    m_items.push_back({WidgetType::TabControl, "Tab Control", "icon_tabs", "Containers",
                       "Tabbed container for multiple pages"});

    // Controls
    m_items.push_back({WidgetType::Button, "Button", "icon_button", "Controls",
                       "Clickable push button"});
    m_items.push_back({WidgetType::Checkbox, "Checkbox", "icon_checkbox", "Controls",
                       "Boolean toggle checkbox"});
    m_items.push_back({WidgetType::RadioButton, "Radio Button", "icon_radio", "Controls",
                       "Mutually exclusive radio button"});
    m_items.push_back({WidgetType::Slider, "Slider", "icon_slider", "Controls",
                       "Value slider with draggable thumb"});
    m_items.push_back({WidgetType::ProgressBar, "Progress Bar", "icon_progress", "Controls",
                       "Determinate or indeterminate progress indicator"});

    // Display
    m_items.push_back({WidgetType::Label, "Label", "icon_label", "Display",
                       "Static text display"});
    m_items.push_back({WidgetType::Image, "Image", "icon_image", "Display",
                       "Image display with 9-slice support"});
    m_items.push_back({WidgetType::Separator, "Separator", "icon_separator", "Display",
                       "Visual divider line"});
    m_items.push_back({WidgetType::Spacer, "Spacer", "icon_spacer", "Display",
                       "Flexible empty space"});

    // Input
    m_items.push_back({WidgetType::TextInput, "Text Input", "icon_textinput", "Input",
                       "Single-line editable text field"});
    m_items.push_back({WidgetType::ListBox, "List Box", "icon_listbox", "Input",
                       "Selectable item list"});
    m_items.push_back({WidgetType::ComboBox, "Combo Box", "icon_combobox", "Input",
                       "Dropdown selection control"});
    m_items.push_back({WidgetType::TreeView, "Tree View", "icon_treeview", "Input",
                       "Hierarchical node tree"});

    // Collect unique categories
    for (const auto& item : m_items) {
        bool found = false;
        for (const auto& cat : m_categories) {
            if (cat == item.category) { found = true; break; }
        }
        if (!found) {
            m_categories.push_back(item.category);
            m_category_expanded.push_back(true);
        }
    }
}

void EditorPalette::begin_drag(int item_index) {
    if (item_index >= 0 && item_index < static_cast<int>(m_items.size())) {
        m_dragging = &m_items[item_index];
    }
}

void EditorPalette::end_drag() {
    m_dragging = nullptr;
}

void EditorPalette::cancel_drag() {
    m_dragging = nullptr;
}

bool EditorPalette::is_category_expanded(int index) const {
    if (index < 0 || index >= static_cast<int>(m_category_expanded.size())) return true;
    return m_category_expanded[index];
}

void EditorPalette::toggle_category(int index) {
    if (index >= 0 && index < static_cast<int>(m_category_expanded.size())) {
        m_category_expanded[index] = !m_category_expanded[index];
    }
}

void EditorPalette::update(float /*dt*/, const math::Vec2& /*mouse_pos*/) {
    // Hover detection is handled by the canvas renderer
    // which maps mouse positions to palette item rects
}

} // namespace editor
} // namespace gui
} // namespace window
