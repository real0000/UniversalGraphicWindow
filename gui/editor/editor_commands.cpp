/*
 * editor_commands.cpp - Undo/Redo Command System Implementation
 */

#include "editor_commands.hpp"
#include "gui/gui_context.hpp"

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// CommandHistory
// ============================================================================

void CommandHistory::execute(std::unique_ptr<IEditorCommand> cmd) {
    cmd->execute();

    // Try to merge with previous command
    if (!m_undo_stack.empty() && m_undo_stack.back()->merge_with(cmd.get())) {
        return; // merged into existing command
    }

    m_undo_stack.push_back(std::move(cmd));
    m_redo_stack.clear();
}

bool CommandHistory::can_undo() const {
    return !m_undo_stack.empty();
}

bool CommandHistory::can_redo() const {
    return !m_redo_stack.empty();
}

void CommandHistory::undo() {
    if (m_undo_stack.empty()) return;
    auto cmd = std::move(m_undo_stack.back());
    m_undo_stack.pop_back();
    cmd->undo();
    m_redo_stack.push_back(std::move(cmd));
}

void CommandHistory::redo() {
    if (m_redo_stack.empty()) return;
    auto cmd = std::move(m_redo_stack.back());
    m_redo_stack.pop_back();
    cmd->execute();
    m_undo_stack.push_back(std::move(cmd));
}

const char* CommandHistory::get_undo_description() const {
    if (m_undo_stack.empty()) return nullptr;
    return m_undo_stack.back()->get_description();
}

const char* CommandHistory::get_redo_description() const {
    if (m_redo_stack.empty()) return nullptr;
    return m_redo_stack.back()->get_description();
}

void CommandHistory::clear() {
    m_undo_stack.clear();
    m_redo_stack.clear();
    m_saved_index = 0;
}

void CommandHistory::mark_saved() {
    m_saved_index = static_cast<int>(m_undo_stack.size());
}

bool CommandHistory::is_dirty() const {
    return static_cast<int>(m_undo_stack.size()) != m_saved_index;
}

// ============================================================================
// CreateWidgetCommand
// ============================================================================

static const char* widget_type_name(WidgetType type) {
    switch (type) {
        case WidgetType::Container:   return "Container";
        case WidgetType::Panel:       return "Panel";
        case WidgetType::Button:      return "Button";
        case WidgetType::Label:       return "Label";
        case WidgetType::TextInput:   return "TextInput";
        case WidgetType::Checkbox:    return "Checkbox";
        case WidgetType::RadioButton: return "RadioButton";
        case WidgetType::Slider:      return "Slider";
        case WidgetType::ProgressBar: return "ProgressBar";
        case WidgetType::ScrollArea:  return "ScrollArea";
        case WidgetType::ListBox:     return "ListBox";
        case WidgetType::ComboBox:    return "ComboBox";
        case WidgetType::TabControl:  return "TabControl";
        case WidgetType::TreeView:    return "TreeView";
        case WidgetType::Image:       return "Image";
        case WidgetType::Separator:   return "Separator";
        case WidgetType::Spacer:      return "Spacer";
        default:                      return "Widget";
    }
}

CreateWidgetCommand::CreateWidgetCommand(IGuiContext* ctx, IGuiWidget* parent,
                                         WidgetType type, const math::Box& bounds,
                                         const char* name)
    : m_ctx(ctx), m_parent(parent), m_type(type), m_bounds(bounds)
{
    if (name) m_name = name;
    m_desc = std::string("Create ") + widget_type_name(type);
}

