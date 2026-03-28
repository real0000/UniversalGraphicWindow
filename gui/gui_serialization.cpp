/*
 * gui_serialization.cpp - GUI Save/Load Implementation
 *
 * Self-contained JSON writer/parser and binary encoder/decoder for the GUI
 * widget tree.  No external JSON library required.
 */

#include "gui_serialization.hpp"
#include "gui.hpp"
#include "gui_widget_base.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <sstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace window {
namespace gui {

// ============================================================================
// String conversion
// ============================================================================

const char* gui_serialize_result_to_string(GuiSerializeResult r) {
    switch (r) {
        case GuiSerializeResult::Success:              return "Success";
        case GuiSerializeResult::ErrorFileOpen:         return "ErrorFileOpen";
        case GuiSerializeResult::ErrorFileWrite:        return "ErrorFileWrite";
        case GuiSerializeResult::ErrorFileRead:         return "ErrorFileRead";
        case GuiSerializeResult::ErrorParse:            return "ErrorParse";
        case GuiSerializeResult::ErrorVersionMismatch:  return "ErrorVersionMismatch";
        case GuiSerializeResult::ErrorInvalidData:      return "ErrorInvalidData";
        case GuiSerializeResult::ErrorOutOfMemory:      return "ErrorOutOfMemory";
        case GuiSerializeResult::ErrorInvalidParameter: return "ErrorInvalidParameter";
    }
    return "Unknown";
}

// ============================================================================
// Enum <-> string tables
// ============================================================================

static const char* widget_type_names[] = {
    "custom", "container", "panel", "button", "label", "text_input",
    "checkbox", "radio_button", "slider", "progress_bar", "scroll_area",
    "list_box", "combo_box", "tab_control", "tree_view", "image",
    "separator", "spacer"
};

static WidgetType parse_widget_type(const char* s) {
    for (int i = 0; i < (int)(sizeof(widget_type_names)/sizeof(widget_type_names[0])); ++i)
        if (std::strcmp(s, widget_type_names[i]) == 0) return static_cast<WidgetType>(i);
    return WidgetType::Custom;
}

static const char* widget_type_str(WidgetType t) {
    int i = static_cast<int>(t);
    if (i >= 0 && i < (int)(sizeof(widget_type_names)/sizeof(widget_type_names[0])))
        return widget_type_names[i];
    return "custom";
}

static const char* alignment_names[] = {
    "top_left", "top_center", "top_right",
    "center_left", "center", "center_right",
    "bottom_left", "bottom_center", "bottom_right"
};

static Alignment parse_alignment(const char* s) {
    for (int i = 0; i < 9; ++i)
        if (std::strcmp(s, alignment_names[i]) == 0) return static_cast<Alignment>(i);
    return Alignment::TopLeft;
}

static const char* alignment_str(Alignment a) {
    int i = static_cast<int>(a);
    return (i >= 0 && i < 9) ? alignment_names[i] : "top_left";
}

static const char* size_mode_names[] = { "fixed", "relative", "auto", "fill" };
static SizeMode parse_size_mode(const char* s) {
    for (int i = 0; i < 4; ++i)
        if (std::strcmp(s, size_mode_names[i]) == 0) return static_cast<SizeMode>(i);
    return SizeMode::Auto;
}
static const char* size_mode_str(SizeMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 4) ? size_mode_names[i] : "auto";
}

static const char* layout_dir_names[] = { "horizontal", "vertical" };
static LayoutDirection parse_layout_dir(const char* s) {
    return (s && std::strcmp(s, "vertical") == 0) ? LayoutDirection::Vertical : LayoutDirection::Horizontal;
}
static const char* layout_dir_str(LayoutDirection d) {
    return d == LayoutDirection::Vertical ? "vertical" : "horizontal";
}

static const char* button_type_names[] = { "normal", "toggle", "radio", "checkbox" };
static ButtonType parse_button_type(const char* s) {
    for (int i = 0; i < 4; ++i)
        if (std::strcmp(s, button_type_names[i]) == 0) return static_cast<ButtonType>(i);
    return ButtonType::Normal;
}
static const char* button_type_str(ButtonType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 4) ? button_type_names[i] : "normal";
}

static const char* slider_orient_names[] = { "horizontal", "vertical" };
static SliderOrientation parse_slider_orient(const char* s) {
    return (s && std::strcmp(s, "vertical") == 0) ? SliderOrientation::Vertical : SliderOrientation::Horizontal;
}
static const char* slider_orient_str(SliderOrientation o) {
    return o == SliderOrientation::Vertical ? "vertical" : "horizontal";
}

static const char* progress_mode_names[] = { "determinate", "indeterminate" };
static ProgressBarMode parse_progress_mode(const char* s) {
    return (s && std::strcmp(s, "indeterminate") == 0) ? ProgressBarMode::Indeterminate : ProgressBarMode::Determinate;
}
static const char* progress_mode_str(ProgressBarMode m) {
    return m == ProgressBarMode::Indeterminate ? "indeterminate" : "determinate";
}

static const char* color_picker_mode_names[] = { "hsv_square", "hsv_wheel", "rgb_sliders", "hsl_sliders", "palette" };
static ColorPickerMode parse_color_picker_mode(const char* s) {
    for (int i = 0; i < 5; ++i)
        if (std::strcmp(s, color_picker_mode_names[i]) == 0) return static_cast<ColorPickerMode>(i);
    return ColorPickerMode::HSVSquare;
}
static const char* color_picker_mode_str(ColorPickerMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 5) ? color_picker_mode_names[i] : "hsv_square";
}

static const char* tab_position_names[] = { "top", "bottom", "left", "right" };
static TabPosition parse_tab_position(const char* s) {
    for (int i = 0; i < 4; ++i)
        if (std::strcmp(s, tab_position_names[i]) == 0) return static_cast<TabPosition>(i);
    return TabPosition::Top;
}
static const char* tab_position_str(TabPosition p) {
    int i = static_cast<int>(p);
    return (i >= 0 && i < 4) ? tab_position_names[i] : "top";
}

static const char* tab_size_mode_names[] = { "fixed", "fit_content", "fill" };
static TabSizeMode parse_tab_size_mode(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, tab_size_mode_names[i]) == 0) return static_cast<TabSizeMode>(i);
    return TabSizeMode::FitContent;
}
static const char* tab_size_mode_str(TabSizeMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 3) ? tab_size_mode_names[i] : "fit_content";
}

static const char* split_orient_names[] = { "horizontal", "vertical" };
static SplitOrientation parse_split_orient(const char* s) {
    return (s && std::strcmp(s, "vertical") == 0) ? SplitOrientation::Vertical : SplitOrientation::Horizontal;
}
static const char* split_orient_str(SplitOrientation o) {
    return o == SplitOrientation::Vertical ? "vertical" : "horizontal";
}

static const char* split_unit_names[] = { "pixels", "ratio" };
static SplitSizeUnit parse_split_unit(const char* s) {
    return (s && std::strcmp(s, "ratio") == 0) ? SplitSizeUnit::Ratio : SplitSizeUnit::Pixels;
}
static const char* split_unit_str(SplitSizeUnit u) {
    return u == SplitSizeUnit::Ratio ? "ratio" : "pixels";
}

static const char* listbox_sel_names[] = { "single", "multi", "none" };
static ListBoxSelectionMode parse_listbox_sel(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, listbox_sel_names[i]) == 0) return static_cast<ListBoxSelectionMode>(i);
    return ListBoxSelectionMode::Single;
}
static const char* listbox_sel_str(ListBoxSelectionMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 3) ? listbox_sel_names[i] : "single";
}

static const char* treeview_sel_names[] = { "single", "multi", "none" };
static TreeViewSelectionMode parse_treeview_sel(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, treeview_sel_names[i]) == 0) return static_cast<TreeViewSelectionMode>(i);
    return TreeViewSelectionMode::Single;
}
static const char* treeview_sel_str(TreeViewSelectionMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 3) ? treeview_sel_names[i] : "single";
}

static const char* scrollbar_vis_names[] = { "auto", "always", "never" };
static ScrollBarVisibility parse_scrollbar_vis(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, scrollbar_vis_names[i]) == 0) return static_cast<ScrollBarVisibility>(i);
    return ScrollBarVisibility::Auto;
}
static const char* scrollbar_vis_str(ScrollBarVisibility v) {
    int i = static_cast<int>(v);
    return (i >= 0 && i < 3) ? scrollbar_vis_names[i] : "auto";
}

static const char* scrollbar_orient_names[] = { "horizontal", "vertical" };
static ScrollBarOrientation parse_scrollbar_orient(const char* s) {
    return (s && std::strcmp(s, "vertical") == 0) ? ScrollBarOrientation::Vertical : ScrollBarOrientation::Horizontal;
}
static const char* scrollbar_orient_str(ScrollBarOrientation o) {
    return o == ScrollBarOrientation::Vertical ? "vertical" : "horizontal";
}

static const char* dialog_buttons_names[] = {
    "none", "ok", "ok_cancel", "yes_no", "yes_no_cancel",
    "retry_cancel", "abort_retry_ignore", "custom"
};
static DialogButtons parse_dialog_buttons(const char* s) {
    for (int i = 0; i < 8; ++i)
        if (std::strcmp(s, dialog_buttons_names[i]) == 0) return static_cast<DialogButtons>(i);
    return DialogButtons::OK;
}
static const char* dialog_buttons_str(DialogButtons b) {
    int i = static_cast<int>(b);
    return (i >= 0 && i < 8) ? dialog_buttons_names[i] : "ok";
}

static const char* menu_item_type_names[] = { "normal", "checkbox", "radio", "separator", "submenu" };
static MenuItemType parse_menu_item_type(const char* s) {
    for (int i = 0; i < 5; ++i)
        if (std::strcmp(s, menu_item_type_names[i]) == 0) return static_cast<MenuItemType>(i);
    return MenuItemType::Normal;
}
static const char* menu_item_type_str(MenuItemType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 5) ? menu_item_type_names[i] : "normal";
}

static const char* toolbar_orient_names[] = { "horizontal", "vertical" };
static ToolbarOrientation parse_toolbar_orient(const char* s) {
    return (s && std::strcmp(s, "vertical") == 0) ? ToolbarOrientation::Vertical : ToolbarOrientation::Horizontal;
}
static const char* toolbar_orient_str(ToolbarOrientation o) {
    return o == ToolbarOrientation::Vertical ? "vertical" : "horizontal";
}

static const char* toolbar_item_type_names[] = { "button", "toggle_button", "separator", "widget" };
static const char* toolbar_item_type_str(ToolbarItemType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 4) ? toolbar_item_type_names[i] : "button";
}

static const char* statusbar_size_mode_names[] = { "fixed", "auto", "fill" };
static StatusBarPanelSizeMode parse_statusbar_size_mode(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, statusbar_size_mode_names[i]) == 0) return static_cast<StatusBarPanelSizeMode>(i);
    return StatusBarPanelSizeMode::Auto;
}
static const char* statusbar_size_mode_str(StatusBarPanelSizeMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 3) ? statusbar_size_mode_names[i] : "auto";
}

static const char* editbox_wrap_names[] = { "none", "word", "character" };
static EditBoxWordWrap parse_editbox_wrap(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, editbox_wrap_names[i]) == 0) return static_cast<EditBoxWordWrap>(i);
    return EditBoxWordWrap::None;
}
static const char* editbox_wrap_str(EditBoxWordWrap w) {
    int i = static_cast<int>(w);
    return (i >= 0 && i < 3) ? editbox_wrap_names[i] : "none";
}

static const char* slice_center_mode_names[] = { "stretch", "tile", "hidden" };
static SliceCenterMode parse_slice_center_mode(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, slice_center_mode_names[i]) == 0) return static_cast<SliceCenterMode>(i);
    return SliceCenterMode::Stretch;
}
static const char* slice_center_mode_str(SliceCenterMode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < 3) ? slice_center_mode_names[i] : "stretch";
}

static const char* page_transition_names[] = {
    "none", "fade", "slide_left", "slide_right", "slide_up",
    "slide_down", "push", "pop", "zoom", "flip", "custom"
};
static PageTransitionType parse_page_transition(const char* s) {
    for (int i = 0; i < 11; ++i)
        if (std::strcmp(s, page_transition_names[i]) == 0) return static_cast<PageTransitionType>(i);
    return PageTransitionType::None;
}
static const char* page_transition_str(PageTransitionType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 11) ? page_transition_names[i] : "none";
}

static const char* anim_easing_names[] = {
    "linear", "ease_in", "ease_out", "ease_in_out",
    "ease_in_quad", "ease_out_quad", "ease_in_out_quad",
    "ease_in_cubic", "ease_out_cubic", "ease_in_out_cubic",
    "ease_in_elastic", "ease_out_elastic", "ease_in_out_elastic",
    "ease_in_bounce", "ease_out_bounce", "ease_in_out_bounce"
};
static AnimationEasing parse_anim_easing(const char* s) {
    for (int i = 0; i < 16; ++i)
        if (std::strcmp(s, anim_easing_names[i]) == 0) return static_cast<AnimationEasing>(i);
    return AnimationEasing::Linear;
}
static const char* anim_easing_str(AnimationEasing e) {
    int i = static_cast<int>(e);
    return (i >= 0 && i < 16) ? anim_easing_names[i] : "linear";
}

static const char* anim_target_names[] = {
    "position_x", "position_y", "position", "width", "height", "size",
    "opacity", "color_r", "color_g", "color_b", "color_a", "color",
    "rotation", "scale_x", "scale_y", "scale"
};
static AnimationTarget parse_anim_target(const char* s) {
    for (int i = 0; i < 16; ++i)
        if (std::strcmp(s, anim_target_names[i]) == 0) return static_cast<AnimationTarget>(i);
    return AnimationTarget::PositionX;
}
static const char* anim_target_str(AnimationTarget t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 16) ? anim_target_names[i] : "position_x";
}

