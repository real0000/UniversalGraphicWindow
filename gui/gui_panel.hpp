/*
 * gui_panel.hpp - SplitPanel and DockPanel Interfaces
 *
 * Contains IGuiSplitPanel for resizable splits and IGuiDockPanel for dockable layouts.
 */

#ifndef WINDOW_GUI_PANEL_HPP
#define WINDOW_GUI_PANEL_HPP

namespace window {
namespace gui {

// ============================================================================
// SplitPanel Interface - Resizable split container
// ============================================================================

enum class SplitOrientation : uint8_t {
    Horizontal = 0,     // Left | Right
    Vertical            // Top / Bottom
};

enum class SplitSizeUnit : uint8_t {
    Pixels = 0,
    Ratio               // 0.0 - 1.0 of total size
};

struct SplitterStyle {
    math::Vec4 splitter_color;
    math::Vec4 splitter_hover_color;
    math::Vec4 splitter_drag_color;
    math::Vec4 grip_color;
    float splitter_thickness = 4.0f;
    float hit_area_thickness = 8.0f;    // Larger than visual for easier grabbing
    float grip_length = 30.0f;
    float grip_dot_size = 2.0f;
    int grip_dot_count = 3;

    static SplitterStyle default_style() {
        SplitterStyle s;
        s.splitter_color = color_rgba8(45, 45, 48);
        s.splitter_hover_color = color_rgba8(0, 122, 204);
        s.splitter_drag_color = color_rgba8(0, 122, 204);
        s.grip_color = color_rgba8(110, 110, 110);
        return s;
    }
};

struct SplitPanelRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box first_panel_rect;
    math::Box second_panel_rect;
    math::Box splitter_rect;

    SplitterStyle style;
    SplitOrientation orientation = SplitOrientation::Horizontal;
    bool splitter_hovered = false;
    bool splitter_dragging = false;
    float split_position = 0.0f;        // Current position in pixels
    float split_ratio = 0.5f;           // Current ratio 0.0-1.0
};

class ISplitPanelEventHandler {
public:
    virtual ~ISplitPanelEventHandler() = default;
    virtual void on_split_changed(float position, float ratio) = 0;
    virtual void on_split_drag_started() = 0;
    virtual void on_split_drag_ended() = 0;
};

class IGuiSplitPanel : public IGuiWidget {
public:
    virtual ~IGuiSplitPanel() = default;

    // Orientation
    virtual SplitOrientation get_orientation() const = 0;
    virtual void set_orientation(SplitOrientation orientation) = 0;

    // Panel content
    virtual IGuiWidget* get_first_panel() const = 0;
    virtual void set_first_panel(IGuiWidget* widget) = 0;
    virtual IGuiWidget* get_second_panel() const = 0;
    virtual void set_second_panel(IGuiWidget* widget) = 0;

    // Split position
    virtual float get_split_position() const = 0;
    virtual void set_split_position(float position) = 0;
    virtual float get_split_ratio() const = 0;
    virtual void set_split_ratio(float ratio) = 0;
    virtual SplitSizeUnit get_split_unit() const = 0;
    virtual void set_split_unit(SplitSizeUnit unit) = 0;

    // Constraints
    virtual float get_first_min_size() const = 0;
    virtual void set_first_min_size(float size) = 0;
    virtual float get_first_max_size() const = 0;
    virtual void set_first_max_size(float size) = 0;
    virtual float get_second_min_size() const = 0;
    virtual void set_second_min_size(float size) = 0;
    virtual float get_second_max_size() const = 0;
    virtual void set_second_max_size(float size) = 0;

    // Collapse
    virtual bool is_first_collapsed() const = 0;
    virtual void set_first_collapsed(bool collapsed) = 0;
    virtual bool is_second_collapsed() const = 0;
    virtual void set_second_collapsed(bool collapsed) = 0;
    virtual bool is_collapsible() const = 0;
    virtual void set_collapsible(bool collapsible) = 0;

    // Splitter interaction
    virtual bool is_splitter_fixed() const = 0;
    virtual void set_splitter_fixed(bool fixed) = 0;
    virtual bool is_splitter_hovered() const = 0;
    virtual bool is_splitter_dragging() const = 0;

    // Style
    virtual const SplitterStyle& get_splitter_style() const = 0;
    virtual void set_splitter_style(const SplitterStyle& style) = 0;

    // Event handler
    virtual void set_split_event_handler(ISplitPanelEventHandler* handler) = 0;

    // Render info
    virtual void get_split_panel_render_info(SplitPanelRenderInfo* out_info) const = 0;
};

// ============================================================================
// DockPanel Interface - Dockable panel layout system
// ============================================================================

enum class DockZone : uint8_t {
    Center = 0,
    Left,
    Right,
    Top,
    Bottom,
    Float           // Detached floating window
};

enum class DockPanelState : uint8_t {
    Docked = 0,
    Floating,
    AutoHide,       // Collapsed to edge, slides out on hover
    Hidden
};

struct DockPanelStyle {
    math::Vec4 background_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_bar_active_color;
    math::Vec4 title_text_color;
    math::Vec4 title_active_text_color;
    math::Vec4 tab_bar_color;
    math::Vec4 drop_indicator_color;
    math::Vec4 auto_hide_tab_color;
    float title_bar_height = 26.0f;
    float tab_height = 24.0f;
    float auto_hide_tab_width = 24.0f;
    float min_dock_width = 100.0f;
    float min_dock_height = 80.0f;
    float drop_indicator_thickness = 3.0f;
    float font_size = 12.0f;

