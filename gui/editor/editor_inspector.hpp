/*
 * editor_inspector.hpp - Property Inspector Panel
 *
 * Displays and edits properties of the currently selected widget.
 * Uses IGuiPropertyGrid to show widget name, bounds, style, layout, etc.
 * Type-specific sections (Text, Checked, Value, Range...) are rebuilt
 * each time the selection changes so only relevant properties appear.
 */

#ifndef GUI_EDITOR_INSPECTOR_HPP
#define GUI_EDITOR_INSPECTOR_HPP

#include "gui/gui.hpp"
#include "editor_commands.hpp"
#include <string>

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// Property IDs for the inspector grid
// ============================================================================

enum class InspectorProperty : int {
    // Identity
    Name = 0,
    Type,
    Visible,
    Enabled,

    // Geometry
    PosX,
    PosY,
    Width,
    Height,
    WidthMode,
    HeightMode,
    Alignment,

    // Layout
    LayoutDirection,
    Spacing,

    // Style - Colors
    BackgroundColor,
    BorderColor,
    HoverColor,
    PressedColor,
    DisabledColor,
    FocusColor,

    // Style - Sizing
    BorderWidth,
    CornerRadius,
    PaddingLeft,
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    MarginLeft,
    MarginTop,
    MarginRight,
    MarginBottom,

    // Widget-specific: text-bearing widgets (Label, Button, Checkbox, RadioButton, TextInput)
    Text,
    // Widget-specific: Label / TextInput — LabelStyle
    LabelFontSize,
    LabelTextColor,
    LabelAlignment,
    LabelWordWrap,
    LabelEllipsis,
    // Widget-specific: TextInput
    Placeholder,
    TextInputPasswordMode,
    TextInputReadOnly,
    TextInputMaxLength,
    // Widget-specific: Checkbox / RadioButton
    Checked,
    // Widget-specific: RadioButton
    RadioGroup,
    // Widget-specific: Button
    ButtonTypeEnum,
    ButtonIcon,
    ButtonStylePresetEnum,
    // Widget-specific: Slider
    SliderMin,
    SliderMax,
    SliderValue,
    SliderOrientationEnum,
    SliderStep,
    SliderTicksVisible,
    SliderTickInterval,
    // Widget-specific: ProgressBar
    ProgressValue,
    ProgressModeEnum,
    ProgressShowText,
    ProgressText,

    // Widget-specific: Image
    ImageName,
    ImageTint,
    ImageUseSlice9,
    ImageSliceLeft,
    ImageSliceTop,
    ImageSliceRight,
    ImageSliceBottom,

    // Widget-specific: ScrollArea
    ScrollHVisibility,
    ScrollVVisibility,
    ScrollSpeed,

    // Widget-specific: ListBox
    ListSelectionMode,

    // Widget-specific: ComboBox
    ComboPlaceholder,

    // Widget-specific: TabControl
    TabPosition,
    TabActiveTab,

    // Widget-specific: TreeView
    TreeSelectionMode,
    TreeShowLines,

    // Widget-specific: Button transition animation
    TransDuration,
    TransNormalTint,
    TransNormalScale,
    TransNormalOffsetX,
    TransNormalOffsetY,
    TransHoverTint,
    TransHoverScale,
    TransHoverOffsetX,
    TransHoverOffsetY,
    TransPressedTint,
    TransPressedScale,
    TransPressedOffsetX,
    TransPressedOffsetY,
    TransDisabledTint,
    TransDisabledScale,
    TransDisabledOffsetX,
    TransDisabledOffsetY,

    Count
};

// ============================================================================
// EditorInspector — manages the property inspector panel
// ============================================================================

class EditorInspector : public IPropertyGridEventHandler {
public:
    void initialize(IGuiContext* ctx, CommandHistory* history);
    void shutdown();

    // Select a widget to inspect
    void set_selected_widget(IGuiWidget* widget);
    IGuiWidget* get_selected_widget() const { return m_selected; }

    // Refresh property values from the selected widget
    void refresh();

    // Get the root widget for embedding in editor layout
    IGuiWidget* get_root_widget() const { return m_root; }

    // IPropertyGridEventHandler
    void on_property_changed(int property_id) override;

private:
    // Rebuild property list based on selected widget type (called on selection change)
    void build_properties();
    void populate_values();
    void apply_property_change(int property_id);

    IGuiContext* m_ctx = nullptr;
    CommandHistory* m_history = nullptr;
    IGuiWidget* m_root = nullptr;
    IGuiPropertyGrid* m_property_grid = nullptr;
    IGuiWidget* m_selected = nullptr;

    // Property IDs in the grid; -1 means not present for current widget type
    int m_prop_ids[static_cast<int>(InspectorProperty::Count)] = {};
};

} // namespace editor
} // namespace gui
} // namespace window

#endif // GUI_EDITOR_INSPECTOR_HPP
