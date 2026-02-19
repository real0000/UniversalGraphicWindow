/*
 * gui_property.cpp - PropertyGrid Implementation
 */

#include "gui_widget_base.hpp"
#include <unordered_map>

namespace window {
namespace gui {

class GuiPropertyGrid : public WidgetBase<IGuiPropertyGrid, WidgetType::Custom> {
    struct Prop {
        int id=-1; std::string name, category, str_val;
        PropertyType type=PropertyType::String;
        int int_val=0; float float_val=0; bool bool_val=false;
        math::Vec2 vec2_val; math::Vec4 vec4_val;
        std::vector<std::string> enum_opts; int enum_idx=0;
        float range_min=0, range_max=1;
        bool read_only=false;
    };
    std::vector<Prop> props_;
    int next_id_=0, selected_=-1;
    std::unordered_map<std::string,bool> cat_expanded_;
    float name_col_w_=150, row_h_=24;
    PropertyGridStyle style_=PropertyGridStyle::default_style();
    IPropertyGridEventHandler* handler_=nullptr;
    static const std::vector<std::string> empty_opts_;
    int editing_id_=-1;
    std::string edit_buf_;
    int find_idx(int id) const { for(int i=0;i<(int)props_.size();++i) if(props_[i].id==id) return i; return -1; }

    void start_editing(int prop_id) {
        int i = find_idx(prop_id);
        if (i < 0 || props_[i].read_only) return;
        auto& p = props_[i];
        if (p.type == PropertyType::Bool || p.type == PropertyType::Category) return;
        editing_id_ = prop_id;
        edit_buf_ = format_value(i);
    }

    void commit_edit() {
        int i = find_idx(editing_id_);
        if (i < 0) { editing_id_ = -1; return; }
        auto& p = props_[i];
        switch (p.type) {
            case PropertyType::String: p.str_val = edit_buf_; break;
            case PropertyType::Int: p.int_val = std::atoi(edit_buf_.c_str()); break;
            case PropertyType::Float: p.float_val = (float)std::atof(edit_buf_.c_str()); break;
            case PropertyType::Range: {
                float v = (float)std::atof(edit_buf_.c_str());
                if (v < p.range_min) v = p.range_min;
                if (v > p.range_max) v = p.range_max;
                p.float_val = v;
                break;
            }
            default: break;
        }
        if (handler_) handler_->on_property_changed(editing_id_);
        editing_id_ = -1;
    }

    void cancel_edit() { editing_id_ = -1; }

    // Build visible rows: category headers + properties under expanded categories
    struct VisRow { bool is_cat; std::string cat_name; int prop_idx; };
    void collect_visible(std::vector<VisRow>& out) const {
        std::vector<std::string> cats;
        for (auto& p : props_) {
            bool found = false;
            for (auto& c : cats) { if (c == p.category) { found = true; break; } }
            if (!found) cats.push_back(p.category);
        }
        for (auto& cat : cats) {
            out.push_back({true, cat, -1});
            bool expanded = true;
            auto it = cat_expanded_.find(cat);
            if (it != cat_expanded_.end()) expanded = it->second;
            if (expanded) {
                for (int i = 0; i < (int)props_.size(); ++i) {
                    if (props_[i].category == cat)
                        out.push_back({false, cat, i});
                }
            }
        }
    }

