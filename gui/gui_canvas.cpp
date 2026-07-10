/*
 * gui_canvas.cpp - CanvasView implementation
 *
 * Widget-side only: view state, the world-space content layer (a container
 * carrying the content transform), the wire list, and the rubber band. The
 * actual backdrop/grid/wire DRAWING happens in the renderer facade
 * (GpuGuiRenderer::render_window_frame), which walks the context tree for
 * visible CanvasView widgets and emits them through the vector renderer —
 * keeping gui/ free of any renderer dependency.
 */

#include "gui_widget_base.hpp"

#include <chrono>

namespace window {
namespace gui {

namespace {

// Content layer: bounds mirror the canvas rect (screen space), children live in
// world space. Overrides set_bounds so a canvas move does NOT run the default
// translate-children cascade — world coordinates are not screen-relative.
class CanvasContentLayer : public GuiWidget {
public:
    CanvasContentLayer() : GuiWidget(WidgetType::Container) {
        GuiStyle st = GuiStyle::default_style();
        st.background_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        set_style(st);
    }
    void set_bounds(const math::Box& b) override {
        if (math::x(math::box_min(b)) == math::x(math::box_min(bounds_)) &&
            math::y(math::box_min(b)) == math::y(math::box_min(bounds_)) &&
            math::box_width(b) == math::box_width(bounds_) &&
            math::box_height(b) == math::box_height(bounds_)) return;
        bounds_ = b;
        mark_dirty();
    }
};

// Nodes layer: self-drawing child of the content layer. Pushes every node's
// rounded body + header/title + pin dots/labels + selection ring in WORLD
// coords; the content transform (world→screen) and the context UI scale carry
// them to physical px, and sub-text_min_px labels are culled at collect. No
// child widgets, no pooling — one render_info rebuilt only when the model
// changes.
class CanvasNodesLayer : public GuiWidget {
public:
    CanvasNodesLayer() : GuiWidget(WidgetType::Container) {
        GuiStyle st = GuiStyle::default_style();
        st.background_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        set_style(st);
    }
    const CanvasNodeStyle& node_style() const { return style_; }
    const std::vector<CanvasNode>& nodes() const { return nodes_; }   // for hit-testing
    void set_node_style(const CanvasNodeStyle& s) { style_ = s; reflow(); }
    void set_nodes(const std::vector<CanvasNode>& nodes) {
        if (nodes == nodes_) return;              // idempotent rebind
        nodes_ = nodes;
        reflow();
    }
    // world coords; a canvas move re-anchors via the parent transform, not here.
    void set_bounds(const math::Box& b) override {
        if (math::x(math::box_min(b)) == math::x(math::box_min(bounds_)) &&
            math::y(math::box_min(b)) == math::y(math::box_min(bounds_)) &&
            math::box_width(b) == math::box_width(bounds_) &&
            math::box_height(b) == math::box_height(bounds_)) return;
        bounds_ = b; mark_dirty();
    }
    const WidgetRenderInfo& get_render_info(Window*) const override {
        if (!dirty_) return render_info_;
        render_info_.invalidate();
        render_info_.clip_rect = bounds_;
        const CanvasNodeStyle& s = style_;
        const float lineh = s.pin_font * 1.35f;   // label line height (no measurer here)
        int32_t d = 0;
        for (const auto& n : nodes_) {
            const float x = math::x(n.pos), y = math::y(n.pos), w = n.width, h = n.height;
            const math::Box clip = bounds_;
            if (n.selected) {   // rounded ring behind the body
                const float bw = s.selection_border;
                render_info_.push_round_rect(x - bw, y - bw, w + 2 * bw, h + 2 * bw,
                                             s.corner_radius + bw, GuiColor::Selection, d++, clip);
            }
            render_info_.push_round_rect(x, y, w, h, s.corner_radius, n.body_color, d++, clip);
            render_info_.push_round_rect(x, y, w, s.header_height, s.corner_radius, n.header_color, d++, clip);
            if (!n.title.empty())
                render_info_.push_text(n.title.c_str(), x + s.title_pad, y, w - 2 * s.title_pad,
                                       s.header_height, GuiColor::NodeTitle, s.title_font,
                                       Alignment::CenterLeft, d++, clip);
            for (const auto& p : n.pins) {
                const float py = y + s.header_height + s.row_height * (float(p.row) + 0.5f);
                const float px = p.output ? x + w : x;
                render_info_.push_circle(px, py, s.pin_radius, p.dot_color, d++, clip);
                if (!p.name.empty())
                    render_info_.push_text(p.name.c_str(), x + s.pin_label_pad, py - lineh * 0.5f,
                                           w - 2 * s.pin_label_pad, lineh, GuiColor::PinLabel, s.pin_font,
                                           p.output ? Alignment::CenterRight : Alignment::CenterLeft, d++, clip);
            }
        }
        render_info_.finalize();
        dirty_ = false;
        return render_info_;
    }
private:
    // Cover all nodes so collect visits us (empty bounds are skipped) and the
    // clip encloses every node. A 1×1 fallback keeps an empty canvas valid.
    void reflow() {
        if (nodes_.empty()) { set_bounds(math::make_box(0.0f, 0.0f, 1.0f, 1.0f)); mark_dirty(); return; }
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        const float bw = style_.selection_border;
        for (const auto& n : nodes_) {
            x0 = std::min(x0, math::x(n.pos) - bw);
            y0 = std::min(y0, math::y(n.pos) - bw);
            x1 = std::max(x1, math::x(n.pos) + n.width + bw);
            y1 = std::max(y1, math::y(n.pos) + n.height + bw);
        }
        set_bounds(math::make_box(x0, y0, x1 - x0, y1 - y0));
        mark_dirty();
    }
    std::vector<CanvasNode> nodes_;
    CanvasNodeStyle style_ = CanvasNodeStyle::default_style();
};

// Rubber band: screen-space fill + 1 px outline, sized by the canvas.
class CanvasRubberBand : public GuiWidget {
public:
    CanvasRubberBand() : GuiWidget(WidgetType::Custom) { visible_ = false; }
    void set_colors(const math::Vec4& fill, const math::Vec4& border) {
        fill_ = fill; border_ = border; mark_dirty();
    }
    const WidgetRenderInfo& get_render_info(Window*) const override {
        if (!dirty_) return render_info_;
        render_info_.invalidate();
        render_info_.clip_rect = bounds_;
        const float x = math::x(math::box_min(bounds_)), y = math::y(math::box_min(bounds_));
        const float w = math::box_width(bounds_), h = math::box_height(bounds_);
        int32_t depth = 0;
        render_info_.push_rect(x, y, w, h, fill_, depth++, bounds_);
        render_info_.push_outline(x, y, w, h, border_, depth, bounds_);
        render_info_.finalize();
        dirty_ = false;
        return render_info_;
    }
private:
    math::Vec4 fill_ = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    math::Vec4 border_ = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
};

} // namespace

class GuiCanvasView : public WidgetBase<IGuiCanvasView, WidgetType::CanvasView> {
public:
    GuiCanvasView() {
        // The canvas paints nothing itself (backdrop/grid go through the vector
        // pass, UNDER the widget batch); it only clips + transforms its content.
        GuiStyle st = GuiStyle::default_style();
        st.background_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        base_.set_style(st);
        base_.set_clip_enabled(true);
        content_.set_content_text_min_px(style_.text_min_px);
        content_.add_child(&nodes_layer_);   // world-space; drawn under app content() overlays
        base_.add_child(&content_);
        base_.add_child(&rubber_);
        rubber_.set_colors(style_.rubber_fill, style_.rubber_border);
        refresh_transform();
    }

