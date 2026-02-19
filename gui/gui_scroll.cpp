/*
 * gui_scroll.cpp - ScrollBar and ScrollView Implementations
 */

#include "gui_widget_base.hpp"
#include <cmath>

namespace window {
namespace gui {

class GuiScrollBar : public WidgetBase<IGuiScrollBar, WidgetType::Custom> {
    ScrollBarOrientation orient_ = ScrollBarOrientation::Vertical;
    float value_=0, min_=0, max_=100, page_size_=10, line_step_=1, page_step_=10;
    bool thumb_hover_=false, thumb_press_=false;
    float drag_offset_=0; // offset from thumb top to click point
    ScrollBarStyle style_ = ScrollBarStyle::default_style();

    // Compute thumb geometry: returns thumb_pos and thumb_len along the scrollbar axis
    void get_thumb_geometry(float& thumb_pos, float& thumb_len, float& track_start, float& track_len) const {
        auto b = base_.get_bounds();
        bool vert = (orient_ == ScrollBarOrientation::Vertical);
        track_start = vert ? math::y(math::box_min(b)) : math::x(math::box_min(b));
        track_len = vert ? math::box_height(b) : math::box_width(b);
        float range = max_ - min_;
        float total = range + page_size_;
        if (total <= 0) { thumb_pos = track_start; thumb_len = track_len; return; }
        float thumb_ratio = page_size_ / total;
        thumb_len = track_len * thumb_ratio;
        if (thumb_len < style_.thumb_min_length) thumb_len = style_.thumb_min_length;
        float track_range = track_len - thumb_len;
        float pos_ratio = (range > 0) ? (value_ - min_) / range : 0;
        thumb_pos = track_start + track_range * pos_ratio;
    }

    // Convert a position along the axis to a scroll value
    float pos_to_value(float pos, float track_start, float track_len, float thumb_len) const {
        float track_range = track_len - thumb_len;
        if (track_range <= 0) return min_;
        float ratio = (pos - track_start) / track_range;
        ratio = std::max(0.0f, std::min(1.0f, ratio));
        return min_ + ratio * (max_ - min_);
    }

public:
    explicit GuiScrollBar(ScrollBarOrientation o=ScrollBarOrientation::Vertical) : orient_(o) {}

