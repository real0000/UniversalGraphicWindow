#include "css_theme.hpp"
#include "../../gui/gui_context.hpp"   // IGuiContext::set_default_style / set_default_label_style

namespace window {
namespace gui {

// ─────────────────────────────────────────────────────────────────────────────
// Palette
// ─────────────────────────────────────────────────────────────────────────────
CssPalette CssPalette::light() {
    CssPalette p;
    p.page_bg        = color_rgba8(255, 255, 255);
    p.surface        = color_rgba8(248, 249, 250);
    p.hover_surface  = color_rgba8(233, 236, 239);
    p.text           = color_rgba8(33, 37, 41);
    p.text_muted     = color_rgba8(108, 117, 125);
    p.border         = color_rgba8(222, 226, 230);
    p.border_strong  = color_rgba8(206, 212, 218);
    p.primary        = color_rgba8(13, 110, 253);
    p.primary_hover  = color_rgba8(11, 94, 215);
    p.primary_active = color_rgba8(10, 88, 202);
    p.on_primary     = color_rgba8(255, 255, 255);
    p.focus          = color_rgba8(13, 110, 253);
    p.selection      = color_rgba8(13, 110, 253, 64);
    p.track          = color_rgba8(233, 236, 239);
    return p;
}

CssPalette CssPalette::dark() {
    CssPalette p;
    p.page_bg        = color_rgba8(24, 24, 27);
    p.surface        = color_rgba8(33, 37, 41);
    p.hover_surface  = color_rgba8(52, 58, 64);
    p.text           = color_rgba8(233, 236, 239);
    p.text_muted     = color_rgba8(150, 150, 150);
    p.border         = color_rgba8(73, 80, 87);
    p.border_strong  = color_rgba8(90, 98, 104);
    p.primary        = color_rgba8(13, 110, 253);
    p.primary_hover  = color_rgba8(51, 133, 255);
    p.primary_active = color_rgba8(10, 88, 202);
    p.on_primary     = color_rgba8(255, 255, 255);
    p.focus          = color_rgba8(51, 133, 255);
    p.selection      = color_rgba8(13, 110, 253, 90);
    p.track          = color_rgba8(52, 58, 64);
    return p;
}

namespace {
// primary at a given alpha — used for translucent accent fills (toggles, tints).
math::Vec4 tint(const math::Vec4& c, float a) { return math::Vec4(c.x, c.y, c.z, a); }
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Presets — start from each struct's default_style() (keeps sensible metrics),
// then override the colours / radii / fonts that define the CSS look.
// ─────────────────────────────────────────────────────────────────────────────
GuiStyle CssTheme::gui_style() const {
    GuiStyle s = GuiStyle::default_style();
    s.background_color = p_.surface;          // panels read this as their fill
    s.border_color     = p_.border;
    s.hover_color      = p_.hover_surface;
    s.pressed_color    = p_.border;
    s.disabled_color   = p_.surface;
    s.focus_color      = p_.focus;
    s.border_width     = p_.border_width;
    s.corner_radius    = p_.radius;
    return s;
}

LabelStyle CssTheme::label_style() const {
    LabelStyle s = LabelStyle::default_style();
    s.text_color      = p_.text;
    s.selection_color = p_.selection;
    s.font_size       = p_.font_size;
    s.font_name       = p_.font_name;
    return s;
}

// Neutral gray button (the browser default <button> look). Also drives checkbox /
// radio, so the fill stays light: a blue fill would turn checkboxes blue.
ButtonStyle CssTheme::button_style() const {
    ButtonStyle s = ButtonStyle::default_style();
    s.background_color    = p_.hover_surface;
    s.hover_color         = p_.border;
    s.pressed_color       = p_.border_strong;
    s.disabled_color      = p_.surface;
    s.checked_color       = p_.primary;        // checkbox tick / radio dot
    s.text_color          = p_.text;
    s.text_disabled_color = p_.text_muted;
    s.border_color        = p_.border_strong;
    s.focus_border_color  = p_.focus;
    s.border_width        = p_.border_width;
    s.corner_radius       = p_.control_radius;
    s.font_size           = p_.font_size;
    return s;
}

// Accent call-to-action button — apply selectively (e.g. an "OK" button).
ButtonStyle CssTheme::button_primary_style() const {
    ButtonStyle s = button_style();
    s.background_color    = p_.primary;
    s.hover_color         = p_.primary_hover;
    s.pressed_color       = p_.primary_active;
    s.disabled_color      = tint(p_.primary, 0.5f);
    s.text_color          = p_.on_primary;
    s.text_disabled_color = tint(p_.on_primary, 0.7f);
    s.border_color        = p_.primary_active;
    return s;
}

SliderStyle CssTheme::slider_style() const {
    SliderStyle s = SliderStyle::default_style();
    s.track_color         = p_.track;
    s.track_fill_color    = p_.primary;
    s.thumb_color         = p_.primary;
    s.thumb_hover_color   = p_.primary_hover;
    s.thumb_pressed_color = p_.primary_active;
    s.tick_color          = p_.border_strong;
    return s;
}

ProgressBarStyle CssTheme::progress_bar_style() const {
    ProgressBarStyle s = ProgressBarStyle::default_style();
    s.track_color         = p_.track;
    s.fill_color          = p_.primary;
    s.indeterminate_color = p_.primary;
    s.text_color          = p_.on_primary;
    s.corner_radius       = p_.control_radius;
    return s;
}

ColorPickerStyle CssTheme::color_picker_style() const {
    ColorPickerStyle s = ColorPickerStyle::default_style();
    s.background_color    = p_.surface;
    s.border_color        = p_.border;
    s.label_color         = p_.text_muted;
    s.input_background    = p_.page_bg;
    s.input_text_color    = p_.text;
    s.swatch_border_color = p_.border_strong;
    s.selector_color      = color_rgba8(255, 255, 255);
    return s;
}

EditBoxStyle CssTheme::editbox_style() const {
    EditBoxStyle s = EditBoxStyle::default_style();
    s.background_color       = p_.page_bg;
    s.text_color             = p_.text;
    s.selection_color        = p_.selection;
    s.cursor_color           = p_.text;
    s.line_number_background = p_.surface;
    s.line_number_color      = p_.text_muted;
    s.current_line_highlight = p_.surface;
    s.border_color           = p_.border;
    s.gutter_border_color    = p_.border;
    s.font_name              = p_.font_name;
    return s;
}

ListBoxStyle CssTheme::list_box_style() const {
    ListBoxStyle s = ListBoxStyle::default_style();
    s.row_background        = p_.page_bg;
    s.row_alt_background    = p_.surface;
    s.selected_background   = p_.primary;
    s.hover_background      = p_.hover_surface;
    s.text_color           = p_.text;
    s.selected_text_color  = p_.on_primary;
    s.icon_color           = p_.text_muted;
    s.separator_color      = p_.border;
    s.font_size            = p_.font_size;
    return s;
}

ComboBoxStyle CssTheme::combo_box_style() const {
    ComboBoxStyle s = ComboBoxStyle::default_style();
    s.background_color          = p_.page_bg;
    s.hover_background          = p_.hover_surface;
    s.open_background           = p_.surface;
    s.text_color               = p_.text;
    s.placeholder_color        = p_.text_muted;
    s.arrow_color              = p_.text_muted;
    s.dropdown_background      = p_.page_bg;
    s.dropdown_border_color    = p_.border;
    s.item_hover_background    = p_.hover_surface;
    s.item_selected_background = p_.primary;
    s.item_text_color          = p_.text;
    s.item_selected_text_color = p_.on_primary;
    s.corner_radius            = p_.control_radius;
    s.dropdown_corner_radius   = p_.control_radius;
    s.border_width             = p_.border_width;
    s.font_size                = p_.font_size;
    return s;
}

MenuStyle CssTheme::menu_style() const {
    MenuStyle s = MenuStyle::default_style();
    s.background_color          = p_.page_bg;
    s.border_color             = p_.border;
    s.item_text_color          = p_.text;
    s.item_hover_background    = p_.primary;
    s.item_hover_text_color    = p_.on_primary;
    s.item_disabled_text_color = p_.text_muted;
    s.separator_color          = p_.border;
    s.shortcut_text_color      = p_.text_muted;
    s.check_color              = p_.primary;
    s.submenu_arrow_color      = p_.text_muted;
    s.shadow_color             = color_rgba8(0, 0, 0, 40);
    s.corner_radius            = p_.radius;
    s.font_size                = p_.font_size;
    return s;
}

MenuBarStyle CssTheme::menu_bar_style() const {
    MenuBarStyle s = MenuBarStyle::default_style();
    s.background_color       = p_.surface;
    s.item_text_color        = p_.text;
    s.item_hover_background  = p_.hover_surface;
    s.item_hover_text_color  = p_.text;
    s.item_open_background   = p_.hover_surface;
    s.font_size              = p_.font_size;
    return s;
}

SplitterStyle CssTheme::splitter_style() const {
    SplitterStyle s = SplitterStyle::default_style();
    s.splitter_color       = p_.border;
    s.splitter_hover_color = p_.primary;
    s.splitter_drag_color  = p_.primary;
    s.grip_color           = p_.text_muted;
    return s;
}

DockPanelStyle CssTheme::dock_panel_style() const {
    DockPanelStyle s = DockPanelStyle::default_style();
    s.background_color       = p_.surface;
    s.title_bar_color        = p_.surface;
    s.title_bar_active_color = p_.primary;
    s.title_text_color       = p_.text_muted;
    s.title_active_text_color= p_.on_primary;
    s.tab_bar_color          = p_.surface;
    s.drop_indicator_color   = tint(p_.primary, 0.7f);
    s.auto_hide_tab_color    = p_.surface;
    return s;
}

TabStyle CssTheme::tab_style() const {
    TabStyle s = TabStyle::default_style();
    s.tab_background          = p_.surface;
    s.tab_hover_background    = p_.hover_surface;
    s.tab_active_background   = p_.page_bg;
    s.tab_text_color          = p_.text_muted;
    s.tab_active_text_color   = p_.text;
    s.tab_bar_background      = p_.surface;
    s.indicator_color         = p_.primary;
    s.close_button_color      = p_.text_muted;
    s.close_button_hover_color= p_.text;
    s.corner_radius           = p_.control_radius;
    s.font_size               = p_.font_size;
    return s;
}

ToolbarStyle CssTheme::toolbar_style() const {
    ToolbarStyle s = ToolbarStyle::default_style();
    s.background_color      = p_.surface;
    s.button_color          = color_rgba8(0, 0, 0, 0);   // transparent until hovered
    s.button_hover_color    = p_.hover_surface;
    s.button_pressed_color  = p_.border;
    s.button_toggled_color  = tint(p_.primary, 0.16f);
    s.button_disabled_color = color_rgba8(0, 0, 0, 0);
    s.icon_color            = p_.text;
    s.icon_disabled_color   = p_.text_muted;
    s.separator_color       = p_.border;
    s.overflow_button_color = p_.text;
    s.button_corner_radius  = p_.control_radius;
    return s;
}

StatusBarStyle CssTheme::status_bar_style() const {
    StatusBarStyle s = StatusBarStyle::default_style();
    s.background_color = p_.surface;
    s.text_color       = p_.text_muted;
    s.separator_color  = p_.border;
    s.hover_background  = p_.hover_surface;
    s.icon_color       = p_.text_muted;
    s.font_size        = p_.font_size;
    return s;
}

TreeViewStyle CssTheme::tree_view_style() const {
    TreeViewStyle s = TreeViewStyle::default_style();
    s.row_background      = p_.page_bg;
    s.row_alt_background  = p_.surface;
    s.selected_background = p_.primary;
    s.hover_background    = p_.hover_surface;
    s.text_color         = p_.text;
    s.icon_color         = p_.text_muted;
    s.line_color         = p_.border;
    s.font_size          = p_.font_size;
    return s;
}

ScrollBarStyle CssTheme::scrollbar_style() const {
    ScrollBarStyle s = ScrollBarStyle::default_style();
    s.track_color         = p_.surface;
    s.thumb_color         = p_.border_strong;
    s.thumb_hover_color   = p_.text_muted;
    s.thumb_pressed_color = p_.text_muted;
    return s;
}

PropertyGridStyle CssTheme::property_grid_style() const {
    PropertyGridStyle s = PropertyGridStyle::default_style();
    s.category_background = p_.surface;
    s.category_text_color = p_.text;
    s.name_text_color     = p_.text;
    s.value_text_color    = p_.text;
    s.row_background      = p_.page_bg;
    s.row_alt_background  = p_.surface;
    s.selected_background = p_.hover_surface;   // dark text stays readable
    s.separator_color     = p_.border;
    s.font_size           = p_.font_size;
    return s;
}

DialogStyle CssTheme::dialog_style() const {
    DialogStyle s = DialogStyle::default_style();
    s.overlay_color    = color_rgba8(0, 0, 0, 128);
    s.background_color = p_.page_bg;
    s.border_color     = p_.border;
    s.title_bar_color  = p_.surface;
    s.title_text_color = p_.text;
    s.shadow_color     = color_rgba8(0, 0, 0, 60);
    s.border_width     = p_.border_width;
    s.corner_radius    = p_.radius;
    return s;
}

PageStyle CssTheme::page_style() const {
    PageStyle s = PageStyle::default_style();
    s.background_color = p_.page_bg;
    s.overlay_color    = color_rgba8(0, 0, 0, 128);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────
bool CssTheme::apply_one(IGuiWidget* w) const {
    if (!w) return false;

    // Match the concrete interface and push its dedicated style. Order most-
    // derived first where it matters (IGuiTextInput is an IGuiLabel, so the
    // IGuiLabel branch covers both via the shared set_label_style).
    if      (auto* p = dynamic_cast<IGuiButton*>(w))       p->set_button_style(button_style());
    else if (auto* p = dynamic_cast<IGuiSlider*>(w))       p->set_slider_style(slider_style());
    else if (auto* p = dynamic_cast<IGuiProgressBar*>(w))  p->set_progress_bar_style(progress_bar_style());
    else if (auto* p = dynamic_cast<IGuiColorPicker*>(w))  p->set_color_picker_style(color_picker_style());
    else if (auto* p = dynamic_cast<IGuiEditBox*>(w))      p->set_editbox_style(editbox_style());
    else if (auto* p = dynamic_cast<IGuiLabel*>(w))        p->set_label_style(label_style());   // Label + TextInput
    else if (auto* p = dynamic_cast<IGuiListBox*>(w))      p->set_list_box_style(list_box_style());
    else if (auto* p = dynamic_cast<IGuiComboBox*>(w))     p->set_combo_box_style(combo_box_style());
    else if (auto* p = dynamic_cast<IGuiMenu*>(w))         p->set_menu_style(menu_style());
    else if (auto* p = dynamic_cast<IGuiMenuBar*>(w))      p->set_menu_bar_style(menu_bar_style());
    else if (auto* p = dynamic_cast<IGuiSplitPanel*>(w))   p->set_splitter_style(splitter_style());
    else if (auto* p = dynamic_cast<IGuiDockPanel*>(w))    p->set_dock_panel_style(dock_panel_style());
    else if (auto* p = dynamic_cast<IGuiTabControl*>(w))   p->set_tab_style(tab_style());
    else if (auto* p = dynamic_cast<IGuiToolbar*>(w))      p->set_toolbar_style(toolbar_style());
    else if (auto* p = dynamic_cast<IGuiStatusBar*>(w))    p->set_status_bar_style(status_bar_style());
    else if (auto* p = dynamic_cast<IGuiTreeView*>(w))     p->set_tree_view_style(tree_view_style());
    else if (auto* p = dynamic_cast<IGuiScrollBar*>(w))    p->set_scrollbar_style(scrollbar_style());
    else if (auto* p = dynamic_cast<IGuiPropertyGrid*>(w)) p->set_property_grid_style(property_grid_style());
    else if (auto* p = dynamic_cast<IGuiDialog*>(w))       p->set_dialog_style(dialog_style());
    else if (auto* p = dynamic_cast<IGuiPage*>(w))         p->set_page_style(page_style());
    else                                                   w->set_style(gui_style());  // panels / containers / unknown
    return true;
}

int CssTheme::apply(IGuiWidget* root, bool recursive) const {
    if (!root) return 0;
    int n = apply_one(root) ? 1 : 0;
    if (recursive) {
        const int count = root->get_child_count();
        for (int i = 0; i < count; ++i)
            n += apply(root->get_child(i), true);
    }
    return n;
}

void CssTheme::apply_defaults(IGuiContext* ctx) const {
    if (!ctx) return;
    ctx->set_default_style(gui_style());
    ctx->set_default_label_style(label_style());
}

} // namespace gui
} // namespace window
