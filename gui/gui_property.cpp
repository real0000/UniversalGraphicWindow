/*
 * gui_property.cpp - PropertyGrid Implementation
 */

#include "gui_widget_base.hpp"

namespace window { namespace gui { IGuiTextInput* create_text_input_widget(); } }
#include <unordered_map>

namespace window {
namespace gui {

class GuiPropertyGrid : public WidgetBase<IGuiPropertyGrid, WidgetType::Custom> {
    struct Prop {
        int id=-1; std::string key, name, category, str_val;
        PropertyType type=PropertyType::String;
        int int_val=0; float float_val=0; bool bool_val=false;
        math::Vec2 vec2_val; math::Vec4 vec4_val;
        std::vector<std::string> enum_opts, enum_values; int enum_idx=0;
        float range_min=0, range_max=1;
        bool read_only=false;
        std::vector<PropertyAction> actions;   // right-aligned row buttons
    };
    // Canonical value the app should store for a property (Bool→"true"/"false",
    // Enum→selected value (or label), else the text). Backs get_property_value.
    mutable std::string val_str_;
    const char* canonical_value(const Prop& p) const {
        if (p.type == PropertyType::Bool) { val_str_ = p.bool_val ? "true" : "false"; return val_str_.c_str(); }
        if (p.type == PropertyType::Enum) {
            int i = p.enum_idx;
            if (i >= 0 && i < (int)p.enum_values.size()) { val_str_ = p.enum_values[i]; return val_str_.c_str(); }
            if (i >= 0 && i < (int)p.enum_opts.size())   { val_str_ = p.enum_opts[i];   return val_str_.c_str(); }
            val_str_.clear(); return val_str_.c_str();
        }
        val_str_ = p.str_val; return val_str_.c_str();
    }
    // Right-aligned action-button rects for a value row (right→left), paired with id.
    void row_action_rects(const Prop& p, float bx, float bw, float ry, float rh,
                          std::vector<std::pair<math::Box,int>>& out) const {
        const float aw = rh - 6.0f;
        float ax = bx + bw - 4.0f - (style_.stacked ? style_.side_padding - 4.0f : 0.0f);
        for (int k = (int)p.actions.size() - 1; k >= 0; --k) {
            ax -= aw + 3.0f;
            out.push_back({ math::make_box(ax, ry + 3.0f, aw, rh - 6.0f), p.actions[k].id });
        }
    }
    // The action strip rides the header line (Category rows) or the value box
    // (stacked field rows): resolve the strip's y/height for a row.
    void action_strip(const Prop& pr, float ry, float& ay, float& ah) const {
        ay = ry; ah = row_h_;
        if (style_.stacked && pr.type != PropertyType::Category) {
            ay = ry + style_.label_height; ah = style_.field_height;
        }
    }
    std::vector<Prop> props_;
    int next_id_=0, selected_=-1;
    std::unordered_map<std::string,bool> cat_expanded_;
    float name_col_w_=150, row_h_=24, scroll_y_=0;
    PropertyGridStyle style_=PropertyGridStyle::default_style();
    IPropertyGridEventHandler* handler_=nullptr;
    std::function<std::vector<PropertyModel>()> provider_;   // bound model source (set once)
    std::function<PropertyForm()> form_provider_;            // bound form source (set once)
    // Array-section CRUD routing: generated section/card header rows (negative ids)
    // map to (array_id, element index; -1 = section header). Action button ids encode
    // the operation (PA_ADD/UP/DN/DEL).
    enum { PA_ADD=1, PA_UP, PA_DN, PA_DEL };
    struct ArrayRoute { std::string array_key; int index=-1; };   // index=-1 = section header
    std::unordered_map<int, ArrayRoute> array_routes_;
    static const std::vector<std::string> empty_opts_;
    int editing_id_=-1;
    std::string edit_buf_;
    int edit_cursor_=0;
    int enum_popup_id_=-1;
    bool sb_drag_=false;
    // Embedded field editor: a REAL single-line GuiTextInput child (mouse caret,
    // drag-select, CJK, IME preedit, blink) positioned over the edited field.
    // Enter commits (on_text_commit), Escape cancels; the grid owns the value.
    IGuiTextInput* editor_ = nullptr;
    ITextMeasurer* measurer_ = nullptr;
    struct EditorH : ITextInputEventHandler {
        GuiPropertyGrid* g = nullptr;
        void on_text_commit(const char* t) override;
        void on_text_cancel() override;
    } editor_h_;
    mutable WidgetRenderInfo ri_;
    int find_idx(int id) const { for(int i=0;i<(int)props_.size();++i) if(props_[i].id==id) return i; return -1; }

