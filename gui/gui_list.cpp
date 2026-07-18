/*
 * gui_list.cpp - ListBox and ComboBox Implementations
 */

#include "gui_widget_base.hpp"
#include <algorithm>

namespace window {
namespace gui {

class GuiListBox : public WidgetBase<IGuiListBox, WidgetType::ListBox> {
    std::vector<WidgetItem> items_;
    int next_id_=0, selected_=-1, hovered_=-1;
    ListBoxSelectionMode sel_mode_=ListBoxSelectionMode::Single;
    std::vector<int> multi_sel_;
    float scroll_y_=0;
    bool sb_drag_=false;
    // Drag-out (palette rows): press arms a candidate; moving past the threshold
    // tracks with on_item_drag; release fires on_item_drop (or a plain click).
    bool drag_out_=false, drag_active_=false;
    int  drag_id_=-1;
    math::Vec2 drag_press_{0.0f, 0.0f};
    ListBoxStyle style_=ListBoxStyle::default_style();
    IListBoxEventHandler* handler_=nullptr;
    std::function<std::vector<ListItemModel>()> provider_;   // bound model source (set once)
    // Custom row-widget template (set once): factory builds a reusable row widget,
    // binder populates it from a row's model. When set, built-in row drawing is
    // suppressed and the list manages a pool of row child widgets instead.
    std::function<IGuiWidget*()> row_factory_;
    std::function<void(IGuiWidget*, const ListItemModel&)> row_binder_;
    std::vector<IGuiWidget*> row_widgets_;                   // pooled row widgets (children)
    mutable WidgetRenderInfo ri_;
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }
    float row_pitch() const { return style_.row_height + style_.row_gap; }
    // Reconstruct a ListItemModel for row i (what the binder consumes).
    ListItemModel item_model(int i) const {
        const WidgetItem& it = items_[i];
        ListItemModel m; m.id=it.id; m.text=it.text; m.swatch=it.swatch;
        m.editable=it.editable; m.has_action=it.has_action; m.enabled=it.enabled;
        m.selected=(it.id==selected_); m.text_color=it.text_color;
        m.row_color=it.row_color; m.center=it.centered; return m;
    }
    // Grow/position/populate the row-widget pool to match the model. Called before
    // each render (refresh_bindings). Rows fully off-view are hidden; extras hidden.
    void reconcile_row_widgets() {
        if (!row_factory_) return;
        auto b=base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b));
        float bw=math::box_width(b), bh=math::box_height(b);
        float rh=style_.row_height, pitch=row_pitch();
        while ((int)row_widgets_.size() < (int)items_.size()) {
            IGuiWidget* w=row_factory_(); if(!w) break;
            row_widgets_.push_back(w); base_.add_child(w);
        }
        for (int i=0;i<(int)row_widgets_.size();++i) {
            IGuiWidget* w=row_widgets_[i]; if(!w) continue;
            if (i<(int)items_.size()) {
                float ry=by+i*pitch-scroll_y_;
                bool vis=(ry+rh>by)&&(ry<by+bh);
                w->set_visible(vis);
                if (vis) { w->set_bounds(math::make_box(bx,ry,bw,rh)); if(row_binder_) row_binder_(w,item_model(i)); }
            } else {
                w->set_visible(false);
            }
        }
    }