static const char* anim_loop_names[] = { "none", "loop", "ping_pong" };
static AnimationLoop parse_anim_loop(const char* s) {
    for (int i = 0; i < 3; ++i)
        if (std::strcmp(s, anim_loop_names[i]) == 0) return static_cast<AnimationLoop>(i);
    return AnimationLoop::None;
}
static const char* anim_loop_str(AnimationLoop l) {
    int i = static_cast<int>(l);
    return (i >= 0 && i < 3) ? anim_loop_names[i] : "none";
}

static const char* scroll_view_size_names[] = { "compact", "small", "medium", "large", "custom" };
static ScrollViewSize parse_scroll_view_size(const char* s) {
    for (int i = 0; i < 5; ++i)
        if (std::strcmp(s, scroll_view_size_names[i]) == 0) return static_cast<ScrollViewSize>(i);
    return ScrollViewSize::Medium;
}
static const char* scroll_view_size_str(ScrollViewSize sz) {
    int i = static_cast<int>(sz);
    return (i >= 0 && i < 5) ? scroll_view_size_names[i] : "medium";
}

static const char* property_type_names[] = {
    "string", "int", "float", "bool", "color", "vec2", "vec4", "enum", "range", "category"
};
static PropertyType parse_property_type(const char* s) {
    for (int i = 0; i < 10; ++i)
        if (std::strcmp(s, property_type_names[i]) == 0) return static_cast<PropertyType>(i);
    return PropertyType::String;
}
static const char* property_type_str(PropertyType t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 10) ? property_type_names[i] : "string";
}

// ============================================================================
// JSON via Boost.PropertyTree
// ============================================================================

namespace pt = boost::property_tree;

// --- ptree write helpers ---

static void pt_put_float(pt::ptree& node, const char* key, float v) {
    char buf[64];
    if (v == std::floor(v) && std::abs(v) < 1e15f)
        snprintf(buf, sizeof(buf), "%.1f", (double)v);
    else
        snprintf(buf, sizeof(buf), "%.6g", (double)v);
    node.put(key, std::string(buf));
}

static pt::ptree pt_make_float(float v) {
    pt::ptree node;
    char buf[64];
    if (v == std::floor(v) && std::abs(v) < 1e15f)
        snprintf(buf, sizeof(buf), "%.1f", (double)v);
    else
        snprintf(buf, sizeof(buf), "%.6g", (double)v);
    node.put_value(std::string(buf));
    return node;
}

static void pt_put_vec2(pt::ptree& node, const char* key, const math::Vec2& v) {
    pt::ptree arr;
    arr.push_back(std::make_pair("", pt_make_float(math::x(v))));
    arr.push_back(std::make_pair("", pt_make_float(math::y(v))));
    node.add_child(key, arr);
}

static void pt_put_vec4(pt::ptree& node, const char* key, const math::Vec4& v) {
    pt::ptree arr;
    arr.push_back(std::make_pair("", pt_make_float(v.x)));
    arr.push_back(std::make_pair("", pt_make_float(v.y)));
    arr.push_back(std::make_pair("", pt_make_float(v.z)));
    arr.push_back(std::make_pair("", pt_make_float(v.w)));
    node.add_child(key, arr);
}

static void pt_put_box(pt::ptree& node, const char* key, const math::Box& b) {
    math::Vec2 mn = math::box_min(b);
    math::Vec2 sz = math::box_size(b);
    pt::ptree arr;
    arr.push_back(std::make_pair("", pt_make_float(math::x(mn))));
    arr.push_back(std::make_pair("", pt_make_float(math::y(mn))));
    arr.push_back(std::make_pair("", pt_make_float(math::x(sz))));
    arr.push_back(std::make_pair("", pt_make_float(math::y(sz))));
    node.add_child(key, arr);
}

// --- ptree read helpers ---

static float pt_get_float(const pt::ptree& node, const char* key, float def = 0.0f) {
    auto opt = node.get_optional<std::string>(key);
    if (!opt) return def;
    try { return std::stof(*opt); } catch (...) { return def; }
}

static int pt_get_int(const pt::ptree& node, const char* key, int def = 0) {
    auto opt = node.get_optional<std::string>(key);
    if (!opt) return def;
    try { return std::stoi(*opt); } catch (...) { return def; }
}

static bool pt_get_bool(const pt::ptree& node, const char* key, bool def = false) {
    auto opt = node.get_optional<std::string>(key);
    if (!opt) return def;
    return (*opt == "true" || *opt == "1");
}

static std::string pt_get_string(const pt::ptree& node, const char* key, const char* def = "") {
    return node.get<std::string>(key, def ? def : "");
}

static bool pt_has(const pt::ptree& node, const char* key) {
    return node.find(key) != node.not_found();
}

static const pt::ptree* pt_get_child(const pt::ptree& node, const char* key) {
    auto it = node.find(key);
    if (it == node.not_found()) return nullptr;
    return &it->second;
}

static float pt_arr_float(const pt::ptree& elem) {
    try { return std::stof(elem.get_value<std::string>()); } catch (...) { return 0.0f; }
}

static math::Vec2 pt_get_vec2(const pt::ptree& node, const char* key, math::Vec2 def = math::Vec2(0,0)) {
    auto it = node.find(key);
    if (it == node.not_found()) return def;
    auto& arr = it->second;
    if (arr.size() < 2) return def;
    auto ci = arr.begin();
    float x = pt_arr_float(ci->second); ++ci;
    float y = pt_arr_float(ci->second);
    return math::Vec2(x, y);
}

static math::Vec4 pt_get_vec4(const pt::ptree& node, const char* key, math::Vec4 def = math::Vec4(0,0,0,0)) {
    auto it = node.find(key);
    if (it == node.not_found()) return def;
    auto& arr = it->second;
    if (arr.size() < 4) return def;
    auto ci = arr.begin();
    float x = pt_arr_float(ci->second); ++ci;
    float y = pt_arr_float(ci->second); ++ci;
    float z = pt_arr_float(ci->second); ++ci;
    float w = pt_arr_float(ci->second);
    return math::Vec4(x, y, z, w);
}

static math::Box pt_get_box(const pt::ptree& node, const char* key, math::Box def = math::Box()) {
    auto it = node.find(key);
    if (it == node.not_found()) return def;
    auto& arr = it->second;
    if (arr.size() < 4) return def;
    auto ci = arr.begin();
    float a = pt_arr_float(ci->second); ++ci;
    float b = pt_arr_float(ci->second); ++ci;
    float c = pt_arr_float(ci->second); ++ci;
    float d = pt_arr_float(ci->second);
    return math::make_box(a, b, c, d);
}

// ============================================================================
// Binary Writer / Reader helpers
// ============================================================================

class BinaryWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }
    void write_u16(uint16_t v) { write_bytes(&v, 2); }
    void write_u32(uint32_t v) { write_bytes(&v, 4); }
    void write_i32(int32_t v) { write_bytes(&v, 4); }
    void write_f32(float v) { write_bytes(&v, 4); }
    void write_bool(bool v) { write_u8(v ? 1 : 0); }
    void write_string(const char* s) {
        uint32_t len = s ? (uint32_t)std::strlen(s) : 0;
        write_u32(len);
        if (len > 0) {
            size_t offset = buf_.size();
            buf_.resize(buf_.size() + len);
            std::memcpy(buf_.data() + offset, s, len);
        }
    }
    void write_vec2(const math::Vec2& v) { write_f32(math::x(v)); write_f32(math::y(v)); }
    void write_vec4(const math::Vec4& v) { write_f32(v.x); write_f32(v.y); write_f32(v.z); write_f32(v.w); }
    void write_box(const math::Box& b) {
        math::Vec2 mn = math::box_min(b);
        math::Vec2 sz = math::box_size(b);
        write_f32(math::x(mn)); write_f32(math::y(mn));
        write_f32(math::x(sz)); write_f32(math::y(sz));
    }

    const std::vector<uint8_t>& data() const { return buf_; }

private:
    std::vector<uint8_t> buf_;

    void write_bytes(const void* src, size_t n) {
        size_t offset = buf_.size();
        buf_.resize(buf_.size() + n);
        std::memcpy(buf_.data() + offset, src, n);
    }
};

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}

    bool ok() const { return !err_; }
    size_t remaining() const { return err_ ? 0 : (size_ - pos_); }

    uint8_t read_u8() { uint8_t v = 0; read_bytes(&v, 1); return v; }
    uint16_t read_u16() { uint16_t v = 0; read_bytes(&v, 2); return v; }
    uint32_t read_u32() { uint32_t v = 0; read_bytes(&v, 4); return v; }
    int32_t read_i32() { int32_t v = 0; read_bytes(&v, 4); return v; }
    float read_f32() { float v = 0; read_bytes(&v, 4); return v; }
    bool read_bool() { return read_u8() != 0; }
    std::string read_string() {
        uint32_t len = read_u32();
        if (err_ || pos_ + len > size_) { err_ = true; return ""; }
        std::string s((const char*)data_ + pos_, len);
        pos_ += len;
        return s;
    }
    math::Vec2 read_vec2() { float x = read_f32(), y = read_f32(); return math::Vec2(x, y); }
    math::Vec4 read_vec4() { float x = read_f32(), y = read_f32(), z = read_f32(), w = read_f32(); return math::Vec4(x, y, z, w); }
    math::Box read_box() {
        float x = read_f32(), y = read_f32(), w = read_f32(), h = read_f32();
        return math::make_box(x, y, w, h);
    }

private:
    const uint8_t* data_;
    size_t size_, pos_;
    bool err_ = false;

    void read_bytes(void* dst, size_t n) {
        if (pos_ + n > size_) { err_ = true; return; }
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }
};

// Binary magic number: "WGUI"
static constexpr uint32_t BINARY_MAGIC = 0x49554757; // 'W','G','U','I' little-endian

// ============================================================================
// JSON Serialization - Write (ptree)
// ============================================================================

static pt::ptree write_gui_style_pt(const GuiStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "background_color", s.background_color);
    pt_put_vec4(n, "border_color", s.border_color);
    pt_put_vec4(n, "hover_color", s.hover_color);
    pt_put_vec4(n, "pressed_color", s.pressed_color);
    pt_put_vec4(n, "disabled_color", s.disabled_color);
    pt_put_vec4(n, "focus_color", s.focus_color);
    pt_put_float(n, "border_width", s.border_width);
    pt_put_float(n, "corner_radius", s.corner_radius);
    pt_put_vec4(n, "padding", s.padding);
    pt_put_vec4(n, "margin", s.margin);
    return n;
}

static pt::ptree write_label_style_pt(const LabelStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "text_color", s.text_color);
    pt_put_vec4(n, "selection_color", s.selection_color);
    pt_put_float(n, "font_size", s.font_size);
    if (s.font_name) n.put("font_name", s.font_name);
    n.put("alignment", alignment_str(s.alignment));
    n.put("wrap", s.wrap ? "true" : "false");
    n.put("ellipsis", s.ellipsis ? "true" : "false");
    return n;
}

static pt::ptree write_button_style_pt(const ButtonStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "background_color", s.background_color);
    pt_put_vec4(n, "hover_color", s.hover_color);
    pt_put_vec4(n, "pressed_color", s.pressed_color);
    pt_put_vec4(n, "disabled_color", s.disabled_color);
    pt_put_vec4(n, "checked_color", s.checked_color);
    pt_put_vec4(n, "text_color", s.text_color);
    pt_put_vec4(n, "text_disabled_color", s.text_disabled_color);
    pt_put_vec4(n, "border_color", s.border_color);
    pt_put_vec4(n, "focus_border_color", s.focus_border_color);
    pt_put_float(n, "border_width", s.border_width);
    pt_put_float(n, "corner_radius", s.corner_radius);
    pt_put_float(n, "padding", s.padding);
    pt_put_float(n, "icon_size", s.icon_size);
    pt_put_float(n, "icon_text_spacing", s.icon_text_spacing);
    pt_put_float(n, "font_size", s.font_size);
    return n;
}

static pt::ptree write_slider_style_pt(const SliderStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "track_color", s.track_color);
    pt_put_vec4(n, "track_fill_color", s.track_fill_color);
    pt_put_vec4(n, "thumb_color", s.thumb_color);
    pt_put_vec4(n, "thumb_hover_color", s.thumb_hover_color);
    pt_put_vec4(n, "thumb_pressed_color", s.thumb_pressed_color);
    pt_put_vec4(n, "tick_color", s.tick_color);
    pt_put_float(n, "track_height", s.track_height);
    pt_put_float(n, "thumb_radius", s.thumb_radius);
    pt_put_float(n, "tick_length", s.tick_length);
    pt_put_float(n, "tick_width", s.tick_width);
    pt_put_float(n, "track_corner_radius", s.track_corner_radius);
    return n;
}

static pt::ptree write_progress_style_pt(const ProgressBarStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "track_color", s.track_color);
    pt_put_vec4(n, "fill_color", s.fill_color);
    pt_put_vec4(n, "indeterminate_color", s.indeterminate_color);
    pt_put_vec4(n, "text_color", s.text_color);
    pt_put_float(n, "height", s.height);
    pt_put_float(n, "corner_radius", s.corner_radius);
    pt_put_float(n, "indeterminate_width", s.indeterminate_width);
    return n;
}

