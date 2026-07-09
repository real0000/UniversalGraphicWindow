/*
 * gui_property.hpp - PropertyGrid Interface
 *
 * Contains IGuiPropertyGrid for editable name/value property lists.
 */

#ifndef WINDOW_GUI_PROPERTY_HPP
#define WINDOW_GUI_PROPERTY_HPP

namespace window {
namespace gui {

// ============================================================================
// PropertyGrid Interface - Editable name/value property list
// ============================================================================

enum class PropertyType : uint8_t {
    String = 0,
    Int,
    Float,
    Bool,
    Color,          // Vec4 RGBA
    Vec2,
    Vec4,
    Enum,           // Dropdown selection from options
    Range,          // Float with min/max (rendered as slider)
    Category        // Group header, no value
};

// PropertyGridStyle is defined in gui_styles.hpp (presets in gui_styles.cpp).

struct PropertyRenderItem {
    int property_id = -1;
    const char* name = nullptr;
    const char* category = nullptr;
    PropertyType type = PropertyType::String;
    int depth = 0;
    bool is_category_header = false;
    bool expanded = true;
    bool read_only = false;
    bool selected = false;
    math::Box row_rect;
    math::Box name_rect;
    math::Box value_rect;
};

struct PropertyGridRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    PropertyGridStyle style;
    int total_row_count = 0;
    int visible_row_count = 0;
    int selected_property = -1;
    float scroll_offset_y = 0.0f;

    // Editing state
    int editing_property = -1;      // property_id being edited, -1 = none
    const char* edit_buffer = "";   // Current text being edited
};

// Declarative property row for IGuiPropertyGrid::set_properties — the app hands the
// grid a whole form and it diffs/rebuilds/self-renders. Structured node editors map
// to categories: e.g. an "If" node = a "Case 1" category with name/operator/value
// rows. `id` is stable and echoed back by on_property_changed. svalue carries
// String/Int/Float/Range as text; bvalue is Bool; options+enum_index are Enum.
struct PropertyModel {
    int         id = -1;
    std::string category, name;
    PropertyType type = PropertyType::String;
    std::string svalue;
    bool        bvalue = false;
    std::vector<std::string> options;
    int         enum_index = 0;
    bool        read_only = false;
};

class IPropertyGridEventHandler {
public:
    virtual ~IPropertyGridEventHandler() = default;
    virtual void on_property_changed(int property_id) = 0;
};

class IGuiPropertyGrid : public IGuiWidget {
public:
    virtual ~IGuiPropertyGrid() = default;

    // Property management
    virtual int add_property(const char* category, const char* name, PropertyType type) = 0;
    virtual bool remove_property(int property_id) = 0;
    virtual void clear_properties() = 0;
    virtual int get_property_count() const = 0;

    // Model-driven population: replace the whole form from a declarative model in one
    // idempotent call. When the STRUCTURE (ids/names/categories/types/options) is
    // unchanged only values update in place — scroll position and an in-progress
    // inline edit are preserved; a structural change rebuilds. The app owns the model
    // and may rebuild + push on every change (mirrors ListBox::set_items).
    virtual void set_properties(const std::vector<PropertyModel>& props) = 0;

    // Property info
    virtual const char* get_property_name(int property_id) const = 0;
    virtual const char* get_property_category(int property_id) const = 0;
    virtual PropertyType get_property_type(int property_id) const = 0;

    // Value access - String
    virtual const char* get_string_value(int property_id) const = 0;
    virtual void set_string_value(int property_id, const char* value) = 0;

    // Value access - Int
    virtual int get_int_value(int property_id) const = 0;
    virtual void set_int_value(int property_id, int value) = 0;

    // Value access - Float
    virtual float get_float_value(int property_id) const = 0;
    virtual void set_float_value(int property_id, float value) = 0;

    // Value access - Bool
    virtual bool get_bool_value(int property_id) const = 0;
    virtual void set_bool_value(int property_id, bool value) = 0;

    // Value access - Vec2
    virtual math::Vec2 get_vec2_value(int property_id) const = 0;
    virtual void set_vec2_value(int property_id, const math::Vec2& value) = 0;

    // Value access - Vec4 / Color
    virtual math::Vec4 get_vec4_value(int property_id) const = 0;
    virtual void set_vec4_value(int property_id, const math::Vec4& value) = 0;

    // Enum options
    virtual void set_enum_options(int property_id, const std::vector<std::string>& options) = 0;
    virtual const std::vector<std::string>& get_enum_options(int property_id) const = 0;
    virtual int get_enum_index(int property_id) const = 0;
    virtual void set_enum_index(int property_id, int index) = 0;

    // Range limits (for PropertyType::Range)
    virtual void set_range_limits(int property_id, float min_val, float max_val) = 0;
    virtual float get_range_min(int property_id) const = 0;
    virtual float get_range_max(int property_id) const = 0;

    // Read-only
    virtual bool is_property_read_only(int property_id) const = 0;
    virtual void set_property_read_only(int property_id, bool read_only) = 0;

    // Category management
    virtual bool is_category_expanded(const char* category) const = 0;
    virtual void set_category_expanded(const char* category, bool expanded) = 0;
    virtual void expand_all() = 0;
    virtual void collapse_all() = 0;

    // Selection
    virtual int get_selected_property() const = 0;
    virtual void set_selected_property(int property_id) = 0;

    // Scrolling
    virtual float get_scroll_offset() const = 0;
    virtual void set_scroll_offset(float offset) = 0;
    virtual float get_total_content_height() const = 0;

    // Layout
    virtual float get_name_column_width() const = 0;
    virtual void set_name_column_width(float width) = 0;
    virtual float get_row_height() const = 0;
    virtual void set_row_height(float height) = 0;

    // Style
    virtual const PropertyGridStyle& get_property_grid_style() const = 0;
    virtual void set_property_grid_style(const PropertyGridStyle& style) = 0;

    // Event handler
    virtual void set_property_event_handler(IPropertyGridEventHandler* handler) = 0;

    // Render info
    virtual void get_property_grid_render_info(PropertyGridRenderInfo* out_info) const = 0;
    virtual int get_visible_property_items(PropertyRenderItem* out_items, int max_items) const = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_PROPERTY_HPP
