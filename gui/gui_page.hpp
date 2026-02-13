/*
 * gui_page.hpp - Page Navigation Interfaces
 *
 * Contains IGuiPage for individual pages/screens and IGuiPageView
 * for managing page navigation with stack-based or tab-based switching.
 */

#ifndef WINDOW_GUI_PAGE_HPP
#define WINDOW_GUI_PAGE_HPP

namespace window {
namespace gui {

// ============================================================================
// Page Enums
// ============================================================================

enum class PageTransitionType : uint8_t {
    None = 0,           // Instant switch
    Fade,               // Fade out/in
    SlideLeft,          // Slide from right to left
    SlideRight,         // Slide from left to right
    SlideUp,            // Slide from bottom to top
    SlideDown,          // Slide from top to bottom
    Push,               // Push effect (like iOS navigation)
    Pop,                // Pop effect (reverse of push)
    Zoom,               // Zoom in/out
    Flip,               // 3D flip effect
    Custom              // User-defined transition
};

enum class PageState : uint8_t {
    Hidden = 0,         // Not visible
    Entering,           // Transition in progress (becoming visible)
    Active,             // Fully visible and interactive
    Leaving,            // Transition in progress (becoming hidden)
    Paused              // Visible but not interactive (behind modal)
};

// ============================================================================
// Page Transition Configuration
// ============================================================================

struct PageTransition {
    PageTransitionType type = PageTransitionType::SlideLeft;
    float duration = 0.3f;              // Transition duration in seconds
    AnimationEasing easing = AnimationEasing::EaseInOut;

    static PageTransition instant() {
        PageTransition t;
        t.type = PageTransitionType::None;
        t.duration = 0.0f;
        return t;
    }

    static PageTransition fade(float duration = 0.25f) {
        PageTransition t;
        t.type = PageTransitionType::Fade;
        t.duration = duration;
        return t;
    }

    static PageTransition slide_left(float duration = 0.3f) {
        PageTransition t;
        t.type = PageTransitionType::SlideLeft;
        t.duration = duration;
        return t;
    }

    static PageTransition slide_right(float duration = 0.3f) {
        PageTransition t;
        t.type = PageTransitionType::SlideRight;
        t.duration = duration;
        return t;
    }
};

// ============================================================================
// Page Style
// ============================================================================

struct PageStyle {
    math::Vec4 background_color;
    math::Vec4 overlay_color;           // For modal overlay
    float overlay_opacity = 0.5f;
    bool enable_gesture_navigation = true;  // Swipe to go back
    float gesture_threshold = 0.3f;     // Swipe distance ratio to trigger navigation

    static PageStyle default_style() {
        PageStyle s;
        s.background_color = color_rgba8(30, 30, 30);
        s.overlay_color = color_rgba8(0, 0, 0);
        return s;
    }
};

// ============================================================================
// Page Event Handler
// ============================================================================

class IGuiPage;

class IPageEventHandler {
public:
    virtual ~IPageEventHandler() = default;

    // Lifecycle events
    virtual void on_page_created(IGuiPage* page) = 0;
    virtual void on_page_will_appear(IGuiPage* page) = 0;
    virtual void on_page_did_appear(IGuiPage* page) = 0;
    virtual void on_page_will_disappear(IGuiPage* page) = 0;
    virtual void on_page_did_disappear(IGuiPage* page) = 0;
    virtual void on_page_destroyed(IGuiPage* page) = 0;

    // Navigation events
    virtual bool on_page_should_pop(IGuiPage* page) = 0;  // Return false to prevent
};

// ============================================================================
// Page Interface - Single page/screen
// ============================================================================

class IGuiPage : public IGuiWidget {
public:
    virtual ~IGuiPage() = default;

    // Page identification
    virtual const char* get_page_id() const = 0;
    virtual void set_page_id(const char* id) = 0;
    virtual const char* get_page_title() const = 0;
    virtual void set_page_title(const char* title) = 0;

    // Page state
    virtual PageState get_page_state() const = 0;

    // Content widget (the main content of the page)
    virtual IGuiWidget* get_content() const = 0;
    virtual void set_content(IGuiWidget* content) = 0;

    // Header/footer (optional navigation bars)
    virtual IGuiWidget* get_header() const = 0;
    virtual void set_header(IGuiWidget* header) = 0;
    virtual IGuiWidget* get_footer() const = 0;
    virtual void set_footer(IGuiWidget* footer) = 0;

    // Page data (user data associated with page)
    virtual void* get_user_data() const = 0;
    virtual void set_user_data(void* data) = 0;

    // Navigation parameters (data passed when navigating to this page)
    virtual const char* get_parameter(const char* key) const = 0;
    virtual void set_parameter(const char* key, const char* value) = 0;
    virtual void clear_parameters() = 0;

    // Style
    virtual const PageStyle& get_page_style() const = 0;
    virtual void set_page_style(const PageStyle& style) = 0;

