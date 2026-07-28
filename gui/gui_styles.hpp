/*
 * gui_styles.hpp - Centralized widget style structs (geometry + colours)
 *
 * Every widget's visual STYLE struct lives here in one place, instead of being
 * defined inline next to each widget interface. Each struct is plain data — the
 * geometry (sizes, radii, paddings) plus the colours a widget still owns as
 * values (chrome colours a theme owns are semantic GuiColor roles; see
 * gui_theme.hpp). The default_style() presets are OUT of line in gui_styles.cpp,
 * so this header carries no colour literals and no dependency on color_rgba8 —
 * only the field layout. This makes retuning the default look a single .cpp edit.
 *
 * Include order: this header is pulled in by gui.hpp AFTER the core enums
 * (Alignment …) and GuiColor, and BEFORE the widget interface headers — so both
 * the widget interfaces and the sub-headers at the bottom of gui.hpp see the
 * style structs. It is not meant to be included standalone (same convention as
 * the other gui_*.hpp sub-headers).
 */

#ifndef WINDOW_GUI_STYLES_HPP
#define WINDOW_GUI_STYLES_HPP

#include "../math_util.hpp"
#include "gui_theme.hpp"   // GuiColor semantic roles
#include "gui_enums.hpp"   // Alignment (used by ButtonStyle/EditBoxStyle/LabelStyle)

namespace window {
namespace gui {

// ── Core widget style (any widget) + text/label style ────────────────────────

struct GuiStyle {
    // Colors (math::Vec4: x=r, y=g, z=b, w=a)
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 hover_color;
    math::Vec4 pressed_color;
    math::Vec4 disabled_color;
    math::Vec4 focus_color;

    // Semantic role for the background fill. When != None the context resolves the
    // background colour from its theme at collect time (background_color is ignored),
    // so a plain container/panel themes without holding a colour value. Default None
    // keeps the literal background_color (behaviour-neutral).
    GuiColor background_role = GuiColor::None;

    // Sizing
    float border_width = 1.0f;
    float corner_radius = 0.0f;
    math::Vec4 padding;       // x=left, y=top, z=right, w=bottom
    math::Vec4 margin;        // x=left, y=top, z=right, w=bottom

    static GuiStyle default_style();
};

struct LabelStyle {
    math::Vec4 text_color;
    math::Vec4 selection_color;       // For TextInput
    float font_size = 14.0f;
    const char* font_name = nullptr;
    Alignment alignment = Alignment::CenterLeft;
    bool wrap = false;          // Word wrap
    bool ellipsis = false;      // Truncate with "..."

    static LabelStyle default_style();
};

// ── Text entry (single-line input + multi-line edit box) ─────────────────────

struct TextInputStyle {
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 focus_border_color;   // drawn only when alpha > 0 (0 = no focus ring)
    math::Vec4 text_color;
    math::Vec4 placeholder_color;
    math::Vec4 selection_color;
    math::Vec4 cursor_color;
    float font_size    = 14.0f;
    float corner_radius = 0.0f;       // > 0 → rounded background
    float padding       = 6.0f;       // left/right text inset
    bool  hide_placeholder_on_focus = false;  // false = show placeholder even when focused+empty

    static TextInputStyle default_style();
};

struct EditBoxStyle {
    math::Vec4 background_color;
    math::Vec4 text_color;
    math::Vec4 selection_color;
    math::Vec4 cursor_color;
    math::Vec4 line_number_background;
    math::Vec4 line_number_color;
    math::Vec4 current_line_highlight;
    math::Vec4 border_color;
    math::Vec4 gutter_border_color;
    float font_size = 13.0f;
    const char* font_name = nullptr;    // Monospace recommended
    float line_height = 1.2f;           // Multiplier of font_size
    float gutter_width = 50.0f;         // Line number gutter
    float padding = 4.0f;
    float cursor_width = 2.0f;
    float tab_width = 4.0f;             // Tab width in spaces
    Alignment text_alignment = Alignment::CenterLeft;   // vertical placement of each line's text

