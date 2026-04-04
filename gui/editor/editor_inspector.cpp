/*
 * editor_inspector.cpp - Property Inspector Panel Implementation
 */

#include "editor_inspector.hpp"
#include "gui/gui_context.hpp"
#include "gui/gui_property.hpp"
#include "gui/gui_controls.hpp"
#include "gui/gui_label.hpp"
#include "gui/gui_scroll.hpp"
#include "gui/gui_list.hpp"
#include "gui/gui_tab.hpp"
#include "gui/gui_tree.hpp"
#include <cstring>

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// Helpers
// ============================================================================

static const std::vector<std::string> s_size_modes    = {"Fixed", "Relative", "Auto", "Fill"};
static const std::vector<std::string> s_alignments    = {
    "TopLeft", "TopCenter", "TopRight",
    "CenterLeft", "Center", "CenterRight",
    "BottomLeft", "BottomCenter", "BottomRight"
};
static const std::vector<std::string> s_layout_dirs   = {"Horizontal", "Vertical"};
static const std::vector<std::string> s_widget_types  = {
    "Custom", "Container", "Panel", "Button", "Label", "TextInput",
    "Checkbox", "RadioButton", "Slider", "ProgressBar", "ScrollArea",
    "ListBox", "ComboBox", "TabControl", "TreeView", "Image",
    "Separator", "Spacer"
};
static const std::vector<std::string> s_button_types  = {"Normal", "Toggle", "Radio", "Checkbox"};
static const std::vector<std::string> s_button_presets= {"Default", "Primary", "Success", "Warning", "Danger", "Ghost", "Flat"};
static const std::vector<std::string> s_slider_oris   = {"Horizontal", "Vertical"};
static const std::vector<std::string> s_progress_modes= {"Determinate", "Indeterminate"};
static const std::vector<std::string> s_scroll_vis    = {"Auto", "Always", "Never"};
static const std::vector<std::string> s_list_sel_modes= {"Single", "Multi", "None"};
static const std::vector<std::string> s_tab_positions = {"Top", "Bottom", "Left", "Right"};
static const std::vector<std::string> s_tree_sel_modes= {"Single", "Multi", "None"};

// ============================================================================
// EditorInspector
// ============================================================================

void EditorInspector::initialize(IGuiContext* ctx, CommandHistory* history) {
    m_ctx     = ctx;
    m_history = history;

    m_root = ctx->create_widget(WidgetType::Container);
    if (m_root) {
        m_root->set_name("inspector_root");
        m_root->set_layout_direction(LayoutDirection::Vertical);
        m_root->set_spacing(0.0f);
        GuiStyle style = GuiStyle::default_style();
        style.background_color = math::Vec4(0, 0, 0, 0);
        style.padding = math::Vec4(0.0f);
        m_root->set_style(style);
    }

    m_property_grid = ctx->create_property_grid();
    if (m_property_grid) {
        m_property_grid->set_name("inspector_grid");
        m_property_grid->set_size_mode(SizeMode::Fill, SizeMode::Fill);
        m_property_grid->set_property_event_handler(this);
        m_property_grid->set_property_grid_style(PropertyGridStyle::default_style());
        if (m_root) m_root->add_child(m_property_grid);
    }

    // Initialize all IDs to -1 (not present)
    for (int i = 0; i < static_cast<int>(InspectorProperty::Count); ++i)
        m_prop_ids[i] = -1;

    build_properties();
}

void EditorInspector::shutdown() {
    m_selected      = nullptr;
    m_property_grid = nullptr;
    m_root          = nullptr;
    m_ctx           = nullptr;
}

// ============================================================================
// build_properties — rebuilds the grid for the currently selected widget type
// ============================================================================

