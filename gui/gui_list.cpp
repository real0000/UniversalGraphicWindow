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
    ListBoxStyle style_=ListBoxStyle::default_style();
    IListBoxEventHandler* handler_=nullptr;
    mutable WidgetRenderInfo ri_;
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }
public:
    bool handle_mouse_move(const math::Vec2& p) override {
        if (sb_drag_) {
            float content_h = get_total_content_height();
            set_scroll_offset(scrollbar_offset_from_mouse(base_.get_bounds(), content_h, math::y(p)));
            return true;
        }
        return base_.handle_mouse_move(p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && !pressed) { sb_drag_ = false; }
        if (btn == MouseButton::Left && pressed) {
            float content_h = get_total_content_height();
            if (scrollbar_hit_test(base_.get_bounds(), content_h, p)) {
                sb_drag_ = true;
                set_scroll_offset(scrollbar_offset_from_mouse(base_.get_bounds(), content_h, math::y(p)));
                return true;
            }
            auto b = base_.get_bounds();
            float rel_y = math::y(p) - math::y(math::box_min(b)) + scroll_y_;
            int row = (style_.row_height > 0) ? (int)(rel_y / style_.row_height) : -1;
            if (row >= 0 && row < (int)items_.size()) {
                int old = selected_;
                selected_ = items_[row].id;
                if (selected_ != old && handler_) handler_->on_item_selected(selected_);
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    int add_item(const char* text,const char* icon) override {
        int id=next_id_++; items_.push_back({id,text?text:"",icon?icon:""}); return id;
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
        return (float)items_.size() * style_.row_height;
    }
    bool handle_mouse_scroll(float, float dy) override {
        float step = style_.row_height * 2;
        set_scroll_offset(scroll_y_ - dy * step);
        return true;
    }
    void scroll_to_item(int id) override {
        int i = find_idx(id);
        if (i >= 0) set_scroll_offset((float)i * style_.row_height);
    }
    void ensure_item_visible(int id) override {
        int i = find_idx(id);
        if (i < 0) return;
        float item_top = (float)i * style_.row_height;
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
        // Background
        ri_.push_rect(bx, by, bw, bh, s.row_background, d++, noclip);
        // Rows
        int count = (int)items_.size();
        float row_h = s.row_height;
        for (int i = 0; i < count; i++) {
            float ry = by + i * row_h - scroll_y_;
            if (ry + row_h < by || ry > by + bh) continue;
            bool is_sel = (items_[i].id == selected_);
            math::Vec4 row_bg = is_sel ? s.selected_background
                              : (i%2==0) ? s.row_background : s.row_alt_background;
            ri_.push_rect(bx, ry, bw, row_h, row_bg, d++, clip);
            math::Vec4 text_col = is_sel ? s.selected_text_color : s.text_color;
            if (!items_[i].text.empty())
                ri_.push_text(items_[i].text.c_str(), bx+s.item_padding, ry, bw-s.item_padding, row_h,
                              text_col, 11.0f, Alignment::CenterLeft, d++, clip);
        }
        // Embedded scrollbar
        float content_h = (float)count * row_h;
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
        ri_.push_outline(bx, by, bw, bh, math::Vec4(0.25f,0.25f,0.27f,1.0f), d, noclip);
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
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }
public:
    bool hit_test(const math::Vec2& p) const override {
        // When open, hit test includes the dropdown area
        if (base_.hit_test(p)) return true;
        if (open_) {
            auto b = base_.get_bounds();
            float bx = math::x(math::box_min(b)), by = math::y(math::box_max(b));
            float bw = math::box_width(b);
            float dh = std::min(style_.dropdown_max_height, (float)items_.size() * style_.item_height);
            auto drop_box = math::make_box(bx, by, bx + bw, by + dh);
            if (math::box_contains(drop_box, p)) return true;
        }
        return false;
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled()) return false;
        if (btn == MouseButton::Left && pressed) {
            if (open_) {
                // Check if click is in dropdown area
                auto b = base_.get_bounds();
                float drop_y = math::y(math::box_max(b));
                float bx = math::x(math::box_min(b));
                float bw = math::box_width(b);
                float rel_y = math::y(p) - drop_y;
                if (rel_y >= 0 && math::x(p) >= bx && math::x(p) <= bx + bw) {
                    int row = (style_.item_height > 0) ? (int)(rel_y / style_.item_height) : -1;
                    if (row >= 0 && row < (int)items_.size()) {
                        selected_ = items_[row].id;
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
    void set_selected_item(int id) override { selected_=id; if(handler_) handler_->on_selection_changed(id); }
    const char* get_placeholder() const override { return placeholder_.c_str(); }
    void set_placeholder(const char* t) override { placeholder_=t?t:""; }
    bool is_open() const override { return open_; }
    void open() override { open_=true; if(handler_) handler_->on_dropdown_opened(); }
    void close() override { open_=false; if(handler_) handler_->on_dropdown_closed(); }
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
        // Background + border
        math::Vec4 bg = open_ ? s.open_background : s.background_color;
        ri_.push_rect(bx, by, bw, bh, bg, d++, noclip);
        ri_.push_outline(bx, by, bw, bh, s.dropdown_border_color, d, noclip);
        // Selected text or placeholder
        int si = find_idx(selected_);
        const char* text = (si >= 0) ? items_[si].text.c_str() : placeholder_.c_str();
        math::Vec4 text_col = (si >= 0) ? s.text_color : s.placeholder_color;
        if (text && text[0])
            ri_.push_text(text, bx+8, by, bw-26, bh, text_col, 13.0f, Alignment::CenterLeft, d++, noclip);
        // Arrow indicator
        ri_.push_rect(bx+bw-18, by+bh/2-3, 8, 6, s.arrow_color, d++, noclip);
        // Dropdown list
        if (open_) {
            int count = (int)items_.size();
            float drop_h = std::min(s.dropdown_max_height, (float)count * s.item_height);
            float dy = by + bh;
            ri_.push_rect(bx, dy, bw, drop_h, s.dropdown_background, d++, noclip);
            math::Box drop_clip = math::make_box(bx, dy, bw, drop_h);
            for (int i = 0; i < count && i*s.item_height < drop_h; i++) {
                float ry = dy + i * s.item_height;
                bool is_sel = (items_[i].id == selected_);
                ri_.push_rect(bx, ry, bw, s.item_height,
                              is_sel ? s.item_selected_background : s.dropdown_background, d++, drop_clip);
                if (!items_[i].text.empty()) {
                    math::Vec4 ic = is_sel ? s.item_selected_text_color : s.item_text_color;
                    ri_.push_text(items_[i].text.c_str(), bx+s.item_padding, ry, bw-s.item_padding, s.item_height,
                                  ic, 13.0f, Alignment::CenterLeft, d++, drop_clip);
                }
            }
            ri_.push_outline(bx, dy, bw, drop_h, s.dropdown_border_color, d, noclip);
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
