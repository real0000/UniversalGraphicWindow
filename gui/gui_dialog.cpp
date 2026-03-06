/*
 * gui_dialog.cpp - Dialog and Popup Implementations
 */

#include "gui_widget_base.hpp"

namespace window {
namespace gui {

class GuiDialog : public WidgetBase<IGuiDialog, WidgetType::Custom> {
    struct CustomBtn { int index; std::string text; DialogResult result; };
    std::string title_;
    bool modal_=true, open_=false, draggable_=true, resizable_=false;
    bool close_btn_=true, close_on_overlay_=false;
    DialogResult result_=DialogResult::None;
    DialogButtons buttons_=DialogButtons::OK;
    IGuiWidget* content_=nullptr;
    DialogStyle style_=DialogStyle::default_style();
    IDialogEventHandler* handler_=nullptr;
    std::vector<CustomBtn> custom_buttons_;
    mutable WidgetRenderInfo ri_;
public:
    const char* get_title() const override { return title_.c_str(); }
    void set_title(const char* t) override { title_=t?t:""; }
    bool is_modal() const override { return modal_; }
    void set_modal(bool m) override { modal_=m; }
    void show() override { open_=true; result_=DialogResult::None; }
    void hide() override { open_=false; }
    bool is_open() const override { return open_; }
    DialogResult get_result() const override { return result_; }
    void set_buttons(DialogButtons b) override { buttons_=b; }
    DialogButtons get_buttons() const override { return buttons_; }
    void set_custom_button(int index, const char* text, DialogResult result) override {
        for(auto& cb:custom_buttons_) if(cb.index==index){cb.text=text?text:"";cb.result=result;return;}
        custom_buttons_.push_back({index,text?text:"",result});
    }
    int get_button_count() const override {
        if(buttons_==DialogButtons::Custom) return (int)custom_buttons_.size();
        switch(buttons_){
            case DialogButtons::None: return 0;
            case DialogButtons::OK: return 1;
            case DialogButtons::OKCancel: case DialogButtons::YesNo: case DialogButtons::RetryCancel: return 2;
            case DialogButtons::YesNoCancel: case DialogButtons::AbortRetryIgnore: return 3;
            default: return 0;
        }
    }
    IGuiWidget* get_content() const override { return content_; }
    void set_content(IGuiWidget* c) override { content_=c; }
    bool is_draggable() const override { return draggable_; }
    void set_draggable(bool d) override { draggable_=d; }
    bool is_resizable() const override { return resizable_; }
    void set_resizable(bool r) override { resizable_=r; }
    bool has_close_button() const override { return close_btn_; }
    void set_close_button(bool s) override { close_btn_=s; }
    bool is_close_on_overlay_click() const override { return close_on_overlay_; }
    void set_close_on_overlay_click(bool e) override { close_on_overlay_=e; }
    const DialogStyle& get_dialog_style() const override { return style_; }
    void set_dialog_style(const DialogStyle& s) override { style_=s; }
    void set_dialog_event_handler(IDialogEventHandler* h) override { handler_=h; }

    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!open_) return false;
        auto b = base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b));
        float bw=math::box_width(b), bh=math::box_height(b);
        // Clicks outside the dialog always pass through (lets background widgets work while dialog is visible)
        if (!math::box_contains(b, p)) return false;
        // Inside the dialog: absorb non-left-press events
        if (btn != MouseButton::Left || !pressed) return true;
        // Close button (matches render: cx=bx+bw-title_h, cy=by, size=title_h)
        if (close_btn_) {
            float th = style_.title_bar_height;
            if (math::box_contains(math::make_box(bx+bw-th, by, th, th), p)) {
                hide();
                if (handler_) { handler_->on_dialog_button_clicked(DialogResult::None);
                                 handler_->on_dialog_closed(DialogResult::None); }
                return true;
            }
        }
        // Action buttons (matches render: centered, btn_w=80, gap=8, row at bottom)
        int n = get_button_count();
        if (n > 0) {
            auto result_for = [&](int i) -> DialogResult {
                switch(buttons_) {
                    case DialogButtons::OK:               return DialogResult::OK;
                    case DialogButtons::OKCancel:         return i==0?DialogResult::OK:DialogResult::Cancel;
                    case DialogButtons::YesNo:            return i==0?DialogResult::Yes:DialogResult::No;
                    case DialogButtons::YesNoCancel:      return i==0?DialogResult::Yes:(i==1?DialogResult::No:DialogResult::Cancel);
                    case DialogButtons::RetryCancel:      return i==0?DialogResult::Retry:DialogResult::Cancel;
                    case DialogButtons::AbortRetryIgnore: return i==0?DialogResult::Abort:(i==1?DialogResult::Retry:DialogResult::Ignore);
                    default: return DialogResult::None;
                }
            };
            float btn_w=80.f, btn_gap=8.f, btn_area_h=style_.button_area_height;
            float total_w=n*(btn_w+btn_gap)-btn_gap;
            float bstart=bx+(bw-total_w)*0.5f, bar_y=by+bh-btn_area_h;
            for (int i=0;i<n;i++) {
                if (math::box_contains(math::make_box(bstart, bar_y+6, btn_w, btn_area_h-12), p)) {
                    result_ = result_for(i);
                    hide();
                    if (handler_) { handler_->on_dialog_button_clicked(result_);
                                     handler_->on_dialog_closed(result_); }
                    return true;
                }
                bstart += btn_w+btn_gap;
            }
        }
        return true; // absorb left click inside dialog (not on a specific button)
    }

    void get_dialog_render_info(DialogRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->title=title_.c_str(); out->is_modal=modal_;
        out->is_draggable=draggable_; out->is_resizable=resizable_; out->show_close_button=close_btn_;
    }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        if (!open_) { ri_.finalize(); return ri_; }
        auto b = base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b));
        float bw=math::box_width(b), bh=math::box_height(b);
        auto noclip=math::make_box(0,0,0,0);
        int32_t d=0;
        const auto& s=style_;
        // Modal overlay dimming
        if (modal_)
            ri_.push_rect(0, 0, 9999, 9999, math::Vec4(0,0,0,0.45f), d++, noclip);
        // Dialog background + shadow
        ri_.push_rect(bx+3, by+3, bw, bh, math::Vec4(0,0,0,0.35f), d++, noclip);
        ri_.push_rect(bx, by, bw, bh, s.background_color, d++, noclip);
        // Title bar
        const float title_h = s.title_bar_height;
        ri_.push_rect(bx, by, bw, title_h, s.title_bar_color, d++, noclip);
        if (!title_.empty())
            ri_.push_text(title_.c_str(), bx+10, by, bw-40, title_h,
                          s.title_text_color, s.title_font_size, Alignment::CenterLeft, d++, noclip);
        // Close button
        if (close_btn_) {
            float cx = bx+bw-title_h, cy = by;
            ri_.push_rect(cx, cy, title_h, title_h, math::Vec4(0.8f,0.2f,0.2f,0.8f), d++, noclip);
            ri_.push_text("X", cx, cy, title_h, title_h,
                          math::Vec4(1,1,1,1), 10.0f, Alignment::Center, d++, noclip);
        }
        // Button bar at bottom
        int btn_count = get_button_count();
        if (btn_count > 0) {
            float btn_h = s.button_area_height;
            float bar_y = by + bh - btn_h;
            ri_.push_rect(bx, bar_y, bw, btn_h, s.background_color, d++, noclip);
            ri_.push_rect(bx, bar_y, bw, 1, s.border_color, d++, noclip); // separator line
            float btn_w = 80.0f, btn_gap = 8.0f;
            float total_w = btn_count*(btn_w+btn_gap)-btn_gap;
            float bstart = bx + (bw-total_w)*0.5f;
            math::Vec4 btn_bg(0.25f, 0.25f, 0.28f, 1.0f);
            auto draw_btn = [&](const char* label) {
                ri_.push_rect(bstart, bar_y+6, btn_w, btn_h-12, btn_bg, d++, noclip);
                ri_.push_outline(bstart, bar_y+6, btn_w, btn_h-12, s.border_color, d, noclip);
                ri_.push_text(label, bstart, bar_y+6, btn_w, btn_h-12,
                              s.title_text_color, 11.0f, Alignment::Center, d++, noclip);
                bstart += btn_w + btn_gap;
            };
            if (buttons_ == DialogButtons::Custom) {
                for (auto& cb : custom_buttons_) draw_btn(cb.text.c_str());
            } else {
                switch(buttons_) {
                    case DialogButtons::OK:            draw_btn("OK"); break;
                    case DialogButtons::OKCancel:      draw_btn("OK"); draw_btn("Cancel"); break;
                    case DialogButtons::YesNo:         draw_btn("Yes"); draw_btn("No"); break;
                    case DialogButtons::YesNoCancel:   draw_btn("Yes"); draw_btn("No"); draw_btn("Cancel"); break;
                    case DialogButtons::RetryCancel:   draw_btn("Retry"); draw_btn("Cancel"); break;
                    case DialogButtons::AbortRetryIgnore: draw_btn("Abort"); draw_btn("Retry"); draw_btn("Ignore"); break;
                    default: break;
                }
            }
        }
        ri_.push_outline(bx, by, bw, bh, s.border_color, d, noclip);
        ri_.finalize(); base_.clear_dirty(); return ri_;
    }
};

