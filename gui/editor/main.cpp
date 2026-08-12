/*
 * main.cpp - GUI Editor Standalone Application
 *
 * Renders entirely through the engine's reusable renderers over the backend-neutral
 * graphics abstraction (GraphicDevice / GraphicCommander), so the editor is just a
 * client of the same building blocks any project would use:
 *
 *   - gui::GpuGuiRenderer    draws EVERYTHING: the editor chrome, the design canvas
 *                            widgets and the canvas overlay (background, grid, selection
 *                            outlines, resize handles, rubber band, panel splitters).
 *                            Its render_window_frame() owns the whole frame — backbuffer
 *                            and stencil bind, clears, per-layer draws and submit.
 *   - gui::GpuTextRasterizer the shared glyph-atlas text rasterizer/measurer used by
 *                            both GUI contexts and GpuGuiRenderer.
 *
 * The frame is a list of layers drawn back to front, each naming its own projection: the
 * design surface is zoomed and panned, the overlay and chrome are not. That is why the
 * overlay is an immediate WidgetRenderInfo rather than hand-rolled vector primitives —
 * one renderer, one frame path, and every command carries its own clip box.
 *
 * There is no hand-written GL here: the backend is selected at window creation and the
 * frame is recorded into one commander, then presented through the swapchain.
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

// ============================================================================
// Globals — the engine renderers + the per-frame draw state the draw_* helpers read.
// ============================================================================

static GraphicCommander*   g_cmd  = nullptr;   // current frame's commander
static GpuGuiRenderer*     g_gui  = nullptr;   // WidgetRenderInfo renderer (chrome + design)
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
    // GpuGuiRenderer applies the per-widget clip rects (panels) itself, drawing each as a
    // stencil mask through g_proj — the same matrix as the content, so the logical-px clip
    // boxes need no conversion.
    g_gui->render(g_cmd, ri, g_atlas, g_proj, g_fb_w, g_fb_h);
}

// ============================================================================
// Design canvas widgets — the user-designed layout, transformed by canvas zoom/pan.
// ============================================================================

// The canvas viewport expressed in DESIGN coordinates. A design point p is drawn at the
// logical-screen point p*zoom + (viewport_origin + pan), so inverting that gives the region
// of design space the viewport shows. Used to clip the design content to the canvas.
static Box design_visible_box(EditorCanvas& canvas) {
    Box vp = canvas.get_viewport_bounds();
    const float zoom = canvas.get_zoom();
    const Vec2 pan = canvas.get_pan();
    if (zoom <= 0.0f) return make_box(0, 0, 0, 0);
    return make_box(-x(pan) / zoom, -y(pan) / zoom,
                    box_width(vp) / zoom, box_height(vp) / zoom);
}

// The zoom/pan projection the design surface is drawn through: a design point p maps to
// the logical-screen point p*zoom + (viewport_origin + pan), then the usual ortho.
// g_proj is column-major, so composing scale+translate is element-wise.
static void make_design_proj(EditorCanvas& canvas, float out[16]) {
    Box vp = canvas.get_viewport_bounds();
    const float zoom = canvas.get_zoom();
    const Vec2  pan  = canvas.get_pan();
    const float ox = x(box_min(vp)) + x(pan), oy = y(box_min(vp)) + y(pan);
    std::memset(out, 0, sizeof(float) * 16);
    out[0]  = g_proj[0] * zoom;
    out[5]  = g_proj[5] * zoom;
    out[10] = g_proj[10];
    out[12] = g_proj[0] * ox + g_proj[12];
    out[13] = g_proj[5] * oy + g_proj[13];
    out[15] = 1.0f;
}

// ============================================================================
// Canvas overlay — canvas background, grid, selection outlines/handles, rubber band and
// panel splitters, built as an immediate WidgetRenderInfo layer.
//
// This used to be hand-rolled VectorRenderer primitives. Going through the GUI renderer
// instead means each command carries its OWN clip box, so the canvas-space content is
// bounded to the canvas viewport per command rather than by splitting the work into
// separately-clipped batches — and the editor no longer needs a second renderer at all.
//
// The clip matters for the selection handles and the rubber band: those follow the widget
// being dragged, so they reach outside the canvas whenever a widget is dragged towards a
// panel — and unlike the grid, their extent is not derived from the viewport. The panel
// splitters are chrome, so they pass an empty clip box ("no clip").
// ============================================================================

static void build_canvas_overlay(GuiEditor& editor, WidgetRenderInfo& ov, int window_h) {
    EditorCanvas& canvas = editor.get_canvas();
    const Vec4 selc(0.0f, 0.48f, 0.8f, 1.0f);
    WidgetRenderInfo* g_ov = &ov;
    int32_t depth = 0;
    Box clip = canvas.get_viewport_bounds();   // canvas-space content is bounded to the canvas

    // Canvas background, under everything else the canvas draws.
    {
        Box vp = canvas.get_viewport_bounds();
        g_ov->push_rect(x(box_min(vp)), y(box_min(vp)), box_width(vp), box_height(vp),
                        Vec4(0.16f, 0.16f, 0.17f, 1.0f), depth++, clip);
    }

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
            g_ov->push_rect(x(sp), y(sp), 1.0f, y(ep) - y(sp), major ? grid.major_color : grid.color, depth++, clip);
        }
        for (float gy = std::floor(start_y / spacing) * spacing; gy <= end_y; gy += spacing) {
            Vec2 sp = canvas.canvas_to_screen(Vec2(start_x, gy));
            Vec2 ep = canvas.canvas_to_screen(Vec2(end_x, gy));
            bool major = (std::fmod(std::abs(gy), grid.major_spacing) < 0.01f);
            g_ov->push_rect(x(sp), y(sp), x(ep) - x(sp), 1.0f, major ? grid.major_color : grid.color, depth++, clip);
        }

        // Origin axes (canvas 0,0) — slightly brighter than major grid.
        Vec2 ox_s = canvas.canvas_to_screen(Vec2(0, start_y));
        Vec2 ox_e = canvas.canvas_to_screen(Vec2(0, end_y));
        g_ov->push_rect(x(ox_s), y(ox_s), 1.0f, y(ox_e) - y(ox_s), Vec4(0.35f, 0.35f, 0.4f, 0.8f), depth++, clip);
        Vec2 oy_s = canvas.canvas_to_screen(Vec2(start_x, 0));
        Vec2 oy_e = canvas.canvas_to_screen(Vec2(end_x, 0));
        g_ov->push_rect(x(oy_s), y(oy_s), x(oy_e) - x(oy_s), 1.0f, Vec4(0.35f, 0.35f, 0.4f, 0.8f), depth++, clip);
    }

    // Selection outlines (solid blue, 1-px borders).
    const SelectionInfo& sel = canvas.get_selection();
    for (auto* w : sel.widgets) {
        Box b = w->get_bounds();
        Vec2 tl = canvas.canvas_to_screen(box_min(b));
        Vec2 br = canvas.canvas_to_screen(box_max(b));
        float tlx = x(tl), tly = y(tl), brx = x(br), bry = y(br);
        float sw = brx - tlx, sh = bry - tly;
        g_ov->push_rect(tlx, tly, sw, 1.0f, selc, depth++, clip);
        g_ov->push_rect(tlx, bry, sw, 1.0f, selc, depth++, clip);
        g_ov->push_rect(tlx, tly, 1.0f, sh, selc, depth++, clip);
        g_ov->push_rect(brx, tly, 1.0f, sh, selc, depth++, clip);
    }

    // Resize handles (white fill, blue inner border; blue fill when hovered).
    std::vector<EditorCanvas::HandleRect> handles;
    canvas.get_selection_handles(handles);
    for (const auto& h : handles) {
        float hx = x(box_min(h.rect)), hy = y(box_min(h.rect));
        float hw = box_width(h.rect), hh = box_height(h.rect);
        g_ov->push_rect(hx, hy, hw, hh, h.hovered ? selc : Vec4(1.0f, 1.0f, 1.0f, 0.9f), depth++, clip);
        g_ov->push_rect(hx + 1, hy + 1, hw - 2, hh - 2, selc, depth++, clip);
    }

    // Rubber band (translucent fill + border).
    if (canvas.get_mode() == CanvasMode::RubberBand) {
        Box rb = canvas.get_rubber_band_rect();
        float rx = x(box_min(rb)), ry = y(box_min(rb));
        float rw = box_width(rb), rh = box_height(rb);
        const Vec4 rbb(0.0f, 0.48f, 0.8f, 0.6f);
        g_ov->push_rect(rx, ry, rw, rh, Vec4(0.0f, 0.48f, 0.8f, 0.15f), depth++, clip);
        g_ov->push_rect(rx, ry, rw, 1.0f, rbb, depth++, clip);
        g_ov->push_rect(rx, ry + rh, rw, 1.0f, rbb, depth++, clip);
        g_ov->push_rect(rx, ry, 1.0f, rh, rbb, depth++, clip);
        g_ov->push_rect(rx + rw, ry, 1.0f, rh, rbb, depth++, clip);
    }

    // Panel splitters (1-px bright + 1-px dark = 2-px visual handle). These sit on the
    // panel edges, i.e. outside the canvas, so from here on the clip is the empty box the
    // renderer reads as "no clip".
    clip = make_box(0, 0, 0, 0);
    {
        float sw_log = g_fb_w / g_dpi_scale;
        float top = GuiEditor::MENUBAR_H + GuiEditor::TOOLBAR_H;
        float bot = (float)window_h - GuiEditor::STATUSBAR_H;
        float tree_x = editor.get_tree_panel_w();
        float insp_x = sw_log - editor.get_inspector_w();
        g_ov->push_rect(tree_x - 1, top, 1, bot - top, Vec4(0.12f, 0.12f, 0.14f, 1.0f), depth++, clip);
        g_ov->push_rect(tree_x,     top, 1, bot - top, Vec4(0.35f, 0.35f, 0.38f, 1.0f), depth++, clip);
        g_ov->push_rect(insp_x - 1, top, 1, bot - top, Vec4(0.12f, 0.12f, 0.14f, 1.0f), depth++, clip);
        g_ov->push_rect(insp_x,     top, 1, bot - top, Vec4(0.35f, 0.35f, 0.38f, 1.0f), depth++, clip);
    }
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

    // The canvas overlay (background, grid, selection, handles, rubber band, splitters) is
    // rebuilt into this every frame and drawn as an immediate layer by the GUI renderer.
    // It used to be a second renderer's worth of hand-rolled primitives; going through the
    // GUI renderer gives every command its own clip box for free.
    WidgetRenderInfo overlay_ri;

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
        gui_rend.shutdown(); text_rast.shutdown();
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
            // Bound the design content to what the canvas viewport actually shows. The
            // context intersects a clip-enabled widget's rect into everything below it, so
            // setting it on the root clips the whole design tree — in DESIGN coordinates,
            // which is the space those widgets' clips live in. Set before begin_frame so
            // this frame's collect sees it.
            if (IGuiWidget* design_root = design_ctx->get_root()) {
                design_root->set_clip_enabled(true);
                design_root->set_clip_rect(design_visible_box(gui_editor.get_canvas()));
            }
            design_ctx->begin_frame(dt);
            design_ctx->end_frame();
        }

        // The overlay is rebuilt each frame as an immediate layer.
        overlay_ri.invalidate();
        build_canvas_overlay(gui_editor, overlay_ri, sh);

        // The whole frame goes through the GUI renderer: it owns the backbuffer bind (with
        // the stencil buffer clipping needs), the clears, the per-layer draws, and the
        // submit. Layers are drawn in order, back to front:
        //
        //   1. the design surface, through the zoom/pan projection
        //   2. the canvas overlay + panel splitters, in logical px
        //   3. the editor chrome, in logical px — opaque, so it goes last
        //
        // Each layer names its own projection, which is why this uses the layered form:
        // the design surface is zoomed and panned while the chrome is not.
        // The contexts are handed over as their already-collected render infos rather than
        // as `context` layers: GuiEditor::update() and the loop above already drive their
        // begin_frame/end_frame, and letting the renderer drive them too would advance each
        // context twice per frame.
        WidgetRenderInfo& editor_ri = const_cast<WidgetRenderInfo&>(gui_editor.get_render_info(win));
        WidgetRenderInfo* design_ri = design_ctx ? &const_cast<WidgetRenderInfo&>(design_ctx->get_render_info()) : nullptr;

        float design_proj[16];
        make_design_proj(gui_editor.get_canvas(), design_proj);
        GpuGuiRenderer::FrameLayer layers[3];
        int n = 0;
        if (design_ri) { layers[n].immediate = design_ri;  layers[n].proj = design_proj; ++n; }
        layers[n].immediate = &overlay_ri;  layers[n].proj = g_proj; ++n;
        layers[n].immediate = &editor_ri;   layers[n].proj = g_proj; ++n;
        gui_rend.render_window_frame(gfx, cmd, &text_rast, sw_p, sh_p,
                                     ClearColor(0.12f, 0.12f, 0.13f, 1.0f),
                                     layers, n, dt);
        gfx->present();
    }

    // Cleanup
    gui_editor.shutdown();
    editor_ctx->detach_window(win);
    destroy_gui_context(editor_ctx);

    gui_rend.shutdown(); g_gui = nullptr;
    text_rast.shutdown();
    destroy_commander(cmd); g_cmd = nullptr;
    destroy_device(dev);
    win->destroy();

    printf("GUI Editor closed.\n");
    return 0;
}