static pt::ptree write_tab_style_pt(const TabStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "tab_background", s.tab_background);
    pt_put_vec4(n, "tab_hover_background", s.tab_hover_background);
    pt_put_vec4(n, "tab_active_background", s.tab_active_background);
    pt_put_vec4(n, "tab_text_color", s.tab_text_color);
    pt_put_vec4(n, "tab_active_text_color", s.tab_active_text_color);
    pt_put_vec4(n, "tab_bar_background", s.tab_bar_background);
    pt_put_vec4(n, "indicator_color", s.indicator_color);
    pt_put_vec4(n, "close_button_color", s.close_button_color);
    pt_put_vec4(n, "close_button_hover_color", s.close_button_hover_color);
    pt_put_float(n, "tab_height", s.tab_height);
    pt_put_float(n, "tab_min_width", s.tab_min_width);
    pt_put_float(n, "tab_max_width", s.tab_max_width);
    pt_put_float(n, "tab_padding", s.tab_padding);
    pt_put_float(n, "indicator_height", s.indicator_height);
    pt_put_float(n, "icon_size", s.icon_size);
    pt_put_float(n, "close_button_size", s.close_button_size);
    pt_put_float(n, "corner_radius", s.corner_radius);
    pt_put_float(n, "font_size", s.font_size);
    return n;
}

static pt::ptree write_splitter_style_pt(const SplitterStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "splitter_color", s.splitter_color);
    pt_put_vec4(n, "splitter_hover_color", s.splitter_hover_color);
    pt_put_vec4(n, "splitter_drag_color", s.splitter_drag_color);
    pt_put_vec4(n, "grip_color", s.grip_color);
    pt_put_float(n, "splitter_thickness", s.splitter_thickness);
    pt_put_float(n, "hit_area_thickness", s.hit_area_thickness);
    pt_put_float(n, "grip_length", s.grip_length);
    pt_put_float(n, "grip_dot_size", s.grip_dot_size);
    n.put("grip_dot_count", s.grip_dot_count);
    return n;
}

static pt::ptree write_listbox_style_pt(const ListBoxStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "row_background", s.row_background);
    pt_put_vec4(n, "row_alt_background", s.row_alt_background);
    pt_put_vec4(n, "selected_background", s.selected_background);
    pt_put_vec4(n, "hover_background", s.hover_background);
    pt_put_vec4(n, "text_color", s.text_color);
    pt_put_vec4(n, "selected_text_color", s.selected_text_color);
    pt_put_vec4(n, "icon_color", s.icon_color);
    pt_put_vec4(n, "separator_color", s.separator_color);
    pt_put_float(n, "row_height", s.row_height);
    pt_put_float(n, "icon_size", s.icon_size);
    pt_put_float(n, "item_padding", s.item_padding);
    pt_put_float(n, "font_size", s.font_size);
    n.put("show_separator", s.show_separator ? "true" : "false");
    return n;
}

static pt::ptree write_treeview_style_pt(const TreeViewStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "row_background", s.row_background);
    pt_put_vec4(n, "row_alt_background", s.row_alt_background);
    pt_put_vec4(n, "selected_background", s.selected_background);
    pt_put_vec4(n, "hover_background", s.hover_background);
    pt_put_vec4(n, "text_color", s.text_color);
    pt_put_vec4(n, "icon_color", s.icon_color);
    pt_put_vec4(n, "line_color", s.line_color);
    pt_put_float(n, "row_height", s.row_height);
    pt_put_float(n, "indent_width", s.indent_width);
    pt_put_float(n, "icon_size", s.icon_size);
    pt_put_float(n, "font_size", s.font_size);
    n.put("show_lines", s.show_lines ? "true" : "false");
    n.put("show_root_lines", s.show_root_lines ? "true" : "false");
    return n;
}

static pt::ptree write_dialog_style_pt(const DialogStyle& s) {
    pt::ptree n;
    pt_put_vec4(n, "overlay_color", s.overlay_color);
    pt_put_vec4(n, "background_color", s.background_color);
    pt_put_vec4(n, "border_color", s.border_color);
    pt_put_vec4(n, "title_bar_color", s.title_bar_color);
    pt_put_vec4(n, "title_text_color", s.title_text_color);
    pt_put_vec4(n, "shadow_color", s.shadow_color);
    pt_put_float(n, "border_width", s.border_width);
    pt_put_float(n, "corner_radius", s.corner_radius);
    pt_put_float(n, "title_bar_height", s.title_bar_height);
    pt_put_float(n, "button_area_height", s.button_area_height);
    pt_put_float(n, "padding", s.padding);
    pt_put_float(n, "shadow_offset", s.shadow_offset);
    pt_put_float(n, "shadow_blur", s.shadow_blur);
    pt_put_float(n, "min_width", s.min_width);
    pt_put_float(n, "min_height", s.min_height);
    pt_put_float(n, "font_size", s.font_size);
    pt_put_float(n, "title_font_size", s.title_font_size);
    return n;
}

static pt::ptree write_page_transition_pt(const PageTransition& t) {
    pt::ptree n;
    n.put("type", page_transition_str(t.type));
    pt_put_float(n, "duration", t.duration);
    n.put("easing", anim_easing_str(t.easing));
    return n;
}

// Forward declaration
static pt::ptree write_widget_pt(const IGuiWidget* widget, const GuiSerializeOptions& opts);

static void write_base_props_pt(pt::ptree& n, const IGuiWidget* widget, const GuiSerializeOptions& opts) {
    n.put("type", widget_type_str(widget->get_type()));
    if (widget->get_name() && widget->get_name()[0])
        n.put("name", widget->get_name());
    pt_put_box(n, "bounds", widget->get_bounds());
    pt_put_vec2(n, "min_size", widget->get_min_size());
    pt_put_vec2(n, "max_size", widget->get_max_size());
    n.put("visible", widget->is_visible() ? "true" : "false");
    n.put("enabled", widget->is_enabled() ? "true" : "false");
    n.put("focusable", widget->is_focusable() ? "true" : "false");
    n.put("width_mode", size_mode_str(widget->get_width_mode()));
    n.put("height_mode", size_mode_str(widget->get_height_mode()));
    n.put("alignment", alignment_str(widget->get_alignment()));
    n.put("layout_direction", layout_dir_str(widget->get_layout_direction()));
    pt_put_float(n, "spacing", widget->get_spacing());
    n.put("clip_enabled", widget->is_clip_enabled() ? "true" : "false");
    if (widget->is_clip_enabled())
        pt_put_box(n, "clip_rect", widget->get_clip_rect());

    if (opts.save_styles) {
        n.add_child("style", write_gui_style_pt(widget->get_style()));
    }
}

static void write_widget_specific_pt(pt::ptree& n, const IGuiWidget* widget, const GuiSerializeOptions& opts) {
    WidgetType type = widget->get_type();

    switch (type) {
    case WidgetType::Button: {
        auto* btn = static_cast<const IGuiButton*>(widget);
        n.put("button_type", button_type_str(btn->get_button_type()));
        if (btn->get_text() && btn->get_text()[0])
            n.put("text", btn->get_text());
        if (btn->get_icon() && btn->get_icon()[0])
            n.put("icon", btn->get_icon());
        n.put("checked", btn->is_checked() ? "true" : "false");
        n.put("radio_group", btn->get_radio_group());
        if (opts.save_styles)
            n.add_child("button_style", write_button_style_pt(btn->get_button_style()));
        break;
    }
    case WidgetType::Label: {
        auto* lbl = static_cast<const IGuiLabel*>(widget);
        if (lbl->get_text() && lbl->get_text()[0])
            n.put("text", lbl->get_text());
        if (opts.save_styles)
            n.add_child("label_style", write_label_style_pt(lbl->get_label_style()));
        break;
    }
    case WidgetType::TextInput: {
        auto* inp = static_cast<const IGuiTextInput*>(widget);
        if (inp->get_text() && inp->get_text()[0])
            n.put("text", inp->get_text());
        if (inp->get_placeholder() && inp->get_placeholder()[0])
            n.put("placeholder", inp->get_placeholder());
        n.put("password_mode", inp->is_password_mode() ? "true" : "false");
        n.put("read_only", inp->is_read_only() ? "true" : "false");
        n.put("max_length", inp->get_max_length());
        if (opts.save_styles)
            n.add_child("label_style", write_label_style_pt(inp->get_label_style()));
        break;
    }
    case WidgetType::Slider: {
        auto* sl = static_cast<const IGuiSlider*>(widget);
        n.put("orientation", slider_orient_str(sl->get_orientation()));
        pt_put_float(n, "value", sl->get_value());
        pt_put_float(n, "min_value", sl->get_min_value());
        pt_put_float(n, "max_value", sl->get_max_value());
        pt_put_float(n, "step", sl->get_step());
        n.put("ticks_visible", sl->is_ticks_visible() ? "true" : "false");
        pt_put_float(n, "tick_interval", sl->get_tick_interval());
        if (opts.save_styles)
            n.add_child("slider_style", write_slider_style_pt(sl->get_slider_style()));
        break;
    }
    case WidgetType::ProgressBar: {
        auto* pb = static_cast<const IGuiProgressBar*>(widget);
        n.put("mode", progress_mode_str(pb->get_mode()));
        pt_put_float(n, "value", pb->get_value());
        n.put("text_visible", pb->is_text_visible() ? "true" : "false");
        if (pb->get_text() && pb->get_text()[0])
            n.put("text", pb->get_text());
        if (opts.save_styles)
            n.add_child("progress_style", write_progress_style_pt(pb->get_progress_bar_style()));
        break;
    }
    case WidgetType::Image: {
        auto* img = static_cast<const IGuiImage*>(widget);
        n.put("image_name", img->get_image_name());
        pt_put_vec4(n, "tint", img->get_tint());
        n.put("use_slice9", img->get_use_slice9() ? "true" : "false");
        if (img->get_use_slice9()) {
            auto& sb = img->get_slice_border();
            pt::ptree arr;
            arr.push_back(std::make_pair("", pt_make_float(sb.left)));
            arr.push_back(std::make_pair("", pt_make_float(sb.top)));
            arr.push_back(std::make_pair("", pt_make_float(sb.right)));
            arr.push_back(std::make_pair("", pt_make_float(sb.bottom)));
            n.add_child("slice_border", arr);
            n.put("slice_center_mode", slice_center_mode_str(img->get_slice_center_mode()));
            pt_put_vec2(n, "source_size", img->get_source_size());
        }
        break;
    }
    case WidgetType::ScrollArea: {
        auto* sv = dynamic_cast<const IGuiScrollView*>(widget);
        if (sv) {
            pt_put_vec2(n, "scroll_offset", sv->get_scroll_offset());
            pt_put_vec2(n, "content_size", sv->get_content_size());
            n.put("h_scrollbar_visibility", scrollbar_vis_str(sv->get_h_scrollbar_visibility()));
            n.put("v_scrollbar_visibility", scrollbar_vis_str(sv->get_v_scrollbar_visibility()));
            pt_put_float(n, "scroll_speed", sv->get_scroll_speed());
            n.put("scroll_inertia_enabled", sv->is_scroll_inertia_enabled() ? "true" : "false");
            n.put("size", scroll_view_size_str(sv->get_size()));
        }
        break;
    }
    case WidgetType::ListBox: {
        auto* lb = static_cast<const IGuiListBox*>(widget);
        n.put("selection_mode", listbox_sel_str(lb->get_selection_mode()));
        n.put("selected_item", lb->get_selected_item());
        if (opts.save_items) {
            pt::ptree items_arr;
            int count = lb->get_item_count();
            for (int i = 0; i < count; ++i) {
                pt::ptree item;
                const char* text = lb->get_item_text(i);
                const char* icon = lb->get_item_icon(i);
                if (text) item.put("text", text);
                if (icon && icon[0]) item.put("icon", icon);
                item.put("enabled", lb->is_item_enabled(i) ? "true" : "false");
                items_arr.push_back(std::make_pair("", item));
            }
            n.add_child("items", items_arr);
        }
        if (opts.save_styles)
            n.add_child("list_style", write_listbox_style_pt(lb->get_list_box_style()));
        break;
    }
    case WidgetType::ComboBox: {
        auto* cb = static_cast<const IGuiComboBox*>(widget);
        n.put("selected_item", cb->get_selected_item());
        if (cb->get_placeholder() && cb->get_placeholder()[0])
            n.put("placeholder", cb->get_placeholder());
        if (opts.save_items) {
            pt::ptree items_arr;
            int count = cb->get_item_count();
            for (int i = 0; i < count; ++i) {
                pt::ptree item;
                const char* text = cb->get_item_text(i);
                const char* icon = cb->get_item_icon(i);
                if (text) item.put("text", text);
                if (icon && icon[0]) item.put("icon", icon);
                item.put("enabled", cb->is_item_enabled(i) ? "true" : "false");
                items_arr.push_back(std::make_pair("", item));
            }
            n.add_child("items", items_arr);
        }
        break;
    }
    case WidgetType::TabControl: {
        auto* tc = static_cast<const IGuiTabControl*>(widget);
        n.put("tab_position", tab_position_str(tc->get_tab_position()));
        n.put("tab_size_mode", tab_size_mode_str(tc->get_tab_size_mode()));
        pt_put_float(n, "fixed_tab_width", tc->get_fixed_tab_width());
        n.put("active_tab", tc->get_active_tab());
        n.put("drag_reorder_enabled", tc->is_drag_reorder_enabled() ? "true" : "false");
        if (opts.save_items) {
            pt::ptree tabs_arr;
            int count = tc->get_tab_count();
            for (int i = 0; i < count; ++i) {
                pt::ptree tab;
                const char* text = tc->get_tab_text(i);
                const char* icon = tc->get_tab_icon(i);
                if (text) tab.put("text", text);
                if (icon && icon[0]) tab.put("icon", icon);
                tab.put("enabled", tc->is_tab_enabled(i) ? "true" : "false");
                tab.put("closable", tc->is_tab_closable(i) ? "true" : "false");
                // Serialize tab content subtree
                IGuiWidget* content = tc->get_tab_content(i);
                if (content) {
                    tab.add_child("content", write_widget_pt(content, opts));
                }
                tabs_arr.push_back(std::make_pair("", tab));
            }
            n.add_child("tabs", tabs_arr);
        }
        if (opts.save_styles)
            n.add_child("tab_style", write_tab_style_pt(tc->get_tab_style()));
        break;
    }
    case WidgetType::TreeView: {
        auto* tv = static_cast<const IGuiTreeView*>(widget);
        n.put("selection_mode", treeview_sel_str(tv->get_selection_mode()));
        n.put("selected_node", tv->get_selected_node());
        n.put("drag_reorder_enabled", tv->is_drag_reorder_enabled() ? "true" : "false");
        if (opts.save_items) {
            // Serialize tree nodes using a flat list with parent references
            pt::ptree nodes_arr;
            int root_count = tv->get_root_node_count();
            // Use BFS to serialize all nodes
            std::vector<int> queue;
            for (int i = 0; i < root_count; ++i)
                queue.push_back(tv->get_root_node(i));
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                int nid = queue[qi];
                pt::ptree node;
                node.put("id", nid);
                node.put("parent", tv->get_node_parent(nid));
                const char* text = tv->get_node_text(nid);
                const char* icon = tv->get_node_icon(nid);
                if (text) node.put("text", text);
                if (icon && icon[0]) node.put("icon", icon);
                node.put("expanded", tv->is_node_expanded(nid) ? "true" : "false");
                node.put("enabled", tv->is_node_enabled(nid) ? "true" : "false");
                nodes_arr.push_back(std::make_pair("", node));
                int child_count = tv->get_node_child_count(nid);
                for (int c = 0; c < child_count; ++c)
                    queue.push_back(tv->get_node_child(nid, c));
            }
            n.add_child("nodes", nodes_arr);
        }
        if (opts.save_styles)
            n.add_child("tree_style", write_treeview_style_pt(tv->get_tree_view_style()));
        break;
    }
    case WidgetType::Panel: {
        // Could be SplitPanel or DockPanel - try dynamic_cast
        auto* sp = dynamic_cast<const IGuiSplitPanel*>(widget);
        if (sp) {
            n.put("panel_type", "split");
            n.put("orientation", split_orient_str(sp->get_orientation()));
            pt_put_float(n, "split_position", sp->get_split_position());
            pt_put_float(n, "split_ratio", sp->get_split_ratio());
            n.put("split_unit", split_unit_str(sp->get_split_unit()));
            pt_put_float(n, "first_min_size", sp->get_first_min_size());
            pt_put_float(n, "first_max_size", sp->get_first_max_size());
            pt_put_float(n, "second_min_size", sp->get_second_min_size());
            pt_put_float(n, "second_max_size", sp->get_second_max_size());
            n.put("first_collapsed", sp->is_first_collapsed() ? "true" : "false");
            n.put("second_collapsed", sp->is_second_collapsed() ? "true" : "false");
            n.put("collapsible", sp->is_collapsible() ? "true" : "false");
            n.put("splitter_fixed", sp->is_splitter_fixed() ? "true" : "false");
            if (sp->get_first_panel()) {
                n.add_child("first_panel", write_widget_pt(sp->get_first_panel(), opts));
            }
            if (sp->get_second_panel()) {
                n.add_child("second_panel", write_widget_pt(sp->get_second_panel(), opts));
            }
            if (opts.save_styles)
                n.add_child("splitter_style", write_splitter_style_pt(sp->get_splitter_style()));
        }
        auto* dp = dynamic_cast<const IGuiDockPanel*>(widget);
        if (dp) {
            n.put("panel_type", "dock");
            n.put("layout_data", dp->save_layout());
            n.put("active_panel", dp->get_active_panel());
            n.put("drag_docking_enabled", dp->is_drag_docking_enabled() ? "true" : "false");
        }
        break;
    }
    default:
        break;
    }
}