void CreateWidgetCommand::execute() {
    if (!m_widget) {
        // Create the proper widget type so the canvas preview matches real GUI appearance.
        switch (m_type) {
            case WidgetType::Button:
                m_widget = m_ctx->create_button(ButtonType::Normal);
                if (m_widget) static_cast<IGuiButton*>(m_widget)->set_text("Button");
                break;
            case WidgetType::Checkbox:
                m_widget = m_ctx->create_button(ButtonType::Checkbox);
                if (m_widget) static_cast<IGuiButton*>(m_widget)->set_text("Checkbox");
                break;
            case WidgetType::RadioButton:
                m_widget = m_ctx->create_button(ButtonType::Radio);
                if (m_widget) static_cast<IGuiButton*>(m_widget)->set_text("Radio");
                break;
            case WidgetType::Label:
                m_widget = m_ctx->create_label("Label");
                break;
            case WidgetType::TextInput:
                m_widget = m_ctx->create_text_input("placeholder");
                break;
            case WidgetType::Slider:
                m_widget = m_ctx->create_slider(SliderOrientation::Horizontal);
                break;
            case WidgetType::ProgressBar:
                m_widget = m_ctx->create_progress_bar(ProgressBarMode::Determinate);
                break;
            case WidgetType::ScrollArea:
                m_widget = m_ctx->create_scroll_view();
                break;
            case WidgetType::ListBox:
                m_widget = m_ctx->create_list_box();
                break;
            case WidgetType::ComboBox:
                m_widget = m_ctx->create_combo_box();
                break;
            case WidgetType::TabControl:
                m_widget = m_ctx->create_tab_control(TabPosition::Top);
                break;
            case WidgetType::TreeView:
                m_widget = m_ctx->create_tree_view();
                break;
            case WidgetType::Image:
                m_widget = m_ctx->create_image("");
                break;
            default:
                // Container, Panel, Separator, Spacer — plain widgets are correct
                m_widget = m_ctx->create_widget(m_type);
                break;
        }
        if (!m_widget) return;
        m_widget->set_bounds(m_bounds);
        if (!m_name.empty()) {
            m_widget->set_name(m_name.c_str());
        } else {
            m_widget->set_name(widget_type_name(m_type));
        }
        m_widget->set_size_mode(SizeMode::Fixed, SizeMode::Fixed);
    }
    if (m_parent) {
        m_parent->add_child(m_widget);
    }
    m_owns_widget = false;
}

void CreateWidgetCommand::undo() {
    if (m_widget && m_parent) {
        m_parent->remove_child(m_widget);
        m_owns_widget = true;
    }
}

// ============================================================================
// DeleteWidgetCommand
// ============================================================================

DeleteWidgetCommand::DeleteWidgetCommand(IGuiContext* ctx, IGuiWidget* widget)
    : m_ctx(ctx), m_widget(widget)
{
    m_parent = widget ? widget->get_parent() : nullptr;
    m_desc = std::string("Delete ") + (widget ? widget->get_name() : "widget");

    // Find child index for reinsertion on undo
    if (m_parent) {
        for (int i = 0; i < m_parent->get_child_count(); ++i) {
            if (m_parent->get_child(i) == m_widget) {
                m_child_index = i;
                break;
            }
        }
    }

}

void DeleteWidgetCommand::execute() {
    if (m_widget && m_parent) {
        m_parent->remove_child(m_widget);
        m_owns_widget = true;
    }
}

void DeleteWidgetCommand::undo() {
    if (!m_widget || !m_parent) return;
    m_parent->add_child(m_widget);
    m_owns_widget = false;
}

// ============================================================================
// MoveWidgetCommand
// ============================================================================

MoveWidgetCommand::MoveWidgetCommand(IGuiWidget* widget,
                                     const math::Box& old_bounds,
                                     const math::Box& new_bounds)
    : m_widget(widget), m_old_bounds(old_bounds), m_new_bounds(new_bounds)
{
}

void MoveWidgetCommand::execute() {
    if (m_widget) m_widget->set_bounds(m_new_bounds);
}

void MoveWidgetCommand::undo() {
    if (m_widget) m_widget->set_bounds(m_old_bounds);
}

bool MoveWidgetCommand::merge_with(const IEditorCommand* other) {
    auto* mv = dynamic_cast<const MoveWidgetCommand*>(other);
    if (mv && mv->m_widget == m_widget) {
        m_new_bounds = mv->m_new_bounds;
        return true;
    }
    return false;
}

// ============================================================================
// ResizeWidgetCommand
// ============================================================================

ResizeWidgetCommand::ResizeWidgetCommand(IGuiWidget* widget,
                                         const math::Box& old_bounds,
                                         const math::Box& new_bounds)
    : m_widget(widget), m_old_bounds(old_bounds), m_new_bounds(new_bounds)
{
}

void ResizeWidgetCommand::execute() {
    if (m_widget) m_widget->set_bounds(m_new_bounds);
}

void ResizeWidgetCommand::undo() {
    if (m_widget) m_widget->set_bounds(m_old_bounds);
}

bool ResizeWidgetCommand::merge_with(const IEditorCommand* other) {
    auto* rs = dynamic_cast<const ResizeWidgetCommand*>(other);
    if (rs && rs->m_widget == m_widget) {
        m_new_bounds = rs->m_new_bounds;
        return true;
    }
    return false;
}

// ============================================================================
// ChangePropertyCommand
// ============================================================================

