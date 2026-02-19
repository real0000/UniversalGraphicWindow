/*
 * gui_menu.cpp - Menu and MenuBar Implementations
 */

#include "gui_widget_base.hpp"

namespace window {
namespace gui {

class GuiMenu : public WidgetBase<IGuiMenu, WidgetType::Custom> {
    struct Item {
        int id=-1; MenuItemType type=MenuItemType::Normal;
        std::string text, icon, shortcut;
        bool enabled=true, checked=false;
        int group_id=-1;
        IGuiMenu* submenu=nullptr;
        void* user_data=nullptr;
    };
    std::vector<Item> items_;
    int next_id_=0;
    bool open_=false;
    MenuStyle style_=MenuStyle::default_style();
    IMenuEventHandler* handler_=nullptr;
    int find_idx(int id) const { for(int i=0;i<(int)items_.size();++i) if(items_[i].id==id) return i; return -1; }

    // Compute the row Y offset for item at visual index, accounting for separators
    float item_y_offset(int visual_idx) const {
        float y = 0;
        for (int i = 0; i < visual_idx && i < (int)items_.size(); i++) {
            y += (items_[i].type == MenuItemType::Separator) ? style_.separator_height : style_.item_height;
        }
        return y;
    }
    float total_height() const {
        float h = 0;
        for (auto& it : items_)
            h += (it.type == MenuItemType::Separator) ? style_.separator_height : style_.item_height;
        return h;
    }
public:
    bool hit_test(const math::Vec2& p) const override {
        if (!open_) return false;
        auto b = base_.get_bounds();
        float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        float w = std::max(math::box_width(b), style_.min_width);
        float h = total_height();
        auto drop = math::make_box(bx, by, bx + w, by + h);
        return math::box_contains(drop, p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!open_) return false;
        if (btn == MouseButton::Left && pressed) {
            if (!hit_test(p)) { hide(); return false; }
            auto b = base_.get_bounds();
            float by = math::y(math::box_min(b));
            float rel_y = math::y(p) - by;
            // Find which item was clicked
            float y = 0;
            for (int i = 0; i < (int)items_.size(); i++) {
                float ih = (items_[i].type == MenuItemType::Separator) ? style_.separator_height : style_.item_height;
                if (rel_y >= y && rel_y < y + ih) {
                    if (items_[i].type == MenuItemType::Separator) break;
                    if (!items_[i].enabled) break;
                    if (items_[i].type == MenuItemType::Checkbox) {
                        items_[i].checked = !items_[i].checked;
                    } else if (items_[i].type == MenuItemType::Radio) {
                        for (auto& it : items_)
                            if (it.type == MenuItemType::Radio && it.group_id == items_[i].group_id) it.checked = false;
                        items_[i].checked = true;
                    }
                    if (handler_) handler_->on_menu_item_clicked(items_[i].id);
                    hide();
                    return true;
                }
                y += ih;
            }
        }
        return false;
    }
    int add_item(const char* text, const char* icon, const char* shortcut) override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Normal;
        it.text=text?text:""; it.icon=icon?icon:""; it.shortcut=shortcut?shortcut:"";
        items_.push_back(it); return id;
    }
    int add_checkbox_item(const char* text, bool checked) override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Checkbox;
        it.text=text?text:""; it.checked=checked; items_.push_back(it); return id;
    }
    int add_radio_item(const char* text, int group_id, bool checked) override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Radio;
        it.text=text?text:""; it.group_id=group_id; it.checked=checked;
        items_.push_back(it); return id;
    }
    int add_separator() override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Separator;
        items_.push_back(it); return id;
    }
    int add_submenu(const char* text, IGuiMenu* sub) override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Submenu;
        it.text=text?text:""; it.submenu=sub; items_.push_back(it); return id;
    }
    int insert_item(int index, const char* text, const char* icon, const char* shortcut) override {
        int id=next_id_++; Item it; it.id=id; it.type=MenuItemType::Normal;
        it.text=text?text:""; it.icon=icon?icon:""; it.shortcut=shortcut?shortcut:"";
        if(index<0)index=0; if(index>(int)items_.size())index=(int)items_.size();
        items_.insert(items_.begin()+index,it); return id;
    }
    bool remove_item(int id) override { int i=find_idx(id); if(i<0)return false; items_.erase(items_.begin()+i); return true; }
    void clear_items() override { items_.clear(); }
    int get_item_count() const override { return (int)items_.size(); }
    const char* get_item_text(int id) const override { int i=find_idx(id); return i>=0?items_[i].text.c_str():""; }
    void set_item_text(int id, const char* t) override { int i=find_idx(id); if(i>=0) items_[i].text=t?t:""; }
    const char* get_item_icon(int id) const override { int i=find_idx(id); return i>=0?items_[i].icon.c_str():""; }
    void set_item_icon(int id, const char* ic) override { int i=find_idx(id); if(i>=0) items_[i].icon=ic?ic:""; }
    const char* get_item_shortcut(int id) const override { int i=find_idx(id); return i>=0?items_[i].shortcut.c_str():""; }
    void set_item_shortcut(int id, const char* s) override { int i=find_idx(id); if(i>=0) items_[i].shortcut=s?s:""; }
    MenuItemType get_item_type(int id) const override { int i=find_idx(id); return i>=0?items_[i].type:MenuItemType::Normal; }
    bool is_item_enabled(int id) const override { int i=find_idx(id); return i>=0?items_[i].enabled:false; }
    void set_item_enabled(int id, bool e) override { int i=find_idx(id); if(i>=0) items_[i].enabled=e; }
    bool is_item_checked(int id) const override { int i=find_idx(id); return i>=0?items_[i].checked:false; }
    void set_item_checked(int id, bool c) override {
        int i=find_idx(id); if(i<0) return;
        items_[i].checked=c;
        if(c && items_[i].type==MenuItemType::Radio){
            for(auto& it:items_) if(it.type==MenuItemType::Radio && it.group_id==items_[i].group_id && it.id!=id) it.checked=false;
        }
    }
    IGuiMenu* get_submenu(int id) const override { int i=find_idx(id); return i>=0?items_[i].submenu:nullptr; }
    void show_at(const math::Vec2& pos) override {
        auto b=base_.get_bounds(); float w=math::box_width(b),h=math::box_height(b);
        base_.set_bounds(math::make_box(math::x(pos),math::y(pos),math::x(pos)+w,math::y(pos)+h));
        open_=true; if(handler_) handler_->on_menu_opened();
    }
    void show_relative_to(const IGuiWidget* anchor, PopupPlacement placement) override {
        if(!anchor){show_at(math::Vec2());return;}
        auto ab=anchor->get_bounds();
        float ax=math::x(math::box_min(ab)),ay=math::y(math::box_min(ab));
        float ah=math::box_height(ab);
        show_at(math::Vec2(ax,ay+ah));
    }
    void hide() override { open_=false; if(handler_) handler_->on_menu_closed(); }
    bool is_open() const override { return open_; }
    void set_item_user_data(int id, void* d) override { int i=find_idx(id); if(i>=0) items_[i].user_data=d; }
    void* get_item_user_data(int id) const override { int i=find_idx(id); return i>=0?items_[i].user_data:nullptr; }
    const MenuStyle& get_menu_style() const override { return style_; }
    void set_menu_style(const MenuStyle& s) override { style_=s; }
    void set_menu_event_handler(IMenuEventHandler* h) override { handler_=h; }
    void get_menu_render_info(MenuRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->item_count=(int)items_.size(); out->is_open=open_;
    }
    int get_visible_menu_items(MenuItemRenderInfo* out, int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)items_.size());
        for(int i=0;i<n;++i){
            out[i].item_id=items_[i].id; out[i].type=items_[i].type;
            out[i].text=items_[i].text.c_str(); out[i].icon_name=items_[i].icon.c_str();
            out[i].shortcut_text=items_[i].shortcut.c_str();
            out[i].enabled=items_[i].enabled; out[i].checked=items_[i].checked;
            out[i].has_submenu=(items_[i].type==MenuItemType::Submenu);
        }
        return n;
    }
};