static pt::ptree write_widget_pt(const IGuiWidget* widget, const GuiSerializeOptions& opts) {
    pt::ptree n;
    if (!widget) {
        n.put_value("null");
        return n;
    }

    write_base_props_pt(n, widget, opts);
    write_widget_specific_pt(n, widget, opts);

    // Children
    int child_count = widget->get_child_count();
    if (child_count > 0) {
        pt::ptree children_arr;
        for (int i = 0; i < child_count; ++i) {
            children_arr.push_back(std::make_pair("", write_widget_pt(widget->get_child(i), opts)));
        }
        n.add_child("children", children_arr);
    }

    return n;
}

static std::string serialize_json(const IGuiContext* context, const GuiSerializeOptions& opts) {
    pt::ptree tree;

    // Version
    pt::ptree ver;
    ver.put("major", GUI_SERIALIZE_VERSION_MAJOR);
    ver.put("minor", GUI_SERIALIZE_VERSION_MINOR);
    tree.add_child("version", ver);

    // Default styles
    if (opts.save_styles) {
        tree.add_child("default_style", write_gui_style_pt(context->get_default_style()));
        tree.add_child("default_label_style", write_label_style_pt(context->get_default_label_style()));
    }

    // Root widget tree
    IGuiWidget* root = const_cast<IGuiContext*>(context)->get_root();
    if (root) {
        tree.add_child("root", write_widget_pt(root, opts));
    }

    std::ostringstream ss;
    pt::write_json(ss, tree, opts.pretty_print);
    return ss.str();
}

// ============================================================================
// JSON Deserialization - Read (ptree)
// ============================================================================

static GuiStyle read_gui_style_pt(const pt::ptree& v) {
    GuiStyle s = GuiStyle::default_style();
    s.background_color = pt_get_vec4(v, "background_color", s.background_color);
    s.border_color = pt_get_vec4(v, "border_color", s.border_color);
    s.hover_color = pt_get_vec4(v, "hover_color", s.hover_color);
    s.pressed_color = pt_get_vec4(v, "pressed_color", s.pressed_color);
    s.disabled_color = pt_get_vec4(v, "disabled_color", s.disabled_color);
    s.focus_color = pt_get_vec4(v, "focus_color", s.focus_color);
    s.border_width = pt_get_float(v, "border_width", s.border_width);
    s.corner_radius = pt_get_float(v, "corner_radius", s.corner_radius);
    s.padding = pt_get_vec4(v, "padding", s.padding);
    s.margin = pt_get_vec4(v, "margin", s.margin);
    return s;
}

static LabelStyle read_label_style_pt(const pt::ptree& v) {
    LabelStyle s = LabelStyle::default_style();
    s.text_color = pt_get_vec4(v, "text_color", s.text_color);
    s.selection_color = pt_get_vec4(v, "selection_color", s.selection_color);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    s.alignment = parse_alignment(pt_get_string(v, "alignment", "center_left").c_str());
    s.wrap = pt_get_bool(v, "wrap", s.wrap);
    s.ellipsis = pt_get_bool(v, "ellipsis", s.ellipsis);
    // font_name: loaded as string but LabelStyle stores const char* - caller handles lifetime
    return s;
}

static ButtonStyle read_button_style_pt(const pt::ptree& v) {
    ButtonStyle s = ButtonStyle::default_style();
    s.background_color = pt_get_vec4(v, "background_color", s.background_color);
    s.hover_color = pt_get_vec4(v, "hover_color", s.hover_color);
    s.pressed_color = pt_get_vec4(v, "pressed_color", s.pressed_color);
    s.disabled_color = pt_get_vec4(v, "disabled_color", s.disabled_color);
    s.checked_color = pt_get_vec4(v, "checked_color", s.checked_color);
    s.text_color = pt_get_vec4(v, "text_color", s.text_color);
    s.text_disabled_color = pt_get_vec4(v, "text_disabled_color", s.text_disabled_color);
    s.border_color = pt_get_vec4(v, "border_color", s.border_color);
    s.focus_border_color = pt_get_vec4(v, "focus_border_color", s.focus_border_color);
    s.border_width = pt_get_float(v, "border_width", s.border_width);
    s.corner_radius = pt_get_float(v, "corner_radius", s.corner_radius);
    s.padding = pt_get_float(v, "padding", s.padding);
    s.icon_size = pt_get_float(v, "icon_size", s.icon_size);
    s.icon_text_spacing = pt_get_float(v, "icon_text_spacing", s.icon_text_spacing);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    return s;
}

static SliderStyle read_slider_style_pt(const pt::ptree& v) {
    SliderStyle s = SliderStyle::default_style();
    s.track_color = pt_get_vec4(v, "track_color", s.track_color);
    s.track_fill_color = pt_get_vec4(v, "track_fill_color", s.track_fill_color);
    s.thumb_color = pt_get_vec4(v, "thumb_color", s.thumb_color);
    s.thumb_hover_color = pt_get_vec4(v, "thumb_hover_color", s.thumb_hover_color);
    s.thumb_pressed_color = pt_get_vec4(v, "thumb_pressed_color", s.thumb_pressed_color);
    s.tick_color = pt_get_vec4(v, "tick_color", s.tick_color);
    s.track_height = pt_get_float(v, "track_height", s.track_height);
    s.thumb_radius = pt_get_float(v, "thumb_radius", s.thumb_radius);
    s.tick_length = pt_get_float(v, "tick_length", s.tick_length);
    s.tick_width = pt_get_float(v, "tick_width", s.tick_width);
    s.track_corner_radius = pt_get_float(v, "track_corner_radius", s.track_corner_radius);
    return s;
}

static TabStyle read_tab_style_pt(const pt::ptree& v) {
    TabStyle s = TabStyle::default_style();
    s.tab_background = pt_get_vec4(v, "tab_background", s.tab_background);
    s.tab_hover_background = pt_get_vec4(v, "tab_hover_background", s.tab_hover_background);
    s.tab_active_background = pt_get_vec4(v, "tab_active_background", s.tab_active_background);
    s.tab_text_color = pt_get_vec4(v, "tab_text_color", s.tab_text_color);
    s.tab_active_text_color = pt_get_vec4(v, "tab_active_text_color", s.tab_active_text_color);
    s.tab_bar_background = pt_get_vec4(v, "tab_bar_background", s.tab_bar_background);
    s.indicator_color = pt_get_vec4(v, "indicator_color", s.indicator_color);
    s.close_button_color = pt_get_vec4(v, "close_button_color", s.close_button_color);
    s.close_button_hover_color = pt_get_vec4(v, "close_button_hover_color", s.close_button_hover_color);
    s.tab_height = pt_get_float(v, "tab_height", s.tab_height);
    s.tab_min_width = pt_get_float(v, "tab_min_width", s.tab_min_width);
    s.tab_max_width = pt_get_float(v, "tab_max_width", s.tab_max_width);
    s.tab_padding = pt_get_float(v, "tab_padding", s.tab_padding);
    s.indicator_height = pt_get_float(v, "indicator_height", s.indicator_height);
    s.icon_size = pt_get_float(v, "icon_size", s.icon_size);
    s.close_button_size = pt_get_float(v, "close_button_size", s.close_button_size);
    s.corner_radius = pt_get_float(v, "corner_radius", s.corner_radius);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    return s;
}

static SplitterStyle read_splitter_style_pt(const pt::ptree& v) {
    SplitterStyle s = SplitterStyle::default_style();
    s.splitter_color = pt_get_vec4(v, "splitter_color", s.splitter_color);
    s.splitter_hover_color = pt_get_vec4(v, "splitter_hover_color", s.splitter_hover_color);
    s.splitter_drag_color = pt_get_vec4(v, "splitter_drag_color", s.splitter_drag_color);
    s.grip_color = pt_get_vec4(v, "grip_color", s.grip_color);
    s.splitter_thickness = pt_get_float(v, "splitter_thickness", s.splitter_thickness);
    s.hit_area_thickness = pt_get_float(v, "hit_area_thickness", s.hit_area_thickness);
    s.grip_length = pt_get_float(v, "grip_length", s.grip_length);
    s.grip_dot_size = pt_get_float(v, "grip_dot_size", s.grip_dot_size);
    s.grip_dot_count = pt_get_int(v, "grip_dot_count", s.grip_dot_count);
    return s;
}

static ListBoxStyle read_listbox_style_pt(const pt::ptree& v) {
    ListBoxStyle s = ListBoxStyle::default_style();
    s.row_background = pt_get_vec4(v, "row_background", s.row_background);
    s.row_alt_background = pt_get_vec4(v, "row_alt_background", s.row_alt_background);
    s.selected_background = pt_get_vec4(v, "selected_background", s.selected_background);
    s.hover_background = pt_get_vec4(v, "hover_background", s.hover_background);
    s.text_color = pt_get_vec4(v, "text_color", s.text_color);
    s.selected_text_color = pt_get_vec4(v, "selected_text_color", s.selected_text_color);
    s.icon_color = pt_get_vec4(v, "icon_color", s.icon_color);
    s.separator_color = pt_get_vec4(v, "separator_color", s.separator_color);
    s.row_height = pt_get_float(v, "row_height", s.row_height);
    s.icon_size = pt_get_float(v, "icon_size", s.icon_size);
    s.item_padding = pt_get_float(v, "item_padding", s.item_padding);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    s.show_separator = pt_get_bool(v, "show_separator", s.show_separator);
    return s;
}

