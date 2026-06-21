/*
 * main.cpp - GUI Editor Standalone Application
 *
 * Renders entirely through the engine's reusable renderers over the backend-neutral
 * graphics abstraction (GraphicDevice / GraphicCommander), so the editor is just a
 * client of the same building blocks any project would use:
 *
 *   - gui::GpuGuiRenderer    draws a flattened WidgetRenderInfo (editor chrome AND the
 *                            design canvas widgets). Zoom/pan is baked into the
 *                            projection matrix, so the same renderer draws both.
 *   - gfx::VectorRenderer    draws the 2D overlay (canvas background, grid, selection
 *                            outlines, resize handles, rubber band, panel splitters).
 *   - gui::GpuTextRasterizer the shared glyph-atlas text rasterizer/measurer used by
 *                            both GUI contexts and GpuGuiRenderer.
 *
 * There is no hand-written GL here: the backend is selected at window creation and the
 * frame is recorded into one commander, then presented through the swapchain. Other
 * projects reproduce the canvas by wiring these three renderers the same way.
 *
 * Keyboard shortcuts:
 *   Ctrl+N       New layout
 *   Ctrl+O       Open file
 *   Ctrl+S       Save file
 *   Ctrl+Shift+S Save as
 *   Ctrl+Z       Undo
 *   Ctrl+Y       Redo
 *   Ctrl+C/X/V   Copy / Cut / Paste
 *   Ctrl+A       Select all
 *   Delete       Delete selected widget(s)
 *   Arrow keys   Nudge selection (hold Shift for 10px)
 *   Scroll       Zoom canvas
 *   Middle drag  Pan canvas
 */

#include "window.hpp"
#include "graphics_api.hpp"
#include "gui/gui.hpp"
#include "gui/gui_context.hpp"
#include "renderer/gui_renderer.hpp"
#include "renderer/gui_text_rasterizer.hpp"
#include "renderer/vector_renderer.hpp"
#include "editor.hpp"
#include "input/input_keyboard.hpp"
#include "input/input_mouse.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>

using namespace window;
using namespace window::math;
using namespace window::gui;
using namespace window::gui::editor;
using window::gfx::VectorRenderer;
using window::gfx::VectorRendererDesc;

// ============================================================================
// Globals — the engine renderers + the per-frame draw state the draw_* helpers read.
// ============================================================================

static GraphicCommander*   g_cmd  = nullptr;   // current frame's commander
static GpuGuiRenderer*     g_gui  = nullptr;   // WidgetRenderInfo renderer (chrome + design)
static VectorRenderer*     g_vec  = nullptr;   // 2D overlay primitives
static TextureHandle       g_atlas;            // glyph atlas (refreshed each frame)
static float               g_proj[16] = {};    // chrome ortho: logical px -> clip
static int                 g_fb_w = 0, g_fb_h = 0;  // framebuffer (physical px)
static float               g_time = 0;
static int                 g_window_h = 720;   // logical px
static float               g_dpi_scale = 1.0f; // physical / logical

// Orthographic projection mapping logical pixels (top-left origin) to clip space.
static void make_ortho(float out[16], float w, float h) {
    const float m[16] = {
        2.0f / w, 0.0f,      0.0f, 0.0f,
        0.0f,    -2.0f / h,  0.0f, 0.0f,
        0.0f,     0.0f,     -1.0f, 0.0f,
       -1.0f,     1.0f,      0.0f, 1.0f
    };
    std::memcpy(out, m, sizeof m);
}

// ============================================================================
// Editor chrome — menubar/toolbar/panels/menus, drawn straight from its WidgetRenderInfo.
// ============================================================================

static void draw_render_info(WidgetRenderInfo& ri) {
    // GpuGuiRenderer applies the per-widget clip rects (panels) itself, using g_dpi_scale
    // to turn the logical-px clip boxes into physical scissor rectangles.
    g_gui->render(g_cmd, ri, g_atlas, g_proj, g_fb_w, g_fb_h, g_dpi_scale);
}

// ============================================================================
// Design canvas widgets — the user-designed layout, transformed by canvas zoom/pan.
// ============================================================================

