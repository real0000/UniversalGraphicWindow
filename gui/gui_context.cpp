/*
 * gui_context.cpp - GuiContext Implementation and Factory Functions
 *
 * Contains only the IGuiContext implementation and the public
 * create_gui_context/destroy_gui_context functions.
 * Widget implementations are in their respective gui_*.cpp files.
 * String conversions and apply_easing() are in gui.cpp.
 */

#include "gui_widget_base.hpp"
#include "../input/input_mouse.hpp"
#include "../input/input_keyboard.hpp"
#include "../window.hpp"
#include <algorithm>
#include <memory>

namespace window {
namespace gui {

// ============================================================================
// Forward declarations for widget factory functions (in separate .cpp files)
// ============================================================================

// gui_label.cpp
IGuiLabel* create_label_widget();
IGuiTextInput* create_text_input_widget();
IGuiEditBox* create_editbox_widget();

// gui_controls.cpp
IGuiButton* create_button_widget(ButtonType type);
IGuiImage* create_image_widget();
IGuiSlider* create_slider_widget(SliderOrientation orient);
IGuiProgressBar* create_progress_bar_widget(ProgressBarMode mode);
IGuiColorPicker* create_color_picker_widget(ColorPickerMode mode);

// gui_scroll.cpp
IGuiScrollBar* create_scroll_bar_widget(ScrollBarOrientation orient);
IGuiScrollView* create_scroll_view_widget();

// gui_list.cpp
IGuiListBox* create_list_box_widget();
IGuiCanvasView* create_canvas_view_widget();
IGuiCollapseSection* create_collapse_section_widget();
IGuiComboBox* create_combo_box_widget();

// gui_tree.cpp
IGuiTreeView* create_tree_view_widget();

// gui_tab.cpp
IGuiTabControl* create_tab_control_widget(TabPosition pos);

// gui_property.cpp
IGuiPropertyGrid* create_property_grid_widget();

// gui_dialog.cpp
IGuiDialog* create_dialog_widget(DialogButtons buttons);
IGuiPopup* create_popup_widget();

// gui_menu.cpp
IGuiMenu* create_menu_widget();
IGuiMenuBar* create_menu_bar_widget();

// gui_toolbar.cpp
IGuiToolbar* create_toolbar_widget(ToolbarOrientation orient);
IGuiStatusBar* create_status_bar_widget();

// gui_panel.cpp
IGuiSplitPanel* create_split_panel(SplitOrientation orientation);
IGuiDockPanel* create_dock_panel();

// gui_page.cpp
IGuiPage* create_page_widget();
IGuiPageView* create_page_view_widget();

// gui_animation.cpp
IGuiAnimationManager* create_animation_manager_widget();

// ============================================================================
// GuiContext
// ============================================================================

class GuiContext : public IGuiContext {
    bool initialized_=false;
    GuiWidget root_{WidgetType::Container};
    IGuiWidget* focused_=nullptr;
    std::vector<IGuiWidget*> modal_stack_;
    std::vector<Viewport> viewports_;
    GuiInputState input_state_;
    int cur_mods_ = 0;    // latest platform key-modifier bitmask (from mouse/key events)
    GuiStyle default_style_=GuiStyle::default_style();
    LabelStyle default_label_style_=LabelStyle::default_style();
    ITextMeasurer* text_measurer_=nullptr;
    IGuiTextRasterizer* text_rasterizer_=nullptr;
    std::unique_ptr<IGuiAnimationManager> anim_mgr_;
    bool debug_draw_=false;
    std::vector<std::unique_ptr<IGuiWidget>> owned_widgets_;
    IGuiLabel* tooltip_=nullptr;
    bool tooltip_visible_=false;
    std::vector<IGuiWidget*> overlays_;
    mutable WidgetRenderInfo frame_ri_;
    Window* attached_window_=nullptr;
    float window_dpi_scale_=1.0f;  // Mirror of attached window's DPI scale (input to_ui path)
    float ui_scale_=1.0f;          // global render/layout scale (see set_ui_scale); 1 = logical==physical
    GuiTheme theme_=GuiTheme::dark();  // semantic role→colour palette (see set_theme); resolved at collect

    // ---- Event-driven host (see set_host_window / post / pump) --------------
    Window* host_=nullptr;             // window whose loop drives us (may differ from attached_window_)
    bool needs_layout_=true;           // root sizer tree must re-flow before next render
    bool suppress_dirty_notify_=false; // guard: relayout marks widgets dirty; don't re-arm during paint
    int  last_win_w_=-1, last_win_h_=-1;  // last window size we cascaded to the root
    int  blink_timer_id_=0;            // caret-blink timer id while a text widget is focused
    std::function<bool()> pre_render_; // measure hook (after layout, before collect)