static TreeViewStyle read_treeview_style_pt(const pt::ptree& v) {
    TreeViewStyle s = TreeViewStyle::default_style();
    s.row_background = pt_get_vec4(v, "row_background", s.row_background);
    s.row_alt_background = pt_get_vec4(v, "row_alt_background", s.row_alt_background);
    s.selected_background = pt_get_vec4(v, "selected_background", s.selected_background);
    s.hover_background = pt_get_vec4(v, "hover_background", s.hover_background);
    s.text_color = pt_get_vec4(v, "text_color", s.text_color);
    s.icon_color = pt_get_vec4(v, "icon_color", s.icon_color);
    s.line_color = pt_get_vec4(v, "line_color", s.line_color);
    s.row_height = pt_get_float(v, "row_height", s.row_height);
    s.indent_width = pt_get_float(v, "indent_width", s.indent_width);
    s.icon_size = pt_get_float(v, "icon_size", s.icon_size);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    s.show_lines = pt_get_bool(v, "show_lines", s.show_lines);
    s.show_root_lines = pt_get_bool(v, "show_root_lines", s.show_root_lines);
    return s;
}

static DialogStyle read_dialog_style_pt(const pt::ptree& v) {
    DialogStyle s = DialogStyle::default_style();
    s.overlay_color = pt_get_vec4(v, "overlay_color", s.overlay_color);
    s.background_color = pt_get_vec4(v, "background_color", s.background_color);
    s.border_color = pt_get_vec4(v, "border_color", s.border_color);
    s.title_bar_color = pt_get_vec4(v, "title_bar_color", s.title_bar_color);
    s.title_text_color = pt_get_vec4(v, "title_text_color", s.title_text_color);
    s.shadow_color = pt_get_vec4(v, "shadow_color", s.shadow_color);
    s.border_width = pt_get_float(v, "border_width", s.border_width);
    s.corner_radius = pt_get_float(v, "corner_radius", s.corner_radius);
    s.title_bar_height = pt_get_float(v, "title_bar_height", s.title_bar_height);
    s.button_area_height = pt_get_float(v, "button_area_height", s.button_area_height);
    s.padding = pt_get_float(v, "padding", s.padding);
    s.shadow_offset = pt_get_float(v, "shadow_offset", s.shadow_offset);
    s.shadow_blur = pt_get_float(v, "shadow_blur", s.shadow_blur);
    s.min_width = pt_get_float(v, "min_width", s.min_width);
    s.min_height = pt_get_float(v, "min_height", s.min_height);
    s.font_size = pt_get_float(v, "font_size", s.font_size);
    s.title_font_size = pt_get_float(v, "title_font_size", s.title_font_size);
    return s;
}

// Forward declaration
static IGuiWidget* load_widget_pt(IGuiContext* ctx, const pt::ptree& v, const GuiSerializeOptions& opts);

static void load_base_props_pt(IGuiWidget* widget, const pt::ptree& v, const GuiSerializeOptions& opts) {
    if (pt_has(v, "name")) widget->set_name(pt_get_string(v, "name").c_str());
    if (pt_has(v, "bounds")) widget->set_bounds(pt_get_box(v, "bounds"));
    if (pt_has(v, "min_size")) widget->set_min_size(pt_get_vec2(v, "min_size"));
    if (pt_has(v, "max_size")) widget->set_max_size(pt_get_vec2(v, "max_size"));
    if (pt_has(v, "visible")) widget->set_visible(pt_get_bool(v, "visible", true));
    if (pt_has(v, "enabled")) widget->set_enabled(pt_get_bool(v, "enabled", true));
    if (pt_has(v, "width_mode") || pt_has(v, "height_mode")) {
        SizeMode wm = parse_size_mode(pt_get_string(v, "width_mode", "auto").c_str());
        SizeMode hm = parse_size_mode(pt_get_string(v, "height_mode", "auto").c_str());
        widget->set_size_mode(wm, hm);
    }
    if (pt_has(v, "alignment"))
        widget->set_alignment(parse_alignment(pt_get_string(v, "alignment").c_str()));
    if (pt_has(v, "layout_direction"))
        widget->set_layout_direction(parse_layout_dir(pt_get_string(v, "layout_direction").c_str()));
    if (pt_has(v, "spacing"))
        widget->set_spacing(pt_get_float(v, "spacing"));
    if (pt_has(v, "clip_enabled")) {
        widget->set_clip_enabled(pt_get_bool(v, "clip_enabled"));
        if (pt_has(v, "clip_rect"))
            widget->set_clip_rect(pt_get_box(v, "clip_rect"));
    }

    if (opts.save_styles) {
        const pt::ptree* style_val = pt_get_child(v, "style");
        if (style_val)
            widget->set_style(read_gui_style_pt(*style_val));
    }
}

static void load_widget_specific_pt(IGuiContext* ctx, IGuiWidget* widget, const pt::ptree& v,
                                    const GuiSerializeOptions& opts) {
    WidgetType type = widget->get_type();

    switch (type) {
    case WidgetType::Button: {
        auto* btn = static_cast<IGuiButton*>(widget);
        if (pt_has(v, "button_type"))
            btn->set_button_type(parse_button_type(pt_get_string(v, "button_type").c_str()));
        if (pt_has(v, "text"))
            btn->set_text(pt_get_string(v, "text").c_str());
        if (pt_has(v, "icon"))
            btn->set_icon(pt_get_string(v, "icon").c_str());
        if (pt_has(v, "checked"))
            btn->set_checked(pt_get_bool(v, "checked"));
        if (pt_has(v, "radio_group"))
            btn->set_radio_group(pt_get_int(v, "radio_group"));
        const pt::ptree* bs = pt_get_child(v, "button_style");
        if (bs) btn->set_button_style(read_button_style_pt(*bs));
        break;
    }
    case WidgetType::Label: {
        auto* lbl = static_cast<IGuiLabel*>(widget);
        if (pt_has(v, "text"))
            lbl->set_text(pt_get_string(v, "text").c_str());
        const pt::ptree* ls = pt_get_child(v, "label_style");
        if (ls) lbl->set_label_style(read_label_style_pt(*ls));
        break;
    }
    case WidgetType::TextInput: {
        auto* inp = static_cast<IGuiTextInput*>(widget);
        if (pt_has(v, "text"))
            inp->set_text(pt_get_string(v, "text").c_str());
        if (pt_has(v, "placeholder"))
            inp->set_placeholder(pt_get_string(v, "placeholder").c_str());
        if (pt_has(v, "password_mode"))
            inp->set_password_mode(pt_get_bool(v, "password_mode"));
        if (pt_has(v, "read_only"))
            inp->set_read_only(pt_get_bool(v, "read_only"));
        if (pt_has(v, "max_length"))
            inp->set_max_length(pt_get_int(v, "max_length"));
        const pt::ptree* ls = pt_get_child(v, "label_style");
        if (ls) inp->set_label_style(read_label_style_pt(*ls));
        break;
    }
    case WidgetType::Slider: {
        auto* sl = static_cast<IGuiSlider*>(widget);
        if (pt_has(v, "orientation"))
            sl->set_orientation(parse_slider_orient(pt_get_string(v, "orientation").c_str()));
        float mn = pt_get_float(v, "min_value", 0.0f);
        float mx = pt_get_float(v, "max_value", 1.0f);
        sl->set_range(mn, mx);
        if (pt_has(v, "value"))
            sl->set_value(pt_get_float(v, "value"));
        if (pt_has(v, "step"))
            sl->set_step(pt_get_float(v, "step"));
        if (pt_has(v, "ticks_visible"))
            sl->set_ticks_visible(pt_get_bool(v, "ticks_visible"));
        if (pt_has(v, "tick_interval"))
            sl->set_tick_interval(pt_get_float(v, "tick_interval"));
        const pt::ptree* ss = pt_get_child(v, "slider_style");
        if (ss) sl->set_slider_style(read_slider_style_pt(*ss));
        break;
    }
    case WidgetType::ProgressBar: {
        auto* pb = static_cast<IGuiProgressBar*>(widget);
        if (pt_has(v, "mode"))
            pb->set_mode(parse_progress_mode(pt_get_string(v, "mode").c_str()));
        if (pt_has(v, "value"))
            pb->set_value(pt_get_float(v, "value"));
        if (pt_has(v, "text_visible"))
            pb->set_text_visible(pt_get_bool(v, "text_visible"));
        if (pt_has(v, "text"))
            pb->set_text(pt_get_string(v, "text").c_str());
        break;
    }
    case WidgetType::Image: {
        auto* img = static_cast<IGuiImage*>(widget);
        if (pt_has(v, "image_name"))
            img->set_image_name(pt_get_string(v, "image_name"));
        if (pt_has(v, "tint"))
            img->set_tint(pt_get_vec4(v, "tint", math::Vec4(1,1,1,1)));
        if (pt_has(v, "use_slice9")) {
            img->set_use_slice9(pt_get_bool(v, "use_slice9"));
            const pt::ptree* sb = pt_get_child(v, "slice_border");
            if (sb && sb->size() >= 4) {
                auto ci = sb->begin();
                SliceBorder border;
                border.left = pt_arr_float(ci->second); ++ci;
                border.top = pt_arr_float(ci->second); ++ci;
                border.right = pt_arr_float(ci->second); ++ci;
                border.bottom = pt_arr_float(ci->second);
                img->set_slice_border(border);
            }
            if (pt_has(v, "slice_center_mode"))
                img->set_slice_center_mode(parse_slice_center_mode(pt_get_string(v, "slice_center_mode").c_str()));
            if (pt_has(v, "source_size"))
                img->set_source_size(pt_get_vec2(v, "source_size"));
        }
        break;
    }
    case WidgetType::ScrollArea: {
        auto* sv = dynamic_cast<IGuiScrollView*>(widget);
        if (sv) {
            if (pt_has(v, "scroll_offset"))
                sv->set_scroll_offset(pt_get_vec2(v, "scroll_offset"));
            if (pt_has(v, "content_size"))
                sv->set_content_size(pt_get_vec2(v, "content_size"));
            if (pt_has(v, "h_scrollbar_visibility"))
                sv->set_h_scrollbar_visibility(parse_scrollbar_vis(pt_get_string(v, "h_scrollbar_visibility").c_str()));
            if (pt_has(v, "v_scrollbar_visibility"))
                sv->set_v_scrollbar_visibility(parse_scrollbar_vis(pt_get_string(v, "v_scrollbar_visibility").c_str()));
            if (pt_has(v, "scroll_speed"))
                sv->set_scroll_speed(pt_get_float(v, "scroll_speed"));
            if (pt_has(v, "scroll_inertia_enabled"))
                sv->set_scroll_inertia_enabled(pt_get_bool(v, "scroll_inertia_enabled"));
            if (pt_has(v, "size"))
                sv->set_size(parse_scroll_view_size(pt_get_string(v, "size").c_str()));
        }
        break;
    }
    case WidgetType::ListBox: {
        auto* lb = static_cast<IGuiListBox*>(widget);
        if (pt_has(v, "selection_mode"))
            lb->set_selection_mode(parse_listbox_sel(pt_get_string(v, "selection_mode").c_str()));
        const pt::ptree* items = pt_get_child(v, "items");
        if (items) {
            for (auto& [k, item] : *items) {
                std::string text = pt_get_string(item, "text", "");
                std::string icon = pt_get_string(item, "icon", "");
                int id = lb->add_item(text.c_str(), icon.empty() ? nullptr : icon.c_str());
                if (pt_has(item, "enabled"))
                    lb->set_item_enabled(id, pt_get_bool(item, "enabled", true));
            }
        }
        if (pt_has(v, "selected_item"))
            lb->set_selected_item(pt_get_int(v, "selected_item", -1));
        const pt::ptree* ls = pt_get_child(v, "list_style");
        if (ls) lb->set_list_box_style(read_listbox_style_pt(*ls));
        break;
    }
    case WidgetType::ComboBox: {
        auto* cb = static_cast<IGuiComboBox*>(widget);
        if (pt_has(v, "placeholder"))
            cb->set_placeholder(pt_get_string(v, "placeholder").c_str());
        const pt::ptree* items = pt_get_child(v, "items");
        if (items) {
            for (auto& [k, item] : *items) {
                std::string text = pt_get_string(item, "text", "");
                std::string icon = pt_get_string(item, "icon", "");
                int id = cb->add_item(text.c_str(), icon.empty() ? nullptr : icon.c_str());
                if (pt_has(item, "enabled"))
                    cb->set_item_enabled(id, pt_get_bool(item, "enabled", true));
            }
        }
        if (pt_has(v, "selected_item"))
            cb->set_selected_item(pt_get_int(v, "selected_item", -1));
        break;
    }
    case WidgetType::TabControl: {
        auto* tc = static_cast<IGuiTabControl*>(widget);
        if (pt_has(v, "tab_position"))
            tc->set_tab_position(parse_tab_position(pt_get_string(v, "tab_position").c_str()));
        if (pt_has(v, "tab_size_mode"))
            tc->set_tab_size_mode(parse_tab_size_mode(pt_get_string(v, "tab_size_mode").c_str()));
        if (pt_has(v, "fixed_tab_width"))
            tc->set_fixed_tab_width(pt_get_float(v, "fixed_tab_width"));
        if (pt_has(v, "drag_reorder_enabled"))
            tc->set_drag_reorder_enabled(pt_get_bool(v, "drag_reorder_enabled"));
        const pt::ptree* tabs = pt_get_child(v, "tabs");
        if (tabs) {
            for (auto& [k, tab] : *tabs) {
                std::string text = pt_get_string(tab, "text", "Tab");
                std::string icon = pt_get_string(tab, "icon", "");
                int id = tc->add_tab(text.c_str(), icon.empty() ? nullptr : icon.c_str());
                if (pt_has(tab, "enabled"))
                    tc->set_tab_enabled(id, pt_get_bool(tab, "enabled", true));
                if (pt_has(tab, "closable"))
                    tc->set_tab_closable(id, pt_get_bool(tab, "closable", false));
                const pt::ptree* content = pt_get_child(tab, "content");
                if (content) {
                    IGuiWidget* child = load_widget_pt(ctx, *content, opts);
                    if (child)
                        tc->set_tab_content(id, child);
                }
            }
        }
        if (pt_has(v, "active_tab"))
            tc->set_active_tab(pt_get_int(v, "active_tab", 0));
        const pt::ptree* ts = pt_get_child(v, "tab_style");
        if (ts) tc->set_tab_style(read_tab_style_pt(*ts));
        break;
    }
    case WidgetType::TreeView: {
        auto* tv = static_cast<IGuiTreeView*>(widget);
        if (pt_has(v, "selection_mode"))
            tv->set_selection_mode(parse_treeview_sel(pt_get_string(v, "selection_mode").c_str()));
        if (pt_has(v, "drag_reorder_enabled"))
            tv->set_drag_reorder_enabled(pt_get_bool(v, "drag_reorder_enabled"));
        const pt::ptree* nodes = pt_get_child(v, "nodes");
        if (nodes) {
            // Map old_id -> new_id for parent resolution
            std::unordered_map<int, int> id_map;
            for (auto& [k, node] : *nodes) {
                int old_id = pt_get_int(node, "id", -1);
                int old_parent = pt_get_int(node, "parent", -1);
                std::string text = pt_get_string(node, "text", "");
                std::string icon = pt_get_string(node, "icon", "");
                int parent_id = -1;
                if (old_parent >= 0) {
                    auto pit = id_map.find(old_parent);
                    if (pit != id_map.end()) parent_id = pit->second;
                }
                int new_id = tv->add_node(parent_id, text.c_str(), icon.empty() ? nullptr : icon.c_str());
                if (old_id >= 0) id_map[old_id] = new_id;
                if (pt_has(node, "expanded"))
                    tv->set_node_expanded(new_id, pt_get_bool(node, "expanded", false));
                if (pt_has(node, "enabled"))
                    tv->set_node_enabled(new_id, pt_get_bool(node, "enabled", true));
            }
        }
        if (pt_has(v, "selected_node"))
            tv->set_selected_node(pt_get_int(v, "selected_node", -1));
        const pt::ptree* ts = pt_get_child(v, "tree_style");
        if (ts) tv->set_tree_view_style(read_treeview_style_pt(*ts));
        break;
    }
    case WidgetType::Panel: {
        std::string panel_type = pt_get_string(v, "panel_type", "");
        if (panel_type == "split") {
            auto* sp = dynamic_cast<IGuiSplitPanel*>(widget);
            if (sp) {
                if (pt_has(v, "orientation"))
                    sp->set_orientation(parse_split_orient(pt_get_string(v, "orientation").c_str()));
                if (pt_has(v, "split_unit"))
                    sp->set_split_unit(parse_split_unit(pt_get_string(v, "split_unit").c_str()));
                if (pt_has(v, "split_position"))
                    sp->set_split_position(pt_get_float(v, "split_position"));
                if (pt_has(v, "split_ratio"))
                    sp->set_split_ratio(pt_get_float(v, "split_ratio"));
                if (pt_has(v, "first_min_size"))
                    sp->set_first_min_size(pt_get_float(v, "first_min_size"));
                if (pt_has(v, "first_max_size"))
                    sp->set_first_max_size(pt_get_float(v, "first_max_size"));
                if (pt_has(v, "second_min_size"))
                    sp->set_second_min_size(pt_get_float(v, "second_min_size"));
                if (pt_has(v, "second_max_size"))
                    sp->set_second_max_size(pt_get_float(v, "second_max_size"));
                if (pt_has(v, "first_collapsed"))
                    sp->set_first_collapsed(pt_get_bool(v, "first_collapsed"));
                if (pt_has(v, "second_collapsed"))
                    sp->set_second_collapsed(pt_get_bool(v, "second_collapsed"));
                if (pt_has(v, "collapsible"))
                    sp->set_collapsible(pt_get_bool(v, "collapsible"));
                if (pt_has(v, "splitter_fixed"))
                    sp->set_splitter_fixed(pt_get_bool(v, "splitter_fixed"));
                const pt::ptree* fp = pt_get_child(v, "first_panel");
                if (fp) {
                    IGuiWidget* child = load_widget_pt(ctx, *fp, opts);
                    if (child) sp->set_first_panel(child);
                }
                const pt::ptree* sp2 = pt_get_child(v, "second_panel");
                if (sp2) {
                    IGuiWidget* child = load_widget_pt(ctx, *sp2, opts);
                    if (child) sp->set_second_panel(child);
                }
                const pt::ptree* ss = pt_get_child(v, "splitter_style");
                if (ss) sp->set_splitter_style(read_splitter_style_pt(*ss));
            }
        } else if (panel_type == "dock") {
            auto* dp = dynamic_cast<IGuiDockPanel*>(widget);
            if (dp) {
                if (pt_has(v, "layout_data"))
                    dp->load_layout(pt_get_string(v, "layout_data").c_str());
                if (pt_has(v, "active_panel"))
                    dp->set_active_panel(pt_get_int(v, "active_panel"));
                if (pt_has(v, "drag_docking_enabled"))
                    dp->set_drag_docking_enabled(pt_get_bool(v, "drag_docking_enabled"));
            }
        }
        break;
    }
    default:
        break;
    }
}

