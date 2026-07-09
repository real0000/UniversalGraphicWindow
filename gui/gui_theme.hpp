/*
 * gui_theme.hpp - Semantic colour roles + palette (theme)
 *
 * Widgets never hold colour VALUES. They tag the geometry they emit with a
 * semantic ROLE (GuiColor); the context resolves the role to a concrete colour
 * through its GuiTheme when it collects the frame. This keeps appearance out of
 * the widget layer entirely — a widget knows "this is a panel background" or
 * "this is muted text", not what colour that is — so retheming is a palette
 * swap with no widget changes. Per-item DATA colours (a node's header colour, a
 * log line's colour) stay data and are pushed literally; roles are for the
 * uniform chrome/decoration colours a theme owns.
 *
 * The default palette matches the AIWrapper dark look; an app installs its own
 * via IGuiContext::set_theme (values centralized in renderer/style/).
 */

#ifndef WINDOW_GUI_THEME_HPP
#define WINDOW_GUI_THEME_HPP

#include <cstdint>
#include "../math_util.hpp"

namespace window {
namespace gui {

// Local rgba helper (gui.hpp's color_rgba8 is defined later; this header is
// included before it so it carries its own).
inline math::Vec4 theme_rgb8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return math::Vec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

// Semantic colour roles. Extend as widgets need; keep values stable (they are
// stored in draw commands). kCount must stay last.
enum class GuiColor : int16_t {
    None = -1,          // literal colour (not themed)

    Backdrop = 0,       // canvas / deepest surface
    GridLine,           // world grid
    PanelFill,          // side-panel / dock surface
    PanelBorder,        // 1px separators
    ToolbarFill,        // top toolbar strip
    BreadcrumbFill,     // breadcrumb strip
    ConsoleBodyFill,    // console log body
    ConsoleBarFill,     // console title bar
    StatusFill,         // bottom status strip

    Text,               // primary text
    TextMuted,          // secondary / label text
    TextDim,            // tertiary / disabled text
    TextBright,         // emphasised text (titles)

    ButtonFill,         // neutral button surface
    ButtonHover,        // neutral button hover
    ButtonText,         // button label
    Accent,             // primary accent (links, active)
    AccentText,         // text on accent fills

    RowHover,           // list/menu row hover
    RowSelected,        // list/menu row selected
    Selection,          // selection outline / marquee accent

    NodeTitle,          // node header title text
    PinLabel,           // node pin label text

    Danger,             // error text/fills
    Warning,            // warning text/fills
    Success,            // ok/positive text/fills

    kCount
};

struct GuiTheme {
    math::Vec4 colors[static_cast<int>(GuiColor::kCount)];

    math::Vec4 get(GuiColor role) const {
        const int i = static_cast<int>(role);
        return (i >= 0 && i < static_cast<int>(GuiColor::kCount)) ? colors[i]
                                                                  : math::Vec4(0, 0, 0, 0);
    }
    void set(GuiColor role, const math::Vec4& c) {
        const int i = static_cast<int>(role);
        if (i >= 0 && i < static_cast<int>(GuiColor::kCount)) colors[i] = c;
    }

    // AIWrapper dark palette (matches the pre-theme hard-coded colours so a
    // themed widget renders identically). Apps may override individual roles.
    static GuiTheme dark() {
        GuiTheme t;
        t.set(GuiColor::Backdrop,        theme_rgb8(30, 31, 34));
        t.set(GuiColor::GridLine,        theme_rgb8(44, 46, 50));
        t.set(GuiColor::PanelFill,       theme_rgb8(37, 37, 40));
        t.set(GuiColor::PanelBorder,     theme_rgb8(62, 62, 68));
        t.set(GuiColor::ToolbarFill,     theme_rgb8(26, 26, 28));
        t.set(GuiColor::BreadcrumbFill,  theme_rgb8(32, 32, 36));
        t.set(GuiColor::ConsoleBodyFill, theme_rgb8(22, 23, 25));
        t.set(GuiColor::ConsoleBarFill,  theme_rgb8(30, 31, 34));
        t.set(GuiColor::StatusFill,      theme_rgb8(22, 22, 24));
        t.set(GuiColor::Text,            theme_rgb8(241, 241, 241));
        t.set(GuiColor::TextMuted,       theme_rgb8(150, 152, 158));
        t.set(GuiColor::TextDim,         theme_rgb8(110, 112, 118));
        t.set(GuiColor::TextBright,      theme_rgb8(240, 240, 244));
        t.set(GuiColor::ButtonFill,      theme_rgb8(54, 57, 63));
        t.set(GuiColor::ButtonHover,     theme_rgb8(70, 73, 80));
        t.set(GuiColor::ButtonText,      theme_rgb8(235, 235, 240));
        t.set(GuiColor::Accent,          theme_rgb8(0, 122, 204));
        t.set(GuiColor::AccentText,      theme_rgb8(255, 255, 255));
        t.set(GuiColor::RowHover,        theme_rgb8(44, 46, 52));
        t.set(GuiColor::RowSelected,     theme_rgb8(48, 56, 72));
        t.set(GuiColor::Selection,       theme_rgb8(255, 200, 80));
        t.set(GuiColor::NodeTitle,       theme_rgb8(240, 240, 244));
        t.set(GuiColor::PinLabel,        theme_rgb8(185, 188, 196));
        t.set(GuiColor::Danger,          theme_rgb8(230, 110, 110));
        t.set(GuiColor::Warning,         theme_rgb8(220, 150, 120));
        t.set(GuiColor::Success,         theme_rgb8(130, 160, 130));
        return t;
    }
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_THEME_HPP