    // Push a repaint (and re-layout) request to the host loop. Fired from the root
    // widget's dirty listener, so ANY widget change schedules exactly one re-layout
    // + repaint with no app involvement.
    void notify_dirty() {
        if (suppress_dirty_notify_) return;
        needs_layout_ = true;
        if (host_) host_->request_redraw();
    }
    // Arm/disarm the caret-blink timer to match the focused widget. Only a focused
    // editable text widget needs a periodic repaint; everything else stays idle.
    void update_blink_timer() {
        const bool want = host_ && focused_ && focused_->wants_caret_blink();
        if (want && !blink_timer_id_) {
            // 500 ms == half of the 1 s caret square wave, so consecutive samples
            // always land in opposite halves → a clean on/off blink.
            blink_timer_id_ = host_->add_timer(500, true, [this] {
                if (host_) host_->request_redraw();   // repaint only, no re-layout
            });
        } else if (!want && blink_timer_id_) {
            host_->remove_timer(blink_timer_id_);
            blink_timer_id_ = 0;
        }
    }

    // Intersect two clip rects. An empty (zero-area) box means "no clip", so it is the
    // identity — intersecting with it returns the other.
    static math::Box clip_isect(const math::Box& a, const math::Box& b) {
        if (math::box_is_empty(a)) return b;
        if (math::box_is_empty(b)) return a;
        float ax0 = math::x(math::box_min(a)), ay0 = math::y(math::box_min(a));
        float ax1 = ax0 + math::box_width(a),  ay1 = ay0 + math::box_height(a);
        float bx0 = math::x(math::box_min(b)), by0 = math::y(math::box_min(b));
        float bx1 = bx0 + math::box_width(b),  by1 = by0 + math::box_height(b);
        float ix0 = std::max(ax0, bx0), iy0 = std::max(ay0, by0);
        float ix1 = std::min(ax1, bx1), iy1 = std::min(ay1, by1);
        if (ix1 <= ix0 || iy1 <= iy0) return math::make_box(ix0, iy0, 0, 0);  // empty → fully clipped
        return math::make_box(ix0, iy0, ix1 - ix0, iy1 - iy0);
    }
    // Accumulated content transform along the ancestor chain: a command point p in
    // the current widget's space lands on screen at p*scale + (ox, oy). Identity
    // (scale 1, offset 0) for every widget outside a transformed container, so the
    // common path stays a plain copy. text_min_px culls text whose transformed font
    // drops below that many screen px (0 = never) — set via content_text_min_px().
    struct CollectXf {
        float scale = 1.0f;
        float ox = 0.0f, oy = 0.0f;
        float text_min_px = 0.0f;
        const GuiTheme* theme = nullptr;   // resolves ColorCmd/TextCmd roles → colour
        bool identity() const { return scale == 1.0f && ox == 0.0f && oy == 0.0f; }
        math::Box box(const math::Box& b) const {
            if (math::box_is_empty(b)) return b;   // empty = "no clip" identity — keep it
            return math::make_box(math::x(math::box_min(b)) * scale + ox,
                                  math::y(math::box_min(b)) * scale + oy,
                                  math::box_width(b) * scale, math::box_height(b) * scale);
        }
    };

    // Fill a themed command's colour from the palette. A role of None leaves the
    // literal colour the widget pushed; anything else is looked up in the theme, so
    // the widget layer never holds a chrome colour value (see gui_theme.hpp).
    static void resolve_role(WidgetRenderInfo::ColorCmd& c, const GuiTheme* th) {
        if (th && c.role != GuiColor::None) c.color = th->get(c.role);
    }
    static void resolve_role(WidgetRenderInfo::TextCmd& c, const GuiTheme* th) {
        if (th && c.role != GuiColor::None) c.color = th->get(c.role);
    }