static IGuiWidget* create_widget_by_type_pt(IGuiContext* ctx, WidgetType type, const pt::ptree& v) {
    switch (type) {
    case WidgetType::Button:
        return ctx->create_button(parse_button_type(pt_get_string(v, "button_type", "normal").c_str()));
    case WidgetType::Label:
        return ctx->create_label();
    case WidgetType::TextInput:
        return ctx->create_text_input();
    case WidgetType::Slider:
        return ctx->create_slider(parse_slider_orient(pt_get_string(v, "orientation", "horizontal").c_str()));
    case WidgetType::ProgressBar:
        return ctx->create_progress_bar(parse_progress_mode(pt_get_string(v, "mode", "determinate").c_str()));
    case WidgetType::Image:
        return ctx->create_image();
    case WidgetType::ScrollArea:
        return ctx->create_scroll_view();
    case WidgetType::ListBox:
        return ctx->create_list_box();
    case WidgetType::ComboBox:
        return ctx->create_combo_box();
    case WidgetType::TabControl:
        return ctx->create_tab_control(parse_tab_position(pt_get_string(v, "tab_position", "top").c_str()));
    case WidgetType::TreeView:
        return ctx->create_tree_view();
    case WidgetType::Panel: {
        std::string ptype = pt_get_string(v, "panel_type", "split");
        if (ptype == "dock")
            return ctx->create_dock_panel();
        return ctx->create_split_panel(
            parse_split_orient(pt_get_string(v, "orientation", "horizontal").c_str()));
    }
    default:
        return ctx->create_widget(type);
    }
}

static IGuiWidget* load_widget_pt(IGuiContext* ctx, const pt::ptree& v,
                                  const GuiSerializeOptions& opts) {
    WidgetType type = parse_widget_type(pt_get_string(v, "type", "custom").c_str());
    IGuiWidget* widget = create_widget_by_type_pt(ctx, type, v);
    if (!widget) return nullptr;

    load_base_props_pt(widget, v, opts);
    load_widget_specific_pt(ctx, widget, v, opts);

    // Children
    const pt::ptree* children = pt_get_child(v, "children");
    if (children) {
        for (auto& [k, child_val] : *children) {
            IGuiWidget* child = load_widget_pt(ctx, child_val, opts);
            if (child)
                widget->add_child(child);
        }
    }

    return widget;
}

static GuiSerializeResult deserialize_json(IGuiContext* ctx, const char* data, size_t size,
                                           const GuiSerializeOptions& opts) {
    pt::ptree root;
    try {
        std::istringstream ss(std::string(data, size));
        pt::read_json(ss, root);
    } catch (...) {
        return GuiSerializeResult::ErrorParse;
    }

    // Check version
    const pt::ptree* ver = pt_get_child(root, "version");
    if (ver) {
        GuiSerializeVersion file_ver;
        file_ver.major = (uint16_t)pt_get_int(*ver, "major", 1);
        file_ver.minor = (uint16_t)pt_get_int(*ver, "minor", 0);
        GuiSerializeVersion current;
        if (!current.is_compatible(file_ver))
            return GuiSerializeResult::ErrorVersionMismatch;
    }

    // Clear existing if requested
    if (opts.clear_before_load) {
        IGuiWidget* old_root = ctx->get_root();
        if (old_root) old_root->clear_children();
    }

    // Default styles
    if (opts.save_styles) {
        const pt::ptree* ds = pt_get_child(root, "default_style");
        if (ds) ctx->set_default_style(read_gui_style_pt(*ds));
        const pt::ptree* dls = pt_get_child(root, "default_label_style");
        if (dls) ctx->set_default_label_style(read_label_style_pt(*dls));
    }

    // Root widget
    const pt::ptree* root_widget = pt_get_child(root, "root");
    if (root_widget) {
        // Load children of root into the context's existing root
        const pt::ptree* children = pt_get_child(*root_widget, "children");
        if (children) {
            IGuiWidget* ctx_root = ctx->get_root();
            for (auto& [k, child_val] : *children) {
                IGuiWidget* child = load_widget_pt(ctx, child_val, opts);
                if (child && ctx_root)
                    ctx_root->add_child(child);
            }
        }
        // Also apply base props to root
        IGuiWidget* ctx_root = ctx->get_root();
        if (ctx_root)
            load_base_props_pt(ctx_root, *root_widget, opts);
    }

    return GuiSerializeResult::Success;
}

// ============================================================================
// Binary Serialization - Write
// ============================================================================

static void write_gui_style_bin(BinaryWriter& w, const GuiStyle& s) {
    w.write_vec4(s.background_color);
    w.write_vec4(s.border_color);
    w.write_vec4(s.hover_color);
    w.write_vec4(s.pressed_color);
    w.write_vec4(s.disabled_color);
    w.write_vec4(s.focus_color);
    w.write_f32(s.border_width);
    w.write_f32(s.corner_radius);
    w.write_vec4(s.padding);
    w.write_vec4(s.margin);
}

static GuiStyle read_gui_style_bin(BinaryReader& r) {
    GuiStyle s;
    s.background_color = r.read_vec4();
    s.border_color = r.read_vec4();
    s.hover_color = r.read_vec4();
    s.pressed_color = r.read_vec4();
    s.disabled_color = r.read_vec4();
    s.focus_color = r.read_vec4();
    s.border_width = r.read_f32();
    s.corner_radius = r.read_f32();
    s.padding = r.read_vec4();
    s.margin = r.read_vec4();
    return s;
}

static void write_label_style_bin(BinaryWriter& w, const LabelStyle& s) {
    w.write_vec4(s.text_color);
    w.write_vec4(s.selection_color);
    w.write_f32(s.font_size);
    w.write_string(s.font_name);
    w.write_u8(static_cast<uint8_t>(s.alignment));
    w.write_bool(s.wrap);
    w.write_bool(s.ellipsis);
}

static LabelStyle read_label_style_bin(BinaryReader& r) {
    LabelStyle s;
    s.text_color = r.read_vec4();
    s.selection_color = r.read_vec4();
    s.font_size = r.read_f32();
    r.read_string(); // font_name - skip, cannot set const char* from transient string
    s.alignment = static_cast<Alignment>(r.read_u8());
    s.wrap = r.read_bool();
    s.ellipsis = r.read_bool();
    return s;
}

// Forward declaration
static void write_widget_bin(BinaryWriter& w, const IGuiWidget* widget, const GuiSerializeOptions& opts);

static void write_base_props_bin(BinaryWriter& w, const IGuiWidget* widget, const GuiSerializeOptions& opts) {
    w.write_u8(static_cast<uint8_t>(widget->get_type()));
    w.write_string(widget->get_name());
    w.write_box(widget->get_bounds());
    w.write_vec2(widget->get_min_size());
    w.write_vec2(widget->get_max_size());
    w.write_bool(widget->is_visible());
    w.write_bool(widget->is_enabled());
    w.write_bool(widget->is_focusable());
    w.write_u8(static_cast<uint8_t>(widget->get_width_mode()));
    w.write_u8(static_cast<uint8_t>(widget->get_height_mode()));
    w.write_u8(static_cast<uint8_t>(widget->get_alignment()));
    w.write_u8(static_cast<uint8_t>(widget->get_layout_direction()));
    w.write_f32(widget->get_spacing());
    w.write_bool(widget->is_clip_enabled());
    if (widget->is_clip_enabled())
        w.write_box(widget->get_clip_rect());

    if (opts.save_styles)
        write_gui_style_bin(w, widget->get_style());
}