    static EditBoxStyle default_style();
};

// ── Controls (button + presets, slider, progress bar, colour picker) ─────────

// Ready-made button looks (fill/hover/border presets); see ButtonStyle::from_preset.
enum class ButtonStylePreset : uint8_t {
    Default = 0,
    Primary,     // Blue accent
    Success,     // Green
    Warning,     // Orange
    Danger,      // Red
    Ghost,       // Transparent fill, visible border
    Flat         // No border, subtle hover
};

struct ButtonStyle {
    math::Vec4 background_color;
    math::Vec4 hover_color;
    math::Vec4 pressed_color;
    math::Vec4 disabled_color;
    math::Vec4 checked_color;           // For toggle/checkbox/radio when checked
    math::Vec4 text_color;
    math::Vec4 text_disabled_color;
    math::Vec4 border_color;
    math::Vec4 focus_border_color;
    float border_width = 1.0f;
    float corner_radius = 4.0f;
    float padding = 8.0f;
    float icon_size = 16.0f;
    float icon_text_spacing = 6.0f;
    float font_size = 13.0f;
    Alignment text_alignment = Alignment::Center;   // label placement within the button

    static ButtonStyle default_style();
    static ButtonStyle from_preset(ButtonStylePreset preset);
};

struct SliderStyle {
    math::Vec4 track_color;
    math::Vec4 track_fill_color;
    math::Vec4 thumb_color;
    math::Vec4 thumb_hover_color;
    math::Vec4 thumb_pressed_color;
    math::Vec4 tick_color;
    float track_height = 4.0f;
    float thumb_radius = 7.0f;
    float tick_length = 6.0f;
    float tick_width = 1.0f;
    float track_corner_radius = 2.0f;

    static SliderStyle default_style();
};

struct ProgressBarStyle {
    math::Vec4 track_color;
    math::Vec4 fill_color;
    math::Vec4 indeterminate_color;
    math::Vec4 text_color;
    float height = 20.0f;
    float corner_radius = 4.0f;
    float indeterminate_width = 0.3f;   // Width of indeterminate bar as ratio

    static ProgressBarStyle default_style();
};

struct ColorPickerStyle {
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 label_color;
    math::Vec4 input_background;
    math::Vec4 input_text_color;
    math::Vec4 swatch_border_color;
    math::Vec4 selector_color;          // Ring/crosshair on color area
    float wheel_outer_radius = 100.0f;
    float wheel_inner_radius = 80.0f;
    float sv_square_size = 140.0f;
    float slider_height = 18.0f;
    float swatch_size = 20.0f;
    float swatch_spacing = 4.0f;
    float alpha_checker_size = 6.0f;
    float selector_radius = 5.0f;
    float preview_height = 30.0f;
    float font_size = 12.0f;
    float padding = 8.0f;

    static ColorPickerStyle default_style();
};

// ── Scroll bar ───────────────────────────────────────────────────────────────

struct ScrollBarStyle {
    math::Vec4 track_color;
    math::Vec4 thumb_color;
    math::Vec4 thumb_hover_color;
    math::Vec4 thumb_pressed_color;
    float track_width = 12.0f;
    float thumb_min_length = 20.0f;
    float corner_radius = 6.0f;

    static ScrollBarStyle default_style();
};

// ── Property grid ─────────────────────────────────────────────────────────────

struct PropertyGridStyle {
    math::Vec4 category_background;
    math::Vec4 category_text_color;
    math::Vec4 name_text_color;
    math::Vec4 value_text_color;
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 separator_color;
    float row_height = 24.0f;
    float name_column_width = 150.0f;
    float indent_width = 16.0f;
    float font_size = 13.0f;
    // ── Stacked (form/inspector) layout: the label renders ABOVE a boxed value
    // field instead of the two-column table. All *_ fields below apply only when
    // stacked; the table look is unchanged (stacked defaults false).
    bool  stacked = false;
    math::Vec4 field_background;        // value box fill
    math::Vec4 field_border_color;      // value box outline (alpha 0 = none)
    float field_corner_radius = 4.0f;
    float field_height = 26.0f;         // value box height
    float label_height = 16.0f;         // label line height above the box
    float label_font   = 11.0f;         // label font px (value text uses font_size)
    float row_gap      = 8.0f;          // gap below each field
    float side_padding = 10.0f;         // left/right content inset

