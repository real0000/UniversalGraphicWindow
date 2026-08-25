/*
 * gui_label.cpp - Label, TextInput, and EditBox Implementations
 */

#include "gui_widget_base.hpp"
#include <algorithm>
#include <sstream>

namespace window {
namespace gui {

// ============================================================================
// GuiLabel
// ============================================================================

class GuiLabel : public WidgetBase<IGuiLabel, WidgetType::Label> {
    std::string text_;
    LabelStyle label_style_ = LabelStyle::default_style();
    ITextMeasurer* measurer_ = nullptr;
    math::Vec2 explicit_pref_{0.0f, 0.0f};   // pinned size; 0 component = auto/measure
    mutable WidgetRenderInfo ri_;
public:
    const char* get_text() const override { return text_.c_str(); }
    void apply_bound_text(const char* t) override { set_text(t); }   // IGuiWidget::bind_text
    // Dirty-on-change (see GuiWidget::set_style): re-bound text/looks must repaint.
    void set_text(const char* t) override {
        const char* nt = t ? t : "";
        if (text_ == nt) return;
        text_ = nt; base_.mark_dirty();
    }
    const LabelStyle& get_label_style() const override { return label_style_; }
    void set_label_style(const LabelStyle& s) override {
        const LabelStyle& o = label_style_;
        const bool same = o.text_color.x == s.text_color.x && o.text_color.y == s.text_color.y &&
                          o.text_color.z == s.text_color.z && o.text_color.w == s.text_color.w &&
                          o.font_size == s.font_size && o.alignment == s.alignment &&
                          o.wrap == s.wrap && o.ellipsis == s.ellipsis && o.font_name == s.font_name;
        label_style_ = s;
        if (!same) base_.mark_dirty();
    }
    void set_text_measurer(ITextMeasurer* m) override { measurer_ = m; }
    void set_preferred_size(const math::Vec2& s) override { explicit_pref_ = s; }

