/*
 * gui_styles.cpp - default_style() presets for every widget style struct
 *
 * The style STRUCTS are declared in gui_styles.hpp (geometry + fields only); the
 * concrete DEFAULT VALUES — the AIWrapper dark look — live here, out of line, so
 * the headers carry no colour literals and retuning the default palette is a
 * single-file edit. color_rgba8() (gui.hpp) is used for the RGB8 → Vec4 shorthand.
 */

#include "gui.hpp"

namespace window {
namespace gui {

// ── Core widget + label ──────────────────────────────────────────────────────

GuiStyle GuiStyle::default_style() {
    GuiStyle style;
    style.background_color = color_rgba8(45, 45, 48);
    style.border_color = color_rgba8(63, 63, 70);
    style.hover_color = color_rgba8(62, 62, 66);
    style.pressed_color = color_rgba8(27, 27, 28);
    style.disabled_color = color_rgba8(78, 78, 80);
    style.focus_color = color_rgba8(0, 122, 204);
    style.padding = math::Vec4(8.0f, 4.0f, 8.0f, 4.0f);
    style.margin = math::Vec4(2.0f);
    return style;
}

LabelStyle LabelStyle::default_style() {
    LabelStyle style;
    style.text_color = color_rgba8(241, 241, 241);
    style.selection_color = color_rgba8(51, 153, 255, 128);
    return style;
}

// ── Text entry ────────────────────────────────────────────────────────────────

TextInputStyle TextInputStyle::default_style() {
    TextInputStyle s;
    s.background_color    = color_rgba8(30, 30, 30);
    s.border_color        = color_rgba8(64, 64, 69);
    s.focus_border_color  = color_rgba8(0, 122, 204);
    s.text_color          = color_rgba8(240, 240, 240);
    s.placeholder_color   = math::Vec4(0.5f, 0.5f, 0.5f, 0.7f);
    s.selection_color     = color_rgba8(38, 79, 120);
    s.cursor_color        = color_rgba8(240, 240, 240);
    return s;
}

EditBoxStyle EditBoxStyle::default_style() {
    EditBoxStyle s;
    s.background_color = color_rgba8(30, 30, 30);
    s.text_color = color_rgba8(212, 212, 212);
    s.selection_color = color_rgba8(38, 79, 120);
    s.cursor_color = color_rgba8(255, 255, 255);
    s.line_number_background = color_rgba8(37, 37, 38);
    s.line_number_color = color_rgba8(133, 133, 133);
    s.current_line_highlight = color_rgba8(40, 40, 40);
    s.border_color = color_rgba8(63, 63, 70);
    s.gutter_border_color = color_rgba8(45, 45, 48);
    return s;
}

// ── Controls ──────────────────────────────────────────────────────────────────

ButtonStyle ButtonStyle::default_style() {
    ButtonStyle s;
    s.background_color = color_rgba8(60, 60, 60);
    s.hover_color = color_rgba8(70, 70, 70);
    s.pressed_color = color_rgba8(50, 50, 50);
    s.disabled_color = color_rgba8(45, 45, 48);
    s.checked_color = color_rgba8(0, 122, 204);
    s.text_color = color_rgba8(241, 241, 241);
    s.text_disabled_color = color_rgba8(110, 110, 110);
    s.border_color = color_rgba8(80, 80, 80);
    s.focus_border_color = color_rgba8(0, 122, 204);
    return s;
}

ButtonStyle ButtonStyle::from_preset(ButtonStylePreset preset) {
    ButtonStyle s = default_style();
    switch (preset) {
        case ButtonStylePreset::Primary:
            s.background_color = color_rgba8(0, 122, 204);
            s.hover_color      = color_rgba8(0, 140, 230);
            s.pressed_color    = color_rgba8(0, 100, 180);
            s.border_color     = color_rgba8(0, 100, 170);
            break;
        case ButtonStylePreset::Success:
            s.background_color = color_rgba8(40, 160, 80);
            s.hover_color      = color_rgba8(50, 180, 90);
            s.pressed_color    = color_rgba8(30, 140, 65);
            s.border_color     = color_rgba8(30, 130, 60);
            break;
        case ButtonStylePreset::Warning:
            s.background_color = color_rgba8(200, 130, 0);
            s.hover_color      = color_rgba8(220, 148, 0);
            s.pressed_color    = color_rgba8(175, 110, 0);
            s.border_color     = color_rgba8(175, 110, 0);
            s.text_color       = color_rgba8(255, 255, 255);
            break;
        case ButtonStylePreset::Danger:
            s.background_color = color_rgba8(190, 40, 40);
            s.hover_color      = color_rgba8(215, 50, 50);
            s.pressed_color    = color_rgba8(165, 30, 30);
            s.border_color     = color_rgba8(160, 30, 30);
            break;
        case ButtonStylePreset::Ghost:
            s.background_color = color_rgba8(0, 0, 0, 0);
            s.hover_color      = color_rgba8(255, 255, 255, 25);
            s.pressed_color    = color_rgba8(255, 255, 255, 15);
            s.border_color     = color_rgba8(150, 150, 150);
            break;
        case ButtonStylePreset::Flat:
            s.background_color = color_rgba8(0, 0, 0, 0);
            s.hover_color      = color_rgba8(255, 255, 255, 20);
            s.pressed_color    = color_rgba8(255, 255, 255, 10);
            s.border_color     = color_rgba8(0, 0, 0, 0);
            s.border_width     = 0.0f;
            break;
        default: break;
    }
    return s;
}

SliderStyle SliderStyle::default_style() {
    SliderStyle s;
    s.track_color = color_rgba8(63, 63, 70);
    s.track_fill_color = color_rgba8(0, 122, 204);
    s.thumb_color = color_rgba8(200, 200, 200);
    s.thumb_hover_color = color_rgba8(0, 122, 204);
    s.thumb_pressed_color = color_rgba8(0, 100, 180);
    s.tick_color = color_rgba8(110, 110, 110);
    return s;
}

ProgressBarStyle ProgressBarStyle::default_style() {
    ProgressBarStyle s;
    s.track_color = color_rgba8(63, 63, 70);
    s.fill_color = color_rgba8(0, 122, 204);
    s.indeterminate_color = color_rgba8(0, 122, 204);
    s.text_color = color_rgba8(241, 241, 241);
    return s;
}

ColorPickerStyle ColorPickerStyle::default_style() {
    ColorPickerStyle s;
    s.background_color = color_rgba8(37, 37, 38);
    s.border_color = color_rgba8(63, 63, 70);
    s.label_color = color_rgba8(180, 180, 180);
    s.input_background = color_rgba8(30, 30, 30);
    s.input_text_color = color_rgba8(241, 241, 241);
    s.swatch_border_color = color_rgba8(80, 80, 80);
    s.selector_color = color_rgba8(255, 255, 255);
    return s;
}

// ── Scroll bar ───────────────────────────────────────────────────────────────

ScrollBarStyle ScrollBarStyle::default_style() {
    ScrollBarStyle s;
    s.track_color = math::Vec4(30 / 255.0f, 30 / 255.0f, 30 / 255.0f, 1.0f);
    s.thumb_color = math::Vec4(80 / 255.0f, 80 / 255.0f, 80 / 255.0f, 1.0f);
    s.thumb_hover_color = math::Vec4(120 / 255.0f, 120 / 255.0f, 120 / 255.0f, 1.0f);
    s.thumb_pressed_color = math::Vec4(160 / 255.0f, 160 / 255.0f, 160 / 255.0f, 1.0f);
    return s;
}

// ── Property grid ─────────────────────────────────────────────────────────────

PropertyGridStyle PropertyGridStyle::default_style() {
    PropertyGridStyle s;
    s.category_background = color_rgba8(37, 37, 38);
    s.category_text_color = color_rgba8(220, 220, 220);
    s.name_text_color = color_rgba8(200, 200, 200);
    s.value_text_color = color_rgba8(241, 241, 241);
    s.row_background = color_rgba8(45, 45, 48);
    s.row_alt_background = color_rgba8(50, 50, 53);
    s.selected_background = color_rgba8(0, 122, 204);
    s.separator_color = color_rgba8(63, 63, 70);
    s.field_background = color_rgba8(30, 31, 35);          // stacked-mode value box
    s.field_border_color = color_rgba8(58, 60, 66);
    return s;
}

// ── List box + combo box ─────────────────────────────────────────────────────

ChoiceCardStyle ChoiceCardStyle::default_style() {
    ChoiceCardStyle s;
    s.background_color = color_rgba8(38, 38, 42);
    s.border_color     = color_rgba8(64, 64, 70);
    s.prompt_color     = color_rgba8(226, 226, 232);
    s.answer_color     = color_rgba8(150, 200, 150);
    return s;
}

ListBoxStyle ListBoxStyle::default_style() {
    ListBoxStyle s;
    s.row_background = color_rgba8(45, 45, 48);
    s.row_alt_background = color_rgba8(50, 50, 53);
    s.selected_background = color_rgba8(0, 122, 204);
    s.hover_background = color_rgba8(62, 62, 66);
    s.text_color = color_rgba8(241, 241, 241);
    s.selected_text_color = color_rgba8(255, 255, 255);
    s.disabled_text_color = color_rgba8(140, 142, 150);
    s.icon_color = color_rgba8(200, 200, 200);
    s.separator_color = color_rgba8(63, 63, 70);
    s.background_color = s.row_background;                 // legacy look: solid fill
    s.border_color = math::Vec4(0.25f, 0.25f, 0.27f, 1.0f);
    s.action_background = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);   // plain "×" glyph by default
    s.action_text_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);   // 0 = text_color
    return s;
}