static void draw_design_widgets(GuiEditor& editor, WidgetRenderInfo& design_ri, int window_h) {
    (void)window_h;
    EditorCanvas& canvas = editor.get_canvas();
    Box vp = canvas.get_viewport_bounds();
    float vpx = x(box_min(vp)), vpy = y(box_min(vp));
    float vpw = box_width(vp), vph = box_height(vp);

    // Canvas background, under the widgets.
    g_vec->begin(g_proj, g_fb_w, g_fb_h);
    g_vec->fill_rect(vpx, vpy, vpw, vph, Vec4(0.16f, 0.16f, 0.17f, 1.0f));
    g_vec->end(g_cmd);

    // The zoom/pan transform is folded into the projection: a design point p maps to the
    // logical-screen point p*zoom + (viewport_origin + pan), then the usual ortho. Painter
    // order (opaque chrome drawn last) hides any overflow into the panels, so no scissor
    // is needed here. g_proj is column-major, so composing scale+translate is element-wise.
    float zoom = canvas.get_zoom();
    Vec2  pan  = canvas.get_pan();
    float ox = vpx + x(pan), oy = vpy + y(pan);
    float dp[16] = {};
    dp[0]  = g_proj[0] * zoom;
    dp[5]  = g_proj[5] * zoom;
    dp[10] = g_proj[10];
    dp[12] = g_proj[0] * ox + g_proj[12];
    dp[13] = g_proj[5] * oy + g_proj[13];
    dp[15] = 1.0f;
    g_gui->render(g_cmd, design_ri, g_atlas, dp, g_fb_w, g_fb_h, g_dpi_scale);
}

// ============================================================================
// Canvas overlay — grid, selection outlines/handles, rubber band, panel splitters.
// One vector-renderer batch in logical-pixel space (top-left origin).
// ============================================================================

