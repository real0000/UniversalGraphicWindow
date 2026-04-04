/*
 * editor_canvas.cpp - Editor Canvas Implementation
 */

#include "editor_canvas.hpp"
#include "gui/gui_context.hpp"
#include "gui/gui_serialization.hpp"
#include <cmath>
#include <algorithm>

namespace window {
namespace gui {
namespace editor {

using math::x; using math::y;
using math::box_min; using math::box_max;
using math::box_width; using math::box_height;
using math::make_box; using math::box_contains;

// ============================================================================
// SelectionInfo
// ============================================================================

math::Box SelectionInfo::get_combined_bounds() const {
    if (widgets.empty()) return {};
    math::Box result = widgets[0]->get_bounds();
    for (size_t i = 1; i < widgets.size(); ++i) {
        math::Box b = widgets[i]->get_bounds();
        float min_x = std::min(x(box_min(result)), x(box_min(b)));
        float min_y = std::min(y(box_min(result)), y(box_min(b)));
        float max_x = std::max(x(box_max(result)), x(box_max(b)));
        float max_y = std::max(y(box_max(result)), y(box_max(b)));
        result = math::Box(math::Vec2(min_x, min_y), math::Vec2(max_x, max_y));
    }
    return result;
}

// ============================================================================
// EditorCanvas
// ============================================================================

void EditorCanvas::initialize(IGuiContext* ctx, CommandHistory* history) {
    m_ctx = ctx;
    m_history = history;
}

void EditorCanvas::shutdown() {
    m_selection.clear();
    m_clipboard.clear();
    m_design_root = nullptr;
    m_ctx = nullptr;
    m_history = nullptr;
}

void EditorCanvas::select_widget(IGuiWidget* widget, bool add_to_selection) {
    if (!add_to_selection) {
        m_selection.clear();
    }
    if (widget && widget != m_design_root) {
        if (m_selection.contains(widget) && add_to_selection) {
            m_selection.remove(widget);
        } else {
            m_selection.add(widget);
        }
    }
}

void EditorCanvas::clear_selection() {
    m_selection.clear();
}

void EditorCanvas::select_all() {
    m_selection.clear();
    if (!m_design_root) return;
    for (int i = 0; i < m_design_root->get_child_count(); ++i) {
        m_selection.add(m_design_root->get_child(i));
    }
}

void EditorCanvas::delete_selection() {
    if (m_selection.empty() || !m_history) return;
    auto widgets = m_selection.widgets;
    m_selection.clear();
    for (auto it = widgets.rbegin(); it != widgets.rend(); ++it) {
        m_history->execute(std::make_unique<DeleteWidgetCommand>(m_ctx, *it));
    }
}

// ============================================================================
// Input handling
// ============================================================================

bool EditorCanvas::handle_mouse_down(MouseButton button, const math::Vec2& pos,
                                     bool shift, bool ctrl) {
    m_mouse_down_pos = pos;
    m_mouse_current_pos = pos;
    math::Vec2 canvas_pos = screen_to_canvas(pos);

    if (button == MouseButton::Middle) {
        m_mode = CanvasMode::Panning;
        return true;
    }

    if (button != MouseButton::Left) return false;

    if (!m_selection.empty()) {
        HandlePosition handle = hit_test_handles(pos);
        if (handle != HandlePosition::None) {
            m_active_handle = handle;
            m_drag_start_bounds = m_selection.get_combined_bounds();
            m_drag_start_canvas = canvas_pos;
            m_mode = CanvasMode::Resizing;
            return true;
        }
    }

    IGuiWidget* hit = hit_test_widget(canvas_pos);

    if (hit) {
        bool already_selected = m_selection.contains(hit);
        if (ctrl || shift) {
            select_widget(hit, true);
        } else if (!already_selected) {
            select_widget(hit, false);
        }
        m_drag_start_bounds = m_selection.get_combined_bounds();
        m_drag_start_canvas = canvas_pos;
        build_drag_all_bounds();
        m_mode = CanvasMode::Moving;
        return true;
    }

    if (!ctrl && !shift) {
        m_selection.clear();
    }
    m_rubber_band = make_box(x(pos), y(pos), 0.0f, 0.0f);
    m_mode = CanvasMode::RubberBand;
    return true;
}

bool EditorCanvas::handle_mouse_up(MouseButton button, const math::Vec2& pos) {
    (void)pos;

    if (button == MouseButton::Middle && m_mode == CanvasMode::Panning) {
        m_mode = CanvasMode::Idle;
        return true;
    }

    if (button != MouseButton::Left) return false;

    switch (m_mode) {
        case CanvasMode::Moving:    finish_move(); break;
        case CanvasMode::Resizing:  finish_resize(); break;
        case CanvasMode::RubberBand: finish_rubber_band(false); break;
        default: break;
    }

    m_mode = CanvasMode::Idle;
    m_active_handle = HandlePosition::None;
    return true;
}

bool EditorCanvas::handle_mouse_move(const math::Vec2& pos) {
    float dx = x(pos) - x(m_mouse_current_pos);
    float dy = y(pos) - y(m_mouse_current_pos);
    m_mouse_current_pos = pos;
    math::Vec2 canvas_pos = screen_to_canvas(pos);

    switch (m_mode) {
        case CanvasMode::Panning: {
            m_pan = math::Vec2(x(m_pan) + dx, y(m_pan) + dy);
            return true;
        }

        case CanvasMode::Moving: {
            // cdx/cdy are the total displacement in canvas space from drag start.
            // Use screen_to_canvas on both endpoints so zoom and pan are properly
            // inverted (widget coords are in canvas/world space).
            float cdx = x(canvas_pos) - x(m_drag_start_canvas);
            float cdy = y(canvas_pos) - y(m_drag_start_canvas);

            if (m_grid.snap_enabled) {
                float dsb_x = x(box_min(m_drag_start_bounds));
                float dsb_y = y(box_min(m_drag_start_bounds));
                cdx = m_grid.snap(dsb_x + cdx) - dsb_x;
                cdy = m_grid.snap(dsb_y + cdy) - dsb_y;
            }

            // Apply absolute new position = original_pos + delta to selected
            // widgets AND all their descendants (children move with parent).
            for (auto& [w, orig] : m_drag_all_bounds) {
                float nx = x(box_min(orig)) + cdx;
                float ny = y(box_min(orig)) + cdy;
                w->set_bounds(make_box(nx, ny, box_width(orig), box_height(orig)));
            }
            return true;
        }

        case CanvasMode::Resizing: {
            float cdx = x(canvas_pos) - x(m_drag_start_canvas);
            float cdy = y(canvas_pos) - y(m_drag_start_canvas);
            float bx = x(box_min(m_drag_start_bounds));
            float by = y(box_min(m_drag_start_bounds));
            float bw = box_width(m_drag_start_bounds);
            float bh = box_height(m_drag_start_bounds);

            switch (m_active_handle) {
                case HandlePosition::TopLeft:
                    bx += cdx; by += cdy; bw -= cdx; bh -= cdy; break;
                case HandlePosition::TopCenter:
                    by += cdy; bh -= cdy; break;
                case HandlePosition::TopRight:
                    by += cdy; bw += cdx; bh -= cdy; break;
                case HandlePosition::CenterLeft:
                    bx += cdx; bw -= cdx; break;
                case HandlePosition::CenterRight:
                    bw += cdx; break;
                case HandlePosition::BottomLeft:
                    bx += cdx; bw -= cdx; bh += cdy; break;
                case HandlePosition::BottomCenter:
                    bh += cdy; break;
                case HandlePosition::BottomRight:
                    bw += cdx; bh += cdy; break;
                default: break;
            }

            if (bw < MIN_WIDGET_SIZE) bw = MIN_WIDGET_SIZE;
            if (bh < MIN_WIDGET_SIZE) bh = MIN_WIDGET_SIZE;

            if (m_grid.snap_enabled) {
                bx = m_grid.snap(bx); by = m_grid.snap(by);
                bw = m_grid.snap(bw); bh = m_grid.snap(bh);
            }

            IGuiWidget* primary = m_selection.primary();
            if (primary) {
                primary->set_bounds(make_box(bx, by, bw, bh));
            }
            return true;
        }

        case CanvasMode::RubberBand: {
            float mdx = x(m_mouse_down_pos), mdy = y(m_mouse_down_pos);
            float px = x(pos), py = y(pos);
            float rx = std::min(mdx, px);
            float ry = std::min(mdy, py);
            float rw = std::abs(px - mdx);
            float rh = std::abs(py - mdy);
            m_rubber_band = make_box(rx, ry, rw, rh);
            return true;
        }

        case CanvasMode::Idle: {
            m_hovered_handle = m_selection.empty() ? HandlePosition::None : hit_test_handles(pos);
            return false;
        }

        default:
            return false;
    }
}

bool EditorCanvas::handle_mouse_scroll(float /*unused_dx*/, float scroll_dy) {
    float old_zoom = m_zoom;
    m_zoom += scroll_dy * ZOOM_STEP;
    m_zoom = std::max(ZOOM_MIN, std::min(ZOOM_MAX, m_zoom));

    float ratio = m_zoom / old_zoom;
    float crx = x(m_mouse_current_pos) - x(box_min(m_viewport_bounds)) - x(m_pan);
    float cry = y(m_mouse_current_pos) - y(box_min(m_viewport_bounds)) - y(m_pan);
    m_pan = math::Vec2(x(m_pan) - crx * (ratio - 1.0f),
                        y(m_pan) - cry * (ratio - 1.0f));

    return true;
}

bool EditorCanvas::handle_key_down(int key, bool ctrl, bool shift) {
    float nudge = shift ? NUDGE_LARGE : NUDGE_AMOUNT;

    if (key == 0x25) { nudge_selection(-nudge, 0); return true; }
    if (key == 0x26) { nudge_selection(0, -nudge); return true; }
    if (key == 0x27) { nudge_selection(nudge, 0); return true; }
    if (key == 0x28) { nudge_selection(0, nudge); return true; }
    if (key == 0x2E) { delete_selection(); return true; }
    if (ctrl && key == 'A') { select_all(); return true; }
    if (ctrl && key == 'C') { copy_selection(); return true; }
    if (ctrl && key == 'X') { cut_selection(); return true; }
    if (ctrl && key == 'V') { paste(screen_to_canvas(m_mouse_current_pos)); return true; }

    return false;
}

// ============================================================================
// View transform
// ============================================================================

void EditorCanvas::set_zoom(float z) {
    m_zoom = std::max(ZOOM_MIN, std::min(ZOOM_MAX, z));
}

void EditorCanvas::zoom_to_fit() {
    if (!m_design_root || m_design_root->get_child_count() == 0) {
        reset_view();
        return;
    }

    math::Box content = m_design_root->get_child(0)->get_bounds();
    for (int i = 1; i < m_design_root->get_child_count(); ++i) {
        math::Box b = m_design_root->get_child(i)->get_bounds();
        float min_x = std::min(x(box_min(content)), x(box_min(b)));
        float min_y = std::min(y(box_min(content)), y(box_min(b)));
        float max_x = std::max(x(box_max(content)), x(box_max(b)));
        float max_y = std::max(y(box_max(content)), y(box_max(b)));
        content = math::Box(math::Vec2(min_x, min_y), math::Vec2(max_x, max_y));
    }

    float cw = box_width(content);
    float ch = box_height(content);
    float vw = box_width(m_viewport_bounds);
    float vh = box_height(m_viewport_bounds);

    if (cw <= 0 || ch <= 0 || vw <= 0 || vh <= 0) {
        reset_view();
        return;
    }

    float margin = 40.0f;
    float zx = (vw - margin * 2) / cw;
    float zy = (vh - margin * 2) / ch;
    m_zoom = std::min(zx, zy);
    m_zoom = std::max(ZOOM_MIN, std::min(ZOOM_MAX, m_zoom));

    m_pan = math::Vec2(
        (vw - cw * m_zoom) * 0.5f - x(box_min(content)) * m_zoom,
        (vh - ch * m_zoom) * 0.5f - y(box_min(content)) * m_zoom
    );
}

void EditorCanvas::reset_view() {
    m_zoom = 1.0f;
    // Center canvas origin (0,0) in the viewport
    m_pan = math::Vec2(
        box_width(m_viewport_bounds) * 0.5f,
        box_height(m_viewport_bounds) * 0.5f
    );
}

math::Vec2 EditorCanvas::screen_to_canvas(const math::Vec2& screen) const {
    return math::Vec2(
        (x(screen) - x(box_min(m_viewport_bounds)) - x(m_pan)) / m_zoom,
        (y(screen) - y(box_min(m_viewport_bounds)) - y(m_pan)) / m_zoom
    );
}

math::Vec2 EditorCanvas::canvas_to_screen(const math::Vec2& canvas) const {
    return math::Vec2(
        x(canvas) * m_zoom + x(m_pan) + x(box_min(m_viewport_bounds)),
        y(canvas) * m_zoom + y(m_pan) + y(box_min(m_viewport_bounds))
    );
}

// ============================================================================
// Selection handles
// ============================================================================

void EditorCanvas::get_selection_handles(std::vector<HandleRect>& out_handles) const {
    out_handles.clear();
    if (m_selection.empty()) return;

    math::Box bounds = m_selection.get_combined_bounds();
    math::Vec2 tl = canvas_to_screen(box_min(bounds));
    math::Vec2 br = canvas_to_screen(box_max(bounds));
    float hs = HANDLE_SIZE;

    auto make_handle = [&](float cx, float cy, HandlePosition pos) {
        HandleRect hr;
        hr.rect = make_box(cx - hs * 0.5f, cy - hs * 0.5f, hs, hs);
        hr.pos = pos;
        hr.hovered = (pos == m_hovered_handle);
        out_handles.push_back(hr);
    };

    float mx = (x(tl) + x(br)) * 0.5f;
    float my = (y(tl) + y(br)) * 0.5f;

    make_handle(x(tl), y(tl), HandlePosition::TopLeft);
    make_handle(mx,    y(tl), HandlePosition::TopCenter);
    make_handle(x(br), y(tl), HandlePosition::TopRight);
    make_handle(x(tl), my,    HandlePosition::CenterLeft);
    make_handle(x(br), my,    HandlePosition::CenterRight);
    make_handle(x(tl), y(br), HandlePosition::BottomLeft);
    make_handle(mx,    y(br), HandlePosition::BottomCenter);
    make_handle(x(br), y(br), HandlePosition::BottomRight);
}

HandlePosition EditorCanvas::hit_test_handles(const math::Vec2& pos) const {
    std::vector<HandleRect> handles;
    get_selection_handles(handles);
    for (const auto& h : handles) {
        if (box_contains(h.rect, pos)) {
            return h.pos;
        }
    }
    return HandlePosition::None;
}

// ============================================================================
// Hit testing
// ============================================================================

IGuiWidget* EditorCanvas::hit_test_widget(const math::Vec2& canvas_pos) const {
    if (!m_design_root) return nullptr;
    for (int i = m_design_root->get_child_count() - 1; i >= 0; --i) {
        IGuiWidget* result = hit_test_recursive(m_design_root->get_child(i), canvas_pos);
        if (result) return result;
    }
    return nullptr;
}

IGuiWidget* EditorCanvas::hit_test_recursive(IGuiWidget* widget, const math::Vec2& pos) const {
    if (!widget || !widget->is_visible()) return nullptr;
    for (int i = widget->get_child_count() - 1; i >= 0; --i) {
        IGuiWidget* result = hit_test_recursive(widget->get_child(i), pos);
        if (result) return result;
    }
    if (box_contains(widget->get_bounds(), pos)) {
        return widget;
    }
    return nullptr;
}

// ============================================================================
// Finish interactions → create commands
// ============================================================================

// Build the set of widgets to move during a drag.
// Only selected root widgets are tracked — children cascade automatically via set_bounds.
// Skips selected widgets whose ancestor is also selected (it will carry them).
void EditorCanvas::build_drag_all_bounds() {
    m_drag_all_bounds.clear();
    for (auto* w : m_selection.widgets) {
        bool ancestor_selected = false;
        IGuiWidget* p = w->get_parent();
        while (p && p != m_design_root) {
            if (m_selection.contains(p)) { ancestor_selected = true; break; }
            p = p->get_parent();
        }
        if (!ancestor_selected)
            m_drag_all_bounds.push_back({w, w->get_bounds()});
    }
}

void EditorCanvas::finish_move() {
    if (m_drag_all_bounds.empty() || !m_history) return;

    for (auto& [w, orig_bounds] : m_drag_all_bounds) {
        math::Box new_bounds = w->get_bounds();
        if (std::abs(x(box_min(new_bounds)) - x(box_min(orig_bounds))) > 0.01f ||
            std::abs(y(box_min(new_bounds)) - y(box_min(orig_bounds))) > 0.01f) {
            w->set_bounds(orig_bounds);
            m_history->execute(std::make_unique<MoveWidgetCommand>(w, orig_bounds, new_bounds));
        }
    }
}

void EditorCanvas::finish_resize() {
    IGuiWidget* primary = m_selection.primary();
    if (!primary || !m_history) return;

    math::Box new_bounds = primary->get_bounds();
    primary->set_bounds(m_drag_start_bounds);
    m_history->execute(std::make_unique<ResizeWidgetCommand>(primary, m_drag_start_bounds, new_bounds));
}

void EditorCanvas::finish_rubber_band(bool shift) {
    float rw = box_width(m_rubber_band);
    float rh = box_height(m_rubber_band);
    if (rw < MIN_RUBBER_BAND && rh < MIN_RUBBER_BAND) return;

    math::Vec2 rb_tl = screen_to_canvas(box_min(m_rubber_band));
    math::Vec2 rb_br = screen_to_canvas(box_max(m_rubber_band));

    if (!m_design_root) return;
    if (!shift) m_selection.clear();

    for (int i = 0; i < m_design_root->get_child_count(); ++i) {
        IGuiWidget* child = m_design_root->get_child(i);
        if (!child->is_visible()) continue;
        math::Box b = child->get_bounds();

        if (x(box_min(b)) <= x(rb_br) && x(box_max(b)) >= x(rb_tl) &&
            y(box_min(b)) <= y(rb_br) && y(box_max(b)) >= y(rb_tl)) {
            m_selection.add(child);
        }
    }

    m_rubber_band = {};
}

// ============================================================================
// Copy/Paste
// ============================================================================

void EditorCanvas::copy_selection() {
    if (m_selection.empty()) return;
    m_clipboard.clear();
    IGuiWidget* primary = m_selection.primary();
    if (primary) {
        GuiSerializeOptions opts;
        gui_save_widget(primary, m_clipboard, GuiSerializeFormat::Binary, opts);
    }
}

void EditorCanvas::paste(const math::Vec2& position) {
    if (m_clipboard.empty() || !m_ctx || !m_design_root) return;

    IGuiWidget* new_widget = nullptr;
    GuiSerializeOptions opts;
    opts.clear_before_load = false;
    GuiSerializeResult result = gui_load_widget(
        m_ctx, m_clipboard.data(), m_clipboard.size(),
        GuiSerializeFormat::Binary, &new_widget, opts
    );

    if (result == GuiSerializeResult::Success && new_widget) {
        math::Box b = new_widget->get_bounds();
        new_widget->set_bounds(make_box(x(position), y(position), box_width(b), box_height(b)));
        m_design_root->add_child(new_widget);
        m_selection.clear();
        m_selection.add(new_widget);
    }
}

void EditorCanvas::cut_selection() {
    copy_selection();
    delete_selection();
}

void EditorCanvas::nudge_selection(float ndx, float ndy) {
    if (m_selection.empty() || !m_history) return;

    for (auto* w : m_selection.widgets) {
        // Skip if an ancestor is also selected (it will carry this widget via cascade)
        bool ancestor_selected = false;
        IGuiWidget* p = w->get_parent();
        while (p && p != m_design_root) {
            if (m_selection.contains(p)) { ancestor_selected = true; break; }
            p = p->get_parent();
        }
        if (ancestor_selected) continue;

        math::Box old_bounds = w->get_bounds();
        math::Box new_bounds = make_box(
            x(box_min(old_bounds)) + ndx,
            y(box_min(old_bounds)) + ndy,
            box_width(old_bounds),
            box_height(old_bounds)
        );
        m_history->execute(std::make_unique<MoveWidgetCommand>(w, old_bounds, new_bounds));
    }
}

} // namespace editor
} // namespace gui
} // namespace window