// ============================================================================
// GuiPopup
// ============================================================================

class GuiPopup : public WidgetBase<IGuiPopup, WidgetType::Custom> {
    bool open_=false, close_outside_=true, close_esc_=true;
    IGuiWidget* content_=nullptr;
    math::Vec4 bg_color_=color_rgba8(37,37,38);
    math::Vec4 border_color_=color_rgba8(63,63,70);
    float corner_radius_=4.0f;
    IPopupEventHandler* handler_=nullptr;
    mutable WidgetRenderInfo ri_;
public:
    void show(PopupPlacement) override { open_=true; if(handler_) handler_->on_popup_opened(); }
    void show_at(const math::Vec2& pos) override {
        auto b=base_.get_bounds();
        base_.set_bounds(math::make_box(math::x(pos),math::y(pos),
            math::x(pos)+math::box_width(b),math::y(pos)+math::box_height(b)));
        open_=true; if(handler_) handler_->on_popup_opened();
    }
    void show_relative_to(const IGuiWidget* anchor, PopupPlacement placement) override {
        if(!anchor){show(placement);return;}
        auto ab=anchor->get_bounds(); auto b=base_.get_bounds();
        float w=math::box_width(b), h=math::box_height(b);
        float ax=math::x(math::box_min(ab)), ay=math::y(math::box_min(ab));
        float aw=math::box_width(ab), ah=math::box_height(ab);
        float px=ax,py=ay;
        switch(placement){
            case PopupPlacement::Below: py=ay+ah; break;
            case PopupPlacement::Above: py=ay-h; break;
            case PopupPlacement::Right: px=ax+aw; break;
            case PopupPlacement::Left: px=ax-w; break;
            case PopupPlacement::Center: px=ax+(aw-w)*0.5f; py=ay+(ah-h)*0.5f; break;
            default: break;
        }
        base_.set_bounds(math::make_box(px,py,px+w,py+h));
        open_=true; if(handler_) handler_->on_popup_opened();
    }
    void hide() override { open_=false; if(handler_) handler_->on_popup_closed(); }
    bool is_open() const override { return open_; }
    IGuiWidget* get_content() const override { return content_; }
    void set_content(IGuiWidget* c) override { content_=c; }
    bool is_close_on_click_outside() const override { return close_outside_; }
    void set_close_on_click_outside(bool e) override { close_outside_=e; }
    bool is_close_on_escape() const override { return close_esc_; }
    void set_close_on_escape(bool e) override { close_esc_=e; }
    math::Vec4 get_background_color() const override { return bg_color_; }
    void set_background_color(const math::Vec4& c) override { bg_color_=c; }
    math::Vec4 get_border_color() const override { return border_color_; }
    void set_border_color(const math::Vec4& c) override { border_color_=c; }
    float get_corner_radius() const override { return corner_radius_; }
    void set_corner_radius(float r) override { corner_radius_=r; }
    void set_popup_event_handler(IPopupEventHandler* h) override { handler_=h; }
    void get_popup_render_info(PopupRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->background_color=bg_color_; out->border_color=border_color_;
        out->corner_radius=corner_radius_; out->is_open=open_;
    }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        if (!open_) { ri_.finalize(); return ri_; }
        auto b = base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b));
        float bw=math::box_width(b), bh=math::box_height(b);
        auto noclip=math::make_box(0,0,0,0);
        int32_t d=0;
        ri_.push_rect(bx+2, by+2, bw, bh, math::Vec4(0,0,0,0.3f), d++, noclip);
        ri_.push_rect(bx, by, bw, bh, bg_color_, d++, noclip);
        ri_.push_outline(bx, by, bw, bh, border_color_, d, noclip);
        ri_.finalize(); base_.clear_dirty(); return ri_;
    }
};

// Factory functions
IGuiDialog* create_dialog_widget(DialogButtons buttons) { auto* d = new GuiDialog(); d->set_buttons(buttons); return d; }
IGuiPopup* create_popup_widget() { return new GuiPopup(); }

} // namespace gui
} // namespace window