void EditorInspector::build_properties() {
    if (!m_property_grid) return;
    m_property_grid->clear_properties();

    // Reset all IDs
    for (int i = 0; i < static_cast<int>(InspectorProperty::Count); ++i)
        m_prop_ids[i] = -1;

    if (!m_selected) return;

    WidgetType wtype = m_selected->get_type();

    auto add = [&](InspectorProperty prop, const char* cat, const char* name, PropertyType type) -> int {
        int id = m_property_grid->add_property(cat, name, type);
        m_prop_ids[static_cast<int>(prop)] = id;
        return id;
    };

    // ----------------------------------------------------------------
    // Identity (all widgets)
    // ----------------------------------------------------------------
    add(InspectorProperty::Name,    "Identity", "Name",    PropertyType::String);
    int tid = add(InspectorProperty::Type, "Identity", "Type", PropertyType::Enum);
    m_property_grid->set_enum_options(tid, s_widget_types);
    m_property_grid->set_property_read_only(tid, true);
    add(InspectorProperty::Visible, "Identity", "Visible", PropertyType::Bool);
    add(InspectorProperty::Enabled, "Identity", "Enabled", PropertyType::Bool);

    // ----------------------------------------------------------------
    // Widget-specific — Content section
    // ----------------------------------------------------------------

    // --- Label ---
    if (wtype == WidgetType::Label) {
        add(InspectorProperty::Text,          "Content", "Text",      PropertyType::String);
        add(InspectorProperty::LabelFontSize, "Content", "Font Size", PropertyType::Float);
        add(InspectorProperty::LabelTextColor,"Content", "Text Color",PropertyType::Color);
        int la = add(InspectorProperty::LabelAlignment, "Content", "Alignment", PropertyType::Enum);
        m_property_grid->set_enum_options(la, s_alignments);
        add(InspectorProperty::LabelWordWrap, "Content", "Word Wrap", PropertyType::Bool);
        add(InspectorProperty::LabelEllipsis, "Content", "Ellipsis",  PropertyType::Bool);
    }

    // --- TextInput ---
    if (wtype == WidgetType::TextInput) {
        add(InspectorProperty::Text,                 "Content", "Text",          PropertyType::String);
        add(InspectorProperty::Placeholder,          "Content", "Placeholder",   PropertyType::String);
        add(InspectorProperty::LabelFontSize,        "Content", "Font Size",     PropertyType::Float);
        add(InspectorProperty::LabelTextColor,       "Content", "Text Color",    PropertyType::Color);
        add(InspectorProperty::TextInputPasswordMode,"Content", "Password Mode", PropertyType::Bool);
        add(InspectorProperty::TextInputReadOnly,    "Content", "Read Only",     PropertyType::Bool);
        add(InspectorProperty::TextInputMaxLength,   "Content", "Max Length",    PropertyType::Float);
    }

    // --- Button ---
    if (wtype == WidgetType::Button) {
        add(InspectorProperty::Text,       "Content", "Text", PropertyType::String);
        add(InspectorProperty::ButtonIcon, "Content", "Icon", PropertyType::String);
        int bt = add(InspectorProperty::ButtonTypeEnum, "Content", "Button Type", PropertyType::Enum);
        m_property_grid->set_enum_options(bt, s_button_types);
        int bp = add(InspectorProperty::ButtonStylePresetEnum, "Content", "Style Preset", PropertyType::Enum);
        m_property_grid->set_enum_options(bp, s_button_presets);
        add(InspectorProperty::Checked, "Content", "Checked", PropertyType::Bool);
    }

    // --- Checkbox ---
    if (wtype == WidgetType::Checkbox) {
        add(InspectorProperty::Text,    "Content", "Text",    PropertyType::String);
        add(InspectorProperty::Checked, "Content", "Checked", PropertyType::Bool);
    }

    // --- RadioButton ---
    if (wtype == WidgetType::RadioButton) {
        add(InspectorProperty::Text,       "Content", "Text",        PropertyType::String);
        add(InspectorProperty::Checked,    "Content", "Checked",     PropertyType::Bool);
        add(InspectorProperty::RadioGroup, "Content", "Radio Group", PropertyType::Float);
    }

    // --- Slider ---
    if (wtype == WidgetType::Slider) {
        add(InspectorProperty::SliderMin,   "Content", "Min",   PropertyType::Float);
        add(InspectorProperty::SliderMax,   "Content", "Max",   PropertyType::Float);
        add(InspectorProperty::SliderValue, "Content", "Value", PropertyType::Float);
        add(InspectorProperty::SliderStep,  "Content", "Step",  PropertyType::Float);
        int so = add(InspectorProperty::SliderOrientationEnum, "Content", "Orientation", PropertyType::Enum);
        m_property_grid->set_enum_options(so, s_slider_oris);
        add(InspectorProperty::SliderTicksVisible,  "Content", "Show Ticks",    PropertyType::Bool);
        add(InspectorProperty::SliderTickInterval,  "Content", "Tick Interval", PropertyType::Float);
    }

    // --- ProgressBar ---
    if (wtype == WidgetType::ProgressBar) {
        add(InspectorProperty::ProgressValue,    "Content", "Value (0-1)", PropertyType::Float);
        int pm = add(InspectorProperty::ProgressModeEnum, "Content", "Mode", PropertyType::Enum);
        m_property_grid->set_enum_options(pm, s_progress_modes);
        add(InspectorProperty::ProgressShowText, "Content", "Show Text", PropertyType::Bool);
        add(InspectorProperty::ProgressText,     "Content", "Text",      PropertyType::String);
    }

    // --- Image ---
    if (wtype == WidgetType::Image) {
        add(InspectorProperty::ImageName,        "Content", "Image Name",    PropertyType::String);
        add(InspectorProperty::ImageTint,        "Content", "Tint",          PropertyType::Color);
        add(InspectorProperty::ImageUseSlice9,   "Content", "Use 9-Slice",   PropertyType::Bool);
        add(InspectorProperty::ImageSliceLeft,   "Content", "Slice Left",    PropertyType::Float);
        add(InspectorProperty::ImageSliceTop,    "Content", "Slice Top",     PropertyType::Float);
        add(InspectorProperty::ImageSliceRight,  "Content", "Slice Right",   PropertyType::Float);
        add(InspectorProperty::ImageSliceBottom, "Content", "Slice Bottom",  PropertyType::Float);
    }

    // --- ScrollArea ---
    if (wtype == WidgetType::ScrollArea) {
        int hv = add(InspectorProperty::ScrollHVisibility, "Content", "H Scrollbar", PropertyType::Enum);
        m_property_grid->set_enum_options(hv, s_scroll_vis);
        int vv = add(InspectorProperty::ScrollVVisibility, "Content", "V Scrollbar", PropertyType::Enum);
        m_property_grid->set_enum_options(vv, s_scroll_vis);
        add(InspectorProperty::ScrollSpeed, "Content", "Scroll Speed", PropertyType::Float);
    }

    // --- ListBox ---
    if (wtype == WidgetType::ListBox) {
        int lm = add(InspectorProperty::ListSelectionMode, "Content", "Selection Mode", PropertyType::Enum);
        m_property_grid->set_enum_options(lm, s_list_sel_modes);
    }

    // --- ComboBox ---
    if (wtype == WidgetType::ComboBox) {
        add(InspectorProperty::ComboPlaceholder, "Content", "Placeholder", PropertyType::String);
    }

    // --- TabControl ---
    if (wtype == WidgetType::TabControl) {
        int tp = add(InspectorProperty::TabPosition, "Content", "Tab Position", PropertyType::Enum);
        m_property_grid->set_enum_options(tp, s_tab_positions);
        add(InspectorProperty::TabActiveTab, "Content", "Active Tab", PropertyType::Float);
    }

    // --- TreeView ---
    if (wtype == WidgetType::TreeView) {
        int tm = add(InspectorProperty::TreeSelectionMode, "Content", "Selection Mode", PropertyType::Enum);
        m_property_grid->set_enum_options(tm, s_tree_sel_modes);
        add(InspectorProperty::TreeShowLines, "Content", "Show Lines", PropertyType::Bool);
    }

    // --- Button transition animation ---
    if (wtype == WidgetType::Button) {
        add(InspectorProperty::TransDuration,       "Transition", "Duration",         PropertyType::Float);
        add(InspectorProperty::TransNormalTint,     "Transition", "Normal Tint",      PropertyType::Color);
        add(InspectorProperty::TransNormalScale,    "Transition", "Normal Scale",     PropertyType::Float);
        add(InspectorProperty::TransNormalOffsetX,  "Transition", "Normal Offset X",  PropertyType::Float);
        add(InspectorProperty::TransNormalOffsetY,  "Transition", "Normal Offset Y",  PropertyType::Float);
        add(InspectorProperty::TransHoverTint,      "Transition", "Hover Tint",       PropertyType::Color);
        add(InspectorProperty::TransHoverScale,     "Transition", "Hover Scale",      PropertyType::Float);
        add(InspectorProperty::TransHoverOffsetX,   "Transition", "Hover Offset X",   PropertyType::Float);
        add(InspectorProperty::TransHoverOffsetY,   "Transition", "Hover Offset Y",   PropertyType::Float);
        add(InspectorProperty::TransPressedTint,    "Transition", "Pressed Tint",     PropertyType::Color);
        add(InspectorProperty::TransPressedScale,   "Transition", "Pressed Scale",    PropertyType::Float);
        add(InspectorProperty::TransPressedOffsetX, "Transition", "Pressed Offset X", PropertyType::Float);
        add(InspectorProperty::TransPressedOffsetY, "Transition", "Pressed Offset Y", PropertyType::Float);
        add(InspectorProperty::TransDisabledTint,    "Transition", "Disabled Tint",     PropertyType::Color);
        add(InspectorProperty::TransDisabledScale,   "Transition", "Disabled Scale",    PropertyType::Float);
        add(InspectorProperty::TransDisabledOffsetX, "Transition", "Disabled Offset X", PropertyType::Float);
        add(InspectorProperty::TransDisabledOffsetY, "Transition", "Disabled Offset Y", PropertyType::Float);
    }

    // ----------------------------------------------------------------
    // Geometry (all widgets)
    // ----------------------------------------------------------------
    add(InspectorProperty::PosX,   "Geometry", "X",      PropertyType::Float);
    add(InspectorProperty::PosY,   "Geometry", "Y",      PropertyType::Float);
    add(InspectorProperty::Width,  "Geometry", "Width",  PropertyType::Float);
    add(InspectorProperty::Height, "Geometry", "Height", PropertyType::Float);
    int wm = add(InspectorProperty::WidthMode,  "Geometry", "Width Mode",  PropertyType::Enum);
    m_property_grid->set_enum_options(wm, s_size_modes);
    int hm = add(InspectorProperty::HeightMode, "Geometry", "Height Mode", PropertyType::Enum);
    m_property_grid->set_enum_options(hm, s_size_modes);
    int al = add(InspectorProperty::Alignment,  "Geometry", "Alignment",   PropertyType::Enum);
    m_property_grid->set_enum_options(al, s_alignments);

    // ----------------------------------------------------------------
    // Layout (all widgets)
    // ----------------------------------------------------------------
    int ld = add(InspectorProperty::LayoutDirection, "Layout", "Direction", PropertyType::Enum);
    m_property_grid->set_enum_options(ld, s_layout_dirs);
    add(InspectorProperty::Spacing, "Layout", "Spacing", PropertyType::Float);

    // ----------------------------------------------------------------
    // Style — Colors (all widgets)
    // ----------------------------------------------------------------
    add(InspectorProperty::BackgroundColor, "Style", "Background",    PropertyType::Color);
    add(InspectorProperty::BorderColor,     "Style", "Border Color",  PropertyType::Color);
    add(InspectorProperty::HoverColor,      "Style", "Hover Color",   PropertyType::Color);
    add(InspectorProperty::PressedColor,    "Style", "Pressed Color", PropertyType::Color);
    add(InspectorProperty::DisabledColor,   "Style", "Disabled Color",PropertyType::Color);
    add(InspectorProperty::FocusColor,      "Style", "Focus Color",   PropertyType::Color);

    // ----------------------------------------------------------------
    // Style — Sizing (all widgets)
    // ----------------------------------------------------------------
    add(InspectorProperty::BorderWidth,   "Style",   "Border Width",   PropertyType::Float);
    add(InspectorProperty::CornerRadius,  "Style",   "Corner Radius",  PropertyType::Float);
    add(InspectorProperty::PaddingLeft,   "Padding", "Left",           PropertyType::Float);
    add(InspectorProperty::PaddingTop,    "Padding", "Top",            PropertyType::Float);
    add(InspectorProperty::PaddingRight,  "Padding", "Right",          PropertyType::Float);
    add(InspectorProperty::PaddingBottom, "Padding", "Bottom",         PropertyType::Float);
    add(InspectorProperty::MarginLeft,    "Margin",  "Left",           PropertyType::Float);
    add(InspectorProperty::MarginTop,     "Margin",  "Top",            PropertyType::Float);
    add(InspectorProperty::MarginRight,   "Margin",  "Right",          PropertyType::Float);
    add(InspectorProperty::MarginBottom,  "Margin",  "Bottom",         PropertyType::Float);
}

