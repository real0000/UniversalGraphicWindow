/*
 * gui_panel.cpp - SplitPanel and DockPanel Implementations
 *
 * Concrete implementations of IGuiSplitPanel and IGuiDockPanel.
 */

#include "gui.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <cassert>

namespace window {
namespace gui {

// ============================================================================
// GuiSplitPanel Implementation
// ============================================================================

class GuiSplitPanel : public IGuiSplitPanel {
public:
    explicit GuiSplitPanel(SplitOrientation orientation)
        : orientation_(orientation)
        , style_(SplitterStyle::default_style())
        , widget_style_(GuiStyle::default_style())
    {
    }

    ~GuiSplitPanel() override = default;

    // -- IGuiWidget base --

    WidgetType get_type() const override { return WidgetType::Panel; }

    const char* get_name() const override { return name_.c_str(); }
    void set_name(const char* name) override { name_ = name ? name : ""; }

    IGuiWidget* find_by_name(const char* name) override {
        if (name_ == name) return this;
        if (first_panel_) {
            IGuiWidget* found = first_panel_->find_by_name(name);
            if (found) return found;
        }
        if (second_panel_) {
            IGuiWidget* found = second_panel_->find_by_name(name);
            if (found) return found;
        }
        return nullptr;
    }

    void find_all_by_name(const char* name, std::vector<IGuiWidget*>& out) override {
        if (name_ == name) out.push_back(this);
        if (first_panel_) first_panel_->find_all_by_name(name, out);
        if (second_panel_) second_panel_->find_all_by_name(name, out);
    }

    IGuiWidget* get_parent() const override { return parent_; }
    void set_parent(IGuiWidget* parent) override { parent_ = parent; }

    math::Box get_bounds() const override { return bounds_; }
    void set_bounds(const math::Box& bounds) override {
        bounds_ = bounds;
        recalculate_layout();
    }

    math::Vec2 get_preferred_size() const override { return math::Vec2(400.0f, 300.0f); }
    math::Vec2 get_min_size() const override { return min_size_; }
    math::Vec2 get_max_size() const override { return max_size_; }
    void set_min_size(const math::Vec2& size) override { min_size_ = size; }
    void set_max_size(const math::Vec2& size) override { max_size_ = size; }

    bool is_clip_enabled() const override { return clip_enabled_; }
    void set_clip_enabled(bool enabled) override { clip_enabled_ = enabled; }
    math::Box get_clip_rect() const override { return clip_rect_; }
    void set_clip_rect(const math::Box& rect) override { clip_rect_ = rect; }

    bool is_visible() const override { return visible_; }
    void set_visible(bool visible) override { visible_ = visible; }
    bool is_enabled() const override { return enabled_; }
    void set_enabled(bool enabled) override { enabled_ = enabled; }
    WidgetState get_state() const override { return state_; }

    const GuiStyle& get_style() const override { return widget_style_; }
    void set_style(const GuiStyle& style) override { widget_style_ = style; }

    SizeMode get_width_mode() const override { return width_mode_; }
    SizeMode get_height_mode() const override { return height_mode_; }
    void set_size_mode(SizeMode wm, SizeMode hm) override { width_mode_ = wm; height_mode_ = hm; }
    Alignment get_alignment() const override { return alignment_; }
    void set_alignment(Alignment a) override { alignment_ = a; }

    void set_event_handler(IGuiEventHandler* handler) override { event_handler_ = handler; }

    void update(float delta_time) override {
        if (first_panel_ && !first_collapsed_) first_panel_->update(delta_time);
        if (second_panel_ && !second_collapsed_) second_panel_->update(delta_time);
    }

    void get_render_info(Window* window, WidgetRenderInfo* out) const override {
        if (!out) return;
        out->clear();
        out->clip_rect = clip_enabled_ ? clip_rect_ : bounds_;

        // Background
        TextureEntry bg;
        bg.source_type = TextureSourceType::Generated;
        bg.solid_color = widget_style_.background_color;
        bg.dest_rect = bounds_;
        bg.clip_rect = out->clip_rect;
        bg.depth = 0;
        out->textures.push_back(bg);

        // Splitter bar
        if (!first_collapsed_ && !second_collapsed_) {
            TextureEntry splitter;
            splitter.source_type = TextureSourceType::Generated;
            splitter.solid_color = splitter_dragging_ ? style_.splitter_drag_color
                                 : splitter_hovered_ ? style_.splitter_hover_color
                                 : style_.splitter_color;
            splitter.dest_rect = get_splitter_visual_rect();
            splitter.clip_rect = out->clip_rect;
            splitter.depth = 1;
            out->textures.push_back(splitter);

            // Grip dots
            add_grip_dots(out);
        }

        out->sort_and_batch();
    }

    bool handle_mouse_move(const math::Vec2& position) override {
        if (!enabled_ || !visible_) return false;

        if (splitter_dragging_) {
            float total = get_total_size();
            float bmin = (orientation_ == SplitOrientation::Horizontal)
                ? math::x(bounds_.min_corner()) : math::y(bounds_.min_corner());
            float mouse_pos = (orientation_ == SplitOrientation::Horizontal)
                ? math::x(position) : math::y(position);
            float new_pos = mouse_pos - bmin;
            new_pos = clamp_split_position(new_pos, total);
            split_position_ = new_pos;
            split_ratio_ = (total > 0.0f) ? new_pos / total : 0.5f;
            recalculate_layout();
            if (split_handler_) split_handler_->on_split_changed(split_position_, split_ratio_);
            return true;
        }

        math::Box hit_rect = get_splitter_hit_rect();
        bool was_hovered = splitter_hovered_;
        splitter_hovered_ = math::box_contains(hit_rect, position);

        // Forward to children
        bool handled = false;
        if (first_panel_ && !first_collapsed_ && math::box_contains(first_rect_, position))
            handled = first_panel_->handle_mouse_move(position);
        if (!handled && second_panel_ && !second_collapsed_ && math::box_contains(second_rect_, position))
            handled = second_panel_->handle_mouse_move(position);

        return handled || (splitter_hovered_ != was_hovered);
    }