// ============================================================================
// GuiMenuBar
// ============================================================================

class GuiMenuBar : public WidgetBase<IGuiMenuBar, WidgetType::Custom> {
    struct Entry { int id=-1; std::string text; IGuiMenu* menu=nullptr; bool enabled=true; };
    std::vector<Entry> entries_;
    int next_id_=0;
    int open_menu_=-1;  // id of currently open menu entry, -1 if none
    MenuBarStyle style_=MenuBarStyle::default_style();
    int find_idx(int id) const { for(int i=0;i<(int)entries_.size();++i) if(entries_[i].id==id) return i; return -1; }
public:
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled()) return false;

        // If a menu is open, forward click to it first
        if (open_menu_ >= 0) {
            int oi = find_idx(open_menu_);
            if (oi >= 0 && entries_[oi].menu) {
                if (entries_[oi].menu->handle_mouse_button(btn, pressed, p))
                    { open_menu_ = -1; return true; }
            }
        }

        if (btn == MouseButton::Left && pressed) {
            if (!hit_test(p)) {
                // Click outside: close open menu
                if (open_menu_ >= 0) { close_open_menu(); return true; }
                return false;
            }
            auto b = base_.get_bounds();
            float bx = math::x(math::box_min(b));
            float rel_x = math::x(p) - bx - 8;
            float item_w = 60.0f;
            int col = (item_w > 0) ? (int)(rel_x / item_w) : -1;
            if (col >= 0 && col < (int)entries_.size() && entries_[col].enabled) {
                if (open_menu_ == entries_[col].id) {
                    close_open_menu();
                } else {
                    close_open_menu();
                    open_menu_ = entries_[col].id;
                    if (entries_[col].menu) {
                        float mx = bx + 8 + col * item_w;
                        float my = math::y(math::box_max(b));
                        entries_[col].menu->show_at(math::Vec2(mx, my));
                    }
                }
                return true;
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    void close_open_menu() {
        if (open_menu_ >= 0) {
            int oi = find_idx(open_menu_);
            if (oi >= 0 && entries_[oi].menu) entries_[oi].menu->hide();
            open_menu_ = -1;
        }
    }
    int get_open_menu() const { return open_menu_; }
    int add_menu(const char* text, IGuiMenu* menu) override {
        int id=next_id_++; entries_.push_back({id,text?text:"",menu,true}); return id;
    }
    int insert_menu(int index, const char* text, IGuiMenu* menu) override {
        int id=next_id_++; if(index<0)index=0; if(index>(int)entries_.size())index=(int)entries_.size();
        entries_.insert(entries_.begin()+index,{id,text?text:"",menu,true}); return id;
    }
    bool remove_menu(int id) override { int i=find_idx(id); if(i<0)return false; entries_.erase(entries_.begin()+i); return true; }
    void clear_menus() override { entries_.clear(); }
    int get_menu_count() const override { return (int)entries_.size(); }
    const char* get_menu_text(int id) const override { int i=find_idx(id); return i>=0?entries_[i].text.c_str():""; }
    void set_menu_text(int id, const char* t) override { int i=find_idx(id); if(i>=0) entries_[i].text=t?t:""; }
    IGuiMenu* get_menu(int id) const override { int i=find_idx(id); return i>=0?entries_[i].menu:nullptr; }
    bool is_menu_enabled(int id) const override { int i=find_idx(id); return i>=0?entries_[i].enabled:false; }
    void set_menu_enabled(int id, bool e) override { int i=find_idx(id); if(i>=0) entries_[i].enabled=e; }
    const MenuBarStyle& get_menu_bar_style() const override { return style_; }
    void set_menu_bar_style(const MenuBarStyle& s) override { style_=s; }
    void get_menu_bar_render_info(MenuBarRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->item_count=(int)entries_.size();
    }
    int get_visible_menu_bar_items(MenuBarItemRenderInfo* out, int max) const override {
        if(!out||max<=0) return 0;
        int n=std::min(max,(int)entries_.size());
        for(int i=0;i<n;++i){out[i].item_id=entries_[i].id;out[i].text=entries_[i].text.c_str();out[i].enabled=entries_[i].enabled;out[i].open=(entries_[i].id==open_menu_);}
        return n;
    }
};

// Factory functions
IGuiMenu* create_menu_widget() { return new GuiMenu(); }
IGuiMenuBar* create_menu_bar_widget() { return new GuiMenuBar(); }

} // namespace gui
} // namespace window
