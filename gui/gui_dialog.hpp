/*
 * gui_dialog.hpp - Dialog and Popup Interfaces
 *
 * Contains IGuiDialog for modal dialogs and IGuiPopup for non-modal overlays.
 */

#ifndef WINDOW_GUI_DIALOG_HPP
#define WINDOW_GUI_DIALOG_HPP

namespace window {
namespace gui {

// ============================================================================
// Dialog/Popup Interface - Modal and non-modal overlays
// ============================================================================

enum class DialogResult : uint8_t {
    None = 0,
    OK,
    Cancel,
    Yes,
    No,
    Retry,
    Abort,
    Custom
};

enum class DialogButtons : uint8_t {
    None = 0,
    OK,
    OKCancel,
    YesNo,
    YesNoCancel,
    RetryCancel,
    AbortRetryIgnore,
    Custom
};

enum class PopupPlacement : uint8_t {
    Center = 0,         // Center of parent / screen
    AtCursor,           // At current mouse position
    Below,              // Below anchor widget
    Above,              // Above anchor widget
    Right,              // Right of anchor widget
    Left,               // Left of anchor widget
    Manual              // Use explicit position
};

struct DialogStyle {
    math::Vec4 overlay_color;           // Dimmed background behind modal
    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 title_bar_color;
    math::Vec4 title_text_color;
    math::Vec4 shadow_color;
    float border_width = 1.0f;
    float corner_radius = 6.0f;
    float title_bar_height = 32.0f;
    float button_area_height = 44.0f;
    float padding = 16.0f;
    float shadow_offset = 4.0f;
    float shadow_blur = 8.0f;
    float min_width = 300.0f;
    float min_height = 150.0f;
    float font_size = 13.0f;
    float title_font_size = 14.0f;

    static DialogStyle default_style() {
        DialogStyle s;
        s.overlay_color = color_rgba8(0, 0, 0, 128);
        s.background_color = color_rgba8(45, 45, 48);
        s.border_color = color_rgba8(63, 63, 70);
        s.title_bar_color = color_rgba8(37, 37, 38);
        s.title_text_color = color_rgba8(241, 241, 241);
        s.shadow_color = color_rgba8(0, 0, 0, 100);
        return s;
    }
};

struct DialogRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;
    math::Box overlay_rect;         // Full-screen dim overlay (modal only)
    math::Box title_bar_rect;
    math::Box content_rect;
    math::Box button_area_rect;
    math::Box close_button_rect;

    DialogStyle style;
    const char* title = nullptr;
    bool is_modal = false;
    bool is_draggable = false;
    bool is_resizable = false;
    bool show_close_button = true;
    bool close_button_hovered = false;
};

struct PopupRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    math::Vec4 background_color;
    math::Vec4 border_color;
    math::Vec4 shadow_color;
    float border_width = 1.0f;
    float corner_radius = 4.0f;
    float shadow_offset = 2.0f;
    float shadow_blur = 6.0f;
    bool is_open = false;
};

class IDialogEventHandler {
public:
    virtual ~IDialogEventHandler() = default;
    virtual void on_dialog_closed(DialogResult result) = 0;
    virtual void on_dialog_button_clicked(DialogResult button) = 0;
};

class IGuiDialog : public IGuiWidget {
public:
    virtual ~IGuiDialog() = default;

    // Title
    virtual const char* get_title() const = 0;
    virtual void set_title(const char* title) = 0;

    // Modal
    virtual bool is_modal() const = 0;
    virtual void set_modal(bool modal) = 0;

    // Show/hide
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool is_open() const = 0;

    // Result
    virtual DialogResult get_result() const = 0;

    // Buttons
    virtual void set_buttons(DialogButtons buttons) = 0;
    virtual DialogButtons get_buttons() const = 0;
    virtual void set_custom_button(int index, const char* text, DialogResult result) = 0;
    virtual int get_button_count() const = 0;

    // Content widget
    virtual IGuiWidget* get_content() const = 0;
    virtual void set_content(IGuiWidget* content) = 0;

    // Behavior
    virtual bool is_draggable() const = 0;
    virtual void set_draggable(bool draggable) = 0;
    virtual bool is_resizable() const = 0;
    virtual void set_resizable(bool resizable) = 0;
    virtual bool has_close_button() const = 0;
    virtual void set_close_button(bool show) = 0;
    virtual bool is_close_on_overlay_click() const = 0;
    virtual void set_close_on_overlay_click(bool enabled) = 0;

    // Style
    virtual const DialogStyle& get_dialog_style() const = 0;
    virtual void set_dialog_style(const DialogStyle& style) = 0;

    // Event handler
    virtual void set_dialog_event_handler(IDialogEventHandler* handler) = 0;

    // Render info
    virtual void get_dialog_render_info(DialogRenderInfo* out_info) const = 0;
};

class IPopupEventHandler {
public:
    virtual ~IPopupEventHandler() = default;
    virtual void on_popup_opened() = 0;
    virtual void on_popup_closed() = 0;
};

class IGuiPopup : public IGuiWidget {
public:
    virtual ~IGuiPopup() = default;

    // Show/hide
    virtual void show(PopupPlacement placement = PopupPlacement::AtCursor) = 0;
    virtual void show_at(const math::Vec2& position) = 0;
    virtual void show_relative_to(const IGuiWidget* anchor, PopupPlacement placement) = 0;
    virtual void hide() = 0;
    virtual bool is_open() const = 0;

    // Content widget
    virtual IGuiWidget* get_content() const = 0;
    virtual void set_content(IGuiWidget* content) = 0;

    // Auto-close behavior
    virtual bool is_close_on_click_outside() const = 0;
    virtual void set_close_on_click_outside(bool enabled) = 0;
    virtual bool is_close_on_escape() const = 0;
    virtual void set_close_on_escape(bool enabled) = 0;

    // Style
    virtual math::Vec4 get_background_color() const = 0;
    virtual void set_background_color(const math::Vec4& color) = 0;
    virtual math::Vec4 get_border_color() const = 0;
    virtual void set_border_color(const math::Vec4& color) = 0;
    virtual float get_corner_radius() const = 0;
    virtual void set_corner_radius(float radius) = 0;

    // Event handler
    virtual void set_popup_event_handler(IPopupEventHandler* handler) = 0;

    // Render info
    virtual void get_popup_render_info(PopupRenderInfo* out_info) const = 0;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_DIALOG_HPP
