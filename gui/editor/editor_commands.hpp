/*
 * editor_commands.hpp - Undo/Redo Command System
 *
 * Implements the command pattern for all editor operations.
 * Each user action is encapsulated as a command that can be undone and redone.
 */

#ifndef GUI_EDITOR_COMMANDS_HPP
#define GUI_EDITOR_COMMANDS_HPP

#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <cstring>
#include "gui/gui.hpp"

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// Command Interface
// ============================================================================

class IEditorCommand {
public:
    virtual ~IEditorCommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual const char* get_description() const = 0;

    // For command merging (e.g., consecutive property edits on the same field)
    virtual bool merge_with(const IEditorCommand* other) { (void)other; return false; }
};

// ============================================================================
// Command History (Undo/Redo stack)
// ============================================================================

class CommandHistory {
public:
    void execute(std::unique_ptr<IEditorCommand> cmd);

    bool can_undo() const;
    bool can_redo() const;
    void undo();
    void redo();

    const char* get_undo_description() const;
    const char* get_redo_description() const;

    void clear();
    int get_undo_count() const { return static_cast<int>(m_undo_stack.size()); }
    int get_redo_count() const { return static_cast<int>(m_redo_stack.size()); }

    // Mark the current state as the saved state (for dirty tracking)
    void mark_saved();
    bool is_dirty() const;

private:
    std::vector<std::unique_ptr<IEditorCommand>> m_undo_stack;
    std::vector<std::unique_ptr<IEditorCommand>> m_redo_stack;
    int m_saved_index = 0;  // undo_stack size at last save
};

// ============================================================================
// Concrete Commands
// ============================================================================

// Create a new widget and add it as a child
class CreateWidgetCommand : public IEditorCommand {
public:
    CreateWidgetCommand(IGuiContext* ctx, IGuiWidget* parent, WidgetType type,
                        const math::Box& bounds, const char* name = nullptr);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return m_desc.c_str(); }

    IGuiWidget* get_created_widget() const { return m_widget; }

private:
    IGuiContext* m_ctx;
    IGuiWidget* m_parent;
    IGuiWidget* m_widget = nullptr;
    WidgetType m_type;
    math::Box m_bounds;
    std::string m_name;
    std::string m_desc;
    bool m_owns_widget = false;
};

// Delete a widget and its subtree
class DeleteWidgetCommand : public IEditorCommand {
public:
    DeleteWidgetCommand(IGuiContext* ctx, IGuiWidget* widget);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return m_desc.c_str(); }

private:
    IGuiContext* m_ctx;
    IGuiWidget* m_widget;
    IGuiWidget* m_parent;
    int m_child_index = -1;
    std::string m_desc;
    bool m_owns_widget = false;
};

// Move a widget (change bounds position)
class MoveWidgetCommand : public IEditorCommand {
public:
    MoveWidgetCommand(IGuiWidget* widget, const math::Box& old_bounds, const math::Box& new_bounds);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Move widget"; }
    bool merge_with(const IEditorCommand* other) override;

private:
    IGuiWidget* m_widget;
    math::Box m_old_bounds;
    math::Box m_new_bounds;
};

// Resize a widget
class ResizeWidgetCommand : public IEditorCommand {
public:
    ResizeWidgetCommand(IGuiWidget* widget, const math::Box& old_bounds, const math::Box& new_bounds);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Resize widget"; }
    bool merge_with(const IEditorCommand* other) override;

private:
    IGuiWidget* m_widget;
    math::Box m_old_bounds;
    math::Box m_new_bounds;
};

// Change a widget property (generic via callback)
class ChangePropertyCommand : public IEditorCommand {
public:
    using ApplyFunc = std::function<void()>;

    ChangePropertyCommand(const char* desc, ApplyFunc apply, ApplyFunc revert);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return m_desc.c_str(); }

private:
    std::string m_desc;
    ApplyFunc m_apply;
    ApplyFunc m_revert;
};

// Reparent a widget (move to different parent)
class ReparentWidgetCommand : public IEditorCommand {
public:
    ReparentWidgetCommand(IGuiWidget* widget, IGuiWidget* old_parent, IGuiWidget* new_parent);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Reparent widget"; }

private:
    IGuiWidget* m_widget;
    IGuiWidget* m_old_parent;
    IGuiWidget* m_new_parent;
    int m_old_index = -1;
};

// Change widget name
class RenameWidgetCommand : public IEditorCommand {
public:
    RenameWidgetCommand(IGuiWidget* widget, const char* old_name, const char* new_name);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Rename widget"; }

private:
    IGuiWidget* m_widget;
    std::string m_old_name;
    std::string m_new_name;
};

// Change widget style
class ChangeStyleCommand : public IEditorCommand {
public:
    ChangeStyleCommand(IGuiWidget* widget, const GuiStyle& old_style, const GuiStyle& new_style);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Change style"; }

private:
    IGuiWidget* m_widget;
    GuiStyle m_old_style;
    GuiStyle m_new_style;
};

// Change widget visibility
class ChangeVisibilityCommand : public IEditorCommand {
public:
    ChangeVisibilityCommand(IGuiWidget* widget, bool new_visible);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Toggle visibility"; }

private:
    IGuiWidget* m_widget;
    bool m_old_visible;
    bool m_new_visible;
};

// Change widget size mode
class ChangeSizeModeCommand : public IEditorCommand {
public:
    ChangeSizeModeCommand(IGuiWidget* widget, SizeMode old_w, SizeMode old_h,
                          SizeMode new_w, SizeMode new_h);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Change size mode"; }

private:
    IGuiWidget* m_widget;
    SizeMode m_old_w, m_old_h;
    SizeMode m_new_w, m_new_h;
};

// Change layout direction
class ChangeLayoutCommand : public IEditorCommand {
public:
    ChangeLayoutCommand(IGuiWidget* widget, LayoutDirection old_dir, LayoutDirection new_dir);
    void execute() override;
    void undo() override;
    const char* get_description() const override { return "Change layout"; }

private:
    IGuiWidget* m_widget;
    LayoutDirection m_old_dir;
    LayoutDirection m_new_dir;
};

} // namespace editor
} // namespace gui
} // namespace window

#endif // GUI_EDITOR_COMMANDS_HPP
