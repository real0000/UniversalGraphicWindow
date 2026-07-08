/*
 * gui_canvas.hpp - Pannable/zoomable world-coordinate canvas
 *
 * IGuiCanvasView is a container whose CONTENT children live in a world
 * coordinate system: the app parents ordinary widgets under content() with
 * world-space bounds and never repositions them for camera changes — panning
 * and zooming are a single set_view() call. The GUI system applies the view
 * transform when it collects render commands (positions, sizes, corner radii,
 * stroke widths and font sizes all scale; text below CanvasStyle::text_min_px
 * transformed pixels is culled) and inverse-transforms mouse positions when it
 * dispatches input, so world children hit-test correctly with no app math.
 *
 * Node editors / flowcharts / mind maps also need retained connection curves:
 * the canvas keeps a list of world-space WIRES (polyline waypoints rendered as
 * a smooth bezier chain with horizontal end tangents, plus optional draggable
 * waypoint handles) and a world grid + backdrop. Those are emitted through the
 * vector renderer by GpuGuiRenderer::render_window_frame() each time a frame
 * is actually drawn — the app only add_wire()s when its DATA changes.
 *
 * Everything visual is driven by CanvasStyle / CanvasWireStyle so other apps
 * can retune the look without touching the widget.
 */

#ifndef WINDOW_GUI_CANVAS_HPP
#define WINDOW_GUI_CANVAS_HPP

namespace window {
namespace gui {

// ============================================================================
// CanvasStyle - Canvas-wide visuals (backdrop, grid, text culling, rubber band)
// ============================================================================

struct CanvasStyle {
    math::Vec4 backdrop_color;      // canvas background fill
    math::Vec4 grid_color;          // world-grid line colour
    float      grid_spacing;        // world units between grid lines (0 = no grid)
    float      grid_min_scale;      // hide the grid below this view scale
    float      grid_line_px;        // grid line thickness in screen px
    float      text_min_px;         // cull content text under this many screen px (0 = never)
    math::Vec4 rubber_fill;         // rubber-band (marquee) interior
    math::Vec4 rubber_border;       // rubber-band 1 px border

    static CanvasStyle default_style() {
        CanvasStyle s;
        s.backdrop_color = color_rgba8(30, 31, 34);
        s.grid_color     = color_rgba8(44, 46, 50);
        s.grid_spacing   = 100.0f;
        s.grid_min_scale = 0.3f;
        s.grid_line_px   = 1.0f;
        s.text_min_px    = 6.0f;
        s.rubber_fill    = math::Vec4(0.35f, 0.55f, 0.9f, 0.18f);
        s.rubber_border  = color_rgba8(110, 150, 230);
        return s;
    }
};

// ============================================================================
// CanvasWireStyle / CanvasWire - Retained world-space connection curves
// ============================================================================

// Sizes are WORLD units unless suffixed _px; *_min_px clamps keep thin strokes
// and small handles visible when zoomed far out (non-scaling minimum).
struct CanvasWireStyle {
    math::Vec4 color;               // stroke colour
    float      width;               // stroke width (world units)
    float      min_width_px;        // screen-px floor for the stroke
    float      end_tangent_min;     // min horizontal end-tangent length (world units)
    bool       handles;             // draw a handle ring on interior waypoints
    float      handle_radius;       // handle outer radius (world units)
    float      handle_min_px;       // screen-px floor for the outer radius
    float      handle_hole_radius;  // handle inner "hole" radius (world units)
    float      handle_hole_min_px;  // screen-px floor for the hole radius
    math::Vec4 handle_hole_color;   // hole fill; alpha 0 = use CanvasStyle::backdrop_color

    static CanvasWireStyle default_style() {
        CanvasWireStyle s;
        s.color              = color_rgba8(204, 204, 204);
        s.width              = 2.0f;
        s.min_width_px       = 1.5f;
        s.end_tangent_min    = 30.0f;
        s.handles            = false;
        s.handle_radius      = 4.0f;
        s.handle_min_px      = 3.0f;
        s.handle_hole_radius = 2.0f;
        s.handle_hole_min_px = 1.5f;
        s.handle_hole_color  = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        return s;
    }

    bool operator==(const CanvasWireStyle& o) const {
        auto veq = [](const math::Vec4& a, const math::Vec4& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        };
        return veq(color, o.color) && width == o.width && min_width_px == o.min_width_px &&
               end_tangent_min == o.end_tangent_min && handles == o.handles &&
               handle_radius == o.handle_radius && handle_min_px == o.handle_min_px &&
               handle_hole_radius == o.handle_hole_radius &&
               handle_hole_min_px == o.handle_hole_min_px &&
               veq(handle_hole_color, o.handle_hole_color);
    }
    bool operator!=(const CanvasWireStyle& o) const { return !(*this == o); }
};

struct CanvasWire {
    int                     id = -1;
    std::vector<math::Vec2> points;   // world-space waypoints (>= 2 to draw)
    CanvasWireStyle         style = CanvasWireStyle::default_style();
};

// ============================================================================
// CanvasNode / CanvasPin / CanvasNodeStyle - retained world-space node cards
//
// A node editor hands the canvas a NODE MODEL (positions, titles, colours,
// pins) and the canvas renders each node as a rounded body + header + title +
// pin dots/labels + selection outline, entirely in world space — the app never
// creates or positions a single widget. Sizes are WORLD units; the view
// transform scales them to the screen and culls sub-`text_min_px` text.
// ============================================================================

struct CanvasNodeStyle {
    float      corner_radius;       // node body/header rounding (world units)
    float      header_height;       // header band height (world units)
    float      row_height;          // vertical spacing between pin rows (world units)
    float      pin_radius;          // pin dot radius (world units)
    float      title_font;          // header title font (world units → screen px)
    float      pin_font;            // pin label font (world units)
    float      title_pad;           // header title left inset (world units)
    float      pin_label_pad;       // pin label inset from the node edge (world units)
    math::Vec4 title_color;         // header title colour
    math::Vec4 pin_label_color;     // pin label colour
    float      selection_border;    // selection outline thickness (screen px)
    math::Vec4 selection_color;     // selection outline colour

