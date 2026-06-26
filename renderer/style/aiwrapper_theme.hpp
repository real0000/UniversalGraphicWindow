#pragma once
// AIWrapper chat — the dark UI style preset, centralized here in renderer/style
// (formerly renderer/css). Widgets pull their decoration (colours, radii, borders,
// fonts) from one place instead of each call site hand-setting a *Style struct.
//
// All metrics are PHYSICAL px: pass the chat's dpi (and the already-scaled font /
// line-height it computed) so the returned styles are ready for the chat's
// physical-px renderer with no further scaling at the call site.

#include "../../gui/gui.hpp"            // color_rgba8, math::Vec4
#include "../../gui/gui_controls.hpp"   // ButtonStyle
#include "../../gui/gui_label.hpp"      // EditBoxStyle
#include "../../gui/gui_menu.hpp"       // MenuStyle
#include "../../gui/gui_tree.hpp"       // TreeViewStyle

namespace window {
namespace gui {
namespace aiw {   // AIWrapper preset

// Project Explorer file tree — VSCode-ish dark, folders lighter than files.
inline TreeViewStyle tree_style(float dpi, float font_px, float line_h) {
    TreeViewStyle s = TreeViewStyle::default_style();
    s.row_background      = color_rgba8(34, 35, 39);
    s.row_alt_background  = color_rgba8(34, 35, 39);
    s.selected_background = color_rgba8(48, 56, 72);
    s.hover_background    = color_rgba8(44, 46, 52);
    s.text_color          = color_rgba8(184, 186, 194);   // files
    s.folder_text_color   = color_rgba8(208, 210, 216);   // folders
    s.icon_color          = color_rgba8(150, 152, 160);
    s.font_size    = font_px;
    s.row_height   = line_h + 4.0f * dpi;
    s.indent_width = 12.0f * dpi;
    s.show_lines   = false;
    return s;
}

// ≡ / right-click context menus.
inline MenuStyle menu_style(float dpi, float font_px) {
    MenuStyle s = MenuStyle::default_style();
    s.background_color          = color_rgba8(46, 48, 54);
    s.border_color             = color_rgba8(70, 72, 80);
    s.item_text_color          = color_rgba8(220, 222, 228);
    s.item_hover_background     = color_rgba8(58, 62, 72);
    s.item_hover_text_color     = color_rgba8(236, 238, 242);
    s.item_disabled_text_color  = color_rgba8(112, 114, 120);
    s.separator_color          = color_rgba8(60, 62, 68);
    s.item_height     = 22.0f * dpi;
    s.separator_height = 7.0f * dpi;
    s.font_size       = font_px;
    s.min_width       = 150.0f * dpi;
    return s;
}

// Read-only file preview (multiline editbox).
inline EditBoxStyle preview_style(float dpi, float font_px) {
    EditBoxStyle s = EditBoxStyle::default_style();
    s.background_color = color_rgba8(37, 37, 40);
    s.text_color       = color_rgba8(206, 208, 214);
    s.selection_color  = color_rgba8(54, 80, 130);
    s.font_size        = font_px;
    s.padding          = 12.0f * dpi;
    return s;
}

// Transparent toolbar/icon button: no fill until hovered (the old `tbtn`).
inline ButtonStyle button_ghost(float dpi, float font_px, const math::Vec4& fg) {
    ButtonStyle s = ButtonStyle::default_style();
    s.background_color = math::Vec4(0, 0, 0, 0);
    s.hover_color      = color_rgba8(58, 62, 70);
    s.pressed_color    = color_rgba8(50, 54, 62);
    s.text_color       = fg;
    s.border_color     = math::Vec4(0, 0, 0, 0);   // borderless (widget skips the outline)
    s.border_width     = 0.0f;
    s.corner_radius    = 4.0f * dpi;
    s.font_size        = font_px;
    return s;
}

// Filled action button (Send / Stop / staged-count).
inline ButtonStyle button_filled(float dpi, float font_px, const math::Vec4& bg,
                                 const math::Vec4& hover, const math::Vec4& fg) {
    ButtonStyle s = ButtonStyle::default_style();
    s.background_color = bg;
    s.hover_color      = hover;
    s.pressed_color    = bg;
    s.text_color       = fg;
    s.border_color     = math::Vec4(0, 0, 0, 0);
    s.border_width     = 0.0f;
    s.corner_radius    = 5.0f * dpi;
    s.font_size        = font_px;
    return s;
}

}  // namespace aiw
}  // namespace gui
}  // namespace window