// ============================================================================
// set_selected_widget
// ============================================================================

void EditorInspector::set_selected_widget(IGuiWidget* widget) {
    m_selected = widget;
    build_properties();   // rebuild for this widget's type
    populate_values();
}

void EditorInspector::refresh() {
    populate_values();
}

// ============================================================================
// populate_values — read current state from the selected widget
// ============================================================================

void EditorInspector::populate_values() {
    if (!m_property_grid || !m_selected) return;

    auto pid = [&](InspectorProperty p) { return m_prop_ids[static_cast<int>(p)]; };
    auto set_str   = [&](InspectorProperty p, const char* v)     { int id=pid(p); if(id>=0) m_property_grid->set_string_value(id, v ? v : ""); };
    auto set_bool  = [&](InspectorProperty p, bool v)            { int id=pid(p); if(id>=0) m_property_grid->set_bool_value(id, v); };
    auto set_float = [&](InspectorProperty p, float v)           { int id=pid(p); if(id>=0) m_property_grid->set_float_value(id, v); };
    auto set_enum  = [&](InspectorProperty p, int v)             { int id=pid(p); if(id>=0) m_property_grid->set_enum_index(id, v); };
    auto set_color = [&](InspectorProperty p, const math::Vec4& v){ int id=pid(p); if(id>=0) m_property_grid->set_vec4_value(id, v); };

    IGuiWidget* w = m_selected;
    WidgetType  wtype = w->get_type();

    // Identity
    set_str (InspectorProperty::Name,    w->get_name());
    set_enum(InspectorProperty::Type,    static_cast<int>(wtype));
    set_bool(InspectorProperty::Visible, w->is_visible());
    set_bool(InspectorProperty::Enabled, w->is_enabled());

    // Widget-specific — Content
    if (wtype == WidgetType::Label) {
        if (auto* lbl = dynamic_cast<IGuiLabel*>(w)) {
            set_str  (InspectorProperty::Text,           lbl->get_text());
            const LabelStyle& ls = lbl->get_label_style();
            set_float(InspectorProperty::LabelFontSize,  ls.font_size);
            set_color(InspectorProperty::LabelTextColor, ls.text_color);
            set_enum (InspectorProperty::LabelAlignment, static_cast<int>(ls.alignment));
            set_bool (InspectorProperty::LabelWordWrap,  ls.wrap);
            set_bool (InspectorProperty::LabelEllipsis,  ls.ellipsis);
        }
    }
    if (wtype == WidgetType::TextInput) {
        if (auto* ti = dynamic_cast<IGuiTextInput*>(w)) {
            set_str  (InspectorProperty::Text,                  ti->get_text());
            set_str  (InspectorProperty::Placeholder,           ti->get_placeholder());
            const LabelStyle& ls = ti->get_label_style();
            set_float(InspectorProperty::LabelFontSize,         ls.font_size);
            set_color(InspectorProperty::LabelTextColor,        ls.text_color);
            set_bool (InspectorProperty::TextInputPasswordMode, ti->is_password_mode());
            set_bool (InspectorProperty::TextInputReadOnly,     ti->is_read_only());
            set_float(InspectorProperty::TextInputMaxLength,    static_cast<float>(ti->get_max_length()));
        }
    }
    if (wtype == WidgetType::Button) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            set_str (InspectorProperty::Text,              btn->get_text());
            set_str (InspectorProperty::ButtonIcon,        btn->get_icon());
            set_enum(InspectorProperty::ButtonTypeEnum,    static_cast<int>(btn->get_button_type()));
            set_enum(InspectorProperty::ButtonStylePresetEnum, 0);  // read-only hint; preset is write-only
            set_bool(InspectorProperty::Checked,           btn->is_checked());
        }
    }
    if (wtype == WidgetType::Checkbox) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            set_str (InspectorProperty::Text,    btn->get_text());
            set_bool(InspectorProperty::Checked, btn->is_checked());
        }
    }
    if (wtype == WidgetType::RadioButton) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            set_str  (InspectorProperty::Text,       btn->get_text());
            set_bool (InspectorProperty::Checked,    btn->is_checked());
            set_float(InspectorProperty::RadioGroup, static_cast<float>(btn->get_radio_group()));
        }
    }
    if (wtype == WidgetType::Slider) {
        if (auto* sl = dynamic_cast<IGuiSlider*>(w)) {
            set_float(InspectorProperty::SliderMin,            sl->get_min_value());
            set_float(InspectorProperty::SliderMax,            sl->get_max_value());
            set_float(InspectorProperty::SliderValue,          sl->get_value());
            set_float(InspectorProperty::SliderStep,           sl->get_step());
            set_enum (InspectorProperty::SliderOrientationEnum,static_cast<int>(sl->get_orientation()));
            set_bool (InspectorProperty::SliderTicksVisible,   sl->is_ticks_visible());
            set_float(InspectorProperty::SliderTickInterval,   sl->get_tick_interval());
        }
    }
    if (wtype == WidgetType::ProgressBar) {
        if (auto* pb = dynamic_cast<IGuiProgressBar*>(w)) {
            set_float(InspectorProperty::ProgressValue,    pb->get_value());
            set_enum (InspectorProperty::ProgressModeEnum, static_cast<int>(pb->get_mode()));
            set_bool (InspectorProperty::ProgressShowText, pb->is_text_visible());
            set_str  (InspectorProperty::ProgressText,     pb->get_text());
        }
    }
    if (wtype == WidgetType::Image) {
        if (auto* img = dynamic_cast<IGuiImage*>(w)) {
            set_str  (InspectorProperty::ImageName,        img->get_image_name().c_str());
            set_color(InspectorProperty::ImageTint,        img->get_tint());
            set_bool (InspectorProperty::ImageUseSlice9,   img->get_use_slice9());
            const SliceBorder& sb = img->get_slice_border();
            set_float(InspectorProperty::ImageSliceLeft,   sb.left);
            set_float(InspectorProperty::ImageSliceTop,    sb.top);
            set_float(InspectorProperty::ImageSliceRight,  sb.right);
            set_float(InspectorProperty::ImageSliceBottom, sb.bottom);
        }
    }
    if (wtype == WidgetType::ScrollArea) {
        if (auto* sv = dynamic_cast<IGuiScrollView*>(w)) {
            set_enum (InspectorProperty::ScrollHVisibility, static_cast<int>(sv->get_h_scrollbar_visibility()));
            set_enum (InspectorProperty::ScrollVVisibility, static_cast<int>(sv->get_v_scrollbar_visibility()));
            set_float(InspectorProperty::ScrollSpeed,       sv->get_scroll_speed());
        }
    }
    if (wtype == WidgetType::ListBox) {
        if (auto* lb = dynamic_cast<IGuiListBox*>(w))
            set_enum(InspectorProperty::ListSelectionMode, static_cast<int>(lb->get_selection_mode()));
    }
    if (wtype == WidgetType::ComboBox) {
        if (auto* cb = dynamic_cast<IGuiComboBox*>(w))
            set_str(InspectorProperty::ComboPlaceholder, cb->get_placeholder());
    }
    if (wtype == WidgetType::TabControl) {
        if (auto* tc = dynamic_cast<IGuiTabControl*>(w)) {
            set_enum (InspectorProperty::TabPosition,  static_cast<int>(tc->get_tab_position()));
            set_float(InspectorProperty::TabActiveTab, static_cast<float>(tc->get_active_tab()));
        }
    }
    if (wtype == WidgetType::TreeView) {
        if (auto* tv = dynamic_cast<IGuiTreeView*>(w)) {
            set_enum(InspectorProperty::TreeSelectionMode, static_cast<int>(tv->get_selection_mode()));
            set_bool(InspectorProperty::TreeShowLines,     tv->get_tree_view_style().show_lines);
        }
    }
    if (wtype == WidgetType::Button) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            const ButtonTransition& tr = btn->get_button_transition();
            set_float(InspectorProperty::TransDuration,       tr.duration);
            set_color(InspectorProperty::TransNormalTint,     tr.normal.tint);
            set_float(InspectorProperty::TransNormalScale,    tr.normal.scale);
            set_float(InspectorProperty::TransNormalOffsetX,  math::x(tr.normal.offset));
            set_float(InspectorProperty::TransNormalOffsetY,  math::y(tr.normal.offset));
            set_color(InspectorProperty::TransHoverTint,      tr.hovered.tint);
            set_float(InspectorProperty::TransHoverScale,     tr.hovered.scale);
            set_float(InspectorProperty::TransHoverOffsetX,   math::x(tr.hovered.offset));
            set_float(InspectorProperty::TransHoverOffsetY,   math::y(tr.hovered.offset));
            set_color(InspectorProperty::TransPressedTint,    tr.pressed.tint);
            set_float(InspectorProperty::TransPressedScale,   tr.pressed.scale);
            set_float(InspectorProperty::TransPressedOffsetX, math::x(tr.pressed.offset));
            set_float(InspectorProperty::TransPressedOffsetY, math::y(tr.pressed.offset));
            set_color(InspectorProperty::TransDisabledTint,    tr.disabled.tint);
            set_float(InspectorProperty::TransDisabledScale,   tr.disabled.scale);
            set_float(InspectorProperty::TransDisabledOffsetX, math::x(tr.disabled.offset));
            set_float(InspectorProperty::TransDisabledOffsetY, math::y(tr.disabled.offset));
        }
    }

    // Geometry
    math::Box b = w->get_bounds();
    set_float(InspectorProperty::PosX,       math::x(math::box_min(b)));
    set_float(InspectorProperty::PosY,       math::y(math::box_min(b)));
    set_float(InspectorProperty::Width,      math::box_width(b));
    set_float(InspectorProperty::Height,     math::box_height(b));
    set_enum (InspectorProperty::WidthMode,  static_cast<int>(w->get_width_mode()));
    set_enum (InspectorProperty::HeightMode, static_cast<int>(w->get_height_mode()));
    set_enum (InspectorProperty::Alignment,  static_cast<int>(w->get_alignment()));

    // Layout
    set_enum (InspectorProperty::LayoutDirection, static_cast<int>(w->get_layout_direction()));
    set_float(InspectorProperty::Spacing,         w->get_spacing());

    // Style
    const GuiStyle& s = w->get_style();
    set_color(InspectorProperty::BackgroundColor, s.background_color);
    set_color(InspectorProperty::BorderColor,     s.border_color);
    set_color(InspectorProperty::HoverColor,      s.hover_color);
    set_color(InspectorProperty::PressedColor,    s.pressed_color);
    set_color(InspectorProperty::DisabledColor,   s.disabled_color);
    set_color(InspectorProperty::FocusColor,      s.focus_color);
    set_float(InspectorProperty::BorderWidth,     s.border_width);
    set_float(InspectorProperty::CornerRadius,    s.corner_radius);
    set_float(InspectorProperty::PaddingLeft,     s.padding.x);
    set_float(InspectorProperty::PaddingTop,      s.padding.y);
    set_float(InspectorProperty::PaddingRight,    s.padding.z);
    set_float(InspectorProperty::PaddingBottom,   s.padding.w);
    set_float(InspectorProperty::MarginLeft,      s.margin.x);
    set_float(InspectorProperty::MarginTop,       s.margin.y);
    set_float(InspectorProperty::MarginRight,     s.margin.z);
    set_float(InspectorProperty::MarginBottom,    s.margin.w);
}

