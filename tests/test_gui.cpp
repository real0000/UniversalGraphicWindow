/*
 * test_gui.cpp - Unit tests for GUI widget system
 *
 * Headless tests - no display or graphics backend required.
 */

#include "gui/gui.hpp"
#include <cstdio>
#include <cstring>

using namespace window::gui;

// Simple test framework (same as test_window.cpp)
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    try { \
        test_##name(); \
        printf("PASSED\n"); \
        g_tests_passed++; \
    } catch (...) { \
        printf("FAILED (exception)\n"); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED at line %d: %s != %s\n", __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAILED at line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

// ============================================================================
// Context Tests
// ============================================================================

TEST(context_create_destroy) {
    GuiResult result;
    IGuiContext* ctx = create_gui_context(&result);
    ASSERT(ctx != nullptr);
    ASSERT_EQ(result, GuiResult::Success);
    ASSERT(ctx->is_initialized());
    destroy_gui_context(ctx);
}

TEST(context_viewport) {
    IGuiContext* ctx = create_gui_context();
    Viewport vp;
    vp.id = 1;
    vp.bounds = window::math::make_box(0, 0, 800, 600);
    ASSERT_EQ(ctx->add_viewport(vp), GuiResult::Success);
    ASSERT(ctx->get_viewport(1) != nullptr);
    ASSERT_EQ(ctx->get_viewport(1)->id, 1);
    ASSERT_EQ(ctx->add_viewport(vp), GuiResult::ErrorInvalidParameter); // duplicate
    ASSERT_EQ(ctx->remove_viewport(1), GuiResult::Success);
    ASSERT(ctx->get_viewport(1) == nullptr);
    destroy_gui_context(ctx);
}

TEST(context_root) {
    IGuiContext* ctx = create_gui_context();
    IGuiWidget* root = ctx->get_root();
    ASSERT(root != nullptr);
    ASSERT_EQ(root->get_type(), WidgetType::Container);
    ASSERT_STREQ(root->get_name(), "root");
    destroy_gui_context(ctx);
}

// ============================================================================
// Widget Factory Tests
// ============================================================================

TEST(factory_button) {
    IGuiContext* ctx = create_gui_context();
    IGuiButton* btn = ctx->create_button(ButtonType::Toggle);
    ASSERT(btn != nullptr);
    ASSERT_EQ(btn->get_type(), WidgetType::Button);
    ASSERT_EQ(btn->get_button_type(), ButtonType::Toggle);
    btn->set_text("Test");
    ASSERT_STREQ(btn->get_text(), "Test");
    destroy_gui_context(ctx);
}

TEST(factory_label) {
    IGuiContext* ctx = create_gui_context();
    IGuiLabel* lbl = ctx->create_label("Hello");
    ASSERT(lbl != nullptr);
    ASSERT_EQ(lbl->get_type(), WidgetType::Label);
    ASSERT_STREQ(lbl->get_text(), "Hello");
    destroy_gui_context(ctx);
}

TEST(factory_text_input) {
    IGuiContext* ctx = create_gui_context();
    IGuiTextInput* ti = ctx->create_text_input("placeholder");
    ASSERT(ti != nullptr);
    ASSERT_EQ(ti->get_type(), WidgetType::TextInput);
    ASSERT_STREQ(ti->get_placeholder(), "placeholder");
    destroy_gui_context(ctx);
}

TEST(factory_editbox) {
    IGuiContext* ctx = create_gui_context();
    IGuiEditBox* eb = ctx->create_editbox();
    ASSERT(eb != nullptr);
    eb->set_text("line1\nline2\nline3");
    ASSERT_EQ(eb->get_line_count(), 3);
    ASSERT_STREQ(eb->get_line(1), "line2");
    destroy_gui_context(ctx);
}

TEST(factory_slider) {
    IGuiContext* ctx = create_gui_context();
    IGuiSlider* s = ctx->create_slider(SliderOrientation::Horizontal);
    ASSERT(s != nullptr);
    ASSERT_EQ(s->get_type(), WidgetType::Slider);
    s->set_range(0, 100);
    s->set_value(50);
    ASSERT(s->get_value() >= 49.9f && s->get_value() <= 50.1f);
    destroy_gui_context(ctx);
}

TEST(factory_progress_bar) {
    IGuiContext* ctx = create_gui_context();
    IGuiProgressBar* p = ctx->create_progress_bar(ProgressBarMode::Indeterminate);
    ASSERT(p != nullptr);
    ASSERT_EQ(p->get_type(), WidgetType::ProgressBar);
    ASSERT_EQ(p->get_mode(), ProgressBarMode::Indeterminate);
    destroy_gui_context(ctx);
}

TEST(factory_image) {
    IGuiContext* ctx = create_gui_context();
    IGuiImage* img = ctx->create_image("test.png");
    ASSERT(img != nullptr);
    ASSERT_EQ(img->get_type(), WidgetType::Image);
    ASSERT_STREQ(img->get_image_name().c_str(), "test.png");
    destroy_gui_context(ctx);
}

TEST(factory_scroll) {
    IGuiContext* ctx = create_gui_context();
    IGuiScrollView* sv = ctx->create_scroll_view();
    ASSERT(sv != nullptr);
    ASSERT_EQ(sv->get_type(), WidgetType::ScrollArea);
    IGuiScrollBar* sb = ctx->create_scroll_bar(ScrollBarOrientation::Vertical);
    ASSERT(sb != nullptr);
    destroy_gui_context(ctx);
}

TEST(factory_listbox) {
    IGuiContext* ctx = create_gui_context();
    IGuiListBox* lb = ctx->create_list_box();
    ASSERT(lb != nullptr);
    int id1 = lb->add_item("Item1", nullptr);
    int id2 = lb->add_item("Item2", nullptr);
    ASSERT_EQ(lb->get_item_count(), 2);
    ASSERT_STREQ(lb->get_item_text(id1), "Item1");
    lb->set_selected_item(id2);
    ASSERT_EQ(lb->get_selected_item(), id2);
    destroy_gui_context(ctx);
}

TEST(factory_combobox) {
    IGuiContext* ctx = create_gui_context();
    IGuiComboBox* cb = ctx->create_combo_box();
    ASSERT(cb != nullptr);
    cb->set_placeholder("Pick one");
    int id = cb->add_item("Option A", nullptr);
    cb->set_selected_item(id);
    ASSERT_EQ(cb->get_selected_item(), id);
    ASSERT(!cb->is_open());
    cb->open();
    ASSERT(cb->is_open());
    cb->close();
    ASSERT(!cb->is_open());
    destroy_gui_context(ctx);
}

TEST(factory_treeview) {
    IGuiContext* ctx = create_gui_context();
    IGuiTreeView* tv = ctx->create_tree_view();
    ASSERT(tv != nullptr);
    int root = tv->add_node(-1, "Root", nullptr);
    int child = tv->add_node(root, "Child", nullptr);
    ASSERT_EQ(tv->get_node_count(), 2);
    ASSERT_EQ(tv->get_node_parent(child), root);
    ASSERT_EQ(tv->get_node_child_count(root), 1);
    destroy_gui_context(ctx);
}

TEST(factory_tabcontrol) {
    IGuiContext* ctx = create_gui_context();
    IGuiTabControl* tc = ctx->create_tab_control(TabPosition::Top);
    ASSERT(tc != nullptr);
    int t1 = tc->add_tab("Tab1", nullptr);
    int t2 = tc->add_tab("Tab2", nullptr);
    ASSERT_EQ(tc->get_tab_count(), 2);
    ASSERT_EQ(tc->get_active_tab(), t1); // first tab auto-selected
    tc->set_active_tab(t2);
    ASSERT_EQ(tc->get_active_tab(), t2);
    destroy_gui_context(ctx);
}

TEST(factory_property_grid) {
    IGuiContext* ctx = create_gui_context();
    IGuiPropertyGrid* pg = ctx->create_property_grid();
    ASSERT(pg != nullptr);
    int id = pg->add_property("General", "Name", PropertyType::String);
    pg->set_string_value(id, "TestValue");
    ASSERT_STREQ(pg->get_string_value(id), "TestValue");
    destroy_gui_context(ctx);
}

TEST(factory_dialog) {
    IGuiContext* ctx = create_gui_context();
    IGuiDialog* dlg = ctx->create_dialog("Test Dialog", DialogButtons::OKCancel);
    ASSERT(dlg != nullptr);
    ASSERT_STREQ(dlg->get_title(), "Test Dialog");
    ASSERT_EQ(dlg->get_buttons(), DialogButtons::OKCancel);
    ASSERT(!dlg->is_open());
    dlg->show();
    ASSERT(dlg->is_open());
    dlg->hide();
    ASSERT(!dlg->is_open());
    destroy_gui_context(ctx);
}

TEST(factory_popup) {
    IGuiContext* ctx = create_gui_context();
    IGuiPopup* popup = ctx->create_popup();
    ASSERT(popup != nullptr);
    ASSERT(!popup->is_open());
    destroy_gui_context(ctx);
}

TEST(factory_menu) {
    IGuiContext* ctx = create_gui_context();
    IGuiMenu* menu = ctx->create_menu();
    ASSERT(menu != nullptr);
    int id1 = menu->add_item("File", nullptr, nullptr);
    int id2 = menu->add_separator();
    int id3 = menu->add_item("Exit", nullptr, "Alt+F4");
    ASSERT_EQ(menu->get_item_count(), 3);
    ASSERT_STREQ(menu->get_item_shortcut(id3), "Alt+F4");
    destroy_gui_context(ctx);
}

TEST(factory_menubar) {
    IGuiContext* ctx = create_gui_context();
    IGuiMenuBar* mb = ctx->create_menu_bar();
    ASSERT(mb != nullptr);
    IGuiMenu* file_menu = ctx->create_menu();
    int id = mb->add_menu("File", file_menu);
    ASSERT_EQ(mb->get_menu_count(), 1);
    ASSERT(mb->get_menu(id) == file_menu);
    destroy_gui_context(ctx);
}

TEST(factory_toolbar) {
    IGuiContext* ctx = create_gui_context();
    IGuiToolbar* tb = ctx->create_toolbar(ToolbarOrientation::Horizontal);
    ASSERT(tb != nullptr);
    int id = tb->add_button("icon.png", "Do something");
    ASSERT_EQ(tb->get_item_count(), 1);
    ASSERT_STREQ(tb->get_item_tooltip(id), "Do something");
    destroy_gui_context(ctx);
}

TEST(factory_statusbar) {
    IGuiContext* ctx = create_gui_context();
    IGuiStatusBar* sb = ctx->create_status_bar();
    ASSERT(sb != nullptr);
    int id = sb->add_panel("Ready", StatusBarPanelSizeMode::Auto);
    ASSERT_EQ(sb->get_panel_count(), 1);
    ASSERT_STREQ(sb->get_panel_text(id), "Ready");
    destroy_gui_context(ctx);
}

TEST(factory_color_picker) {
    IGuiContext* ctx = create_gui_context();
    IGuiColorPicker* cp = ctx->create_color_picker(ColorPickerMode::HSVSquare);
    ASSERT(cp != nullptr);
    cp->set_color(window::math::Vec4(1, 0, 0, 1));
    ASSERT(cp->get_hue() < 1.0f || cp->get_hue() > 359.0f); // red = ~0 degrees
    destroy_gui_context(ctx);
}

TEST(factory_page) {
    IGuiContext* ctx = create_gui_context();
    IGuiPage* page = ctx->create_page("home");
    ASSERT(page != nullptr);
    ASSERT_STREQ(page->get_page_id(), "home");
    IGuiPageView* pv = ctx->create_page_view();
    ASSERT(pv != nullptr);
    pv->add_page(page);
    ASSERT_EQ(pv->get_page_count(), 1);
    destroy_gui_context(ctx);
}

TEST(factory_split_panel) {
    IGuiContext* ctx = create_gui_context();
    IGuiSplitPanel* sp = ctx->create_split_panel(SplitOrientation::Horizontal);
    ASSERT(sp != nullptr);
    destroy_gui_context(ctx);
}

TEST(factory_dock_panel) {
    IGuiContext* ctx = create_gui_context();
    IGuiDockPanel* dp = ctx->create_dock_panel();
    ASSERT(dp != nullptr);
    destroy_gui_context(ctx);
}

// ============================================================================
// Widget Tree Tests
// ============================================================================

TEST(widget_tree) {
    IGuiContext* ctx = create_gui_context();
    IGuiWidget* root = ctx->get_root();

    IGuiButton* btn1 = ctx->create_button();
    btn1->set_name("btn1");
    IGuiButton* btn2 = ctx->create_button();
    btn2->set_name("btn2");
    IGuiLabel* lbl = ctx->create_label("text");
    lbl->set_name("lbl");

    ASSERT(root->add_child(btn1));
    ASSERT(root->add_child(btn2));
    ASSERT(root->add_child(lbl));
    ASSERT_EQ(root->get_child_count(), 3);
    ASSERT(root->get_child(0) == btn1);
    ASSERT(btn1->get_parent() == root);

    // find_by_name
    ASSERT(root->find_by_name("btn2") == btn2);
    ASSERT(root->find_by_name("lbl") == lbl);
    ASSERT(root->find_by_name("nonexistent") == nullptr);

    // find_all_by_name
    std::vector<IGuiWidget*> found;
    root->find_all_by_name("btn1", found);
    ASSERT_EQ((int)found.size(), 1);

    // remove_child
    ASSERT(root->remove_child(btn2));
    ASSERT_EQ(root->get_child_count(), 2);
    ASSERT(root->find_by_name("btn2") == nullptr);

    destroy_gui_context(ctx);
}

// ============================================================================
// Widget Properties Tests
// ============================================================================

TEST(widget_properties) {
    IGuiContext* ctx = create_gui_context();
    IGuiWidget* w = ctx->create_widget(WidgetType::Panel);
    ASSERT_EQ(w->get_type(), WidgetType::Panel);

    w->set_name("panel1");
    ASSERT_STREQ(w->get_name(), "panel1");

    w->set_bounds(window::math::make_box(10, 20, 110, 70));
    auto b = w->get_bounds();
    ASSERT(window::math::x(window::math::box_min(b)) >= 9.9f);
    ASSERT(window::math::y(window::math::box_min(b)) >= 19.9f);

    w->set_visible(false);
    ASSERT(!w->is_visible());
    w->set_visible(true);
    ASSERT(w->is_visible());

    w->set_enabled(false);
    ASSERT(!w->is_enabled());

    destroy_gui_context(ctx);
}

// ============================================================================
// Input Handling Tests
// ============================================================================

class TestClickHandler : public IGuiEventHandler {
public:
    int clicks = 0;
    void on_gui_event(const GuiEvent& ev) override {
        if (ev.type == GuiEventType::Click) clicks++;
    }
};

TEST(button_click) {
    IGuiContext* ctx = create_gui_context();
    IGuiButton* btn = ctx->create_button();
    btn->set_bounds(window::math::make_box(0, 0, 100, 50));
    ctx->get_root()->add_child(btn);

    TestClickHandler handler;
    btn->set_event_handler(&handler);

    window::math::Vec2 inside(50, 25);
    btn->handle_mouse_button(MouseButton::Left, true, inside);
    btn->handle_mouse_button(MouseButton::Left, false, inside);
    ASSERT_EQ(handler.clicks, 1);

    // Click outside should not fire
    window::math::Vec2 outside(200, 200);
    btn->handle_mouse_button(MouseButton::Left, true, outside);
    btn->handle_mouse_button(MouseButton::Left, false, outside);
    ASSERT_EQ(handler.clicks, 1);

    destroy_gui_context(ctx);
}

TEST(text_input_handling) {
    IGuiContext* ctx = create_gui_context();
    IGuiTextInput* ti = ctx->create_text_input();
    ti->set_text("Hello");
    ti->set_cursor_position(5);
    ti->handle_text_input(" World");
    ASSERT_STREQ(ti->get_text(), "Hello World");
    ASSERT_EQ(ti->get_cursor_position(), 11);

    ti->select_all();
    ASSERT_EQ(ti->get_selection_start(), 0);
    ASSERT_EQ(ti->get_selection_length(), 11);

    destroy_gui_context(ctx);
}

TEST(toggle_button) {
    IGuiContext* ctx = create_gui_context();
    IGuiButton* btn = ctx->create_button(ButtonType::Toggle);
    btn->set_bounds(window::math::make_box(0, 0, 100, 50));

    ASSERT(!btn->is_checked());
    window::math::Vec2 inside(50, 25);
    btn->handle_mouse_button(MouseButton::Left, true, inside);
    btn->handle_mouse_button(MouseButton::Left, false, inside);
    ASSERT(btn->is_checked());

    // Toggle off
    btn->handle_mouse_button(MouseButton::Left, true, inside);
    btn->handle_mouse_button(MouseButton::Left, false, inside);
    ASSERT(!btn->is_checked());

    destroy_gui_context(ctx);
}

// ============================================================================
// Animation Tests
// ============================================================================

TEST(animation_basic) {
    IGuiContext* ctx = create_gui_context();
    IGuiAnimationManager* mgr = ctx->get_animation_manager();
    ASSERT(mgr != nullptr);
    ASSERT_EQ(mgr->get_animation_count(), 0);

    IGuiAnimation* anim = mgr->create_animation();
    ASSERT(anim != nullptr);
    ASSERT_EQ(mgr->get_animation_count(), 1);
    ASSERT_EQ(anim->get_state(), AnimationState::Idle);

    anim->set_name("test_anim");
    ASSERT_STREQ(anim->get_name(), "test_anim");

    anim->animate_from_to(
        window::math::Vec4(0, 0, 0, 0),
        window::math::Vec4(1, 0, 0, 0),
        1.0f
    );
    anim->start();
    ASSERT_EQ(anim->get_state(), AnimationState::Playing);

    // Update halfway
    mgr->update(0.5f);
    ASSERT(anim->get_progress() > 0.4f && anim->get_progress() < 0.6f);

    // Update to completion
    mgr->update(0.6f);
    ASSERT_EQ(anim->get_state(), AnimationState::Completed);
    ASSERT(anim->get_current_value().x > 0.99f);

    mgr->destroy_animation(anim);
    ASSERT_EQ(mgr->get_animation_count(), 0);

    destroy_gui_context(ctx);
}

TEST(animation_pause_resume) {
    IGuiContext* ctx = create_gui_context();
    IGuiAnimationManager* mgr = ctx->get_animation_manager();
    IGuiAnimation* anim = mgr->create_animation();
    anim->animate_from_to(
        window::math::Vec4(0, 0, 0, 0),
        window::math::Vec4(1, 0, 0, 0),
        2.0f
    );
    anim->start();
    mgr->update(0.5f);
    float prog_before = anim->get_progress();

    anim->pause();
    ASSERT_EQ(anim->get_state(), AnimationState::Paused);
    mgr->update(1.0f); // should not advance
    ASSERT(anim->get_progress() >= prog_before - 0.01f && anim->get_progress() <= prog_before + 0.01f);

    anim->resume();
    ASSERT_EQ(anim->get_state(), AnimationState::Playing);

    destroy_gui_context(ctx);
}

// ============================================================================
// String Conversion Tests
// ============================================================================

TEST(string_conversions) {
    ASSERT_STREQ(gui_result_to_string(GuiResult::Success), "Success");
    ASSERT_STREQ(gui_result_to_string(GuiResult::ErrorOutOfMemory), "Out of memory");
    ASSERT_STREQ(widget_type_to_string(WidgetType::Button), "Button");
    ASSERT_STREQ(widget_type_to_string(WidgetType::Label), "Label");
    ASSERT_STREQ(widget_state_to_string(WidgetState::Normal), "Normal");
    ASSERT_STREQ(widget_state_to_string(WidgetState::Hovered), "Hovered");
    ASSERT_STREQ(gui_event_type_to_string(GuiEventType::Click), "Click");
    ASSERT_STREQ(animation_easing_to_string(AnimationEasing::Linear), "Linear");
    ASSERT_STREQ(animation_state_to_string(AnimationState::Playing), "Playing");
    ASSERT_STREQ(animation_target_to_string(AnimationTarget::Opacity), "Opacity");
    ASSERT_STREQ(animation_loop_to_string(AnimationLoop::PingPong), "PingPong");
}

// ============================================================================
// Render Info Tests
// ============================================================================

TEST(render_info) {
    IGuiContext* ctx = create_gui_context();
    IGuiButton* btn = ctx->create_button();
    btn->set_bounds(window::math::make_box(0, 0, 100, 30));

    WidgetRenderInfo info;
    btn->get_render_info(nullptr, &info);
    ASSERT(!info.textures.empty());
    ASSERT_EQ(info.textures[0].source_type, TextureSourceType::Generated);

    destroy_gui_context(ctx);
}

// ============================================================================
// Modal Tests
// ============================================================================

TEST(modal_stack) {
    IGuiContext* ctx = create_gui_context();
    IGuiDialog* dlg = ctx->create_dialog("Modal", DialogButtons::OK);
    ASSERT(ctx->get_modal() == nullptr);

    ctx->push_modal(dlg);
    ASSERT(ctx->get_modal() == dlg);

    ctx->pop_modal();
    ASSERT(ctx->get_modal() == nullptr);

    destroy_gui_context(ctx);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== GUI Unit Tests ===\n\n");

    // Context
    RUN_TEST(context_create_destroy);
    RUN_TEST(context_viewport);
    RUN_TEST(context_root);

    // Widget factories
    RUN_TEST(factory_button);
    RUN_TEST(factory_label);
    RUN_TEST(factory_text_input);
    RUN_TEST(factory_editbox);
    RUN_TEST(factory_slider);
    RUN_TEST(factory_progress_bar);
    RUN_TEST(factory_image);
    RUN_TEST(factory_scroll);
    RUN_TEST(factory_listbox);
    RUN_TEST(factory_combobox);
    RUN_TEST(factory_treeview);
    RUN_TEST(factory_tabcontrol);
    RUN_TEST(factory_property_grid);
    RUN_TEST(factory_dialog);
    RUN_TEST(factory_popup);
    RUN_TEST(factory_menu);
    RUN_TEST(factory_menubar);
    RUN_TEST(factory_toolbar);
    RUN_TEST(factory_statusbar);
    RUN_TEST(factory_color_picker);
    RUN_TEST(factory_page);
    RUN_TEST(factory_split_panel);
    RUN_TEST(factory_dock_panel);

    // Widget tree
    RUN_TEST(widget_tree);
    RUN_TEST(widget_properties);

    // Input
    RUN_TEST(button_click);
    RUN_TEST(text_input_handling);
    RUN_TEST(toggle_button);

    // Animation
    RUN_TEST(animation_basic);
    RUN_TEST(animation_pause_resume);

    // String conversions
    RUN_TEST(string_conversions);

    // Render info
    RUN_TEST(render_info);

    // Modal
    RUN_TEST(modal_stack);

    printf("\n=== Results: %d passed, %d failed ===\n", g_tests_passed, g_tests_failed);
    return g_tests_failed > 0 ? 1 : 0;
}