    // The edited field's box (stacked: the value box under the label; table: the
    // value cell). Empty when the row isn't visible / editable.
    math::Box field_rect_for(int prop_id) const {
        std::vector<VisRow> vis; collect_visible(vis);
        auto b = base_.get_bounds();
        const float bx = math::x(math::box_min(b)), by = math::y(math::box_min(b));
        const float bw = math::box_width(b);
        for (int i = 0; i < (int)vis.size(); ++i) {
            if (vis[i].is_cat || props_[vis[i].prop_idx].id != prop_id) continue;
            const float ry = by + row_offset(vis, i) - scroll_y_;
            if (style_.stacked)
                return math::make_box(bx + style_.side_padding, ry + style_.label_height,
                                      bw - style_.side_padding * 2, style_.field_height);
            return math::make_box(bx + name_col_w_ + 2, ry + 1, bw - name_col_w_ - 4, row_h_ - 2);
        }
        return math::make_box(0, 0, 0, 0);
    }
    void ensure_editor() {
        if (editor_) return;
        editor_ = create_text_input_widget();
        if (!editor_) return;
        editor_h_.g = this;
        editor_->set_text_input_event_handler(&editor_h_);
        editor_->set_visible(false);
        if (measurer_) editor_->set_text_measurer(measurer_);
        base_.add_child(editor_);
    }
    void start_editing(int prop_id) {
        int i = find_idx(prop_id);
        if (i < 0 || props_[i].read_only) return;
        auto& p = props_[i];
        if (p.type == PropertyType::Bool || p.type == PropertyType::Category ||
            p.type == PropertyType::Enum) return;
        ensure_editor();
        const math::Box fb = field_rect_for(prop_id);
        if (!editor_ || math::box_is_empty(fb)) return;
        editing_id_ = prop_id;
        edit_buf_ = format_value(i);
        edit_cursor_ = (int)edit_buf_.size();
        TextInputStyle ts = TextInputStyle::default_style();
        ts.background_color   = style_.stacked ? style_.field_background : math::Vec4(0.1f, 0.1f, 0.12f, 1.0f);
        ts.border_color       = math::Vec4(0, 0, 0, 0);
        ts.focus_border_color = math::Vec4(0, 0, 0, 0);
        ts.text_color         = style_.value_text_color;
        ts.selection_color    = math::Vec4(0.21f, 0.31f, 0.51f, 1.0f);
        ts.cursor_color       = style_.value_text_color;
        ts.font_size          = style_.stacked ? style_.font_size : 11.0f;
        ts.corner_radius      = style_.stacked ? style_.field_corner_radius : 0.0f;
        ts.padding            = 6.0f;
        editor_->set_text_input_style(ts);
        editor_->set_text(edit_buf_.c_str());
        editor_->set_cursor_position((int)edit_buf_.size());
        editor_->set_bounds(fb);
        editor_->set_visible(true);
        editor_->set_focus(true);
        base_.mark_dirty();
    }
    void editor_commit(const char* text) {
        int i = find_idx(editing_id_);
        if (i >= 0) {
            Prop& p = props_[i];
            const std::string t = text ? text : "";
            if (p.type == PropertyType::Int)        p.int_val   = std::atoi(t.c_str());
            else if (p.type == PropertyType::Float) p.float_val = (float)std::atof(t.c_str());
            else if (p.type == PropertyType::Range) {
                float v = (float)std::atof(t.c_str());
                if (v < p.range_min) v = p.range_min;
                if (v > p.range_max) v = p.range_max;
                p.float_val = v;
            }
            p.str_val = t;
            if (handler_) handler_->on_property_changed(p.id);
        }
        editor_dismiss();
    }
    void editor_dismiss() {
        editing_id_ = -1; edit_cursor_ = 0;
        if (editor_) { editor_->set_visible(false); editor_->set_focus(false); }
        base_.mark_dirty();
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

    void cancel_edit() { enum_popup_id_ = -1; editor_dismiss(); }

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
            // An empty category name emits no header row — the props render flat, and a
            // structured editor supplies its own header rows (Category-type props with
            // actions). Named categories keep the collapsible header + expand state.
            bool expanded = true;
            if (!cat.empty()) {
                out.push_back({true, cat, -1});
                auto it = cat_expanded_.find(cat);
                if (it != cat_expanded_.end()) expanded = it->second;
            }
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

    // ── row geometry — the ONE place that knows how tall each visible row is.
    // Table mode: every row is row_h_. Stacked mode: label line + value box + gap
    // (headers keep row_h_; Bool rows are a single checkbox line).
    float row_extent(const VisRow& v) const {
        if (!style_.stacked) return row_h_;
        if (v.is_cat) return row_h_;
        const Prop& pr = props_[v.prop_idx];
        if (pr.type == PropertyType::Category) return row_h_ + style_.row_gap * 0.5f;
        if (pr.type == PropertyType::Bool)     return style_.field_height * 0.85f + style_.row_gap;
        return style_.label_height + style_.field_height + style_.row_gap;
    }
    float row_offset(const std::vector<VisRow>& vis, int row) const {
        float y = 0; for (int i = 0; i < row && i < (int)vis.size(); ++i) y += row_extent(vis[i]);
        return y;
    }
    int row_at(const std::vector<VisRow>& vis, float rel_y) const {
        float y = 0;
        for (int i = 0; i < (int)vis.size(); ++i) { y += row_extent(vis[i]); if (rel_y < y) return i; }
        return -1;
    }
    float content_height(const std::vector<VisRow>& vis) const {
        float y = 0; for (const auto& v : vis) y += row_extent(v); return y;
    }
    void clamp_scroll() {
        std::vector<VisRow> visible;
        collect_visible(visible);
        float content_h = content_height(visible);
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
        std::vector<VisRow> visible;
        collect_visible(visible);
        float view_h = math::box_height(base_.get_bounds());
        float row_top = row_offset(visible, row);
        float row_bot = row_top + (row < (int)visible.size() ? row_extent(visible[row]) : row_h_);
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
        if (editor_ && editor_->is_visible()) editor_commit(editor_->get_text());
        scroll_y_ -= dy * row_h_ * 3;
        clamp_scroll();
        base_.mark_dirty();
        return true;
    }
    bool handle_mouse_move(const math::Vec2& p) override {
        if (editor_ && editor_->is_visible() && editor_->handle_mouse_move(p)) return true;
        if (sb_drag_) {
            float content_h = get_total_content_height();
            set_scroll_offset(scrollbar_offset_from_mouse(base_.get_bounds(), content_h, math::y(p)));
            return true;
        }
        return base_.handle_mouse_move(p);
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        // The open field editor owns clicks inside itself (caret / drag-select);
        // a left press anywhere else commits the pending edit first (blur-commit).
        if (editor_ && editor_->is_visible()) {
            if (editor_->handle_mouse_button(btn, pressed, p)) return true;
            if (btn == MouseButton::Left && pressed) editor_commit(editor_->get_text());
        }
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

            std::vector<VisRow> visible;
            collect_visible(visible);
            int row = row_at(visible, rel_y);

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
                    const float opt_h = style_.stacked ? style_.field_height : row_h_;
                    float pop_y = by_top + row_offset(visible, popup_row) + row_extent(visible[popup_row])
                                  - (style_.stacked ? style_.row_gap : 0.0f) - scroll_y_;
                    float pop_x = style_.stacked ? bx + style_.side_padding : bx + name_col_w_;
                    float pop_w = style_.stacked ? bw - style_.side_padding * 2 : bw - name_col_w_;
                    float pop_h = num_opts * opt_h;
                    float px = math::x(p), py = math::y(p);
                    if (px >= pop_x && px < pop_x + pop_w && py >= pop_y && py < pop_y + pop_h) {
                        int opt = (int)((py - pop_y) / opt_h);
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
                    // Row action buttons (× delete / + add / ↑ ↓ reorder) take the click first.
                    if (!prop.actions.empty()) {
                        std::vector<std::pair<math::Box,int>> arects;
                        float ry = by_top + row_offset(visible, row) - scroll_y_;
                        float ay, ah; action_strip(prop, ry, ay, ah);
                        row_action_rects(prop, bx, bw, ay, ah, arects);
                        for (auto& ar : arects)
                            if (math::box_contains(ar.first, p)) {
                                // A generated array-section header routes to on_property_array_*;
                                // any other action row is the app's own → on_property_action.
                                if (!route_array_action(prop.id, ar.second))
                                    if (handler_) handler_->on_property_action(prop.id, ar.second);
                                return true;
                            }
                    }
                    selected_ = prop.id;

                    // Stacked: the whole row is the value zone; table: right of the name column.
                    const bool in_value = style_.stacked || rel_x > name_col_w_;
                    if (prop.type == PropertyType::Bool && !prop.read_only) {
                        prop.bool_val = !prop.bool_val;
                        if (handler_) handler_->on_property_changed(prop.id);
                        cancel_edit();
                    } else if (prop.type == PropertyType::Enum && in_value && !prop.read_only) {
                        // Open dropdown popup
                        cancel_edit();
                        enum_popup_id_ = prop.id;
                    } else if (in_value && !prop.read_only) {
                        start_editing(prop.id);
                    } else if (editing_id_ != prop.id) {
                        cancel_edit();
                    }
                }
            } else {
                cancel_edit();
            }
            base_.mark_dirty();   // selection/popup/editing all changed something visual
        }
        base_.handle_mouse_button(btn, pressed, p);
        return true;   // in-bounds click belongs to the grid (entry is hit-guarded)
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
            // The embedded editor is focused and handles the typing itself; only
            // route a stray Enter/Escape that reached the grid instead.
            if (code == K_Enter)  { editor_commit(editor_ ? editor_->get_text() : edit_buf_.c_str()); return true; }
            if (code == K_Escape) { cancel_edit(); return true; }
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
    // Copy a model row's value into a Prop's typed field (svalue is text for
    // String/Int/Float/Range; bvalue is Bool; options+enum_index are Enum).
    static void apply_value(Prop& p, const PropertyModel& m) {
        switch (m.type) {
            case PropertyType::Int:   p.int_val   = std::atoi(m.svalue.c_str()); p.str_val=m.svalue; break;
            case PropertyType::Float:
            case PropertyType::Range: p.float_val = (float)std::atof(m.svalue.c_str()); p.str_val=m.svalue; break;
            case PropertyType::Bool:  p.bool_val  = m.bvalue; break;
            case PropertyType::Enum:  p.enum_opts = m.options; p.enum_values = m.option_values; p.enum_idx = m.enum_index; break;
            default:                  p.str_val   = m.svalue; break;
        }
    }
    static bool value_equal(const Prop& p, const PropertyModel& m) {
        switch (m.type) {
            case PropertyType::Int:   return p.int_val   == std::atoi(m.svalue.c_str());
            case PropertyType::Float:
            case PropertyType::Range: return p.float_val == (float)std::atof(m.svalue.c_str());
            case PropertyType::Bool:  return p.bool_val  == m.bvalue;
            case PropertyType::Enum:  return p.enum_idx  == m.enum_index && p.enum_opts == m.options;
            default:                  return p.str_val   == m.svalue;
        }
    }
    // Idempotent whole-form rebind. Structure unchanged (same ids/names/categories/
    // types/read-only/enum options) → only values update in place (scroll + an active
    // inline edit are kept); structural change → rebuild. Unchanged → no repaint.
    void set_properties(const std::vector<PropertyModel>& model) override {
        auto actions_same = [](const std::vector<PropertyAction>& a, const std::vector<PropertyAction>& b) {
            if (a.size()!=b.size()) return false;
            for (size_t k=0;k<a.size();++k) if (a[k].id!=b[k].id || a[k].label!=b[k].label) return false;
            return true;
        };
        bool struct_same = model.size()==props_.size();
        if (struct_same) for (size_t i=0;i<model.size();++i) {
            const auto& m=model[i]; const auto& p=props_[i];
            // id==-1 = "app didn't assign one" → the grid gives it a stable position id
            // (below), so don't treat that default as a structure change.
            if ((m.id!=-1 && m.id!=p.id) || m.key!=p.key || m.name!=p.name || m.category!=p.category || m.type!=p.type ||
                m.read_only!=p.read_only || !actions_same(m.actions, p.actions) ||
                (m.type==PropertyType::Enum && (m.options!=p.enum_opts || m.option_values!=p.enum_values))) { struct_same=false; break; }
        }
        if (struct_same) {
            if (editing_id_ >= 0) return;                 // don't disturb an in-progress edit
            bool changed = false;
            for (size_t i=0;i<model.size();++i)
                if (!value_equal(props_[i], model[i])) { apply_value(props_[i], model[i]); changed = true; }
            if (changed) base_.mark_dirty();
            return;
        }
        cancel_edit();
        props_.clear(); props_.reserve(model.size());
        for (const auto& m : model) {
            // App props default id to -1 (they route by key); give each a UNIQUE stable id
            // = its position, so find_idx / on_property_changed don't collide. Generated
            // array-header ids (<= -1000000, in array_routes_) and explicit ids are kept.
            Prop p; p.id=(m.id==-1)?(int)props_.size():m.id; p.key=m.key; p.name=m.name; p.category=m.category; p.type=m.type; p.read_only=m.read_only;
            p.actions=m.actions;
            apply_value(p, m);
            props_.push_back(std::move(p));
            if (m.id>=next_id_) next_id_=m.id+1;
        }
        if (selected_ >= 0 && find_idx(selected_) < 0) selected_ = -1;
        set_scroll_offset(scroll_y_);
        base_.mark_dirty();
    }
    void bind(std::function<std::vector<PropertyModel>()> provider) override {
        provider_ = std::move(provider); base_.mark_dirty();
    }
    // Expand a form (scalars + array sections) into flat rows, generating a section
    // header ("+") and per-element "#N" card header (↑ ↓ ×) via the action-row
    // mechanism, and record the header→(array,index) routing. Reuses set_properties so
    // an unchanged form still diffs to no repaint (generated ids are deterministic).
    void set_form(const PropertyForm& form) override {
        array_routes_.clear();
        std::vector<PropertyModel> flat;
        flat.reserve(form.props.size() + form.arrays.size()*4);
        for (const auto& p : form.props) flat.push_back(p);
        int gen = -1000000;
        const math::Vec4 addc(0.24f,0.47f,0.78f,1.0f), mvc(0.21f,0.22f,0.25f,1.0f), delc(0.43f,0.19f,0.19f,1.0f);
        for (const auto& arr : form.arrays) {
            PropertyModel h; h.id = gen--; h.type = PropertyType::Category; h.read_only = true; h.name = arr.title;
            if (arr.can_add) h.actions.push_back({PA_ADD, "+", addc});
            array_routes_[h.id] = {arr.key, -1};
            flat.push_back(std::move(h));
            for (int i = 0; i < (int)arr.elements.size(); ++i) {
                const PropertyArrayElement& el = arr.elements[i];
                PropertyModel ch; ch.id = gen--; ch.type = PropertyType::Category; ch.read_only = true;
                ch.name = el.title.empty() ? ("#" + std::to_string(i+1)) : el.title;
                if (arr.can_move && el.movable) { ch.actions.push_back({PA_UP, "^", mvc}); ch.actions.push_back({PA_DN, "v", mvc}); }
                if (arr.can_remove && el.removable) ch.actions.push_back({PA_DEL, "x", delc});
                array_routes_[ch.id] = {arr.key, i};
                flat.push_back(std::move(ch));
                for (const auto& f : el.fields) flat.push_back(f);
            }
        }
        set_properties(flat);
    }
    void bind_form(std::function<PropertyForm()> provider) override {
        form_provider_ = std::move(provider); base_.mark_dirty();
    }
    void refresh_bindings() override {
        if (form_provider_) set_form(form_provider_());
        else if (provider_) set_properties(provider_());
    }
    // Translate an action-row click on a generated header into an array-CRUD callback.
    // Returns true if the id was a generated array header (consumed).
    bool route_array_action(int prop_id, int action_id) {
        auto it = array_routes_.find(prop_id);
        if (it == array_routes_.end()) return false;
        const ArrayRoute rt = it->second;
        if (handler_) {
            const char* k = rt.array_key.c_str();
            if (action_id == PA_ADD)      handler_->on_property_array_add(k);
            else if (action_id == PA_UP)  handler_->on_property_array_move(k, rt.index, -1);
            else if (action_id == PA_DN)  handler_->on_property_array_move(k, rt.index, +1);
            else if (action_id == PA_DEL) handler_->on_property_array_remove(k, rt.index);
        }
        return true;
    }
    bool remove_property(int id) override { int i=find_idx(id); if(i<0)return false; props_.erase(props_.begin()+i); return true; }
    void clear_properties() override { props_.clear(); selected_=-1; }
    int get_property_count() const override { return (int)props_.size(); }
    const char* get_property_name(int id) const override { int i=find_idx(id); return i>=0?props_[i].name.c_str():""; }
    const char* get_property_category(int id) const override { int i=find_idx(id); return i>=0?props_[i].category.c_str():""; }
    PropertyType get_property_type(int id) const override { int i=find_idx(id); return i>=0?props_[i].type:PropertyType::String; }
    const char* get_property_key(int id) const override { int i=find_idx(id); return i>=0?props_[i].key.c_str():""; }
    const char* get_property_value(int id) const override { int i=find_idx(id); return i>=0?canonical_value(props_[i]):""; }
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
        return content_height(visible);
    }
    float get_name_column_width() const override { return name_col_w_; }
    void set_name_column_width(float w) override { name_col_w_=w; }
    float get_row_height() const override { return row_h_; }
    void set_row_height(float h) override { row_h_=h; }
    const PropertyGridStyle& get_property_grid_style() const override { return style_; }
    void set_property_grid_style(const PropertyGridStyle& s) override { style_=s; }
    void set_text_measurer(ITextMeasurer* m) override {
        measurer_ = m;
        if (editor_) editor_->set_text_measurer(m);
    }
    // The embedded field editor lives in base_'s child list; expose it so the
    // context's collect/focus traversal reaches it (wrapper default hides children).
    int get_child_count() const override { return base_.get_child_count(); }
    IGuiWidget* get_child(int i) const override { return base_.get_child(i); }
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