    static PropertyGridStyle default_style();
};

// ── List box + combo box ─────────────────────────────────────────────────────

struct ListBoxStyle {
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 hover_background;
    math::Vec4 text_color;
    math::Vec4 selected_text_color;
    math::Vec4 disabled_text_color;   // dimmer text for disabled rows (e.g. group headers)
    math::Vec4 icon_color;
    math::Vec4 separator_color;
    math::Vec4 background_color;      // full-box fill behind the rows (alpha 0 = none)
    math::Vec4 border_color;          // widget outline (alpha 0 = none)
    // Trailing "×" action affordance: filled button look when action_background has
    // alpha (rounded rect of action_size, action_text_color glyph); plain glyph otherwise.
    math::Vec4 action_background;
    math::Vec4 action_text_color;
    float action_size = 0.0f;         // button square edge (0 = row_height)
    float action_corner_radius = 3.0f;
    float row_height = 24.0f;
    float row_gap = 0.0f;             // vertical gap between rows (card look)
    float row_corner_radius = 0.0f;   // rounded row card (0 = square strip)
    float icon_size = 16.0f;
    float item_padding = 8.0f;
    float font_size = 13.0f;
    bool show_separator = false;

    static ListBoxStyle default_style();
};

// ChoiceCard: a prompt plus a set of answer options (gui_choice.hpp).
struct ChoiceCardStyle {
    math::Vec4 background_color;      // card fill (alpha 0 = none)
    math::Vec4 border_color;          // card outline (alpha 0 = none)
    math::Vec4 prompt_color;          // the question text
    math::Vec4 answer_color;          // the resolved answer line
    float corner_radius = 4.0f;
    float padding = 8.0f;             // card inset around prompt + options
    float prompt_gap = 6.0f;          // prompt -> options
    float prompt_font_size = 13.0f;
    float prompt_line_height = 1.35f; // multiple of prompt_font_size
    float option_height = 24.0f;
    float option_gap = 4.0f;
    float editor_height = 24.0f;      // inline free-text editor row

    static ChoiceCardStyle default_style();
};

struct ComboBoxStyle {
    math::Vec4 background_color;
    math::Vec4 hover_background;
    math::Vec4 open_background;
    math::Vec4 text_color;
    math::Vec4 placeholder_color;
    math::Vec4 arrow_color;
    math::Vec4 dropdown_background;
    math::Vec4 dropdown_border_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_selected_background;
    math::Vec4 item_text_color;
    math::Vec4 item_selected_text_color;
    float height = 28.0f;
    float dropdown_max_height = 200.0f;
    float item_height = 24.0f;
    float item_padding = 8.0f;
    float arrow_size = 10.0f;
    float icon_size = 16.0f;
    float corner_radius = 4.0f;
    float dropdown_corner_radius = 4.0f;
    float border_width = 1.0f;
    float font_size = 13.0f;

    static ComboBoxStyle default_style();
};

// ── Canvas (backdrop/grid, wire, node card) ──────────────────────────────────

struct CanvasStyle {
    math::Vec4 backdrop_color;      // canvas background fill
    math::Vec4 grid_color;          // world-grid line colour
    float      grid_spacing;        // world units between grid lines (0 = no grid)
    float      grid_min_scale;      // hide the grid below this view scale
    float      grid_line_px;        // grid line thickness in screen px
    float      text_min_px;         // cull content text under this many screen px (0 = never)
    math::Vec4 rubber_fill;         // rubber-band (marquee) interior
    math::Vec4 rubber_border;       // rubber-band 1 px border

    static CanvasStyle default_style();
};

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

