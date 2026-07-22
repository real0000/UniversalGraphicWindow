/*
 * gui_widget_base.hpp - Internal base classes for GUI widget implementations
 *
 * Contains GuiWidget (concrete IGuiWidget), WidgetBase<> template, and WidgetItem.
 * This is an internal header - not part of the public API. Non-trivial GuiWidget
 * method bodies live in gui_widget_base.cpp; only one-line accessors stay inline
 * (WidgetBase<> is a template and stays fully in this header).
 */

#ifndef WINDOW_GUI_WIDGET_BASE_HPP
#define WINDOW_GUI_WIDGET_BASE_HPP

#include "gui.hpp"
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace window {
namespace gui {

// ============================================================================
// Invalidation — internal on purpose
// ============================================================================
// Whether the picture changed is a widget's OWN conclusion, so this is not on
// IGuiWidget: application code cannot assert it. A widget invalidates itself
// from its own mutators and the flag propagates up this interface to the root,
// where the context turns it into a scheduled repaint. Every UGW widget
// implements it (GuiWidget, WidgetBase<>, and the panels that implement
// IGuiWidget directly), so invalidator_of() resolves for any of them.
//
// App code says what happened instead: mutate through the widget's API, or call
// IGuiWidget::refresh_bindings() when the change is in data the widget reads.
class IWidgetInternal {
public:
    virtual ~IWidgetInternal() = default;
    virtual void mark_dirty() = 0;
    // Re-read every bound provider. The context calls this on the whole tree before
    // it lays out, and the widget's own semantic APIs call it before acting on their
    // model — so an application never has to ask for it either. Implemented once by
    // GuiWidget/WidgetBase<> (universal bind_* properties + refresh_providers()).
    virtual void refresh_bindings() {}
    // Sink for IGuiWidget::bind_text: the widget's own set_text(). Default ignores
    // it, so binding text to a widget that shows none is harmless.
    virtual void apply_bound_text(const char* text) { (void)text; }
};

// A widget's internal channel (null if it is not a UGW widget).
inline IWidgetInternal* internal_of(IGuiWidget* w) {
    return dynamic_cast<IWidgetInternal*>(w);
}

// ============================================================================
// WidgetBindings - storage + evaluation for the universal IGuiWidget::bind_*
// ============================================================================
// One implementation for every widget: GuiWidget owns an instance, WidgetBase<>
// forwards to the one inside its base_, and the panels that implement IGuiWidget
// directly own their own.
struct WidgetBindings {
    std::function<bool()> visible, enabled;
    std::function<std::string()> text;
    std::string text_cache;

    // Pull the bound values onto `w`, routing text through `sink`. Setters are
    // no-change-guarded, so re-reading an unchanged model marks nothing dirty.
    void apply(IGuiWidget* w, IWidgetInternal* sink) {
        if (visible) w->set_visible(visible());
        if (enabled) w->set_enabled(enabled());
        // Focus wins over the model: a bound field is seeded when nobody is
        // editing it, and left alone the moment someone is.
        if (text && !w->has_focus()) {
            std::string t = text();
            if (t != text_cache) { text_cache = std::move(t); sink->apply_bound_text(text_cache.c_str()); }
        }
    }
};

// ============================================================================
// GuiWidget - Concrete base implementing all IGuiWidget methods
// ============================================================================

class GuiWidget : public IGuiWidget, public IWidgetInternal {
public:
    explicit GuiWidget(WidgetType type) : type_(type) {}
    ~GuiWidget() override = default;

    WidgetType get_type() const override { return type_; }
    const char* get_name() const override { return name_.c_str(); }
    void set_name(const char* n) override { name_ = n ? n : ""; }
    IGuiWidget* find_by_name(const char* n) override;
    void find_all_by_name(const char* n, std::vector<IGuiWidget*>& out) override;
    IGuiWidget* get_parent() const override { return parent_; }
    void set_parent(IGuiWidget* p) override { parent_ = p; parent_inv_ = internal_of(p); }
    math::Box get_bounds() const override { return bounds_; }
    void set_bounds(const math::Box& b) override;
    // Content transform (see IGuiWidget docs): children's space → this space.
    float content_scale() const override { return content_scale_; }
    math::Vec2 content_offset() const override { return content_offset_; }
    void set_content_transform(float scale, const math::Vec2& offset) override;
    float content_text_min_px() const override { return content_text_min_px_; }
    void set_content_text_min_px(float px) override { content_text_min_px_ = px; }
    bool has_content_transform() const {
        return content_scale_ != 1.0f ||
               math::x(content_offset_) != 0.0f || math::y(content_offset_) != 0.0f;
    }
    // Parent-space position → children's coordinate space.
    math::Vec2 to_content(const math::Vec2& p) const;
    // Sizer-driven container hooks (see IGuiWidget docs).
    void set_sizer(ISizer* s) override;
    ISizer* get_sizer() const override { return sizer_; }
    math::Vec2 get_preferred_size() const override;
    // Pin the preferred size (a container in a sizer cell reports this as its
    // fixed extent). Marks dirty so the host re-flows the owning sizer; the
    // no-change guard keeps per-event rebinds silent.
    void set_preferred_size(const math::Vec2& s) override;
    math::Vec2 get_min_size() const override { return min_size_; }
    math::Vec2 get_max_size() const override { return max_size_; }
    void set_min_size(const math::Vec2& s) override { min_size_ = s; }
    void set_max_size(const math::Vec2& s) override { max_size_ = s; }
    bool is_clip_enabled() const override { return clip_enabled_; }
    void set_clip_enabled(bool e) override { clip_enabled_ = e; }
    math::Box get_clip_rect() const override { return clip_rect_; }
    void set_clip_rect(const math::Box& r) override { clip_rect_ = r; }
    bool is_visible() const override { return visible_; }
    // A visibility change alters both what renders AND the sizer layout (a hidden
    // sizer item yields its space), so it must invalidate — otherwise a shown/hidden
    // widget keeps its stale bounds until some other change forces a re-layout.
    void set_visible(bool v) override { if (visible_ != v) { visible_ = v; mark_dirty(); } }
    bool is_enabled() const override { return enabled_; }
    // Enabled feeds the drawn look (disabled_color) as much as the input path, so
    // it invalidates on change like visibility does.
    void set_enabled(bool e) override { if (enabled_ != e) { enabled_ = e; mark_dirty(); } }
    WidgetState get_state() const override { return state_; }
    void bind_visible(std::function<bool()> p) override { binds_.visible = std::move(p); mark_dirty(); }
    void bind_enabled(std::function<bool()> p) override { binds_.enabled = std::move(p); mark_dirty(); }
    void bind_text(std::function<std::string()> p) override { binds_.text = std::move(p); mark_dirty(); }
    // Universal bindings first, then whatever model this widget binds.
    void refresh_bindings() override { binds_.apply(this, this); refresh_providers(); }
    const GuiStyle& get_style() const override { return style_; }
    // Dirty-on-change: a re-bound style (e.g. a list row's selection background)
    // must invalidate the cached render info — without this the new look only
    // appears when something else (scroll/resize) happens to dirty the widget.
    static bool style_eq(const GuiStyle& a, const GuiStyle& b);
    void set_style(const GuiStyle& s) override;
    SizeMode get_width_mode() const override { return width_mode_; }
    SizeMode get_height_mode() const override { return height_mode_; }
    void set_size_mode(SizeMode w, SizeMode h) override { width_mode_ = w; height_mode_ = h; }
    Alignment get_alignment() const override { return alignment_; }
    void set_alignment(Alignment a) override { alignment_ = a; }
    void set_event_handler(IGuiEventHandler* h) override { event_handler_ = h; }
    void update(float dt) override { for (auto* c : children_) c->update(dt); }
    const WidgetRenderInfo& get_render_info(Window*) const override;
    void mark_dirty() override;
    bool is_dirty() const override { return dirty_; }
    // Install a callback fired on every mark_dirty(). The context sets this on its
    // root widget to drive automatic re-layout/repaint.
    void set_dirty_listener(std::function<void()> l) { dirty_listener_ = std::move(l); }
    // Allow concrete subclasses to reset dirty after rebuilding their own render_info_
    void clear_dirty() const { dirty_ = false; }
    bool handle_mouse_move(const math::Vec2& pos) override;
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& pos) override;
    bool handle_mouse_scroll(float dx, float dy) override;
    bool handle_key(int code, bool pressed, int mods) override;
    bool handle_text_input(const char* text) override;
    bool is_focusable() const override { return focusable_; }
    bool has_focus() const override { return focused_; }
    void set_focus(bool f) override { focused_ = f; if (f) state_ = WidgetState::Focused; }
    bool hit_test(const math::Vec2& pos) const override { return math::box_contains(bounds_, pos); }
    IGuiWidget* find_widget_at(const math::Vec2& pos) override;
    int get_child_count() const override { return (int)children_.size(); }
    IGuiWidget* get_child(int i) const override { return (i >= 0 && i < (int)children_.size()) ? children_[i] : nullptr; }
    bool add_child(IGuiWidget* c) override { if (!c) return false; children_.push_back(c); c->set_parent(this); return true; }
    bool insert_child_before(IGuiWidget* c, IGuiWidget* before) override;
    bool remove_child(IGuiWidget* c) override;
    bool remove_child_at(int i) override;
    void clear_children() override { children_.clear(); }
    LayoutDirection get_layout_direction() const override { return layout_dir_; }
    void set_layout_direction(LayoutDirection d) override { layout_dir_ = d; }
    float get_spacing() const override { return spacing_; }
    void set_spacing(float s) override { spacing_ = s; }
    void layout_children() override {}

    // Direct access for WidgetBase<>, which forwards its own bind_* here.
    WidgetBindings& bindings() { return binds_; }

protected:
    // Re-read this widget's own data provider (items / form / nodes / …).
    // Subclasses override THIS, not refresh_bindings, so the universal bindings
    // above can never be skipped by forgetting to chain to the base.
    virtual void refresh_providers() {}

    WidgetType type_;
    std::string name_;
    IGuiWidget* parent_ = nullptr;
    IWidgetInternal* parent_inv_ = nullptr;   // resolved once in set_parent
    // Boost.Geometry points are NOT default-initialized, so an unset box is
    // garbage (~1e23). Zero it: a never-laid-out widget then reads as empty
    // (skipped by render/sizer measure) instead of a wild rectangle.
    math::Box bounds_ = math::make_box(0.0f, 0.0f, 0.0f, 0.0f);
    math::Vec2 preferred_size_ = math::Vec2(100.0f, 30.0f);
    math::Vec2 explicit_pref_ = math::Vec2(0.0f, 0.0f);         // pinned axes (0 = auto)
    float content_scale_ = 1.0f;                                 // children→this space (identity default)
    math::Vec2 content_offset_ = math::Vec2(0.0f, 0.0f);
    float content_text_min_px_ = 0.0f;                           // cull descendants' text below this (screen px)
    math::Vec2 min_size_ = math::Vec2(0.0f, 0.0f);
    math::Vec2 max_size_ = math::Vec2(1e12f, 1e12f);
    math::Box clip_rect_ = math::make_box(0.0f, 0.0f, 0.0f, 0.0f);
    bool clip_enabled_ = false;
    bool visible_ = true, enabled_ = true, focusable_ = false, focused_ = false;
    WidgetState state_ = WidgetState::Normal;
    GuiStyle style_ = GuiStyle::default_style();
    SizeMode width_mode_ = SizeMode::Auto, height_mode_ = SizeMode::Auto;
    Alignment alignment_ = Alignment::TopLeft;
    LayoutDirection layout_dir_ = LayoutDirection::Vertical;
    float spacing_ = 0.0f;
    IGuiEventHandler* event_handler_ = nullptr;
    std::vector<IGuiWidget*> children_;
    ISizer* sizer_ = nullptr;       // optional: drives child layout + preferred size
    WidgetBindings binds_;          // visible / enabled / text derived from the model
    mutable WidgetRenderInfo render_info_;
    mutable bool dirty_ = true;
    std::function<void()> dirty_listener_;   // fired on mark_dirty (context root only)
};

// ============================================================================
// WidgetBase<> - Template that delegates all IGuiWidget to GuiWidget base_
// ============================================================================

template<typename Interface, WidgetType TYPE>
class WidgetBase : public Interface, public IWidgetInternal {
protected:
    GuiWidget base_{TYPE};
public:
    WidgetType get_type() const override { return TYPE; }
    const char* get_name() const override { return base_.get_name(); }
    void set_name(const char* n) override { base_.set_name(n); }
    IGuiWidget* find_by_name(const char* n) override {
        return (n && std::strcmp(base_.get_name(), n) == 0) ? static_cast<IGuiWidget*>(this) : nullptr;
    }
    void find_all_by_name(const char* n, std::vector<IGuiWidget*>& out) override {
        if (n && std::strcmp(base_.get_name(), n) == 0) out.push_back(this);
    }
    IGuiWidget* get_parent() const override { return base_.get_parent(); }
    void set_parent(IGuiWidget* p) override { base_.set_parent(p); }
    math::Box get_bounds() const override { return base_.get_bounds(); }
    void set_bounds(const math::Box& b) override { base_.set_bounds(b); }
    math::Vec2 get_preferred_size() const override { return base_.get_preferred_size(); }
    void set_preferred_size(const math::Vec2& s) override { base_.set_preferred_size(s); }
    math::Vec2 get_min_size() const override { return base_.get_min_size(); }
    math::Vec2 get_max_size() const override { return base_.get_max_size(); }
    void set_min_size(const math::Vec2& s) override { base_.set_min_size(s); }
    void set_max_size(const math::Vec2& s) override { base_.set_max_size(s); }
    bool is_clip_enabled() const override { return base_.is_clip_enabled(); }
    void set_clip_enabled(bool e) override { base_.set_clip_enabled(e); }
    math::Box get_clip_rect() const override { return base_.get_clip_rect(); }
    void set_clip_rect(const math::Box& r) override { base_.set_clip_rect(r); }
    bool is_visible() const override { return base_.is_visible(); }
    void set_visible(bool v) override { base_.set_visible(v); }
    bool is_enabled() const override { return base_.is_enabled(); }
    void set_enabled(bool e) override { base_.set_enabled(e); }
    WidgetState get_state() const override { return base_.get_state(); }
    void bind_visible(std::function<bool()> p) override { base_.bindings().visible = std::move(p); base_.mark_dirty(); }
    void bind_enabled(std::function<bool()> p) override { base_.bindings().enabled = std::move(p); base_.mark_dirty(); }
    void bind_text(std::function<std::string()> p) override { base_.bindings().text = std::move(p); base_.mark_dirty(); }
    // Universal bindings first (applied to THIS wrapper, so set_visible/has_focus
    // hit the widget's own overrides), then this widget's own model provider.
    void refresh_bindings() override { base_.bindings().apply(this, this); refresh_providers(); }
    const GuiStyle& get_style() const override { return base_.get_style(); }
    void set_style(const GuiStyle& s) override { base_.set_style(s); }
    SizeMode get_width_mode() const override { return base_.get_width_mode(); }
    SizeMode get_height_mode() const override { return base_.get_height_mode(); }
    void set_size_mode(SizeMode w, SizeMode h) override { base_.set_size_mode(w, h); }
    Alignment get_alignment() const override { return base_.get_alignment(); }
    void set_alignment(Alignment a) override { base_.set_alignment(a); }
    void set_event_handler(IGuiEventHandler* h) override { base_.set_event_handler(h); }
    float content_scale() const override { return base_.content_scale(); }
    math::Vec2 content_offset() const override { return base_.content_offset(); }
    void set_content_transform(float s, const math::Vec2& o) override { base_.set_content_transform(s, o); }
    float content_text_min_px() const override { return base_.content_text_min_px(); }
    void set_content_text_min_px(float px) override { base_.set_content_text_min_px(px); }
    void update(float dt) override { base_.update(dt); }
    const WidgetRenderInfo& get_render_info(Window* w) const override { return base_.get_render_info(w); }
    void mark_dirty() override { base_.mark_dirty(); }
    bool is_dirty() const override { return base_.is_dirty(); }
    bool handle_mouse_move(const math::Vec2& p) override { return base_.handle_mouse_move(p); }
    bool handle_mouse_button(MouseButton b, bool pr, const math::Vec2& p) override { return base_.handle_mouse_button(b, pr, p); }
    bool handle_mouse_scroll(float dx, float dy) override { return base_.handle_mouse_scroll(dx, dy); }
    bool handle_key(int c, bool pr, int m) override { return base_.handle_key(c, pr, m); }
    bool handle_text_input(const char* t) override { return base_.handle_text_input(t); }
    bool is_focusable() const override { return false; }
    bool has_focus() const override { return base_.has_focus(); }
    void set_focus(bool f) override { base_.set_focus(f); }
    bool hit_test(const math::Vec2& p) const override { return base_.hit_test(p); }
    IGuiWidget* find_widget_at(const math::Vec2& p) override {
        return hit_test(p) ? static_cast<IGuiWidget*>(this) : nullptr;
    }
    int get_child_count() const override { return 0; }
    IGuiWidget* get_child(int) const override { return nullptr; }
    bool add_child(IGuiWidget*) override { return false; }
    bool insert_child_before(IGuiWidget*, IGuiWidget*) override { return false; }
    bool remove_child(IGuiWidget*) override { return false; }
    bool remove_child_at(int) override { return false; }
    void clear_children() override {}
    LayoutDirection get_layout_direction() const override { return LayoutDirection::Vertical; }
    void set_layout_direction(LayoutDirection) override {}
    float get_spacing() const override { return 0; }
    void set_spacing(float) override {}
    void layout_children() override {}

protected:
    // See GuiWidget::refresh_providers — subclasses override this, never
    // refresh_bindings, so the universal bindings are always applied.
    virtual void refresh_providers() {}
};

// ============================================================================
// Common item storage for item-based widgets
// ============================================================================

struct WidgetItem {
    int id = -1;
    std::string text, icon;
    // Optional underlying value/key distinct from the display `text` — lets a
    // combo/list item carry its own id (e.g. a graph id) so the widget owns the
    // selection end to end and the app never mirrors it.
    std::string value;
    bool enabled = true, checked = false;
    void* user_data = nullptr;
    // Optional per-row decoration used by model-driven lists (ListBox::set_items):
    // a leading colour swatch (alpha 0 = none), an inline-editable label, and a
    // trailing action affordance ("×"/delete). Ignored by widgets that don't draw them.
    math::Vec4 swatch = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    bool editable = false;
    bool has_action = false;
    // Optional per-row text colour override (alpha 0 = use the style's text colour).
    // Lets a log/console list colour rows by severity without a per-row widget.
    math::Vec4 text_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    // Optional per-row background override (alpha 0 = style default) + centred text —
    // accent rows ("+ New …" button-like entries) without a per-row widget.
    math::Vec4 row_color = math::Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    bool centered = false;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_WIDGET_BASE_HPP
