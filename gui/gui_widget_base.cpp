/*
 * gui_widget_base.cpp - GuiWidget method bodies (see gui_widget_base.hpp).
 * WidgetBase<> is a template and lives entirely in the header.
 */

#include "gui_widget_base.hpp"

#include <algorithm>
#include <cmath>

namespace window {
namespace gui {

IGuiWidget* GuiWidget::find_by_name(const char* n) {
    if (n && name_ == n) return this;
    for (auto* c : children_) { if (auto* f = c->find_by_name(n)) return f; }
    return nullptr;
}

void GuiWidget::find_all_by_name(const char* n, std::vector<IGuiWidget*>& out) {
    if (n && name_ == n) out.push_back(this);
    for (auto* c : children_) c->find_all_by_name(n, out);
}

void GuiWidget::set_bounds(const math::Box& b) {
    float dx = math::x(math::box_min(b)) - math::x(math::box_min(bounds_));
    float dy = math::y(math::box_min(b)) - math::y(math::box_min(bounds_));
    float dw = math::box_width(b)  - math::box_width(bounds_);
    float dh = math::box_height(b) - math::box_height(bounds_);
    bounds_ = b;
    // Cached render_info stores draw cmds in absolute coords (bg.dest =
    // bounds_), so any bounds change must invalidate the cache or the
    // widget will keep rendering at its old position even though
    // hit-tests already use the new bounds.
    bool moved   = std::abs(dx) > 0.001f || std::abs(dy) > 0.001f;
    bool resized = std::abs(dw) > 0.001f || std::abs(dh) > 0.001f;
    if (moved || resized) mark_dirty();
    if (sizer_) {
        // Sizer owns child placement: re-flow over the new bounds on EVERY
        // call (not just on change). Callers may drive the sizer directly to
        // measure (desyncing it from these bounds), or change child content
        // without moving the container — both must be corrected here. Skips
        // the translate cascade below entirely.
        sizer_->set_bounds(bounds_); sizer_->layout();
    } else if (moved && !children_.empty()) {
        for (auto* c : children_) {
            auto cb = c->get_bounds();
            c->set_bounds(math::make_box(
                math::x(math::box_min(cb)) + dx,
                math::y(math::box_min(cb)) + dy,
                math::box_width(cb),
                math::box_height(cb)
            ));
        }
    }
}

void GuiWidget::set_content_transform(float scale, const math::Vec2& offset) {
    const float s = scale > 0.0f ? scale : 1.0f;
    // No-change guard: rebinding the same view each event must not schedule
    // repaints (mark_dirty reaches the event-driven host) — same contract as
    // set_bounds/set_visible.
    if (s == content_scale_ &&
        math::x(offset) == math::x(content_offset_) &&
        math::y(offset) == math::y(content_offset_)) return;
    content_scale_ = s;
    content_offset_ = offset;
    mark_dirty();
}

math::Vec2 GuiWidget::to_content(const math::Vec2& p) const {
    if (!has_content_transform()) return p;
    const float inv = 1.0f / content_scale_;
    return math::Vec2((math::x(p) - math::x(content_offset_)) * inv,
                      (math::y(p) - math::y(content_offset_)) * inv);
}

void GuiWidget::set_sizer(ISizer* s) {
    sizer_ = s;
    if (sizer_) { sizer_->set_bounds(bounds_); sizer_->layout(); mark_dirty(); }
}

math::Vec2 GuiWidget::get_preferred_size() const {
    math::Vec2 p = sizer_ ? sizer_->get_min_size() : preferred_size_;
    // An explicitly pinned axis (set_preferred_size, component > 0) overrides
    // the measured/sizer value — per the IGuiWidget contract (0 = keep auto).
    const float ex = math::x(explicit_pref_), ey = math::y(explicit_pref_);
    return math::Vec2(ex > 0.0f ? ex : math::x(p), ey > 0.0f ? ey : math::y(p));
}

void GuiWidget::set_preferred_size(const math::Vec2& s) {
    if (math::x(s) == math::x(explicit_pref_) && math::y(s) == math::y(explicit_pref_)) return;
    explicit_pref_ = s;
    mark_dirty();
}

bool GuiWidget::style_eq(const GuiStyle& a, const GuiStyle& b) {
    auto veq = [](const math::Vec4& x, const math::Vec4& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w; };
    return veq(a.background_color, b.background_color) && veq(a.border_color, b.border_color) &&
           veq(a.hover_color, b.hover_color) && veq(a.pressed_color, b.pressed_color) &&
           veq(a.disabled_color, b.disabled_color) && veq(a.focus_color, b.focus_color) &&
           a.background_role == b.background_role &&
           a.border_width == b.border_width && a.corner_radius == b.corner_radius &&
           veq(a.padding, b.padding) && veq(a.margin, b.margin);
}

void GuiWidget::set_style(const GuiStyle& s) {
    if (style_eq(style_, s)) return;
    style_ = s; mark_dirty();
}

const WidgetRenderInfo& GuiWidget::get_render_info(Window*) const {
    if (!dirty_) return render_info_;
    render_info_.invalidate();
    render_info_.clip_rect = clip_enabled_ ? clip_rect_ : bounds_;
    WidgetRenderInfo::ColorCmd bg;
    bg.dest  = bounds_;
    bg.color = style_.background_color;   // literal, unless role != None (themed at collect)
    bg.role  = style_.background_role;
    bg.shape = DrawShape::Rect;
    bg.depth = 0;
    bg.clip  = render_info_.clip_rect;
    render_info_.colors.push_back(bg);
    render_info_.finalize();
    dirty_ = false;
    return render_info_;
}

void GuiWidget::mark_dirty() {
    dirty_ = true;
    if (parent_inv_) parent_inv_->mark_dirty();
    // Invalidation sink (set only on the context root): any descendant marking
    // dirty bubbles here, so the event-driven context learns "something changed"
    // and schedules a re-layout + repaint — no per-frame poll, no app call.
    if (dirty_listener_) dirty_listener_();
}

bool GuiWidget::handle_mouse_move(const math::Vec2& pos) {
    if (!enabled_ || !visible_) return false;
    bool was = (state_ == WidgetState::Hovered);
    bool inside = hit_test(pos);
    if (inside && state_ == WidgetState::Normal) state_ = WidgetState::Hovered;
    else if (!inside && state_ == WidgetState::Hovered) state_ = WidgetState::Normal;
    // Mouse-move is a broadcast: EVERY child must see it so hover leave/enter
    // updates and an active drag (editbox selection, slider) keep tracking.
    // Early-outing on the first child that reports a change starves later
    // siblings — a neighbour's hover-leave would swallow the drag's moves.
    const math::Vec2 cpos = to_content(pos);   // children may live in a transformed space
    bool consumed = false;
    for (auto* c : children_) consumed = c->handle_mouse_move(cpos) || consumed;
    return consumed || (inside != was);
}

bool GuiWidget::handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& pos) {
    if (!enabled_ || !visible_) return false;
    // Reverse order: the LAST child renders on top, so it gets first claim —
    // mirrors find_focusable_at and the paint order.
    const math::Vec2 cpos = to_content(pos);
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        if ((*it)->handle_mouse_button(btn, pressed, cpos)) return true;
    if (!hit_test(pos)) return false;
    if (pressed) state_ = WidgetState::Pressed;
    else if (state_ == WidgetState::Pressed) {
        state_ = WidgetState::Hovered;
        if (event_handler_) {
            GuiEvent ev; ev.type = GuiEventType::Click; ev.source = this; ev.position = pos;
            event_handler_->on_gui_event(ev);
        }
    }
    // Passive containers let the click fall through to whatever is beneath
    // them in z-order; only a widget that actually REACTS (an explicit click
    // handler here, or an interactive subclass that returns true itself)
    // consumes. Opaque panels swallowing every in-bounds press starved the
    // widgets stacked under/behind them (list rows, toolbar, breadcrumb).
    return event_handler_ != nullptr;
}

bool GuiWidget::handle_mouse_scroll(float dx, float dy) {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        if ((*it)->handle_mouse_scroll(dx, dy)) return true;
    return false;
}

bool GuiWidget::handle_key(int code, bool pressed, int mods) {
    for (auto* c : children_) { if (c->handle_key(code, pressed, mods)) return true; }
    return false;
}

bool GuiWidget::handle_text_input(const char* text) {
    for (auto* c : children_) { if (c->handle_text_input(text)) return true; }
    return false;
}

IGuiWidget* GuiWidget::find_widget_at(const math::Vec2& pos) {
    if (!visible_ || !hit_test(pos)) return nullptr;
    const math::Vec2 cpos = to_content(pos);
    for (int i = (int)children_.size() - 1; i >= 0; --i) {
        if (auto* w = children_[i]->find_widget_at(cpos)) return w;
    }
    return this;
}

bool GuiWidget::insert_child_before(IGuiWidget* c, IGuiWidget* before) {
    if (!c) return false;
    if (!before) { children_.push_back(c); c->set_parent(this); return true; }
    auto it = std::find(children_.begin(), children_.end(), before);
    children_.insert(it == children_.end() ? children_.end() : it, c);
    c->set_parent(this);
    return true;
}

bool GuiWidget::remove_child(IGuiWidget* c) {
    auto it = std::find(children_.begin(), children_.end(), c);
    if (it == children_.end()) return false;
    children_.erase(it); return true;
}

bool GuiWidget::remove_child_at(int i) {
    if (i < 0 || i >= (int)children_.size()) return false;
    children_.erase(children_.begin() + i); return true;
}

} // namespace gui
} // namespace window