    // `parent_clip` is the clip imposed by clip-enabled ancestors (empty = none), in
    // SCREEN space. A widget's own commands are clamped to it; clip-enabled widgets
    // (e.g. ScrollView) tighten the clip handed to their descendants — so scrolled
    // content stays bounded. `xf` maps this widget's coordinate space to the screen.
    static void collect_recursive(IGuiWidget* w, WidgetRenderInfo& out, int32_t& depth,
                                  const math::Box& parent_clip, const CollectXf& xf) {
        if (!w || !w->is_visible()) return;
        if (math::box_is_empty(w->get_bounds())) return;
        w->refresh_bindings();   // pull any bound data provider before rendering
        const WidgetRenderInfo& ri = w->get_render_info(nullptr);
        if (!ri.is_valid()) return;
        int32_t local_max = 0;
        for (const auto& ref : ri.get_draw_order())
            if (ref.depth > local_max) local_max = ref.depth;
        int32_t base = depth;
        if (xf.identity()) {
            for (auto cmd : ri.colors)   { cmd.depth += base; cmd.clip = clip_isect(cmd.clip, parent_clip); resolve_role(cmd, xf.theme); out.colors.push_back(cmd); }
            for (auto cmd : ri.textures) { cmd.depth += base; cmd.clip = clip_isect(cmd.clip, parent_clip); out.textures.push_back(cmd); }
            for (auto cmd : ri.slices)   { cmd.depth += base; cmd.clip = clip_isect(cmd.clip, parent_clip); out.slices.push_back(cmd); }
            for (auto cmd : ri.texts)    { cmd.depth += base; cmd.clip = clip_isect(cmd.clip, parent_clip); resolve_role(cmd, xf.theme); out.texts.push_back(cmd); }
        } else {
            // Transformed subtree: scale + translate every geometric field so the
            // widget renders exactly as if it had been laid out in screen space.
            for (auto cmd : ri.colors) {
                cmd.depth += base;
                cmd.dest = xf.box(cmd.dest);
                cmd.clip = clip_isect(xf.box(cmd.clip), parent_clip);
                cmd.corner_radius *= xf.scale;
                if (cmd.shape == DrawShape::Line) {
                    cmd.line_x1 = cmd.line_x1 * xf.scale + xf.ox;
                    cmd.line_y1 = cmd.line_y1 * xf.scale + xf.oy;
                    cmd.line_w *= xf.scale;
                }
                resolve_role(cmd, xf.theme);
                out.colors.push_back(cmd);
            }
            for (auto cmd : ri.textures) {
                cmd.depth += base;
                cmd.dest = xf.box(cmd.dest);
                cmd.clip = clip_isect(xf.box(cmd.clip), parent_clip);
                out.textures.push_back(cmd);
            }
            for (auto cmd : ri.slices) {
                cmd.depth += base;
                cmd.dest = xf.box(cmd.dest);
                cmd.clip = clip_isect(xf.box(cmd.clip), parent_clip);
                cmd.border.left *= xf.scale; cmd.border.top *= xf.scale;
                cmd.border.right *= xf.scale; cmd.border.bottom *= xf.scale;
                out.slices.push_back(cmd);
            }
            for (auto cmd : ri.texts) {
                cmd.font_size *= xf.scale;
                if (xf.text_min_px > 0.0f && cmd.font_size < xf.text_min_px) continue;
                cmd.depth += base;
                cmd.dest = xf.box(cmd.dest);
                cmd.clip = clip_isect(xf.box(cmd.clip), parent_clip);
                resolve_role(cmd, xf.theme);
                out.texts.push_back(cmd);
            }
        }
        if (!ri.get_draw_order().empty()) depth = base + local_max + 1;
        const math::Box child_clip = w->is_clip_enabled()
            ? clip_isect(parent_clip, xf.box(w->get_clip_rect())) : parent_clip;
        CollectXf cxf = xf;
        if (w->content_scale() != 1.0f || math::x(w->content_offset()) != 0.0f ||
            math::y(w->content_offset()) != 0.0f || w->content_text_min_px() > 0.0f) {
            // Compose: screen = (child*s2 + o2)*scale + offset
            const float s2 = w->content_scale();
            cxf.ox = xf.ox + xf.scale * math::x(w->content_offset());
            cxf.oy = xf.oy + xf.scale * math::y(w->content_offset());
            cxf.scale = xf.scale * s2;
            cxf.text_min_px = std::max(xf.text_min_px, w->content_text_min_px());
        }
        for (int i = 0; i < w->get_child_count(); ++i)
            collect_recursive(w->get_child(i), out, depth, child_clip, cxf);
    }

    // Find the deepest visible focusable widget at pos within the given subtree.
    // pos is in the WIDGET's coordinate space; content transforms map it into the
    // children's space on the way down (mirrors GuiWidget::find_widget_at).
    static IGuiWidget* find_focusable_at(IGuiWidget* w, const math::Vec2& pos) {
        if (!w || !w->is_visible()) return nullptr;
        if (!w->hit_test(pos)) return nullptr;
        math::Vec2 cpos = pos;
        const float cs = w->content_scale();
        const math::Vec2 co = w->content_offset();
        if (cs != 1.0f || math::x(co) != 0.0f || math::y(co) != 0.0f) {
            const float inv = 1.0f / cs;
            cpos = math::Vec2((math::x(pos) - math::x(co)) * inv,
                              (math::y(pos) - math::y(co)) * inv);
        }
        for (int i = w->get_child_count() - 1; i >= 0; --i) {
            if (auto* f = find_focusable_at(w->get_child(i), cpos)) return f;
        }
        return w->is_focusable() ? w : nullptr;
    }