    // ── semantic driving (automation) — apply a change by KEY, fire the handler ──────
    int find_by_key(const char* key) const {
        if (key) for (const auto& p : props_) if (p.key == key) return p.id;
        return -1;
    }
    bool commit_text(const char* key, const char* text) override {
        int i = find_idx(find_by_key(key)); if (i < 0) return false;
        Prop& p = props_[i];
        const std::string t = text ? text : "";
        if (p.type == PropertyType::Int)        p.int_val   = std::atoi(t.c_str());
        else if (p.type == PropertyType::Float) p.float_val = (float)std::atof(t.c_str());
        p.str_val = t;                                    // canonical_value returns str_val for these
        if (handler_) handler_->on_property_changed(p.id);
        return true;
    }
    bool select_enum(const char* key, int opt) override {
        int i = find_idx(find_by_key(key)); if (i < 0) return false;
        Prop& p = props_[i];
        if (p.type != PropertyType::Enum || opt < 0 || opt >= (int)p.enum_opts.size()) return false;
        p.enum_idx = opt;
        if (handler_) handler_->on_property_changed(p.id);
        return true;
    }
    bool toggle_bool(const char* key) override {
        int i = find_idx(find_by_key(key)); if (i < 0) return false;
        Prop& p = props_[i];
        if (p.type != PropertyType::Bool) return false;
        p.bool_val = !p.bool_val;
        if (handler_) handler_->on_property_changed(p.id);
        return true;
    }
    bool invoke_array_add(const char* array_key) override {
        if (!handler_ || !array_key) return false; handler_->on_property_array_add(array_key); return true;
    }
    bool invoke_array_remove(const char* array_key, int index) override {
        if (!handler_ || !array_key) return false; handler_->on_property_array_remove(array_key, index); return true;
    }
    bool invoke_array_move(const char* array_key, int index, int delta) override {
        if (!handler_ || !array_key) return false; handler_->on_property_array_move(array_key, index, delta); return true;
    }
    int get_enum_options(const char* key, const char** out_values, const char** out_labels, int max) const override {
        int i = find_idx(find_by_key(key)); if (i < 0) return 0;
        const Prop& p = props_[i];
        if (p.type != PropertyType::Enum) return 0;
        int n = std::min(max, (int)p.enum_opts.size());
        for (int k = 0; k < n; ++k) {
            if (out_labels) out_labels[k] = p.enum_opts[k].c_str();
            if (out_values) out_values[k] = (k < (int)p.enum_values.size()) ? p.enum_values[k].c_str() : p.enum_opts[k].c_str();
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
        // Column divider (table layout only — stacked has no name column)
        if (!s.stacked)
            ri_.push_rect(bx+name_col_w_, by, 1, bh, s.separator_color, d++, noclip);

        std::vector<VisRow> visible;
        collect_visible(visible);
        float content_h = content_height(visible);

        float run_y = 0.0f;
        for (int i = 0; i < (int)visible.size(); i++) {
            const float ext = row_extent(visible[i]);
            float ry = by + run_y - scroll_y_;
            run_y += ext;
            if (ry + ext < by || ry > by + bh) continue;
            if (s.stacked && !visible[i].is_cat) {   // ── stacked form row ──
                int idx = visible[i].prop_idx;
                const Prop& pr = props_[idx];
                const float sx = bx + s.side_padding, sw = bw - s.side_padding * 2;
                if (pr.type == PropertyType::Category) {
                    // Inline section header: small-caps grey + action buttons.
                    ri_.push_text(pr.name.c_str(), sx, ry, sw, row_h_,
                                  s.category_text_color, s.label_font, Alignment::CenterLeft, d++, clip);
                    std::vector<std::pair<math::Box,int>> arects;
                    row_action_rects(pr, bx, bw, ry, row_h_, arects);
                    for (size_t k=0;k<arects.size();++k) {
                        const auto& act = pr.actions[pr.actions.size()-1-k];
                        math::Vec4 fill = act.color.w>0.0f ? act.color : math::Vec4(0.33f,0.35f,0.40f,1.0f);
                        const auto& bb = arects[k].first;
                        ri_.push_round_rect(math::x(math::box_min(bb)), math::y(math::box_min(bb)),
                                            math::box_width(bb), math::box_height(bb), 3.0f, fill, d++, clip);
                        ri_.push_text(act.label.c_str(), math::x(math::box_min(bb)), math::y(math::box_min(bb))-1,
                                      math::box_width(bb), math::box_height(bb),
                                      math::Vec4(0.93f,0.93f,0.95f,1.0f), s.label_font, Alignment::Center, d++, clip);
                    }
                    continue;
                }
                if (pr.type == PropertyType::Bool) {
                    // Single line: [checkbox] label
                    const float ch = 13.0f, cy = ry + (ext - s.row_gap - ch) * 0.5f;
                    if (pr.bool_val) {
                        ri_.push_round_rect(sx, cy, ch, ch, 3.0f, math::Vec4(0.23f,0.38f,0.66f,1.0f), d++, clip);
                        ri_.push_text("\xC3\x97", sx, cy - 1.0f, ch, ch,
                                      math::Vec4(0.95f,0.96f,0.98f,1.0f), s.label_font, Alignment::Center, d++, clip);
                    } else {
                        ri_.push_round_rect(sx, cy, ch, ch, 3.0f, s.field_background, d++, clip);
                        if (s.field_border_color.w > 0.0f)
                            ri_.push_outline(sx, cy, ch, ch, s.field_border_color, d, clip);
                    }
                    ri_.push_text(pr.name.c_str(), sx + ch + 8.0f, ry, sw - ch - 8.0f, ext - s.row_gap,
                                  s.value_text_color, s.font_size, Alignment::CenterLeft, d++, clip);
                    continue;
                }
                // Label line (skip when unnamed) …
                if (!pr.name.empty())
                    ri_.push_text(pr.name.c_str(), sx, ry, sw, s.label_height,
                                  s.name_text_color, s.label_font, Alignment::CenterLeft, d++, clip);
                const float fy = ry + s.label_height, fh = s.field_height;
                float act_w = 0.0f;
                for (const auto& a : pr.actions) { (void)a; act_w += (fh-6.0f) + 3.0f; }
                if (pr.read_only && pr.type == PropertyType::String) {
                    // read-only text (subtitle/info): plain value, no box
                    ri_.push_text(format_value(idx), sx, fy, sw - act_w, fh,
                                  s.value_text_color, s.font_size, Alignment::CenterLeft, d++, clip);
                } else {
                    // … then the boxed value field.
                    ri_.push_round_rect(sx, fy, sw, fh, s.field_corner_radius, s.field_background, d++, clip);
                    if (s.field_border_color.w > 0.0f)
                        ri_.push_outline(sx, fy, sw, fh, s.field_border_color, d, clip);
                    const float tx = sx + 8.0f;
                    float tw = sw - 16.0f - act_w;
                    bool editing = (editing_id_ == pr.id && editor_ && editor_->is_visible());
                    if (editing) {
                        // the embedded editor child renders the text/caret/selection
                    } else {
                        if (pr.type == PropertyType::Enum) tw -= 14.0f;   // room for the chevron
                        math::Vec4 vc = pr.read_only
                            ? math::Vec4(s.value_text_color.x*0.6f,s.value_text_color.y*0.6f,s.value_text_color.z*0.6f,1.0f)
                            : s.value_text_color;
                        ri_.push_text(format_value(idx), tx, fy, tw, fh,
                                      vc, s.font_size, Alignment::CenterLeft, d++, clip);
                        if (pr.type == PropertyType::Enum && !pr.read_only)
                            ri_.push_text("v", sx + sw - 16.0f, fy, 14.0f, fh,
                                          s.name_text_color, 9.0f, Alignment::Center, d++, clip);
                    }
                }
                if (!pr.actions.empty()) {
                    std::vector<std::pair<math::Box,int>> arects;
                    row_action_rects(pr, bx, bw, fy, fh, arects);
                    for (size_t k=0;k<arects.size();++k) {
                        const auto& act = pr.actions[pr.actions.size()-1-k];
                        math::Vec4 fill = act.color.w>0.0f ? act.color : math::Vec4(0.33f,0.35f,0.40f,1.0f);
                        const auto& bb = arects[k].first;
                        ri_.push_round_rect(math::x(math::box_min(bb)), math::y(math::box_min(bb)),
                                            math::box_width(bb), math::box_height(bb), 3.0f, fill, d++, clip);
                        ri_.push_text(act.label.c_str(), math::x(math::box_min(bb)), math::y(math::box_min(bb))-1,
                                      math::box_width(bb), math::box_height(bb),
                                      math::Vec4(0.93f,0.93f,0.95f,1.0f), s.label_font, Alignment::Center, d++, clip);
                    }
                }
                continue;
            }
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
            } else if (props_[visible[i].prop_idx].type == PropertyType::Category) {
                // A Category-type PROP renders as an inline header (name only, no value
                // box) that carries action buttons — a section ("Cases  [+]") or card
                // ("#1  ↑ ↓ ×") header inside the form.
                int idx = visible[i].prop_idx;
                ri_.push_rect(bx, ry, bw, row_h_, s.category_background, d++, clip);
                ri_.push_text(props_[idx].name.c_str(), bx+8, ry, bw-16, row_h_,
                              s.category_text_color, 11.0f, Alignment::CenterLeft, d++, clip);
                std::vector<std::pair<math::Box,int>> arects;
                row_action_rects(props_[idx], bx, bw, ry, row_h_, arects);
                for (size_t k=0;k<arects.size();++k) {
                    const auto& act = props_[idx].actions[props_[idx].actions.size()-1-k];
                    math::Vec4 fill = act.color.w>0.0f ? act.color : math::Vec4(0.33f,0.35f,0.40f,1.0f);
                    const auto& bb = arects[k].first;
                    ri_.push_rect(math::x(math::box_min(bb)), math::y(math::box_min(bb)),
                                  math::box_width(bb), math::box_height(bb), fill, d++, clip);
                    ri_.push_text(act.label.c_str(), math::x(math::box_min(bb)), math::y(math::box_min(bb))-1,
                                  math::box_width(bb), math::box_height(bb),
                                  math::Vec4(0.93f,0.93f,0.95f,1.0f), 11.0f, Alignment::Center, d++, clip);
                }
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
                // Value column (leave room for any trailing action buttons)
                float act_w = 0.0f;
                for (const auto& a : props_[idx].actions) { (void)a; act_w += (row_h_-6.0f) + 3.0f; }
                float vx = bx + name_col_w_ + 4;
                float vw = bw - name_col_w_ - 8 - act_w;
                // Trailing action buttons (× delete etc.) on a value row.
                if (!props_[idx].actions.empty()) {
                    std::vector<std::pair<math::Box,int>> arects;
                    row_action_rects(props_[idx], bx, bw, ry, row_h_, arects);
                    for (size_t k=0;k<arects.size();++k) {
                        const auto& act = props_[idx].actions[props_[idx].actions.size()-1-k];
                        math::Vec4 fill = act.color.w>0.0f ? act.color : math::Vec4(0.33f,0.35f,0.40f,1.0f);
                        const auto& bb = arects[k].first;
                        ri_.push_rect(math::x(math::box_min(bb)), math::y(math::box_min(bb)),
                                      math::box_width(bb), math::box_height(bb), fill, d++, clip);
                        ri_.push_text(act.label.c_str(), math::x(math::box_min(bb)), math::y(math::box_min(bb))-1,
                                      math::box_width(bb), math::box_height(bb),
                                      math::Vec4(0.93f,0.93f,0.95f,1.0f), 11.0f, Alignment::Center, d++, clip);
                    }
                }
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
                    bool editing = (editing_id_ == props_[idx].id && editor_ && editor_->is_visible());
                    if (editing) {
                        // the embedded editor child renders the text/caret/selection
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
                const float opt_h = s.stacked ? s.field_height : row_h_;
                float pop_y_top = by + row_offset(visible, popup_row) + row_extent(visible[popup_row])
                                  - (s.stacked ? s.row_gap : 0.0f) - scroll_y_;
                float pop_x = s.stacked ? bx + s.side_padding : bx + name_col_w_;
                float pop_w = s.stacked ? bw - s.side_padding * 2 : bw - name_col_w_;
                float pop_h = num_opts * opt_h;
                // Keep popup inside widget vertically
                if (pop_y_top + pop_h > by + bh)
                    pop_y_top = by + row_offset(visible, popup_row) - scroll_y_ - pop_h;
                int32_t pd = 2000; // very high depth — on top of everything
                auto noclip2 = math::make_box(0,0,0,0);
                ri_.push_rect(pop_x, pop_y_top, pop_w, pop_h, math::Vec4(0.18f,0.18f,0.2f,1.0f), pd++, noclip2);
                ri_.push_outline(pop_x, pop_y_top, pop_w, pop_h, math::Vec4(0.4f,0.4f,0.5f,1.0f), pd, noclip2);
                for (int oi = 0; oi < num_opts; ++oi) {
                    float oy = pop_y_top + oi * opt_h;
                    bool cur = (oi == pop_p.enum_idx);
                    if (cur)
                        ri_.push_rect(pop_x+1, oy+1, pop_w-2, opt_h-2, math::Vec4(0.25f,0.4f,0.7f,0.5f), pd++, noclip2);
                    ri_.push_text(pop_p.enum_opts[oi].c_str(), pop_x+6, oy, pop_w-10, opt_h,
                                  s.value_text_color, s.stacked ? s.font_size : 11.0f, Alignment::CenterLeft, pd++, noclip2);
                    if (oi > 0)
                        ri_.push_rect(pop_x, oy, pop_w, 1, math::Vec4(0.3f,0.3f,0.33f,0.5f), pd++, noclip2);
                }
            }
        }

        ri_.finalize(); base_.clear_dirty(); return ri_;
    }
};
const std::vector<std::string> GuiPropertyGrid::empty_opts_;
void GuiPropertyGrid::EditorH::on_text_commit(const char* t) { if (g) g->editor_commit(t); }
void GuiPropertyGrid::EditorH::on_text_cancel() { if (g) g->editor_dismiss(); }

// Factory function
IGuiPropertyGrid* create_property_grid_widget() { return new GuiPropertyGrid(); }

} // namespace gui
} // namespace window
