/*
 * gui_controls.cpp - Button, Slider, ProgressBar, ColorPicker, Image Implementations
 */

#include "gui_widget_base.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace window {
namespace gui {

class GuiButton : public WidgetBase<IGuiButton, WidgetType::Button> {
    ButtonType type_ = ButtonType::Normal;
    std::string text_, icon_;
    bool checked_ = false;
    int radio_group_ = 0;
    ButtonStyle style_ = ButtonStyle::default_style();
    IButtonEventHandler* handler_ = nullptr;
public:
    explicit GuiButton(ButtonType t = ButtonType::Normal) : type_(t) {}
    bool is_focusable() const override { return true; }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && !pressed) {
            if (type_ == ButtonType::Toggle || type_ == ButtonType::Checkbox) {
                checked_ = !checked_; if (handler_) handler_->on_toggled(checked_);
            } else if (type_ == ButtonType::Radio) {
                if (!checked_) {
                    // Uncheck other radios in same group among siblings
                    auto* parent = base_.get_parent();
                    if (parent) {
                        for (int i = 0; i < parent->get_child_count(); ++i) {
                            auto* sib = parent->get_child(i);
                            if (sib == this || sib->get_type() != WidgetType::Button) continue;
                            auto* sib_btn = static_cast<IGuiButton*>(sib);
                            if (sib_btn->get_button_type() == ButtonType::Radio &&
                                sib_btn->get_radio_group() == radio_group_ &&
                                sib_btn->is_checked()) {
                                sib_btn->set_checked(false);
                            }
                        }
                    }
                    checked_ = true;
                    if (handler_) handler_->on_toggled(true);
                }
            }
            if (handler_) handler_->on_clicked();
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    ButtonType get_button_type() const override { return type_; }
    void set_button_type(ButtonType t) override { type_ = t; }
    const char* get_text() const override { return text_.c_str(); }
    void set_text(const char* t) override { text_ = t ? t : ""; }
    const char* get_icon() const override { return icon_.c_str(); }
    void set_icon(const char* i) override { icon_ = i ? i : ""; }
    bool is_checked() const override { return checked_; }
    void set_checked(bool c) override { checked_ = c; }
    int get_radio_group() const override { return radio_group_; }
    void set_radio_group(int g) override { radio_group_ = g; }
    const ButtonStyle& get_button_style() const override { return style_; }
    void set_button_style(const ButtonStyle& s) override { style_ = s; }
    void set_button_event_handler(IButtonEventHandler* h) override { handler_ = h; }
};

// ============================================================================
// GuiImage
// ============================================================================

class GuiImage : public WidgetBase<IGuiImage, WidgetType::Image> {
    std::string image_name_;
    math::Vec4 tint_{1, 1, 1, 1};
public:
    const std::string& get_image_name() const override { return image_name_; }
    void set_image_name(const std::string& n) override { image_name_ = n; }
    math::Vec4 get_tint() const override { return tint_; }
    void set_tint(const math::Vec4& t) override { tint_ = t; }
};

// ============================================================================
// GuiSlider
// ============================================================================

class GuiSlider : public WidgetBase<IGuiSlider, WidgetType::Slider> {
    SliderOrientation orient_ = SliderOrientation::Horizontal;
    float value_ = 0, min_ = 0, max_ = 1, step_ = 0, tick_interval_ = 0;
    bool ticks_ = false, thumb_hover_ = false, thumb_press_ = false;
    SliderStyle style_ = SliderStyle::default_style();
    ISliderEventHandler* handler_ = nullptr;
    float clamp_val(float v) const {
        v = std::max(min_, std::min(max_, v));
        if (step_ > 0) v = min_ + std::round((v - min_) / step_) * step_;
        return v;
    }
    void update_from_pos(const math::Vec2& p) {
        auto b = base_.get_bounds();
        float norm = (orient_ == SliderOrientation::Horizontal)
            ? (math::x(p) - math::x(math::box_min(b))) / std::max(1.0f, math::box_width(b))
            : 1.0f - (math::y(p) - math::y(math::box_min(b))) / std::max(1.0f, math::box_height(b));
        norm = std::max(0.0f, std::min(1.0f, norm));
        float old = value_;
        value_ = clamp_val(min_ + norm * (max_ - min_));
        if (value_ != old && handler_) handler_->on_value_changed(value_);
    }
public:
    bool is_focusable() const override { return true; }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left) {
            if (pressed) { thumb_press_ = true; if (handler_) handler_->on_drag_started(); update_from_pos(p); }
            else { thumb_press_ = false; if (handler_) handler_->on_drag_ended(); }
        }
        return true;
    }
    bool handle_mouse_move(const math::Vec2& p) override {
        thumb_hover_ = hit_test(p);
        if (thumb_press_) update_from_pos(p);
        return thumb_hover_;
    }
    SliderOrientation get_orientation() const override { return orient_; }
    void set_orientation(SliderOrientation o) override { orient_ = o; }
    float get_value() const override { return value_; }
    void set_value(float v) override { value_ = clamp_val(v); }
    float get_min_value() const override { return min_; }
    float get_max_value() const override { return max_; }
    void set_range(float mn, float mx) override { min_ = mn; max_ = mx; value_ = clamp_val(value_); }
    float get_step() const override { return step_; }
    void set_step(float s) override { step_ = s; }
    bool is_ticks_visible() const override { return ticks_; }
    void set_ticks_visible(bool v) override { ticks_ = v; }
    float get_tick_interval() const override { return tick_interval_; }
    void set_tick_interval(float i) override { tick_interval_ = i; }
    bool is_thumb_hovered() const override { return thumb_hover_; }
    bool is_thumb_pressed() const override { return thumb_press_; }
    const SliderStyle& get_slider_style() const override { return style_; }
    void set_slider_style(const SliderStyle& s) override { style_ = s; }
    void set_slider_event_handler(ISliderEventHandler* h) override { handler_ = h; }
    void get_slider_render_info(SliderRenderInfo* out) const override {
        if (!out) return;
        auto b = base_.get_bounds();
        out->widget = this; out->bounds = b;
        out->clip_rect = base_.is_clip_enabled() ? base_.get_clip_rect() : b;
        out->style = style_; out->orientation = orient_;
        out->value = value_; out->min_value = min_; out->max_value = max_;
        out->normalized = (max_ > min_) ? (value_ - min_) / (max_ - min_) : 0;
        out->thumb_state = thumb_press_ ? WidgetState::Pressed : (thumb_hover_ ? WidgetState::Hovered : WidgetState::Normal);
        out->show_ticks = ticks_;
    }
};