    // Mouse handler: feeds all mouse events into the widget tree.
    // Window events arrive in physical px; widget bounds are in logical
    // (UI) px. Divide by the attached window's DPI scale before dispatching
    // so hit-tests align.
    class MouseInputHandler : public input::IMouseHandler {
        GuiContext* ctx_;
        math::Vec2 to_ui(int x, int y) const {
            float s = ctx_->window_dpi_scale_;
            if (s <= 0.0f) s = 1.0f;
            return math::Vec2((float)x / s, (float)y / s);
        }
    public:
        explicit MouseInputHandler(GuiContext* ctx) : ctx_(ctx) {}
        const char* get_handler_id() const override { return "gui_context_mouse"; }
        int get_priority() const override { return 100; }
        bool on_mouse_move(const MouseMoveEvent& event) override {
            ctx_->input_state_.mouse_position = to_ui(event.x, event.y);
            ctx_->dispatch_mouse_move(ctx_->input_state_.mouse_position);
            return false;
        }
        bool on_mouse_button(const MouseButtonEvent& event) override {
            if (event.button == window::MouseButton::Unknown) return false;
            bool pressed = (event.type == EventType::MouseDown);
            gui::MouseButton btn = static_cast<gui::MouseButton>(static_cast<uint8_t>(event.button));
            math::Vec2 pos = to_ui(event.x, event.y);
            ctx_->cur_mods_ = static_cast<int>(event.modifiers);   // for widgets that need shift/ctrl (canvas)
            ctx_->dispatch_mouse_button(btn, pressed, pos);
            return false; // don't consume: let other handlers see it
        }
        bool on_mouse_wheel(const MouseWheelEvent& event) override {
            ctx_->input_state_.scroll_delta_x += event.dx;
            ctx_->input_state_.scroll_delta_y += event.dy;
            return false;
        }
    } mouse_handler_{this};

    // Keyboard handler: forwards key/char events to the focused widget
    class KeyboardInputHandler : public input::IKeyboardHandler {
        GuiContext* ctx_;
    public:
        explicit KeyboardInputHandler(GuiContext* ctx) : ctx_(ctx) {}
        const char* get_handler_id() const override { return "gui_context_keyboard"; }
        int get_priority() const override { return 100; }
        bool on_key(const KeyEvent& event) override {
            if (!ctx_->focused_) return false;
            bool pressed = (event.type == EventType::KeyDown || event.type == EventType::KeyRepeat);
            int mods = static_cast<int>(event.modifiers);
            return ctx_->focused_->handle_key(static_cast<int>(event.key), pressed, mods);
        }
        bool on_char(const CharEvent& event) override {
            if (!ctx_->focused_) return false;
            uint32_t cp = event.codepoint;
            if (cp < 32) return false; // skip control chars
            char buf[8] = {};
            if      (cp < 0x80)    { buf[0] = (char)cp; }
            else if (cp < 0x800)   { buf[0] = (char)(0xC0|(cp>>6));   buf[1] = (char)(0x80|(cp&0x3F)); }
            else if (cp < 0x10000) { buf[0] = (char)(0xE0|(cp>>12));  buf[1] = (char)(0x80|((cp>>6)&0x3F));  buf[2] = (char)(0x80|(cp&0x3F)); }
            else                   { buf[0] = (char)(0xF0|(cp>>18));  buf[1] = (char)(0x80|((cp>>12)&0x3F)); buf[2] = (char)(0x80|((cp>>6)&0x3F)); buf[3] = (char)(0x80|(cp&0x3F)); }
            return ctx_->focused_->handle_text_input(buf);
        }
        void on_preedit(const std::string& text, int cursor_codepoints) override {
            if (ctx_->focused_) ctx_->focused_->handle_preedit(text.c_str(), cursor_codepoints);   // IME → focused editbox
        }
    } keyboard_handler_{this};
public:
    GuiResult initialize() override {
        initialized_=true;
        root_.set_name("root");
        // Any descendant marking dirty bubbles to the root; this sink turns that
        // into one scheduled re-layout + repaint on the host loop (event-driven).
        root_.set_dirty_listener([this] { notify_dirty(); });
        anim_mgr_.reset(create_animation_manager_widget());
        return GuiResult::Success;
    }
    void shutdown() override {
        if (attached_window_) detach_window(attached_window_);
        if (host_ && blink_timer_id_) host_->remove_timer(blink_timer_id_);
        blink_timer_id_ = 0; host_ = nullptr;
        owned_widgets_.clear(); modal_stack_.clear(); focused_=nullptr; anim_mgr_.reset(); initialized_=false;
    }
    bool is_initialized() const override { return initialized_; }