    // Self-size to one line of text (measured) when a measurer is attached, so a
    // sizer can place the label with no caller-side height. Height is the font em
    // (font_size), i.e. a tight single-line box — matches a caller that reserves
    // exactly one font's worth of vertical space per line. render() insets text by
    // +4 on the left (see below); mirror that as horizontal padding.
    math::Vec2 get_preferred_size() const override {
        math::Vec2 base = base_.get_preferred_size();
        if (measurer_) {
            float tw = text_.empty() ? 0.0f
                     : measurer_->measure_text(text_.c_str(), label_style_.font_size, label_style_.font_name).x();
            base = math::Vec2(tw + 8.0f, label_style_.font_size);
        }
        float w = (math::x(explicit_pref_) > 0.0f) ? math::x(explicit_pref_) : math::x(base);
        float h = (math::y(explicit_pref_) > 0.0f) ? math::y(explicit_pref_) : math::y(base);
        return math::Vec2(w, h);
    }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        auto noclip = math::make_box(0,0,0,0);
        int32_t d = 0;
        if (!text_.empty())
            ri_.push_text(text_.c_str(), bx, by, bw, bh,
                          label_style_.text_color, label_style_.font_size,
                          label_style_.alignment, d++, noclip);
        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

// ============================================================================
// GuiTextInput
// ============================================================================

class GuiTextInput : public WidgetBase<IGuiTextInput, WidgetType::TextInput>,
                     public ITextEditTarget {
public:
    bool wants_caret_blink() const override { return !read_only_; }  // blinking caret when editable+focused
    void apply_bound_text(const char* t) override { set_text(t); }   // IGuiWidget::bind_text
    void bind_placeholder(std::function<std::string()> p) override { ph_bind_ = std::move(p); base_.mark_dirty(); }
    void bind_read_only(std::function<bool()> p) override { ro_bind_ = std::move(p); base_.mark_dirty(); }
protected:
    void refresh_providers() override {
        if (ph_bind_) set_placeholder(ph_bind_().c_str());
        if (ro_bind_) set_read_only(ro_bind_());
    }
public:
private:
    std::string text_, placeholder_, preedit_;
    std::function<std::string()> ph_bind_;   // bind_placeholder
    std::function<bool()> ro_bind_;          // bind_read_only
    LabelStyle label_style_ = LabelStyle::default_style();
    TextInputStyle ti_style_ = TextInputStyle::default_style();
    // Selection = [min(anchor_,cursor_), max]. anchor_==cursor_ means no selection.
    int cursor_ = 0, anchor_ = 0, max_length_ = 0, preedit_cursor_ = 0;
    bool password_ = false, read_only_ = false, dragging_ = false;
    ITextMeasurer* measurer_ = nullptr;   // maps a click x → caret char index
    mutable WidgetRenderInfo ri_;
    // Key codes from window::Key enum
    enum : int { K_Escape=300, K_Enter=308, K_Backspace=309, K_Delete=310,
                 K_Home=312, K_End=313, K_Left=316, K_Right=317 };
    enum { MOD_CTRL = 2 };   // KeyMod::Control
    ITextInputEventHandler* input_handler_ = nullptr;
    enum { MOD_SHIFT = 1 };   // handle_key modifier bits (see the chat's on_key)
    int  sel_lo() const { return std::min(anchor_, cursor_); }
    int  sel_hi() const { return std::max(anchor_, cursor_); }
    bool has_sel() const { return anchor_ != cursor_; }
    // UTF-8 code-point boundary helpers (skip 0b10xxxxxx continuation bytes) so the
    // caret/selection never land mid-glyph — CJK is 2–4 bytes per character. Mirrors
    // gui.hpp's TextEditState so the widget field edits CJK exactly like the immediate one.
    int  snap(int i) const { i = std::max(0, std::min(i, (int)text_.size()));
                             while (i > 0 && ((unsigned char)text_[i] & 0xC0) == 0x80) --i; return i; }
    int  prev_i(int i) const { i = snap(i); if (i > 0) { --i; while (i > 0 && ((unsigned char)text_[i] & 0xC0) == 0x80) --i; } return i; }
    int  next_i(int i) const { int n = (int)text_.size(); i = snap(i); if (i < n) { ++i; while (i < n && ((unsigned char)text_[i] & 0xC0) == 0x80) ++i; } return i; }
    void clamp_caret() { cursor_ = snap(cursor_); }
    // x where the text starts (matches get_render_info + the flatten's +2 nudge).
    float text_origin_x() const {
        return math::x(math::box_min(base_.get_bounds())) + ti_style_.padding + 2.0f;
    }
    int caret_from_x(float px) const {
        // The one caret-mapping path: the measurer maps a click to a byte offset with
        // the SAME layout the renderer draws, so click and drawn caret always agree.
        return measurer_ ? measurer_->caret_index_at(text_.c_str(), px - text_origin_x(), ti_style_.font_size) : cursor_;
    }
public:
    bool is_focusable() const override { return true; }
    void set_text_measurer(ITextMeasurer* m) override { measurer_ = m; }
    // Click positions the caret (drops selection); drag extends it (rubber-band select).
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_visible()) return false;
        if (btn == MouseButton::Left && pressed && base_.hit_test(p)) {
            base_.set_focus(true);
            if (preedit_.empty()) { cursor_ = caret_from_x(math::x(p)); clamp_caret(); }
            anchor_ = cursor_; dragging_ = true;
            base_.mark_dirty();
            return true;
        }
        if (btn == MouseButton::Left && !pressed) dragging_ = false;
        return WidgetBase::handle_mouse_button(btn, pressed, p);
    }
    bool handle_mouse_move(const math::Vec2& p) override {
        if (dragging_ && preedit_.empty()) { cursor_ = caret_from_x(math::x(p)); clamp_caret(); base_.mark_dirty(); return true; }
        return WidgetBase::handle_mouse_move(p);
    }
    bool handle_text_input(const char* t) override {
        if (!base_.is_visible()) return false;   // a hidden (dismissed) editor takes nothing
        if (!read_only_ && t) {
            if (has_sel()) delete_selection();
            std::string s(t);
            if (max_length_ > 0 && (int)(text_.size() + s.size()) > max_length_) return false;
            text_.insert(cursor_, s); cursor_ += (int)s.size(); anchor_ = cursor_;
            base_.mark_dirty();
        }
        return true;
    }
    bool handle_preedit(const char* t, int cursor) override {   // IME composing (shown inline)
        if (read_only_ || !base_.is_visible()) return false;
        if (t && *t) set_preedit(t, cursor); else clear_preedit();
        return true;
    }
    // code = window::Key; mods bit MOD_SHIFT extends the selection instead of collapsing it.
    bool handle_key(int code, bool pressed, int mods) override {
        if (!pressed || !base_.is_visible()) return false;
        const bool shift = (mods & MOD_SHIFT) != 0;
        auto move = [&](int to) { cursor_ = std::max(0, std::min(to, (int)text_.size())); if (!shift) anchor_ = cursor_; base_.mark_dirty(); };
        switch (code) {
            case K_Left:  if (!shift && has_sel()) { cursor_ = anchor_ = sel_lo(); base_.mark_dirty(); } else move(prev_i(cursor_)); return true;
            case K_Right: if (!shift && has_sel()) { cursor_ = anchor_ = sel_hi(); base_.mark_dirty(); } else move(next_i(cursor_)); return true;
            case K_Home:  move(0); return true;
            case K_End:   move((int)text_.size()); return true;
            case K_Backspace: if (!read_only_) { if (has_sel()) delete_selection(); else delete_backward(1); base_.mark_dirty(); } return true;
            case K_Delete:    if (!read_only_) { if (has_sel()) delete_selection(); else delete_forward(1); base_.mark_dirty(); } return true;
            case K_Enter:  if (input_handler_) { input_handler_->on_text_commit(text_.c_str()); return true; } return false;
            case K_Escape: if (input_handler_) { input_handler_->on_text_cancel(); return true; } return false;
        }
        if (mods & MOD_CTRL) {   // shared clipboard shortcuts (system clipboard)
            if (code == 'A') { select_all(); base_.mark_dirty(); return true; }
            if (code == 'C') { if (has_sel()) clipboard_set_text(text_selected().c_str()); return true; }
            if (code == 'X') { if (has_sel() && !read_only_) {
                                   clipboard_set_text(text_selected().c_str());
                                   delete_selection(); base_.mark_dirty(); } return true; }
            if (code == 'V') { if (!read_only_) { const std::string c = clipboard_get_text();
                                   if (!c.empty()) { insert_text(c.c_str()); base_.mark_dirty(); } } return true; }
        }
        return false;
    }
    void set_text_input_event_handler(ITextInputEventHandler* h) override { input_handler_ = h; }
    // ── shared text-edit surface (built-in right-click menu / Ctrl combos) ──
    ITextEditTarget* text_edit_target() override { return this; }
    bool text_is_read_only() const override { return read_only_; }
    bool text_has_selection() const override { return has_sel(); }
    std::string text_selected() const override { return text_.substr((size_t)sel_lo(), (size_t)(sel_hi() - sel_lo())); }
    void text_replace_selection(const char* u) override {
        if (read_only_) return;
        if (has_sel()) delete_selection();
        if (u && *u) insert_text(u);
        base_.mark_dirty();
    }
    void text_select_all() override { select_all(); base_.mark_dirty(); }
    const char* get_text() const override { return text_.c_str(); }
    void set_text(const char* t) override {
        const char* nt = t ? t : "";
        if (text_ == nt) return;               // dirty-on-change (idempotent rebinds)
        text_ = nt;
        cursor_ = snap(std::min(cursor_, (int)text_.size())); anchor_ = cursor_;
        base_.mark_dirty();
    }
    const LabelStyle& get_label_style() const override { return label_style_; }
    void set_label_style(const LabelStyle& s) override { label_style_ = s; }
    const TextInputStyle& get_text_input_style() const override { return ti_style_; }
    void set_text_input_style(const TextInputStyle& s) override { ti_style_ = s; }
    int get_cursor_position() const override { return cursor_; }
    void set_cursor_position(int p) override { cursor_ = snap(p); anchor_ = cursor_; }
    int get_selection_start() const override { return sel_lo(); }
    int get_selection_length() const override { return sel_hi() - sel_lo(); }
    void set_selection(int s, int l) override { anchor_ = snap(s); cursor_ = snap(s + l); }
    void select_all() override { anchor_ = 0; cursor_ = (int)text_.size(); }
    void clear_selection() override { anchor_ = cursor_; }
    void insert_text(const char* t) override {
        if (!t || read_only_) return; delete_selection();
        std::string s(t); text_.insert(cursor_, s); cursor_ += (int)s.size(); anchor_ = cursor_;
    }
    void delete_selection() override {
        if (has_sel()) { int lo = sel_lo(); text_.erase(lo, sel_hi() - lo); cursor_ = anchor_ = lo; }
    }
    void delete_backward(int n) override {   // n = characters (code points), not bytes
        while (n-- > 0 && cursor_ > 0) { int p = prev_i(cursor_); text_.erase(p, cursor_ - p); cursor_ = p; }
        anchor_ = cursor_;
    }
    void delete_forward(int n) override {    // n = characters (code points), not bytes
        while (n-- > 0 && cursor_ < (int)text_.size()) { int q = next_i(cursor_); text_.erase(cursor_, q - cursor_); }
        anchor_ = cursor_;
    }
    const char* get_placeholder() const override { return placeholder_.c_str(); }
    void set_placeholder(const char* p) override { const char* n = p ? p : ""; if (placeholder_ == n) return; placeholder_ = n; base_.mark_dirty(); }
    bool is_password_mode() const override { return password_; }
    void set_password_mode(bool e) override { if (password_ == e) return; password_ = e; base_.mark_dirty(); }
    bool is_read_only() const override { return read_only_; }
    // Also gates the caret blink (wants_caret_blink), so the context re-arms its
    // timer on the repaint this schedules.
    void set_read_only(bool r) override { if (read_only_ == r) return; read_only_ = r; base_.mark_dirty(); }
    int get_max_length() const override { return max_length_; }
    void set_max_length(int m) override { max_length_ = m; }
    void set_preedit(const char* t, int c) override {   // c = IME cursor in code points → byte offset
        preedit_ = t ? t : ""; int b = 0;
        for (int k = 0; k < c && b < (int)preedit_.size(); ++k) {
            ++b; while (b < (int)preedit_.size() && ((unsigned char)preedit_[b] & 0xC0) == 0x80) ++b;
        }
        preedit_cursor_ = b;
    }
    void clear_preedit() override { preedit_.clear(); preedit_cursor_ = 0; }
    const char* get_preedit() const override { return preedit_.c_str(); }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        auto noclip = math::make_box(0,0,0,0);
        const auto& s = ti_style_;
        int32_t d = 0;
        // Background (rounded if requested) + outline + optional focus ring.
        if (s.corner_radius > 0.0f)
            ri_.push_round_rect(bx, by, bw, bh, s.corner_radius, s.background_color, d++, noclip);
        else
            ri_.push_rect(bx, by, bw, bh, s.background_color, d++, noclip);
        if (s.border_color.w > 0.0f)
            ri_.push_outline(bx, by, bw, bh, s.border_color, d, noclip);
        if (base_.has_focus() && s.focus_border_color.w > 0.0f)
            ri_.push_outline(bx-1, by-1, bw+2, bh+2, s.focus_border_color, d, noclip);

