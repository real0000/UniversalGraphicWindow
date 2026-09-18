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
// A small action affordance on a property row (a right-aligned button). Lets a
// structured editor put "+" on a section/card header and "↑ ↓ ×" on a card without
// leaving the grid; a click fires on_property_action(row id, action.id). A row of
// type Category renders as a plain header (name only, no value box) — use it for the
// section ("Cases  [+]") and card ("#1  ↑ ↓ ×") header rows.
struct PropertyAction {
    int         id = 0;
    std::string label;                                 // button glyph/text ("+", "×", "^", "v")
    math::Vec4  color = math::Vec4(0, 0, 0, 0);        // fill; alpha 0 = style default
};

struct PropertyModel {
    int         id = -1;
    std::string key;                                   // opaque app routing tag (echoed via get_property_key)
    std::string category, name;
    PropertyType type = PropertyType::String;
    std::string svalue;
    bool        bvalue = false;
    std::vector<std::string> options;                  // enum DISPLAY labels
    std::vector<std::string> option_values;            // enum VALUES (parallel; empty → value == label)
    int         enum_index = 0;
    bool        read_only = false;
    // Multi-line, user-resizable text (String rows only): the value box is a tall,
    // word-wrapping editor with a drag grip to change its height, instead of the
    // one-line field. Height persists per row (keyed by `key`) across rebuilds, so
    // the app just declares `multiline = true` once and never tracks the size.
    bool        multiline = false;
    std::vector<PropertyAction> actions;               // right-aligned row buttons
};

class IPropertyGridEventHandler {
public:
    virtual ~IPropertyGridEventHandler() = default;
    virtual void on_property_changed(int property_id) = 0;
    // A row action button was clicked (PropertyModel::actions). Default no-op so
    // existing handlers keep compiling.
    virtual void on_property_action(int property_id, int action_id) { (void)property_id; (void)action_id; }
    // Array-section CRUD (set_form): add an element to the array tagged `array_key`;
    // move element `index` by `delta` (-1 up / +1 down); remove element `index`. The
    // grid owns all the header/reorder/delete chrome and just tells the app what to
    // mutate — the app routes purely by its own `array_key`, no id bookkeeping.
    virtual void on_property_array_add(const char* array_key) { (void)array_key; }
    virtual void on_property_array_move(const char* array_key, int index, int delta) { (void)array_key; (void)index; (void)delta; }
    virtual void on_property_array_remove(const char* array_key, int index) { (void)array_key; (void)index; }
};

// One element of a PropertyArray: its sub-rows plus per-card chrome overrides. `title`
// replaces the generated "#N" card header when non-empty (e.g. a pin shows its name);
// `movable`/`removable` gate the ↑↓ / × on THIS card (ANDed with the array-level
// can_move/can_remove — a FIXED pin sets removable=false while its list still adds and
// removes other pins). A card with no `fields` renders as just the header row.
struct PropertyArrayElement {
    std::string title;                      // card header label (empty → "#N")
    bool        movable = true;
    bool        removable = true;
    std::vector<PropertyModel> fields;      // element's sub-rows (may be empty)
};

// A repeated group of sub-property rows with built-in add/reorder/delete chrome. The
// grid renders a section header (title + "+"), and per element a "#N" card header
// (↑ ↓ ×) followed by that element's fields — the app only declares the fields and
// handles on_property_array_*. Element field ids stay app-owned (echoed by
// on_property_changed); the section/card headers are generated by the grid.
struct PropertyArray {
    std::string key;                        // opaque app routing tag (echoed by on_property_array_*)
    std::string title;                      // section header label
    bool        can_add = true;             // "+" on the section header
    bool        can_move = true;            // "↑ ↓" on each card (gated per-element by movable)
    bool        can_remove = true;          // "×" on each card (gated per-element by removable)
    std::vector<PropertyArrayElement> elements;
};

// A full inspector form: flat scalar rows, then the array sections (in order).
struct PropertyForm {
    std::vector<PropertyModel> props;
    std::vector<PropertyArray> arrays;
};

// ── Declarative field types ─────────────────────────────────────────────────
// A PropertyForm is plain data, so building one by hand means filling a
// PropertyModel field-by-field at every call site — and re-deriving the fiddly
// parts (an enum's parallel label/value arrays, its selected index, what to do
// when the stored value isn't in the option list) each time.
//
// A field type owns that: it knows HOW one kind of row is shaped. WHERE the
// value comes from stays with the app, behind IPropertyFieldSource, so this
// layer needs no opinion about the app's model (JSON, a struct, a DB row).
// Building a form then reads as "which fields, in what order":
//
//     TextPropertyField("Name", "name").emit(src, prefix, el.fields);
//     SelectPropertyField("Type", "value_type").emit(src, prefix, el.fields);
//
// `prefix` is the app's routing scope for a row inside an array card (e.g.
// "@if:0:"); the row's key is prefix + the field key, which is exactly what the
// grid echoes back on edit. Empty prefix = a top-level row.
class IPropertyFieldSource {
public:
    virtual ~IPropertyFieldSource() = default;
    // Current value of `key` within `prefix`'s scope ("" / `def` when absent).
    virtual std::string text_value(const std::string& prefix, const std::string& key) const = 0;
    virtual bool        bool_value(const std::string& prefix, const std::string& key, bool def) const = 0;
    // (value, label) choices for a select. `full_key` is prefix + key so options
    // may differ per card; `aux` carries the field's own hint (e.g. which list).
    virtual std::vector<std::pair<std::string, std::string>>
        options(const std::string& full_key, const std::string& aux) const = 0;
};