// ============================================================================
// GuiProgressBar
// ============================================================================

class GuiProgressBar : public WidgetBase<IGuiProgressBar, WidgetType::ProgressBar> {
    ProgressBarMode mode_ = ProgressBarMode::Determinate;
    float value_ = 0, anim_phase_ = 0;
    bool show_text_ = false;
    std::string text_;
    ProgressBarStyle style_ = ProgressBarStyle::default_style();
public:
    void update(float dt) override { if (mode_ == ProgressBarMode::Indeterminate) anim_phase_ = std::fmod(anim_phase_ + dt, 1.0f); }
    ProgressBarMode get_mode() const override { return mode_; }
    void set_mode(ProgressBarMode m) override { mode_ = m; }
    float get_value() const override { return value_; }
    void set_value(float v) override { value_ = std::max(0.0f, std::min(1.0f, v)); }
    bool is_text_visible() const override { return show_text_; }
    void set_text_visible(bool v) override { show_text_ = v; }
    const char* get_text() const override { return text_.c_str(); }
    void set_text(const char* t) override { text_ = t ? t : ""; }
    const ProgressBarStyle& get_progress_bar_style() const override { return style_; }
    void set_progress_bar_style(const ProgressBarStyle& s) override { style_ = s; }
    void get_progress_bar_render_info(ProgressBarRenderInfo* out) const override {
        if (!out) return;
        auto b = base_.get_bounds();
        out->widget = this; out->bounds = b;
        out->clip_rect = base_.is_clip_enabled() ? base_.get_clip_rect() : b;
        out->style = style_; out->mode = mode_; out->value = value_;
        out->animation_phase = anim_phase_; out->show_text = show_text_; out->text = text_.c_str();
    }
};