    // ---- IGuiCanvasView -----------------------------------------------------
    const CanvasStyle& get_canvas_style() const override { return style_; }
    void set_canvas_style(const CanvasStyle& s) override {
        style_ = s;
        content_.set_content_text_min_px(style_.text_min_px);
        rubber_.set_colors(style_.rubber_fill, style_.rubber_border);
        base_.mark_dirty();
    }

    void set_view(const math::Vec2& origin, float scale) override {
        const float s = scale > 0.0f ? scale : 1.0f;
        if (s == scale_ && math::x(origin) == math::x(origin_) &&
            math::y(origin) == math::y(origin_)) return;    // idempotent rebind
        origin_ = origin;
        scale_ = s;
        refresh_transform();
    }
    math::Vec2 view_origin() const override { return origin_; }
    float view_scale() const override { return scale_; }
    math::Vec2 world_to_screen(const math::Vec2& w) const override {
        const math::Box b = base_.get_bounds();
        return math::Vec2((math::x(w) - math::x(origin_)) * scale_ + math::x(math::box_min(b)),
                          (math::y(w) - math::y(origin_)) * scale_ + math::y(math::box_min(b)));
    }
    math::Vec2 screen_to_world(const math::Vec2& s) const override {
        const math::Box b = base_.get_bounds();
        const float inv = 1.0f / scale_;
        return math::Vec2((math::x(s) - math::x(math::box_min(b))) * inv + math::x(origin_),
                          (math::y(s) - math::y(math::box_min(b))) * inv + math::y(origin_));
    }