        const float tx = bx + s.padding, tw = bw - 2.0f * s.padding;
        const bool empty = text_.empty() && preedit_.empty();
        // Placeholder shows while empty — even when focused (matches the original
        // field) unless the caller opts into hide-on-focus.
        if (empty && !placeholder_.empty() && !(s.hide_placeholder_on_focus && base_.has_focus()))
            ri_.push_text(placeholder_.c_str(), tx, by, tw, bh, s.placeholder_color,
                          s.font_size, Alignment::CenterLeft, d++, noclip);
        // Text (+ inline IME preedit) and caret. An empty-but-focused field still
        // emits a TextCmd so the caret blinks at the start.
        if (!empty || base_.has_focus()) {
            std::string disp = text_;
            int caret = cursor_;
            if (!preedit_.empty()) {
                disp.insert(std::min(cursor_, (int)disp.size()), preedit_);
                caret = cursor_ + preedit_cursor_;
            }
            WidgetRenderInfo::TextCmd tc;
            tc.text      = std::move(disp);   // last use of disp — steal the buffer
            tc.dest      = math::make_box(tx, by, tw, bh);
            tc.color     = s.text_color;
            tc.font_size = s.font_size;
            tc.alignment = Alignment::CenterLeft;
            tc.depth     = d++;
            tc.clip      = noclip;
            if (base_.has_focus()) {
                tc.show_cursor  = true;
                tc.cursor_pos   = caret;
                tc.cursor_color = s.cursor_color;
                if (has_sel() && preedit_.empty()) {   // selection highlight (behind the glyphs)
                    tc.sel_start    = sel_lo();
                    tc.sel_end      = sel_hi();
                    tc.sel_bg_color = s.selection_color;
                }
            }
            ri_.texts.push_back(std::move(tc));
        }
        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

// ============================================================================
// GuiEditBox
// ============================================================================

class GuiEditBox : public WidgetBase<IGuiEditBox, WidgetType::Custom>,
                   public ITextEditTarget {
public:
    bool wants_caret_blink() const override { return !read_only_; }  // read-only bubbles never blink
    void apply_bound_text(const char* t) override { set_text(t); }   // IGuiWidget::bind_text
private:
    std::vector<std::string> lines_{""};
    // Word-wrap: visual display lines derived from lines_ + width (cached). When
    // wrap_ != None each logical line is greedily split to fit the content width
    // (same algorithm the immediate-mode chat used) so rendering matches exactly.
    mutable std::vector<std::string> disp_lines_;
    // Per display line: which logical line (paragraph) it came from, and the byte
    // offset within that paragraph where it starts. These map the caret (stored in
    // LOGICAL line/column) to the DISPLAY line it renders on — without them the caret
    // compares a display index against a logical one and sticks on the first wrapped
    // row. With wrap == None they are the identity (disp_para_[d]=d, disp_col0_[d]=0).
    mutable std::vector<int> disp_para_, disp_col0_;
    mutable float disp_w_ = -1.0f;
    mutable EditBoxWordWrap disp_wrap_cached_ = EditBoxWordWrap::None;
    mutable bool disp_dirty_ = true;
    void rewrap(float content_w) const {
        if (!disp_dirty_ && content_w == disp_w_ && wrap_ == disp_wrap_cached_) return;
        disp_dirty_ = false; disp_w_ = content_w; disp_wrap_cached_ = wrap_;
        disp_lines_.clear(); disp_para_.clear(); disp_col0_.clear();
        auto emit = [&](std::string s, int para, int col0) {
            disp_lines_.push_back(std::move(s)); disp_para_.push_back(para); disp_col0_.push_back(col0);
        };
        if (wrap_ == EditBoxWordWrap::None || !measurer_ || content_w <= 0.0f) {
            for (int p = 0; p < (int)lines_.size(); ++p) emit(lines_[p], p, 0);
            if (disp_lines_.empty()) emit("", 0, 0);
            return;
        }
        for (int p = 0; p < (int)lines_.size(); ++p) {
            const std::string& para = lines_[p];
            if (para.empty()) { emit("", p, 0); continue; }
            std::string line; size_t i = 0, len = para.size();
            int line_start = 0;                       // byte offset in `para` where `line` began
            while (i < len) {
                size_t ws = i; while (ws < len && para[ws] == ' ') ++ws;
                size_t we = ws; while (we < len && para[we] != ' ') ++we;
                std::string word = para.substr(i, we - i), cand = line + word;
                if (!line.empty() && measurer_->measure_text(cand.c_str(), style_.font_size, style_.font_name).x() > content_w) {
                    emit(line, p, line_start); line = para.substr(ws, we - ws); line_start = (int)ws;
                } else line = cand;
                i = we;
            }
            emit(line, p, line_start);
        }
        if (disp_lines_.empty()) emit("", 0, 0);
    }
    // logical caret (para,column) → the display line it sits on + column within it.
    void to_disp(const TextPosition& lp, int& dline, int& dcol) const {
        dline = 0; dcol = lp.column;
        for (int d = 0; d < (int)disp_lines_.size(); ++d) {
            if (disp_para_[d] != lp.line) { if (disp_para_[d] > lp.line) break; else continue; }
            if (disp_col0_[d] <= lp.column) { dline = d; dcol = lp.column - disp_col0_[d]; }
            else break;
        }
        dcol = std::max(0, std::min(dcol, (int)disp_lines_[dline].size()));
    }
    // display (line,column) → logical caret. Used to map a click back to the model.
    TextPosition to_logical(int dline, int dcol) const {
        dline = std::max(0, std::min(dline, (int)disp_lines_.size() - 1));
        dcol  = std::max(0, std::min(dcol, (int)disp_lines_[dline].size()));
        return { disp_para_[dline], disp_col0_[dline] + dcol };
    }
    // Height-for-width preferred size: the editbox is as tall as its wrapped
    // content at the width it has been assigned. A sizer that Expands this
    // widget on the cross axis (width) assigns the width first (see BoxSizer's
    // cross pre-pass), then reads this height — so a paragraph stacks to exactly
    // the number of visual lines it wraps to, with no caller-side arithmetic.
    math::Vec2 pref_size_for_width(float bw) const {
        const auto& s = style_;
        float text_x_off = (line_nums_ ? s.gutter_width + s.padding : s.padding);
        float content_w  = bw - text_x_off - s.padding;
        rewrap(content_w);
        float line_h = s.font_size * s.line_height;
        return math::Vec2(bw, (float)disp_lines_.size() * line_h);
    }
    mutable std::string cached_text_, cached_sel_;
    TextPosition cursor_{0,0};
    TextRange selection_{{0,0},{0,0}};
    EditBoxStyle style_ = EditBoxStyle::default_style();
    IEditBoxEventHandler* handler_ = nullptr;
    EditBoxWordWrap wrap_ = EditBoxWordWrap::None;
    bool line_nums_=true, hl_line_=true, read_only_=false, tab_spaces_=true;
    int tab_size_=4, first_vis_=0;
    bool sb_drag_=false, click_drag_=false;
    // User-resize grip (set_user_resizable): rz_h_ is the current dragged height
    // (0 = never dragged, follow the assigned bounds); rz_drag_ tracks the drag with
    // its start mouse-y and start height.
    bool resizable_=false, rz_drag_=false;
    float rz_min_=0.0f, rz_max_=0.0f, rz_h_=0.0f, rz_dy0_=0.0f, rz_h0_=0.0f;
    static constexpr float kGrip = 14.0f;   // grip square edge, px
    ITextMeasurer* measurer_ = nullptr;
    mutable WidgetRenderInfo ri_;
    // Scroll/height are in DISPLAY lines (wrap makes that > logical lines); falls
    // back to logical before the first rewrap.
    int disp_count() const { return disp_lines_.empty() ? (int)lines_.size() : (int)disp_lines_.size(); }
    float content_height() const { return (float)disp_count() * style_.font_size * style_.line_height; }
    // Bottom-right drag-grip box, in screen coords (empty when not resizable).
    math::Box grip_rect() const {
        if (!resizable_) return math::make_box(0,0,0,0);
        auto b = base_.get_bounds();
        const float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        const float bw = math::box_width(b), bh = math::box_height(b);
        return math::make_box(bx + bw - kGrip, by + bh - kGrip, kGrip, kGrip);
    }
    TextPosition cursor_from_pixel(const math::Vec2& p) const {
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b));
        float by = math::y(math::box_min(b));
        float line_h = style_.font_size * style_.line_height;
        // text_x matches get_render_info: gutter_width + padding, then +2 added by draw_text_vc
        const float x_off = (line_nums_ ? style_.gutter_width + style_.padding : style_.padding);
        float text_x = bx + x_off + 2.0f;
        rewrap(math::box_width(b) - x_off - style_.padding);   // ensure display lines are current
        // The click lands on a DISPLAY line (wrap-aware); map it back to logical.
        int dline = first_vis_ + (line_h > 0 ? (int)((math::y(p) - by) / line_h) : 0);
        dline = std::max(0, std::min(dline, (int)disp_lines_.size() - 1));
        const std::string& line_str = disp_lines_[dline];
        float rel_x = math::x(p) - text_x;
        int col = 0;
        if (rel_x > 0.0f) {
            if (measurer_) {
                // Same canonical caret mapping as the single-line field / the drawn
                // caret — one layout, so click and caret agree.
                col = measurer_->caret_index_at(line_str.c_str(), rel_x, style_.font_size, style_.font_name);
            } else {
                float char_w = style_.font_size * 0.6f;
                col = (char_w > 0) ? (int)((rel_x + char_w * 0.5f) / char_w) : 0;
            }
        }
        return to_logical(dline, col);
    }
    void set_scroll_from_pixel(float pixel_offset) {
        float line_h = style_.font_size * style_.line_height;
        int line = (line_h > 0) ? (int)(pixel_offset / line_h) : 0;
        set_first_visible_line(line);
    }
    // Key codes from window::Key enum
    enum : int { K_Tab=301, K_Enter=308, K_Backspace=309, K_Delete=310,
                 K_Home=312, K_End=313, K_Left=316, K_Right=317,
                 K_Up=318, K_Down=319 };
    void clamp(TextPosition& p) const {
        p.line=std::max(0,std::min(p.line,(int)lines_.size()-1));
        p.column=std::max(0,std::min(p.column,(int)lines_[p.line].size()));
    }
    void begin_or_extend_selection() {
        if (!has_selection()) { selection_.start = cursor_; selection_.end = cursor_; }
    }
public:
    bool is_focusable() const override { return true; }
    bool handle_text_input(const char* t) override {
        if (read_only_ || !t || !*t) return false;
        if (has_selection()) delete_selection();
        insert_text(t);
        return true;
    }
    bool handle_key(int code, bool pressed, int mods) override {
        if (!pressed) return false;
        bool shift = (mods & 1) != 0;
        bool ctrl = (mods & 2) != 0;
        // Ctrl+A: select all
        if (ctrl && (code == 'A' || code == 'a')) { select_all(); return true; }
        switch (code) {
            case K_Left:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.column > 0) --cursor_.column;
                else if (cursor_.line > 0) { --cursor_.line; cursor_.column = (int)lines_[cursor_.line].size(); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Right:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.column < (int)lines_[cursor_.line].size()) ++cursor_.column;
                else if (cursor_.line < (int)lines_.size()-1) { ++cursor_.line; cursor_.column = 0; }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Up:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.line > 0) { --cursor_.line; clamp(cursor_); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Down:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.line < (int)lines_.size()-1) { ++cursor_.line; clamp(cursor_); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Home:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                cursor_.column = 0;
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_End:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                cursor_.column = (int)lines_[cursor_.line].size();
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Backspace:
                if (!read_only_) { if (has_selection()) delete_selection(); else delete_backward(1); }
                return true;
            case K_Delete:
                if (!read_only_) { if (has_selection()) delete_selection(); else delete_forward(1); }
                return true;
            case K_Enter:
                if (!read_only_) {
                    if (has_selection()) delete_selection();
                    // Split line at cursor
                    std::string after = lines_[cursor_.line].substr(cursor_.column);
                    lines_[cursor_.line].erase(cursor_.column);
                    lines_.insert(lines_.begin() + cursor_.line + 1, after);
                    ++cursor_.line; cursor_.column = 0;
                }
                return true;
            case K_Tab:
                if (!read_only_) {
                    if (tab_spaces_) { std::string sp(tab_size_, ' '); insert_text(sp.c_str()); }
                    else insert_text("\t");
                }
                return true;
        }
        return false;
    }
    bool handle_mouse_scroll(float, float dy) override {
        int step = (dy > 0) ? -3 : 3;
        set_first_visible_line(first_vis_ + step);
        return true;
    }
    bool handle_mouse_move(const math::Vec2& p) override {
        if (rz_drag_) {
            float h = rz_h0_ + (math::y(p) - rz_dy0_);
            if (h < rz_min_) h = rz_min_;
            if (rz_max_ > 0.0f && h > rz_max_) h = rz_max_;
            if (h != rz_h_) { rz_h_ = h; base_.mark_dirty(); if (handler_) handler_->on_resized(rz_h_); }
            return true;
        }
        if (sb_drag_) {
            set_scroll_from_pixel(scrollbar_offset_from_mouse(base_.get_bounds(), content_height(), math::y(p)));
            return true;
        }
        if (click_drag_) {
            TextPosition pos = cursor_from_pixel(p);
            selection_.end = pos;
            cursor_ = pos;
            if (handler_) handler_->on_selection_changed(selection_);
            return true;
        }
        return base_.handle_mouse_move(p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        // Clear drag state on release even when mouse has moved outside bounds
        if (btn == MouseButton::Left && !pressed) { sb_drag_ = false; click_drag_ = false; rz_drag_ = false; }
        if (!base_.is_enabled() || !hit_test(p)) return false;
        // The resize grip claims the press before caret/scrollbar/selection.
        if (btn == MouseButton::Left && pressed && resizable_ && math::box_contains(grip_rect(), p)) {
            rz_drag_ = true; rz_dy0_ = math::y(p);
            rz_h0_ = rz_h_ > 0.0f ? rz_h_ : math::box_height(base_.get_bounds());
            return true;
        }
        if (btn == MouseButton::Left && pressed) {
            float ch = content_height();
            if (scrollbar_hit_test(base_.get_bounds(), ch, p)) {
                sb_drag_ = true;
                set_scroll_from_pixel(scrollbar_offset_from_mouse(base_.get_bounds(), ch, math::y(p)));
                return true;
            }
            // Set cursor from click point; begin drag-select
            TextPosition pos = cursor_from_pixel(p);
            cursor_ = pos;
            selection_ = {pos, pos};
            click_drag_ = true;
            if (handler_) handler_->on_cursor_moved(cursor_);
        }
        if (btn == MouseButton::Right && pressed) {
            if (handler_) handler_->on_right_click(p);
            return true;
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    const char* get_text() const override {
        cached_text_.clear();
        for(int i=0;i<(int)lines_.size();++i){if(i)cached_text_+='\n';cached_text_+=lines_[i];}
        return cached_text_.c_str();
    }
    void set_text(const char* t) override {
        lines_.clear(); disp_dirty_ = true;
        if(!t||!*t){lines_.push_back("");return;}
        std::istringstream ss(t); std::string l;
        while(std::getline(ss,l)) lines_.push_back(l);
        if(lines_.empty()) lines_.push_back("");
        cursor_={0,0}; selection_={{0,0},{0,0}};
    }
    int get_text_length() const override {
        int n=0; for(auto& l:lines_) n+=(int)l.size(); return n+std::max(0,(int)lines_.size()-1);
    }
    int get_line_count() const override { return (int)lines_.size(); }
    const char* get_line(int i) const override { return (i>=0&&i<(int)lines_.size())?lines_[i].c_str():""; }
    int get_line_length(int i) const override { return (i>=0&&i<(int)lines_.size())?(int)lines_[i].size():0; }
    void insert_line(int i,const char* t) override {
        if(i<0)i=0; if(i>(int)lines_.size())i=(int)lines_.size();
        lines_.insert(lines_.begin()+i,t?t:"");
    }
    void remove_line(int i) override { if(i>=0&&i<(int)lines_.size()&&lines_.size()>1) lines_.erase(lines_.begin()+i); }
    void replace_line(int i,const char* t) override { if(i>=0&&i<(int)lines_.size()) lines_[i]=t?t:""; }
    TextPosition get_cursor_position() const override { return cursor_; }
    void set_cursor_position(const TextPosition& p) override { cursor_=p; clamp(cursor_); }
    void move_cursor(int dl,int dc) override { cursor_.line+=dl; cursor_.column+=dc; clamp(cursor_); }
    void move_cursor_to_line_start() override { cursor_.column=0; }
    void move_cursor_to_line_end() override { cursor_.column=(int)lines_[cursor_.line].size(); }
    void move_cursor_to_start() override { cursor_={0,0}; }
    void move_cursor_to_end() override { cursor_.line=(int)lines_.size()-1; cursor_.column=(int)lines_[cursor_.line].size(); }
    TextRange get_selection() const override { return selection_; }
    void set_selection(const TextRange& r) override { selection_=r; clamp(selection_.start); clamp(selection_.end); }
    void select_all() override { selection_.start={0,0}; selection_.end={(int)lines_.size()-1,(int)lines_.back().size()}; }
    void select_line(int i) override { if(i>=0&&i<(int)lines_.size()){selection_.start={i,0};selection_.end={i,(int)lines_[i].size()};} }
    void select_word_at_cursor() override {}
    void clear_selection() override { selection_={{0,0},{0,0}}; }
    bool has_selection() const override { return !selection_.is_empty(); }
    const char* get_selected_text() const override {
        if(!has_selection()) return "";
        auto s=selection_.start, e=selection_.end;
        if(s.line>e.line||(s.line==e.line&&s.column>e.column)) std::swap(s,e);
        cached_sel_.clear();
        if(s.line==e.line) cached_sel_=lines_[s.line].substr(s.column,e.column-s.column);
        else {
            cached_sel_=lines_[s.line].substr(s.column);
            for(int i=s.line+1;i<e.line;++i){cached_sel_+='\n';cached_sel_+=lines_[i];}
            cached_sel_+='\n'; cached_sel_+=lines_[e.line].substr(0,e.column);
        }
        return cached_sel_.c_str();
    }
    void insert_text(const char* t) override { insert_text_at(cursor_,t); }
    void insert_text_at(const TextPosition& pos,const char* t) override {
        if(!t||read_only_) return;
        TextPosition p=pos; clamp(p);
        std::string after=lines_[p.line].substr(p.column);
        lines_[p.line].erase(p.column);
        std::istringstream ss(t); std::string part; bool first=true; int nl=p.line;
        while(std::getline(ss,part)){
            if(first){lines_[nl]+=part;first=false;}
            else{lines_.insert(lines_.begin()+(++nl),part);}
        }
        lines_[nl]+=after;
        cursor_={nl,(int)(lines_[nl].size()-after.size())};
    }
    void delete_selection() override {
        if(!has_selection()||read_only_) return;
        auto s=selection_.start, e=selection_.end;
        if(s.line>e.line||(s.line==e.line&&s.column>e.column)) std::swap(s,e);
        if(s.line==e.line) lines_[s.line].erase(s.column,e.column-s.column);
        else {
            lines_[s.line].erase(s.column);
            lines_[s.line]+=lines_[e.line].substr(e.column);
            lines_.erase(lines_.begin()+s.line+1,lines_.begin()+e.line+1);
        }
        cursor_=s; clear_selection();
    }
    void delete_range(const TextRange& r) override { selection_=r; delete_selection(); }
    void delete_backward(int n) override {
        if(read_only_) return;
        for(int i=0;i<n&&(cursor_.line>0||cursor_.column>0);++i){
            if(cursor_.column>0){lines_[cursor_.line].erase(--cursor_.column,1);}
            else if(cursor_.line>0){cursor_.column=(int)lines_[cursor_.line-1].size();lines_[cursor_.line-1]+=lines_[cursor_.line];lines_.erase(lines_.begin()+cursor_.line);--cursor_.line;}
        }
    }
    void delete_forward(int n) override {
        if(read_only_) return;
        for(int i=0;i<n;++i){
            if(cursor_.column<(int)lines_[cursor_.line].size()) lines_[cursor_.line].erase(cursor_.column,1);
            else if(cursor_.line<(int)lines_.size()-1){lines_[cursor_.line]+=lines_[cursor_.line+1];lines_.erase(lines_.begin()+cursor_.line+1);}
        }
    }
    void delete_line(int i) override { remove_line(i); }
    void duplicate_line() override { if(cursor_.line<(int)lines_.size()) lines_.insert(lines_.begin()+cursor_.line+1,lines_[cursor_.line]); }
    void move_line_up() override { if(cursor_.line>0){std::swap(lines_[cursor_.line],lines_[cursor_.line-1]);--cursor_.line;} }
    void move_line_down() override { if(cursor_.line<(int)lines_.size()-1){std::swap(lines_[cursor_.line],lines_[cursor_.line+1]);++cursor_.line;} }
    bool can_undo() const override { return false; }
    bool can_redo() const override { return false; }
    void undo() override {}
    void redo() override {}
    void clear_undo_history() override {}
    int get_undo_stack_size() const override { return 0; }
    void cut() override { copy(); delete_selection(); }
    void copy() override {}
    void paste() override {}
    TextPosition find(const char* text,const TextPosition& start,bool cs,bool) const override {
        if(!text||!*text) return {-1,-1};
        std::string needle(text);
        for(int i=start.line;i<(int)lines_.size();++i){
            int col=(i==start.line)?start.column:0;
            size_t pos;
            if(cs) pos=lines_[i].find(needle,col);
            else{std::string ll=lines_[i],ln=needle;std::transform(ll.begin(),ll.end(),ll.begin(),::tolower);std::transform(ln.begin(),ln.end(),ln.begin(),::tolower);pos=ll.find(ln,col);}
            if(pos!=std::string::npos) return {i,(int)pos};
        }
        return {-1,-1};
    }
    int replace(const char* s,const char* r,bool cs,bool ww) override {
        auto pos=find(s,cursor_,cs,ww); if(pos.line<0) return 0;
        cursor_=pos; selection_.start=pos; selection_.end={pos.line,pos.column+(int)std::strlen(s)};
        delete_selection(); insert_text(r); return 1;
    }
    int replace_all(const char* s,const char* r,bool cs,bool ww) override {
        int cnt=0; TextPosition p={0,0};
        while(true){p=find(s,p,cs,ww);if(p.line<0)break;cursor_=p;selection_.start=p;selection_.end={p.line,p.column+(int)std::strlen(s)};delete_selection();insert_text(r);p=cursor_;++cnt;}
        return cnt;
    }
    EditBoxWordWrap get_word_wrap() const override { return wrap_; }
    void set_word_wrap(EditBoxWordWrap w) override { wrap_=w; disp_dirty_=true; }
    bool is_line_numbers_visible() const override { return line_nums_; }
    void set_line_numbers_visible(bool v) override { line_nums_=v; }
    bool is_current_line_highlighted() const override { return hl_line_; }
    void set_current_line_highlighted(bool h) override { hl_line_=h; }
    bool is_read_only() const override { return read_only_; }
    void set_read_only(bool r) override { if (read_only_ == r) return; read_only_=r; base_.mark_dirty(); }
    // ── shared text-edit surface (built-in right-click menu / Ctrl combos) ──
    ITextEditTarget* text_edit_target() override { return this; }
    bool text_is_read_only() const override { return read_only_; }
    bool text_has_selection() const override { return has_selection(); }
    std::string text_selected() const override { const char* t = get_selected_text(); return t ? t : ""; }
    void text_replace_selection(const char* u) override {
        if (read_only_) return;
        if (has_selection()) delete_selection();
        if (u && *u) insert_text(u);
        base_.mark_dirty();
    }
    void text_select_all() override { select_all(); base_.mark_dirty(); }
    bool is_tab_insert_spaces() const override { return tab_spaces_; }
    void set_tab_insert_spaces(bool s) override { tab_spaces_=s; }
    int get_tab_size() const override { return tab_size_; }
    void set_tab_size(int s) override { tab_size_=s; }
    int get_first_visible_line() const override { return first_vis_; }
    void set_first_visible_line(int l) override { first_vis_=std::max(0,std::min(l,disp_count()-1)); }
    int get_visible_line_count() const override {
        float h=math::box_height(base_.get_bounds()); float lh=style_.font_size*style_.line_height;
        return lh>0?(int)(h/lh):0;
    }
    void scroll_to_cursor() override { int dl, dc; to_disp(cursor_, dl, dc); set_first_visible_line(dl); }
    void scroll_to_line(int l) override { set_first_visible_line(l); }
    TextPosition position_from_point(const math::Vec2&) const override { return cursor_; }
    math::Vec2 point_from_position(const TextPosition&) const override { return math::Vec2(0,0); }
    const EditBoxStyle& get_editbox_style() const override { return style_; }
    void set_editbox_style(const EditBoxStyle& s) override { style_=s; }
    void set_editbox_event_handler(IEditBoxEventHandler* h) override { handler_=h; }
    void set_text_measurer(ITextMeasurer* m) override { measurer_=m; }
    void set_user_resizable(bool on, float min_h, float max_h) override {
        resizable_ = on; rz_min_ = min_h; rz_max_ = max_h;
        if (rz_h_ > 0.0f) { if (rz_h_ < rz_min_) rz_h_ = rz_min_; if (rz_max_ > 0.0f && rz_h_ > rz_max_) rz_h_ = rz_max_; }
        base_.mark_dirty();
    }
    float get_user_height() const override { return rz_h_; }

    math::Vec2 get_preferred_size() const override {
        math::Vec2 p = pref_size_for_width(math::box_width(base_.get_bounds()));
        // A user-dragged height overrides the content-fit height, so a sizer-placed
        // box keeps exactly the size the user dragged it to.
        if (rz_h_ > 0.0f) p = math::Vec2(math::x(p), rz_h_);
        return p;
    }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        math::Box clip = b; // clip content to widget bounds
        auto noclip = math::make_box(0,0,0,0);
        int32_t d = 0;
        const auto& s = style_;
        float line_h = s.font_size * s.line_height;
        float text_x = bx + (line_nums_ ? s.gutter_width + s.padding : s.padding);
        const float content_w = bw - (text_x - bx) - s.padding;
        rewrap(content_w);
        int lc = (int)disp_lines_.size();

        // Background
        ri_.push_rect(bx, by, bw, bh, s.background_color, d++, noclip);
        // Gutter
        if (line_nums_) {
            ri_.push_rect(bx, by, s.gutter_width, bh, s.line_number_background, d++, noclip);
            ri_.push_rect(bx+s.gutter_width, by, 1, bh, s.gutter_border_color, d++, noclip);
        }

        // Get selection range normalized (in logical coords), then map both ends AND
        // the caret to DISPLAY (line,col) once — the loop below indexes display lines.
        TextRange sel = selection_;
        if (sel.start.line > sel.end.line || (sel.start.line == sel.end.line && sel.start.column > sel.end.column))
            std::swap(sel.start, sel.end);
        bool has_sel = !selection_.is_empty();
        int cdl = 0, cdc = 0; to_disp(cursor_, cdl, cdc);                       // caret display line/col
        int sdl0 = 0, sdc0 = 0, sdl1 = 0, sdc1 = 0;
        if (has_sel) { to_disp(sel.start, sdl0, sdc0); to_disp(sel.end, sdl1, sdc1); }

        int vis_count = (line_h > 0) ? (int)(bh / line_h) + 2 : lc;
        int end_line = std::min(first_vis_ + vis_count, lc);

        for (int i = first_vis_; i < end_line; i++) {
            float ly = by + (i - first_vis_) * line_h;
            if (ly > by + bh) break;

            // Line number
            if (line_nums_) {
                char num[16]; std::snprintf(num, sizeof(num), "%d", i+1);
                ri_.push_text(num, bx+4, ly, s.gutter_width-4, line_h,
                              s.line_number_color, s.font_size, Alignment::CenterLeft, d++, clip);
            }

            const char* lt = disp_lines_[i].c_str();
            int ll = (int)disp_lines_[i].size();

            // Per-display-line selection range (byte columns within this display
            // line): the flatten pass draws the band from the SAME layout + origin as
            // the glyphs and caret, so nothing here re-measures. Interior lines are
            // fully covered; the first/last line get the partial range.
            int line_sel_s = -1, line_sel_e = -1;
            if (has_sel && i >= sdl0 && i <= sdl1) {
                line_sel_s = (i == sdl0) ? sdc0 : 0;
                line_sel_e = (i == sdl1) ? sdc1 : ll;
            }

            // Line text with optional cursor + selection
            if (lt && lt[0]) {
                WidgetRenderInfo::TextCmd tc;
                tc.text      = lt;
                tc.dest      = math::make_box(text_x, ly, content_w, line_h);
                tc.color     = s.text_color;
                tc.font_size = s.font_size;
                tc.alignment = s.text_alignment;
                tc.depth     = d++;
                tc.clip      = clip;
                if (line_sel_e > line_sel_s) {
                    tc.sel_start = line_sel_s; tc.sel_end = line_sel_e; tc.sel_bg_color = s.selection_color;
                }
                if (base_.has_focus() && i == cdl) {
                    tc.show_cursor  = true;
                    tc.cursor_pos   = cdc;
                    tc.cursor_color = s.text_color;
                }
                ri_.texts.push_back(std::move(tc));
            } else if (base_.has_focus() && i == cdl) {
                // Empty line with cursor
                WidgetRenderInfo::TextCmd tc;
                tc.text       = "";
                tc.dest       = math::make_box(text_x, ly, bw-(text_x-bx)-12, line_h);
                tc.color      = s.text_color;
                tc.font_size  = s.font_size;
                tc.alignment  = Alignment::CenterLeft;
                tc.depth      = d++;
                tc.clip       = clip;
                tc.show_cursor = true;
                tc.cursor_pos  = 0;
                tc.cursor_color = s.text_color;
                ri_.texts.push_back(std::move(tc));
            }
        }

        // Auto-scrollbar — only for real overflow: line_h = font_size * line_height
        // reintroduces float error, so a content-sized box can measure fractionally
        // "taller" than itself and grow a phantom scrollbar without the tolerance.
        float content_h = lc * line_h;
        float scroll_offset = first_vis_ * line_h;
        if (content_h > bh + 0.5f) {
            const float sb_w = 10.0f;
            float sb_x = bx + bw - sb_w - 1;
            ri_.push_rect(sb_x, by, sb_w, bh, math::Vec4(0.12f,0.12f,0.13f,0.6f), d++, noclip);
            float thumb_ratio = bh / content_h;
            float thumb_h = std::max(16.0f, bh * thumb_ratio);
            float track_range = bh - thumb_h;
            float max_scroll = content_h - bh;
            float pos_ratio = (max_scroll > 0) ? scroll_offset / max_scroll : 0;
            ri_.push_rect(sb_x, by + track_range*pos_ratio, sb_w, thumb_h,
                          math::Vec4(0.4f,0.4f,0.42f,0.7f), d++, noclip);
        }

        // Focus border + outer border (outer drawn only when the border colour is
        // opaque — a transparent-background editbox embedded in other content sets
        // alpha 0 to render borderless, like inline text).
        if (base_.has_focus())
            ri_.push_outline(bx-1, by-1, bw+2, bh+2, math::Vec4(0,0.48f,0.8f,1), d, noclip);
        if (s.border_color.w > 0.0f)
            ri_.push_outline(bx, by, bw, bh, s.border_color, d, noclip);

        // Resize grip: three stacked ticks tucked into the bottom-right corner (the
        // familiar "drag to resize" affordance). Drawn last so it sits over content.
        if (resizable_) {
            const math::Vec4 gc(0.55f, 0.57f, 0.62f, 0.95f);
            const float cx = bx + bw, cy = by + bh;
            for (int k = 1; k <= 3; ++k) {
                float off = k * 3.0f;
                ri_.push_rect(cx - off - 3.0f, cy - off - 3.0f, off + 1.0f, 2.0f, gc, d++, noclip);
            }
        }

        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

// Factory functions
IGuiLabel* create_label_widget() { return new GuiLabel(); }
IGuiTextInput* create_text_input_widget() { return new GuiTextInput(); }
IGuiEditBox* create_editbox_widget() { return new GuiEditBox(); }

} // namespace gui
} // namespace window