// ============================================================================
// GuiColorPicker
// ============================================================================

class GuiColorPicker : public WidgetBase<IGuiColorPicker, WidgetType::Custom> {
    ColorPickerMode mode_ = ColorPickerMode::HSVSquare;
    math::Vec4 color_{1, 0, 0, 1}, prev_color_{1, 0, 0, 1};
    float hue_ = 0, sat_ = 1, bright_ = 1, alpha_ = 1;
    bool alpha_enabled_ = true, hex_vis_ = true, preview_vis_ = true;
    std::vector<math::Vec4> swatches_;
    mutable std::string hex_str_;
    ColorPickerStyle style_ = ColorPickerStyle::default_style();
    IColorPickerEventHandler* handler_ = nullptr;
    enum DragTarget { None, SV, Hue, Alpha };
    DragTarget drag_ = None;
    // editing_channel_: 0=R, 1=G, 2=B, 3=A, -1=none
    int editing_channel_ = -1;
    std::string edit_buf_;

    // Layout helpers matching render_colorpicker layout
    void get_layout(float& sq_x, float& sq_y, float& sv_size, float& hue_x, float& hue_w, float& hue_h, float& alpha_y) const {
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        int nch = alpha_enabled_ ? 4 : 3;
        float rgba_area = 25 + nch * 18 + 20;
        float sq_sz = bw - 30;
        if (sq_sz > bh - rgba_area) sq_sz = bh - rgba_area;
        if (sq_sz < 40) sq_sz = 40;
        sq_x = bx + 5; sq_y = by + 5;
        hue_w = 16; hue_x = bx + bw - hue_w - 5;
        hue_h = sq_sz;
        sv_size = sq_sz < (hue_x - sq_x - 5) ? sq_sz : (hue_x - sq_x - 5);
        alpha_y = sq_y + sv_size + 30;
    }

