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
    bool sb_drag_=false;
    mutable WidgetRenderInfo ri_;
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
            float rel_x = math::x(p) - bx;
            float rel_y = math::y(p) - math::y(math::box_min(b)) + scroll_y_;
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
                        tc.cursor_pos = (int)edit_buf_.size();
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
        ri_.finalize(); base_.clear_dirty(); return ri_;
    }
};
const std::vector<std::string> GuiPropertyGrid::empty_opts_;

// Factory function
IGuiPropertyGrid* create_property_grid_widget() { return new GuiPropertyGrid(); }

} // namespace gui
} // namespace window