    // Depth-first: try deepest visible widget under pos first, then walk up
    static bool scroll_recursive(IGuiWidget* w, float dx, float dy, const math::Vec2& pos) {
        if (!w || !w->is_visible()) return false;
        if (!math::box_contains(w->get_bounds(), pos)) return false;
        for (int i = w->get_child_count() - 1; i >= 0; --i)
            if (scroll_recursive(w->get_child(i), dx, dy, pos)) return true;
        return w->handle_mouse_scroll(dx, dy);
    }

    void begin_frame(float dt) override {
        if (anim_mgr_) anim_mgr_->update(dt);
        if (input_state_.scroll_delta_x != 0 || input_state_.scroll_delta_y != 0) {
            dispatch_scroll(input_state_.scroll_delta_x, input_state_.scroll_delta_y,
                            input_state_.mouse_position);
            input_state_.scroll_delta_x = 0;
            input_state_.scroll_delta_y = 0;
        }
    }
    void end_frame() override {}

    bool dispatch_scroll(float dx, float dy, const math::Vec2& pos) override {
        return scroll_recursive(&root_, dx, dy, pos);
    }

    void dispatch_mouse_move(const math::Vec2& pos) override {
        root_.handle_mouse_move(pos);
        for (auto* ov : overlays_) if (ov) ov->handle_mouse_move(pos);
    }

    bool dispatch_mouse_button(MouseButton btn, bool pressed, const math::Vec2& pos) override {
        bool consumed = false;
        if (!modal_stack_.empty()) {
            // Modal: only route to the top modal widget
            consumed = modal_stack_.back()->handle_mouse_button(btn, pressed, pos);
        } else {
            // Overlays first (reverse z-order: last registered = topmost)
            for (int i = (int)overlays_.size() - 1; i >= 0; --i) {
                if (overlays_[i] && overlays_[i]->is_visible() &&
                    overlays_[i]->handle_mouse_button(btn, pressed, pos)) {
                    consumed = true;
                    break;
                }
            }
            if (!consumed) consumed = root_.handle_mouse_button(btn, pressed, pos);
        }
        // Update focus on left press
        if (btn == MouseButton::Left && pressed) {
            IGuiWidget* new_focus = nullptr;
            if (!modal_stack_.empty()) {
                new_focus = find_focusable_at(modal_stack_.back(), pos);
            } else {
                for (int i = (int)overlays_.size() - 1; i >= 0; --i) {
                    if (overlays_[i] && overlays_[i]->is_visible()) {
                        new_focus = find_focusable_at(overlays_[i], pos);
                        if (new_focus) break;
                    }
                }
                if (!new_focus) new_focus = find_focusable_at(&root_, pos);
            }
            // Only update focus when clicking on a focusable widget.
            // Clicking on non-focusable areas (e.g. canvas) preserves current focus
            // so property grid / text inputs keep receiving keyboard events.
            if (new_focus != nullptr && new_focus != focused_) {
                if (focused_) focused_->set_focus(false);
                focused_ = new_focus;
                if (focused_) focused_->set_focus(true);
                update_blink_timer();
            }
        }
        return consumed;
    }

    void attach_window(Window* win) override {
        if (!win || attached_window_ == win) return;
        if (attached_window_) detach_window(attached_window_);
        win->add_mouse_handler(&mouse_handler_);
        win->add_keyboard_handler(&keyboard_handler_);
        attached_window_ = win;
        // Apply the window's current DPI scale to all viewports so widget
        // hit-testing/rendering operate in physical px.
        apply_dpi_scale_to_viewports(win->get_dpi_scale());
        // Track future DPI changes (e.g. window dragged to a HiDPI monitor).
        win->set_dpi_change_callback([this](const DpiChangeEvent& ev) {
            apply_dpi_scale_to_viewports(ev.scale);
        });
    }
    void detach_window(Window* win) override {
        if (!win || attached_window_ != win) return;
        win->remove_mouse_handler(&mouse_handler_);
        win->remove_keyboard_handler(&keyboard_handler_);
        win->set_dpi_change_callback({});
        attached_window_ = nullptr;
    }

    void set_host_window(Window* win) override {
        if (host_ == win) return;
        if (host_ && blink_timer_id_) { host_->remove_timer(blink_timer_id_); blink_timer_id_ = 0; }
        host_ = win;
        last_win_w_ = last_win_h_ = -1;   // force a root cascade on the next render
        needs_layout_ = true;
        update_blink_timer();
    }

