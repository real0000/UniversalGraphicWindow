/*
 * gui_tree.cpp - TreeView Implementation
 */

#include "gui_widget_base.hpp"

namespace window {
namespace gui {

class GuiTreeView : public WidgetBase<IGuiTreeView, WidgetType::TreeView> {
    struct Node : WidgetItem { int parent_id=-1; std::vector<int> children; bool expanded=true; };
    std::vector<Node> nodes_;
    int next_id_=0, selected_=-1;
    TreeViewSelectionMode sel_mode_=TreeViewSelectionMode::Single;
    std::vector<int> multi_sel_;
    bool drag_reorder_=false;
    float scroll_y_=0;
    bool sb_drag_=false;
    TreeViewStyle style_=TreeViewStyle::default_style();
    ITreeViewEventHandler* handler_=nullptr;
    mutable WidgetRenderInfo ri_;
    int find_idx(int id) const { for(int i=0;i<(int)nodes_.size();++i) if(nodes_[i].id==id) return i; return -1; }
    // Collect visible nodes by walking tree depth-first, respecting expanded state
    void collect_visible(int node_id, int depth, std::vector<std::pair<int,int>>& out) const {
        int idx = find_idx(node_id);
        if (idx < 0) return;
        out.push_back({idx, depth});
        if (nodes_[idx].expanded) {
            for (int cid : nodes_[idx].children)
                collect_visible(cid, depth + 1, out);
        }
    }
    void collect_all_visible(std::vector<std::pair<int,int>>& out) const {
        for (auto& n : nodes_) {
            if (n.parent_id < 0)
                collect_visible(n.id, 0, out);
        }
    }
    void clamp_scroll() {
        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        float content_h = (float)visible.size() * style_.row_height;
        float view_h = math::box_height(base_.get_bounds());
        float max_scroll = content_h - view_h;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll_y_ < 0) scroll_y_ = 0;
        if (scroll_y_ > max_scroll) scroll_y_ = max_scroll;
    }
public:
    bool handle_mouse_scroll(float, float dy) override {
        if (dy == 0) return false;
        scroll_y_ -= dy * style_.row_height * 3;
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
            float rel_y = math::y(p) - math::y(math::box_min(b)) + scroll_y_;
            int row = (style_.row_height > 0) ? (int)(rel_y / style_.row_height) : -1;

            // Build visible list to find which node this row corresponds to
            std::vector<std::pair<int,int>> visible;
            collect_all_visible(visible);

            if (row >= 0 && row < (int)visible.size()) {
                int idx = visible[row].first;
                int depth = visible[row].second;
                float rel_x = math::x(p) - math::x(math::box_min(b));
                float indent_x = depth * style_.indent_width;

                // Click on expand/collapse indicator area
                if (!nodes_[idx].children.empty() && rel_x >= indent_x && rel_x < indent_x + 16) {
                    nodes_[idx].expanded = !nodes_[idx].expanded;
                    if (handler_) handler_->on_node_expanded(nodes_[idx].id, nodes_[idx].expanded);
                } else {
                    // Select node
                    int old = selected_;
                    selected_ = nodes_[idx].id;
                    if (selected_ != old && handler_) handler_->on_node_selected(selected_);
                }
            }
        }
        if (btn == MouseButton::Right && pressed) {
            if (handler_) handler_->on_right_click(p);
            return true;
        }
        return base_.handle_mouse_button(btn, pressed, p);
    }
    int add_node(int parent_id,const char* text,const char* icon) override {
        int id=next_id_++; Node n; n.id=id; n.text=text?text:""; n.icon=icon?icon:""; n.parent_id=parent_id;
        nodes_.push_back(n);
        if(parent_id>=0){int pi=find_idx(parent_id);if(pi>=0)nodes_[pi].children.push_back(id);}
        return id;
    }
    bool remove_node(int id) override { int i=find_idx(id); if(i<0)return false; nodes_.erase(nodes_.begin()+i); return true; }
    void clear_nodes() override { nodes_.clear(); selected_=-1; }
    int get_node_count() const override { return (int)nodes_.size(); }
    const char* get_node_text(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].text.c_str():""; }
    void set_node_text(int id,const char* t) override { int i=find_idx(id); if(i>=0)nodes_[i].text=t?t:""; }
    const char* get_node_icon(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].icon.c_str():""; }
    void set_node_icon(int id,const char* ic) override { int i=find_idx(id); if(i>=0)nodes_[i].icon=ic?ic:""; }
    int get_node_parent(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].parent_id:-1; }
    int get_node_child_count(int id) const override { int i=find_idx(id); return i>=0?(int)nodes_[i].children.size():0; }
    int get_node_child(int id,int idx) const override { int i=find_idx(id); return(i>=0&&idx>=0&&idx<(int)nodes_[i].children.size())?nodes_[i].children[idx]:-1; }
    int get_root_node_count() const override { int c=0; for(auto& n:nodes_) if(n.parent_id<0)++c; return c; }
    int get_root_node(int idx) const override { int c=0; for(auto& n:nodes_){if(n.parent_id<0){if(c==idx)return n.id;++c;}} return -1; }
    bool is_node_expanded(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].expanded:false; }
    void set_node_expanded(int id,bool e) override { int i=find_idx(id); if(i>=0)nodes_[i].expanded=e; }
    void expand_all() override { for(auto& n:nodes_) n.expanded=true; }
    void collapse_all() override { for(auto& n:nodes_) n.expanded=false; }
    void expand_to_node(int id) override {
        int i=find_idx(id); if(i<0)return;
        int pid=nodes_[i].parent_id;
        while(pid>=0){int pi=find_idx(pid);if(pi<0)break;nodes_[pi].expanded=true;pid=nodes_[pi].parent_id;}
    }
    TreeViewSelectionMode get_selection_mode() const override { return sel_mode_; }
    void set_selection_mode(TreeViewSelectionMode m) override { sel_mode_=m; }
    int get_selected_node() const override { return selected_; }
    void set_selected_node(int id) override { selected_=id; }
    void get_selected_nodes(std::vector<int>& out) const override { out=multi_sel_; }
    void set_selected_nodes(const std::vector<int>& ids) override { multi_sel_=ids; }
    void clear_selection() override { selected_=-1; multi_sel_.clear(); }
    void scroll_to_node(int id) override {
        // Find the row index of the node in visible list
        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        for (int i = 0; i < (int)visible.size(); ++i) {
            if (nodes_[visible[i].first].id == id) {
                scroll_y_ = i * style_.row_height;
                clamp_scroll();
                return;
            }
        }
    }
    void ensure_node_visible(int id) override {
        expand_to_node(id);
        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        for (int i = 0; i < (int)visible.size(); ++i) {
            if (nodes_[visible[i].first].id == id) {
                float row_top = i * style_.row_height;
                float row_bot = row_top + style_.row_height;
                float view_h = math::box_height(base_.get_bounds());
                if (row_top < scroll_y_) scroll_y_ = row_top;
                else if (row_bot > scroll_y_ + view_h) scroll_y_ = row_bot - view_h;
                clamp_scroll();
                return;
            }
        }
    }
    float get_scroll_offset() const override { return scroll_y_; }
    void set_scroll_offset(float offset) override { scroll_y_ = offset; clamp_scroll(); }
    float get_total_content_height() const override {
        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        return (float)visible.size() * style_.row_height;
    }
    void set_node_user_data(int id,void* d) override { int i=find_idx(id); if(i>=0)nodes_[i].user_data=d; }
    void* get_node_user_data(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].user_data:nullptr; }
    bool is_node_enabled(int id) const override { int i=find_idx(id); return i>=0?nodes_[i].enabled:false; }
    void set_node_enabled(int id,bool e) override { int i=find_idx(id); if(i>=0)nodes_[i].enabled=e; }
    bool is_drag_reorder_enabled() const override { return drag_reorder_; }
    void set_drag_reorder_enabled(bool e) override { drag_reorder_=e; }
    const TreeViewStyle& get_tree_view_style() const override { return style_; }
    void set_tree_view_style(const TreeViewStyle& s) override { style_=s; }
    void set_tree_event_handler(ITreeViewEventHandler* h) override { handler_=h; }
    void get_tree_view_render_info(TreeViewRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->style=style_; out->total_node_count=(int)nodes_.size(); out->scroll_offset_y=scroll_y_;
    }
    int get_visible_tree_items(TreeNodeRenderItem* out,int max) const override {
        if(!out||max<=0) return 0;
        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        int n=std::min(max,(int)visible.size());
        for(int i=0;i<n;++i){
            int idx=visible[i].first;
            out[i].node_id=nodes_[idx].id;
            out[i].text=nodes_[idx].text.c_str();
            out[i].icon_name=nodes_[idx].icon.c_str();
            out[i].has_children=!nodes_[idx].children.empty();
            out[i].expanded=nodes_[idx].expanded;
            out[i].selected=(nodes_[idx].id==selected_);
            out[i].depth=visible[i].second;
        }
        return n;
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
        ri_.push_rect(bx, by, bw, bh, s.row_background, d++, noclip);

        std::vector<std::pair<int,int>> visible;
        collect_all_visible(visible);
        float row_h = s.row_height;
        float content_h = (float)visible.size() * row_h;

        for (int i = 0; i < (int)visible.size(); i++) {
            int idx = visible[i].first;
            int depth = visible[i].second;
            float ry = by + i * row_h - scroll_y_;
            if (ry + row_h < by || ry > by + bh) continue;

            bool is_sel = (nodes_[idx].id == selected_);
            ri_.push_rect(bx, ry, bw, row_h,
                          is_sel ? s.selected_background : s.row_background, d++, clip);

            float indent = bx + depth * s.indent_width + 4;
            // Expand/collapse indicator
            if (!nodes_[idx].children.empty()) {
                float ex = indent, ey = ry + row_h/2 - 3;
                if (nodes_[idx].expanded) {
                    ri_.push_rect(ex,   ey,   6, 2, s.icon_color, d++, clip);
                    ri_.push_rect(ex+1, ey+2, 4, 2, s.icon_color, d++, clip);
                    ri_.push_rect(ex+2, ey+4, 2, 2, s.icon_color, d++, clip);
                } else {
                    ri_.push_rect(ex,   ey,   2, 6, s.icon_color, d++, clip);
                    ri_.push_rect(ex+2, ey+1, 2, 4, s.icon_color, d++, clip);
                    ri_.push_rect(ex+4, ey+2, 2, 2, s.icon_color, d++, clip);
                }
            }
            // Icon placeholder
            math::Vec4 icon_col(s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.5f);
            ri_.push_rect(indent+12, ry+row_h/2-4, 8, 8, icon_col, d++, clip);
            // Node text
            if (!nodes_[idx].text.empty()) {
                math::Vec4 tc = is_sel ? math::Vec4(1,1,1,1) : s.text_color;
                ri_.push_text(nodes_[idx].text.c_str(), indent+24, ry, bw-(indent+24-bx)-12, row_h,
                              tc, 11.0f, Alignment::CenterLeft, d++, clip);
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
        ri_.finalize();
        base_.clear_dirty();
        return ri_;
    }
};

// Factory function
IGuiTreeView* create_tree_view_widget() { return new GuiTreeView(); }

} // namespace gui
} // namespace window