    bool handle_mouse_button(MouseButton button, bool pressed, const math::Vec2& position) override {
        if (!enabled_ || !visible_) return false;

        if (button == MouseButton::Left) {
            if (pressed) {
                math::Box hit_rect = get_splitter_hit_rect();
                if (!splitter_fixed_ && math::box_contains(hit_rect, position)) {
                    splitter_dragging_ = true;
                    if (split_handler_) split_handler_->on_split_drag_started();
                    return true;
                }
            } else {
                if (splitter_dragging_) {
                    splitter_dragging_ = false;
                    if (split_handler_) split_handler_->on_split_drag_ended();
                    return true;
                }
            }
        }

        // Double-click on splitter to toggle collapse
        if (button == MouseButton::Left && pressed && collapsible_) {
            math::Box hit_rect = get_splitter_hit_rect();
            if (math::box_contains(hit_rect, position)) {
                // Simple toggle: collapse whichever side is larger
                if (!first_collapsed_ && !second_collapsed_) {
                    if (split_ratio_ >= 0.5f) set_second_collapsed(true);
                    else set_first_collapsed(true);
                } else {
                    set_first_collapsed(false);
                    set_second_collapsed(false);
                }
                return true;
            }
        }

        // Forward to children
        if (first_panel_ && !first_collapsed_ && math::box_contains(first_rect_, position))
            return first_panel_->handle_mouse_button(button, pressed, position);
        if (second_panel_ && !second_collapsed_ && math::box_contains(second_rect_, position))
            return second_panel_->handle_mouse_button(button, pressed, position);

        return false;
    }

    bool handle_mouse_scroll(float dx, float dy) override {
        if (first_panel_ && !first_collapsed_) {
            if (first_panel_->handle_mouse_scroll(dx, dy)) return true;
        }
        if (second_panel_ && !second_collapsed_) {
            if (second_panel_->handle_mouse_scroll(dx, dy)) return true;
        }
        return false;
    }

    bool handle_key(int key_code, bool pressed, int modifiers) override {
        if (first_panel_ && first_panel_->has_focus())
            return first_panel_->handle_key(key_code, pressed, modifiers);
        if (second_panel_ && second_panel_->has_focus())
            return second_panel_->handle_key(key_code, pressed, modifiers);
        return false;
    }

    bool handle_text_input(const char* text) override {
        if (first_panel_ && first_panel_->has_focus())
            return first_panel_->handle_text_input(text);
        if (second_panel_ && second_panel_->has_focus())
            return second_panel_->handle_text_input(text);
        return false;
    }

    bool is_focusable() const override { return false; }
    bool has_focus() const override {
        if (first_panel_ && first_panel_->has_focus()) return true;
        if (second_panel_ && second_panel_->has_focus()) return true;
        return false;
    }
    void set_focus(bool focus) override { (void)focus; }

    bool hit_test(const math::Vec2& position) const override {
        return math::box_contains(bounds_, position);
    }

    IGuiWidget* find_widget_at(const math::Vec2& position) override {
        if (!visible_ || !math::box_contains(bounds_, position)) return nullptr;
        if (first_panel_ && !first_collapsed_ && math::box_contains(first_rect_, position)) {
            IGuiWidget* w = first_panel_->find_widget_at(position);
            if (w) return w;
        }
        if (second_panel_ && !second_collapsed_ && math::box_contains(second_rect_, position)) {
            IGuiWidget* w = second_panel_->find_widget_at(position);
            if (w) return w;
        }
        return this;
    }

    // Children (split panel exposes its two panels as children)
    int get_child_count() const override {
        int count = 0;
        if (first_panel_) ++count;
        if (second_panel_) ++count;
        return count;
    }

    IGuiWidget* get_child(int index) const override {
        if (index == 0 && first_panel_) return first_panel_;
        if (index == 0 && !first_panel_ && second_panel_) return second_panel_;
        if (index == 1 && second_panel_) return second_panel_;
        return nullptr;
    }

    bool add_child(IGuiWidget* child) override {
        if (!first_panel_) { set_first_panel(child); return true; }
        if (!second_panel_) { set_second_panel(child); return true; }
        return false;
    }

    bool remove_child(IGuiWidget* child) override {
        if (first_panel_ == child) { first_panel_ = nullptr; return true; }
        if (second_panel_ == child) { second_panel_ = nullptr; return true; }
        return false;
    }

    bool remove_child_at(int index) override {
        if (index == 0 && first_panel_) { first_panel_ = nullptr; return true; }
        if (index == 1 && second_panel_) { second_panel_ = nullptr; return true; }
        return false;
    }

    void clear_children() override {
        first_panel_ = nullptr;
        second_panel_ = nullptr;
    }

    LayoutDirection get_layout_direction() const override {
        return (orientation_ == SplitOrientation::Horizontal)
            ? LayoutDirection::Horizontal : LayoutDirection::Vertical;
    }
    void set_layout_direction(LayoutDirection d) override {
        orientation_ = (d == LayoutDirection::Horizontal)
            ? SplitOrientation::Horizontal : SplitOrientation::Vertical;
        recalculate_layout();
    }
    float get_spacing() const override { return style_.splitter_thickness; }
    void set_spacing(float s) override { style_.splitter_thickness = s; recalculate_layout(); }
    void layout_children() override { recalculate_layout(); }

    // -- IGuiSplitPanel --

    SplitOrientation get_orientation() const override { return orientation_; }
    void set_orientation(SplitOrientation o) override { orientation_ = o; recalculate_layout(); }

    IGuiWidget* get_first_panel() const override { return first_panel_; }
    void set_first_panel(IGuiWidget* w) override {
        first_panel_ = w;
        if (w) w->set_parent(this);
        recalculate_layout();
    }

    IGuiWidget* get_second_panel() const override { return second_panel_; }
    void set_second_panel(IGuiWidget* w) override {
        second_panel_ = w;
        if (w) w->set_parent(this);
        recalculate_layout();
    }

    float get_split_position() const override { return split_position_; }
    void set_split_position(float pos) override {
        float total = get_total_size();
        split_position_ = clamp_split_position(pos, total);
        split_ratio_ = (total > 0.0f) ? split_position_ / total : 0.5f;
        recalculate_layout();
    }