    void update_color_from_hsv() {
        float h = hue_ / 60.0f, s = sat_, v = bright_;
        int i = (int)h % 6;
        float f = h - (int)h, p = v*(1-s), q = v*(1-f*s), t = v*(1-(1-f)*s);
        switch(i) {
            case 0: color_={v,t,p,alpha_}; break; case 1: color_={q,v,p,alpha_}; break;
            case 2: color_={p,v,t,alpha_}; break; case 3: color_={p,q,v,alpha_}; break;
            case 4: color_={t,p,v,alpha_}; break; default: color_={v,p,q,alpha_}; break;
        }
    }
    void update_hsv_from_color() {
        float r=color_.x, g=color_.y, b=color_.z;
        float mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
        bright_=mx; sat_=(mx>0)?d/mx:0;
        if(d==0) hue_=0;
        else if(mx==r) hue_=60.0f*std::fmod((g-b)/d,6.0f);
        else if(mx==g) hue_=60.0f*((b-r)/d+2);
        else hue_=60.0f*((r-g)/d+4);
        if(hue_<0) hue_+=360.0f; alpha_=color_.w;
    }
public:
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) { drag_ = None; return false; }
        if (btn == MouseButton::Left) {
            if (pressed) {
                float sq_x, sq_y, sv_size, hue_x, hue_w, hue_h, alpha_y;
                get_layout(sq_x, sq_y, sv_size, hue_x, hue_w, hue_h, alpha_y);
                float mx = math::x(p), my = math::y(p);

                // Check RGBA input boxes click (below preview)
                auto b = base_.get_bounds();
                float bx = math::x(math::box_min(b));
                float input_y = sq_y + sv_size + 30;
                float input_w = sv_size / 2.0f;
                for (int ch = 0; ch < 4; ch++) {
                    float iy = input_y + ch * 18;
                    if (mx >= bx + 30 && mx <= bx + 30 + input_w && my >= iy && my < iy + 16) {
                        if (ch == 3 && !alpha_enabled_) break;
                        editing_channel_ = ch;
                        float vals[4] = {color_.x*255, color_.y*255, color_.z*255, color_.w*255};
                        char buf[16]; std::snprintf(buf, sizeof(buf), "%d", (int)vals[ch]);
                        edit_buf_ = buf;
                        return true;
                    }
                }
                editing_channel_ = -1;

                // SV square
                if (mx >= sq_x && mx < sq_x + sv_size && my >= sq_y && my < sq_y + sv_size) {
                    drag_ = SV;
                    sat_ = std::max(0.0f, std::min(1.0f, (mx - sq_x) / sv_size));
                    bright_ = std::max(0.0f, std::min(1.0f, 1.0f - (my - sq_y) / sv_size));
                    update_color_from_hsv();
                    if (handler_) handler_->on_color_changed(color_);
                }
                // Hue strip
                else if (mx >= hue_x && mx < hue_x + hue_w && my >= sq_y && my < sq_y + hue_h) {
                    drag_ = Hue;
                    hue_ = std::max(0.0f, std::min(359.9f, (my - sq_y) / hue_h * 360.0f));
                    update_color_from_hsv();
                    if (handler_) handler_->on_color_changed(color_);
                }
            } else {
                if (drag_ != None) {
                    if (handler_) handler_->on_color_confirmed(color_);
                    drag_ = None;
                }
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    bool handle_mouse_move(const math::Vec2& p) override {
        if (drag_ != None) {
            float sq_x, sq_y, sv_size, hue_x, hue_w, hue_h, alpha_y;
            get_layout(sq_x, sq_y, sv_size, hue_x, hue_w, hue_h, alpha_y);
            float mx = math::x(p), my = math::y(p);
            if (drag_ == SV) {
                sat_ = std::max(0.0f, std::min(1.0f, (mx - sq_x) / sv_size));
                bright_ = std::max(0.0f, std::min(1.0f, 1.0f - (my - sq_y) / sv_size));
                update_color_from_hsv();
                if (handler_) handler_->on_color_changed(color_);
            } else if (drag_ == Hue) {
                hue_ = std::max(0.0f, std::min(359.9f, (my - sq_y) / hue_h * 360.0f));
                update_color_from_hsv();
                if (handler_) handler_->on_color_changed(color_);
            }
            return true;
        }
        return base_.handle_mouse_move(p);
    }
    bool handle_key(int code, bool pressed, int mods) override {
        if (editing_channel_ >= 0 && pressed) {
            enum : int { K_Enter=308, K_Backspace=309, K_Escape=300 };
            if (code == K_Enter) {
                int val = std::max(0, std::min(255, std::atoi(edit_buf_.c_str())));
                float fval = val / 255.0f;
                switch (editing_channel_) {
                    case 0: color_.x = fval; break;
                    case 1: color_.y = fval; break;
                    case 2: color_.z = fval; break;
                    case 3: color_.w = fval; alpha_ = fval; break;
                }
                update_hsv_from_color();
                if (handler_) handler_->on_color_changed(color_);
                editing_channel_ = -1;
                return true;
            } else if (code == K_Escape) {
                editing_channel_ = -1;
                return true;
            } else if (code == K_Backspace) {
                if (!edit_buf_.empty()) edit_buf_.pop_back();
                return true;
            }
        }
        return base_.handle_key(code, pressed, mods);
    }
    bool handle_text_input(const char* t) override {
        if (editing_channel_ >= 0 && t) {
            for (const char* c = t; *c; ++c) {
                if (*c >= '0' && *c <= '9' && edit_buf_.size() < 3)
                    edit_buf_ += *c;
            }
            return true;
        }
        return false;
    }
    bool is_focusable() const override { return true; }
    int get_editing_channel() const { return editing_channel_; }
    const char* get_edit_buffer() const { return edit_buf_.c_str(); }
    ColorPickerMode get_mode() const override { return mode_; }
    void set_mode(ColorPickerMode m) override { mode_ = m; }
    math::Vec4 get_color() const override { return color_; }
    void set_color(const math::Vec4& c) override { color_=c; update_hsv_from_color(); }
    float get_hue() const override { return hue_; }
    void set_hue(float h) override { hue_=std::fmod(std::max(0.0f,h),360.0f); update_color_from_hsv(); }
    float get_saturation() const override { return sat_; }
    void set_saturation(float s) override { sat_=std::max(0.0f,std::min(1.0f,s)); update_color_from_hsv(); }
    float get_brightness() const override { return bright_; }
    void set_brightness(float b) override { bright_=std::max(0.0f,std::min(1.0f,b)); update_color_from_hsv(); }
    float get_alpha() const override { return alpha_; }
    void set_alpha(float a) override { alpha_=std::max(0.0f,std::min(1.0f,a)); color_.w=alpha_; }
    bool is_alpha_enabled() const override { return alpha_enabled_; }
    void set_alpha_enabled(bool e) override { alpha_enabled_=e; }
    const char* get_hex_string() const override {
        char buf[16];
        int r=(int)(color_.x*255),g=(int)(color_.y*255),b=(int)(color_.z*255),a=(int)(color_.w*255);
        if(alpha_enabled_) std::snprintf(buf,sizeof(buf),"#%02X%02X%02X%02X",r,g,b,a);
        else std::snprintf(buf,sizeof(buf),"#%02X%02X%02X",r,g,b);
        hex_str_=buf; return hex_str_.c_str();
    }
    void set_hex_string(const char* hex) override {
        if(!hex||hex[0]!='#') return;
        unsigned int val=0; std::sscanf(hex+1,"%x",&val);
        int len=(int)std::strlen(hex+1);
        if(len>=8) color_={((val>>24)&0xFF)/255.0f,((val>>16)&0xFF)/255.0f,((val>>8)&0xFF)/255.0f,(val&0xFF)/255.0f};
        else if(len>=6) color_={((val>>16)&0xFF)/255.0f,((val>>8)&0xFF)/255.0f,(val&0xFF)/255.0f,1.0f};
        update_hsv_from_color();
    }
    math::Vec4 get_previous_color() const override { return prev_color_; }
    void set_previous_color(const math::Vec4& c) override { prev_color_=c; }
    int get_swatch_count() const override { return (int)swatches_.size(); }
    math::Vec4 get_swatch_color(int i) const override { return (i>=0&&i<(int)swatches_.size())?swatches_[i]:math::Vec4(); }
    void set_swatch_color(int i,const math::Vec4& c) override { if(i>=0&&i<(int)swatches_.size()) swatches_[i]=c; }
    void add_swatch(const math::Vec4& c) override { swatches_.push_back(c); }
    void remove_swatch(int i) override { if(i>=0&&i<(int)swatches_.size()) swatches_.erase(swatches_.begin()+i); }
    void clear_swatches() override { swatches_.clear(); }
    bool is_hex_input_visible() const override { return hex_vis_; }
    void set_hex_input_visible(bool v) override { hex_vis_=v; }
    bool is_preview_visible() const override { return preview_vis_; }
    void set_preview_visible(bool v) override { preview_vis_=v; }
    const ColorPickerStyle& get_color_picker_style() const override { return style_; }
    void set_color_picker_style(const ColorPickerStyle& s) override { style_=s; }
    void set_color_picker_event_handler(IColorPickerEventHandler* h) override { handler_=h; }
    void get_color_picker_render_info(ColorPickerRenderInfo* out) const override {
        if(!out) return;
        auto b=base_.get_bounds();
        out->widget=this; out->bounds=b;
        out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->mode=mode_;
        out->color=color_; out->hue=hue_; out->saturation=sat_;
        out->value_brightness=bright_; out->alpha=alpha_;
        out->show_alpha=alpha_enabled_; out->show_hex_input=hex_vis_; out->show_preview=preview_vis_;
        out->editing_channel=editing_channel_; out->edit_buffer=edit_buf_.c_str();
    }
};

// Factory functions
IGuiButton* create_button_widget(ButtonType type) { return new GuiButton(type); }
IGuiImage* create_image_widget() { return new GuiImage(); }
IGuiSlider* create_slider_widget(SliderOrientation orient) { auto* s = new GuiSlider(); s->set_orientation(orient); return s; }
IGuiProgressBar* create_progress_bar_widget(ProgressBarMode mode) { auto* p = new GuiProgressBar(); p->set_mode(mode); return p; }
IGuiColorPicker* create_color_picker_widget(ColorPickerMode mode) { auto* c = new GuiColorPicker(); c->set_mode(mode); return c; }

} // namespace gui
} // namespace window
