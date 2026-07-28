/*
 * gui_choice.cpp - ChoiceCard Implementation
 *
 * The card owns the interaction end to end: it draws its own chrome (frame,
 * prompt, resolved answer) and composes two existing generics for the parts that
 * already exist — a ListBox for the option rows and a TextInput for the inline
 * editor a free_text option opens. The host binds prompt + options and receives
 * on_choice; it never positions a row or tracks which option is "the other one".
 */

#include "gui_widget_base.hpp"

namespace window { namespace gui {
IGuiListBox*   create_list_box_widget();
IGuiTextInput* create_text_input_widget();
} }

#include <algorithm>

namespace window {
namespace gui {

class GuiChoiceCard : public WidgetBase<IGuiChoiceCard, WidgetType::Custom> {
    std::string prompt_, answer_;
    bool answered_ = false;
    std::vector<ChoiceOption> options_;
    ChoiceCardStyle style_ = ChoiceCardStyle::default_style();
    IChoiceCardEventHandler* handler_ = nullptr;
    ITextMeasurer* measurer_ = nullptr;

    IGuiListBox*   list_   = nullptr;
    IGuiTextInput* editor_ = nullptr;
    int editing_id_ = -1;              // free_text option awaiting typed input (-1 = none)

    mutable WidgetRenderInfo ri_;
    mutable std::vector<std::string> wrap_cache_;
    mutable std::string wrap_key_;

    // ── children ─────────────────────────────────────────────────────────────

    struct ListH : IListBoxEventHandler {
        GuiChoiceCard* c = nullptr;
        void on_item_selected(int id) override { if (c) c->pick(id); }
        void on_item_double_clicked(int) override {}
    } list_h_;

    struct EditH : ITextInputEventHandler {
        GuiChoiceCard* c = nullptr;
        void on_text_commit(const char* t) override { if (c) c->commit_free_text(t); }
        void on_text_cancel() override { if (c) c->cancel_free_text(); }
    } edit_h_;

    const ChoiceOption* find(int id) const {
        for (const auto& o : options_) if (o.id == id) return &o;
        return nullptr;
    }

    void ensure_children() {
        if (!list_) {
            list_ = create_list_box_widget();
            if (list_) {
                list_h_.c = this;
                list_->set_selection_mode(ListBoxSelectionMode::Single);
                list_->set_list_event_handler(&list_h_);
                base_.add_child(list_);
            }
        }
        if (!editor_) {
            editor_ = create_text_input_widget();
            if (editor_) {
                edit_h_.c = this;
                editor_->set_text_input_event_handler(&edit_h_);
                editor_->set_visible(false);
                if (measurer_) editor_->set_text_measurer(measurer_);
                base_.add_child(editor_);
            }
        }
    }

    // Push the option model into the list. Idempotent: ListBox::set_items already
    // no-ops on an unchanged model.
    void sync_list() {
        if (!list_) return;
        std::vector<ListItemModel> rows;
        rows.reserve(options_.size());
        for (const auto& o : options_) {
            ListItemModel m;
            m.id        = o.id;
            m.text      = o.label;
            m.enabled   = o.enabled;
            m.row_color = o.color;
            rows.push_back(std::move(m));
        }
        list_->set_items(std::move(rows));
        list_->set_visible(!answered_);
    }

    // ── geometry ─────────────────────────────────────────────────────────────

    float prompt_line_h() const { return style_.prompt_font_size * style_.prompt_line_height; }

    float options_h() const {
        if (options_.empty()) return 0.0f;
        return options_.size() * style_.option_height +
               (options_.size() - 1) * style_.option_gap;
    }