ChangePropertyCommand::ChangePropertyCommand(const char* desc,
                                             ApplyFunc apply, ApplyFunc revert)
    : m_desc(desc ? desc : "Change property"), m_apply(std::move(apply)), m_revert(std::move(revert))
{
}

void ChangePropertyCommand::execute() {
    if (m_apply) m_apply();
}

void ChangePropertyCommand::undo() {
    if (m_revert) m_revert();
}

// ============================================================================
// ReparentWidgetCommand
// ============================================================================

ReparentWidgetCommand::ReparentWidgetCommand(IGuiWidget* widget,
                                             IGuiWidget* old_parent,
                                             IGuiWidget* new_parent)
    : m_widget(widget), m_old_parent(old_parent), m_new_parent(new_parent)
{
    if (m_old_parent) {
        for (int i = 0; i < m_old_parent->get_child_count(); ++i) {
            if (m_old_parent->get_child(i) == m_widget) {
                m_old_index = i;
                break;
            }
        }
    }
}

void ReparentWidgetCommand::execute() {
    if (!m_widget) return;
    if (m_old_parent) m_old_parent->remove_child(m_widget);
    if (m_new_parent) m_new_parent->add_child(m_widget);
}

void ReparentWidgetCommand::undo() {
    if (!m_widget) return;
    if (m_new_parent) m_new_parent->remove_child(m_widget);
    if (m_old_parent) m_old_parent->add_child(m_widget);
}

// ============================================================================
// RenameWidgetCommand
// ============================================================================

RenameWidgetCommand::RenameWidgetCommand(IGuiWidget* widget,
                                         const char* old_name,
                                         const char* new_name)
    : m_widget(widget),
      m_old_name(old_name ? old_name : ""),
      m_new_name(new_name ? new_name : "")
{
}

void RenameWidgetCommand::execute() {
    if (m_widget) m_widget->set_name(m_new_name.c_str());
}

void RenameWidgetCommand::undo() {
    if (m_widget) m_widget->set_name(m_old_name.c_str());
}

// ============================================================================
// ChangeStyleCommand
// ============================================================================

ChangeStyleCommand::ChangeStyleCommand(IGuiWidget* widget,
                                       const GuiStyle& old_style,
                                       const GuiStyle& new_style)
    : m_widget(widget), m_old_style(old_style), m_new_style(new_style)
{
}

void ChangeStyleCommand::execute() {
    if (m_widget) m_widget->set_style(m_new_style);
}

void ChangeStyleCommand::undo() {
    if (m_widget) m_widget->set_style(m_old_style);
}

// ============================================================================
// ChangeVisibilityCommand
// ============================================================================

ChangeVisibilityCommand::ChangeVisibilityCommand(IGuiWidget* widget, bool new_visible)
    : m_widget(widget), m_old_visible(widget ? widget->is_visible() : true), m_new_visible(new_visible)
{
}

void ChangeVisibilityCommand::execute() {
    if (m_widget) m_widget->set_visible(m_new_visible);
}

void ChangeVisibilityCommand::undo() {
    if (m_widget) m_widget->set_visible(m_old_visible);
}

// ============================================================================
// ChangeSizeModeCommand
// ============================================================================

ChangeSizeModeCommand::ChangeSizeModeCommand(IGuiWidget* widget,
                                             SizeMode old_w, SizeMode old_h,
                                             SizeMode new_w, SizeMode new_h)
    : m_widget(widget), m_old_w(old_w), m_old_h(old_h), m_new_w(new_w), m_new_h(new_h)
{
}

void ChangeSizeModeCommand::execute() {
    if (m_widget) m_widget->set_size_mode(m_new_w, m_new_h);
}

void ChangeSizeModeCommand::undo() {
    if (m_widget) m_widget->set_size_mode(m_old_w, m_old_h);
}

// ============================================================================
// ChangeLayoutCommand
// ============================================================================

ChangeLayoutCommand::ChangeLayoutCommand(IGuiWidget* widget,
                                         LayoutDirection old_dir,
                                         LayoutDirection new_dir)
    : m_widget(widget), m_old_dir(old_dir), m_new_dir(new_dir)
{
}

void ChangeLayoutCommand::execute() {
    if (m_widget) m_widget->set_layout_direction(m_new_dir);
}

void ChangeLayoutCommand::undo() {
    if (m_widget) m_widget->set_layout_direction(m_old_dir);
}

} // namespace editor
} // namespace gui
} // namespace window
