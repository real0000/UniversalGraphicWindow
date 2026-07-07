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

private:
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
    CanvasRubberBand rubber_;
    math::Box rubber_world_ = math::make_box(0.0f, 0.0f, 0.0f, 0.0f);
    bool rubber_on_ = false;
    std::vector<CanvasWire> wires_;
    int next_wire_id_ = 0;
};

IGuiCanvasView* create_canvas_view_widget() { return new GuiCanvasView(); }

} // namespace gui
} // namespace window