    // Break the prompt to `width`. Newlines always break; the measurer (when the
    // host supplied one) also wraps long lines on spaces.
    const std::vector<std::string>& wrap(float width) const {
        char keybuf[32];
        std::snprintf(keybuf, sizeof keybuf, "|%.1f", width);
        const std::string key = prompt_ + keybuf;
        if (key == wrap_key_) return wrap_cache_;
        wrap_key_ = key;
        wrap_cache_.clear();

        std::size_t start = 0;
        while (start <= prompt_.size()) {
            std::size_t nl = prompt_.find('\n', start);
            std::string line = prompt_.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            if (!measurer_ || width <= 0.0f ||
                math::x(measurer_->measure_text(line.c_str(), style_.prompt_font_size)) <= width) {
                wrap_cache_.push_back(std::move(line));
            } else {
                // Greedy space wrap; a word longer than the line is left to clip.
                std::string cur;
                std::size_t p = 0;
                while (p < line.size()) {
                    std::size_t sp = line.find(' ', p);
                    std::string word = line.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
                    std::string trial = cur.empty() ? word : cur + " " + word;
                    if (!cur.empty() &&
                        math::x(measurer_->measure_text(trial.c_str(), style_.prompt_font_size)) > width) {
                        wrap_cache_.push_back(cur);
                        cur = word;
                    } else {
                        cur.swap(trial);
                    }
                    if (sp == std::string::npos) break;
                    p = sp + 1;
                }
                if (!cur.empty()) wrap_cache_.push_back(cur);
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        if (wrap_cache_.empty()) wrap_cache_.push_back(std::string());
        return wrap_cache_;
    }

    float content_width() const {
        return std::max(0.0f, math::box_width(base_.get_bounds()) - style_.padding * 2.0f);
    }

    void relayout() {
        if (!list_) return;
        const auto b = base_.get_bounds();
        const float x = math::x(math::box_min(b)) + style_.padding;
        const float w = content_width();
        float y = math::y(math::box_min(b)) + style_.padding;

        y += wrap(w).size() * prompt_line_h() + style_.prompt_gap;

        const bool show_opts = !answered_ && !options_.empty();
        list_->set_visible(show_opts);
        if (show_opts) {
            list_->set_bounds(math::make_box(x, y, w, options_h()));
            y += options_h();
        }
        const bool show_editor = show_opts && editing_id_ >= 0;
        if (editor_) {
            editor_->set_visible(show_editor);
            if (show_editor) {
                y += style_.option_gap;
                editor_->set_bounds(math::make_box(x, y, w, style_.editor_height));
            }
        }
    }

    // ── interaction ──────────────────────────────────────────────────────────

    void pick(int id) {
        if (answered_) return;
        const ChoiceOption* o = find(id);
        if (!o || !o->enabled) return;
        if (o->free_text) {
            editing_id_ = id;
            if (editor_) {
                editor_->set_text("");
                editor_->set_placeholder(o->placeholder.c_str());
                editor_->set_visible(true);
                editor_->set_focus(true);
            }
            relayout();
            base_.mark_dirty();
            return;
        }
        answer_with(id, o->value.empty() ? o->label : o->value);
    }

    void commit_free_text(const char* text) {
        if (answered_ || editing_id_ < 0) return;
        const std::string s = text ? text : "";
        if (s.empty()) return;              // empty answer: keep asking
        answer_with(editing_id_, s);
    }

    void cancel_free_text() {
        editing_id_ = -1;
        if (editor_) editor_->set_visible(false);
        relayout();
        base_.mark_dirty();
    }

    void answer_with(int id, const std::string& value) {
        answer_   = value;
        answered_ = true;
        editing_id_ = -1;
        if (editor_) editor_->set_visible(false);
        if (list_)   list_->set_visible(false);
        relayout();
        base_.mark_dirty();
        if (handler_) handler_->on_choice(id, answer_.c_str());
    }

public:
    GuiChoiceCard() { ensure_children(); }

    // ── IGuiChoiceCard ───────────────────────────────────────────────────────

    void set_prompt(const char* t) override {
        const std::string s = t ? t : "";
        if (s == prompt_) return;
        prompt_ = s;
        wrap_key_.clear();
        relayout();
        base_.mark_dirty();
    }
    const char* get_prompt() const override { return prompt_.c_str(); }

    void set_options(std::vector<ChoiceOption> opts) override {
        bool same = opts.size() == options_.size();
        for (std::size_t i = 0; same && i < opts.size(); ++i) {
            const auto& a = opts[i]; const auto& b = options_[i];
            same = a.id == b.id && a.label == b.label && a.value == b.value &&
                   a.free_text == b.free_text && a.enabled == b.enabled;
        }
        if (same) return;                     // unchanged → no repaint
        options_ = std::move(opts);
        if (editing_id_ >= 0 && !find(editing_id_)) editing_id_ = -1;
        sync_list();
        relayout();
        base_.mark_dirty();
    }
    int get_option_count() const override { return (int)options_.size(); }

    void set_answer(const char* v) override {
        const std::string s = v ? v : "";
        if (s == answer_ && answered_ == !s.empty()) return;
        answer_   = s;
        answered_ = !s.empty();
        if (answered_) editing_id_ = -1;
        sync_list();
        relayout();
        base_.mark_dirty();
    }
    const char* get_answer() const override { return answer_.c_str(); }
    bool is_answered() const override { return answered_; }

    bool activate_option(int id) override {
        if (!find(id)) return false;
        pick(id);
        return true;
    }
    bool submit_free_text(int id, const char* text) override {
        const ChoiceOption* o = find(id);
        if (!o || !o->free_text || answered_) return false;
        editing_id_ = id;
        commit_free_text(text);
        return answered_;
    }

    const ChoiceCardStyle& get_choice_card_style() const override { return style_; }
    void set_choice_card_style(const ChoiceCardStyle& s) override {
        style_ = s;
        wrap_key_.clear();
        relayout();
        base_.mark_dirty();
    }
    void set_choice_event_handler(IChoiceCardEventHandler* h) override { handler_ = h; }
    void set_text_measurer(ITextMeasurer* m) override {
        measurer_ = m;
        if (editor_) editor_->set_text_measurer(m);
        wrap_key_.clear();
        base_.mark_dirty();
    }

    // ── IGuiWidget ───────────────────────────────────────────────────────────

    void set_bounds(const math::Box& b) override { base_.set_bounds(b); relayout(); }

    // Height-for-width: the card reports what it needs so a host sizer can place
    // it without the host knowing the card's internal structure.
    math::Vec2 get_preferred_size() const override {
        const float w = math::box_width(base_.get_bounds());
        float h = style_.padding * 2.0f + wrap(std::max(0.0f, w - style_.padding * 2.0f)).size() * prompt_line_h();
        if (!answered_ && !options_.empty()) {
            h += style_.prompt_gap + options_h();
            if (editing_id_ >= 0) h += style_.option_gap + style_.editor_height;
        } else if (answered_) {
            h += style_.prompt_gap + prompt_line_h();
        }
        return math::Vec2(w, h);
    }

    int get_child_count() const override { return base_.get_child_count(); }
    IGuiWidget* get_child(int i) const override { return base_.get_child(i); }
    void layout_children() override { relayout(); }

    IGuiWidget* find_widget_at(const math::Vec2& p) override {
        if (!hit_test(p)) return nullptr;
        if (IGuiWidget* w = base_.find_widget_at(p)) return w;
        return static_cast<IGuiWidget*>(this);
    }
    bool handle_mouse_move(const math::Vec2& p) override { return base_.handle_mouse_move(p); }
    bool handle_mouse_button(MouseButton b, bool pressed, const math::Vec2& p) override {
        return base_.handle_mouse_button(b, pressed, p);
    }
    bool handle_key(int c, bool pressed, int mods) override { return base_.handle_key(c, pressed, mods); }
    bool handle_text_input(const char* t) override { return base_.handle_text_input(t); }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        const auto b = base_.get_bounds();
        const float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        const float bw = math::box_width(b), bh = math::box_height(b);
        const auto noclip = math::make_box(0, 0, 0, 0);
        int32_t d = 0;

        if (style_.background_color.w > 0.0f) {
            if (style_.corner_radius > 0.0f)
                ri_.push_round_rect(bx, by, bw, bh, style_.corner_radius, style_.background_color, d++, noclip);
            else
                ri_.push_rect(bx, by, bw, bh, style_.background_color, d++, noclip);
        }

        const float tx = bx + style_.padding, tw = std::max(0.0f, bw - style_.padding * 2.0f);
        float ty = by + style_.padding;
        const float lh = prompt_line_h();
        for (const auto& line : wrap(tw)) {
            ri_.push_text(line.c_str(), tx, ty, tw, lh, style_.prompt_color,
                          style_.prompt_font_size, Alignment::CenterLeft, d++, b);
            ty += lh;
        }

        // Resolved: the options are gone, so show what was answered in their place.
        if (answered_) {
            ty += style_.prompt_gap;
            const std::string line = "\xE2\x86\x92 " + answer_;   // "→ "
            ri_.push_text(line.c_str(), tx, ty, tw, lh, style_.answer_color,
                          style_.prompt_font_size, Alignment::CenterLeft, d++, b);
        }

        if (style_.border_color.w > 0.0f)
            ri_.push_outline(bx, by, bw, bh, style_.border_color, d, noclip);

        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

IGuiChoiceCard* create_choice_card_widget() { return new GuiChoiceCard(); }

} // namespace gui
} // namespace window
