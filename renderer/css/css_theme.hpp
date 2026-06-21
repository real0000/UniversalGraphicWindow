#pragma once
// CssTheme — a "looks like a web page" skin for the GUI.
//
// The GUI's appearance is decided entirely by each widget's *style struct*
// (GuiStyle, ButtonStyle, LabelStyle, …): the widget reads it when it emits its
// primitives, and the renderer just draws those primitives. So nothing here
// touches the renderer — CssTheme is purely a set of style presets coloured /
// sized to match HTML+CSS defaults (a light, Bootstrap-flavoured look), plus
// helpers that push those presets onto live widgets. Draw the result with the
// ordinary GpuGuiRenderer and the GUI comes out looking like a browser.
//
// Everything is built from a CssPalette, so recolouring the whole theme (or
// switching light/dark) is a one-liner. The factory getters return individual
// style structs for manual / partial use, and apply()/apply_one() push them onto
// a widget subtree — call them on any node to theme just that part of the UI.
//
// Note: a few widgets bake colours in rather than reading a style struct (e.g.
// TextInput's box background/border are hardcoded); for those only what *is*
// style-driven (TextInput text via LabelStyle) is themed.

#include "../../gui/gui.hpp"            // GuiStyle, LabelStyle, IGuiWidget, color_rgba8
#include "../../gui/gui_controls.hpp"   // ButtonStyle, SliderStyle, ProgressBarStyle, ColorPickerStyle
#include "../../gui/gui_label.hpp"      // EditBoxStyle (+ IGuiLabel)
#include "../../gui/gui_list.hpp"       // ListBoxStyle, ComboBoxStyle
#include "../../gui/gui_menu.hpp"       // MenuStyle, MenuBarStyle
#include "../../gui/gui_panel.hpp"      // SplitterStyle, DockPanelStyle
#include "../../gui/gui_tab.hpp"        // TabStyle
#include "../../gui/gui_toolbar.hpp"    // ToolbarStyle, StatusBarStyle
#include "../../gui/gui_tree.hpp"       // TreeViewStyle
#include "../../gui/gui_scroll.hpp"     // ScrollBarStyle
#include "../../gui/gui_property.hpp"   // PropertyGridStyle
#include "../../gui/gui_dialog.hpp"     // DialogStyle
#include "../../gui/gui_page.hpp"       // PageStyle

namespace window {
namespace gui {

class IGuiContext;   // for apply_defaults() (defined in gui_context.hpp)

// The colour + metric palette every preset is derived from. Tweak a field and
// re-fetch the styles to recolour the whole theme. Defaults are CssPalette::light().
struct CssPalette {
    math::Vec4 page_bg;        // window / page background (white)
    math::Vec4 surface;        // raised surfaces: panels, toolbars, headers
    math::Vec4 hover_surface;  // hovered neutral surface
    math::Vec4 text;           // primary text
    math::Vec4 text_muted;     // secondary / placeholder text
    math::Vec4 border;         // hairline borders / separators
    math::Vec4 border_strong;  // emphasised borders (inputs, gray buttons)
    math::Vec4 primary;        // accent: links, selection, primary actions
    math::Vec4 primary_hover;
    math::Vec4 primary_active;
    math::Vec4 on_primary;     // text/icons on an accent fill
    math::Vec4 focus;          // focus ring colour
    math::Vec4 selection;      // translucent text/row selection highlight
    math::Vec4 track;          // slider / progress / scrollbar track

    float radius          = 8.0f;   // cards, panels, menus, dialogs
    float control_radius  = 6.0f;   // buttons, inputs, combos, tabs
    float border_width    = 1.0f;
    float font_size       = 14.0f;
    const char* font_name = nullptr;  // text font; nullptr → rasterizer default

    static CssPalette light();
    static CssPalette dark();
};

class CssTheme {
public:
    CssTheme() : p_(CssPalette::light()) {}
    explicit CssTheme(const CssPalette& p) : p_(p) {}

    static CssTheme light() { return CssTheme(CssPalette::light()); }
    static CssTheme dark()  { return CssTheme(CssPalette::dark());  }

    const CssPalette& palette() const { return p_; }

    // ── Per-widget-type presets ──────────────────────────────────────────────
    // Use these to style a single widget by hand (the most granular "partial"
    // application), or as a starting point you tweak before set_*_style().
    GuiStyle          gui_style()           const;   // base — panels / containers
    LabelStyle        label_style()         const;   // Label + TextInput text
    ButtonStyle       button_style()        const;   // neutral (gray) button + checkbox/radio
    ButtonStyle       button_primary_style()const;   // accent (blue) call-to-action button
    SliderStyle       slider_style()        const;
    ProgressBarStyle  progress_bar_style()  const;
    ColorPickerStyle  color_picker_style()  const;
    EditBoxStyle      editbox_style()       const;
    ListBoxStyle      list_box_style()      const;
    ComboBoxStyle     combo_box_style()     const;
    MenuStyle         menu_style()          const;
    MenuBarStyle      menu_bar_style()      const;
    SplitterStyle     splitter_style()      const;
    DockPanelStyle    dock_panel_style()    const;
    TabStyle          tab_style()           const;
    ToolbarStyle      toolbar_style()       const;
    StatusBarStyle    status_bar_style()    const;
    TreeViewStyle     tree_view_style()     const;
    ScrollBarStyle    scrollbar_style()     const;
    PropertyGridStyle property_grid_style() const;
    DialogStyle       dialog_style()        const;
    PageStyle         page_style()          const;

    // ── Application helpers (partial-friendly) ───────────────────────────────
    // Style just `w` according to its concrete type. Returns true if a style was
    // applied (always true for a non-null widget — unknown types get the base
    // GuiStyle). Does not touch children.
    bool apply_one(IGuiWidget* w) const;

    // Style `root`, then (when recursive) every descendant. Pass a subtree root
    // to theme only that part of the UI. Returns the number of widgets styled.
    int apply(IGuiWidget* root, bool recursive = true) const;

    // Seed a context's defaults so *newly created* widgets inherit the look.
    // Covers what the context exposes (base GuiStyle + LabelStyle); per-widget
    // structs (ButtonStyle, …) still need apply()/set_*_style on the instances.
    void apply_defaults(IGuiContext* ctx) const;

private:
    CssPalette p_;
};

} // namespace gui
} // namespace window
