/*
 * gui_sizer.hpp - Sizer Layout System
 *
 * Sizers are standalone layout managers that position and size a collection of
 * widgets.  Unlike IGuiLayout (which operates on a single widget's children),
 * a sizer owns its own item list with per-item proportion and border metadata.
 *
 * Usage:
 *   IBoxSizer* row = create_box_sizer(LayoutDirection::Horizontal);
 *   row->add(label_widget, 0, SizerFlag::Center, 4.0f);
 *   row->add(input_widget, 1, SizerFlag::Expand | SizerFlag::All, 4.0f);
 *   row->set_bounds(my_box);
 *   row->layout();          // positions all widgets
 */

#ifndef WINDOW_GUI_SIZER_HPP
#define WINDOW_GUI_SIZER_HPP

namespace window {
namespace gui {

// ============================================================================
// SizerFlag - Per-item layout hints
// ============================================================================

enum class SizerFlag : uint8_t {
    None    = 0x00,
    Expand  = 0x01,   // Expand widget to fill the cross axis of the sizer
    Left    = 0x02,   // Apply border space on the left
    Right   = 0x04,   // Apply border space on the right
    Top     = 0x08,   // Apply border space on the top
    Bottom  = 0x10,   // Apply border space on the bottom
    All     = 0x1E,   // Apply border space on all sides (Left|Right|Top|Bottom)
    Center  = 0x20,   // Center on cross axis (ignored when Expand is set)
};

inline SizerFlag operator|(SizerFlag a, SizerFlag b) {
    return static_cast<SizerFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline SizerFlag operator&(SizerFlag a, SizerFlag b) {
    return static_cast<SizerFlag>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool has_flag(SizerFlag flags, SizerFlag f) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(f)) != 0;
}

// ============================================================================
// SizerItem - One slot managed by a sizer
// ============================================================================

class ISizer;  // Forward

struct SizerItem {
    IGuiWidget* widget     = nullptr;          // Widget to position (null if nested sizer or spacer)
    ISizer*     sizer      = nullptr;          // Nested sizer (null if widget or spacer)
    int         proportion = 0;                // 0 = fixed size, >0 = proportional share of free space
    SizerFlag   flags      = SizerFlag::None;  // Layout hints
    float       border     = 0.0f;             // Border width applied to sides selected by flags
    math::Vec2  fixed_size;                    // For spacers: size; for widgets: 0 = use get_preferred_size()
    bool        visible    = true;
};

// ============================================================================
// ISizer - Base interface for all sizer types
// ============================================================================

class ISizer {
public:
    virtual ~ISizer() = default;

    // --- Adding items ---

    // Add a widget.  proportion=0 → fixed (preferred size), >0 → share free space.
    virtual void add(IGuiWidget* widget,
                     int proportion       = 0,
                     SizerFlag flags      = SizerFlag::None,
                     float border        = 0.0f) = 0;

    // Add a nested sizer (treated as a fixed or proportional block).
    virtual void add(ISizer* child_sizer,
                     int proportion       = 0,
                     SizerFlag flags      = SizerFlag::None,
                     float border        = 0.0f) = 0;

    // Add a fixed blank space along the main axis.
    virtual void add_spacer(float size) = 0;

    // Add a proportional blank space (stretch).
    virtual void add_stretch(int proportion = 1) = 0;

    // Insert a widget at a specific index.
    virtual void insert(int index,
                        IGuiWidget* widget,
                        int proportion       = 0,
                        SizerFlag flags      = SizerFlag::None,
                        float border        = 0.0f) = 0;

    // --- Removing items ---

    virtual void remove(IGuiWidget* widget) = 0;
    virtual void remove_at(int index) = 0;
    virtual void clear() = 0;

    // --- Item access ---

    virtual int              get_item_count() const = 0;
    virtual const SizerItem& get_item(int index) const = 0;
    virtual SizerItem*       find_item(IGuiWidget* widget) = 0;
    virtual void             set_item_visible(IGuiWidget* widget, bool visible) = 0;

    // --- Geometry ---

    // Set the bounding box the sizer lays items into.  Call before layout().
    virtual void       set_bounds(const math::Box& bounds) = 0;
    virtual math::Box  get_bounds() const = 0;

    // Minimum size needed to show all items at their minimum/preferred sizes.
    virtual math::Vec2 get_min_size() const = 0;

    // Apply layout: set_bounds() must be called first.
    virtual void layout() = 0;

    // --- Padding around the content area ---

    virtual void       set_padding(float all) = 0;
    virtual void       set_padding(float h, float v) = 0;
    virtual void       set_padding(float left, float top, float right, float bottom) = 0;
    virtual math::Vec4 get_padding() const = 0;  // x=left, y=top, z=right, w=bottom

    // --- Gap between items ---

    virtual void  set_gap(float gap) = 0;
    virtual float get_gap() const = 0;
};

// ============================================================================
// IBoxSizer - Horizontal or vertical linear layout
// ============================================================================

class IBoxSizer : public ISizer {
public:
    virtual LayoutDirection get_direction() const = 0;
};

// ============================================================================
// IGridSizer - Grid layout with a fixed column count
//
// Items are placed left-to-right, top-to-bottom.
// All cells are the same size (max preferred width / max preferred height).
// ============================================================================

class IGridSizer : public ISizer {
public:
    virtual int   get_cols() const = 0;
    virtual void  set_cols(int cols) = 0;

    // Horizontal gap between columns.
    virtual float get_hgap() const = 0;
    virtual void  set_hgap(float gap) = 0;

    // Vertical gap between rows.
    virtual float get_vgap() const = 0;
    virtual void  set_vgap(float gap) = 0;
};

// ============================================================================
// IFlowSizer - Wrapping flow layout
//
// Items are placed along the primary axis and wrap to the next line when they
// would exceed the sizer bounds.
// ============================================================================

class IFlowSizer : public ISizer {
public:
    virtual LayoutDirection get_direction() const = 0;

    // Gap between wrapped lines (perpendicular to primary axis).
    virtual float get_line_gap() const = 0;
    virtual void  set_line_gap(float gap) = 0;
};

// ============================================================================
// Factory functions
// ============================================================================

IBoxSizer*  create_box_sizer(LayoutDirection direction);
IGridSizer* create_grid_sizer(int cols, float hgap = 0.0f, float vgap = 0.0f);
IFlowSizer* create_flow_sizer(LayoutDirection direction = LayoutDirection::Horizontal);
void        destroy_sizer(ISizer* sizer);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_SIZER_HPP