class IPropertyField {
public:
    virtual ~IPropertyField() = default;
    virtual void emit(const IPropertyFieldSource& src, const std::string& prefix,
                      std::vector<PropertyModel>& into) const = 0;
};

// Free-text row (read_only → shown but not editable).
class TextPropertyField : public IPropertyField {
public:
    TextPropertyField(std::string name, std::string key, bool read_only = false)
        : name_(std::move(name)), key_(std::move(key)), ro_(read_only) {}
    void emit(const IPropertyFieldSource& src, const std::string& prefix,
              std::vector<PropertyModel>& into) const override;
private:
    std::string name_, key_; bool ro_;
};

// Multi-line, user-resizable free-text row (a prompt / template / note). Same
// value source as TextPropertyField, but the grid renders a tall word-wrapping
// editor with a drag grip; the height is remembered per row.
class MultilineTextField : public IPropertyField {
public:
    MultilineTextField(std::string name, std::string key, bool read_only = false)
        : name_(std::move(name)), key_(std::move(key)), ro_(read_only) {}
    void emit(const IPropertyFieldSource& src, const std::string& prefix,
              std::vector<PropertyModel>& into) const override;
private:
    std::string name_, key_; bool ro_;
};

// Enum row built from the source's options. When the stored value is missing
// from that list (stale reference, or the list hasn't loaded yet) it is appended
// as a selectable option rather than silently snapping the row to the first
// entry — the user keeps seeing what is actually stored.
class SelectPropertyField : public IPropertyField {
public:
    SelectPropertyField(std::string name, std::string key, std::string aux = "")
        : name_(std::move(name)), key_(std::move(key)), aux_(std::move(aux)) {}
    void emit(const IPropertyFieldSource& src, const std::string& prefix,
              std::vector<PropertyModel>& into) const override;
private:
    std::string name_, key_, aux_;
};

class CheckPropertyField : public IPropertyField {
public:
    CheckPropertyField(std::string name, std::string key, bool default_val = false)
        : name_(std::move(name)), key_(std::move(key)), def_(default_val) {}
    void emit(const IPropertyFieldSource& src, const std::string& prefix,
              std::vector<PropertyModel>& into) const override;
private:
    std::string name_, key_; bool def_;
};

// Read-only caption carrying a literal value. It has no key, so an edit can
// never route to it.
class LabelPropertyField : public IPropertyField {
public:
    LabelPropertyField(std::string name, std::string value)
        : name_(std::move(name)), value_(std::move(value)) {}
    void emit(const IPropertyFieldSource& src, const std::string& prefix,
              std::vector<PropertyModel>& into) const override;
private:
    std::string name_, value_;
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
    virtual void set_properties(std::vector<PropertyModel> props) = 0;   // by value: sink (move-friendly rebinds)

    // Bind a data provider (set ONCE): the grid re-reads it before each render (via
    // refresh_bindings) and set_properties() the result, so the app mutates its own
    // data + asks for a repaint and the inspector reacts. An in-progress inline edit
    // is preserved (set_properties keeps it). Pass {} to clear.
    virtual void bind(std::function<std::vector<PropertyModel>()> provider) = 0;

    // Declarative form with array sections. The grid expands `props` + each array
    // (section header "+", per-element "#N" card header "↑ ↓ ×", element fields) into
    // its rows, owning all that CRUD chrome, and fires on_property_array_* on a click.
    // Idempotent like set_properties (unchanged form → no repaint). set_form and
    // set_properties are mutually exclusive views of the same grid.
    virtual void set_form(PropertyForm form) = 0;   // by value: sink (move-friendly rebinds)
    // Bind a form provider (set ONCE); re-read before each render (refresh_bindings).
    virtual void bind_form(std::function<PropertyForm()> provider) = 0;

    // Property info
    virtual const char* get_property_name(int property_id) const = 0;
    virtual const char* get_property_category(int property_id) const = 0;
    virtual PropertyType get_property_type(int property_id) const = 0;
    // App routing tag set via PropertyModel::key. `get_property_value` returns the
    // CANONICAL value the app should store: "true"/"false" for Bool, the selected
    // option_value (or label) for Enum, else the current text — so a change handler can
    // route by (key, value) alone, with no id→field bookkeeping.
    virtual const char* get_property_key(int property_id) const = 0;
    virtual const char* get_property_value(int property_id) const = 0;

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

    // Semantic driving (automation / scripting) — apply an edit by KEY exactly as a user
    // interaction would and fire the SAME handler callbacks, so a driver never needs the
    // grid's internal geometry. Return false if no row matches `key`.
    virtual bool commit_text(const char* key, const char* text) = 0;   // String/Int/Float row
    virtual bool select_enum(const char* key, int option_index) = 0;   // Enum row → on_property_changed
    virtual bool toggle_bool(const char* key) = 0;                     // Bool row → on_property_changed
    virtual bool invoke_array_add(const char* array_key) = 0;          // section "+" → on_property_array_add
    virtual bool invoke_array_remove(const char* array_key, int index) = 0;
    virtual bool invoke_array_move(const char* array_key, int index, int delta) = 0;
    // A row button (PropertyModel::actions) → on_property_action. False if no row matches
    // `key` or the row has no action with that id.
    virtual bool invoke_action(const char* key, int action_id) = 0;
    // Options of an Enum row (by key), for a driver to pick by value/label. Returns count
    // (0 if not an enum / not found); fills out_values/out_labels up to max.
    virtual int  get_enum_options(const char* key, const char** out_values, const char** out_labels, int max) const = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_PROPERTY_HPP