    float get_split_ratio() const override { return split_ratio_; }
    void set_split_ratio(float ratio) override {
        split_ratio_ = std::max(0.0f, std::min(1.0f, ratio));
        float total = get_total_size();
        split_position_ = clamp_split_position(split_ratio_ * total, total);
        recalculate_layout();
    }

    SplitSizeUnit get_split_unit() const override { return split_unit_; }
    void set_split_unit(SplitSizeUnit unit) override { split_unit_ = unit; }

    float get_first_min_size() const override { return first_min_; }
    void set_first_min_size(float s) override { first_min_ = s; recalculate_layout(); }
    float get_first_max_size() const override { return first_max_; }
    void set_first_max_size(float s) override { first_max_ = s; recalculate_layout(); }
    float get_second_min_size() const override { return second_min_; }
    void set_second_min_size(float s) override { second_min_ = s; recalculate_layout(); }
    float get_second_max_size() const override { return second_max_; }
    void set_second_max_size(float s) override { second_max_ = s; recalculate_layout(); }

    bool is_first_collapsed() const override { return first_collapsed_; }
    void set_first_collapsed(bool c) override { first_collapsed_ = c; recalculate_layout(); }
    bool is_second_collapsed() const override { return second_collapsed_; }
    void set_second_collapsed(bool c) override { second_collapsed_ = c; recalculate_layout(); }
    bool is_collapsible() const override { return collapsible_; }
    void set_collapsible(bool c) override { collapsible_ = c; }

    bool is_splitter_fixed() const override { return splitter_fixed_; }
    void set_splitter_fixed(bool f) override { splitter_fixed_ = f; }
    bool is_splitter_hovered() const override { return splitter_hovered_; }
    bool is_splitter_dragging() const override { return splitter_dragging_; }

    const SplitterStyle& get_splitter_style() const override { return style_; }
    void set_splitter_style(const SplitterStyle& s) override { style_ = s; recalculate_layout(); }

    void set_split_event_handler(ISplitPanelEventHandler* h) override { split_handler_ = h; }

    void get_split_panel_render_info(SplitPanelRenderInfo* out) const override {
        if (!out) return;
        out->widget = this;
        out->bounds = bounds_;
        out->clip_rect = clip_enabled_ ? clip_rect_ : bounds_;
        out->first_panel_rect = first_rect_;
        out->second_panel_rect = second_rect_;
        out->splitter_rect = get_splitter_visual_rect();
        out->style = style_;
        out->orientation = orientation_;
        out->splitter_hovered = splitter_hovered_;
        out->splitter_dragging = splitter_dragging_;
        out->split_position = split_position_;
        out->split_ratio = split_ratio_;
    }

private:
    float get_total_size() const {
        float thickness = style_.splitter_thickness;
        if (orientation_ == SplitOrientation::Horizontal)
            return math::box_width(bounds_) - thickness;
        else
            return math::box_height(bounds_) - thickness;
    }

    float clamp_split_position(float pos, float total) const {
        float first_clamped = std::max(first_min_, std::min(first_max_, pos));
        float second_space = total - first_clamped;
        if (second_space < second_min_) first_clamped = total - second_min_;
        if (second_max_ < 1e12f && second_space > second_max_) first_clamped = total - second_max_;
        return std::max(0.0f, std::min(total, first_clamped));
    }

    math::Box get_splitter_visual_rect() const {
        float bx = math::x(bounds_.min_corner());
        float by = math::y(bounds_.min_corner());
        float bw = math::box_width(bounds_);
        float bh = math::box_height(bounds_);
        float t = style_.splitter_thickness;

        if (orientation_ == SplitOrientation::Horizontal) {
            return math::make_box(bx + split_position_, by, t, bh);
        } else {
            return math::make_box(bx, by + split_position_, bw, t);
        }
    }

    math::Box get_splitter_hit_rect() const {
        float bx = math::x(bounds_.min_corner());
        float by = math::y(bounds_.min_corner());
        float bw = math::box_width(bounds_);
        float bh = math::box_height(bounds_);
        float t = style_.hit_area_thickness;
        float offset = (t - style_.splitter_thickness) * 0.5f;

        if (orientation_ == SplitOrientation::Horizontal) {
            return math::make_box(bx + split_position_ - offset, by, t, bh);
        } else {
            return math::make_box(bx, by + split_position_ - offset, bw, t);
        }
    }

    void add_grip_dots(WidgetRenderInfo* out) const {
        math::Box splitter_rect = get_splitter_visual_rect();
        float cx = math::x(math::box_center(splitter_rect));
        float cy = math::y(math::box_center(splitter_rect));
        float dot_size = style_.grip_dot_size;
        float spacing = dot_size * 3.0f;
        int count = style_.grip_dot_count;
        float total_len = (count - 1) * spacing;

        for (int i = 0; i < count; ++i) {
            float offset = -total_len * 0.5f + i * spacing;
            float dx, dy;
            if (orientation_ == SplitOrientation::Horizontal) {
                dx = cx; dy = cy + offset;
            } else {
                dx = cx + offset; dy = cy;
            }
            TextureEntry dot;
            dot.source_type = TextureSourceType::Generated;
            dot.solid_color = style_.grip_color;
            dot.dest_rect = math::make_box(dx - dot_size * 0.5f, dy - dot_size * 0.5f, dot_size, dot_size);
            dot.clip_rect = out->clip_rect;
            dot.depth = 2;
            out->textures.push_back(dot);
        }
    }

    void recalculate_layout() {
        float bx = math::x(bounds_.min_corner());
        float by = math::y(bounds_.min_corner());
        float bw = math::box_width(bounds_);
        float bh = math::box_height(bounds_);
        float thickness = style_.splitter_thickness;

        if (first_collapsed_ && second_collapsed_) {
            first_rect_ = math::make_box(bx, by, 0, 0);
            second_rect_ = math::make_box(bx, by, 0, 0);
            return;
        }

        if (first_collapsed_) {
            first_rect_ = math::make_box(bx, by, 0, 0);
            second_rect_ = bounds_;
            if (second_panel_) second_panel_->set_bounds(second_rect_);
            return;
        }

        if (second_collapsed_) {
            first_rect_ = bounds_;
            second_rect_ = math::make_box(bx, by, 0, 0);
            if (first_panel_) first_panel_->set_bounds(first_rect_);
            return;
        }

        // Apply ratio to get position if using ratio mode
        if (split_unit_ == SplitSizeUnit::Ratio) {
            float total = get_total_size();
            split_position_ = split_ratio_ * total;
            split_position_ = clamp_split_position(split_position_, total);
        }

        if (orientation_ == SplitOrientation::Horizontal) {
            first_rect_ = math::make_box(bx, by, split_position_, bh);
            second_rect_ = math::make_box(bx + split_position_ + thickness, by,
                                          bw - split_position_ - thickness, bh);
        } else {
            first_rect_ = math::make_box(bx, by, bw, split_position_);
            second_rect_ = math::make_box(bx, by + split_position_ + thickness,
                                          bw, bh - split_position_ - thickness);
        }

        if (first_panel_) first_panel_->set_bounds(first_rect_);
        if (second_panel_) second_panel_->set_bounds(second_rect_);
    }

