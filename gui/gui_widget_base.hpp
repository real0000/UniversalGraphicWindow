/*
 * gui_widget_base.hpp - Internal base classes for GUI widget implementations
 *
 * Contains GuiWidget (concrete IGuiWidget), WidgetBase<> template, and WidgetItem.
 * This is an internal header - not part of the public API.
 */

#ifndef WINDOW_GUI_WIDGET_BASE_HPP
#define WINDOW_GUI_WIDGET_BASE_HPP

#include "gui.hpp"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace window {
namespace gui {

// ============================================================================
// GuiWidget - Concrete base implementing all IGuiWidget methods
// ============================================================================

class GuiWidget : public IGuiWidget {
public:
    explicit GuiWidget(WidgetType type) : type_(type) {}
    ~GuiWidget() override = default;

    WidgetType get_type() const override { return type_; }
    const char* get_name() const override { return name_.c_str(); }
    void set_name(const char* n) override { name_ = n ? n : ""; }
    IGuiWidget* find_by_name(const char* n) override {
        if (n && name_ == n) return this;
        for (auto* c : children_) { if (auto* f = c->find_by_name(n)) return f; }
        return nullptr;
    }
    void find_all_by_name(const char* n, std::vector<IGuiWidget*>& out) override {
        if (n && name_ == n) out.push_back(this);
        for (auto* c : children_) c->find_all_by_name(n, out);
    }
    IGuiWidget* get_parent() const override { return parent_; }
    void set_parent(IGuiWidget* p) override { parent_ = p; }
    math::Box get_bounds() const override { return bounds_; }
    void set_bounds(const math::Box& b) override { bounds_ = b; }
    math::Vec2 get_preferred_size() const override { return preferred_size_; }
    math::Vec2 get_min_size() const override { return min_size_; }
    math::Vec2 get_max_size() const override { return max_size_; }
    void set_min_size(const math::Vec2& s) override { min_size_ = s; }
    void set_max_size(const math::Vec2& s) override { max_size_ = s; }
    bool is_clip_enabled() const override { return clip_enabled_; }
    void set_clip_enabled(bool e) override { clip_enabled_ = e; }
    math::Box get_clip_rect() const override { return clip_rect_; }
    void set_clip_rect(const math::Box& r) override { clip_rect_ = r; }
    bool is_visible() const override { return visible_; }
    void set_visible(bool v) override { visible_ = v; }
    bool is_enabled() const override { return enabled_; }
    void set_enabled(bool e) override { enabled_ = e; }
    WidgetState get_state() const override { return state_; }
    const GuiStyle& get_style() const override { return style_; }
    void set_style(const GuiStyle& s) override { style_ = s; }
    SizeMode get_width_mode() const override { return width_mode_; }
    SizeMode get_height_mode() const override { return height_mode_; }
    void set_size_mode(SizeMode w, SizeMode h) override { width_mode_ = w; height_mode_ = h; }
    Alignment get_alignment() const override { return alignment_; }
    void set_alignment(Alignment a) override { alignment_ = a; }
    void set_event_handler(IGuiEventHandler* h) override { event_handler_ = h; }
    void update(float dt) override { for (auto* c : children_) c->update(dt); }
    void get_render_info(Window*, WidgetRenderInfo* out) const override {
        if (!out) return;
        out->clear();
        out->clip_rect = clip_enabled_ ? clip_rect_ : bounds_;
        TextureEntry bg;
        bg.source_type = TextureSourceType::Generated;
        bg.solid_color = style_.background_color;
        bg.dest_rect = bounds_;
        bg.clip_rect = out->clip_rect;
        bg.depth = 0;
        out->textures.push_back(bg);
        out->sort_and_batch();
    }
    bool handle_mouse_move(const math::Vec2& pos) override {
        if (!enabled_ || !visible_) return false;
        bool was = (state_ == WidgetState::Hovered);
        bool inside = hit_test(pos);
        if (inside && state_ == WidgetState::Normal) state_ = WidgetState::Hovered;
        else if (!inside && state_ == WidgetState::Hovered) state_ = WidgetState::Normal;
        for (auto* c : children_) { if (c->handle_mouse_move(pos)) return true; }
        return inside != was;
    }
    bool handle_mouse_button(MouseButton btn, bool pressed, const math::Vec2& pos) override {
        if (!enabled_ || !visible_) return false;
        for (auto* c : children_) { if (c->handle_mouse_button(btn, pressed, pos)) return true; }
        if (!hit_test(pos)) return false;
        if (pressed) state_ = WidgetState::Pressed;
        else if (state_ == WidgetState::Pressed) {
            state_ = WidgetState::Hovered;
            if (event_handler_) {
                GuiEvent ev; ev.type = GuiEventType::Click; ev.source = this; ev.position = pos;
                event_handler_->on_gui_event(ev);
            }
        }
        return true;
    }
    bool handle_mouse_scroll(float dx, float dy) override {
        for (auto* c : children_) { if (c->handle_mouse_scroll(dx, dy)) return true; }
        return false;
    }
    bool handle_key(int code, bool pressed, int mods) override {
        for (auto* c : children_) { if (c->handle_key(code, pressed, mods)) return true; }
        return false;
    }
    bool handle_text_input(const char* text) override {
        for (auto* c : children_) { if (c->handle_text_input(text)) return true; }
        return false;
    }
    bool is_focusable() const override { return focusable_; }
    bool has_focus() const override { return focused_; }
    void set_focus(bool f) override { focused_ = f; if (f) state_ = WidgetState::Focused; }
    bool hit_test(const math::Vec2& pos) const override { return math::box_contains(bounds_, pos); }
    IGuiWidget* find_widget_at(const math::Vec2& pos) override {
        if (!visible_ || !hit_test(pos)) return nullptr;
        for (int i = (int)children_.size() - 1; i >= 0; --i) {
            if (auto* w = children_[i]->find_widget_at(pos)) return w;
        }
        return this;
    }
    int get_child_count() const override { return (int)children_.size(); }
    IGuiWidget* get_child(int i) const override { return (i >= 0 && i < (int)children_.size()) ? children_[i] : nullptr; }
    bool add_child(IGuiWidget* c) override { if (!c) return false; children_.push_back(c); c->set_parent(this); return true; }
    bool remove_child(IGuiWidget* c) override {
        auto it = std::find(children_.begin(), children_.end(), c);
        if (it == children_.end()) return false;
        children_.erase(it); return true;
    }
    bool remove_child_at(int i) override {
        if (i < 0 || i >= (int)children_.size()) return false;
        children_.erase(children_.begin() + i); return true;
    }
    void clear_children() override { children_.clear(); }
    LayoutDirection get_layout_direction() const override { return layout_dir_; }
    void set_layout_direction(LayoutDirection d) override { layout_dir_ = d; }
    float get_spacing() const override { return spacing_; }
    void set_spacing(float s) override { spacing_ = s; }
    void layout_children() override {}

protected:
    WidgetType type_;
    std::string name_;
    IGuiWidget* parent_ = nullptr;
    math::Box bounds_;
    math::Vec2 preferred_size_ = math::Vec2(100.0f, 30.0f);
    math::Vec2 min_size_ = math::Vec2(0.0f, 0.0f);
    math::Vec2 max_size_ = math::Vec2(1e12f, 1e12f);
    math::Box clip_rect_;
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
};

// ============================================================================
// WidgetBase<> - Template that delegates all IGuiWidget to GuiWidget base_
// ============================================================================

template<typename Interface, WidgetType TYPE>
class WidgetBase : public Interface {
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
    const GuiStyle& get_style() const override { return base_.get_style(); }
    void set_style(const GuiStyle& s) override { base_.set_style(s); }
    SizeMode get_width_mode() const override { return base_.get_width_mode(); }
    SizeMode get_height_mode() const override { return base_.get_height_mode(); }
    void set_size_mode(SizeMode w, SizeMode h) override { base_.set_size_mode(w, h); }
    Alignment get_alignment() const override { return base_.get_alignment(); }
    void set_alignment(Alignment a) override { base_.set_alignment(a); }
    void set_event_handler(IGuiEventHandler* h) override { base_.set_event_handler(h); }
    void update(float dt) override { base_.update(dt); }
    void get_render_info(Window* w, WidgetRenderInfo* out) const override { base_.get_render_info(w, out); }
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
    bool remove_child(IGuiWidget*) override { return false; }
    bool remove_child_at(int) override { return false; }
    void clear_children() override {}
    LayoutDirection get_layout_direction() const override { return LayoutDirection::Vertical; }
    void set_layout_direction(LayoutDirection) override {}
    float get_spacing() const override { return 0; }
    void set_spacing(float) override {}
    void layout_children() override {}
};

// ============================================================================
// Common item storage for item-based widgets
// ============================================================================

struct WidgetItem {
    int id = -1;
    std::string text, icon;
    bool enabled = true, checked = false;
    void* user_data = nullptr;
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_WIDGET_BASE_HPP
