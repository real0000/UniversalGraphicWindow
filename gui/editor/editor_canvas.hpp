/*
 * editor_canvas.hpp - Editor Canvas with Selection and Manipulation
 *
 * The canvas is the main editing area where widgets are placed, selected,
 * moved, and resized. It provides:
 *   - Grid display and snapping
 *   - Widget selection (single and multi)
 *   - Move/resize handles with visual feedback
 *   - Rubber-band selection
 *   - Zoom and pan
 *   - Drop target for palette drag-and-drop
 */

#ifndef GUI_EDITOR_CANVAS_HPP
#define GUI_EDITOR_CANVAS_HPP

#include "gui/gui.hpp"
#include "editor_commands.hpp"
#include <vector>

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// Selection handle positions (8 corners/edges + center move)
// ============================================================================

enum class HandlePosition : uint8_t {
    None = 0,
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    Move        // Inside the widget body
};

// ============================================================================
// Canvas interaction mode
// ============================================================================

enum class CanvasMode : uint8_t {
    Idle = 0,
    Selecting,          // Click to select
    Moving,             // Dragging widget(s)
    Resizing,           // Dragging a resize handle
    RubberBand,         // Rubber-band selection box
    Panning,            // Middle-button pan
    DragFromPalette     // Dragging a new widget from palette
};

// ============================================================================
// Canvas grid settings
// ============================================================================

struct CanvasGrid {
    bool visible = true;
    bool snap_enabled = true;
    float spacing = 10.0f;
    float major_spacing = 50.0f;    // bold grid lines every N pixels
    math::Vec4 color = color_rgba8(50, 50, 55);
    math::Vec4 major_color = color_rgba8(60, 60, 65);

    // Snap a value to grid
    float snap(float v) const {
        if (!snap_enabled) return v;
        return std::round(v / spacing) * spacing;
    }

    math::Vec2 snap(const math::Vec2& p) const {
        return math::Vec2(snap(math::x(p)), snap(math::y(p)));
    }
};

// ============================================================================
// Canvas selection info
// ============================================================================

struct SelectionInfo {
    std::vector<IGuiWidget*> widgets;

    bool contains(IGuiWidget* w) const {
        for (auto* s : widgets) if (s == w) return true;
        return false;
    }

    void add(IGuiWidget* w) {
        if (!contains(w)) widgets.push_back(w);
    }

    void remove(IGuiWidget* w) {
        for (auto it = widgets.begin(); it != widgets.end(); ++it) {
            if (*it == w) { widgets.erase(it); return; }
        }
    }

    void clear() { widgets.clear(); }
    bool empty() const { return widgets.empty(); }
    int count() const { return static_cast<int>(widgets.size()); }

    IGuiWidget* primary() const { return widgets.empty() ? nullptr : widgets[0]; }

    // Compute bounding box of all selected widgets
    math::Box get_combined_bounds() const;
};

// ============================================================================
// EditorCanvas
// ============================================================================

class EditorCanvas {
public:
    void initialize(IGuiContext* ctx, CommandHistory* history);
    void shutdown();

    // The design root — widgets being edited are children of this
    void set_design_root(IGuiWidget* root) { m_design_root = root; }
    IGuiWidget* get_design_root() const { return m_design_root; }

    // Selection
    SelectionInfo& get_selection() { return m_selection; }
    const SelectionInfo& get_selection() const { return m_selection; }
    void select_widget(IGuiWidget* widget, bool add_to_selection = false);
    void clear_selection();
    void select_all();

    // Delete selected widgets
    void delete_selection();

    // Input handling (returns true if consumed)
    bool handle_mouse_down(MouseButton button, const math::Vec2& pos, bool shift, bool ctrl);
    bool handle_mouse_up(MouseButton button, const math::Vec2& pos);
    bool handle_mouse_move(const math::Vec2& pos);
    bool handle_mouse_scroll(float dx, float dy);
    bool handle_key_down(int key, bool ctrl, bool shift);