    IGuiWidget* content() override { return &content_; }

    const CanvasNodeStyle& get_node_style() const override { return nodes_layer_.node_style(); }
    void set_node_style(const CanvasNodeStyle& s) override { nodes_layer_.set_node_style(s); }
    void set_nodes(const std::vector<CanvasNode>& nodes) override { nodes_layer_.set_nodes(nodes); }

    int add_wire(const std::vector<math::Vec2>& pts, const CanvasWireStyle& ws) override {
        CanvasWire w;
        w.id = next_wire_id_++;
        w.points = pts;
        w.style = ws;
        wires_.push_back(std::move(w));
        base_.mark_dirty();
        return wires_.back().id;
    }
    void set_wire_points(int id, const std::vector<math::Vec2>& pts) override {
        if (CanvasWire* w = find_wire(id)) { w->points = pts; base_.mark_dirty(); }
    }
    void set_wire_style(int id, const CanvasWireStyle& ws) override {
        if (CanvasWire* w = find_wire(id)) { w->style = ws; base_.mark_dirty(); }
    }
    void remove_wire(int id) override {
        for (std::size_t i = 0; i < wires_.size(); ++i)
            if (wires_[i].id == id) { wires_.erase(wires_.begin() + i); base_.mark_dirty(); return; }
    }
    void clear_wires() override {
        if (!wires_.empty()) { wires_.clear(); base_.mark_dirty(); }
    }
    int wire_count() const override { return (int)wires_.size(); }
    const CanvasWire& get_wire(int index) const override { return wires_[(std::size_t)index]; }
    void set_wires(const std::vector<CanvasWire>& wires) override {
        if (wires_equal(wires)) return;    // idempotent rebind: no repaint scheduled
        wires_ = wires;
        for (auto& w : wires_) w.id = next_wire_id_++;
        base_.mark_dirty();
    }
    void bind_nodes(std::function<std::vector<CanvasNode>()> provider) override {
        nodes_provider_ = std::move(provider); base_.mark_dirty();
    }
    void bind_wires(std::function<std::vector<CanvasWire>()> provider) override {
        wires_provider_ = std::move(provider); base_.mark_dirty();
    }
    void refresh_bindings() override {
        if (nodes_provider_) set_nodes(nodes_provider_());
        if (wires_provider_) set_wires(wires_provider_());
    }

    void show_rubber_band(const math::Box& world_rect) override {
        if (rubber_on_ && box_equal(rubber_world_, world_rect)) return;
        rubber_world_ = world_rect;
        rubber_on_ = true;
        sync_rubber();
        rubber_.set_visible(true);
    }
    void hide_rubber_band() override {
        rubber_on_ = false;
        rubber_.set_visible(false);
    }