    float get_ui_scale() const override { return ui_scale_; }
    void set_ui_scale(float scale) override {
        if (scale <= 0.0f) scale = 1.0f;
        if (scale == ui_scale_) return;
        ui_scale_ = scale;
        last_win_w_ = last_win_h_ = -1;   // re-derive the logical root size next render
        needs_layout_ = true;
        if (host_) host_->request_redraw();
    }
    math::Vec2 to_logical(const math::Vec2& p) const override {
        const float s = ui_scale_ > 0.0f ? ui_scale_ : 1.0f;
        return math::Vec2(math::x(p) / s, math::y(p) / s);
    }
    math::Vec2 to_physical(const math::Vec2& p) const override {
        return math::Vec2(math::x(p) * ui_scale_, math::y(p) * ui_scale_);
    }

    const GuiTheme& get_theme() const override { return theme_; }
    void set_theme(const GuiTheme& theme) override {
        theme_ = theme;
        // Colours are resolved at collect from cached (unchanged) draw commands, so
        // force a rebuild of the whole tree's render info to pick up the new palette.
        root_.mark_dirty();
        for (auto* ov : overlays_) if (ov) ov->mark_dirty();
        if (host_) host_->request_redraw();
    }

    void post(std::function<void()> fn) override {
        if (!fn) return;
        if (host_) {
            // Run on the UI thread; a posted mutation may change sizing, so mark
            // for re-layout and repaint. request_redraw() also happens via the
            // widgets' dirty listener, but request it explicitly in case the task
            // only toggles visibility on an already-dirty-clean subtree.
            host_->post_task([this, fn = std::move(fn)]() mutable {
                fn();
                needs_layout_ = true;
                if (host_) host_->request_redraw();
            });
        } else {
            // Headless / no host bound: apply immediately; the caller's next
            // synchronous render picks it up.
            fn();
            needs_layout_ = true;
        }
    }

    void pump() override { if (host_) host_->run_pending(); }

    void set_pre_render(std::function<bool()> hook) override { pre_render_ = std::move(hook); }

    void apply_dpi_scale_to_viewports(float scale) {
        if (scale <= 0.0f) scale = 1.0f;
        window_dpi_scale_ = scale;
        for (auto& v : viewports_) v.scale = scale;
    }

    void add_overlay(IGuiWidget* w) override { if (w) overlays_.push_back(w); }
    void remove_overlay(IGuiWidget* w) override {
        overlays_.erase(std::remove(overlays_.begin(), overlays_.end(), w), overlays_.end());
    }
    const WidgetRenderInfo& get_render_info() override {
        // ---- Automatic layout (event-driven; app never calls cascade/relayout) --
        // Keep the root filling the host window, and re-flow the sizer tree if any
        // widget marked dirty since the last render. Done here (once per render,
        // only when something changed) instead of every frame.
        if (host_) {
            int w = 0, h = 0; host_->get_size(&w, &h);   // physical px
            float s = ui_scale_; if (s <= 0.0f) s = 1.0f;
            if (w != last_win_w_ || h != last_win_h_) {
                last_win_w_ = w; last_win_h_ = h;
                // Root fills the window in LOGICAL px; collect scales draws by ui_scale.
                root_.set_bounds(math::make_box(0.0f, 0.0f, (float)w / s, (float)h / s));
                needs_layout_ = false;   // set_bounds just re-flowed everything
            }
        }
        if (needs_layout_) {
            // Re-flow over the CURRENT bounds. Suppress the dirty sink so the
            // widgets marked dirty by this layout pass don't schedule another one.
            suppress_dirty_notify_ = true;
            root_.set_bounds(root_.get_bounds());
            suppress_dirty_notify_ = false;
            needs_layout_ = false;
        }
        // Measure hook: content sized to the just-laid-out width (e.g. scroll
        // content height). If it changed sizes, settle with one more re-flow.
        if (pre_render_) {
            suppress_dirty_notify_ = true;
            const bool changed = pre_render_();
            if (changed) root_.set_bounds(root_.get_bounds());
            suppress_dirty_notify_ = false;
        }

        frame_ri_.invalidate();
        int32_t depth = 0;
        const math::Box noclip = math::make_box(0, 0, 0, 0);   // top level: no ancestor clip
        // Root transform = the global UI scale: every collected draw (incl. font
        // sizes → crisp glyphs) is scaled to physical px. 1.0 = logical==physical.
        CollectXf root_xf;
        root_xf.scale = (ui_scale_ > 0.0f) ? ui_scale_ : 1.0f;
        root_xf.theme = &theme_;   // resolves widgets' semantic colour roles → colours
        collect_recursive(&root_, frame_ri_, depth, noclip, root_xf);
        for (auto* ov : overlays_) collect_recursive(ov, frame_ri_, depth, noclip, root_xf);
        frame_ri_.finalize();
        if (text_rasterizer_)
            frame_ri_.flatten(text_rasterizer_);
        return frame_ri_;
    }