    // Canvas transform (zoom/pan)
    float get_zoom() const { return m_zoom; }
    void set_zoom(float z);
    math::Vec2 get_pan() const { return m_pan; }
    void set_pan(const math::Vec2& p) { m_pan = p; }
    void zoom_to_fit();
    void reset_view();

    // Transform between screen and canvas coordinates
    math::Vec2 screen_to_canvas(const math::Vec2& screen) const;
    math::Vec2 canvas_to_screen(const math::Vec2& canvas) const;

    // Grid
    CanvasGrid& get_grid() { return m_grid; }
    const CanvasGrid& get_grid() const { return m_grid; }

    // Current mode
    CanvasMode get_mode() const { return m_mode; }

    // Resize handle info for rendering
    HandlePosition get_active_handle() const { return m_active_handle; }

    // Rubber-band selection box (in screen coords)
    math::Box get_rubber_band_rect() const { return m_rubber_band; }

    // Drag preview position (for palette drag-and-drop)
    math::Vec2 get_drag_preview_pos() const { return m_drag_preview_pos; }

    // Canvas viewport bounds (set by the editor layout)
    void set_viewport_bounds(const math::Box& bounds) {
        float old_w = math::box_width(m_viewport_bounds);
        bool first = (old_w < 1.0f && math::box_width(bounds) > 1.0f);
        m_viewport_bounds = bounds;
        if (first) reset_view();
    }
    math::Box get_viewport_bounds() const { return m_viewport_bounds; }

    // Get render data for the selection handles
    struct HandleRect {
        math::Box rect;
        HandlePosition pos;
        bool hovered;
    };
    void get_selection_handles(std::vector<HandleRect>& out_handles) const;

    // Copy/paste
    void copy_selection();
    void paste(const math::Vec2& position);
    void cut_selection();
    bool has_clipboard() const { return !m_clipboard.empty(); }

    // Nudge selected widgets by delta
    void nudge_selection(float dx, float dy);

private:
    HandlePosition hit_test_handles(const math::Vec2& pos) const;
    IGuiWidget* hit_test_widget(const math::Vec2& canvas_pos) const;
    IGuiWidget* hit_test_recursive(IGuiWidget* widget, const math::Vec2& pos) const;
    // Build m_drag_all_bounds: selected root widgets (children cascade via set_bounds)
    void build_drag_all_bounds();
    void finish_move();
    void finish_resize();
    void finish_rubber_band(bool shift);

    IGuiContext* m_ctx = nullptr;
    CommandHistory* m_history = nullptr;
    IGuiWidget* m_design_root = nullptr;

    SelectionInfo m_selection;
    CanvasMode m_mode = CanvasMode::Idle;
    CanvasGrid m_grid;

    // View transform
    float m_zoom = 1.0f;
    math::Vec2 m_pan;
    math::Box m_viewport_bounds;

    // Interaction state
    HandlePosition m_active_handle = HandlePosition::None;
    HandlePosition m_hovered_handle = HandlePosition::None;
    math::Vec2 m_mouse_down_pos;
    math::Vec2 m_mouse_current_pos;
    math::Vec2 m_drag_start_canvas;
    math::Box m_drag_start_bounds;               // combined bounds at drag start
    // All widgets to move during a drag (selected + their descendants), with original bounds
    std::vector<std::pair<IGuiWidget*, math::Box>> m_drag_all_bounds;
    math::Box m_rubber_band;
    math::Vec2 m_drag_preview_pos;

    // Clipboard (serialized widget data)
    std::vector<uint8_t> m_clipboard;

    static constexpr float HANDLE_SIZE = 8.0f;
    static constexpr float MIN_RUBBER_BAND = 4.0f;
    static constexpr float MIN_WIDGET_SIZE = 10.0f;
    static constexpr float ZOOM_MIN = 0.1f;
    static constexpr float ZOOM_MAX = 5.0f;
    static constexpr float ZOOM_STEP = 0.1f;
    static constexpr float NUDGE_AMOUNT = 1.0f;
    static constexpr float NUDGE_LARGE = 10.0f;
};

} // namespace editor
} // namespace gui
} // namespace window

#endif // GUI_EDITOR_CANVAS_HPP