    static CanvasNodeStyle default_style() {
        CanvasNodeStyle s;
        s.corner_radius   = 6.0f;
        s.header_height   = 24.0f;
        s.row_height      = 18.0f;
        s.pin_radius      = 5.0f;
        s.title_font      = 14.0f;
        s.pin_font        = 11.0f;
        s.title_pad       = 8.0f;
        s.pin_label_pad   = 8.0f;
        s.title_color     = color_rgba8(240, 240, 244);
        s.pin_label_color = color_rgba8(185, 188, 196);
        s.selection_border = 2.0f;
        s.selection_color  = color_rgba8(255, 200, 80);
        return s;
    }
};

struct CanvasPin {
    std::string name;                                  // label (empty = no label)
    math::Vec4  dot_color = color_rgba8(255, 255, 255);
    int         row = 0;                               // vertical slot under the header
    bool        output = false;                        // false = left/input, true = right/output

    bool operator==(const CanvasPin& o) const {
        return name == o.name && row == o.row && output == o.output &&
               dot_color.x == o.dot_color.x && dot_color.y == o.dot_color.y &&
               dot_color.z == o.dot_color.z && dot_color.w == o.dot_color.w;
    }
    bool operator!=(const CanvasPin& o) const { return !(*this == o); }
};

struct CanvasNode {
    std::string            id;                     // stable identity (for hit-testing/selection)
    math::Vec2             pos = math::Vec2(0.0f, 0.0f);   // world top-left
    float                  width = 0.0f;           // world
    float                  height = 0.0f;          // world
    std::string            title;
    math::Vec4             header_color = color_rgba8(70, 74, 86);
    math::Vec4             body_color   = color_rgba8(40, 42, 48);
    bool                   selected = false;
    std::vector<CanvasPin> pins;

    bool operator==(const CanvasNode& o) const {
        auto veq = [](const math::Vec4& a, const math::Vec4& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        };
        return id == o.id && math::x(pos) == math::x(o.pos) && math::y(pos) == math::y(o.pos) &&
               width == o.width && height == o.height && title == o.title &&
               veq(header_color, o.header_color) && veq(body_color, o.body_color) &&
               selected == o.selected && pins == o.pins;
    }
    bool operator!=(const CanvasNode& o) const { return !(*this == o); }
};

// ============================================================================
// CanvasView Interface - world-space container + view + wires + rubber band
// ============================================================================

class IGuiCanvasView : public IGuiWidget {
public:
    // Style
    virtual const CanvasStyle& get_canvas_style() const = 0;
    virtual void set_canvas_style(const CanvasStyle& style) = 0;

    // View: world point `origin` appears at the canvas's top-left corner;
    // `scale` is screen px per world unit. This is the ONLY call a camera
    // change needs — children, wires and grid all follow.
    virtual void set_view(const math::Vec2& origin, float scale) = 0;
    virtual math::Vec2 view_origin() const = 0;
    virtual float view_scale() const = 0;
    virtual math::Vec2 world_to_screen(const math::Vec2& world) const = 0;
    virtual math::Vec2 screen_to_world(const math::Vec2& screen) const = 0;

    // World-space content container: parent widgets here with WORLD bounds.
    // (Node editors should use set_nodes instead — see below — and leave this
    // for extra bespoke overlays.)
    virtual IGuiWidget* content() = 0;

    // Node model (retained; rebuild only when the graph data changes). The
    // canvas renders each node as a rounded body + header/title + pin dots and
    // labels + a selection outline, in world space, managing the widgets itself.
    // set_nodes is idempotent: an unchanged rebind schedules no repaint, so a
    // data binding may rebuild + rebind on every event. The style is shared by
    // all nodes (per-node colours live on CanvasNode).
    virtual const CanvasNodeStyle& get_node_style() const = 0;
    virtual void set_node_style(const CanvasNodeStyle& style) = 0;
    virtual void set_nodes(const std::vector<CanvasNode>& nodes) = 0;

    // Wires (retained; rebuild only when the underlying data changes)
    virtual int  add_wire(const std::vector<math::Vec2>& world_points,
                          const CanvasWireStyle& style) = 0;
    virtual void set_wire_points(int id, const std::vector<math::Vec2>& world_points) = 0;
    virtual void set_wire_style(int id, const CanvasWireStyle& style) = 0;
    virtual void remove_wire(int id) = 0;
    virtual void clear_wires() = 0;
    virtual int  wire_count() const = 0;
    virtual const CanvasWire& get_wire(int index) const = 0;   // by index, not id
    // Idempotent whole-list binding: replaces the wire list, but only marks the
    // canvas changed (→ repaint) when the content actually differs. Lets a data
    // binding rebuild + rebind on every event without scheduling redundant
    // repaints; ids in `wires` are ignored (reassigned).
    virtual void set_wires(const std::vector<CanvasWire>& wires) = 0;

    // Rubber band (marquee) — a screen-crisp selection rectangle drawn above
    // the content. Rect is WORLD space; the canvas tracks view changes itself.
    virtual void show_rubber_band(const math::Box& world_rect) = 0;
    virtual void hide_rubber_band() = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_CANVAS_HPP