    GuiResult add_viewport(const Viewport& vp) override {
        for(auto& v:viewports_) if(v.id==vp.id) return GuiResult::ErrorInvalidParameter;
        viewports_.push_back(vp);
        // If a window is attached, override the viewport's scale with the
        // current DPI scale so callers don't have to pass it explicitly.
        if (attached_window_) viewports_.back().scale = window_dpi_scale_;
        return GuiResult::Success;
    }
    GuiResult remove_viewport(int id) override {
        auto it=std::find_if(viewports_.begin(),viewports_.end(),[id](const Viewport& v){return v.id==id;});
        if(it==viewports_.end()) return GuiResult::ErrorViewportNotFound;
        viewports_.erase(it); return GuiResult::Success;
    }
    GuiResult update_viewport(const Viewport& vp) override {
        for(auto& v:viewports_) if(v.id==vp.id){
            v=vp;
            // Don't let callers reset the DPI-driven scale; the context owns it.
            if (attached_window_) v.scale = window_dpi_scale_;
            return GuiResult::Success;
        }
        return GuiResult::ErrorViewportNotFound;
    }
    const Viewport* get_viewport(int id) const override {
        for(auto& v:viewports_) if(v.id==id) return &v;
        return nullptr;
    }

    void set_input_state(int, const GuiInputState& s) override { input_state_=s; }
    const GuiInputState& get_input_state() const override { return input_state_; }

    IGuiWidget* get_root() override { return &root_; }

    IGuiWidget* get_focused_widget() const override { return focused_; }
    void set_focused_widget(IGuiWidget* w) override { focused_=w; update_blink_timer(); }
    void clear_focus() override { focused_=nullptr; update_blink_timer(); }

    void get_widgets_in_box(const math::Box& box, std::vector<IGuiWidget*>& out) override {
        collect_in_box(&root_, box, out);
    }

    void set_text_measurer(ITextMeasurer* m) override { text_measurer_=m; }
    ITextMeasurer* get_text_measurer() const override { return text_measurer_; }
    void set_text_rasterizer(IGuiTextRasterizer* r) override { text_rasterizer_=r; }
    IGuiTextRasterizer* get_text_rasterizer() const override { return text_rasterizer_; }

    const GuiStyle& get_default_style() const override { return default_style_; }
    void set_default_style(const GuiStyle& s) override { default_style_=s; }
    const LabelStyle& get_default_label_style() const override { return default_label_style_; }
    void set_default_label_style(const LabelStyle& s) override { default_label_style_=s; }