    // State
    SplitOrientation orientation_;
    SplitterStyle style_;
    GuiStyle widget_style_;
    std::string name_;
    IGuiWidget* parent_ = nullptr;
    math::Box bounds_;
    math::Vec2 min_size_ = math::Vec2(100.0f, 100.0f);
    math::Vec2 max_size_ = math::Vec2(1e12f, 1e12f);
    math::Box clip_rect_;
    bool clip_enabled_ = true;
    bool visible_ = true;
    bool enabled_ = true;
    WidgetState state_ = WidgetState::Normal;
    SizeMode width_mode_ = SizeMode::Fill;
    SizeMode height_mode_ = SizeMode::Fill;
    Alignment alignment_ = Alignment::TopLeft;
    IGuiEventHandler* event_handler_ = nullptr;
    ISplitPanelEventHandler* split_handler_ = nullptr;

    // Panels
    IGuiWidget* first_panel_ = nullptr;
    IGuiWidget* second_panel_ = nullptr;
    math::Box first_rect_;
    math::Box second_rect_;

    // Split state
    float split_position_ = 200.0f;
    float split_ratio_ = 0.5f;
    SplitSizeUnit split_unit_ = SplitSizeUnit::Ratio;
    float first_min_ = 0.0f;
    float first_max_ = 1e12f;
    float second_min_ = 0.0f;
    float second_max_ = 1e12f;
    bool first_collapsed_ = false;
    bool second_collapsed_ = false;
    bool collapsible_ = true;
    bool splitter_fixed_ = false;
    bool splitter_hovered_ = false;
    bool splitter_dragging_ = false;
};

// ============================================================================
// DockPanel Internal Types
// ============================================================================

struct DockPanelEntry {
    int id = -1;
    std::string title;
    std::string icon_name;
    IGuiWidget* content = nullptr;
    DockZone zone = DockZone::Center;
    DockPanelState state = DockPanelState::Docked;
    math::Box floating_bounds;
    bool visible = true;
    bool closable = true;
    bool active = false;
    bool auto_hide_expanded = false;
    void* user_data = nullptr;
};

// ============================================================================
// GuiDockPanel Implementation
// ============================================================================

class GuiDockPanel : public IGuiDockPanel {
public:
    GuiDockPanel()
        : style_(DockPanelStyle::default_style())
        , widget_style_(GuiStyle::default_style())
    {
    }

    ~GuiDockPanel() override = default;

    // -- IGuiWidget base --

    WidgetType get_type() const override { return WidgetType::Panel; }

    const char* get_name() const override { return name_.c_str(); }
    void set_name(const char* name) override { name_ = name ? name : ""; }

    IGuiWidget* find_by_name(const char* name) override {
        if (name_ == name) return this;
        for (auto& p : panels_) {
            if (p.content) {
                IGuiWidget* found = p.content->find_by_name(name);
                if (found) return found;
            }
        }
        if (center_content_) {
            IGuiWidget* found = center_content_->find_by_name(name);
            if (found) return found;
        }
        return nullptr;
    }

    void find_all_by_name(const char* name, std::vector<IGuiWidget*>& out) override {
        if (name_ == name) out.push_back(this);
        for (auto& p : panels_) {
            if (p.content) p.content->find_all_by_name(name, out);
        }
        if (center_content_) center_content_->find_all_by_name(name, out);
    }

    IGuiWidget* get_parent() const override { return parent_; }
    void set_parent(IGuiWidget* parent) override { parent_ = parent; }

    math::Box get_bounds() const override { return bounds_; }
    void set_bounds(const math::Box& bounds) override {
        bounds_ = bounds;
        recalculate_layout();
    }

    math::Vec2 get_preferred_size() const override { return math::Vec2(800.0f, 600.0f); }
    math::Vec2 get_min_size() const override { return min_size_; }
    math::Vec2 get_max_size() const override { return max_size_; }
    void set_min_size(const math::Vec2& size) override { min_size_ = size; }
    void set_max_size(const math::Vec2& size) override { max_size_ = size; }

    bool is_clip_enabled() const override { return clip_enabled_; }
    void set_clip_enabled(bool enabled) override { clip_enabled_ = enabled; }
    math::Box get_clip_rect() const override { return clip_rect_; }
    void set_clip_rect(const math::Box& rect) override { clip_rect_ = rect; }

    bool is_visible() const override { return visible_; }
    void set_visible(bool v) override { visible_ = v; }
    bool is_enabled() const override { return enabled_; }
    void set_enabled(bool e) override { enabled_ = e; }
    WidgetState get_state() const override { return state_; }

    const GuiStyle& get_style() const override { return widget_style_; }
    void set_style(const GuiStyle& s) override { widget_style_ = s; }

    SizeMode get_width_mode() const override { return width_mode_; }
    SizeMode get_height_mode() const override { return height_mode_; }
    void set_size_mode(SizeMode wm, SizeMode hm) override { width_mode_ = wm; height_mode_ = hm; }
    Alignment get_alignment() const override { return alignment_; }
    void set_alignment(Alignment a) override { alignment_ = a; }

    void set_event_handler(IGuiEventHandler* handler) override { event_handler_ = handler; }

    void update(float delta_time) override {
        for (auto& p : panels_) {
            if (p.content && p.visible && p.state != DockPanelState::Hidden)
                p.content->update(delta_time);
        }
        if (center_content_) center_content_->update(delta_time);
    }

