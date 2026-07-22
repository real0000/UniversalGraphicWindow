/*
 * gui_context.hpp - GUI Context, Theme, Layout, and Factory Functions
 *
 * Contains IGuiContext for cross-window UI management,
 * IGuiTheme for styling, IGuiLayout for layout calculations,
 * and factory functions.
 */

#ifndef WINDOW_GUI_CONTEXT_HPP
#define WINDOW_GUI_CONTEXT_HPP

#include <functional>   // IGuiContext::post

// Forward declarations
namespace window {
class Window;
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
class IGuiCanvasView;
class IGuiCollapseSection;
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
    // Current key-modifier bitmask, fed to widgets that need it (e.g. the canvas's
    // modifier provider). attach_window sets this from platform events; an app that owns
    // its own input routing (set_host_window) sets it before each dispatch_* call.
    virtual void set_current_modifiers(int mods) = 0;

    // Dispatch scroll to the topmost widget under mouse_pos (depth-first hit-test).
    // Called automatically by begin_frame() when input_state has non-zero scroll delta.
    virtual bool dispatch_scroll(float dx, float dy, const math::Vec2& mouse_pos) = 0;

    // Dispatch mouse move / button / scroll to the widget tree (overlays, then root).
    // On left-press also updates focused widget. Can be called manually for injected events.
    virtual void dispatch_mouse_move(const math::Vec2& pos) = 0;
    virtual bool dispatch_mouse_button(MouseButton button, bool pressed, const math::Vec2& pos) = 0;

    // Attach to a Window: registers built-in mouse and keyboard handlers automatically so
    // all platform events are routed through the widget tree without manual dispatcher setup.
    // Automatically unregisters on detach_window() or context shutdown.
    virtual void attach_window(Window* win) = 0;
    virtual void detach_window(Window* win) = 0;

    //-------------------------------------------------------------------------
    // Event-driven host binding (retained GUI, no per-frame loop)
    //-------------------------------------------------------------------------
    // Bind the window whose main loop drives this context, WITHOUT taking over
    // input (unlike attach_window). Enables three things that make the GUI update
    // itself the way a webview does — the app only mutates widgets:
    //   • post(): cross-thread UI marshalling (the "postMessage" of this GUI).
    //   • automatic re-layout + repaint when any widget marks dirty (the root's
    //     bounds also auto-follow the window size).
    //   • a caret-blink timer while a text widget holds focus.
    // Use this when the app manages its own input routing; use attach_window when
    // you want the context to own input too.
    virtual void set_host_window(Window* win) = 0;

    // Global UI scale (DPI). The whole widget tree is declared in LOGICAL units;
    // the context lays the root out in logical space and scales every collected
    // draw command — positions, sizes, corner radii, stroke widths AND font
    // sizes (so glyphs rasterize at the physical pixel size, staying crisp) — by
    // this factor at render time. Consumers therefore never multiply by DPI: set
    // this once (typically to the host window's DPI scale) and author everything
    // in logical pixels. Input arriving in physical pixels must be divided by it
    // before hit-testing (the context does this on its own input path; apps that
    // own input divide themselves). Default 1.0 (logical == physical). The
    // immediate/overlay layers and the vector underlay are scaled to match by
    // GpuGuiRenderer::render_window_frame, which reads this value.
    virtual float get_ui_scale() const = 0;
    virtual void set_ui_scale(float scale) = 0;

    // Convert between physical (window/event) px and logical (widget) px using
    // the UI scale, so a consumer that owns its own input never does DPI
    // arithmetic: divide raw event coords through to_logical() before hit-testing
    // the logical widget tree. (Input dispatched via dispatch_* is already
    // converted on the context's own input path.)
    virtual math::Vec2 to_logical(const math::Vec2& physical) const = 0;
    virtual math::Vec2 to_physical(const math::Vec2& logical) const = 0;

    // Semantic colour theme (GuiColor role → colour). Draw commands a widget
    // tags with a role carry no colour value; the context fills the colour from
    // this palette when it collects the frame, so retheming is a palette swap
    // with zero widget changes (see gui_theme.hpp). Default: GuiTheme::dark().
    // Installing a new theme marks the tree dirty so the next frame recolours.
    virtual const GuiTheme& get_theme() const = 0;
    virtual void set_theme(const GuiTheme& theme) = 0;

    // Queue a widget mutation to run on the UI (loop) thread, then wake the loop.
    // Thread-safe — call from worker threads (agent/network) to update the UI. The
    // task runs, the affected sizers re-flow, and only the changed region repaints;
    // no invalidate()/render() call from the app. This is the ONE cross-thread UI
    // update primitive (mirrors a webview's postMessage → onmessage handler).
    virtual void post(std::function<void()> fn) = 0;

    // Drain queued post() tasks and fire due timers on the calling thread. Used by
    // a synchronous one-shot render (tests/host) so a post()'d change is applied
    // before the next get_render_info(). The live loop calls this itself.
    virtual void pump() = 0;

    // The built-in text-edit context menu (right-click on a text widget) is open.
    // Semantic query for tests/automation.
    virtual bool text_menu_open() const = 0;

    // Measure hook: run inside get_render_info() AFTER the root has been laid out
    // (so widget bounds/widths are current) but BEFORE render commands are
    // collected. This is where content whose size depends on the laid-out width
    // is sized — e.g. a scroll view's content height from height-for-width text.
    // Return true if the hook changed any widget's size, so the context re-flows
    // once more before collecting. Called only on a real render (event-driven),
    // and expected to self-gate (return false when nothing needs rebuilding).
    virtual void set_pre_render(std::function<bool()> hook) = 0;

    // Overlay widgets: rendered after the root tree (on top), in registration order.
    // Suitable for standalone widgets, dropdowns, dialogs, and context menus.
    // get_render_info() skips overlays that are invisible or have empty bounds.
    virtual void add_overlay(IGuiWidget* widget) = 0;
    virtual void remove_overlay(IGuiWidget* widget) = 0;

    // Collect render commands from the entire widget tree (root + overlays) into a
    // single merged WidgetRenderInfo, sorted by depth. Call once per frame.
    virtual const WidgetRenderInfo& get_render_info() = 0;

    // Root widget
    virtual IGuiWidget* get_root() = 0;

    // Focus management
    virtual IGuiWidget* get_focused_widget() const = 0;
    virtual void set_focused_widget(IGuiWidget* widget) = 0;
    virtual void clear_focus() = 0;
    // Autofocus: the widget that takes focus whenever nothing else holds it (and it
    // is visible + enabled), checked once per render. Lets a form declare "typing
    // goes here by default" instead of the app watching focus to put it back.
    virtual void set_default_focus(IGuiWidget* widget) = 0;

    // Get all widgets that intersect a rect
    virtual void get_widgets_in_box(const math::Box& box, std::vector<IGuiWidget*>& out_widgets) = 0;

    // Text measurement
    virtual void set_text_measurer(ITextMeasurer* measurer) = 0;
    virtual ITextMeasurer* get_text_measurer() const = 0;

    // Text rasterizer — when set, get_render_info() calls flatten() to expand
    // Slice9 and Text commands into Color + Texture primitives.
    virtual void set_text_rasterizer(IGuiTextRasterizer* rasterizer) = 0;
    virtual IGuiTextRasterizer* get_text_rasterizer() const = 0;

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
    virtual IGuiCanvasView* create_canvas_view() = 0;
    virtual IGuiCollapseSection* create_collapse_section() = 0;
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