    static CanvasWireStyle default_style();

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

// Geometry only — the title, pin-label and selection COLOURS are semantic theme
// roles (GuiColor::NodeTitle / PinLabel / Selection), resolved by the context from
// its palette at collect time; a node's header/body/pin-dot colours stay per-node
// DATA on CanvasNode/CanvasPin. So this struct holds no chrome colour value.
struct CanvasNodeStyle {
    float      corner_radius;       // node body/header rounding (world units)
    float      header_height;       // header band height (world units)
    float      row_height;          // vertical spacing between pin rows (world units)
    float      pin_radius;          // pin dot radius (world units)
    float      title_font;          // header title font (world units → screen px)
    float      pin_font;            // pin label font (world units)
    float      title_pad;           // header title left inset (world units)
    float      pin_label_pad;       // pin label inset from the node edge (world units)
    float      selection_border;    // selection outline thickness (world units)

    static CanvasNodeStyle default_style();
};

// ── Panel (collapse section, splitter, dock) ─────────────────────────────────

// Geometry + semantic colour ROLES only (no colour values): the context resolves
// each role from its theme at collect time, so a re-theme is a palette swap. Set a
// role to GuiColor::None to hide that element (e.g. no header border).
struct CollapseSectionStyle {
    GuiColor    header_role        = GuiColor::ConsoleBarFill;   // header bar fill
    GuiColor    header_border_role = GuiColor::PanelBorder;      // 1px line above the header
    GuiColor    header_text_role   = GuiColor::TextMuted;        // header title text
    GuiColor    body_role          = GuiColor::ConsoleBodyFill;  // body fill behind content
    float       header_height = 22.0f;
    float       header_border_px = 1.0f;
    float       header_pad_x = 8.0f;
    float       font_size = 12.0f;
    const char* glyph_collapsed = "\xE2\x96\xB8";   // ▸
    const char* glyph_expanded  = "\xE2\x96\xBE";   // ▾

    static CollapseSectionStyle default_style() { return CollapseSectionStyle(); }
};

struct SplitterStyle {
    math::Vec4 splitter_color;
    math::Vec4 splitter_hover_color;
    math::Vec4 splitter_drag_color;
    math::Vec4 grip_color;
    float splitter_thickness = 4.0f;
    float hit_area_thickness = 8.0f;    // Larger than visual for easier grabbing
    float grip_length = 30.0f;
    float grip_dot_size = 2.0f;
    int grip_dot_count = 3;

    static SplitterStyle default_style();
};

struct DockPanelStyle {
    math::Vec4 background_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_bar_active_color;
    math::Vec4 title_text_color;
    math::Vec4 title_active_text_color;
    math::Vec4 tab_bar_color;
    math::Vec4 drop_indicator_color;
    math::Vec4 auto_hide_tab_color;
    float title_bar_height = 26.0f;
    float tab_height = 24.0f;
    float auto_hide_tab_width = 24.0f;
    float min_dock_width = 100.0f;
    float min_dock_height = 80.0f;
    float drop_indicator_thickness = 3.0f;
    float font_size = 12.0f;

    static DockPanelStyle default_style();
};

// ── Menu + menu bar ──────────────────────────────────────────────────────────

struct MenuStyle {
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 item_text_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_hover_text_color;
    math::Vec4 item_disabled_text_color;
    math::Vec4 separator_color;
    math::Vec4 shortcut_text_color;
    math::Vec4 check_color;
    math::Vec4 submenu_arrow_color;
    math::Vec4 shadow_color;
    float item_height = 26.0f;
    float separator_height = 7.0f;
    float item_padding = 24.0f;
    float icon_size = 16.0f;
    float icon_column_width = 28.0f;
    float shortcut_margin = 40.0f;
    float submenu_arrow_size = 8.0f;
    float corner_radius = 4.0f;
    float border_width = 1.0f;
    float shadow_offset = 2.0f;
    float shadow_blur = 6.0f;
    float min_width = 140.0f;
    float font_size = 13.0f;

    static MenuStyle default_style();
};

struct MenuBarStyle {
    math::Vec4 background_color;
    math::Vec4 item_text_color;
    math::Vec4 item_hover_background;
    math::Vec4 item_hover_text_color;
    math::Vec4 item_open_background;
    float height = 28.0f;
    float item_padding = 10.0f;
    float font_size = 13.0f;