static void draw_canvas_overlay(GuiEditor& editor, int window_h) {
    EditorCanvas& canvas = editor.get_canvas();
    const Vec4 selc(0.0f, 0.48f, 0.8f, 1.0f);

    g_vec->begin(g_proj, g_fb_w, g_fb_h);

    // Grid. The line ranges derive from the viewport, so they stay inside it.
    const CanvasGrid& grid = canvas.get_grid();
    if (grid.visible) {
        Box vp = canvas.get_viewport_bounds();
        float vpw = box_width(vp), vph = box_height(vp);
        float zoom = canvas.get_zoom();
        Vec2 pan = canvas.get_pan();
        float start_x = -x(pan) / zoom, start_y = -y(pan) / zoom;
        float end_x = start_x + vpw / zoom, end_y = start_y + vph / zoom;

        float spacing = grid.spacing;
        if (spacing * zoom < 5.0f) spacing = grid.major_spacing;   // skip fine grid at low zoom

        for (float gx = std::floor(start_x / spacing) * spacing; gx <= end_x; gx += spacing) {
            Vec2 sp = canvas.canvas_to_screen(Vec2(gx, start_y));
            Vec2 ep = canvas.canvas_to_screen(Vec2(gx, end_y));
            bool major = (std::fmod(std::abs(gx), grid.major_spacing) < 0.01f);
            g_vec->fill_rect(x(sp), y(sp), 1.0f, y(ep) - y(sp), major ? grid.major_color : grid.color);
        }
        for (float gy = std::floor(start_y / spacing) * spacing; gy <= end_y; gy += spacing) {
            Vec2 sp = canvas.canvas_to_screen(Vec2(start_x, gy));
            Vec2 ep = canvas.canvas_to_screen(Vec2(end_x, gy));
            bool major = (std::fmod(std::abs(gy), grid.major_spacing) < 0.01f);
            g_vec->fill_rect(x(sp), y(sp), x(ep) - x(sp), 1.0f, major ? grid.major_color : grid.color);
        }

        // Origin axes (canvas 0,0) — slightly brighter than major grid.
        Vec2 ox_s = canvas.canvas_to_screen(Vec2(0, start_y));
        Vec2 ox_e = canvas.canvas_to_screen(Vec2(0, end_y));
        g_vec->fill_rect(x(ox_s), y(ox_s), 1.0f, y(ox_e) - y(ox_s), Vec4(0.35f, 0.35f, 0.4f, 0.8f));
        Vec2 oy_s = canvas.canvas_to_screen(Vec2(start_x, 0));
        Vec2 oy_e = canvas.canvas_to_screen(Vec2(end_x, 0));
        g_vec->fill_rect(x(oy_s), y(oy_s), x(oy_e) - x(oy_s), 1.0f, Vec4(0.35f, 0.35f, 0.4f, 0.8f));
    }

    // Selection outlines (solid blue, 1-px borders).
    const SelectionInfo& sel = canvas.get_selection();
    for (auto* w : sel.widgets) {
        Box b = w->get_bounds();
        Vec2 tl = canvas.canvas_to_screen(box_min(b));
        Vec2 br = canvas.canvas_to_screen(box_max(b));
        float tlx = x(tl), tly = y(tl), brx = x(br), bry = y(br);
        float sw = brx - tlx, sh = bry - tly;
        g_vec->fill_rect(tlx, tly, sw, 1.0f, selc);
        g_vec->fill_rect(tlx, bry, sw, 1.0f, selc);
        g_vec->fill_rect(tlx, tly, 1.0f, sh, selc);
        g_vec->fill_rect(brx, tly, 1.0f, sh, selc);
    }

    // Resize handles (white fill, blue inner border; blue fill when hovered).
    std::vector<EditorCanvas::HandleRect> handles;
    canvas.get_selection_handles(handles);
    for (const auto& h : handles) {
        float hx = x(box_min(h.rect)), hy = y(box_min(h.rect));
        float hw = box_width(h.rect), hh = box_height(h.rect);
        g_vec->fill_rect(hx, hy, hw, hh, h.hovered ? selc : Vec4(1.0f, 1.0f, 1.0f, 0.9f));
        g_vec->fill_rect(hx + 1, hy + 1, hw - 2, hh - 2, selc);
    }

    // Rubber band (translucent fill + border).
    if (canvas.get_mode() == CanvasMode::RubberBand) {
        Box rb = canvas.get_rubber_band_rect();
        float rx = x(box_min(rb)), ry = y(box_min(rb));
        float rw = box_width(rb), rh = box_height(rb);
        const Vec4 rbb(0.0f, 0.48f, 0.8f, 0.6f);
        g_vec->fill_rect(rx, ry, rw, rh, Vec4(0.0f, 0.48f, 0.8f, 0.15f));
        g_vec->fill_rect(rx, ry, rw, 1.0f, rbb);
        g_vec->fill_rect(rx, ry + rh, rw, 1.0f, rbb);
        g_vec->fill_rect(rx, ry, 1.0f, rh, rbb);
        g_vec->fill_rect(rx + rw, ry, 1.0f, rh, rbb);
    }

    // Panel splitters (1-px bright + 1-px dark = 2-px visual handle).
    {
        float sw_log = g_fb_w / g_dpi_scale;
        float top = GuiEditor::MENUBAR_H + GuiEditor::TOOLBAR_H;
        float bot = (float)window_h - GuiEditor::STATUSBAR_H;
        float tree_x = editor.get_tree_panel_w();
        float insp_x = sw_log - editor.get_inspector_w();
        g_vec->fill_rect(tree_x - 1, top, 1, bot - top, Vec4(0.12f, 0.12f, 0.14f, 1.0f));
        g_vec->fill_rect(tree_x,     top, 1, bot - top, Vec4(0.35f, 0.35f, 0.38f, 1.0f));
        g_vec->fill_rect(insp_x - 1, top, 1, bot - top, Vec4(0.12f, 0.12f, 0.14f, 1.0f));
        g_vec->fill_rect(insp_x,     top, 1, bot - top, Vec4(0.35f, 0.35f, 0.38f, 1.0f));
    }

    g_vec->end(g_cmd);
}

// ============================================================================
// Mouse handler — forwards events to the EditorCanvas
// ============================================================================

struct EditorMouseHandler : public input::IMouseHandler {
    GuiEditor* editor = nullptr;
    Window*    win    = nullptr;

    // Splitter drag state
    enum class SplitterDrag { None, Tree, Inspector };
    SplitterDrag drag_splitter = SplitterDrag::None;
    float drag_start_x = 0.0f;
    float drag_start_w = 0.0f;

    const char* get_handler_id() const override { return "editor_canvas_mouse"; }
    // Priority below the GUI context handler (100) so editor UI gets first pick,
    // but above default (0) so we're still reliably called.
    int get_priority() const override { return 50; }

    static constexpr float SPLITTER_HIT_W = 6.0f; // pixels either side of divider

    bool is_near_tree_splitter(float x, float y) const {
        if (!win) return false;
        int sw2, sh2; win->get_size(&sw2, &sh2);
        float lh = sh2 / g_dpi_scale;
        float tree_x = editor->get_tree_panel_w();
        float top = 26.0f + 32.0f;
        float bot = lh - 24.0f;
        return (x >= tree_x - SPLITTER_HIT_W && x <= tree_x + SPLITTER_HIT_W &&
                y >= top && y <= bot);
    }
    bool is_near_insp_splitter(float x, float y) const {
        if (!win) return false;
        int sw2, sh2; win->get_size(&sw2, &sh2);
        float lw = sw2 / g_dpi_scale, lh = sh2 / g_dpi_scale;
        float insp_x = lw - editor->get_inspector_w();
        float top = 26.0f + 32.0f;
        float bot = lh - 24.0f;
        return (x >= insp_x - SPLITTER_HIT_W && x <= insp_x + SPLITTER_HIT_W &&
                y >= top && y <= bot);
    }