    // ---- IGuiWidget bits WidgetBase stubs out but a real container needs ----
    // Children: the app-facing tree is the CONTENT layer (world space); the
    // context still traverses the real children (content + rubber) via base_.
    int get_child_count() const override { return base_.get_child_count(); }
    IGuiWidget* get_child(int i) const override { return base_.get_child(i); }
    bool add_child(IGuiWidget* c) override { return content_.add_child(c); }
    bool insert_child_before(IGuiWidget* c, IGuiWidget* before) override { return content_.insert_child_before(c, before); }
    bool remove_child(IGuiWidget* c) override { return content_.remove_child(c); }
    bool remove_child_at(int i) override { return content_.remove_child_at(i); }
    void clear_children() override { content_.clear_children(); }
    IGuiWidget* find_by_name(const char* n) override { return base_.find_by_name(n); }
    void find_all_by_name(const char* n, std::vector<IGuiWidget*>& out) override { base_.find_all_by_name(n, out); }
    IGuiWidget* find_widget_at(const math::Vec2& p) override { return base_.find_widget_at(p); }

    // Camera re-anchors when the canvas rect moves/resizes.
    void set_bounds(const math::Box& b) override {
        if (box_equal(b, base_.get_bounds())) return;    // idempotent rebind
        base_.set_bounds(b);
        refresh_transform();
    }

    void set_canvas_event_handler(ICanvasViewEventHandler* h) override { evt_ = h; }
    void set_zoom_limits(float mn, float mx) override {
        if (mn > 0.0f) min_scale_ = mn;
        if (mx > 0.0f) max_scale_ = mx;
    }
    void set_modifier_provider(std::function<int()> p) override { mods_provider_ = std::move(p); }