    static DockPanelStyle default_style() {
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
};

struct DockPanelRenderInfo {
    int panel_id = -1;
    const char* title = nullptr;
    const char* icon_name = nullptr;
    DockPanelState state = DockPanelState::Docked;
    DockZone zone = DockZone::Center;
    bool active = false;
    bool title_hovered = false;
    math::Box panel_rect;
    math::Box title_bar_rect;
    math::Box content_rect;
    math::Box close_button_rect;
};

struct DockDropIndicatorInfo {
    bool visible = false;
    DockZone target_zone = DockZone::Center;
    math::Box indicator_rect;
    math::Box preview_rect;         // Preview of where panel would dock
};

struct DockLayoutRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box center_rect;         // Center content area after docking

    DockPanelStyle style;
    int docked_panel_count = 0;
    int floating_panel_count = 0;
    int auto_hide_panel_count = 0;

    DockDropIndicatorInfo drop_indicator;
};

class IDockPanelEventHandler {
public:
    virtual ~IDockPanelEventHandler() = default;
    virtual void on_panel_docked(int panel_id, DockZone zone) = 0;
    virtual void on_panel_undocked(int panel_id) = 0;
    virtual void on_panel_floated(int panel_id) = 0;
    virtual void on_panel_closed(int panel_id) = 0;
    virtual void on_panel_activated(int panel_id) = 0;
    virtual void on_layout_changed() = 0;
};

class IGuiDockPanel : public IGuiWidget {
public:
    virtual ~IGuiDockPanel() = default;

    // Panel management
    virtual int add_panel(const char* title, IGuiWidget* content, const char* icon_name = nullptr) = 0;
    virtual bool remove_panel(int panel_id) = 0;
    virtual void clear_panels() = 0;
    virtual int get_panel_count() const = 0;

    // Panel info
    virtual const char* get_panel_title(int panel_id) const = 0;
    virtual void set_panel_title(int panel_id, const char* title) = 0;
    virtual const char* get_panel_icon(int panel_id) const = 0;
    virtual void set_panel_icon(int panel_id, const char* icon_name) = 0;
    virtual IGuiWidget* get_panel_content(int panel_id) const = 0;

    // Docking
    virtual DockZone get_panel_zone(int panel_id) const = 0;
    virtual void dock_panel(int panel_id, DockZone zone) = 0;
    virtual void dock_panel_relative(int panel_id, int target_panel_id, DockZone zone) = 0;
    virtual void dock_panel_as_tab(int panel_id, int target_panel_id) = 0;
    virtual void undock_panel(int panel_id) = 0;

    // Panel state
    virtual DockPanelState get_panel_state(int panel_id) const = 0;
    virtual void set_panel_state(int panel_id, DockPanelState state) = 0;

    // Floating
    virtual void float_panel(int panel_id, const math::Box& bounds) = 0;
    virtual math::Box get_floating_bounds(int panel_id) const = 0;
    virtual void set_floating_bounds(int panel_id, const math::Box& bounds) = 0;

    // Auto-hide
    virtual void auto_hide_panel(int panel_id) = 0;
    virtual bool is_auto_hide_expanded(int panel_id) const = 0;
    virtual void expand_auto_hide(int panel_id) = 0;
    virtual void collapse_auto_hide(int panel_id) = 0;

    // Active panel
    virtual int get_active_panel() const = 0;
    virtual void set_active_panel(int panel_id) = 0;

    // Panel visibility
    virtual bool is_panel_visible(int panel_id) const = 0;
    virtual void set_panel_visible(int panel_id, bool visible) = 0;
    virtual bool is_panel_closable(int panel_id) const = 0;
    virtual void set_panel_closable(int panel_id, bool closable) = 0;

    // Zone sizes
    virtual float get_zone_size(DockZone zone) const = 0;
    virtual void set_zone_size(DockZone zone, float size) = 0;

    // Drag and drop docking
    virtual bool is_drag_docking_enabled() const = 0;
    virtual void set_drag_docking_enabled(bool enabled) = 0;

    // Layout save/restore
    virtual std::string save_layout() const = 0;
    virtual bool load_layout(const char* layout_data) = 0;

    // Center content (the area not occupied by docked panels)
    virtual IGuiWidget* get_center_content() const = 0;
    virtual void set_center_content(IGuiWidget* widget) = 0;

    // Panel user data
    virtual void set_panel_user_data(int panel_id, void* data) = 0;
    virtual void* get_panel_user_data(int panel_id) const = 0;

    // Style
    virtual const DockPanelStyle& get_dock_panel_style() const = 0;
    virtual void set_dock_panel_style(const DockPanelStyle& style) = 0;

    // Event handler
    virtual void set_dock_event_handler(IDockPanelEventHandler* handler) = 0;

    // Render info
    virtual void get_dock_layout_render_info(DockLayoutRenderInfo* out_info) const = 0;
    virtual int get_visible_dock_panels(DockPanelRenderInfo* out_items, int max_items) const = 0;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* split_orientation_to_string(SplitOrientation orientation);
const char* dock_zone_to_string(DockZone zone);
const char* dock_panel_state_to_string(DockPanelState state);

// ============================================================================
// Factory Functions
// ============================================================================

IGuiSplitPanel* create_split_panel(SplitOrientation orientation);
IGuiDockPanel* create_dock_panel();

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_PANEL_HPP