    bool on_mouse_button(const MouseButtonEvent& event) override {
        if (!editor) return false;
        EditorCanvas& canvas = editor->get_canvas();
        // Window events report physical px; editor works in logical px.
        math::Vec2 pos((float)event.x / g_dpi_scale, (float)event.y / g_dpi_scale);
        bool pressed = (event.type == EventType::MouseDown);

        // Splitter release
        if (!pressed && drag_splitter != SplitterDrag::None) {
            drag_splitter = SplitterDrag::None;
            return true;
        }

        // Splitter hit test
        if (pressed && event.button == window::MouseButton::Left) {
            if (is_near_tree_splitter(math::x(pos), math::y(pos))) {
                drag_splitter = SplitterDrag::Tree;
                drag_start_x  = math::x(pos);
                drag_start_w  = editor->get_tree_panel_w();
                return true;
            }
            if (is_near_insp_splitter(math::x(pos), math::y(pos))) {
                drag_splitter = SplitterDrag::Inspector;
                drag_start_x  = math::x(pos);
                drag_start_w  = editor->get_inspector_w();
                return true;
            }
        }

        bool in_vp   = math::box_contains(canvas.get_viewport_bounds(), pos);
        // Also forward when canvas is actively dragging (even outside viewport)
        bool active  = (canvas.get_mode() != CanvasMode::Idle);

        if (!in_vp && !active) return false;

        gui::MouseButton btn = static_cast<gui::MouseButton>(
            static_cast<uint8_t>(event.button));
        bool shift = (static_cast<uint8_t>(event.modifiers) & static_cast<uint8_t>(KeyMod::Shift)) != 0;
        bool ctrl  = (static_cast<uint8_t>(event.modifiers) & static_cast<uint8_t>(KeyMod::Control)) != 0;

        bool consumed = false;
        if (pressed)
            consumed = canvas.handle_mouse_down(btn, pos, shift, ctrl);
        else
            consumed = canvas.handle_mouse_up(btn, pos);

        // After any button event sync the inspector / hierarchy
        editor->on_canvas_interaction();
        return consumed;
    }

    bool on_mouse_move(const MouseMoveEvent& event) override {
        if (!editor) return false;
        EditorCanvas& canvas = editor->get_canvas();
        math::Vec2 pos((float)event.x / g_dpi_scale, (float)event.y / g_dpi_scale);

        // Splitter drag
        if (drag_splitter != SplitterDrag::None) {
            float delta = math::x(pos) - drag_start_x;
            if (drag_splitter == SplitterDrag::Tree)
                editor->set_tree_panel_w(drag_start_w + delta);
            else
                editor->set_inspector_w(drag_start_w - delta);
            return true;
        }

        bool in_vp  = math::box_contains(canvas.get_viewport_bounds(), pos);
        bool active = (canvas.get_mode() != CanvasMode::Idle);
        if (!in_vp && !active) return false;
        return canvas.handle_mouse_move(pos);
    }

    bool on_mouse_wheel(const MouseWheelEvent& event) override {
        if (!editor) return false;
        EditorCanvas& canvas = editor->get_canvas();
        math::Vec2 pos((float)event.x / g_dpi_scale, (float)event.y / g_dpi_scale);
        if (!math::box_contains(canvas.get_viewport_bounds(), pos)) return false;
        return canvas.handle_mouse_scroll(event.dx, event.dy);
    }
};

// ============================================================================
// Keyboard handler for editor shortcuts
// ============================================================================

struct EditorKeyHandler : public input::IKeyboardHandler {
    GuiEditor* editor = nullptr;
    Window* win = nullptr;

    const char* get_handler_id() const override { return "editor_shortcuts"; }
    int get_priority() const override { return 100; }

