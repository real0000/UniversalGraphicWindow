/*
 * gui_label.hpp - Label, TextInput, and EditBox Interfaces
 *
 * Contains IGuiLabel for text display, IGuiTextInput for single-line editable text,
 * and IGuiEditBox for multi-line text editing.
 */

#ifndef WINDOW_GUI_LABEL_HPP
#define WINDOW_GUI_LABEL_HPP

namespace window {
namespace gui {

// ============================================================================
// Label Interface - For text display
// ============================================================================

class IGuiLabel : public IGuiWidget {
public:
    virtual ~IGuiLabel() = default;

    // Text content
    virtual const char* get_text() const = 0;
    virtual void set_text(const char* text) = 0;

    // Label style
    virtual const LabelStyle& get_label_style() const = 0;
    virtual void set_label_style(const LabelStyle& style) = 0;
};

// ============================================================================
// TextInput Interface - For editable text
// ============================================================================

// Visual style for a single-line text input. Lets a caller reproduce a bespoke
// field (rounded bg, custom outline, no focus ring, placeholder colour) instead
// of the hardcoded default look.
// TextInputStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

// Enter/Escape hooks for a single-line input embedded by another widget (e.g. the
// PropertyGrid's field editor): Enter commits the text, Escape abandons the edit.
// Default no-ops so plain inputs keep their behaviour.
class ITextInputEventHandler {
public:
    virtual ~ITextInputEventHandler() = default;
    virtual void on_text_commit(const char* text) { (void)text; }
    virtual void on_text_cancel() {}
};

class IGuiTextInput : public IGuiLabel {
public:
    virtual ~IGuiTextInput() = default;

    // Enter → on_text_commit, Escape → on_text_cancel (unset: both keys fall through).
    virtual void set_text_input_event_handler(ITextInputEventHandler* handler) = 0;

    // Field style (background, outline, focus ring, placeholder colour, …).
    virtual const TextInputStyle& get_text_input_style() const = 0;
    virtual void set_text_input_style(const TextInputStyle& style) = 0;

    // Cursor and selection
    virtual int get_cursor_position() const = 0;
    virtual void set_cursor_position(int position) = 0;
    virtual int get_selection_start() const = 0;
    virtual int get_selection_length() const = 0;
    virtual void set_selection(int start, int length) = 0;
    virtual void select_all() = 0;
    virtual void clear_selection() = 0;

    // Editing
    virtual void insert_text(const char* text) = 0;
    virtual void delete_selection() = 0;
    virtual void delete_backward(int count = 1) = 0;
    virtual void delete_forward(int count = 1) = 0;

    // Placeholder
    virtual const char* get_placeholder() const = 0;
    virtual void set_placeholder(const char* placeholder) = 0;

    // Input mode
    virtual bool is_password_mode() const = 0;
    virtual void set_password_mode(bool enabled) = 0;
    virtual bool is_read_only() const = 0;
    virtual void set_read_only(bool read_only) = 0;
    virtual int get_max_length() const = 0;
    virtual void set_max_length(int max_length) = 0;

    // IME composition (preedit): in-progress, not-yet-committed text shown inline at
    // the caret (accent colour). `cursor` is the caret within the preedit. Cleared on
    // commit. Lets a custom IME (e.g. ibus) drive a retained text input.
    virtual void set_preedit(const char* text, int cursor) = 0;
    virtual void clear_preedit() = 0;
    virtual const char* get_preedit() const = 0;
};

// ============================================================================
// EditBox Interface - Multi-line text editor
// ============================================================================

enum class EditBoxWordWrap : uint8_t {
    None = 0,       // No word wrap, horizontal scroll
    Word,           // Wrap at word boundaries
    Character       // Wrap at character boundaries
};

// EditBoxStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

struct TextPosition {
    int line = 0;       // 0-based line index
    int column = 0;     // 0-based column index
};

struct TextRange {
    TextPosition start;
    TextPosition end;

    bool is_empty() const {
        return start.line == end.line && start.column == end.column;
    }
};

class IEditBoxEventHandler {
public:
    virtual ~IEditBoxEventHandler() = default;
    virtual void on_text_changed() = 0;
    virtual void on_cursor_moved(const TextPosition& position) = 0;
    virtual void on_selection_changed(const TextRange& selection) = 0;
    virtual void on_right_click(const math::Vec2& pos) {}
};

class IGuiEditBox : public IGuiWidget {
public:
    virtual ~IGuiEditBox() = default;