public:
    bool handle_mouse_move(const math::Vec2& p) override {
        if (sb_drag_) {
            float content_h = get_total_content_height();
            set_scroll_offset(scrollbar_offset_from_mouse(base_.get_bounds(), content_h, math::y(p)));
            return true;
        }
        if (drag_id_ >= 0) {
            if (!drag_active_ &&
                std::abs(math::x(p) - math::x(drag_press_)) +
                std::abs(math::y(p) - math::y(drag_press_)) > 6.0f)
                drag_active_ = true;
            if (drag_active_) { if (handler_) handler_->on_item_drag(drag_id_, p); return true; }
        }
        return base_.handle_mouse_move(p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled()) return false;
        // An active row drag keeps receiving events; its release may land anywhere
        // (that's the drop point).
        if (drag_id_ >= 0 && btn == MouseButton::Left && !pressed) {
            const int id = drag_id_; const bool dropped = drag_active_;
            drag_id_ = -1; drag_active_ = false;
            if (dropped) { if (handler_) handler_->on_item_drop(id, p); return true; }
            // fall through: an in-place release completes a plain click
        }
        if (!hit_test(p)) return false;
        if (btn == MouseButton::Left && !pressed) { sb_drag_ = false; }
        if (row_factory_) {
            // Custom row widgets: their child buttons (delete/test) take the click
            // first; a click on the bare row still selects it.
            if (base_.handle_mouse_button(btn, pressed, p)) return true;
            if (btn == MouseButton::Left && pressed) {
                auto b = base_.get_bounds();
                float rel_y = math::y(p) - math::y(math::box_min(b)) + scroll_y_;
                int row = (row_pitch() > 0) ? (int)(rel_y / row_pitch()) : -1;
                if (row >= 0 && row < (int)items_.size() && items_[row].enabled) {
                    selected_ = items_[row].id;
                    // fire on EVERY click (an action-style list, e.g. the node
                    // palette, acts on each click of the same row)
                    if (handler_) handler_->on_item_selected(selected_);
                    if (drag_out_) { drag_id_ = selected_; drag_press_ = p; drag_active_ = false; }
                }
            }
            return true;
        }
        if (btn == MouseButton::Left && pressed) {
            float content_h = get_total_content_height();
            if (scrollbar_hit_test(base_.get_bounds(), content_h, p)) {
                sb_drag_ = true;
                set_scroll_offset(scrollbar_offset_from_mouse(base_.get_bounds(), content_h, math::y(p)));
                return true;
            }
            auto b = base_.get_bounds();
            float rel_y = math::y(p) - math::y(math::box_min(b)) + scroll_y_;
            int row = (row_pitch() > 0) ? (int)(rel_y / row_pitch()) : -1;
            if (row >= 0 && row < (int)items_.size()) {
                // Trailing "×" action takes the click before selection.
                if (items_[row].has_action && math::box_contains(action_rect(row), p)) {
                    if (handler_) handler_->on_item_action(items_[row].id);
                    return true;
                }
                selected_ = items_[row].id;
                if (handler_) handler_->on_item_selected(selected_);   // every click (see above)
                if (drag_out_) { drag_id_ = selected_; drag_press_ = p; drag_active_ = false; }
            }
        }
        base_.handle_mouse_button(btn, pressed, p);
        return true;   // in-bounds click belongs to the list (entry is hit-guarded)
    }
    int add_item(const char* text,const char* icon) override {
        int id=next_id_++; items_.push_back({id,text?text:"",icon?icon:""}); return id;
    }
    // Idempotent whole-list rebind: if the incoming model matches the current rows
    // (id/text/swatch/action/enabled + selection) nothing changes and no repaint is
    // scheduled; otherwise the rows + selection are replaced and the widget redraws.
    void set_items(std::vector<ListItemModel> model) override {
        auto veq = [](const math::Vec4& a, const math::Vec4& b){
            return a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w; };
        int want_sel = -1;
        for (const auto& m : model) if (m.selected) { want_sel = m.id; break; }
        bool same = model.size()==items_.size() && want_sel==selected_;
        if (same) for (size_t i=0;i<model.size();++i) {
            const auto& m=model[i]; const auto& it=items_[i];
            if (m.id!=it.id || m.text!=it.text || m.enabled!=it.enabled ||
                m.has_action!=it.has_action || m.editable!=it.editable ||
                !veq(m.swatch,it.swatch) || !veq(m.text_color,it.text_color) ||
                !veq(m.row_color,it.row_color) || m.center!=it.centered) { same=false; break; }
        }
        if (same) return;                       // unchanged → no repaint (like set_nodes)
        items_.clear(); items_.reserve(model.size());
        for (auto& m : model) {
            WidgetItem it; it.id=m.id; it.text=std::move(m.text); it.enabled=m.enabled;
            it.swatch=m.swatch; it.editable=m.editable; it.has_action=m.has_action;
            it.text_color=m.text_color; it.row_color=m.row_color; it.centered=m.center;
            items_.push_back(std::move(it));
            if (m.id>=next_id_) next_id_=m.id+1;
        }
        selected_ = want_sel;
        set_scroll_offset(scroll_y_);           // re-clamp against the new content height
        base_.mark_dirty();
    }
    void bind(std::function<std::vector<ListItemModel>()> provider) override {
        provider_ = std::move(provider); base_.mark_dirty();
    }
    void set_row_widget(std::function<IGuiWidget*()> factory,
                        std::function<void(IGuiWidget*, const ListItemModel&)> binder) override {
        row_factory_=std::move(factory); row_binder_=std::move(binder);
        base_.mark_dirty();
    }
    void refresh_bindings() override {
        if (provider_) set_items(provider_());
        reconcile_row_widgets();
    }
    // Pooled custom row widgets live in base_'s child list; expose them so the
    // context's collect/focus traversal reaches them (the WidgetBase wrapper
    // default hides children — same forwarding the CanvasView does).
    int get_child_count() const override { return base_.get_child_count(); }
    IGuiWidget* get_child(int i) const override { return base_.get_child(i); }
    // Trailing "×" action hit zone for a row (right edge, one row_height wide).
    math::Box action_rect(int row) const {
        auto b=base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b)), bw=math::box_width(b);
        float rh=style_.row_height, aw=rh;
        return math::make_box(bx+bw-aw-2.0f, by+row*row_pitch()-scroll_y_, aw, rh);
    }
    int insert_item(int idx,const char* text,const char* icon) override {
        int id=next_id_++; if(idx<0)idx=0; if(idx>(int)items_.size())idx=(int)items_.size();
        items_.insert(items_.begin()+idx,{id,text?text:"",icon?icon:""}); return id;
    }
    bool remove_item(int id) override { int i=find_idx(id); if(i<0) return false; items_.erase(items_.begin()+i); return true; }
    void clear_items() override { items_.clear(); selected_=-1; multi_sel_.clear(); }
    int get_item_count() const override { return (int)items_.size(); }
    const char* get_item_text(int id) const override { int i=find_idx(id); return i>=0?items_[i].text.c_str():""; }
    void set_item_text(int id,const char* t) override { int i=find_idx(id); if(i>=0) items_[i].text=t?t:""; }
    const char* get_item_icon(int id) const override { int i=find_idx(id); return i>=0?items_[i].icon.c_str():""; }
    void set_item_icon(int id,const char* ic) override { int i=find_idx(id); if(i>=0) items_[i].icon=ic?ic:""; }
    bool is_item_enabled(int id) const override { int i=find_idx(id); return i>=0?items_[i].enabled:false; }
    void set_item_enabled(int id,bool e) override { int i=find_idx(id); if(i>=0) items_[i].enabled=e; }
    ListBoxSelectionMode get_selection_mode() const override { return sel_mode_; }
    void set_selection_mode(ListBoxSelectionMode m) override { sel_mode_=m; }
    int get_selected_item() const override { return selected_; }
    void set_selected_item(int id) override { selected_=id; }
    bool activate_item(int item_id) override {
        for (const auto& it : items_) if (it.id == item_id) {
            selected_ = item_id;
            if (handler_) handler_->on_item_selected(item_id);
            base_.mark_dirty();
            return true;
        }
        return false;
    }
    bool activate_item_action(int item_id) override {
        for (const auto& it : items_) if (it.id == item_id) {
            if (handler_) handler_->on_item_action(item_id);
            return true;
        }
        return false;
    }
    void set_item_drag_out(bool enabled) override { drag_out_ = enabled; }
    bool activate_item_drop(int item_id, const math::Vec2& pos) override {
        for (const auto& it : items_) if (it.id == item_id) {
            if (handler_) handler_->on_item_drop(item_id, pos);
            return true;
        }
        return false;
    }
    void get_selected_items(std::vector<int>& out) const override { out=multi_sel_; }
    void set_selected_items(const std::vector<int>& ids) override { multi_sel_=ids; }
    void clear_selection() override { selected_=-1; multi_sel_.clear(); }
    float get_scroll_offset() const override { return scroll_y_; }
    void set_scroll_offset(float offset) override {
        float max_scroll = get_total_content_height() - math::box_height(base_.get_bounds());
        if (max_scroll < 0) max_scroll = 0;
        scroll_y_ = std::max(0.0f, std::min(offset, max_scroll));
    }
    float get_total_content_height() const override {
        const int n = (int)items_.size();
        return n <= 0 ? 0.0f : (float)n * row_pitch() - style_.row_gap;
    }
    bool handle_mouse_scroll(float, float dy) override {
        if (get_total_content_height() <= math::box_height(base_.get_bounds()) + 0.5f)
            return false;   // content fits: let an enclosing ScrollView take the wheel
        float step = row_pitch() * 2;
        set_scroll_offset(scroll_y_ - dy * step);
        return true;
    }
    void scroll_to_item(int id) override {
        int i = find_idx(id);
        if (i >= 0) set_scroll_offset((float)i * row_pitch());
    }
    void ensure_item_visible(int id) override {
        int i = find_idx(id);
        if (i < 0) return;
        float item_top = (float)i * row_pitch();
        float item_bot = item_top + style_.row_height;
        float view_h = math::box_height(base_.get_bounds());
        if (item_top < scroll_y_) set_scroll_offset(item_top);
        else if (item_bot > scroll_y_ + view_h) set_scroll_offset(item_bot - view_h);
    }
    void set_item_user_data(int id,void* d) override { int i=find_idx(id); if(i>=0) items_[i].user_data=d; }
    void* get_item_user_data(int id) const override { int i=find_idx(id); return i>=0?items_[i].user_data:nullptr; }
    void sort_items(bool asc) override {
        std::sort(items_.begin(),items_.end(),[asc](const WidgetItem& a,const WidgetItem& b){return asc?(a.text<b.text):(a.text>b.text);});
    }
    const ListBoxStyle& get_list_box_style() const override { return style_; }
    void set_list_box_style(const ListBoxStyle& s) override { style_=s; }
    void set_list_event_handler(IListBoxEventHandler* h) override { handler_=h; }
    void get_list_box_render_info(ListBoxRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->total_item_count=(int)items_.size(); out->scroll_offset_y=scroll_y_;
    }
    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        math::Box clip = b;
        auto noclip = math::make_box(0,0,0,0);
        int32_t d = 0;
        const auto& s = style_;
        // Background (alpha-gated: transparent lists ride the panel fill)
        if (s.background_color.w > 0.0f)
            ri_.push_rect(bx, by, bw, bh, s.background_color, d++, noclip);
        // Rows — suppressed when custom row widgets draw them (children render on top).
        int count = row_factory_ ? 0 : (int)items_.size();
        float row_h = s.row_height, pitch = row_pitch();
        for (int i = 0; i < count; i++) {
            float ry = by + i * pitch - scroll_y_;
            if (ry + row_h < by || ry > by + bh) continue;
            bool is_sel = (items_[i].id == selected_);
            bool dis = !items_[i].enabled;
            math::Vec4 row_bg = (is_sel && !dis) ? s.selected_background
                              : items_[i].row_color.w > 0.0f ? items_[i].row_color   // accent row
                              : (i%2==0) ? s.row_background : s.row_alt_background;
            if (row_bg.w > 0.0f) {
                if (s.row_corner_radius > 0.0f)
                    ri_.push_round_rect(bx, ry, bw, row_h, s.row_corner_radius, row_bg, d++, clip);
                else
                    ri_.push_rect(bx, ry, bw, row_h, row_bg, d++, clip);
            }
            math::Vec4 text_col = dis ? s.disabled_text_color
                                : is_sel ? s.selected_text_color : s.text_color;
            if (!is_sel && items_[i].text_color.w > 0.0f)
                text_col = items_[i].text_color;   // per-row severity/accent colour
            // Leading colour swatch (model rows): a small rounded dot; text indents past it.
            float text_x = bx + s.item_padding;
            if (items_[i].swatch.w > 0.0f) {
                float sw = row_h * 0.42f, sxo = bx + s.item_padding, syo = ry + (row_h - sw) * 0.5f;
                ri_.push_round_rect(sxo, syo, sw, sw, 2.0f, items_[i].swatch, d++, clip);
                text_x = sxo + sw + s.item_padding * 0.75f;
            }
            // Trailing "×" delete affordance (model has_action): a filled square button
            // when the style gives it a background, else the plain glyph.
            float text_w = bx + bw - text_x - s.item_padding;
            if (items_[i].has_action) {
                float aw = row_h;
                const math::Vec4 axc = s.action_text_color.w > 0.0f ? s.action_text_color
                                     : dis ? s.disabled_text_color : s.text_color;
                if (s.action_background.w > 0.0f) {
                    const float asz = s.action_size > 0.0f ? s.action_size : row_h - 8.0f;
                    const float axm = bx + bw - aw - 2.0f + (aw - asz) * 0.5f;
                    const float aym = ry + (row_h - asz) * 0.5f;
                    ri_.push_round_rect(axm, aym, asz, asz, s.action_corner_radius,
                                        s.action_background, d++, clip);
                    ri_.push_text("\xC3\x97", axm, aym, asz, asz, axc,
                                  s.font_size * 0.92f, Alignment::Center, d++, clip);
                } else {
                    ri_.push_text("\xC3\x97", bx + bw - aw, ry, aw, row_h, axc,
                                  s.font_size, Alignment::Center, d++, clip);   // × (U+00D7)
                }
                text_w -= aw;
            }
            if (!items_[i].text.empty())
                ri_.push_text(items_[i].text.c_str(), text_x, ry, text_w, row_h,
                              text_col, s.font_size,
                              items_[i].centered ? Alignment::Center : Alignment::CenterLeft,
                              d++, clip);
        }
        // Embedded scrollbar
        float content_h = (float)count * pitch;
        if (content_h > bh) {
            const float sb_w = 10.0f;
            float sb_x = bx + bw - sb_w - 1;
            ri_.push_rect(sb_x, by, sb_w, bh, math::Vec4(0.12f,0.12f,0.13f,0.6f), d++, noclip);
            float thumb_ratio = bh / content_h;
            float thumb_h = std::max(16.0f, bh * thumb_ratio);
            float track_range = bh - thumb_h;
            float max_scroll = content_h - bh;
            float pos_ratio = (max_scroll > 0) ? scroll_y_ / max_scroll : 0.0f;
            ri_.push_rect(sb_x, by + track_range*pos_ratio, sb_w, thumb_h,
                          math::Vec4(0.4f,0.4f,0.42f,0.7f), d++, noclip);
        }
        if (s.border_color.w > 0.0f)
            ri_.push_outline(bx, by, bw, bh, s.border_color, d, noclip);
        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }

    int get_visible_list_items(ListBoxItemRenderInfo* out,int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)items_.size());
        for(int i=0;i<n;++i){out[i].item_id=items_[i].id;out[i].text=items_[i].text.c_str();out[i].icon_name=items_[i].icon.c_str();out[i].enabled=items_[i].enabled;out[i].selected=(items_[i].id==selected_);}
        return n;
    }
};