    bool on_key(const KeyEvent& event) override {
        if (!editor || event.type != EventType::KeyDown) return false;

        bool ctrl = (static_cast<uint8_t>(event.modifiers) & static_cast<uint8_t>(KeyMod::Control)) != 0;
        bool shift = (static_cast<uint8_t>(event.modifiers) & static_cast<uint8_t>(KeyMod::Shift)) != 0;

        // File shortcuts
        if (ctrl && !shift && event.key == Key::N) { editor->new_file(); return true; }
        if (ctrl && !shift && event.key == Key::O) { editor->open_file(); return true; }
        if (ctrl && !shift && event.key == Key::S) { editor->save_file(); return true; }
        if (ctrl && shift && event.key == Key::S) { editor->save_file_as(); return true; }

        // Undo/redo
        if (ctrl && event.key == Key::Z) { editor->undo(); return true; }
        if (ctrl && event.key == Key::Y) { editor->redo(); return true; }

        // Delete key routes through the editor so hierarchy/inspector refresh
        if (event.key == Key::Delete) { editor->delete_selected(); return true; }

        // Forward to canvas for selection/manipulation shortcuts
        int key_code = static_cast<int>(event.key);
        return editor->get_canvas().handle_key_down(key_code, ctrl, shift);
    }
};

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    printf("=== GUI Editor ===\n");

    // Parse command line - optional file to open
    const char* open_filepath = nullptr;
    if (argc > 1) open_filepath = argv[1];

    // Create window
    Config config;
    config.windows[0].title = "GUI Editor";
    config.windows[0].width = 1440;
    config.windows[0].height = 900;
    config.backend = Backend::OpenGL;

    Result result;
    auto windows = Window::create(config, &result);
    if (result != Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", result_to_string(result));
        return 1;
    }
    Window* win = windows[0];
    Graphics* gfx = win->graphics();
    printf("Window: %s (%s)\n", gfx->get_backend_name(), gfx->get_device_name());

    // RHI device + commander (records the whole frame, presented through the swapchain).
    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { printf("Failed to create device: %s\n", result_to_string(dr)); win->destroy(); return 1; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { printf("Failed to create commander\n"); destroy_device(dev); win->destroy(); return 1; }
    g_cmd = cmd;

    // Shared glyph-atlas text rasterizer (also the layout text measurer).
    GpuTextRasterizer text_rast;
    if (!text_rast.init(dev, "Segoe UI", 14.0f)) {
        printf("Failed to init text rasterizer\n");
        destroy_commander(cmd); destroy_device(dev); win->destroy(); return 1;
    }

    // GUI renderer (editor chrome + design widgets).
    GpuGuiRenderer gui_rend;
    if (!gui_rend.init(dev)) {
        printf("Failed to init GUI renderer (needs the built-in shader compiler)\n");
        text_rast.shutdown(); destroy_commander(cmd); destroy_device(dev); win->destroy(); return 1;
    }
    gui_rend.set_text_rasterizer(&text_rast);
    g_gui = &gui_rend;

    // Vector renderer for the 2D overlay (depth off — it draws on top of the GUI).
    VectorRenderer vec;
    VectorRendererDesc vd;
    vd.depth_test = false;
    vd.depth_write = false;
    if (!vec.init(dev, vd)) {
        printf("Failed to init vector renderer\n");
        gui_rend.shutdown(); text_rast.shutdown();
        destroy_commander(cmd); destroy_device(dev); win->destroy(); return 1;
    }
    g_vec = &vec;

    // Create editor GUI context
    GuiResult gresult;
    IGuiContext* editor_ctx = create_gui_context(&gresult);
    if (!editor_ctx) { printf("Failed to create GUI context\n"); return 1; }

    editor_ctx->attach_window(win);
    // Logical-extent layout: viewport/root bounds use logical (CSS-style) px, so the
    // editor's hardcoded sizes (MENUBAR_H = 26 etc.) keep their physical size on Hi-DPI.
    // The renderers project logical-px space onto a physical-px framebuffer.
    float dpi_scale = win->get_dpi_scale();
    g_dpi_scale = dpi_scale;
    int   sw_phys, sh_phys; win->get_size(&sw_phys, &sh_phys);
    float lw = sw_phys / dpi_scale, lh = sh_phys / dpi_scale;
    gui::Viewport vp; vp.id=0; vp.bounds=make_box(0,0,lw,lh); vp.scale=1.f;
    editor_ctx->add_viewport(vp);
    editor_ctx->get_root()->set_bounds(make_box(0,0,lw,lh));

    // Text measurer + rasterizer (the same object provides both).
    editor_ctx->set_text_measurer(&text_rast);
    editor_ctx->set_text_rasterizer(&text_rast);

    // Initialize the GUI editor
    GuiEditor gui_editor;
    if (!gui_editor.initialize(editor_ctx, win)) {
        printf("Failed to initialize GUI editor\n");
        destroy_gui_context(editor_ctx);
        vec.shutdown(); gui_rend.shutdown(); text_rast.shutdown();
        destroy_commander(cmd); destroy_device(dev); win->destroy();
        return 1;
    }

    // Set up text measurer/rasterizer for the design context too.
    IGuiContext* design_ctx = gui_editor.get_design_context();
    if (design_ctx) {
        design_ctx->set_text_measurer(&text_rast);
        design_ctx->set_text_rasterizer(&text_rast);
    }

    // Open file from command line if provided
    if (open_filepath) {
        gui_editor.open_file(open_filepath);
    }

    // Mouse handler — forwards canvas interactions (select, move, resize, zoom)
    EditorMouseHandler mouse_handler;
    mouse_handler.editor = &gui_editor;
    mouse_handler.win    = win;
    win->add_mouse_handler(&mouse_handler);

    // Keyboard handler
    EditorKeyHandler key_handler;
    key_handler.editor = &gui_editor;
    key_handler.win = win;
    win->add_keyboard_handler(&key_handler);

    printf("GUI Editor initialized. Ready.\n");

    // Main loop
    auto start_time = std::chrono::high_resolution_clock::now();
    float prev_time = 0;

    while (!win->should_close()) {
        win->poll_events();

        auto now = std::chrono::high_resolution_clock::now();
        float current_time = std::chrono::duration<float>(now - start_time).count();
        float dt = current_time - prev_time;
        prev_time = current_time;
        g_time = current_time;

        int sw_p, sh_p;
        win->get_size(&sw_p, &sh_p);
        // Refresh in case the window moved to a monitor with different DPI.
        dpi_scale = win->get_dpi_scale();
        g_dpi_scale = dpi_scale;
        int sw = (int)(sw_p / dpi_scale);
        int sh = (int)(sh_p / dpi_scale);
        g_window_h = sh;
        g_fb_w = sw_p; g_fb_h = sh_p;
        make_ortho(g_proj, (float)sw, (float)sh);

        // Update viewport (logical-px space).
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        editor_ctx->update_viewport(vp);
        editor_ctx->get_root()->set_bounds(make_box(0, 0, (float)sw, (float)sh));

        // Update editor
        gui_editor.update(dt);

        // Also update design context frame
        if (design_ctx) {
            design_ctx->begin_frame(dt);
            design_ctx->end_frame();
        }

        // Build the frame's render infos, flatten them (rasterising any new glyphs into the
        // RAM atlas), then upload the atlas once before recording draws.
        WidgetRenderInfo& editor_ri = const_cast<WidgetRenderInfo&>(gui_editor.get_render_info(win));
        WidgetRenderInfo* design_ri = design_ctx ? &const_cast<WidgetRenderInfo&>(design_ctx->get_render_info()) : nullptr;
        editor_ri.flatten(&text_rast);
        if (design_ri) design_ri->flatten(&text_rast);
        g_atlas = text_rast.sync_atlas();

        // Record + present one frame.
        cmd->begin();
        cmd->set_render_target_backbuffer();
        window::Viewport rvp; rvp.x = 0; rvp.y = 0; rvp.width = (float)sw_p; rvp.height = (float)sh_p;
        rvp.min_depth = 0; rvp.max_depth = 1;
        cmd->set_viewport(rvp);
        cmd->clear_color(ClearColor(0.12f, 0.12f, 0.13f, 1.0f));

        // 1. Design canvas (background + zoom/pan'd widgets) — under the chrome.
        if (design_ri) draw_design_widgets(gui_editor, *design_ri, sh);
        // 2. Canvas overlay (grid, selection, handles, rubber band, splitters).
        draw_canvas_overlay(gui_editor, sh);
        // 3. Editor chrome (menubar, toolbar, panels, popups) — on top.
        draw_render_info(editor_ri);

        cmd->end();
        submit_commander(gfx, cmd);
        gfx->present();
    }

    // Cleanup
    gui_editor.shutdown();
    editor_ctx->detach_window(win);
    destroy_gui_context(editor_ctx);
    vec.shutdown();      g_vec = nullptr;
    gui_rend.shutdown(); g_gui = nullptr;
    text_rast.shutdown();
    destroy_commander(cmd); g_cmd = nullptr;
    destroy_device(dev);
    win->destroy();

    printf("GUI Editor closed.\n");
    return 0;
}