    bool handle_mouse_move(const math::Vec2& p) override {
        if (thumb_press_) {
            // Dragging thumb
            float thumb_pos, thumb_len, track_start, track_len;
            get_thumb_geometry(thumb_pos, thumb_len, track_start, track_len);
            bool vert = (orient_ == ScrollBarOrientation::Vertical);
            float mouse_pos = vert ? math::y(p) : math::x(p);
            float new_thumb_top = mouse_pos - drag_offset_;
            value_ = pos_to_value(new_thumb_top, track_start, track_len, thumb_len);
            value_ = std::max(min_, std::min(max_, value_));
            return true;
        }
        // Hover detection
        if (!hit_test(p)) { thumb_hover_ = false; return base_.handle_mouse_move(p); }
        float thumb_pos, thumb_len, track_start, track_len;
        get_thumb_geometry(thumb_pos, thumb_len, track_start, track_len);
        bool vert = (orient_ == ScrollBarOrientation::Vertical);
        float mouse_pos = vert ? math::y(p) : math::x(p);
        thumb_hover_ = (mouse_pos >= thumb_pos && mouse_pos < thumb_pos + thumb_len);
        return base_.handle_mouse_move(p);
    }

    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (btn == MouseButton::Left) {
            if (pressed && hit_test(p)) {
                float thumb_pos, thumb_len, track_start, track_len;
                get_thumb_geometry(thumb_pos, thumb_len, track_start, track_len);
                bool vert = (orient_ == ScrollBarOrientation::Vertical);
                float mouse_pos = vert ? math::y(p) : math::x(p);

                if (mouse_pos >= thumb_pos && mouse_pos < thumb_pos + thumb_len) {
                    // Click on thumb: start dragging
                    thumb_press_ = true;
                    drag_offset_ = mouse_pos - thumb_pos;
                } else {
                    // Click on track: jump to position
                    float new_thumb_top = mouse_pos - thumb_len / 2.0f;
                    value_ = pos_to_value(new_thumb_top, track_start, track_len, thumb_len);
                    value_ = std::max(min_, std::min(max_, value_));

                    // Start drag from new position
                    thumb_press_ = true;
                    drag_offset_ = thumb_len / 2.0f;
                }
                return true;
            } else if (!pressed) {
                if (thumb_press_) {
                    thumb_press_ = false;
                    return true;
                }
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }

    ScrollBarOrientation get_orientation() const override { return orient_; }
    void set_orientation(ScrollBarOrientation o) override { orient_=o; }
    float get_value() const override { return value_; }
    void set_value(float v) override { value_=std::max(min_,std::min(max_,v)); }
    float get_min_value() const override { return min_; }
    float get_max_value() const override { return max_; }
    void set_range(float mn,float mx) override { min_=mn; max_=mx; value_=std::max(min_,std::min(max_,value_)); }
    float get_page_size() const override { return page_size_; }
    void set_page_size(float s) override { page_size_=s; }
    float get_line_step() const override { return line_step_; }
    void set_line_step(float s) override { line_step_=s; }
    float get_page_step() const override { return page_step_; }
    void set_page_step(float s) override { page_step_=s; }
    const ScrollBarStyle& get_scrollbar_style() const override { return style_; }
    void set_scrollbar_style(const ScrollBarStyle& s) override { style_=s; }
    bool is_thumb_hovered() const override { return thumb_hover_; }
    bool is_thumb_pressed() const override { return thumb_press_; }

    void get_scrollbar_render_info(ScrollBarRenderInfo* out) const override {
        if(!out) return;
        auto b=base_.get_bounds();
        out->widget=this; out->orientation=orient_; out->bounds=b; out->track_rect=b;
        out->style=style_; out->value=value_; out->page_size=page_size_;
        out->thumb_state=thumb_press_?WidgetState::Pressed:(thumb_hover_?WidgetState::Hovered:WidgetState::Normal);
    }
};

// ============================================================================
// GuiScrollView
// ============================================================================

class GuiScrollView : public WidgetBase<IGuiScrollView, WidgetType::ScrollArea> {
    math::Vec2 scroll_offset_, content_size_{800,600};
    ScrollBarVisibility h_vis_=ScrollBarVisibility::Auto, v_vis_=ScrollBarVisibility::Auto;
    IGuiScrollBar *h_bar_=nullptr, *v_bar_=nullptr;
    float scroll_speed_=20.0f;
    bool inertia_=true;
    ScrollViewSize size_preset_=ScrollViewSize::Medium;
    ScrollViewSizeParams size_params_=ScrollViewSizeParams::from_size(ScrollViewSize::Medium);
public:
    bool handle_mouse_scroll(float dx,float dy) override {
        auto mx=get_max_scroll_offset();
        scroll_offset_=math::Vec2(
            std::max(0.0f,std::min(math::x(scroll_offset_)-dx*scroll_speed_,math::x(mx))),
            std::max(0.0f,std::min(math::y(scroll_offset_)-dy*scroll_speed_,math::y(mx))));
        return true;
    }
    int get_child_count() const override { return base_.get_child_count(); }
    IGuiWidget* get_child(int i) const override { return base_.get_child(i); }
    bool add_child(IGuiWidget* c) override { return base_.add_child(c); }
    bool remove_child(IGuiWidget* c) override { return base_.remove_child(c); }
    bool remove_child_at(int i) override { return base_.remove_child_at(i); }
    void clear_children() override { base_.clear_children(); }
    math::Vec2 get_scroll_offset() const override { return scroll_offset_; }
    void set_scroll_offset(const math::Vec2& o) override { scroll_offset_=o; }
    math::Vec2 get_content_size() const override { return content_size_; }
    void set_content_size(const math::Vec2& s) override { content_size_=s; }
    math::Vec2 get_viewport_size() const override {
        auto b=base_.get_bounds(); return math::Vec2(math::box_width(b),math::box_height(b));
    }
    math::Vec2 get_max_scroll_offset() const override {
        auto vp=get_viewport_size();
        return math::Vec2(std::max(0.0f,math::x(content_size_)-math::x(vp)),
                          std::max(0.0f,math::y(content_size_)-math::y(vp)));
    }
    ScrollBarVisibility get_h_scrollbar_visibility() const override { return h_vis_; }
    void set_h_scrollbar_visibility(ScrollBarVisibility v) override { h_vis_=v; }
    ScrollBarVisibility get_v_scrollbar_visibility() const override { return v_vis_; }
    void set_v_scrollbar_visibility(ScrollBarVisibility v) override { v_vis_=v; }
    IGuiScrollBar* get_h_scrollbar() const override { return h_bar_; }
    void set_h_scrollbar(IGuiScrollBar* b) override { h_bar_=b; }
    IGuiScrollBar* get_v_scrollbar() const override { return v_bar_; }
    void set_v_scrollbar(IGuiScrollBar* b) override { v_bar_=b; }
    float get_scroll_speed() const override { return scroll_speed_; }
    void set_scroll_speed(float s) override { scroll_speed_=s; }
    bool is_scroll_inertia_enabled() const override { return inertia_; }
    void set_scroll_inertia_enabled(bool e) override { inertia_=e; }
    void scroll_to(const math::Vec2& o,bool) override { scroll_offset_=o; }
    void scroll_to_widget(const IGuiWidget*,bool) override {}
    void scroll_to_top(bool) override { scroll_offset_=math::Vec2(math::x(scroll_offset_),0); }
    void scroll_to_bottom(bool) override { auto mx=get_max_scroll_offset(); scroll_offset_=math::Vec2(math::x(scroll_offset_),math::y(mx)); }
    bool is_scrolling() const override { return false; }
    bool can_scroll_horizontal() const override { return math::x(content_size_)>math::x(get_viewport_size()); }
    bool can_scroll_vertical() const override { return math::y(content_size_)>math::y(get_viewport_size()); }
    ScrollViewSize get_size() const override { return size_preset_; }
    void set_size(ScrollViewSize s) override { size_preset_=s; size_params_=ScrollViewSizeParams::from_size(s); }
    ScrollViewSizeParams get_size_params() const override { return size_params_; }
    void set_size_params(const ScrollViewSizeParams& p) override { size_params_=p; size_preset_=ScrollViewSize::Custom; }
    void get_scroll_render_info(ScrollViewRenderInfo* out) const override {
        if(!out) return;
        auto b=base_.get_bounds();
        out->widget=this; out->bounds=b;
        out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->scroll_offset=scroll_offset_; out->content_size=content_size_;
        out->viewport_size=get_viewport_size();
        out->size_preset=size_preset_; out->size_params=size_params_;
        out->h_scrollbar_visible=can_scroll_horizontal()&&h_vis_!=ScrollBarVisibility::Never;
        out->v_scrollbar_visible=can_scroll_vertical()&&v_vis_!=ScrollBarVisibility::Never;
    }
};

// Factory functions
IGuiScrollBar* create_scroll_bar_widget(ScrollBarOrientation orient) { return new GuiScrollBar(orient); }
IGuiScrollView* create_scroll_view_widget() { return new GuiScrollView(); }

} // namespace gui
} // namespace window