// ============================================================================
// GuiComboBox
// ============================================================================

class GuiComboBox : public WidgetBase<IGuiComboBox, WidgetType::ComboBox> {
    std::vector<WidgetItem> items_;
    int next_id_=0, selected_=-1;
    bool open_=false;
    std::string placeholder_;
    ComboBoxStyle style_=ComboBoxStyle::default_style();
    IComboBoxEventHandler* handler_=nullptr;
    mutable WidgetRenderInfo ri_;
    mutable float drop_scroll_=0.0f;   // vertical scroll within the dropdown (px), when it can't fit
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }
    // Where the open dropdown is placed: below the box when there's room, otherwise
    // flipped above; height clamped to the available space (the widget's clip rect,
    // which the host sets to the window — so it never spills off-screen). When it
    // still can't fit, the list scrolls (drop_scroll_) with a scrollbar.
    struct DropGeom { float dy, h, full; bool up, scroll; };
    DropGeom drop_geom() const {
        auto b = base_.get_bounds();
        float by = math::y(math::box_min(b)), bh = math::box_height(b);
        float full = (float)items_.size() * style_.item_height;
        bool bounded = base_.is_clip_enabled();
        math::Box area = base_.get_clip_rect();
        float top = bounded ? math::y(math::box_min(area)) : by + bh - full;
        float bot = bounded ? math::y(math::box_min(area)) + math::box_height(area) : by + bh + full;
        float below = bot - (by + bh), above = by - top;
        bool up = (full <= below) ? false : (full <= above) ? true : (above > below);
        float avail = up ? above : below;
        float h = std::min(style_.dropdown_max_height, std::min(full, std::max(0.0f, avail)));
        return { up ? by - h : by + bh, h, full, up, full > h + 0.5f };
    }
    float max_drop_scroll() const { auto g = drop_geom(); return std::max(0.0f, g.full - g.h); }
