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
    float name_col_w_=150, row_h_=24, scroll_y_=0;
    PropertyGridStyle style_=PropertyGridStyle::default_style();
    IPropertyGridEventHandler* handler_=nullptr;
    static const std::vector<std::string> empty_opts_;
    int editing_id_=-1;
    std::string edit_buf_;
    int edit_cursor_=0;
    int enum_popup_id_=-1;
    bool sb_drag_=false;
    mutable WidgetRenderInfo ri_;
    int find_idx(int id) const { for(int i=0;i<(int)props_.size();++i) if(props_[i].id==id) return i; return -1; }

    void start_editing(int prop_id) {
        int i = find_idx(prop_id);
        if (i < 0 || props_[i].read_only) return;
        auto& p = props_[i];
        if (p.type == PropertyType::Bool || p.type == PropertyType::Category ||
            p.type == PropertyType::Enum) return;
        editing_id_ = prop_id;
        edit_buf_ = format_value(i);
        edit_cursor_ = (int)edit_buf_.size();
    }

    void commit_edit() {
        int i = find_idx(editing_id_);
        if (i < 0) { editing_id_ = -1; edit_cursor_ = 0; return; }
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
        edit_cursor_ = 0;
    }

    void cancel_edit() { editing_id_ = -1; edit_cursor_ = 0; enum_popup_id_ = -1; }

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

    void clamp_scroll() {
        std::vector<VisRow> visible;
        collect_visible(visible);
        float content_h = (float)visible.size() * row_h_;
        float view_h = math::box_height(base_.get_bounds());
        float max_scroll = content_h - view_h;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_y_ < 0) scroll_y_ = 0;
        if (scroll_y_ > max_scroll) scroll_y_ = max_scroll;
    }

    void navigate_selection(int dir) {
        std::vector<VisRow> visible;
        collect_visible(visible);
        if (visible.empty()) return;
        int cur_row = -1;
        for (int i = 0; i < (int)visible.size(); ++i) {
            if (!visible[i].is_cat && props_[visible[i].prop_idx].id == selected_) {
                cur_row = i; break;
            }
        }
        if (cur_row < 0) {
            // No selection: pick first or last property
            if (dir > 0) {
                for (int i = 0; i < (int)visible.size(); ++i) {
                    if (!visible[i].is_cat) { selected_ = props_[visible[i].prop_idx].id; scroll_to_row(i); return; }
                }
            } else {
                for (int i = (int)visible.size()-1; i >= 0; --i) {
                    if (!visible[i].is_cat) { selected_ = props_[visible[i].prop_idx].id; scroll_to_row(i); return; }
                }
            }
            return;
        }
        int next = cur_row + dir;
        while (next >= 0 && next < (int)visible.size() && visible[next].is_cat)
            next += dir;
        if (next >= 0 && next < (int)visible.size() && !visible[next].is_cat) {
            selected_ = props_[visible[next].prop_idx].id;
            scroll_to_row(next);
        }
    }

    void scroll_to_row(int row) {
        float view_h = math::box_height(base_.get_bounds());
        float row_top = row * row_h_;
        float row_bot = row_top + row_h_;
        if (row_top < scroll_y_) scroll_y_ = row_top;
        else if (row_bot > scroll_y_ + view_h) scroll_y_ = row_bot - view_h;
        clamp_scroll();
    }

    void adjust_value_by_arrow(int dir) {
        if (selected_ < 0) return;
        int i = find_idx(selected_);
        if (i < 0 || props_[i].read_only) return;
        auto& p = props_[i];
        switch (p.type) {
            case PropertyType::Bool:
                p.bool_val = !p.bool_val;
                break;
            case PropertyType::Int:
                p.int_val += dir;
                break;
            case PropertyType::Float:
                p.float_val += dir * 0.1f;
                break;
            case PropertyType::Range: {
                float step = (p.range_max - p.range_min) * 0.01f;
                if (step < 0.001f) step = 0.1f;
                p.float_val += dir * step;
                if (p.float_val < p.range_min) p.float_val = p.range_min;
                if (p.float_val > p.range_max) p.float_val = p.range_max;
                break;
            }
            case PropertyType::Enum:
                if (!p.enum_opts.empty()) {
                    p.enum_idx = (p.enum_idx + dir + (int)p.enum_opts.size()) % (int)p.enum_opts.size();
                }
                break;
            default: return;
        }
        if (handler_) handler_->on_property_changed(p.id);
    }
