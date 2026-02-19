/*
 * gui_context.cpp - GuiContext Implementation and Factory Functions
 *
 * Contains only the IGuiContext implementation and the public
 * create_gui_context/destroy_gui_context functions.
 * Widget implementations are in their respective gui_*.cpp files.
 * String conversions and apply_easing() are in gui.cpp.
 */

#include "gui_widget_base.hpp"
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
    GuiStyle default_style_=GuiStyle::default_style();
    LabelStyle default_label_style_=LabelStyle::default_style();
    ITextMeasurer* text_measurer_=nullptr;
    std::unique_ptr<IGuiAnimationManager> anim_mgr_;
    bool debug_draw_=false;
    std::vector<std::unique_ptr<IGuiWidget>> owned_widgets_;
    IGuiLabel* tooltip_=nullptr;
    bool tooltip_visible_=false;
public:
    GuiResult initialize() override {
        initialized_=true;
        root_.set_name("root");
        anim_mgr_.reset(create_animation_manager_widget());
        return GuiResult::Success;
    }
    void shutdown() override { owned_widgets_.clear(); modal_stack_.clear(); focused_=nullptr; anim_mgr_.reset(); initialized_=false; }
    bool is_initialized() const override { return initialized_; }

    void begin_frame(float dt) override { if(anim_mgr_) anim_mgr_->update(dt); }
    void end_frame() override {}

    GuiResult add_viewport(const Viewport& vp) override {
        for(auto& v:viewports_) if(v.id==vp.id) return GuiResult::ErrorInvalidParameter;
        viewports_.push_back(vp); return GuiResult::Success;
    }
    GuiResult remove_viewport(int id) override {
        auto it=std::find_if(viewports_.begin(),viewports_.end(),[id](const Viewport& v){return v.id==id;});
        if(it==viewports_.end()) return GuiResult::ErrorViewportNotFound;
        viewports_.erase(it); return GuiResult::Success;
    }
    GuiResult update_viewport(const Viewport& vp) override {
        for(auto& v:viewports_) if(v.id==vp.id){v=vp;return GuiResult::Success;}
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
    void set_focused_widget(IGuiWidget* w) override { focused_=w; }
    void clear_focus() override { focused_=nullptr; }

    void get_widgets_in_box(const math::Box& box, std::vector<IGuiWidget*>& out) override {
        collect_in_box(&root_, box, out);
    }

    void set_text_measurer(ITextMeasurer* m) override { text_measurer_=m; }
    ITextMeasurer* get_text_measurer() const override { return text_measurer_; }

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
        for(int i=0;i<w->get_child_count();++i) collect_in_box(w->get_child(i),box,out);
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
