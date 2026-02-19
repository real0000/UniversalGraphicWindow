/*
 * gui_toolbar.cpp - Toolbar and StatusBar Implementations
 */

#include "gui_widget_base.hpp"

namespace window {
namespace gui {

class GuiToolbar : public WidgetBase<IGuiToolbar, WidgetType::Custom> {
    struct Item {
        int id=-1; ToolbarItemType type=ToolbarItemType::Button;
        std::string icon, tooltip;
        bool enabled=true, toggled=false;
        IGuiWidget* widget=nullptr;
        void* user_data=nullptr;
    };
    std::vector<Item> items_;
    int next_id_=0;
    ToolbarOrientation orient_=ToolbarOrientation::Horizontal;
    bool overflow_=true;
    ToolbarStyle style_=ToolbarStyle::default_style();
    IToolbarEventHandler* handler_=nullptr;
    int hovered_idx_=-1;
    int pressed_idx_=-1;
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }

    // Find which item index a click x-position falls on (-1 if none)
    int hit_item(float rel_x) const {
        float ix = style_.toolbar_padding;
        for (int i = 0; i < (int)items_.size(); i++) {
            float w;
            if (items_[i].type == ToolbarItemType::Separator)
                w = style_.separator_width + style_.separator_padding * 2;
            else
                w = style_.button_size + style_.button_padding;
            if (rel_x >= ix && rel_x < ix + w) return i;
            ix += w;
        }
        return -1;
    }
public:
    bool handle_mouse_move(const math::Vec2& p) override {
        if (!base_.is_enabled() || !base_.is_visible()) { hovered_idx_ = -1; return false; }
        if (!hit_test(p)) { hovered_idx_ = -1; return base_.handle_mouse_move(p); }
        auto b = base_.get_bounds();
        float rel_x = math::x(p) - math::x(math::box_min(b));
        int idx = hit_item(rel_x);
        hovered_idx_ = (idx >= 0 && items_[idx].type != ToolbarItemType::Separator) ? idx : -1;
        return base_.handle_mouse_move(p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) { pressed_idx_ = -1; return false; }
        if (btn == MouseButton::Left) {
            auto b = base_.get_bounds();
            float rel_x = math::x(p) - math::x(math::box_min(b));
            int idx = hit_item(rel_x);
            if (pressed) {
                pressed_idx_ = (idx >= 0 && items_[idx].type != ToolbarItemType::Separator && items_[idx].enabled) ? idx : -1;
            } else {
                // On release, fire click/toggle if still on same item
                if (pressed_idx_ >= 0 && pressed_idx_ == idx && items_[idx].enabled) {
                    if (items_[idx].type == ToolbarItemType::ToggleButton) {
                        items_[idx].toggled = !items_[idx].toggled;
                        if (handler_) handler_->on_toolbar_item_toggled(items_[idx].id, items_[idx].toggled);
                    } else {
                        if (handler_) handler_->on_toolbar_item_clicked(items_[idx].id);
                    }
                }
                pressed_idx_ = -1;
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    int add_button(const char* icon, const char* tooltip) override {
        int id=next_id_++; Item it; it.id=id; it.type=ToolbarItemType::Button;
        it.icon=icon?icon:""; it.tooltip=tooltip?tooltip:""; items_.push_back(it); return id;
    }
    int add_toggle_button(const char* icon, const char* tooltip, bool toggled) override {
        int id=next_id_++; Item it; it.id=id; it.type=ToolbarItemType::ToggleButton;
        it.icon=icon?icon:""; it.tooltip=tooltip?tooltip:""; it.toggled=toggled;
        items_.push_back(it); return id;
    }
    int add_separator() override {
        int id=next_id_++; Item it; it.id=id; it.type=ToolbarItemType::Separator;
        items_.push_back(it); return id;
    }
    int add_widget_item(IGuiWidget* w) override {
        int id=next_id_++; Item it; it.id=id; it.type=ToolbarItemType::Widget;
        it.widget=w; items_.push_back(it); return id;
    }
    int insert_button(int index, const char* icon, const char* tooltip) override {
        int id=next_id_++; Item it; it.id=id; it.type=ToolbarItemType::Button;
        it.icon=icon?icon:""; it.tooltip=tooltip?tooltip:"";
        if(index<0)index=0; if(index>(int)items_.size())index=(int)items_.size();
        items_.insert(items_.begin()+index,it); return id;
    }
    bool remove_item(int id) override { int i=find_idx(id); if(i<0)return false; items_.erase(items_.begin()+i); return true; }
    void clear_items() override { items_.clear(); }
    int get_item_count() const override { return (int)items_.size(); }
    ToolbarItemType get_item_type(int id) const override { int i=find_idx(id); return i>=0?items_[i].type:ToolbarItemType::Button; }
    const char* get_item_icon(int id) const override { int i=find_idx(id); return i>=0?items_[i].icon.c_str():""; }
    void set_item_icon(int id, const char* ic) override { int i=find_idx(id); if(i>=0) items_[i].icon=ic?ic:""; }
    const char* get_item_tooltip(int id) const override { int i=find_idx(id); return i>=0?items_[i].tooltip.c_str():""; }
    void set_item_tooltip(int id, const char* t) override { int i=find_idx(id); if(i>=0) items_[i].tooltip=t?t:""; }
    bool is_item_enabled(int id) const override { int i=find_idx(id); return i>=0?items_[i].enabled:false; }
    void set_item_enabled(int id, bool e) override { int i=find_idx(id); if(i>=0) items_[i].enabled=e; }
    bool is_item_toggled(int id) const override { int i=find_idx(id); return i>=0?items_[i].toggled:false; }
    void set_item_toggled(int id, bool t) override { int i=find_idx(id); if(i>=0) items_[i].toggled=t; }
    IGuiWidget* get_item_widget(int id) const override { int i=find_idx(id); return i>=0?items_[i].widget:nullptr; }
    ToolbarOrientation get_orientation() const override { return orient_; }
    void set_orientation(ToolbarOrientation o) override { orient_=o; }
    bool is_overflow_enabled() const override { return overflow_; }
    void set_overflow_enabled(bool e) override { overflow_=e; }
    IGuiMenu* get_overflow_menu() const override { return nullptr; }
    void set_item_user_data(int id, void* d) override { int i=find_idx(id); if(i>=0) items_[i].user_data=d; }
    void* get_item_user_data(int id) const override { int i=find_idx(id); return i>=0?items_[i].user_data:nullptr; }
    const ToolbarStyle& get_toolbar_style() const override { return style_; }
    void set_toolbar_style(const ToolbarStyle& s) override { style_=s; }
    void set_toolbar_event_handler(IToolbarEventHandler* h) override { handler_=h; }
    void get_toolbar_render_info(ToolbarRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->orientation=orient_; out->item_count=(int)items_.size();
    }
    int get_visible_toolbar_items(ToolbarItemRenderInfo* out, int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)items_.size());
        for(int i=0;i<n;++i){
            out[i].item_id=items_[i].id; out[i].type=items_[i].type;
            out[i].icon_name=items_[i].icon.c_str(); out[i].tooltip_text=items_[i].tooltip.c_str();
            out[i].enabled=items_[i].enabled; out[i].toggled=items_[i].toggled;
            out[i].hovered=(i==hovered_idx_); out[i].pressed=(i==pressed_idx_);
        }
        return n;
    }
};

// ============================================================================
// GuiStatusBar
// ============================================================================

class GuiStatusBar : public WidgetBase<IGuiStatusBar, WidgetType::Custom> {
    struct Panel {
        int id=-1; std::string text, icon, tooltip;
        StatusBarPanelSizeMode size_mode=StatusBarPanelSizeMode::Auto;
        float fixed_width=100, min_width=0;
        bool clickable=false;
        IGuiWidget* widget=nullptr;
        void* user_data=nullptr;
    };
    std::vector<Panel> panels_;
    int next_id_=0;
    StatusBarStyle style_=StatusBarStyle::default_style();
    IStatusBarEventHandler* handler_=nullptr;
    int find_idx(int id) const { for(int i=0;i<(int)panels_.size();++i) if(panels_[i].id==id) return i; return -1; }
public:
    int add_panel(const char* text, StatusBarPanelSizeMode mode) override {
        int id=next_id_++; Panel p; p.id=id; p.text=text?text:""; p.size_mode=mode;
        panels_.push_back(p); return id;
    }
    int insert_panel(int index, const char* text, StatusBarPanelSizeMode mode) override {
        int id=next_id_++; Panel p; p.id=id; p.text=text?text:""; p.size_mode=mode;
        if(index<0)index=0; if(index>(int)panels_.size())index=(int)panels_.size();
        panels_.insert(panels_.begin()+index,p); return id;
    }
    bool remove_panel(int id) override { int i=find_idx(id); if(i<0)return false; panels_.erase(panels_.begin()+i); return true; }
    void clear_panels() override { panels_.clear(); }
    int get_panel_count() const override { return (int)panels_.size(); }
    const char* get_panel_text(int id) const override { int i=find_idx(id); return i>=0?panels_[i].text.c_str():""; }
    void set_panel_text(int id, const char* t) override { int i=find_idx(id); if(i>=0) panels_[i].text=t?t:""; }
    const char* get_panel_icon(int id) const override { int i=find_idx(id); return i>=0?panels_[i].icon.c_str():""; }
    void set_panel_icon(int id, const char* ic) override { int i=find_idx(id); if(i>=0) panels_[i].icon=ic?ic:""; }
    const char* get_panel_tooltip(int id) const override { int i=find_idx(id); return i>=0?panels_[i].tooltip.c_str():""; }
    void set_panel_tooltip(int id, const char* t) override { int i=find_idx(id); if(i>=0) panels_[i].tooltip=t?t:""; }
    StatusBarPanelSizeMode get_panel_size_mode(int id) const override { int i=find_idx(id); return i>=0?panels_[i].size_mode:StatusBarPanelSizeMode::Auto; }
    void set_panel_size_mode(int id, StatusBarPanelSizeMode m) override { int i=find_idx(id); if(i>=0) panels_[i].size_mode=m; }
    float get_panel_fixed_width(int id) const override { int i=find_idx(id); return i>=0?panels_[i].fixed_width:0; }
    void set_panel_fixed_width(int id, float w) override { int i=find_idx(id); if(i>=0) panels_[i].fixed_width=w; }
    float get_panel_min_width(int id) const override { int i=find_idx(id); return i>=0?panels_[i].min_width:0; }
    void set_panel_min_width(int id, float w) override { int i=find_idx(id); if(i>=0) panels_[i].min_width=w; }
    bool is_panel_clickable(int id) const override { int i=find_idx(id); return i>=0?panels_[i].clickable:false; }
    void set_panel_clickable(int id, bool c) override { int i=find_idx(id); if(i>=0) panels_[i].clickable=c; }
    IGuiWidget* get_panel_widget(int id) const override { int i=find_idx(id); return i>=0?panels_[i].widget:nullptr; }
    void set_panel_widget(int id, IGuiWidget* w) override { int i=find_idx(id); if(i>=0) panels_[i].widget=w; }
    void set_panel_user_data(int id, void* d) override { int i=find_idx(id); if(i>=0) panels_[i].user_data=d; }
    void* get_panel_user_data(int id) const override { int i=find_idx(id); return i>=0?panels_[i].user_data:nullptr; }
    const StatusBarStyle& get_status_bar_style() const override { return style_; }
    void set_status_bar_style(const StatusBarStyle& s) override { style_=s; }
    void set_status_bar_event_handler(IStatusBarEventHandler* h) override { handler_=h; }
    void get_status_bar_render_info(StatusBarRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->panel_count=(int)panels_.size();
    }
    int get_visible_status_bar_panels(StatusBarPanelRenderInfo* out, int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)panels_.size());
        for(int i=0;i<n;++i){
            out[i].panel_id=panels_[i].id; out[i].text=panels_[i].text.c_str();
            out[i].icon_name=panels_[i].icon.c_str(); out[i].tooltip_text=panels_[i].tooltip.c_str();
            out[i].clickable=panels_[i].clickable;
        }
        return n;
    }
};

// Factory functions
IGuiToolbar* create_toolbar_widget(ToolbarOrientation orient) { auto* t = new GuiToolbar(); t->set_orientation(orient); return t; }
IGuiStatusBar* create_status_bar_widget() { return new GuiStatusBar(); }

} // namespace gui
} // namespace window