    void get_render_info(Window* window, WidgetRenderInfo* out) const override {
        if (!out) return;
        out->clear();
        out->clip_rect = clip_enabled_ ? clip_rect_ : bounds_;

        // Overall background
        TextureEntry bg;
        bg.source_type = TextureSourceType::Generated;
        bg.solid_color = widget_style_.background_color;
        bg.dest_rect = bounds_;
        bg.clip_rect = out->clip_rect;
        bg.depth = 0;
        out->textures.push_back(bg);

        // Docked panel backgrounds and title bars
        int depth = 1;
        for (const auto& p : panels_) {
            if (!p.visible || p.state == DockPanelState::Hidden) continue;
            if (p.state == DockPanelState::Floating) continue;  // Rendered separately

            math::Box panel_rect = get_zone_rect(p.zone);
            if (math::box_width(panel_rect) <= 0 || math::box_height(panel_rect) <= 0) continue;

            // Panel background
            TextureEntry panel_bg;
            panel_bg.source_type = TextureSourceType::Generated;
            panel_bg.solid_color = style_.background_color;
            panel_bg.dest_rect = panel_rect;
            panel_bg.clip_rect = out->clip_rect;
            panel_bg.depth = depth++;
            out->textures.push_back(panel_bg);

            // Title bar
            TextureEntry title_bar;
            title_bar.source_type = TextureSourceType::Generated;
            title_bar.solid_color = p.active ? style_.title_bar_active_color : style_.title_bar_color;
            float px = math::x(panel_rect.min_corner());
            float py = math::y(panel_rect.min_corner());
            float pw = math::box_width(panel_rect);
            title_bar.dest_rect = math::make_box(px, py, pw, style_.title_bar_height);
            title_bar.clip_rect = out->clip_rect;
            title_bar.depth = depth++;
            out->textures.push_back(title_bar);
        }

        // Drop indicator
        if (drop_indicator_.visible) {
            TextureEntry indicator;
            indicator.source_type = TextureSourceType::Generated;
            indicator.solid_color = style_.drop_indicator_color;
            indicator.dest_rect = drop_indicator_.indicator_rect;
            indicator.clip_rect = out->clip_rect;
            indicator.depth = 100;
            out->textures.push_back(indicator);
        }

        out->sort_and_batch();
    }

    bool handle_mouse_move(const math::Vec2& pos) override {
        if (!enabled_ || !visible_) return false;

        // Update title bar hover states
        for (auto& p : panels_) {
            if (!p.visible || p.state == DockPanelState::Hidden) continue;
            math::Box zone_rect = get_zone_rect(p.zone);
            float px = math::x(zone_rect.min_corner());
            float py = math::y(zone_rect.min_corner());
            float pw = math::box_width(zone_rect);
            math::Box title_rect = math::make_box(px, py, pw, style_.title_bar_height);
            // Title hover is informational for render
            (void)math::box_contains(title_rect, pos);
        }

        // Forward to active panel content
        for (auto& p : panels_) {
            if (!p.visible || p.state == DockPanelState::Hidden || !p.content) continue;
            math::Box content_rect = get_panel_content_rect(p);
            if (math::box_contains(content_rect, pos))
                return p.content->handle_mouse_move(pos);
        }

        if (center_content_ && math::box_contains(center_rect_, pos))
            return center_content_->handle_mouse_move(pos);

        return false;
    }

    bool handle_mouse_button(MouseButton button, bool pressed, const math::Vec2& pos) override {
        if (!enabled_ || !visible_) return false;

        // Check title bar clicks for activation
        if (button == MouseButton::Left && pressed) {
            for (auto& p : panels_) {
                if (!p.visible || p.state == DockPanelState::Hidden) continue;
                math::Box zone_rect = get_zone_rect(p.zone);
                float px = math::x(zone_rect.min_corner());
                float py = math::y(zone_rect.min_corner());
                float pw = math::box_width(zone_rect);
                math::Box title_rect = math::make_box(px, py, pw, style_.title_bar_height);
                if (math::box_contains(title_rect, pos)) {
                    set_active_panel(p.id);
                    return true;
                }
            }
        }

        // Forward to panel content
        for (auto& p : panels_) {
            if (!p.visible || p.state == DockPanelState::Hidden || !p.content) continue;
            math::Box content_rect = get_panel_content_rect(p);
            if (math::box_contains(content_rect, pos))
                return p.content->handle_mouse_button(button, pressed, pos);
        }

        if (center_content_ && math::box_contains(center_rect_, pos))
            return center_content_->handle_mouse_button(button, pressed, pos);

        return false;
    }

    bool handle_mouse_scroll(float dx, float dy) override {
        for (auto& p : panels_) {
            if (p.content && p.visible && p.state != DockPanelState::Hidden)
                if (p.content->handle_mouse_scroll(dx, dy)) return true;
        }
        if (center_content_) return center_content_->handle_mouse_scroll(dx, dy);
        return false;
    }

    bool handle_key(int key_code, bool pressed, int modifiers) override {
        for (auto& p : panels_) {
            if (p.content && p.active && p.content->has_focus())
                return p.content->handle_key(key_code, pressed, modifiers);
        }
        if (center_content_ && center_content_->has_focus())
            return center_content_->handle_key(key_code, pressed, modifiers);
        return false;
    }

    bool handle_text_input(const char* text) override {
        for (auto& p : panels_) {
            if (p.content && p.active && p.content->has_focus())
                return p.content->handle_text_input(text);
        }
        if (center_content_ && center_content_->has_focus())
            return center_content_->handle_text_input(text);
        return false;
    }

    bool is_focusable() const override { return false; }
    bool has_focus() const override {
        for (auto& p : panels_) {
            if (p.content && p.content->has_focus()) return true;
        }
        if (center_content_ && center_content_->has_focus()) return true;
        return false;
    }
    void set_focus(bool) override {}

    bool hit_test(const math::Vec2& pos) const override {
        return math::box_contains(bounds_, pos);
    }

    IGuiWidget* find_widget_at(const math::Vec2& pos) override {
        if (!visible_ || !math::box_contains(bounds_, pos)) return nullptr;
        for (auto& p : panels_) {
            if (p.content && p.visible && p.state != DockPanelState::Hidden) {
                IGuiWidget* w = p.content->find_widget_at(pos);
                if (w) return w;
            }
        }
        if (center_content_) {
            IGuiWidget* w = center_content_->find_widget_at(pos);
            if (w) return w;
        }
        return this;
    }

