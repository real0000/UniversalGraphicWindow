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

// CanvasStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

// ============================================================================
// CanvasWireStyle / CanvasWire - Retained world-space connection curves
// ============================================================================

// Sizes are WORLD units unless suffixed _px; *_min_px clamps keep thin strokes
// and small handles visible when zoomed far out (non-scaling minimum).
// CanvasWireStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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

// Geometry only — the title, pin-label and selection COLOURS are semantic theme
// roles (GuiColor::NodeTitle / PinLabel / Selection), resolved by the context from
// its palette at collect time; a node's header/body/pin-dot colours stay per-node
// DATA on CanvasNode/CanvasPin. So this struct holds no chrome colour value.
// CanvasNodeStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

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

    // Bind node/wire data providers (set ONCE): the canvas re-reads them before each
    // render (via refresh_bindings) and set_nodes()/set_wires() the result — so the
    // app mutates its graph + asks for a repaint and the canvas reacts, without the
    // app pushing on every change. Pass {} to clear.
    virtual void bind_nodes(std::function<std::vector<CanvasNode>()> provider) = 0;
    virtual void bind_wires(std::function<std::vector<CanvasWire>()> provider) = 0;

    // Rubber band (marquee) — a screen-crisp selection rectangle drawn above
    // the content. Rect is WORLD space; the canvas tracks view changes itself.
    virtual void show_rubber_band(const math::Box& world_rect) = 0;
    virtual void hide_rubber_band() = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_CANVAS_HPP