ComboBoxStyle ComboBoxStyle::default_style() {
    ComboBoxStyle s;
    s.background_color = color_rgba8(45, 45, 48);
    s.hover_background = color_rgba8(62, 62, 66);
    s.open_background = color_rgba8(37, 37, 38);
    s.text_color = color_rgba8(241, 241, 241);
    s.placeholder_color = color_rgba8(130, 130, 130);
    s.arrow_color = color_rgba8(160, 160, 160);
    s.dropdown_background = color_rgba8(37, 37, 38);
    s.dropdown_border_color = color_rgba8(63, 63, 70);
    s.item_hover_background = color_rgba8(62, 62, 66);
    s.item_selected_background = color_rgba8(0, 122, 204);
    s.item_text_color = color_rgba8(241, 241, 241);
    s.item_selected_text_color = color_rgba8(255, 255, 255);
    return s;
}

// ── Canvas ────────────────────────────────────────────────────────────────────

CanvasStyle CanvasStyle::default_style() {
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

CanvasWireStyle CanvasWireStyle::default_style() {
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

CanvasNodeStyle CanvasNodeStyle::default_style() {
    CanvasNodeStyle s;
    s.corner_radius   = 6.0f;
    s.header_height   = 24.0f;
    s.row_height      = 18.0f;
    s.pin_radius      = 5.0f;
    s.title_font      = 14.0f;
    s.pin_font        = 11.0f;
    s.title_pad       = 8.0f;
    s.pin_label_pad   = 8.0f;
    s.selection_border = 2.0f;
    return s;
}

// ── Panel ─────────────────────────────────────────────────────────────────────

SplitterStyle SplitterStyle::default_style() {
    SplitterStyle s;
    s.splitter_color = color_rgba8(45, 45, 48);
    s.splitter_hover_color = color_rgba8(0, 122, 204);
    s.splitter_drag_color = color_rgba8(0, 122, 204);
    s.grip_color = color_rgba8(110, 110, 110);
    return s;
}

DockPanelStyle DockPanelStyle::default_style() {
    DockPanelStyle s;
    s.background_color = color_rgba8(37, 37, 38);
    s.title_bar_color = color_rgba8(45, 45, 48);
    s.title_bar_active_color = color_rgba8(0, 122, 204);
    s.title_text_color = color_rgba8(160, 160, 160);
    s.title_active_text_color = color_rgba8(255, 255, 255);
    s.tab_bar_color = color_rgba8(37, 37, 38);
    s.drop_indicator_color = color_rgba8(0, 122, 204, 180);
    s.auto_hide_tab_color = color_rgba8(45, 45, 48);
    return s;
}

// ── Menu ──────────────────────────────────────────────────────────────────────

MenuStyle MenuStyle::default_style() {
    MenuStyle s;
    s.background_color = color_rgba8(37, 37, 38);
    s.border_color = color_rgba8(63, 63, 70);
    s.item_text_color = color_rgba8(241, 241, 241);
    s.item_hover_background = color_rgba8(0, 122, 204);
    s.item_hover_text_color = color_rgba8(255, 255, 255);
    s.item_disabled_text_color = color_rgba8(110, 110, 110);
    s.separator_color = color_rgba8(63, 63, 70);
    s.shortcut_text_color = color_rgba8(160, 160, 160);
    s.check_color = color_rgba8(0, 122, 204);
    s.submenu_arrow_color = color_rgba8(160, 160, 160);
    s.shadow_color = color_rgba8(0, 0, 0, 100);
    return s;
}

MenuBarStyle MenuBarStyle::default_style() {
    MenuBarStyle s;
    s.background_color = color_rgba8(45, 45, 48);
    s.item_text_color = color_rgba8(241, 241, 241);
    s.item_hover_background = color_rgba8(62, 62, 66);
    s.item_hover_text_color = color_rgba8(255, 255, 255);
    s.item_open_background = color_rgba8(37, 37, 38);
    return s;
}

// ── Page ──────────────────────────────────────────────────────────────────────

PageStyle PageStyle::default_style() {
    PageStyle s;
    s.background_color = color_rgba8(30, 30, 30);
    s.overlay_color = color_rgba8(0, 0, 0);
    return s;
}

// ── Dialog ────────────────────────────────────────────────────────────────────

DialogStyle DialogStyle::default_style() {
    DialogStyle s;
    s.overlay_color = color_rgba8(0, 0, 0, 128);
    s.background_color = color_rgba8(45, 45, 48);
    s.border_color = color_rgba8(63, 63, 70);
    s.title_bar_color = color_rgba8(37, 37, 38);
    s.title_text_color = color_rgba8(241, 241, 241);
    s.shadow_color = color_rgba8(0, 0, 0, 100);
    return s;
}

// ── Tab control ──────────────────────────────────────────────────────────────

TabStyle TabStyle::default_style() {
    TabStyle s;
    s.tab_background = color_rgba8(45, 45, 48);
    s.tab_hover_background = color_rgba8(62, 62, 66);
    s.tab_active_background = color_rgba8(37, 37, 38);
    s.tab_text_color = color_rgba8(160, 160, 160);
    s.tab_active_text_color = color_rgba8(241, 241, 241);
    s.tab_bar_background = color_rgba8(30, 30, 30);
    s.indicator_color = color_rgba8(0, 122, 204);
    s.close_button_color = color_rgba8(160, 160, 160);
    s.close_button_hover_color = color_rgba8(241, 241, 241);
    return s;
}

// ── Tree view ────────────────────────────────────────────────────────────────

TreeViewStyle TreeViewStyle::default_style() {
    TreeViewStyle s;
    s.row_background = color_rgba8(45, 45, 48);
    s.row_alt_background = color_rgba8(50, 50, 53);
    s.selected_background = color_rgba8(0, 122, 204);
    s.hover_background = color_rgba8(62, 62, 66);
    s.text_color = color_rgba8(241, 241, 241);
    s.folder_text_color = math::Vec4(0, 0, 0, 0);   // default: fall back to text_color
    s.icon_color = color_rgba8(200, 200, 200);
    s.line_color = color_rgba8(80, 80, 80);
    return s;
}

// ── Toolbar + status bar ─────────────────────────────────────────────────────

ToolbarStyle ToolbarStyle::default_style() {
    ToolbarStyle s;
    s.background_color = color_rgba8(45, 45, 48);
    s.button_color = color_rgba8(45, 45, 48, 0);
    s.button_hover_color = color_rgba8(62, 62, 66);
    s.button_pressed_color = color_rgba8(27, 27, 28);
    s.button_toggled_color = color_rgba8(0, 122, 204, 80);
    s.button_disabled_color = color_rgba8(45, 45, 48, 0);
    s.icon_color = color_rgba8(241, 241, 241);
    s.icon_disabled_color = color_rgba8(110, 110, 110);
    s.separator_color = color_rgba8(63, 63, 70);
    s.overflow_button_color = color_rgba8(80, 80, 80);
    return s;
}

StatusBarStyle StatusBarStyle::default_style() {
    StatusBarStyle s;
    s.background_color = color_rgba8(0, 122, 204);
    s.text_color = color_rgba8(255, 255, 255);
    s.separator_color = color_rgba8(255, 255, 255, 60);
    s.hover_background = color_rgba8(255, 255, 255, 30);
    s.icon_color = color_rgba8(255, 255, 255);
    return s;
}

} // namespace gui
} // namespace window