    int get_child_count() const override {
        int count = 0;
        for (auto& p : panels_) { if (p.content) ++count; }
        if (center_content_) ++count;
        return count;
    }

    IGuiWidget* get_child(int index) const override {
        int i = 0;
        for (auto& p : panels_) {
            if (p.content) {
                if (i == index) return p.content;
                ++i;
            }
        }
        if (center_content_ && i == index) return center_content_;
        return nullptr;
    }

    bool add_child(IGuiWidget* child) override {
        if (!center_content_) { set_center_content(child); return true; }
        return false;
    }

    bool remove_child(IGuiWidget* child) override {
        for (auto& p : panels_) {
            if (p.content == child) { p.content = nullptr; return true; }
        }
        if (center_content_ == child) { center_content_ = nullptr; return true; }
        return false;
    }

    bool remove_child_at(int index) override {
        int i = 0;
        for (auto& p : panels_) {
            if (p.content) {
                if (i == index) { p.content = nullptr; return true; }
                ++i;
            }
        }
        return false;
    }

    void clear_children() override {
        for (auto& p : panels_) p.content = nullptr;
        center_content_ = nullptr;
    }

    LayoutDirection get_layout_direction() const override { return LayoutDirection::Horizontal; }
    void set_layout_direction(LayoutDirection) override {}
    float get_spacing() const override { return 0.0f; }
    void set_spacing(float) override {}
    void layout_children() override { recalculate_layout(); }

    // -- IGuiDockPanel --

    int add_panel(const char* title, IGuiWidget* content, const char* icon_name) override {
        DockPanelEntry entry;
        entry.id = next_id_++;
        entry.title = title ? title : "";
        entry.icon_name = icon_name ? icon_name : "";
        entry.content = content;
        entry.zone = DockZone::Center;
        entry.state = DockPanelState::Docked;
        if (content) content->set_parent(this);
        panels_.push_back(entry);

        if (active_panel_id_ < 0) active_panel_id_ = entry.id;
        recalculate_layout();
        return entry.id;
    }

    bool remove_panel(int id) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return false;
        if (active_panel_id_ == id) active_panel_id_ = -1;
        panels_.erase(it);
        recalculate_layout();
        if (dock_handler_) dock_handler_->on_layout_changed();
        return true;
    }

    void clear_panels() override {
        panels_.clear();
        active_panel_id_ = -1;
        recalculate_layout();
    }

    int get_panel_count() const override { return static_cast<int>(panels_.size()); }

    const char* get_panel_title(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->title.c_str() : "";
    }

    void set_panel_title(int id, const char* title) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->title = title ? title : "";
    }

    const char* get_panel_icon(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->icon_name.c_str() : "";
    }

