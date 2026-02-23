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
    TreeViewStyle style_=TreeViewStyle::default_style();
    ITreeViewEventHandler* handler_=nullptr;
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
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& p) override {
        if (!base_.is_enabled() || !hit_test(p)) return false;
        if (btn == MouseButton::Left && pressed) {
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
};

// Factory function
IGuiTreeView* create_tree_view_widget() { return new GuiTreeView(); }

} // namespace gui
} // namespace window