    // ---- Built-in interaction: camera (middle/right drag pan, wheel zoom) + left-button
    //      node selection / drag / marquee. Pin-link + connection/waypoint land in later
    //      phases; all decisions are made by the app via ICanvasViewEventHandler. --------
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& pos) override {
        if (btn == MouseButton::Middle || btn == MouseButton::Right) {
            if (pressed) { panning_ = true; pan_last_ = pos; }
            else         { panning_ = false; }
            return true;
        }
        if (btn == MouseButton::Left) return pressed ? left_press(pos) : left_release(pos);
        return base_.handle_mouse_button(btn, pressed, pos);
    }
    bool handle_mouse_move(const math::Vec2& pos) override {
        last_mouse_ = pos;                                 // tracked for wheel-zoom centre
        if (panning_) {
            const float inv = 1.0f / scale_;
            origin_ = math::Vec2(math::x(origin_) - (math::x(pos) - math::x(pan_last_)) * inv,
                                 math::y(origin_) - (math::y(pos) - math::y(pan_last_)) * inv);
            pan_last_ = pos;
            refresh_transform();
            base_.mark_dirty();
            if (evt_) evt_->on_canvas_view_changed(origin_, scale_);
            return true;
        }
        if (node_press_ && want_drag_) {
            if (!node_dragging_ &&
                std::abs(math::x(pos) - math::x(press_screen_)) +
                std::abs(math::y(pos) - math::y(press_screen_)) > 4.0f)
                node_dragging_ = true;
            if (node_dragging_) {
                const math::Vec2 w = screen_to_world(pos);
                drag_moved_ = true;
                if (evt_) evt_->on_canvas_nodes_dragged(
                    math::Vec2(math::x(w) - math::x(press_world_), math::y(w) - math::y(press_world_)));
            }
            return true;
        }
        if (marquee_) {
            const math::Vec2 w = screen_to_world(pos);
            mq_cur_world_ = w;
            const float x0 = std::min(math::x(mq_start_world_), math::x(w));
            const float y0 = std::min(math::y(mq_start_world_), math::y(w));
            const float x1 = std::max(math::x(mq_start_world_), math::x(w));
            const float y1 = std::max(math::y(mq_start_world_), math::y(w));
            show_rubber_band(math::make_box(x0, y0, x1 - x0, y1 - y0));
            return true;
        }
        return base_.handle_mouse_move(pos);
    }
    bool handle_mouse_scroll(float dx, float dy) override {
        if (dy == 0.0f) return base_.handle_mouse_scroll(dx, dy);
        zoom_about(last_mouse_, dy > 0.0f ? 1.1f : (1.0f / 1.1f));
        return true;
    }

private:
    // Topmost node whose world rect contains `w` (draw order = model order → iterate back).
    const CanvasNode* node_at(const math::Vec2& w) const {
        const auto& ns = nodes_layer_.nodes();
        for (auto it = ns.rbegin(); it != ns.rend(); ++it) {
            const float x = math::x(it->pos), y = math::y(it->pos);
            if (math::x(w) >= x && math::x(w) <= x + it->width &&
                math::y(w) >= y && math::y(w) <= y + it->height) return &*it;
        }
        return nullptr;
    }
    // Synthesize a double-click (the X11 backend doesn't report click counts): a second
    // left press within 350 ms and 6 px of the first.
    bool detect_double(const math::Vec2& screen) {
        const double t = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool dbl = (t - last_click_ms_ < 350.0) &&
                         std::abs(math::x(screen) - math::x(last_click_pos_)) < 6.0f &&
                         std::abs(math::y(screen) - math::y(last_click_pos_)) < 6.0f;
        last_click_ms_ = dbl ? -1e30 : t;   // a double resets so a 3rd click starts fresh
        last_click_pos_ = screen;
        return dbl;
    }
    bool left_press(const math::Vec2& screen) {
        if (base_.handle_mouse_button(MouseButton::Left, true, screen)) return true;   // app overlays first
        const math::Vec2 w = screen_to_world(screen);
        const bool dbl = detect_double(screen);
        const int mods = mods_provider_ ? mods_provider_() : 0;
        press_screen_ = screen; press_world_ = w;
        node_dragging_ = drag_moved_ = false;
        if (const CanvasNode* n = node_at(w)) {
            node_press_ = true; press_node_id_ = n->id;
            want_drag_ = evt_ ? evt_->on_canvas_node_pressed(n->id, mods, dbl) : false;
            return true;
        }
        marquee_ = true; marquee_mods_ = mods; mq_start_world_ = mq_cur_world_ = w;
        if (evt_) evt_->on_canvas_background_pressed(w, mods);
        return true;
    }
    bool left_release(const math::Vec2& screen) {
        if (node_press_) {
            const bool moved = drag_moved_;
            node_press_ = node_dragging_ = false;
            if (evt_ && want_drag_) evt_->on_canvas_node_drag_end(moved);
            want_drag_ = false;
            return true;
        }
        if (marquee_) {
            marquee_ = false;
            hide_rubber_band();
            const float x0 = std::min(math::x(mq_start_world_), math::x(mq_cur_world_));
            const float y0 = std::min(math::y(mq_start_world_), math::y(mq_cur_world_));
            const float x1 = std::max(math::x(mq_start_world_), math::x(mq_cur_world_));
            const float y1 = std::max(math::y(mq_start_world_), math::y(mq_cur_world_));
            if (evt_) evt_->on_canvas_marquee(math::make_box(x0, y0, x1 - x0, y1 - y0), marquee_mods_);
            return true;
        }
        return base_.handle_mouse_button(MouseButton::Left, false, screen);
    }

    // Zoom keeping the world point under `screen` fixed (wheel + programmatic).
    void zoom_about(const math::Vec2& screen, float factor) {
        const math::Vec2 w = screen_to_world(screen);
        float ns = scale_ * factor;
        if (ns < min_scale_) ns = min_scale_;
        if (ns > max_scale_) ns = max_scale_;
        if (ns == scale_) return;
        scale_ = ns;
        const math::Box b = base_.get_bounds();
        const float inv = 1.0f / scale_;
        origin_ = math::Vec2(math::x(w) - (math::x(screen) - math::x(math::box_min(b))) * inv,
                             math::y(w) - (math::y(screen) - math::y(math::box_min(b))) * inv);
        refresh_transform();
        base_.mark_dirty();
        if (evt_) evt_->on_canvas_view_changed(origin_, scale_);
    }