    // Factory methods - delegate to per-file factory functions
    IGuiWidget* create_widget(WidgetType type) override {
        auto w=std::make_unique<GuiWidget>(type); auto* p=w.get(); owned_widgets_.push_back(std::move(w)); return p;
    }
    IGuiButton* create_button(ButtonType type) override {
        auto* p=create_button_widget(type); owned_widgets_.emplace_back(p); return p;
    }
    IGuiLabel* create_label(const char* text) override {
        auto* p=create_label_widget(); if(text) p->set_text(text);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiTextInput* create_text_input(const char* placeholder) override {
        auto* p=create_text_input_widget(); if(placeholder) p->set_placeholder(placeholder);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiEditBox* create_editbox() override {
        auto* p=create_editbox_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiImage* create_image(const std::string& name) override {
        auto* p=create_image_widget(); if(!name.empty()) p->set_image_name(name);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiScrollView* create_scroll_view() override {
        auto* p=create_scroll_view_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiScrollBar* create_scroll_bar(ScrollBarOrientation orient) override {
        auto* p=create_scroll_bar_widget(orient); owned_widgets_.emplace_back(p); return p;
    }
    IGuiPropertyGrid* create_property_grid() override {
        auto* p=create_property_grid_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiTreeView* create_tree_view() override {
        auto* p=create_tree_view_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiTabControl* create_tab_control(TabPosition pos) override {
        auto* p=create_tab_control_widget(pos); owned_widgets_.emplace_back(p); return p;
    }
    IGuiListBox* create_list_box() override {
        auto* p=create_list_box_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiCanvasView* create_canvas_view() override {
        auto* p=create_canvas_view_widget(); owned_widgets_.emplace_back(p);
        p->set_modifier_provider([this]{ return cur_mods_; });   // feed shift/ctrl to canvas hit-testing
        return p;
    }
    IGuiCollapseSection* create_collapse_section() override {
        auto* p=create_collapse_section_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiComboBox* create_combo_box() override {
        auto* p=create_combo_box_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiDialog* create_dialog(const char* title, DialogButtons buttons) override {
        auto* p=create_dialog_widget(buttons); if(title) p->set_title(title);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiPopup* create_popup() override {
        auto* p=create_popup_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiMenu* create_menu() override {
        auto* p=create_menu_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiMenuBar* create_menu_bar() override {
        auto* p=create_menu_bar_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiToolbar* create_toolbar(ToolbarOrientation orient) override {
        auto* p=create_toolbar_widget(orient); owned_widgets_.emplace_back(p); return p;
    }
    IGuiStatusBar* create_status_bar() override {
        auto* p=create_status_bar_widget(); owned_widgets_.emplace_back(p); return p;
    }
    IGuiSplitPanel* create_split_panel(SplitOrientation orient) override {
        auto* p=::window::gui::create_split_panel(orient);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiDockPanel* create_dock_panel() override {
        auto* p=::window::gui::create_dock_panel();
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiSlider* create_slider(SliderOrientation orient) override {
        auto* p=create_slider_widget(orient); owned_widgets_.emplace_back(p); return p;
    }
    IGuiProgressBar* create_progress_bar(ProgressBarMode mode) override {
        auto* p=create_progress_bar_widget(mode); owned_widgets_.emplace_back(p); return p;
    }
    IGuiColorPicker* create_color_picker(ColorPickerMode mode) override {
        auto* p=create_color_picker_widget(mode); owned_widgets_.emplace_back(p); return p;
    }
    IGuiPage* create_page(const char* page_id) override {
        auto* p=create_page_widget(); if(page_id) p->set_page_id(page_id);
        owned_widgets_.emplace_back(p); return p;
    }
    IGuiPageView* create_page_view() override {
        auto* p=create_page_view_widget(); owned_widgets_.emplace_back(p); return p;
    }
    void destroy_widget(IGuiWidget* widget) override {
        owned_widgets_.erase(std::remove_if(owned_widgets_.begin(),owned_widgets_.end(),
            [widget](const auto& w){return w.get()==widget;}),owned_widgets_.end());
    }

    void push_modal(IGuiWidget* w) override { if(w) modal_stack_.push_back(w); }
    void pop_modal() override { if(!modal_stack_.empty()) modal_stack_.pop_back(); }
    IGuiWidget* get_modal() const override { return modal_stack_.empty()?nullptr:modal_stack_.back(); }

    void show_tooltip(const char* text, const math::Vec2& pos) override {
        if(!tooltip_) tooltip_=create_label(nullptr);
        tooltip_->set_text(text?text:"");
        tooltip_->set_bounds(math::make_box(math::x(pos),math::y(pos),math::x(pos)+200,math::y(pos)+24));
        tooltip_visible_=true;
    }
    void hide_tooltip() override { tooltip_visible_=false; }

    void set_debug_draw(bool e) override { debug_draw_=e; }
    bool is_debug_draw_enabled() const override { return debug_draw_; }

    IGuiAnimationManager* get_animation_manager() override { return anim_mgr_.get(); }

private:
    void collect_in_box(IGuiWidget* w, const math::Box& box, std::vector<IGuiWidget*>& out) {
        if(!w||!w->is_visible()) return;
        if(math::box_contains(box, math::box_min(w->get_bounds())) ||
           math::box_contains(box, math::box_max(w->get_bounds())))
            out.push_back(w);
        // Children of a transformed container live in their own space: map the
        // query box through the inverse transform before descending.
        math::Box cbox = box;
        const float cs = w->content_scale();
        const math::Vec2 co = w->content_offset();
        if (cs != 1.0f || math::x(co) != 0.0f || math::y(co) != 0.0f) {
            const float inv = 1.0f / cs;
            cbox = math::make_box((math::x(math::box_min(box)) - math::x(co)) * inv,
                                  (math::y(math::box_min(box)) - math::y(co)) * inv,
                                  math::box_width(box) * inv, math::box_height(box) * inv);
        }
        for(int i=0;i<w->get_child_count();++i) collect_in_box(w->get_child(i),cbox,out);
    }
};

// ============================================================================
// Factory Functions
// ============================================================================

IGuiContext* create_gui_context(GuiResult* out_result) {
    auto* ctx = new(std::nothrow) GuiContext();
    if(!ctx) { if(out_result) *out_result=GuiResult::ErrorOutOfMemory; return nullptr; }
    auto r = ctx->initialize();
    if(out_result) *out_result=r;
    if(r!=GuiResult::Success) { delete ctx; return nullptr; }
    return ctx;
}

void destroy_gui_context(IGuiContext* context) {
    if(context) { context->shutdown(); delete context; }
}

} // namespace gui
} // namespace window
