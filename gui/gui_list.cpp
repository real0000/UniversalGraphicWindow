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
    ListBoxStyle style_=ListBoxStyle::default_style();
    IListBoxEventHandler* handler_=nullptr;
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }
public:
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && pressed) {
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
    void scroll_to_item(int) override {}
    void ensure_item_visible(int) override {}
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
};

// Factory functions
IGuiListBox* create_list_box_widget() { return new GuiListBox(); }
IGuiComboBox* create_combo_box_widget() { return new GuiComboBox(); }

} // namespace gui
} // namespace window