    static MenuBarStyle default_style();
};

// ── Page ─────────────────────────────────────────────────────────────────────

struct PageStyle {
    math::Vec4 background_color;
    math::Vec4 overlay_color;           // For modal overlay
    float overlay_opacity = 0.5f;
    bool enable_gesture_navigation = true;  // Swipe to go back
    float gesture_threshold = 0.3f;     // Swipe distance ratio to trigger navigation

    static PageStyle default_style();
};

// ── Dialog ───────────────────────────────────────────────────────────────────

struct DialogStyle {
    math::Vec4 overlay_color;           // Dimmed background behind modal
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_text_color;
    math::Vec4 shadow_color;
    float border_width = 1.0f;
    float corner_radius = 6.0f;
    float title_bar_height = 32.0f;
    float button_area_height = 44.0f;
    float padding = 16.0f;
    float shadow_offset = 4.0f;
    float shadow_blur = 8.0f;
    float min_width = 300.0f;
    float min_height = 150.0f;
    float font_size = 13.0f;
    float title_font_size = 14.0f;

    static DialogStyle default_style();
};

// ── Tab control ──────────────────────────────────────────────────────────────

struct TabStyle {
    math::Vec4 tab_background;
    math::Vec4 tab_hover_background;
    math::Vec4 tab_active_background;
    math::Vec4 tab_text_color;
    math::Vec4 tab_active_text_color;
    math::Vec4 tab_bar_background;
    math::Vec4 indicator_color;         // Active tab indicator line
    math::Vec4 close_button_color;
    math::Vec4 close_button_hover_color;
    float tab_height = 30.0f;
    float tab_min_width = 60.0f;
    float tab_max_width = 200.0f;
    float tab_padding = 12.0f;
    float indicator_height = 2.0f;
    float icon_size = 16.0f;
    float close_button_size = 14.0f;
    float corner_radius = 0.0f;
    float font_size = 13.0f;

    static TabStyle default_style();
};

// ── Tree view ────────────────────────────────────────────────────────────────

struct TreeViewStyle {
    math::Vec4 row_background;
    math::Vec4 row_alt_background;
    math::Vec4 selected_background;
    math::Vec4 hover_background;
    math::Vec4 text_color;
    math::Vec4 folder_text_color;   // text color for nodes with icon "folder" (0 alpha → use text_color)
    math::Vec4 icon_color;
    math::Vec4 line_color;          // Indent guide lines
    float row_height = 22.0f;
    float indent_width = 18.0f;
    float icon_size = 16.0f;
    float font_size = 13.0f;
    bool show_lines = true;         // Draw indent guide lines
    bool show_root_lines = false;   // Draw lines from root nodes

    static TreeViewStyle default_style();
};

// ── Toolbar + status bar ─────────────────────────────────────────────────────

struct ToolbarStyle {
    math::Vec4 background_color;
    math::Vec4 button_color;
    math::Vec4 button_hover_color;
    math::Vec4 button_pressed_color;
    math::Vec4 button_toggled_color;
    math::Vec4 button_disabled_color;
    math::Vec4 icon_color;
    math::Vec4 icon_disabled_color;
    math::Vec4 separator_color;
    math::Vec4 overflow_button_color;
    float button_size = 28.0f;
    float icon_size = 16.0f;
    float separator_width = 1.0f;
    float separator_padding = 4.0f;
    float button_padding = 2.0f;
    float button_corner_radius = 4.0f;
    float toolbar_padding = 4.0f;

    static ToolbarStyle default_style();
};

struct StatusBarStyle {
    math::Vec4 background_color;
    math::Vec4 text_color;
    math::Vec4 separator_color;
    math::Vec4 hover_background;
    math::Vec4 icon_color;
    float height = 24.0f;
    float panel_padding = 8.0f;
    float separator_width = 1.0f;
    float icon_size = 14.0f;
    float font_size = 12.0f;

    static StatusBarStyle default_style();
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_STYLES_HPP