    static bool box_equal(const math::Box& a, const math::Box& b) {
        return math::x(math::box_min(a)) == math::x(math::box_min(b)) &&
               math::y(math::box_min(a)) == math::y(math::box_min(b)) &&
               math::box_width(a) == math::box_width(b) &&
               math::box_height(a) == math::box_height(b);
    }
    bool wires_equal(const std::vector<CanvasWire>& o) const {
        if (o.size() != wires_.size()) return false;
        for (std::size_t i = 0; i < o.size(); ++i) {
            const CanvasWire& a = wires_[i];
            const CanvasWire& b = o[i];
            if (a.style != b.style || a.points.size() != b.points.size()) return false;
            for (std::size_t j = 0; j < a.points.size(); ++j)
                if (math::x(a.points[j]) != math::x(b.points[j]) ||
                    math::y(a.points[j]) != math::y(b.points[j])) return false;
        }
        return true;
    }
    CanvasWire* find_wire(int id) {
        for (auto& w : wires_) if (w.id == id) return &w;
        return nullptr;
    }
    // Re-anchor the content transform + clip + rubber band to the current
    // bounds/view. screen = world*scale + (canvas_min - origin*scale). Repaint
    // scheduling rides on the children's own guarded mark_dirty calls, so a
    // no-op rebind stays silent.
    void refresh_transform() {
        const math::Box b = base_.get_bounds();
        const math::Vec2 off(math::x(math::box_min(b)) - math::x(origin_) * scale_,
                             math::y(math::box_min(b)) - math::y(origin_) * scale_);
        content_.set_bounds(b);
        content_.set_content_transform(scale_, off);
        base_.set_clip_rect(b);
        if (rubber_on_) sync_rubber();
    }
    void sync_rubber() {
        const math::Vec2 mn = world_to_screen(math::Vec2(math::x(math::box_min(rubber_world_)),
                                                         math::y(math::box_min(rubber_world_))));
        rubber_.set_bounds(math::make_box(math::x(mn), math::y(mn),
                                          math::box_width(rubber_world_) * scale_,
                                          math::box_height(rubber_world_) * scale_));
    }

    CanvasStyle style_ = CanvasStyle::default_style();
    math::Vec2 origin_ = math::Vec2(0.0f, 0.0f);   // world point at the canvas top-left
    float scale_ = 1.0f;                            // screen px per world unit
    CanvasContentLayer content_;
    CanvasNodesLayer nodes_layer_;                  // self-drawing node cards (world space)
    CanvasRubberBand rubber_;
    math::Box rubber_world_ = math::make_box(0.0f, 0.0f, 0.0f, 0.0f);
    bool rubber_on_ = false;
    std::vector<CanvasWire> wires_;
    int next_wire_id_ = 0;
    std::function<std::vector<CanvasNode>()> nodes_provider_;   // bound model sources (set once)
    std::function<std::vector<CanvasWire>()> wires_provider_;
    // Interaction state.
    ICanvasViewEventHandler* evt_ = nullptr;
    std::function<int()> mods_provider_;                 // context-wired current key mods
    bool       panning_ = false;
    math::Vec2 pan_last_   = math::Vec2(0.0f, 0.0f);
    math::Vec2 last_mouse_ = math::Vec2(0.0f, 0.0f);
    float      min_scale_ = 0.2f, max_scale_ = 3.0f;
    // Left-button node/marquee drag state.
    bool        node_press_ = false, node_dragging_ = false, want_drag_ = false, drag_moved_ = false;
    bool        marquee_ = false;
    int         marquee_mods_ = 0;
    std::string press_node_id_;
    math::Vec2  press_screen_   = math::Vec2(0.0f, 0.0f);
    math::Vec2  press_world_    = math::Vec2(0.0f, 0.0f);
    math::Vec2  mq_start_world_ = math::Vec2(0.0f, 0.0f);
    math::Vec2  mq_cur_world_   = math::Vec2(0.0f, 0.0f);
    double      last_click_ms_  = -1e30;
    math::Vec2  last_click_pos_ = math::Vec2(0.0f, 0.0f);
};

IGuiCanvasView* create_canvas_view_widget() { return new GuiCanvasView(); }

} // namespace gui
} // namespace window