    void set_panel_icon(int id, const char* icon) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->icon_name = icon ? icon : "";
    }

    IGuiWidget* get_panel_content(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->content : nullptr;
    }

    DockZone get_panel_zone(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->zone : DockZone::Center;
    }

    void dock_panel(int id, DockZone zone) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->zone = zone;
        it->state = (zone == DockZone::Float) ? DockPanelState::Floating : DockPanelState::Docked;
        recalculate_layout();
        if (dock_handler_) dock_handler_->on_panel_docked(id, zone);
        if (dock_handler_) dock_handler_->on_layout_changed();
    }

    void dock_panel_relative(int id, int target_id, DockZone zone) override {
        // For simplicity, dock to the specified zone
        dock_panel(id, zone);
    }

    void dock_panel_as_tab(int id, int target_id) override {
        auto it = find_panel(id);
        auto target = find_panel(target_id);
        if (it == panels_.end() || target == panels_.end()) return;
        it->zone = target->zone;
        it->state = DockPanelState::Docked;
        recalculate_layout();
    }

    void undock_panel(int id) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->state = DockPanelState::Floating;
        it->zone = DockZone::Float;
        if (dock_handler_) dock_handler_->on_panel_undocked(id);
        if (dock_handler_) dock_handler_->on_layout_changed();
        recalculate_layout();
    }

    DockPanelState get_panel_state(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->state : DockPanelState::Hidden;
    }

    void set_panel_state(int id, DockPanelState s) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->state = s;
        recalculate_layout();
    }

    void float_panel(int id, const math::Box& bounds) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->state = DockPanelState::Floating;
        it->zone = DockZone::Float;
        it->floating_bounds = bounds;
        if (dock_handler_) dock_handler_->on_panel_floated(id);
        recalculate_layout();
    }

    math::Box get_floating_bounds(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->floating_bounds : math::Box();
    }

    void set_floating_bounds(int id, const math::Box& b) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->floating_bounds = b;
    }

    void auto_hide_panel(int id) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->state = DockPanelState::AutoHide;
        it->auto_hide_expanded = false;
        recalculate_layout();
    }

    bool is_auto_hide_expanded(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->auto_hide_expanded : false;
    }

    void expand_auto_hide(int id) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->auto_hide_expanded = true;
    }

    void collapse_auto_hide(int id) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->auto_hide_expanded = false;
    }

    int get_active_panel() const override { return active_panel_id_; }

    void set_active_panel(int id) override {
        for (auto& p : panels_) p.active = (p.id == id);
        active_panel_id_ = id;
        if (dock_handler_) dock_handler_->on_panel_activated(id);
    }

    bool is_panel_visible(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->visible : false;
    }

    void set_panel_visible(int id, bool v) override {
        auto it = find_panel(id);
        if (it == panels_.end()) return;
        it->visible = v;
        recalculate_layout();
    }

    bool is_panel_closable(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->closable : false;
    }

    void set_panel_closable(int id, bool c) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->closable = c;
    }

    float get_zone_size(DockZone zone) const override {
        switch (zone) {
            case DockZone::Left:   return zone_left_size_;
            case DockZone::Right:  return zone_right_size_;
            case DockZone::Top:    return zone_top_size_;
            case DockZone::Bottom: return zone_bottom_size_;
            default: return 0.0f;
        }
    }

    void set_zone_size(DockZone zone, float size) override {
        switch (zone) {
            case DockZone::Left:   zone_left_size_ = size; break;
            case DockZone::Right:  zone_right_size_ = size; break;
            case DockZone::Top:    zone_top_size_ = size; break;
            case DockZone::Bottom: zone_bottom_size_ = size; break;
            default: break;
        }
        recalculate_layout();
    }

    bool is_drag_docking_enabled() const override { return drag_docking_enabled_; }
    void set_drag_docking_enabled(bool e) override { drag_docking_enabled_ = e; }

    std::string save_layout() const override {
        std::ostringstream ss;
        ss << "dock_layout_v1\n";
        ss << "zone_left=" << zone_left_size_ << "\n";
        ss << "zone_right=" << zone_right_size_ << "\n";
        ss << "zone_top=" << zone_top_size_ << "\n";
        ss << "zone_bottom=" << zone_bottom_size_ << "\n";
        for (const auto& p : panels_) {
            ss << "panel=" << p.id
               << "," << p.title
               << "," << static_cast<int>(p.zone)
               << "," << static_cast<int>(p.state)
               << "," << (p.visible ? 1 : 0)
               << "\n";
        }
        return ss.str();
    }

    bool load_layout(const char* data) override {
        if (!data) return false;
        std::istringstream ss(data);
        std::string line;
        if (!std::getline(ss, line) || line != "dock_layout_v1") return false;

        while (std::getline(ss, line)) {
            if (line.substr(0, 10) == "zone_left=")
                zone_left_size_ = std::stof(line.substr(10));
            else if (line.substr(0, 11) == "zone_right=")
                zone_right_size_ = std::stof(line.substr(11));
            else if (line.substr(0, 9) == "zone_top=")
                zone_top_size_ = std::stof(line.substr(9));
            else if (line.substr(0, 12) == "zone_bottom=")
                zone_bottom_size_ = std::stof(line.substr(12));
            else if (line.substr(0, 6) == "panel=") {
                // Parse: id,title,zone,state,visible
                std::istringstream ps(line.substr(6));
                int id, zone_i, state_i, vis;
                char comma;
                std::string title;
                if (ps >> id >> comma && std::getline(ps, title, ',') &&
                    ps >> zone_i >> comma >> state_i >> comma >> vis) {
                    auto it = find_panel(id);
                    if (it != panels_.end()) {
                        it->zone = static_cast<DockZone>(zone_i);
                        it->state = static_cast<DockPanelState>(state_i);
                        it->visible = (vis != 0);
                    }
                }
            }
        }
        recalculate_layout();
        return true;
    }

    IGuiWidget* get_center_content() const override { return center_content_; }
    void set_center_content(IGuiWidget* w) override {
        center_content_ = w;
        if (w) w->set_parent(this);
        recalculate_layout();
    }

    void set_panel_user_data(int id, void* data) override {
        auto it = find_panel(id);
        if (it != panels_.end()) it->user_data = data;
    }

    void* get_panel_user_data(int id) const override {
        auto it = find_panel_const(id);
        return (it != panels_.end()) ? it->user_data : nullptr;
    }

    const DockPanelStyle& get_dock_panel_style() const override { return style_; }
    void set_dock_panel_style(const DockPanelStyle& s) override { style_ = s; }

    void set_dock_event_handler(IDockPanelEventHandler* h) override { dock_handler_ = h; }

    void get_dock_layout_render_info(DockLayoutRenderInfo* out) const override {
        if (!out) return;
        out->widget = this;
        out->bounds = bounds_;
        out->clip_rect = clip_enabled_ ? clip_rect_ : bounds_;
        out->center_rect = center_rect_;
        out->style = style_;
        out->drop_indicator = drop_indicator_;

        out->docked_panel_count = 0;
        out->floating_panel_count = 0;
        out->auto_hide_panel_count = 0;
        for (const auto& p : panels_) {
            if (!p.visible) continue;
            switch (p.state) {
                case DockPanelState::Docked:   ++out->docked_panel_count; break;
                case DockPanelState::Floating: ++out->floating_panel_count; break;
                case DockPanelState::AutoHide: ++out->auto_hide_panel_count; break;
                default: break;
            }
        }
    }

    int get_visible_dock_panels(DockPanelRenderInfo* out_items, int max_items) const override {
        int count = 0;
        for (const auto& p : panels_) {
            if (count >= max_items) break;
            if (!p.visible || p.state == DockPanelState::Hidden) continue;

            DockPanelRenderInfo& info = out_items[count];
            info.panel_id = p.id;
            info.title = p.title.c_str();
            info.icon_name = p.icon_name.empty() ? nullptr : p.icon_name.c_str();
            info.state = p.state;
            info.zone = p.zone;
            info.active = p.active;
            info.title_hovered = false;

            math::Box zone_rect = (p.state == DockPanelState::Floating)
                ? p.floating_bounds : get_zone_rect(p.zone);

            info.panel_rect = zone_rect;
            float zx = math::x(zone_rect.min_corner());
            float zy = math::y(zone_rect.min_corner());
            float zw = math::box_width(zone_rect);
            float zh = math::box_height(zone_rect);
            info.title_bar_rect = math::make_box(zx, zy, zw, style_.title_bar_height);
            info.content_rect = math::make_box(zx, zy + style_.title_bar_height,
                                                zw, zh - style_.title_bar_height);
            // Close button at top-right of title bar
            float btn_size = style_.title_bar_height - 4.0f;
            info.close_button_rect = math::make_box(
                zx + zw - btn_size - 2.0f, zy + 2.0f, btn_size, btn_size);

            ++count;
        }
        return count;
    }