    // Transition (preferred transition for this page)
    virtual PageTransition get_enter_transition() const = 0;
    virtual void set_enter_transition(const PageTransition& transition) = 0;
    virtual PageTransition get_exit_transition() const = 0;
    virtual void set_exit_transition(const PageTransition& transition) = 0;

    // Event handler
    virtual void set_page_event_handler(IPageEventHandler* handler) = 0;

    // Modal support
    virtual bool is_modal() const = 0;
    virtual void set_modal(bool modal) = 0;

    // Can go back (for navigation UI hints)
    virtual bool can_go_back() const = 0;
};

// ============================================================================
// Page View Event Handler
// ============================================================================

class IGuiPageView;

class IPageViewEventHandler {
public:
    virtual ~IPageViewEventHandler() = default;

    virtual void on_page_changed(IGuiPageView* view, IGuiPage* old_page, IGuiPage* new_page) = 0;
    virtual void on_navigation_started(IGuiPageView* view, IGuiPage* from, IGuiPage* to) = 0;
    virtual void on_navigation_completed(IGuiPageView* view, IGuiPage* page) = 0;
    virtual void on_navigation_cancelled(IGuiPageView* view) = 0;
};

// ============================================================================
// Page View Render Info
// ============================================================================

struct PageViewRenderInfo {
    const IGuiWidget* widget = nullptr;

    math::Box bounds;
    math::Box clip_rect;

    // Current visible pages (during transition, may have two)
    IGuiPage* current_page = nullptr;
    IGuiPage* transitioning_page = nullptr;  // The page being transitioned to/from

    // Transition state
    bool is_transitioning = false;
    float transition_progress = 0.0f;       // 0.0 - 1.0
    PageTransitionType transition_type = PageTransitionType::None;

    // Page positions during transition
    math::Box current_page_rect;
    math::Box transitioning_page_rect;
    float current_page_opacity = 1.0f;
    float transitioning_page_opacity = 1.0f;

    // Overlay for modal pages
    bool show_modal_overlay = false;
    float modal_overlay_opacity = 0.0f;
};

// ============================================================================
// Page View Interface - Container managing page navigation
// ============================================================================

class IGuiPageView : public IGuiWidget {
public:
    virtual ~IGuiPageView() = default;

    // Page management
    virtual int get_page_count() const = 0;
    virtual IGuiPage* get_page(int index) const = 0;
    virtual IGuiPage* get_page_by_id(const char* page_id) const = 0;
    virtual int get_page_index(IGuiPage* page) const = 0;

    // Current page
    virtual IGuiPage* get_current_page() const = 0;
    virtual int get_current_index() const = 0;

    // Stack-based navigation (like iOS UINavigationController)
    virtual void push_page(IGuiPage* page, const PageTransition& transition = PageTransition()) = 0;
    virtual IGuiPage* pop_page(const PageTransition& transition = PageTransition()) = 0;
    virtual void pop_to_page(IGuiPage* page, const PageTransition& transition = PageTransition()) = 0;
    virtual void pop_to_root(const PageTransition& transition = PageTransition()) = 0;
    virtual int get_stack_depth() const = 0;
    virtual bool can_pop() const = 0;

    // Direct navigation (replace current)
    virtual void set_page(IGuiPage* page, const PageTransition& transition = PageTransition()) = 0;
    virtual void set_page_at_index(int index, const PageTransition& transition = PageTransition()) = 0;

    // Page registration (for non-stack navigation)
    virtual void add_page(IGuiPage* page) = 0;
    virtual void remove_page(IGuiPage* page) = 0;
    virtual void remove_page_by_id(const char* page_id) = 0;
    virtual void clear_pages() = 0;

    // Modal presentation
    virtual void present_modal(IGuiPage* page, const PageTransition& transition = PageTransition::fade()) = 0;
    virtual void dismiss_modal(const PageTransition& transition = PageTransition::fade()) = 0;
    virtual IGuiPage* get_presented_modal() const = 0;
    virtual bool has_modal() const = 0;

    // Transition state
    virtual bool is_transitioning() const = 0;
    virtual float get_transition_progress() const = 0;
    virtual void cancel_transition() = 0;

    // Default transitions
    virtual PageTransition get_default_push_transition() const = 0;
    virtual void set_default_push_transition(const PageTransition& transition) = 0;
    virtual PageTransition get_default_pop_transition() const = 0;
    virtual void set_default_pop_transition(const PageTransition& transition) = 0;

    // Gesture navigation
    virtual bool is_gesture_navigation_enabled() const = 0;
    virtual void set_gesture_navigation_enabled(bool enabled) = 0;

    // History (for browser-like back/forward)
    virtual bool can_go_back() const = 0;
    virtual bool can_go_forward() const = 0;
    virtual void go_back() = 0;
    virtual void go_forward() = 0;
    virtual void clear_history() = 0;

    // Event handler
    virtual void set_page_view_event_handler(IPageViewEventHandler* handler) = 0;

    // Render info
    virtual void get_page_view_render_info(PageViewRenderInfo* out_info) const = 0;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* page_transition_type_to_string(PageTransitionType type);
const char* page_state_to_string(PageState state);

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_PAGE_HPP