static void write_widget_specific_bin(BinaryWriter& w, const IGuiWidget* widget,
                                      const GuiSerializeOptions& opts) {
    WidgetType type = widget->get_type();

    switch (type) {
    case WidgetType::Button: {
        auto* btn = static_cast<const IGuiButton*>(widget);
        w.write_u8(static_cast<uint8_t>(btn->get_button_type()));
        w.write_string(btn->get_text());
        w.write_string(btn->get_icon());
        w.write_bool(btn->is_checked());
        w.write_i32(btn->get_radio_group());
        break;
    }
    case WidgetType::Label: {
        auto* lbl = static_cast<const IGuiLabel*>(widget);
        w.write_string(lbl->get_text());
        if (opts.save_styles)
            write_label_style_bin(w, lbl->get_label_style());
        break;
    }
    case WidgetType::TextInput: {
        auto* inp = static_cast<const IGuiTextInput*>(widget);
        w.write_string(inp->get_text());
        w.write_string(inp->get_placeholder());
        w.write_bool(inp->is_password_mode());
        w.write_bool(inp->is_read_only());
        w.write_i32(inp->get_max_length());
        if (opts.save_styles)
            write_label_style_bin(w, inp->get_label_style());
        break;
    }
    case WidgetType::Slider: {
        auto* sl = static_cast<const IGuiSlider*>(widget);
        w.write_u8(static_cast<uint8_t>(sl->get_orientation()));
        w.write_f32(sl->get_value());
        w.write_f32(sl->get_min_value());
        w.write_f32(sl->get_max_value());
        w.write_f32(sl->get_step());
        w.write_bool(sl->is_ticks_visible());
        w.write_f32(sl->get_tick_interval());
        break;
    }
    case WidgetType::ProgressBar: {
        auto* pb = static_cast<const IGuiProgressBar*>(widget);
        w.write_u8(static_cast<uint8_t>(pb->get_mode()));
        w.write_f32(pb->get_value());
        w.write_bool(pb->is_text_visible());
        w.write_string(pb->get_text());
        break;
    }
    case WidgetType::Image: {
        auto* img = static_cast<const IGuiImage*>(widget);
        w.write_string(img->get_image_name().c_str());
        w.write_vec4(img->get_tint());
        w.write_bool(img->get_use_slice9());
        if (img->get_use_slice9()) {
            auto& sb = img->get_slice_border();
            w.write_f32(sb.left); w.write_f32(sb.top);
            w.write_f32(sb.right); w.write_f32(sb.bottom);
            w.write_u8(static_cast<uint8_t>(img->get_slice_center_mode()));
            w.write_vec2(img->get_source_size());
        }
        break;
    }
    case WidgetType::ListBox: {
        auto* lb = static_cast<const IGuiListBox*>(widget);
        w.write_u8(static_cast<uint8_t>(lb->get_selection_mode()));
        w.write_i32(lb->get_selected_item());
        if (opts.save_items) {
            int count = lb->get_item_count();
            w.write_i32(count);
            for (int i = 0; i < count; ++i) {
                w.write_string(lb->get_item_text(i));
                w.write_string(lb->get_item_icon(i));
                w.write_bool(lb->is_item_enabled(i));
            }
        } else {
            w.write_i32(0);
        }
        break;
    }
    case WidgetType::ComboBox: {
        auto* cb = static_cast<const IGuiComboBox*>(widget);
        w.write_i32(cb->get_selected_item());
        w.write_string(cb->get_placeholder());
        if (opts.save_items) {
            int count = cb->get_item_count();
            w.write_i32(count);
            for (int i = 0; i < count; ++i) {
                w.write_string(cb->get_item_text(i));
                w.write_string(cb->get_item_icon(i));
                w.write_bool(cb->is_item_enabled(i));
            }
        } else {
            w.write_i32(0);
        }
        break;
    }
    case WidgetType::TabControl: {
        auto* tc = static_cast<const IGuiTabControl*>(widget);
        w.write_u8(static_cast<uint8_t>(tc->get_tab_position()));
        w.write_u8(static_cast<uint8_t>(tc->get_tab_size_mode()));
        w.write_f32(tc->get_fixed_tab_width());
        w.write_i32(tc->get_active_tab());
        w.write_bool(tc->is_drag_reorder_enabled());
        if (opts.save_items) {
            int count = tc->get_tab_count();
            w.write_i32(count);
            for (int i = 0; i < count; ++i) {
                w.write_string(tc->get_tab_text(i));
                w.write_string(tc->get_tab_icon(i));
                w.write_bool(tc->is_tab_enabled(i));
                w.write_bool(tc->is_tab_closable(i));
                IGuiWidget* content = tc->get_tab_content(i);
                w.write_bool(content != nullptr);
                if (content) write_widget_bin(w, content, opts);
            }
        } else {
            w.write_i32(0);
        }
        break;
    }
    case WidgetType::TreeView: {
        auto* tv = static_cast<const IGuiTreeView*>(widget);
        w.write_u8(static_cast<uint8_t>(tv->get_selection_mode()));
        w.write_i32(tv->get_selected_node());
        w.write_bool(tv->is_drag_reorder_enabled());
        if (opts.save_items) {
            // BFS serialize
            std::vector<int> queue;
            int root_count = tv->get_root_node_count();
            for (int i = 0; i < root_count; ++i)
                queue.push_back(tv->get_root_node(i));
            for (size_t qi = 0; qi < queue.size(); ++qi) {
                int nid = queue[qi];
                int child_count = tv->get_node_child_count(nid);
                for (int c = 0; c < child_count; ++c)
                    queue.push_back(tv->get_node_child(nid, c));
            }
            w.write_i32((int)queue.size());
            for (int nid : queue) {
                w.write_i32(nid);
                w.write_i32(tv->get_node_parent(nid));
                w.write_string(tv->get_node_text(nid));
                w.write_string(tv->get_node_icon(nid));
                w.write_bool(tv->is_node_expanded(nid));
                w.write_bool(tv->is_node_enabled(nid));
            }
        } else {
            w.write_i32(0);
        }
        break;
    }
    case WidgetType::Panel: {
        auto* sp = dynamic_cast<const IGuiSplitPanel*>(widget);
        if (sp) {
            w.write_u8(0); // panel_type: 0=split
            w.write_u8(static_cast<uint8_t>(sp->get_orientation()));
            w.write_f32(sp->get_split_position());
            w.write_f32(sp->get_split_ratio());
            w.write_u8(static_cast<uint8_t>(sp->get_split_unit()));
            w.write_f32(sp->get_first_min_size());
            w.write_f32(sp->get_first_max_size());
            w.write_f32(sp->get_second_min_size());
            w.write_f32(sp->get_second_max_size());
            w.write_bool(sp->is_first_collapsed());
            w.write_bool(sp->is_second_collapsed());
            w.write_bool(sp->is_collapsible());
            w.write_bool(sp->is_splitter_fixed());
            w.write_bool(sp->get_first_panel() != nullptr);
            if (sp->get_first_panel()) write_widget_bin(w, sp->get_first_panel(), opts);
            w.write_bool(sp->get_second_panel() != nullptr);
            if (sp->get_second_panel()) write_widget_bin(w, sp->get_second_panel(), opts);
        } else {
            auto* dp = dynamic_cast<const IGuiDockPanel*>(widget);
            if (dp) {
                w.write_u8(1); // panel_type: 1=dock
                std::string layout = dp->save_layout();
                w.write_string(layout.c_str());
                w.write_i32(dp->get_active_panel());
                w.write_bool(dp->is_drag_docking_enabled());
            } else {
                w.write_u8(255); // unknown panel subtype
            }
        }
        break;
    }
    default:
        break;
    }
}

static void write_widget_bin(BinaryWriter& w, const IGuiWidget* widget,
                             const GuiSerializeOptions& opts) {
    write_base_props_bin(w, widget, opts);
    write_widget_specific_bin(w, widget, opts);

    int child_count = widget->get_child_count();
    w.write_i32(child_count);
    for (int i = 0; i < child_count; ++i)
        write_widget_bin(w, widget->get_child(i), opts);
}

static std::vector<uint8_t> serialize_binary(const IGuiContext* context,
                                              const GuiSerializeOptions& opts) {
    BinaryWriter w;

    // Header
    w.write_u32(BINARY_MAGIC);
    w.write_u16(GUI_SERIALIZE_VERSION_MAJOR);
    w.write_u16(GUI_SERIALIZE_VERSION_MINOR);

    // Flags
    uint8_t flags = 0;
    if (opts.save_styles)     flags |= 0x01;
    if (opts.save_items)      flags |= 0x02;
    if (opts.save_animations) flags |= 0x04;
    if (opts.save_sizers)     flags |= 0x08;
    w.write_u8(flags);

    // Default styles
    if (opts.save_styles) {
        write_gui_style_bin(w, context->get_default_style());
        write_label_style_bin(w, context->get_default_label_style());
    }

    // Root widget tree
    IGuiWidget* root = const_cast<IGuiContext*>(context)->get_root();
    w.write_bool(root != nullptr);
    if (root)
        write_widget_bin(w, root, opts);

    return w.data();
}

// ============================================================================
// Binary Deserialization - Read
// ============================================================================

// Forward declaration
static IGuiWidget* load_widget_bin(IGuiContext* ctx, BinaryReader& r, const GuiSerializeOptions& opts);

static void load_base_props_bin(IGuiWidget* widget, BinaryReader& r, uint8_t /*type_byte*/,
                                const GuiSerializeOptions& opts) {
    std::string name = r.read_string();
    if (!name.empty()) widget->set_name(name.c_str());
    widget->set_bounds(r.read_box());
    widget->set_min_size(r.read_vec2());
    widget->set_max_size(r.read_vec2());
    widget->set_visible(r.read_bool());
    widget->set_enabled(r.read_bool());
    r.read_bool(); // focusable (read-only in most widgets)
    SizeMode wm = static_cast<SizeMode>(r.read_u8());
    SizeMode hm = static_cast<SizeMode>(r.read_u8());
    widget->set_size_mode(wm, hm);
    widget->set_alignment(static_cast<Alignment>(r.read_u8()));
    widget->set_layout_direction(static_cast<LayoutDirection>(r.read_u8()));
    widget->set_spacing(r.read_f32());
    bool clip = r.read_bool();
    widget->set_clip_enabled(clip);
    if (clip)
        widget->set_clip_rect(r.read_box());

    if (opts.save_styles)
        widget->set_style(read_gui_style_bin(r));
}

static IGuiWidget* create_widget_by_type_bin(IGuiContext* ctx, uint8_t type_byte, BinaryReader& r) {
    WidgetType type = static_cast<WidgetType>(type_byte);
    // For types that need construction parameters, peek ahead
    switch (type) {
    case WidgetType::Button:
        return ctx->create_button(ButtonType::Normal);
    case WidgetType::Label:
        return ctx->create_label();
    case WidgetType::TextInput:
        return ctx->create_text_input();
    case WidgetType::Slider:
        return ctx->create_slider(SliderOrientation::Horizontal);
    case WidgetType::ProgressBar:
        return ctx->create_progress_bar(ProgressBarMode::Determinate);
    case WidgetType::Image:
        return ctx->create_image();
    case WidgetType::ScrollArea:
        return ctx->create_scroll_view();
    case WidgetType::ListBox:
        return ctx->create_list_box();
    case WidgetType::ComboBox:
        return ctx->create_combo_box();
    case WidgetType::TabControl:
        return ctx->create_tab_control(TabPosition::Top);
    case WidgetType::TreeView:
        return ctx->create_tree_view();
    case WidgetType::Panel:
        return ctx->create_split_panel(SplitOrientation::Horizontal);
    default:
        return ctx->create_widget(type);
    }
}