public:
    bool handle_mouse_scroll(float, float dy) override {
        if (!open_) return false;
        drop_scroll_ = std::max(0.0f, std::min(drop_scroll_ - dy * style_.item_height, max_drop_scroll()));
        base_.mark_dirty();
        return true;
    }
    bool hit_test(const math::Vec2& p) const override {
        // When open, hit test includes the dropdown area (above or below the box).
        if (base_.hit_test(p)) return true;
        if (open_) {
            auto b = base_.get_bounds();
            float bx = math::x(math::box_min(b)), bw = math::box_width(b);
            auto g = drop_geom();
            if (math::box_contains(math::make_box(bx, g.dy, bw, g.h), p)) return true;
        }
        return false;
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled()) return false;
        if (btn == MouseButton::Left && pressed) {
            if (open_) {
                auto b = base_.get_bounds();
                float bx = math::x(math::box_min(b)), bw = math::box_width(b);
                auto g = drop_geom();
                float rel_y = math::y(p) - g.dy + drop_scroll_;   // account for flip + scroll
                if (rel_y >= 0 && math::y(p) >= g.dy && math::y(p) <= g.dy + g.h &&
                    math::x(p) >= bx && math::x(p) <= bx + bw) {
                    int row = (style_.item_height > 0) ? (int)(rel_y / style_.item_height) : -1;
                    if (row >= 0 && row < (int)items_.size() && selected_ != items_[row].id) {
                        selected_ = items_[row].id;
                        base_.mark_dirty();   // repaint the collapsed label ourselves
                        if (handler_) handler_->on_selection_changed(selected_);
                    }
                }
                close();
                return true;
            } else if (base_.hit_test(p)) {
                open();
                return true;
            }
        }
        return false;
    }
    int add_item(const char* text,const char* icon) override { int id=next_id_++; items_.push_back({id,text?text:"",icon?icon:""}); return id; }
    int insert_item(int idx,const char* text,const char* icon) override {
        int id=next_id_++; if(idx<0)idx=0; if(idx>(int)items_.size())idx=(int)items_.size();
        items_.insert(items_.begin()+idx,{id,text?text:"",icon?icon:""}); return id;
    }
    bool remove_item(int id) override { int i=find_idx(id); if(i<0)return false; items_.erase(items_.begin()+i); return true; }
    void clear_items() override { items_.clear(); selected_=-1; }
    int get_item_count() const override { return (int)items_.size(); }
    const char* get_item_text(int id) const override { int i=find_idx(id); return i>=0?items_[i].text.c_str():""; }
    void set_item_text(int id,const char* t) override { int i=find_idx(id); if(i>=0)items_[i].text=t?t:""; }
    const char* get_item_icon(int id) const override { int i=find_idx(id); return i>=0?items_[i].icon.c_str():""; }
    void set_item_icon(int id,const char* ic) override { int i=find_idx(id); if(i>=0)items_[i].icon=ic?ic:""; }
    bool is_item_enabled(int id) const override { int i=find_idx(id); return i>=0?items_[i].enabled:false; }
    void set_item_enabled(int id,bool e) override { int i=find_idx(id); if(i>=0)items_[i].enabled=e; }
    int get_selected_item() const override { return selected_; }
    void set_selected_item(int id) override { if(selected_==id) return; selected_=id; base_.mark_dirty(); if(handler_) handler_->on_selection_changed(id); }
    void set_item_value(int id,const char* v) override { int i=find_idx(id); if(i>=0)items_[i].value=v?v:""; }
    const char* get_item_value(int id) const override { int i=find_idx(id); return i>=0?items_[i].value.c_str():""; }
    const char* get_selected_value() const override { int i=find_idx(selected_); return i>=0?items_[i].value.c_str():""; }
    void set_selected_value(const char* v) override {
        const std::string want = v?v:"";
        for(const auto& it:items_) if(it.value==want){ set_selected_item(it.id); return; }
    }
    const char* get_placeholder() const override { return placeholder_.c_str(); }
    void set_placeholder(const char* t) override { placeholder_=t?t:""; }
    bool is_open() const override { return open_; }
    // open/close/select self-invalidate so the widget repaints its collapsed label
    // and dropdown on its own — no app callback needed just to trigger a redraw.
    void open() override { if(open_) return; open_=true; drop_scroll_=0.0f; base_.mark_dirty(); if(handler_) handler_->on_dropdown_opened(); }
    void close() override { if(!open_) return; open_=false; base_.mark_dirty(); if(handler_) handler_->on_dropdown_closed(); }
    void toggle() override { if(open_) close(); else open(); }
    void set_item_user_data(int id,void* d) override { int i=find_idx(id); if(i>=0)items_[i].user_data=d; }
    void* get_item_user_data(int id) const override { int i=find_idx(id); return i>=0?items_[i].user_data:nullptr; }
    const ComboBoxStyle& get_combo_box_style() const override { return style_; }
    void set_combo_box_style(const ComboBoxStyle& s) override { style_=s; }
    void set_combo_event_handler(IComboBoxEventHandler* h) override { handler_=h; }
    void get_combo_box_render_info(ComboBoxRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->is_open=open_; out->item_count=(int)items_.size();
        int si=find_idx(selected_);
        out->display_text=(si>=0)?items_[si].text.c_str():placeholder_.c_str();
        out->is_placeholder=(si<0);
    }
    int get_visible_combo_items(ComboBoxItemRenderInfo* out,int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)items_.size());
        for(int i=0;i<n;++i){out[i].item_id=items_[i].id;out[i].text=items_[i].text.c_str();out[i].icon_name=items_[i].icon.c_str();out[i].enabled=items_[i].enabled;out[i].selected=(items_[i].id==selected_);}
        return n;
    }

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float bw = math::box_width(b), bh = math::box_height(b);
        auto noclip = math::make_box(0,0,0,0);
        int32_t d = 0;
        const auto& s = style_;
        // Background (rounded when corner_radius > 0). The closed box is intentionally
        // borderless — a caller wanting an outline sets border_width AND a non-zero
        // dropdown_border_color (the dropdown list below always gets its border).
        math::Vec4 bg = open_ ? s.open_background : s.background_color;
        if (s.corner_radius > 0.0f) ri_.push_round_rect(bx, by, bw, bh, s.corner_radius, bg, d++, noclip);
        else                        ri_.push_rect(bx, by, bw, bh, bg, d++, noclip);
        if (s.border_width > 0.0f && s.dropdown_border_color.w > 0.0f)
            ri_.push_outline(bx, by, bw, bh, s.dropdown_border_color, d, noclip);
        // Selected text or placeholder (styled font; right edge reserves the arrow).
        int si = find_idx(selected_);
        const char* text = (si >= 0) ? items_[si].text.c_str() : placeholder_.c_str();
        math::Vec4 text_col = (si >= 0) ? s.text_color : s.placeholder_color;
        if (text && text[0])
            ri_.push_text(text, bx + s.item_padding, by,
                          bw - 2.0f * s.item_padding - s.arrow_size, bh,
                          text_col, s.font_size, Alignment::CenterLeft, d++, noclip);
        // Arrow (▾ glyph, not a block) at the right, matching a text-drawn chevron.
        ri_.push_text("\xE2\x96\xBE", bx + bw - s.item_padding - s.arrow_size, by,
                      s.arrow_size, bh, s.arrow_color, s.font_size, Alignment::Center, d++, noclip);
        // Dropdown list — placed by drop_geom() (below or flipped above, clamped to
        // the available area), scrolled by drop_scroll_ with a scrollbar when clamped.
        if (open_) {
            int count = (int)items_.size();
            auto g = drop_geom();
            float dy = g.dy, drop_h = g.h;
            drop_scroll_ = std::max(0.0f, std::min(drop_scroll_, std::max(0.0f, g.full - drop_h)));
            math::Box drop_clip = math::make_box(bx, dy, bw, drop_h);
            float sb_w = g.scroll ? 6.0f : 0.0f;
            ri_.push_rect(bx, dy, bw, drop_h, s.dropdown_background, d++, drop_clip);
            for (int i = 0; i < count; i++) {
                float ry = dy + i * s.item_height - drop_scroll_;
                if (ry + s.item_height <= dy || ry >= dy + drop_h) continue;   // off-view
                bool is_sel = (items_[i].id == selected_);
                ri_.push_rect(bx, ry, bw - sb_w, s.item_height,
                              is_sel ? s.item_selected_background : s.dropdown_background, d++, drop_clip);
                if (!items_[i].text.empty()) {
                    math::Vec4 ic = is_sel ? s.item_selected_text_color : s.item_text_color;
                    ri_.push_text(items_[i].text.c_str(), bx+s.item_padding, ry, bw-s.item_padding-sb_w, s.item_height,
                                  ic, s.font_size, Alignment::CenterLeft, d++, drop_clip);
                }
            }
            if (g.scroll) {   // scrollbar track + proportional thumb
                float track_x = bx + bw - sb_w;
                ri_.push_rect(track_x, dy, sb_w, drop_h, math::Vec4(0.10f,0.10f,0.11f,0.8f), d++, drop_clip);
                float thumb_h = std::max(16.0f, drop_h * drop_h / g.full);
                float max_sc = std::max(1.0f, g.full - drop_h);
                float ty = dy + (drop_h - thumb_h) * (drop_scroll_ / max_sc);
                ri_.push_rect(track_x, ty, sb_w, thumb_h, math::Vec4(0.45f,0.45f,0.47f,0.9f), d++, drop_clip);
            }
            ri_.push_outline(bx, dy, bw, drop_h, s.dropdown_border_color, d, drop_clip);
        }
        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

// Factory functions
IGuiListBox* create_list_box_widget() { return new GuiListBox(); }
IGuiComboBox* create_combo_box_widget() { return new GuiComboBox(); }

} // namespace gui
} // namespace window
