/*
 * gui_context.hpp - GUI Context, Theme, Layout, and Factory Functions
 *
 * Contains IGuiContext for cross-window UI management,
 * IGuiTheme for styling, IGuiLayout for layout calculations,
 * and factory functions.
 */

#ifndef WINDOW_GUI_CONTEXT_HPP
#define WINDOW_GUI_CONTEXT_HPP

// Forward declarations for all widget interfaces
namespace window {
namespace gui {

class IGuiLabel;
class IGuiTextInput;
class IGuiEditBox;
class IGuiScrollView;
class IGuiScrollBar;
class IGuiPropertyGrid;
class IGuiTreeView;
class IGuiTabControl;
class IGuiListBox;
class IGuiComboBox;
class IGuiDialog;
class IGuiPopup;
class IGuiMenu;
class IGuiMenuBar;
class IGuiToolbar;
class IGuiStatusBar;
class IGuiSplitPanel;
class IGuiDockPanel;
class IGuiButton;
class IGuiSlider;
class IGuiProgressBar;
class IGuiColorPicker;
class IGuiImage;
class IGuiAnimationManager;
class IGuiPage;
class IGuiPageView;

// Enums from other headers needed for factory methods
enum class ScrollBarOrientation : uint8_t;
enum class TabPosition : uint8_t;
enum class DialogButtons : uint8_t;
enum class ToolbarOrientation : uint8_t;
enum class SplitOrientation : uint8_t;
enum class ButtonType : uint8_t;
enum class SliderOrientation : uint8_t;
enum class ProgressBarMode : uint8_t;
enum class ColorPickerMode : uint8_t;

// ============================================================================
// Context Interface (Abstract) - Cross-Window UI Management
// ============================================================================

class IGuiContext {
public:
    virtual ~IGuiContext() = default;

    // Lifecycle
    virtual GuiResult initialize() = 0;
    virtual void shutdown() = 0;
    virtual bool is_initialized() const = 0;

    // Frame management
    virtual void begin_frame(float delta_time) = 0;
    virtual void end_frame() = 0;

    // Viewport management (for multi-window support)
    virtual GuiResult add_viewport(const Viewport& viewport) = 0;
    virtual GuiResult remove_viewport(int viewport_id) = 0;
    virtual GuiResult update_viewport(const Viewport& viewport) = 0;
    virtual const Viewport* get_viewport(int viewport_id) const = 0;

    // Input (specify which viewport receives input)
    virtual void set_input_state(int viewport_id, const GuiInputState& state) = 0;
    virtual const GuiInputState& get_input_state() const = 0;

    // Root widget
    virtual IGuiWidget* get_root() = 0;

    // Focus management
    virtual IGuiWidget* get_focused_widget() const = 0;
    virtual void set_focused_widget(IGuiWidget* widget) = 0;
    virtual void clear_focus() = 0;

    // Get all widgets that intersect a rect
    virtual void get_widgets_in_box(const math::Box& box, std::vector<IGuiWidget*>& out_widgets) = 0;

    // Text measurement
    virtual void set_text_measurer(ITextMeasurer* measurer) = 0;
    virtual ITextMeasurer* get_text_measurer() const = 0;

    // Style
    virtual const GuiStyle& get_default_style() const = 0;
    virtual void set_default_style(const GuiStyle& style) = 0;
    virtual const LabelStyle& get_default_label_style() const = 0;
    virtual void set_default_label_style(const LabelStyle& style) = 0;

    // Widget creation (factory methods)
    virtual IGuiWidget* create_widget(WidgetType type) = 0;
    virtual IGuiButton* create_button(ButtonType type = ButtonType::Normal) = 0;
    virtual IGuiLabel* create_label(const char* text = nullptr) = 0;
    virtual IGuiTextInput* create_text_input(const char* placeholder = nullptr) = 0;
    virtual IGuiEditBox* create_editbox() = 0;
    virtual IGuiImage* create_image(const std::string& image_name = "") = 0;
    virtual IGuiScrollView* create_scroll_view() = 0;
    virtual IGuiScrollBar* create_scroll_bar(ScrollBarOrientation orientation) = 0;
    virtual IGuiPropertyGrid* create_property_grid() = 0;
    virtual IGuiTreeView* create_tree_view() = 0;
    virtual IGuiTabControl* create_tab_control(TabPosition position) = 0;
    virtual IGuiListBox* create_list_box() = 0;
    virtual IGuiComboBox* create_combo_box() = 0;
    virtual IGuiDialog* create_dialog(const char* title = nullptr, DialogButtons buttons = static_cast<DialogButtons>(1)) = 0;
    virtual IGuiPopup* create_popup() = 0;
    virtual IGuiMenu* create_menu() = 0;
    virtual IGuiMenuBar* create_menu_bar() = 0;
    virtual IGuiToolbar* create_toolbar(ToolbarOrientation orientation) = 0;
    virtual IGuiStatusBar* create_status_bar() = 0;
    virtual IGuiSplitPanel* create_split_panel(SplitOrientation orientation) = 0;
    virtual IGuiDockPanel* create_dock_panel() = 0;
    virtual IGuiSlider* create_slider(SliderOrientation orientation) = 0;
    virtual IGuiProgressBar* create_progress_bar(ProgressBarMode mode) = 0;
    virtual IGuiColorPicker* create_color_picker(ColorPickerMode mode) = 0;
    virtual IGuiPage* create_page(const char* page_id = nullptr) = 0;
    virtual IGuiPageView* create_page_view() = 0;
    virtual void destroy_widget(IGuiWidget* widget) = 0;

    // Modal handling
    virtual void push_modal(IGuiWidget* widget) = 0;
    virtual void pop_modal() = 0;
    virtual IGuiWidget* get_modal() const = 0;

    // Tooltip (creates a temporary label)
    virtual void show_tooltip(const char* text, const math::Vec2& position) = 0;
    virtual void hide_tooltip() = 0;

    // Debug
    virtual void set_debug_draw(bool enabled) = 0;
    virtual bool is_debug_draw_enabled() const = 0;

    // Animation
    virtual IGuiAnimationManager* get_animation_manager() = 0;
};

// ============================================================================
// Theme Interface (Abstract)
// ============================================================================

class IGuiTheme {
public:
    virtual ~IGuiTheme() = default;

    virtual const char* get_name() const = 0;
    virtual GuiStyle get_style_for(WidgetType type, WidgetState state) const = 0;
    virtual LabelStyle get_label_style_for(WidgetType type, WidgetState state) const = 0;
    virtual math::Vec4 get_color(const char* name) const = 0;
    virtual float get_metric(const char* name) const = 0;
};

// ============================================================================
// Layout Interface (Abstract)
// ============================================================================

class IGuiLayout {
public:
    virtual ~IGuiLayout() = default;

    virtual void apply(IGuiWidget* widget) = 0;
    virtual math::Vec2 calculate_size(IGuiWidget* widget) const = 0;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* gui_result_to_string(GuiResult result);
const char* widget_type_to_string(WidgetType type);
const char* widget_state_to_string(WidgetState state);
const char* gui_event_type_to_string(GuiEventType type);

// ============================================================================
// Factory Functions
// ============================================================================

IGuiContext* create_gui_context(GuiResult* out_result = nullptr);
void destroy_gui_context(IGuiContext* context);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_CONTEXT_HPP