private:
    using PanelIter = std::vector<DockPanelEntry>::iterator;
    using PanelConstIter = std::vector<DockPanelEntry>::const_iterator;

    PanelIter find_panel(int id) {
        return std::find_if(panels_.begin(), panels_.end(),
            [id](const DockPanelEntry& p) { return p.id == id; });
    }

    PanelConstIter find_panel_const(int id) const {
        return std::find_if(panels_.begin(), panels_.end(),
            [id](const DockPanelEntry& p) { return p.id == id; });
    }

    bool has_docked_in_zone(DockZone zone) const {
        for (const auto& p : panels_) {
            if (p.zone == zone && p.visible && p.state == DockPanelState::Docked)
                return true;
        }
        return false;
    }

    math::Box get_zone_rect(DockZone zone) const {
        switch (zone) {
            case DockZone::Left:   return left_rect_;
            case DockZone::Right:  return right_rect_;
            case DockZone::Top:    return top_rect_;
            case DockZone::Bottom: return bottom_rect_;
            case DockZone::Center: return center_rect_;
            default: return math::Box();
        }
    }

    math::Box get_panel_content_rect(const DockPanelEntry& p) const {
        math::Box zone_rect = (p.state == DockPanelState::Floating)
            ? p.floating_bounds : get_zone_rect(p.zone);
        float zx = math::x(zone_rect.min_corner());
        float zy = math::y(zone_rect.min_corner());
        float zw = math::box_width(zone_rect);
        float zh = math::box_height(zone_rect);
        return math::make_box(zx, zy + style_.title_bar_height,
                              zw, zh - style_.title_bar_height);
    }

    void recalculate_layout() {
        float bx = math::x(bounds_.min_corner());
        float by = math::y(bounds_.min_corner());
        float bw = math::box_width(bounds_);
        float bh = math::box_height(bounds_);

        // Calculate zone rects based on what zones have docked panels
        float left_w = has_docked_in_zone(DockZone::Left) ? zone_left_size_ : 0.0f;
        float right_w = has_docked_in_zone(DockZone::Right) ? zone_right_size_ : 0.0f;
        float top_h = has_docked_in_zone(DockZone::Top) ? zone_top_size_ : 0.0f;
        float bottom_h = has_docked_in_zone(DockZone::Bottom) ? zone_bottom_size_ : 0.0f;

        // Clamp to prevent overflow
        float available_w = bw - left_w - right_w;
        float available_h = bh - top_h - bottom_h;
        if (available_w < style_.min_dock_width) {
            float excess = style_.min_dock_width - available_w;
            left_w -= excess * 0.5f;
            right_w -= excess * 0.5f;
            left_w = std::max(0.0f, left_w);
            right_w = std::max(0.0f, right_w);
        }
        if (available_h < style_.min_dock_height) {
            float excess = style_.min_dock_height - available_h;
            top_h -= excess * 0.5f;
            bottom_h -= excess * 0.5f;
            top_h = std::max(0.0f, top_h);
            bottom_h = std::max(0.0f, bottom_h);
        }

        // Top/bottom span full width; left/right fill between top and bottom
        top_rect_ = math::make_box(bx, by, bw, top_h);
        bottom_rect_ = math::make_box(bx, by + bh - bottom_h, bw, bottom_h);

        float inner_y = by + top_h;
        float inner_h = bh - top_h - bottom_h;
        left_rect_ = math::make_box(bx, inner_y, left_w, inner_h);
        right_rect_ = math::make_box(bx + bw - right_w, inner_y, right_w, inner_h);

        center_rect_ = math::make_box(bx + left_w, inner_y,
                                       bw - left_w - right_w, inner_h);

        // Update content widget bounds
        for (auto& p : panels_) {
            if (!p.content || !p.visible || p.state == DockPanelState::Hidden) continue;
            if (p.state == DockPanelState::Floating) {
                p.content->set_bounds(p.floating_bounds);
            } else {
                math::Box content_rect = get_panel_content_rect(p);
                p.content->set_bounds(content_rect);
            }
        }

        if (center_content_) center_content_->set_bounds(center_rect_);
    }

    // State
    std::string name_;
    DockPanelStyle style_;
    GuiStyle widget_style_;
    IGuiWidget* parent_ = nullptr;
    math::Box bounds_;
    math::Vec2 min_size_ = math::Vec2(200.0f, 200.0f);
    math::Vec2 max_size_ = math::Vec2(1e12f, 1e12f);
    math::Box clip_rect_;
    bool clip_enabled_ = true;
    bool visible_ = true;
    bool enabled_ = true;
    WidgetState state_ = WidgetState::Normal;
    SizeMode width_mode_ = SizeMode::Fill;
    SizeMode height_mode_ = SizeMode::Fill;
    Alignment alignment_ = Alignment::TopLeft;
    IGuiEventHandler* event_handler_ = nullptr;
    IDockPanelEventHandler* dock_handler_ = nullptr;

    // Panels
    std::vector<DockPanelEntry> panels_;
    int next_id_ = 1;
    int active_panel_id_ = -1;
    IGuiWidget* center_content_ = nullptr;

    // Zone sizes
    float zone_left_size_ = 200.0f;
    float zone_right_size_ = 200.0f;
    float zone_top_size_ = 150.0f;
    float zone_bottom_size_ = 150.0f;

    // Computed rects
    math::Box left_rect_;
    math::Box right_rect_;
    math::Box top_rect_;
    math::Box bottom_rect_;
    math::Box center_rect_;

    // Drag/drop
    bool drag_docking_enabled_ = true;
    DockDropIndicatorInfo drop_indicator_;
};

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* split_orientation_to_string(SplitOrientation orientation) {
    switch (orientation) {
        case SplitOrientation::Horizontal: return "Horizontal";
        case SplitOrientation::Vertical:   return "Vertical";
        default:                           return "Unknown";
    }
}

const char* dock_zone_to_string(DockZone zone) {
    switch (zone) {
        case DockZone::Center: return "Center";
        case DockZone::Left:   return "Left";
        case DockZone::Right:  return "Right";
        case DockZone::Top:    return "Top";
        case DockZone::Bottom: return "Bottom";
        case DockZone::Float:  return "Float";
        default:               return "Unknown";
    }
}

const char* dock_panel_state_to_string(DockPanelState state) {
    switch (state) {
        case DockPanelState::Docked:   return "Docked";
        case DockPanelState::Floating: return "Floating";
        case DockPanelState::AutoHide: return "AutoHide";
        case DockPanelState::Hidden:   return "Hidden";
        default:                       return "Unknown";
    }
}

// ============================================================================
// Factory Functions
// ============================================================================

IGuiSplitPanel* create_split_panel(SplitOrientation orientation) {
    return new GuiSplitPanel(orientation);
}

IGuiDockPanel* create_dock_panel() {
    return new GuiDockPanel();
}

} // namespace gui
} // namespace window