    // Text content
    virtual const char* get_text() const = 0;
    virtual void set_text(const char* text) = 0;
    virtual int get_text_length() const = 0;

    // Line access
    virtual int get_line_count() const = 0;
    virtual const char* get_line(int line_index) const = 0;
    virtual int get_line_length(int line_index) const = 0;
    virtual void insert_line(int line_index, const char* text) = 0;
    virtual void remove_line(int line_index) = 0;
    virtual void replace_line(int line_index, const char* text) = 0;

    // Cursor
    virtual TextPosition get_cursor_position() const = 0;
    virtual void set_cursor_position(const TextPosition& position) = 0;
    virtual void move_cursor(int line_delta, int column_delta) = 0;
    virtual void move_cursor_to_line_start() = 0;
    virtual void move_cursor_to_line_end() = 0;
    virtual void move_cursor_to_start() = 0;
    virtual void move_cursor_to_end() = 0;

    // Selection
    virtual TextRange get_selection() const = 0;
    virtual void set_selection(const TextRange& range) = 0;
    virtual void select_all() = 0;
    virtual void select_line(int line_index) = 0;
    virtual void select_word_at_cursor() = 0;
    virtual void clear_selection() = 0;
    virtual bool has_selection() const = 0;
    virtual const char* get_selected_text() const = 0;

    // Editing
    virtual void insert_text(const char* text) = 0;
    virtual void insert_text_at(const TextPosition& position, const char* text) = 0;
    virtual void delete_selection() = 0;
    virtual void delete_range(const TextRange& range) = 0;
    virtual void delete_backward(int count = 1) = 0;
    virtual void delete_forward(int count = 1) = 0;
    virtual void delete_line(int line_index) = 0;
    virtual void duplicate_line() = 0;
    virtual void move_line_up() = 0;
    virtual void move_line_down() = 0;

    // Undo/Redo
    virtual bool can_undo() const = 0;
    virtual bool can_redo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void clear_undo_history() = 0;
    virtual int get_undo_stack_size() const = 0;

    // Clipboard
    virtual void cut() = 0;
    virtual void copy() = 0;
    virtual void paste() = 0;

    // Search
    virtual TextPosition find(const char* text, const TextPosition& start, bool case_sensitive = true, bool whole_word = false) const = 0;
    virtual int replace(const char* search, const char* replacement, bool case_sensitive = true, bool whole_word = false) = 0;
    virtual int replace_all(const char* search, const char* replacement, bool case_sensitive = true, bool whole_word = false) = 0;

    // Word wrap
    virtual EditBoxWordWrap get_word_wrap() const = 0;
    virtual void set_word_wrap(EditBoxWordWrap wrap) = 0;

    // Line numbers
    virtual bool is_line_numbers_visible() const = 0;
    virtual void set_line_numbers_visible(bool visible) = 0;

    // Current line highlight
    virtual bool is_current_line_highlighted() const = 0;
    virtual void set_current_line_highlighted(bool highlight) = 0;

    // Read-only mode
    virtual bool is_read_only() const = 0;
    virtual void set_read_only(bool read_only) = 0;

    // Tab handling
    virtual bool is_tab_insert_spaces() const = 0;
    virtual void set_tab_insert_spaces(bool insert_spaces) = 0;
    virtual int get_tab_size() const = 0;
    virtual void set_tab_size(int size) = 0;

    // Scroll
    virtual int get_first_visible_line() const = 0;
    virtual void set_first_visible_line(int line_index) = 0;
    virtual int get_visible_line_count() const = 0;
    virtual void scroll_to_cursor() = 0;
    virtual void scroll_to_line(int line_index) = 0;

    // Position conversion
    virtual TextPosition position_from_point(const math::Vec2& point) const = 0;
    virtual math::Vec2 point_from_position(const TextPosition& position) const = 0;

    // Style
    virtual const EditBoxStyle& get_editbox_style() const = 0;
    virtual void set_editbox_style(const EditBoxStyle& style) = 0;

    // Event handler
    virtual void set_editbox_event_handler(IEditBoxEventHandler* handler) = 0;

    // Text measurer (optional; improves click-to-cursor accuracy)
    virtual void set_text_measurer(ITextMeasurer* measurer) = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_LABEL_HPP