    // Format property value as display string
    mutable std::vector<std::string> val_cache_;
    const char* format_value(int idx) const {
        auto& p = props_[idx];
        char buf[128] = {};
        switch (p.type) {
            case PropertyType::String: return p.str_val.c_str();
            case PropertyType::Int: std::snprintf(buf, sizeof(buf), "%d", p.int_val); break;
            case PropertyType::Float: std::snprintf(buf, sizeof(buf), "%.2f", p.float_val); break;
            case PropertyType::Bool: return p.bool_val ? "true" : "false";
            case PropertyType::Range: std::snprintf(buf, sizeof(buf), "%.1f", p.float_val); break;
            case PropertyType::Color: std::snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f, %.2f)", p.vec4_val.x, p.vec4_val.y, p.vec4_val.z, p.vec4_val.w); break;
            case PropertyType::Vec2: std::snprintf(buf, sizeof(buf), "(%.2f, %.2f)", math::x(p.vec2_val), math::y(p.vec2_val)); break;
            case PropertyType::Enum: return (p.enum_idx >= 0 && p.enum_idx < (int)p.enum_opts.size()) ? p.enum_opts[p.enum_idx].c_str() : "";
            default: return p.str_val.c_str();
        }
        val_cache_.push_back(std::string(buf));
        return val_cache_.back().c_str();
    }

public:
    bool is_focusable() const override { return true; }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && pressed) {
            auto b = base_.get_bounds();
            float bx = math::x(math::box_min(b));
            float rel_x = math::x(p) - bx;
            float rel_y = math::y(p) - math::y(math::box_min(b));
            int row = (row_h_ > 0) ? (int)(rel_y / row_h_) : -1;

            std::vector<VisRow> visible;
            collect_visible(visible);

            if (row >= 0 && row < (int)visible.size()) {
                if (visible[row].is_cat) {
                    // Toggle category expand/collapse
                    auto& cat = visible[row].cat_name;
                    bool cur = true;
                    auto it = cat_expanded_.find(cat);
                    if (it != cat_expanded_.end()) cur = it->second;
                    cat_expanded_[cat] = !cur;
                    cancel_edit();
                } else {
                    auto& prop = props_[visible[row].prop_idx];
                    int prev_selected = selected_;
                    selected_ = prop.id;

                    if (prop.type == PropertyType::Bool && !prop.read_only) {
                        prop.bool_val = !prop.bool_val;
                        if (handler_) handler_->on_property_changed(prop.id);
                        cancel_edit();
                    } else if (rel_x > name_col_w_ && !prop.read_only) {
                        // Click on value column: start editing
                        start_editing(prop.id);
                    } else if (editing_id_ != prop.id) {
                        cancel_edit();
                    }
                }
            } else {
                cancel_edit();
            }
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    bool handle_key(int code, bool pressed, int mods) override {
        if (editing_id_ >= 0 && pressed) {
            enum : int { K_Enter=308, K_Backspace=309, K_Escape=300 };
            if (code == K_Enter) { commit_edit(); return true; }
            else if (code == K_Escape) { cancel_edit(); return true; }
            else if (code == K_Backspace) {
                if (!edit_buf_.empty()) edit_buf_.pop_back();
                return true;
            }
        }
        return base_.handle_key(code, pressed, mods);
    }
    bool handle_text_input(const char* t) override {
        if (editing_id_ >= 0 && t) {
            for (const char* c = t; *c; ++c) {
                if (*c >= 32) edit_buf_ += *c;
            }
            return true;
        }
        return false;
    }
    int add_property(const char* cat,const char* name,PropertyType type) override {
        int id=next_id_++; Prop p; p.id=id; p.name=name?name:""; p.category=cat?cat:""; p.type=type;
        props_.push_back(p); return id;
    }
    bool remove_property(int id) override { int i=find_idx(id); if(i<0)return false; props_.erase(props_.begin()+i); return true; }
    void clear_properties() override { props_.clear(); selected_=-1; }
    int get_property_count() const override { return (int)props_.size(); }
    const char* get_property_name(int id) const override { int i=find_idx(id); return i>=0?props_[i].name.c_str():""; }
    const char* get_property_category(int id) const override { int i=find_idx(id); return i>=0?props_[i].category.c_str():""; }
    PropertyType get_property_type(int id) const override { int i=find_idx(id); return i>=0?props_[i].type:PropertyType::String; }
    void set_string_value(int id,const char* v) override { int i=find_idx(id); if(i>=0)props_[i].str_val=v?v:""; }
    int get_int_value(int id) const override { int i=find_idx(id); return i>=0?props_[i].int_val:0; }
    void set_int_value(int id,int v) override { int i=find_idx(id); if(i>=0)props_[i].int_val=v; }
    float get_float_value(int id) const override { int i=find_idx(id); return i>=0?props_[i].float_val:0; }
    void set_float_value(int id,float v) override { int i=find_idx(id); if(i>=0)props_[i].float_val=v; }
    bool get_bool_value(int id) const override { int i=find_idx(id); return i>=0?props_[i].bool_val:false; }
    void set_bool_value(int id,bool v) override { int i=find_idx(id); if(i>=0)props_[i].bool_val=v; }
    math::Vec2 get_vec2_value(int id) const override { int i=find_idx(id); return i>=0?props_[i].vec2_val:math::Vec2(); }
    void set_vec2_value(int id,const math::Vec2& v) override { int i=find_idx(id); if(i>=0)props_[i].vec2_val=v; }
    math::Vec4 get_vec4_value(int id) const override { int i=find_idx(id); return i>=0?props_[i].vec4_val:math::Vec4(); }
    void set_vec4_value(int id,const math::Vec4& v) override { int i=find_idx(id); if(i>=0)props_[i].vec4_val=v; }
    void set_enum_options(int id,const std::vector<std::string>& opts) override { int i=find_idx(id); if(i>=0)props_[i].enum_opts=opts; }
    const std::vector<std::string>& get_enum_options(int id) const override { int i=find_idx(id); return i>=0?props_[i].enum_opts:empty_opts_; }
    int get_enum_index(int id) const override { int i=find_idx(id); return i>=0?props_[i].enum_idx:0; }
    void set_enum_index(int id,int idx) override { int i=find_idx(id); if(i>=0)props_[i].enum_idx=idx; }
    void set_range_limits(int id,float mn,float mx) override { int i=find_idx(id); if(i>=0){props_[i].range_min=mn;props_[i].range_max=mx;} }
    float get_range_min(int id) const override { int i=find_idx(id); return i>=0?props_[i].range_min:0; }
    float get_range_max(int id) const override { int i=find_idx(id); return i>=0?props_[i].range_max:1; }
    bool is_property_read_only(int id) const override { int i=find_idx(id); return i>=0?props_[i].read_only:true; }
    void set_property_read_only(int id,bool r) override { int i=find_idx(id); if(i>=0)props_[i].read_only=r; }
    bool is_category_expanded(const char* cat) const override { auto it=cat_expanded_.find(cat?cat:""); return it!=cat_expanded_.end()?it->second:true; }
    void set_category_expanded(const char* cat,bool e) override { cat_expanded_[cat?cat:""]=e; }
    void expand_all() override { for(auto& p:cat_expanded_) p.second=true; }
    void collapse_all() override { for(auto& p:cat_expanded_) p.second=false; }
    int get_selected_property() const override { return selected_; }
    void set_selected_property(int id) override { selected_=id; }
    float get_name_column_width() const override { return name_col_w_; }
    void set_name_column_width(float w) override { name_col_w_=w; }
    float get_row_height() const override { return row_h_; }
    void set_row_height(float h) override { row_h_=h; }
    const PropertyGridStyle& get_property_grid_style() const override { return style_; }
    void set_property_grid_style(const PropertyGridStyle& s) override { style_=s; }
    void set_property_event_handler(IPropertyGridEventHandler* h) override { handler_=h; }
    void get_property_grid_render_info(PropertyGridRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->total_row_count=(int)props_.size(); out->selected_property=selected_;
        out->editing_property=editing_id_; out->edit_buffer=edit_buf_.c_str();
    }
    const char* get_string_value(int id) const override {
        int i = find_idx(id);
        if (i < 0) return "";
        return format_value(i);
    }

    int get_visible_property_items(PropertyRenderItem* out,int max) const override {
        if(!out||max<=0) return 0;
        val_cache_.clear();
        std::vector<VisRow> visible;
        collect_visible(visible);
        int n=std::min(max,(int)visible.size());
        for(int i=0;i<n;++i){
            if (visible[i].is_cat) {
                // Find a property in this category to get a stable pointer
                const char* cat_str = "";
                for (auto& p : props_) {
                    if (p.category == visible[i].cat_name) { cat_str = p.category.c_str(); break; }
                }
                out[i].property_id = -1;
                out[i].name = cat_str;
                out[i].category = cat_str;
                out[i].is_category_header = true;
                bool exp = true;
                auto it = cat_expanded_.find(visible[i].cat_name);
                if (it != cat_expanded_.end()) exp = it->second;
                out[i].expanded = exp;
                out[i].depth = 0;
                out[i].selected = false;
            } else {
                int idx = visible[i].prop_idx;
                out[i].property_id = props_[idx].id;
                out[i].name = props_[idx].name.c_str();
                out[i].category = props_[idx].category.c_str();
                out[i].type = props_[idx].type;
                out[i].read_only = props_[idx].read_only;
                out[i].selected = (props_[idx].id == selected_);
                out[i].is_category_header = false;
                out[i].depth = 1;
            }
        }
        return n;
    }
};
const std::vector<std::string> GuiPropertyGrid::empty_opts_;

// Factory function
IGuiPropertyGrid* create_property_grid_widget() { return new GuiPropertyGrid(); }

} // namespace gui
} // namespace window