static void load_widget_specific_bin(IGuiContext* ctx, IGuiWidget* widget, BinaryReader& r,
                                     const GuiSerializeOptions& opts) {
    WidgetType type = widget->get_type();

    switch (type) {
    case WidgetType::Button: {
        auto* btn = static_cast<IGuiButton*>(widget);
        btn->set_button_type(static_cast<ButtonType>(r.read_u8()));
        std::string text = r.read_string();
        std::string icon = r.read_string();
        if (!text.empty()) btn->set_text(text.c_str());
        if (!icon.empty()) btn->set_icon(icon.c_str());
        btn->set_checked(r.read_bool());
        btn->set_radio_group(r.read_i32());
        break;
    }
    case WidgetType::Label: {
        auto* lbl = static_cast<IGuiLabel*>(widget);
        std::string text = r.read_string();
        if (!text.empty()) lbl->set_text(text.c_str());
        if (opts.save_styles)
            lbl->set_label_style(read_label_style_bin(r));
        break;
    }
    case WidgetType::TextInput: {
        auto* inp = static_cast<IGuiTextInput*>(widget);
        std::string text = r.read_string();
        std::string placeholder = r.read_string();
        if (!text.empty()) inp->set_text(text.c_str());
        if (!placeholder.empty()) inp->set_placeholder(placeholder.c_str());
        inp->set_password_mode(r.read_bool());
        inp->set_read_only(r.read_bool());
        inp->set_max_length(r.read_i32());
        if (opts.save_styles)
            inp->set_label_style(read_label_style_bin(r));
        break;
    }
    case WidgetType::Slider: {
        auto* sl = static_cast<IGuiSlider*>(widget);
        sl->set_orientation(static_cast<SliderOrientation>(r.read_u8()));
        float val = r.read_f32();
        float mn = r.read_f32();
        float mx = r.read_f32();
        sl->set_range(mn, mx);
        sl->set_value(val);
        sl->set_step(r.read_f32());
        sl->set_ticks_visible(r.read_bool());
        sl->set_tick_interval(r.read_f32());
        break;
    }
    case WidgetType::ProgressBar: {
        auto* pb = static_cast<IGuiProgressBar*>(widget);
        pb->set_mode(static_cast<ProgressBarMode>(r.read_u8()));
        pb->set_value(r.read_f32());
        pb->set_text_visible(r.read_bool());
        std::string text = r.read_string();
        if (!text.empty()) pb->set_text(text.c_str());
        break;
    }
    case WidgetType::Image: {
        auto* img = static_cast<IGuiImage*>(widget);
        std::string name = r.read_string();
        img->set_image_name(name);
        img->set_tint(r.read_vec4());
        bool slice9 = r.read_bool();
        img->set_use_slice9(slice9);
        if (slice9) {
            SliceBorder sb;
            sb.left = r.read_f32(); sb.top = r.read_f32();
            sb.right = r.read_f32(); sb.bottom = r.read_f32();
            img->set_slice_border(sb);
            img->set_slice_center_mode(static_cast<SliceCenterMode>(r.read_u8()));
            img->set_source_size(r.read_vec2());
        }
        break;
    }
    case WidgetType::ListBox: {
        auto* lb = static_cast<IGuiListBox*>(widget);
        lb->set_selection_mode(static_cast<ListBoxSelectionMode>(r.read_u8()));
        int sel = r.read_i32();
        int count = r.read_i32();
        for (int i = 0; i < count; ++i) {
            std::string text = r.read_string();
            std::string icon = r.read_string();
            int id = lb->add_item(text.c_str(), icon.empty() ? nullptr : icon.c_str());
            lb->set_item_enabled(id, r.read_bool());
        }
        lb->set_selected_item(sel);
        break;
    }
    case WidgetType::ComboBox: {
        auto* cb = static_cast<IGuiComboBox*>(widget);
        int sel = r.read_i32();
        std::string placeholder = r.read_string();
        if (!placeholder.empty()) cb->set_placeholder(placeholder.c_str());
        int count = r.read_i32();
        for (int i = 0; i < count; ++i) {
            std::string text = r.read_string();
            std::string icon = r.read_string();
            int id = cb->add_item(text.c_str(), icon.empty() ? nullptr : icon.c_str());
            cb->set_item_enabled(id, r.read_bool());
        }
        cb->set_selected_item(sel);
        break;
    }
    case WidgetType::TabControl: {
        auto* tc = static_cast<IGuiTabControl*>(widget);
        tc->set_tab_position(static_cast<TabPosition>(r.read_u8()));
        tc->set_tab_size_mode(static_cast<TabSizeMode>(r.read_u8()));
        tc->set_fixed_tab_width(r.read_f32());
        int active = r.read_i32();
        tc->set_drag_reorder_enabled(r.read_bool());
        int count = r.read_i32();
        for (int i = 0; i < count; ++i) {
            std::string text = r.read_string();
            std::string icon = r.read_string();
            int id = tc->add_tab(text.c_str(), icon.empty() ? nullptr : icon.c_str());
            tc->set_tab_enabled(id, r.read_bool());
            tc->set_tab_closable(id, r.read_bool());
            bool has_content = r.read_bool();
            if (has_content) {
                IGuiWidget* content = load_widget_bin(ctx, r, opts);
                if (content) tc->set_tab_content(id, content);
            }
        }
        tc->set_active_tab(active);
        break;
    }
    case WidgetType::TreeView: {
        auto* tv = static_cast<IGuiTreeView*>(widget);
        tv->set_selection_mode(static_cast<TreeViewSelectionMode>(r.read_u8()));
        int sel = r.read_i32();
        tv->set_drag_reorder_enabled(r.read_bool());
        int count = r.read_i32();
        std::unordered_map<int, int> id_map;
        for (int i = 0; i < count; ++i) {
            int old_id = r.read_i32();
            int old_parent = r.read_i32();
            std::string text = r.read_string();
            std::string icon = r.read_string();
            bool expanded = r.read_bool();
            bool enabled = r.read_bool();
            int parent_id = -1;
            auto pit = id_map.find(old_parent);
            if (pit != id_map.end()) parent_id = pit->second;
            int new_id = tv->add_node(parent_id, text.c_str(), icon.empty() ? nullptr : icon.c_str());
            id_map[old_id] = new_id;
            tv->set_node_expanded(new_id, expanded);
            tv->set_node_enabled(new_id, enabled);
        }
        tv->set_selected_node(sel);
        break;
    }
    case WidgetType::Panel: {
        uint8_t panel_type = r.read_u8();
        if (panel_type == 0) { // split
            auto* sp = dynamic_cast<IGuiSplitPanel*>(widget);
            if (sp) {
                sp->set_orientation(static_cast<SplitOrientation>(r.read_u8()));
                sp->set_split_position(r.read_f32());
                sp->set_split_ratio(r.read_f32());
                sp->set_split_unit(static_cast<SplitSizeUnit>(r.read_u8()));
                sp->set_first_min_size(r.read_f32());
                sp->set_first_max_size(r.read_f32());
                sp->set_second_min_size(r.read_f32());
                sp->set_second_max_size(r.read_f32());
                sp->set_first_collapsed(r.read_bool());
                sp->set_second_collapsed(r.read_bool());
                sp->set_collapsible(r.read_bool());
                sp->set_splitter_fixed(r.read_bool());
                if (r.read_bool()) {
                    IGuiWidget* child = load_widget_bin(ctx, r, opts);
                    if (child) sp->set_first_panel(child);
                }
                if (r.read_bool()) {
                    IGuiWidget* child = load_widget_bin(ctx, r, opts);
                    if (child) sp->set_second_panel(child);
                }
            }
        } else if (panel_type == 1) { // dock
            auto* dp = dynamic_cast<IGuiDockPanel*>(widget);
            if (dp) {
                std::string layout = r.read_string();
                dp->load_layout(layout.c_str());
                dp->set_active_panel(r.read_i32());
                dp->set_drag_docking_enabled(r.read_bool());
            }
        }
        // else: unknown panel subtype, skip
        break;
    }
    default:
        break;
    }
}

static IGuiWidget* load_widget_bin(IGuiContext* ctx, BinaryReader& r,
                                   const GuiSerializeOptions& opts) {
    if (!r.ok()) return nullptr;

    uint8_t type_byte = r.read_u8();
    IGuiWidget* widget = create_widget_by_type_bin(ctx, type_byte, r);
    if (!widget) return nullptr;

    load_base_props_bin(widget, r, type_byte, opts);
    load_widget_specific_bin(ctx, widget, r, opts);

    int child_count = r.read_i32();
    for (int i = 0; i < child_count && r.ok(); ++i) {
        IGuiWidget* child = load_widget_bin(ctx, r, opts);
        if (child) widget->add_child(child);
    }

    return widget;
}

static GuiSerializeResult deserialize_binary(IGuiContext* ctx, const uint8_t* data, size_t size,
                                              const GuiSerializeOptions& opts) {
    BinaryReader r(data, size);

    // Header
    uint32_t magic = r.read_u32();
    if (magic != BINARY_MAGIC)
        return GuiSerializeResult::ErrorParse;

    GuiSerializeVersion file_ver;
    file_ver.major = r.read_u16();
    file_ver.minor = r.read_u16();

    GuiSerializeVersion current;
    if (!current.is_compatible(file_ver))
        return GuiSerializeResult::ErrorVersionMismatch;

    uint8_t flags = r.read_u8();
    bool has_styles = (flags & 0x01) != 0;
    // bool has_items  = (flags & 0x02) != 0;  // implicitly handled by save_items option

    if (!r.ok()) return GuiSerializeResult::ErrorParse;

    // Clear
    if (opts.clear_before_load) {
        IGuiWidget* old_root = ctx->get_root();
        if (old_root) old_root->clear_children();
    }

    // Default styles
    if (has_styles) {
        ctx->set_default_style(read_gui_style_bin(r));
        ctx->set_default_label_style(read_label_style_bin(r));
    }

    // Root
    bool has_root = r.read_bool();
    if (has_root && r.ok()) {
        // The serialized root contains base props + children
        // We load the entire tree and transplant children to context root
        IGuiWidget* loaded_root = load_widget_bin(ctx, r, opts);
        if (loaded_root) {
            IGuiWidget* ctx_root = ctx->get_root();
            if (ctx_root) {
                // Move children from loaded root to context root
                int count = loaded_root->get_child_count();
                for (int i = 0; i < count; ++i) {
                    IGuiWidget* child = loaded_root->get_child(i);
                    if (child) {
                        child->set_parent(nullptr);
                        ctx_root->add_child(child);
                    }
                }
                // Apply base props to context root
                if (loaded_root->get_name() && loaded_root->get_name()[0])
                    ctx_root->set_name(loaded_root->get_name());
                ctx_root->set_bounds(loaded_root->get_bounds());
                ctx_root->set_visible(loaded_root->is_visible());
                ctx_root->set_style(loaded_root->get_style());
            }
            ctx->destroy_widget(loaded_root);
        }
    }

    if (!r.ok()) return GuiSerializeResult::ErrorParse;
    return GuiSerializeResult::Success;
}

// ============================================================================
// File I/O helpers
// ============================================================================

static bool read_file(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    size_t read = fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    return read == (size_t)sz;
}

static bool write_file(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size;
}

// ============================================================================
// Public API Implementation
// ============================================================================

GuiSerializeResult gui_save(const IGuiContext* context,
                            const char* filepath,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options) {
    if (!context || !filepath) return GuiSerializeResult::ErrorInvalidParameter;

    std::vector<uint8_t> buf;
    GuiSerializeResult r = gui_save(context, buf, format, options);
    if (r != GuiSerializeResult::Success) return r;

    if (!write_file(filepath, buf.data(), buf.size()))
        return GuiSerializeResult::ErrorFileWrite;
    return GuiSerializeResult::Success;
}

GuiSerializeResult gui_load(IGuiContext* context,
                            const char* filepath,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options) {
    if (!context || !filepath) return GuiSerializeResult::ErrorInvalidParameter;

    std::vector<uint8_t> buf;
    if (!read_file(filepath, buf))
        return GuiSerializeResult::ErrorFileOpen;

    return gui_load(context, buf.data(), buf.size(), format, options);
}

GuiSerializeResult gui_save(const IGuiContext* context,
                            std::vector<uint8_t>& out_buffer,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options) {
    if (!context) return GuiSerializeResult::ErrorInvalidParameter;

    if (format == GuiSerializeFormat::Json) {
        std::string json = serialize_json(context, options);
        size_t offset = out_buffer.size();
        out_buffer.resize(offset + json.size());
        std::memcpy(out_buffer.data() + offset, json.data(), json.size());
    } else {
        auto bin = serialize_binary(context, options);
        size_t offset = out_buffer.size();
        out_buffer.resize(offset + bin.size());
        std::memcpy(out_buffer.data() + offset, bin.data(), bin.size());
    }
    return GuiSerializeResult::Success;
}

GuiSerializeResult gui_load(IGuiContext* context,
                            const uint8_t* data, size_t size,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options) {
    if (!context || !data || size == 0)
        return GuiSerializeResult::ErrorInvalidParameter;

    if (format == GuiSerializeFormat::Json) {
        return deserialize_json(context, (const char*)data, size, options);
    } else {
        return deserialize_binary(context, data, size, options);
    }
}

GuiSerializeResult gui_save_widget(const IGuiWidget* widget,
                                   std::vector<uint8_t>& out_buffer,
                                   GuiSerializeFormat format,
                                   const GuiSerializeOptions& options) {
    if (!widget) return GuiSerializeResult::ErrorInvalidParameter;

    if (format == GuiSerializeFormat::Json) {
        pt::ptree tree;
        pt::ptree ver;
        ver.put("major", GUI_SERIALIZE_VERSION_MAJOR);
        ver.put("minor", GUI_SERIALIZE_VERSION_MINOR);
        tree.add_child("version", ver);
        tree.add_child("widget", write_widget_pt(widget, options));
        std::ostringstream ss;
        pt::write_json(ss, tree, options.pretty_print);
        std::string json = ss.str();
        size_t offset = out_buffer.size();
        out_buffer.resize(offset + json.size());
        std::memcpy(out_buffer.data() + offset, json.data(), json.size());
    } else {
        BinaryWriter w;
        w.write_u32(BINARY_MAGIC);
        w.write_u16(GUI_SERIALIZE_VERSION_MAJOR);
        w.write_u16(GUI_SERIALIZE_VERSION_MINOR);
        uint8_t flags = 0;
        if (options.save_styles) flags |= 0x01;
        if (options.save_items) flags |= 0x02;
        w.write_u8(flags);
        write_widget_bin(w, widget, options);
        const auto& d = w.data();
        size_t offset = out_buffer.size();
        out_buffer.resize(offset + d.size());
        std::memcpy(out_buffer.data() + offset, d.data(), d.size());
    }
    return GuiSerializeResult::Success;
}

GuiSerializeResult gui_load_widget(IGuiContext* context,
                                   const uint8_t* data, size_t size,
                                   GuiSerializeFormat format,
                                   IGuiWidget** out_widget,
                                   const GuiSerializeOptions& options) {
    if (!context || !data || size == 0 || !out_widget)
        return GuiSerializeResult::ErrorInvalidParameter;

    *out_widget = nullptr;

    if (format == GuiSerializeFormat::Json) {
        try {
            std::istringstream ss(std::string((const char*)data, size));
            pt::ptree root;
            pt::read_json(ss, root);

            const pt::ptree* ver = pt_get_child(root, "version");
            if (ver) {
                GuiSerializeVersion file_ver;
                file_ver.major = (uint16_t)pt_get_int(*ver, "major", 1);
                file_ver.minor = (uint16_t)pt_get_int(*ver, "minor", 0);
                GuiSerializeVersion current;
                if (!current.is_compatible(file_ver))
                    return GuiSerializeResult::ErrorVersionMismatch;
            }

            const pt::ptree* widget_val = pt_get_child(root, "widget");
            if (!widget_val) return GuiSerializeResult::ErrorInvalidData;

            *out_widget = load_widget_pt(context, *widget_val, options);
            return *out_widget ? GuiSerializeResult::Success : GuiSerializeResult::ErrorInvalidData;
        } catch (...) {
            return GuiSerializeResult::ErrorParse;
        }
    } else {
        BinaryReader r(data, size);
        uint32_t magic = r.read_u32();
        if (magic != BINARY_MAGIC)
            return GuiSerializeResult::ErrorParse;

        GuiSerializeVersion file_ver;
        file_ver.major = r.read_u16();
        file_ver.minor = r.read_u16();
        GuiSerializeVersion current;
        if (!current.is_compatible(file_ver))
            return GuiSerializeResult::ErrorVersionMismatch;

        r.read_u8(); // flags

        *out_widget = load_widget_bin(context, r, options);
        return (*out_widget && r.ok()) ? GuiSerializeResult::Success : GuiSerializeResult::ErrorParse;
    }
}

} // namespace gui
} // namespace window
