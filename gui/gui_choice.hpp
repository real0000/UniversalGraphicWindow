/*
 * gui_choice.hpp - ChoiceCard Interface
 *
 * A question plus the answers offered for it. The widget owns the whole
 * "ask the user something and collect the reply" interaction — prompt text,
 * the option rows, the inline editor for an open-ended option, and the
 * answered/unanswered look — so the host only binds data and receives the
 * answer. Nothing about what the options *mean* lives here.
 *
 * The option set is the extension point: a ChoiceOption with free_text = true
 * turns its row into an inline text entry ("Other…"), so a caller can extend a
 * fixed set with an open-ended escape hatch without the widget knowing why.
 * Further option kinds are added as model fields, never as host-side drawing.
 */

#ifndef WINDOW_GUI_CHOICE_HPP
#define WINDOW_GUI_CHOICE_HPP

namespace window {
namespace gui {

// ChoiceCardStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

// One answer offered for the prompt.
struct ChoiceOption {
    int         id = -1;
    std::string label;             // what the row shows
    std::string value;             // what is reported; falls back to label when empty
    // Picking this option opens an inline editor and the answer becomes whatever
    // the user types, rather than `value`. The open-ended "Other…" row.
    bool        free_text = false;
    std::string placeholder;       // hint shown in the editor (free_text rows)
    bool        enabled = true;
    math::Vec4  color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);  // row tint; alpha 0 = style default
};

class IChoiceCardEventHandler {
public:
    virtual ~IChoiceCardEventHandler() = default;
    // The user answered. `value` is the chosen option's value (or label), or the
    // typed text for a free_text option. Fired once — the card is answered after.
    virtual void on_choice(int option_id, const char* value) = 0;
};

class IGuiChoiceCard : public IGuiWidget {
public:
    virtual ~IGuiChoiceCard() = default;

    // The question. Embedded newlines are honoured; the card grows to fit.
    virtual void set_prompt(const char* text) = 0;
    virtual const char* get_prompt() const = 0;

    // Replace the whole option set in one idempotent call (an unchanged model
    // schedules no repaint), mirroring IGuiListBox::set_items.
    virtual void set_options(std::vector<ChoiceOption> options) = 0;
    virtual int get_option_count() const = 0;

    // The answer is DATA the host owns: set it to render the card resolved
    // (options hidden, answer shown), or clear it to ask again. Setting it does
    // not fire on_choice.
    virtual void set_answer(const char* value) = 0;
    virtual const char* get_answer() const = 0;
    virtual bool is_answered() const = 0;

    // Semantic driving (automation / tests): fire exactly what a click would, by
    // option id and with no geometry. activate_option picks a plain option (and
    // opens the editor for a free_text one); submit_free_text answers a free_text
    // option directly. False if the id isn't present.
    virtual bool activate_option(int option_id) = 0;
    virtual bool submit_free_text(int option_id, const char* text) = 0;

    // Style
    virtual const ChoiceCardStyle& get_choice_card_style() const = 0;
    virtual void set_choice_card_style(const ChoiceCardStyle& style) = 0;

    // Event handler
    virtual void set_choice_event_handler(IChoiceCardEventHandler* handler) = 0;

    // Text measurement for prompt wrapping (optional; same measurer the host
    // gives its other widgets). Without one the prompt only breaks on newlines.
    virtual void set_text_measurer(ITextMeasurer* measurer) = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_CHOICE_HPP