public:
    bool is_focusable() const override { return true; }
    bool handle_mouse_scroll(float, float dy) override {
        if (dy == 0) return false;
        scroll_y_ -= dy * row_h_ * 3;
        clamp_scroll();
        return true;
    }
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
            float bx = math::x(math::box_min(b));
            float by_top = math::y(math::box_min(b));
            float bw = math::box_width(b);
            float bh = math::box_height(b);
            float rel_x = math::x(p) - bx;
            float rel_y = math::y(p) - by_top + scroll_y_;
            int row = (row_h_ > 0) ? (int)(rel_y / row_h_) : -1;

            std::vector<VisRow> visible;
            collect_visible(visible);

            // If enum popup is open, handle clicks inside/outside it first
            if (enum_popup_id_ >= 0) {
                int popup_row = -1;
                for (int i = 0; i < (int)visible.size(); ++i) {
                    if (!visible[i].is_cat && props_[visible[i].prop_idx].id == enum_popup_id_) {
                        popup_row = i; break;
                    }
                }
                if (popup_row >= 0) {
                    auto& pop_p = props_[visible[popup_row].prop_idx];
                    int num_opts = (int)pop_p.enum_opts.size();
                    float pop_y = by_top + (popup_row + 1) * row_h_ - scroll_y_;
                    float pop_x = bx + name_col_w_;
                    float pop_w = bw - name_col_w_;
                    float pop_h = num_opts * row_h_;
                    float px = math::x(p), py = math::y(p);
                    if (px >= pop_x && px < pop_x + pop_w && py >= pop_y && py < pop_y + pop_h) {
                        int opt = (int)((py - pop_y) / row_h_);
                        if (opt >= 0 && opt < num_opts) {
                            pop_p.enum_idx = opt;
                            if (handler_) handler_->on_property_changed(pop_p.id);
                        }
                    }
                }
                enum_popup_id_ = -1;
                return base_.handle_mouse_button(btn, pressed, p);
            }

            if (row >= 0 && row < (int)visible.size()) {
                if (visible[row].is_cat) {
                    auto& cat = visible[row].cat_name;
                    bool cur = true;
                    auto it = cat_expanded_.find(cat);
                    if (it != cat_expanded_.end()) cur = it->second;
                    cat_expanded_[cat] = !cur;
                    cancel_edit();
                } else {
                    auto& prop = props_[visible[row].prop_idx];
                    selected_ = prop.id;

                    if (prop.type == PropertyType::Bool && !prop.read_only) {
                        prop.bool_val = !prop.bool_val;
                        if (handler_) handler_->on_property_changed(prop.id);
                        cancel_edit();
                    } else if (prop.type == PropertyType::Enum && rel_x > name_col_w_ && !prop.read_only) {
                        // Open dropdown popup
                        cancel_edit();
                        enum_popup_id_ = prop.id;
                    } else if (rel_x > name_col_w_ && !prop.read_only) {
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
        if (!pressed) return base_.handle_key(code, pressed, mods);
        enum : int { K_Escape=300, K_Enter=308, K_Backspace=309, K_Delete=310,
                     K_Left=316, K_Right=317, K_Up=318, K_Down=319 };
        // Enum popup dismissal
        if (enum_popup_id_ >= 0) {
            if (code == K_Escape) { enum_popup_id_ = -1; return true; }
            return true; // swallow all keys while popup is open
        }
        if (editing_id_ >= 0) {
            if (code == K_Enter)    { commit_edit(); return true; }
            if (code == K_Escape)   { cancel_edit(); return true; }
            if (code == K_Backspace) {
                if (edit_cursor_ > 0) {
                    edit_buf_.erase(edit_buf_.begin() + edit_cursor_ - 1);
                    --edit_cursor_;
                }
                return true;
            }
            if (code == K_Delete) {
                if (edit_cursor_ < (int)edit_buf_.size()) {
                    edit_buf_.erase(edit_buf_.begin() + edit_cursor_);
                }
                return true;
            }
            if (code == K_Left)  { if (edit_cursor_ > 0) --edit_cursor_; return true; }
            if (code == K_Right) { if (edit_cursor_ < (int)edit_buf_.size()) ++edit_cursor_; return true; }
            if (code == K_Up)    { commit_edit(); navigate_selection(-1); return true; }
            if (code == K_Down)  { commit_edit(); navigate_selection(+1); return true; }
            return base_.handle_key(code, pressed, mods);
        }
        // Not editing
        if (code == K_Up)    { navigate_selection(-1); return true; }
        if (code == K_Down)  { navigate_selection(+1); return true; }
        if (code == K_Enter) {
            if (selected_ >= 0) {
                int i = find_idx(selected_);
                if (i >= 0 && !props_[i].read_only) {
                    if (props_[i].type == PropertyType::Bool) {
                        props_[i].bool_val = !props_[i].bool_val;
                        if (handler_) handler_->on_property_changed(selected_);
                    } else if (props_[i].type == PropertyType::Enum) {
                        enum_popup_id_ = selected_;
                    } else {
                        start_editing(selected_);
                    }
                }
            }
            return true;
        }
        if (code == K_Left)  { adjust_value_by_arrow(-1); return true; }
        if (code == K_Right) { adjust_value_by_arrow(+1); return true; }
        return base_.handle_key(code, pressed, mods);
    }
    bool handle_text_input(const char* t) override {
        if (editing_id_ >= 0 && t) {
            for (const char* c = t; *c; ++c) {
                if ((unsigned char)*c >= 32) {
                    edit_buf_.insert(edit_buf_.begin() + edit_cursor_, *c);
                    ++edit_cursor_;
                }
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
    float get_scroll_offset() const override { return scroll_y_; }
    void set_scroll_offset(float offset) override { scroll_y_ = offset; clamp_scroll(); }
    float get_total_content_height() const override {
        std::vector<VisRow> visible;
        collect_visible(visible);
        return (float)visible.size() * row_h_;
    }
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
        out->scroll_offset_y=scroll_y_;
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

    const WidgetRenderInfo& get_render_info(Window*) const override {
        ri_.invalidate();
        val_cache_.clear();
        auto b = base_.get_bounds();
        float bx=math::x(math::box_min(b)), by=math::y(math::box_min(b));
        float bw=math::box_width(b), bh=math::box_height(b);
        math::Box clip = b;
        auto noclip=math::make_box(0,0,0,0);
        int32_t d=0;
        const auto& s=style_;
        ri_.push_rect(bx, by, bw, bh, s.row_background, d++, noclip);
        // Column divider
        ri_.push_rect(bx+name_col_w_, by, 1, bh, s.separator_color, d++, noclip);

        std::vector<VisRow> visible;
        collect_visible(visible);
        float content_h = (float)visible.size() * row_h_;

        for (int i = 0; i < (int)visible.size(); i++) {
            float ry = by + i * row_h_ - scroll_y_;
            if (ry + row_h_ < by || ry > by + bh) continue;
            if (visible[i].is_cat) {
                // Category header
                ri_.push_rect(bx, ry, bw, row_h_, s.category_background, d++, clip);
                bool exp = true;
                auto it = cat_expanded_.find(visible[i].cat_name);
                if (it != cat_expanded_.end()) exp = it->second;
                // Expand/collapse arrow
                float ax = bx+4, ay = ry+row_h_*0.5f-3;
                if (exp) {
                    ri_.push_rect(ax,   ay,   6, 2, s.category_text_color, d++, clip);
                    ri_.push_rect(ax+1, ay+2, 4, 2, s.category_text_color, d++, clip);
                    ri_.push_rect(ax+2, ay+4, 2, 2, s.category_text_color, d++, clip);
                } else {
                    ri_.push_rect(ax,   ay,   2, 6, s.category_text_color, d++, clip);
                    ri_.push_rect(ax+2, ay+1, 2, 4, s.category_text_color, d++, clip);
                    ri_.push_rect(ax+4, ay+2, 2, 2, s.category_text_color, d++, clip);
                }
                ri_.push_text(visible[i].cat_name.c_str(), bx+16, ry, bw-16, row_h_,
                              s.category_text_color, 11.0f, Alignment::CenterLeft, d++, clip);
            } else {
                int idx = visible[i].prop_idx;
                bool is_sel = (props_[idx].id == selected_);
                math::Vec4 row_bg = is_sel ? s.selected_background : s.row_background;
                ri_.push_rect(bx, ry, bw, row_h_, row_bg, d++, clip);
                // Row separator
                ri_.push_rect(bx, ry+row_h_-1, bw, 1, s.separator_color, d++, clip);
                // Name column
                math::Vec4 nc = props_[idx].read_only ? math::Vec4(s.name_text_color.x*0.6f,s.name_text_color.y*0.6f,s.name_text_color.z*0.6f,1.0f) : s.name_text_color;
                ri_.push_text(props_[idx].name.c_str(), bx+8, ry, name_col_w_-8, row_h_,
                              nc, 11.0f, Alignment::CenterLeft, d++, clip);
                // Value column
                float vx = bx + name_col_w_ + 4;
                float vw = bw - name_col_w_ - 8;
                if (props_[idx].type == PropertyType::Bool) {
                    float cbx = vx, cby = ry+row_h_*0.5f-5;
                    ri_.push_rect(cbx, cby, 10, 10, s.row_background, d++, clip);
                    ri_.push_outline(cbx, cby, 10, 10, s.name_text_color, d, clip);
                    if (props_[idx].bool_val)
                        ri_.push_rect(cbx+2, cby+2, 6, 6, s.value_text_color, d++, clip);
                } else if (props_[idx].type == PropertyType::Color) {
                    ri_.push_rect(vx, ry+3, 16, row_h_-6, props_[idx].vec4_val, d++, clip);
                    const char* vs = format_value(idx);
                    ri_.push_text(vs, vx+20, ry, vw-20, row_h_,
                                  s.value_text_color, 11.0f, Alignment::CenterLeft, d++, clip);
                } else if (props_[idx].type == PropertyType::Enum) {
                    const char* vs = format_value(idx);
                    math::Vec4 vc = props_[idx].read_only
                        ? math::Vec4(s.value_text_color.x*0.6f,s.value_text_color.y*0.6f,s.value_text_color.z*0.6f,1.0f)
                        : s.value_text_color;
                    bool pop_open = (enum_popup_id_ == props_[idx].id);
                    // Highlight cell when popup is open or row is selected
                    if ((is_sel || pop_open) && !props_[idx].read_only)
                        ri_.push_rect(vx-2, ry+1, vw+2, row_h_-2, math::Vec4(0.15f,0.15f,0.22f,1.0f), d++, clip);
                    // Value text (leave room for the dropdown chevron on right)
                    const float chev_w = 16.0f;
                    ri_.push_text(vs, vx+2, ry, vw - chev_w - 4, row_h_,
                                  vc, 11.0f, Alignment::CenterLeft, d++, clip);
                    // Dropdown chevron (▼)
                    if (!props_[idx].read_only) {
                        float cx = bx + bw - chev_w;
                        ri_.push_text("v", cx, ry, chev_w, row_h_, vc, 9.0f, Alignment::Center, d++, clip);
                    }
                } else {
                    bool editing = (editing_id_ == props_[idx].id);
                    if (editing) {
                        ri_.push_rect(vx-2, ry+1, vw+2, row_h_-2, math::Vec4(0.1f,0.1f,0.12f,1.0f), d++, clip);
                        WidgetRenderInfo::TextCmd tc;
                        tc.text = edit_buf_;
                        tc.dest = math::make_box(vx, ry, vw, row_h_);
                        tc.color = s.value_text_color;
                        tc.font_size = 11.0f;
                        tc.alignment = Alignment::CenterLeft;
                        tc.depth = d++;
                        tc.clip = clip;
                        tc.show_cursor = true;
                        tc.cursor_pos = edit_cursor_;
                        tc.cursor_color = s.value_text_color;
                        ri_.texts.push_back(tc);
                    } else {
                        const char* vs = format_value(idx);
                        math::Vec4 vc = props_[idx].read_only ? math::Vec4(s.value_text_color.x*0.6f,s.value_text_color.y*0.6f,s.value_text_color.z*0.6f,1.0f) : s.value_text_color;
                        ri_.push_text(vs, vx, ry, vw, row_h_,
                                      vc, 11.0f, Alignment::CenterLeft, d++, clip);
                    }
                }
            }
        }
        // Embedded scrollbar
        if (content_h > bh) {
            const float sb_w = 10.0f;
            float sb_x = bx + bw - sb_w - 1;
            ri_.push_rect(sb_x, by, sb_w, bh, math::Vec4(0.12f,0.12f,0.13f,0.6f), d++, noclip);
            float thumb_h = std::max(16.0f, bh * bh / content_h);
            float track_range = bh - thumb_h;
            float max_scroll = content_h - bh;
            float pos_ratio = (max_scroll > 0) ? scroll_y_ / max_scroll : 0.0f;
            ri_.push_rect(sb_x, by + track_range*pos_ratio, sb_w, thumb_h,
                          math::Vec4(0.4f,0.4f,0.42f,0.7f), d++, noclip);
        }
        ri_.push_outline(bx, by, bw, bh, math::Vec4(0.25f,0.25f,0.27f,1.0f), d, noclip);

        // Enum dropdown popup overlay
        if (enum_popup_id_ >= 0) {
            int popup_row = -1;
            for (int i = 0; i < (int)visible.size(); ++i) {
                if (!visible[i].is_cat && props_[visible[i].prop_idx].id == enum_popup_id_) {
                    popup_row = i; break;
                }
            }
            if (popup_row >= 0) {
                auto& pop_p = props_[visible[popup_row].prop_idx];
                int num_opts = (int)pop_p.enum_opts.size();
                float pop_y_top = by + (popup_row + 1) * row_h_ - scroll_y_;
                float pop_x = bx + name_col_w_;
                float pop_w = bw - name_col_w_;
                float pop_h = num_opts * row_h_;
                // Keep popup inside widget vertically
                if (pop_y_top + pop_h > by + bh)
                    pop_y_top = by + (popup_row) * row_h_ - scroll_y_ - pop_h;
                int32_t pd = 2000; // very high depth — on top of everything
                auto noclip2 = math::make_box(0,0,0,0);
                ri_.push_rect(pop_x, pop_y_top, pop_w, pop_h, math::Vec4(0.18f,0.18f,0.2f,1.0f), pd++, noclip2);
                ri_.push_outline(pop_x, pop_y_top, pop_w, pop_h, math::Vec4(0.4f,0.4f,0.5f,1.0f), pd, noclip2);
                for (int oi = 0; oi < num_opts; ++oi) {
                    float oy = pop_y_top + oi * row_h_;
                    bool cur = (oi == pop_p.enum_idx);
                    if (cur)
                        ri_.push_rect(pop_x+1, oy+1, pop_w-2, row_h_-2, math::Vec4(0.25f,0.4f,0.7f,0.5f), pd++, noclip2);
                    ri_.push_text(pop_p.enum_opts[oi].c_str(), pop_x+6, oy, pop_w-10, row_h_,
                                  s.value_text_color, 11.0f, Alignment::CenterLeft, pd++, noclip2);
                    if (oi > 0)
                        ri_.push_rect(pop_x, oy, pop_w, 1, math::Vec4(0.3f,0.3f,0.33f,0.5f), pd++, noclip2);
                }
            }
        }

        ri_.finalize(); base_.clear_dirty(); return ri_;
    }
};
const std::vector<std::string> GuiPropertyGrid::empty_opts_;

// Factory function
IGuiPropertyGrid* create_property_grid_widget() { return new GuiPropertyGrid(); }

} // namespace gui
} // namespace window
