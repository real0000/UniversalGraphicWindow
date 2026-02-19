/*
 * gui_tab.cpp - TabControl Implementation
 */

#include "gui_widget_base.hpp"

namespace window {
namespace gui {

class GuiTabControl : public WidgetBase<IGuiTabControl, WidgetType::TabControl> {
    struct Tab : WidgetItem { bool closable=false; IGuiWidget* content=nullptr; };
    std::vector<Tab> tabs_;
    int next_id_=0, active_=-1;
    TabPosition pos_=TabPosition::Top;
    TabSizeMode size_mode_=TabSizeMode::FitContent;
    float fixed_width_=100;
    bool drag_reorder_=false;
    TabStyle style_=TabStyle::default_style();
    ITabControlEventHandler* handler_=nullptr;
    int find_idx(int id) const { for(int i=0;i<(int)tabs_.size();++i) if(tabs_[i].id==id) return i; return -1; }
public:
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && pressed) {
            auto b = base_.get_bounds();
            float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
            float tab_h = style_.tab_height;
            float rel_y = math::y(p) - by;
            // Only handle clicks in the tab bar area
            if (rel_y >= 0 && rel_y < tab_h) {
                float rel_x = math::x(p) - bx;
                // Determine tab width: fixed or fit-content
                float tw = fixed_width_;
                int col = (tw > 0) ? (int)(rel_x / tw) : -1;
                if (col >= 0 && col < (int)tabs_.size()) {
                    int old = active_;
                    active_ = tabs_[col].id;
                    if (active_ != old && handler_) handler_->on_tab_selected(active_);
                }
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    int add_tab(const char* text,const char* icon) override {
        int id=next_id_++; Tab t; t.id=id; t.text=text?text:""; t.icon=icon?icon:"";
        tabs_.push_back(t); if(active_<0) active_=id; return id;
    }
    int insert_tab(int idx,const char* text,const char* icon) override {
        int id=next_id_++; Tab t; t.id=id; t.text=text?text:""; t.icon=icon?icon:"";
        if(idx<0)idx=0; if(idx>(int)tabs_.size())idx=(int)tabs_.size();
        tabs_.insert(tabs_.begin()+idx,t); if(active_<0)active_=id; return id;
    }
    bool remove_tab(int id) override { int i=find_idx(id); if(i<0)return false; tabs_.erase(tabs_.begin()+i); if(active_==id)active_=tabs_.empty()?-1:tabs_[0].id; return true; }
    void clear_tabs() override { tabs_.clear(); active_=-1; }
    int get_tab_count() const override { return (int)tabs_.size(); }
    const char* get_tab_text(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].text.c_str():""; }
    void set_tab_text(int id,const char* t) override { int i=find_idx(id); if(i>=0)tabs_[i].text=t?t:""; }
    const char* get_tab_icon(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].icon.c_str():""; }
    void set_tab_icon(int id,const char* ic) override { int i=find_idx(id); if(i>=0)tabs_[i].icon=ic?ic:""; }
    bool is_tab_enabled(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].enabled:false; }
    void set_tab_enabled(int id,bool e) override { int i=find_idx(id); if(i>=0)tabs_[i].enabled=e; }
    bool is_tab_closable(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].closable:false; }
    void set_tab_closable(int id,bool c) override { int i=find_idx(id); if(i>=0)tabs_[i].closable=c; }
    IGuiWidget* get_tab_content(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].content:nullptr; }
    void set_tab_content(int id,IGuiWidget* c) override { int i=find_idx(id); if(i>=0)tabs_[i].content=c; }
    int get_active_tab() const override { return active_; }
    void set_active_tab(int id) override { active_=id; if(handler_) handler_->on_tab_selected(id); }
    TabPosition get_tab_position() const override { return pos_; }
    void set_tab_position(TabPosition p) override { pos_=p; }
    TabSizeMode get_tab_size_mode() const override { return size_mode_; }
    void set_tab_size_mode(TabSizeMode m) override { size_mode_=m; }
    float get_fixed_tab_width() const override { return fixed_width_; }
    void set_fixed_tab_width(float w) override { fixed_width_=w; }
    bool is_drag_reorder_enabled() const override { return drag_reorder_; }
    void set_drag_reorder_enabled(bool e) override { drag_reorder_=e; }
    void set_tab_user_data(int id,void* d) override { int i=find_idx(id); if(i>=0)tabs_[i].user_data=d; }
    void* get_tab_user_data(int id) const override { int i=find_idx(id); return i>=0?tabs_[i].user_data:nullptr; }
    const TabStyle& get_tab_style() const override { return style_; }
    void set_tab_style(const TabStyle& s) override { style_=s; }
    void set_tab_event_handler(ITabControlEventHandler* h) override { handler_=h; }
    void get_tab_control_render_info(TabControlRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->position=pos_; out->tab_count=(int)tabs_.size(); out->active_tab=active_;
    }
    int get_visible_tab_items(TabRenderItem* out,int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)tabs_.size());
        for(int i=0;i<n;++i){out[i].tab_id=tabs_[i].id;out[i].text=tabs_[i].text.c_str();out[i].icon_name=tabs_[i].icon.c_str();out[i].active=(tabs_[i].id==active_);out[i].closable=tabs_[i].closable;out[i].enabled=tabs_[i].enabled;}
        return n;
    }
};

// Factory function
IGuiTabControl* create_tab_control_widget(TabPosition pos) { auto* t = new GuiTabControl(); t->set_tab_position(pos); return t; }

} // namespace gui
} // namespace window