// ============================================================================
// on_property_changed / apply_property_change
// ============================================================================

void EditorInspector::on_property_changed(int property_id) {
    apply_property_change(property_id);
}

void EditorInspector::apply_property_change(int property_id) {
    if (!m_selected || !m_property_grid || !m_history) return;

    auto pid = [&](InspectorProperty p) { return m_prop_ids[static_cast<int>(p)]; };
    IGuiWidget* w     = m_selected;
    WidgetType  wtype = w->get_type();

    // Helper: check if this property_id matches a tracked property
    auto is = [&](InspectorProperty p) { return pid(p) >= 0 && property_id == pid(p); };

    // ----------------------------------------------------------------
    // Name
    // ----------------------------------------------------------------
    if (is(InspectorProperty::Name)) {
        const char* old_name = w->get_name();
        const char* new_name = m_property_grid->get_string_value(property_id);
        m_history->execute(std::make_unique<RenameWidgetCommand>(w, old_name, new_name));
        return;
    }

    // ----------------------------------------------------------------
    // Visible / Enabled
    // ----------------------------------------------------------------
    if (is(InspectorProperty::Visible)) {
        bool val = m_property_grid->get_bool_value(property_id);
        m_history->execute(std::make_unique<ChangeVisibilityCommand>(w, val));
        return;
    }
    if (is(InspectorProperty::Enabled)) {
        bool old_val = w->is_enabled();
        bool new_val = m_property_grid->get_bool_value(property_id);
        m_history->execute(std::make_unique<ChangePropertyCommand>(
            "Change enabled",
            [w, new_val]() { w->set_enabled(new_val); },
            [w, old_val]() { w->set_enabled(old_val); }
        ));
        return;
    }

    // ----------------------------------------------------------------
    // LabelStyle: font size, text color, alignment, wrap, ellipsis
    // ----------------------------------------------------------------
    if (is(InspectorProperty::LabelFontSize)  || is(InspectorProperty::LabelTextColor) ||
        is(InspectorProperty::LabelAlignment) || is(InspectorProperty::LabelWordWrap)  ||
        is(InspectorProperty::LabelEllipsis)) {
        if (auto* lbl = dynamic_cast<IGuiLabel*>(w)) {
            LabelStyle old_ls = lbl->get_label_style();
            LabelStyle new_ls = old_ls;
            if (is(InspectorProperty::LabelFontSize))
                new_ls.font_size  = m_property_grid->get_float_value(property_id);
            else if (is(InspectorProperty::LabelTextColor))
                new_ls.text_color = m_property_grid->get_vec4_value(property_id);
            else if (is(InspectorProperty::LabelAlignment))
                new_ls.alignment  = static_cast<Alignment>(m_property_grid->get_enum_index(property_id));
            else if (is(InspectorProperty::LabelWordWrap))
                new_ls.wrap       = m_property_grid->get_bool_value(property_id);
            else if (is(InspectorProperty::LabelEllipsis))
                new_ls.ellipsis   = m_property_grid->get_bool_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change label style",
                [lbl, new_ls]() { lbl->set_label_style(new_ls); },
                [lbl, old_ls]() { lbl->set_label_style(old_ls); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // TextInput-specific: password, read-only, max-length
    // ----------------------------------------------------------------
    if (is(InspectorProperty::TextInputPasswordMode) ||
        is(InspectorProperty::TextInputReadOnly)     ||
        is(InspectorProperty::TextInputMaxLength)) {
        if (auto* ti = dynamic_cast<IGuiTextInput*>(w)) {
            if (is(InspectorProperty::TextInputPasswordMode)) {
                bool old_v = ti->is_password_mode(), new_v = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change password mode",
                    [ti, new_v]() { ti->set_password_mode(new_v); },
                    [ti, old_v]() { ti->set_password_mode(old_v); }));
            } else if (is(InspectorProperty::TextInputReadOnly)) {
                bool old_v = ti->is_read_only(), new_v = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change read only",
                    [ti, new_v]() { ti->set_read_only(new_v); },
                    [ti, old_v]() { ti->set_read_only(old_v); }));
            } else if (is(InspectorProperty::TextInputMaxLength)) {
                int old_v = ti->get_max_length();
                int new_v = static_cast<int>(m_property_grid->get_float_value(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change max length",
                    [ti, new_v]() { ti->set_max_length(new_v); },
                    [ti, old_v]() { ti->set_max_length(old_v); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // Button icon
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ButtonIcon)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            std::string old_icon = btn->get_icon() ? btn->get_icon() : "";
            std::string new_icon = m_property_grid->get_string_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>("Change button icon",
                [btn, new_icon]() { btn->set_icon(new_icon.c_str()); },
                [btn, old_icon]() { btn->set_icon(old_icon.c_str()); }));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Button style preset (applies preset colors to ButtonStyle)
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ButtonStylePresetEnum)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            ButtonStyle old_bs = btn->get_button_style();
            ButtonStylePreset preset = static_cast<ButtonStylePreset>(m_property_grid->get_enum_index(property_id));
            ButtonStyle new_bs = ButtonStyle::from_preset(preset);
            m_history->execute(std::make_unique<ChangePropertyCommand>("Apply button style preset",
                [btn, new_bs]() { btn->set_button_style(new_bs); },
                [btn, old_bs]() { btn->set_button_style(old_bs); }));
        }
        return;
    }

    // ----------------------------------------------------------------
    // RadioGroup (RadioButton)
    // ----------------------------------------------------------------
    if (is(InspectorProperty::RadioGroup)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            int old_g = btn->get_radio_group();
            int new_g = static_cast<int>(m_property_grid->get_float_value(property_id));
            m_history->execute(std::make_unique<ChangePropertyCommand>("Change radio group",
                [btn, new_g]() { btn->set_radio_group(new_g); },
                [btn, old_g]() { btn->set_radio_group(old_g); }));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Text (Label, Button, Checkbox, RadioButton, TextInput)
    // ----------------------------------------------------------------
    if (is(InspectorProperty::Text)) {
        const char* new_text = m_property_grid->get_string_value(property_id);
        std::string nt = new_text ? new_text : "";

        if (wtype == WidgetType::Label) {
            if (auto* lbl = dynamic_cast<IGuiLabel*>(w)) {
                std::string old_text = lbl->get_text() ? lbl->get_text() : "";
                m_history->execute(std::make_unique<ChangePropertyCommand>(
                    "Change text",
                    [lbl, nt]()        { lbl->set_text(nt.c_str()); },
                    [lbl, old_text]()  { lbl->set_text(old_text.c_str()); }
                ));
            }
        } else if (wtype == WidgetType::TextInput) {
            if (auto* ti = dynamic_cast<IGuiTextInput*>(w)) {
                std::string old_text = ti->get_text() ? ti->get_text() : "";
                m_history->execute(std::make_unique<ChangePropertyCommand>(
                    "Change text",
                    [ti, nt]()        { ti->set_text(nt.c_str()); },
                    [ti, old_text]()  { ti->set_text(old_text.c_str()); }
                ));
            }
        } else if (wtype == WidgetType::Button   ||
                   wtype == WidgetType::Checkbox  ||
                   wtype == WidgetType::RadioButton) {
            if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
                std::string old_text = btn->get_text() ? btn->get_text() : "";
                m_history->execute(std::make_unique<ChangePropertyCommand>(
                    "Change text",
                    [btn, nt]()        { btn->set_text(nt.c_str()); },
                    [btn, old_text]()  { btn->set_text(old_text.c_str()); }
                ));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // Placeholder (TextInput)
    // ----------------------------------------------------------------
    if (is(InspectorProperty::Placeholder)) {
        if (auto* ti = dynamic_cast<IGuiTextInput*>(w)) {
            std::string old_ph = ti->get_placeholder() ? ti->get_placeholder() : "";
            std::string new_ph = m_property_grid->get_string_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change placeholder",
                [ti, new_ph]() { ti->set_placeholder(new_ph.c_str()); },
                [ti, old_ph]() { ti->set_placeholder(old_ph.c_str()); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Checked (Checkbox / RadioButton)
    // ----------------------------------------------------------------
    if (is(InspectorProperty::Checked)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            bool old_val = btn->is_checked();
            bool new_val = m_property_grid->get_bool_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change checked",
                [btn, new_val]() { btn->set_checked(new_val); },
                [btn, old_val]() { btn->set_checked(old_val); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Button type
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ButtonTypeEnum)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            ButtonType old_bt = btn->get_button_type();
            ButtonType new_bt = static_cast<ButtonType>(m_property_grid->get_enum_index(property_id));
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change button type",
                [btn, new_bt]() { btn->set_button_type(new_bt); },
                [btn, old_bt]() { btn->set_button_type(old_bt); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Slider: Min / Max / Value / Orientation / Step / Ticks
    // ----------------------------------------------------------------
    if (is(InspectorProperty::SliderMin)            || is(InspectorProperty::SliderMax)          ||
        is(InspectorProperty::SliderValue)          || is(InspectorProperty::SliderStep)         ||
        is(InspectorProperty::SliderOrientationEnum)||
        is(InspectorProperty::SliderTicksVisible)   || is(InspectorProperty::SliderTickInterval)) {
        if (auto* sl = dynamic_cast<IGuiSlider*>(w)) {
            if (is(InspectorProperty::SliderOrientationEnum)) {
                SliderOrientation old_or = sl->get_orientation();
                SliderOrientation new_or = static_cast<SliderOrientation>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change orientation",
                    [sl, new_or]() { sl->set_orientation(new_or); },
                    [sl, old_or]() { sl->set_orientation(old_or); }));
            } else if (is(InspectorProperty::SliderTicksVisible)) {
                bool old_v = sl->is_ticks_visible(), new_v = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change ticks visible",
                    [sl, new_v]() { sl->set_ticks_visible(new_v); },
                    [sl, old_v]() { sl->set_ticks_visible(old_v); }));
            } else if (is(InspectorProperty::SliderTickInterval)) {
                float old_v = sl->get_tick_interval(), new_v = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change tick interval",
                    [sl, new_v]() { sl->set_tick_interval(new_v); },
                    [sl, old_v]() { sl->set_tick_interval(old_v); }));
            } else if (is(InspectorProperty::SliderStep)) {
                float old_v = sl->get_step(), new_v = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change step",
                    [sl, new_v]() { sl->set_step(new_v); },
                    [sl, old_v]() { sl->set_step(old_v); }));
            } else {
                float old_min = sl->get_min_value(), old_max = sl->get_max_value(), old_val = sl->get_value();
                float new_min = m_property_grid->get_float_value(pid(InspectorProperty::SliderMin));
                float new_max = m_property_grid->get_float_value(pid(InspectorProperty::SliderMax));
                float new_val = m_property_grid->get_float_value(pid(InspectorProperty::SliderValue));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change slider",
                    [sl, new_min, new_max, new_val]() { sl->set_range(new_min, new_max); sl->set_value(new_val); },
                    [sl, old_min, old_max, old_val]() { sl->set_range(old_min, old_max); sl->set_value(old_val); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // ProgressBar: Value / Mode / ShowText / Text
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ProgressValue)   || is(InspectorProperty::ProgressModeEnum) ||
        is(InspectorProperty::ProgressShowText) || is(InspectorProperty::ProgressText)) {
        if (auto* pb = dynamic_cast<IGuiProgressBar*>(w)) {
            if (is(InspectorProperty::ProgressModeEnum)) {
                ProgressBarMode old_mode = pb->get_mode();
                ProgressBarMode new_mode = static_cast<ProgressBarMode>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change progress mode",
                    [pb, new_mode]() { pb->set_mode(new_mode); },
                    [pb, old_mode]() { pb->set_mode(old_mode); }));
            } else if (is(InspectorProperty::ProgressShowText)) {
                bool old_v = pb->is_text_visible(), new_v = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change progress show text",
                    [pb, new_v]() { pb->set_text_visible(new_v); },
                    [pb, old_v]() { pb->set_text_visible(old_v); }));
            } else if (is(InspectorProperty::ProgressText)) {
                std::string old_t = pb->get_text() ? pb->get_text() : "";
                std::string new_t = m_property_grid->get_string_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change progress text",
                    [pb, new_t]() { pb->set_text(new_t.c_str()); },
                    [pb, old_t]() { pb->set_text(old_t.c_str()); }));
            } else {
                float old_val = pb->get_value(), new_val = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change progress value",
                    [pb, new_val]() { pb->set_value(new_val); },
                    [pb, old_val]() { pb->set_value(old_val); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // Image: Tint / UseSlice9 / SliceBorder
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ImageTint)        || is(InspectorProperty::ImageUseSlice9)   ||
        is(InspectorProperty::ImageSliceLeft)   || is(InspectorProperty::ImageSliceTop)    ||
        is(InspectorProperty::ImageSliceRight)  || is(InspectorProperty::ImageSliceBottom)) {
        if (auto* img = dynamic_cast<IGuiImage*>(w)) {
            if (is(InspectorProperty::ImageTint)) {
                math::Vec4 old_t = img->get_tint(), new_t = m_property_grid->get_vec4_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change image tint",
                    [img, new_t]() { img->set_tint(new_t); },
                    [img, old_t]() { img->set_tint(old_t); }));
            } else if (is(InspectorProperty::ImageUseSlice9)) {
                bool old_v = img->get_use_slice9(), new_v = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change use 9-slice",
                    [img, new_v]() { img->set_use_slice9(new_v); },
                    [img, old_v]() { img->set_use_slice9(old_v); }));
            } else {
                SliceBorder old_sb = img->get_slice_border(), new_sb = old_sb;
                if (is(InspectorProperty::ImageSliceLeft))   new_sb.left   = m_property_grid->get_float_value(property_id);
                if (is(InspectorProperty::ImageSliceTop))    new_sb.top    = m_property_grid->get_float_value(property_id);
                if (is(InspectorProperty::ImageSliceRight))  new_sb.right  = m_property_grid->get_float_value(property_id);
                if (is(InspectorProperty::ImageSliceBottom)) new_sb.bottom = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change slice border",
                    [img, new_sb]() { img->set_slice_border(new_sb); },
                    [img, old_sb]() { img->set_slice_border(old_sb); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // ScrollArea
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ScrollHVisibility) || is(InspectorProperty::ScrollVVisibility) ||
        is(InspectorProperty::ScrollSpeed)) {
        if (auto* sv = dynamic_cast<IGuiScrollView*>(w)) {
            if (is(InspectorProperty::ScrollHVisibility)) {
                auto old_v = sv->get_h_scrollbar_visibility();
                auto new_v = static_cast<ScrollBarVisibility>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change H scrollbar",
                    [sv, new_v]() { sv->set_h_scrollbar_visibility(new_v); },
                    [sv, old_v]() { sv->set_h_scrollbar_visibility(old_v); }));
            } else if (is(InspectorProperty::ScrollVVisibility)) {
                auto old_v = sv->get_v_scrollbar_visibility();
                auto new_v = static_cast<ScrollBarVisibility>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change V scrollbar",
                    [sv, new_v]() { sv->set_v_scrollbar_visibility(new_v); },
                    [sv, old_v]() { sv->set_v_scrollbar_visibility(old_v); }));
            } else {
                float old_v = sv->get_scroll_speed(), new_v = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change scroll speed",
                    [sv, new_v]() { sv->set_scroll_speed(new_v); },
                    [sv, old_v]() { sv->set_scroll_speed(old_v); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // ListBox
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ListSelectionMode)) {
        if (auto* lb = dynamic_cast<IGuiListBox*>(w)) {
            auto old_m = lb->get_selection_mode();
            auto new_m = static_cast<ListBoxSelectionMode>(m_property_grid->get_enum_index(property_id));
            m_history->execute(std::make_unique<ChangePropertyCommand>("Change selection mode",
                [lb, new_m]() { lb->set_selection_mode(new_m); },
                [lb, old_m]() { lb->set_selection_mode(old_m); }));
        }
        return;
    }

    // ----------------------------------------------------------------
    // ComboBox
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ComboPlaceholder)) {
        if (auto* cb = dynamic_cast<IGuiComboBox*>(w)) {
            std::string old_ph = cb->get_placeholder() ? cb->get_placeholder() : "";
            std::string new_ph = m_property_grid->get_string_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>("Change combo placeholder",
                [cb, new_ph]() { cb->set_placeholder(new_ph.c_str()); },
                [cb, old_ph]() { cb->set_placeholder(old_ph.c_str()); }));
        }
        return;
    }

    // ----------------------------------------------------------------
    // TabControl
    // ----------------------------------------------------------------
    if (is(InspectorProperty::TabPosition) || is(InspectorProperty::TabActiveTab)) {
        if (auto* tc = dynamic_cast<IGuiTabControl*>(w)) {
            if (is(InspectorProperty::TabPosition)) {
                auto old_p = tc->get_tab_position();
                auto new_p = static_cast<TabPosition>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change tab position",
                    [tc, new_p]() { tc->set_tab_position(new_p); },
                    [tc, old_p]() { tc->set_tab_position(old_p); }));
            } else {
                int old_t = tc->get_active_tab(), new_t = static_cast<int>(m_property_grid->get_float_value(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change active tab",
                    [tc, new_t]() { tc->set_active_tab(new_t); },
                    [tc, old_t]() { tc->set_active_tab(old_t); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // TreeView
    // ----------------------------------------------------------------
    if (is(InspectorProperty::TreeSelectionMode) || is(InspectorProperty::TreeShowLines)) {
        if (auto* tv = dynamic_cast<IGuiTreeView*>(w)) {
            if (is(InspectorProperty::TreeSelectionMode)) {
                auto old_m = tv->get_selection_mode();
                auto new_m = static_cast<TreeViewSelectionMode>(m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change tree selection mode",
                    [tv, new_m]() { tv->set_selection_mode(new_m); },
                    [tv, old_m]() { tv->set_selection_mode(old_m); }));
            } else {
                TreeViewStyle old_s = tv->get_tree_view_style(), new_s = old_s;
                new_s.show_lines = m_property_grid->get_bool_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>("Change show lines",
                    [tv, new_s]() { tv->set_tree_view_style(new_s); },
                    [tv, old_s]() { tv->set_tree_view_style(old_s); }));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // Image: Name
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ImageName)) {
        if (auto* img = dynamic_cast<IGuiImage*>(w)) {
            std::string old_name = img->get_image_name();
            std::string new_name = m_property_grid->get_string_value(property_id);
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change image name",
                [img, new_name]() { img->set_image_name(new_name); },
                [img, old_name]() { img->set_image_name(old_name); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // Button: Transition
    // ----------------------------------------------------------------
    if (is(InspectorProperty::TransDuration)       ||
        is(InspectorProperty::TransNormalTint)     || is(InspectorProperty::TransNormalScale)    ||
        is(InspectorProperty::TransNormalOffsetX)  || is(InspectorProperty::TransNormalOffsetY)  ||
        is(InspectorProperty::TransHoverTint)      || is(InspectorProperty::TransHoverScale)     ||
        is(InspectorProperty::TransHoverOffsetX)   || is(InspectorProperty::TransHoverOffsetY)   ||
        is(InspectorProperty::TransPressedTint)    || is(InspectorProperty::TransPressedScale)   ||
        is(InspectorProperty::TransPressedOffsetX) || is(InspectorProperty::TransPressedOffsetY) ||
        is(InspectorProperty::TransDisabledTint)   || is(InspectorProperty::TransDisabledScale)  ||
        is(InspectorProperty::TransDisabledOffsetX)|| is(InspectorProperty::TransDisabledOffsetY)) {
        if (auto* btn = dynamic_cast<IGuiButton*>(w)) {
            ButtonTransition old_tr = btn->get_button_transition();
            ButtonTransition new_tr = old_tr;
            auto gpid = [&](InspectorProperty p) { return m_prop_ids[static_cast<int>(p)]; };
            new_tr.duration           = m_property_grid->get_float_value(gpid(InspectorProperty::TransDuration));
            new_tr.normal.tint        = m_property_grid->get_vec4_value(gpid(InspectorProperty::TransNormalTint));
            new_tr.normal.scale       = m_property_grid->get_float_value(gpid(InspectorProperty::TransNormalScale));
            math::set_x(new_tr.normal.offset,   m_property_grid->get_float_value(gpid(InspectorProperty::TransNormalOffsetX)));
            math::set_y(new_tr.normal.offset,   m_property_grid->get_float_value(gpid(InspectorProperty::TransNormalOffsetY)));
            new_tr.hovered.tint       = m_property_grid->get_vec4_value(gpid(InspectorProperty::TransHoverTint));
            new_tr.hovered.scale      = m_property_grid->get_float_value(gpid(InspectorProperty::TransHoverScale));
            math::set_x(new_tr.hovered.offset,  m_property_grid->get_float_value(gpid(InspectorProperty::TransHoverOffsetX)));
            math::set_y(new_tr.hovered.offset,  m_property_grid->get_float_value(gpid(InspectorProperty::TransHoverOffsetY)));
            new_tr.pressed.tint       = m_property_grid->get_vec4_value(gpid(InspectorProperty::TransPressedTint));
            new_tr.pressed.scale      = m_property_grid->get_float_value(gpid(InspectorProperty::TransPressedScale));
            math::set_x(new_tr.pressed.offset,  m_property_grid->get_float_value(gpid(InspectorProperty::TransPressedOffsetX)));
            math::set_y(new_tr.pressed.offset,  m_property_grid->get_float_value(gpid(InspectorProperty::TransPressedOffsetY)));
            new_tr.disabled.tint      = m_property_grid->get_vec4_value(gpid(InspectorProperty::TransDisabledTint));
            new_tr.disabled.scale     = m_property_grid->get_float_value(gpid(InspectorProperty::TransDisabledScale));
            math::set_x(new_tr.disabled.offset, m_property_grid->get_float_value(gpid(InspectorProperty::TransDisabledOffsetX)));
            math::set_y(new_tr.disabled.offset, m_property_grid->get_float_value(gpid(InspectorProperty::TransDisabledOffsetY)));
            m_history->execute(std::make_unique<ChangePropertyCommand>(
                "Change button transition",
                [btn, new_tr]() { btn->set_button_transition(new_tr); },
                [btn, old_tr]() { btn->set_button_transition(old_tr); }
            ));
        }
        return;
    }

    // ----------------------------------------------------------------
    // ProgressBar: Value / Mode
    // ----------------------------------------------------------------
    if (is(InspectorProperty::ProgressValue) ||
        is(InspectorProperty::ProgressModeEnum)) {
        if (auto* pb = dynamic_cast<IGuiProgressBar*>(w)) {
            if (is(InspectorProperty::ProgressModeEnum)) {
                ProgressBarMode old_mode = pb->get_mode();
                ProgressBarMode new_mode = static_cast<ProgressBarMode>(
                    m_property_grid->get_enum_index(property_id));
                m_history->execute(std::make_unique<ChangePropertyCommand>(
                    "Change progress mode",
                    [pb, new_mode]() { pb->set_mode(new_mode); },
                    [pb, old_mode]() { pb->set_mode(old_mode); }
                ));
            } else {
                float old_val = pb->get_value();
                float new_val = m_property_grid->get_float_value(property_id);
                m_history->execute(std::make_unique<ChangePropertyCommand>(
                    "Change progress value",
                    [pb, new_val]() { pb->set_value(new_val); },
                    [pb, old_val]() { pb->set_value(old_val); }
                ));
            }
        }
        return;
    }

    // ----------------------------------------------------------------
    // Geometry: X / Y / Width / Height
    // ----------------------------------------------------------------
    if (is(InspectorProperty::PosX)  || is(InspectorProperty::PosY) ||
        is(InspectorProperty::Width) || is(InspectorProperty::Height)) {
        math::Box old_bounds = w->get_bounds();
        float x  = m_property_grid->get_float_value(pid(InspectorProperty::PosX));
        float y  = m_property_grid->get_float_value(pid(InspectorProperty::PosY));
        float bw = m_property_grid->get_float_value(pid(InspectorProperty::Width));
        float bh = m_property_grid->get_float_value(pid(InspectorProperty::Height));
        math::Box new_bounds = math::make_box(x, y, bw, bh);
        m_history->execute(std::make_unique<MoveWidgetCommand>(w, old_bounds, new_bounds));
        return;
    }

    // Size mode
    if (is(InspectorProperty::WidthMode) || is(InspectorProperty::HeightMode)) {
        SizeMode old_w = w->get_width_mode();
        SizeMode old_h = w->get_height_mode();
        SizeMode new_w = static_cast<SizeMode>(m_property_grid->get_enum_index(pid(InspectorProperty::WidthMode)));
        SizeMode new_h = static_cast<SizeMode>(m_property_grid->get_enum_index(pid(InspectorProperty::HeightMode)));
        m_history->execute(std::make_unique<ChangeSizeModeCommand>(w, old_w, old_h, new_w, new_h));
        return;
    }

    // Alignment
    if (is(InspectorProperty::Alignment)) {
        Alignment old_al = w->get_alignment();
        Alignment new_al = static_cast<Alignment>(m_property_grid->get_enum_index(property_id));
        m_history->execute(std::make_unique<ChangePropertyCommand>(
            "Change alignment",
            [w, new_al]() { w->set_alignment(new_al); },
            [w, old_al]() { w->set_alignment(old_al); }
        ));
        return;
    }

    // Layout direction
    if (is(InspectorProperty::LayoutDirection)) {
        LayoutDirection old_dir = w->get_layout_direction();
        LayoutDirection new_dir = static_cast<LayoutDirection>(m_property_grid->get_enum_index(property_id));
        m_history->execute(std::make_unique<ChangeLayoutCommand>(w, old_dir, new_dir));
        return;
    }

    // Spacing
    if (is(InspectorProperty::Spacing)) {
        float old_sp = w->get_spacing();
        float new_sp = m_property_grid->get_float_value(property_id);
        m_history->execute(std::make_unique<ChangePropertyCommand>(
            "Change spacing",
            [w, new_sp]() { w->set_spacing(new_sp); },
            [w, old_sp]() { w->set_spacing(old_sp); }
        ));
        return;
    }

    // ----------------------------------------------------------------
    // Style properties
    // ----------------------------------------------------------------
    GuiStyle old_style = w->get_style();
    GuiStyle new_style = old_style;

    if      (is(InspectorProperty::BackgroundColor)) new_style.background_color = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::BorderColor))     new_style.border_color     = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::HoverColor))      new_style.hover_color      = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::PressedColor))    new_style.pressed_color    = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::DisabledColor))   new_style.disabled_color   = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::FocusColor))      new_style.focus_color      = m_property_grid->get_vec4_value(property_id);
    else if (is(InspectorProperty::BorderWidth))     new_style.border_width     = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::CornerRadius))    new_style.corner_radius    = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::PaddingLeft))     new_style.padding.x        = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::PaddingTop))      new_style.padding.y        = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::PaddingRight))    new_style.padding.z        = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::PaddingBottom))   new_style.padding.w        = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::MarginLeft))      new_style.margin.x         = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::MarginTop))       new_style.margin.y         = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::MarginRight))     new_style.margin.z         = m_property_grid->get_float_value(property_id);
    else if (is(InspectorProperty::MarginBottom))    new_style.margin.w         = m_property_grid->get_float_value(property_id);
    else return; // unknown

    m_history->execute(std::make_unique<ChangeStyleCommand>(w, old_style, new_style));
}

} // namespace editor
} // namespace gui
} // namespace window
