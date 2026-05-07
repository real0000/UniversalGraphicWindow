/*
 * main.cpp - GUI Editor Standalone Application
 *
 * Launches the visual GUI editor using OpenGL rendering.
 * Uses the same rendering pattern as the gui_serialization example.
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
#include "gui/gui.hpp"
#include "gui/gui_context.hpp"
#include "gui/font/font.hpp"
#include "editor.hpp"
#include "input/input_keyboard.hpp"
#include "input/input_mouse.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

#include "api/glad.h"

using namespace window;
using namespace window::math;
using namespace window::gui;
using namespace window::gui::editor;

// ============================================================================
// Shader sources
// ============================================================================

static const char* g_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in vec4 aColor;
out vec3 TexCoord;
out vec4 vColor;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    vColor = aColor;
}
)";

static const char* g_fragment_shader = R"(
#version 330 core
in vec3 TexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2DArray uTexture;
void main() {
    if (TexCoord.z >= 0.0)
        FragColor = texture(uTexture, TexCoord) * vColor;
    else
        FragColor = vColor;
}
)";

// ============================================================================
// QuadRenderer
// ============================================================================

class QuadRenderer {
public:
    GLuint program = 0, vao = 0, vbo = 0;
    GLint  loc_projection = -1, loc_texture = -1;
    static const int MAX_VERTS = 8192 * 6;

    struct DrawCmd {
        GLuint first, count;
        GLenum primitive;
        bool scissor_active;
        int sc_x, sc_y, sc_w, sc_h;
    };

    std::vector<float>   m_verts;
    std::vector<DrawCmd> m_draw_cmds;
    GLuint m_cur_first = 0;
    GLenum m_cur_prim  = GL_TRIANGLES;
    bool m_scissor_active = false;
    int  m_sc_x = 0, m_sc_y = 0, m_sc_w = 0, m_sc_h = 0;

    bool init() {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &g_vertex_shader, nullptr);
        glCompileShader(vs);
        GLint ok;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(vs, 512, nullptr, log); printf("VS: %s\n", log); return false; }

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &g_fragment_shader, nullptr);
        glCompileShader(fs);
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(fs, 512, nullptr, log); printf("FS: %s\n", log); return false; }

        program = glCreateProgram();
        glAttachShader(program, vs); glAttachShader(program, fs);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(program, 512, nullptr, log); printf("Link: %s\n", log); return false; }
        glDeleteShader(vs); glDeleteShader(fs);

        loc_projection = glGetUniformLocation(program, "uProjection");
        loc_texture    = glGetUniformLocation(program, "uTexture");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_VERTS * 9 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9*sizeof(float), (void*)(5*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        m_verts.reserve(512*6*9);
        m_draw_cmds.reserve(256);
        return true;
    }

    void destroy() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }

    void set_projection(int w, int h) {
        float proj[16] = {
            2.0f/w, 0,       0, 0,
            0,     -2.0f/h,  0, 0,
            0,      0,      -1, 0,
           -1,      1,       0, 1
        };
        glUseProgram(program);
        glUniformMatrix4fv(loc_projection, 1, GL_FALSE, proj);
    }

    void set_scissor_state(bool active, int x=0, int y=0, int w=0, int h=0) {
        if (active == m_scissor_active &&
            (!active || (x==m_sc_x && y==m_sc_y && w==m_sc_w && h==m_sc_h))) return;
        close_batch();
        m_scissor_active=active; m_sc_x=x; m_sc_y=y; m_sc_w=w; m_sc_h=h;
    }

    void draw_rect(float px, float py, float pw, float ph, float r, float g, float b, float a=1.f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float v[] = {
            px,py,       0,0,-1, r,g,b,a,
            px+pw,py,    0,0,-1, r,g,b,a,
            px+pw,py+ph, 0,0,-1, r,g,b,a,
            px,py,       0,0,-1, r,g,b,a,
            px+pw,py+ph, 0,0,-1, r,g,b,a,
            px,py+ph,    0,0,-1, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v+54);
    }

    void draw_texture(int layer, float u0, float v0, float u1, float v1,
                      float px, float py, float pw, float ph,
                      float r=1.f, float g=1.f, float b=1.f, float a=1.f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float lf = (float)layer;
        float v[] = {
            px,py,       u0,v0,lf, r,g,b,a,
            px+pw,py,    u1,v0,lf, r,g,b,a,
            px+pw,py+ph, u1,v1,lf, r,g,b,a,
            px,py,       u0,v0,lf, r,g,b,a,
            px+pw,py+ph, u1,v1,lf, r,g,b,a,
            px,py+ph,    u0,v1,lf, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v+54);
    }

    void draw_circle(float cx, float cy, float radius, float r, float g, float b, float a=1.f) {
        close_batch(); m_cur_prim = GL_TRIANGLE_FAN;
        const int segs = 24;
        float verts[(segs+2)*9];
        verts[0]=cx; verts[1]=cy; verts[2]=0; verts[3]=0; verts[4]=-1; verts[5]=r; verts[6]=g; verts[7]=b; verts[8]=a;
        for (int i=0; i<=segs; i++) {
            float angle = 2.f*3.14159265f*i/segs;
            int base=(i+1)*9;
            verts[base]=cx+radius*cosf(angle); verts[base+1]=cy+radius*sinf(angle);
            verts[base+2]=0; verts[base+3]=0; verts[base+4]=-1;
            verts[base+5]=r; verts[base+6]=g; verts[base+7]=b; verts[base+8]=a;
        }
        m_verts.insert(m_verts.end(), verts, verts+(segs+2)*9);
        close_batch(); m_cur_prim = GL_TRIANGLES;
    }

    void flush_all(GLuint atlas_tex) {
        close_batch();
        if (m_draw_cmds.empty()) { m_verts.clear(); return; }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        size_t vbuf_size = m_verts.size() * sizeof(float);
        if (vbuf_size > (size_t)(MAX_VERTS * 9 * sizeof(float))) {
            // Reallocate if we exceeded the initial buffer size
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbuf_size, m_verts.data(), GL_DYNAMIC_DRAW);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)vbuf_size, m_verts.data());
        }

        glUseProgram(program);
        if (atlas_tex) {
            glUniform1i(loc_texture, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex);
        }

        glBindVertexArray(vao);
        for (const auto& dc : m_draw_cmds) {
            if (dc.scissor_active) { glEnable(GL_SCISSOR_TEST); glScissor(dc.sc_x,dc.sc_y,dc.sc_w,dc.sc_h); }
            else glDisable(GL_SCISSOR_TEST);
            glDrawArrays(dc.primitive, (GLint)dc.first, (GLsizei)dc.count);
        }
        glDisable(GL_SCISSOR_TEST);
        glBindVertexArray(0);
        m_verts.clear(); m_draw_cmds.clear(); m_cur_first=0; m_cur_prim=GL_TRIANGLES; m_scissor_active=false;
    }

private:
    void close_batch() {
        GLuint total = (GLuint)(m_verts.size()/9);
        GLuint count = total - m_cur_first;
        if (count == 0) return;
        m_draw_cmds.push_back({m_cur_first, count, m_cur_prim, m_scissor_active, m_sc_x, m_sc_y, m_sc_w, m_sc_h});
        m_cur_first = total;
    }
};

// ============================================================================
// Globals
// ============================================================================

static QuadRenderer*     g_renderer    = nullptr;
static font::IFontAtlas* g_font_atlas  = nullptr;
static float g_time = 0;
static int   g_window_h = 720;     // logical px
static float g_dpi_scale = 1.0f;   // physical / logical

static font::IFontRenderer* g_font_renderer = nullptr;
static font::ITextShaper*   g_text_shaper   = nullptr;
static font::IFontFace* g_font_ui = nullptr;
static font::IFontFace* g_font_small = nullptr;
static std::unordered_map<std::string, std::tuple<int,int,int,float,float,float,float>> g_text_cache;

struct TextEntry {
    int layer=-1, width=0, height=0;
    float u0=0, v0=0, u1=0, v1=0;
};

static TextEntry get_text_entry(const char* text, font::IFontFace* face) {
    if (!text || !text[0] || !face || !g_font_renderer) return {};
    std::string key = std::string(text) + "|" + std::to_string((int)face->get_size());
    auto it = g_text_cache.find(key);
    if (it != g_text_cache.end()) {
        auto& [layer,w,h,u0,v0,u1,v1] = it->second;
        return {layer,w,h,u0,v0,u1,v1};
    }

    font::RenderOptions ropts;
    ropts.antialias = font::AntiAliasMode::Grayscale;
    ropts.output_format = font::PixelFormat::RGBA8;
    font::TextLayoutOptions lopts;

    void* pixels = nullptr; int w=0, h=0; font::PixelFormat fmt;
    Vec4 white(1,1,1,1);
    font::Result r = g_font_renderer->render_text(face, text, -1, white, ropts, lopts, &pixels, &w, &h, &fmt);
    if (r != font::Result::Success || !pixels || w<=0 || h<=0) {
        if (pixels) g_font_renderer->free_bitmap(pixels);
        return {};
    }
    font::AtlasEntry ar = g_font_atlas->add(pixels, w, h);
    g_font_renderer->free_bitmap(pixels);
    if (!ar.valid()) return {};

    TextEntry entry; entry.layer=ar.layer; entry.width=w; entry.height=h;
    entry.u0=ar.u0; entry.v0=ar.v0; entry.u1=ar.u1; entry.v1=ar.v1;
    g_text_cache[key] = std::make_tuple(ar.layer, w, h, ar.u0, ar.v0, ar.u1, ar.v1);
    return entry;
}

static float measure_text_width_n(const char* text, int n, font::IFontFace* face = nullptr) {
    if (!text || n <= 0) return 0;
    if (!face) face = g_font_ui;
    int len = (int)strlen(text);
    if (n > len) n = len;
    if (g_text_shaper) {
        std::vector<float> carets;
        g_text_shaper->get_caret_positions(face, text, len, carets);
        if (n < (int)carets.size()) return carets[n];
        if (!carets.empty()) return carets.back();
    }
    // Fallback: render substring bitmap
    std::string sub(text, n);
    TextEntry e = get_text_entry(sub.c_str(), face);
    return (float)e.width;
}

// ============================================================================
// Draw render info
// ============================================================================

static void draw_render_info(const gui::WidgetRenderInfo& ri) {
    using Pool = gui::WidgetRenderInfo::DrawRef::Pool;
    bool scissor_on = false;
    for (const auto& ref : ri.get_draw_order()) {
        if (ref.clip_changed) {
            if (!box_is_empty(ref.clip)) {
                // Logical → physical for glScissor.
                float s = g_dpi_scale;
                float bx=x(box_min(ref.clip)), by=y(box_min(ref.clip));
                float bw=box_width(ref.clip), bh=box_height(ref.clip);
                g_renderer->set_scissor_state(true, (int)(bx*s), (int)((g_window_h-(by+bh))*s), (int)(bw*s), (int)(bh*s));
                scissor_on = true;
            } else {
                g_renderer->set_scissor_state(false);
                scissor_on = false;
            }
        }
        switch (ref.pool) {
            case Pool::Color: {
                const auto& cmd = ri.colors[ref.index];
                float px=x(box_min(cmd.dest)), py=y(box_min(cmd.dest));
                float pw=box_width(cmd.dest), ph=box_height(cmd.dest);
                if (cmd.shape == gui::DrawShape::Circle)
                    g_renderer->draw_circle(px+pw*.5f, py+ph*.5f, pw*.5f, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                else
                    g_renderer->draw_rect(px, py, pw, ph, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                break;
            }
            case Pool::Texture: {
                const auto& cmd = ri.textures[ref.index];
                if (cmd.atlas_layer < 0) break;
                float px=x(box_min(cmd.dest)), py=y(box_min(cmd.dest));
                float pw=box_width(cmd.dest), ph=box_height(cmd.dest);
                float u0=x(box_min(cmd.uv)), v0=y(box_min(cmd.uv));
                float u1=x(box_max(cmd.uv)), v1=y(box_max(cmd.uv));
                g_renderer->draw_texture(cmd.atlas_layer, u0,v0,u1,v1, px,py,pw,ph,
                                         cmd.tint.x, cmd.tint.y, cmd.tint.z, cmd.tint.w);
                break;
            }
            default: break;
        }
    }
    if (scissor_on) g_renderer->set_scissor_state(false);
}

// ============================================================================
// Draw editor canvas overlay (grid, selection handles, rubber band)
// ============================================================================

static void draw_canvas_overlay(const GuiEditor& editor, int window_h) {
    const EditorCanvas& canvas = const_cast<GuiEditor&>(editor).get_canvas();

    // Draw grid
    const CanvasGrid& grid = canvas.get_grid();
    if (grid.visible) {
        Box vp = canvas.get_viewport_bounds();
        float vpx = x(box_min(vp)), vpy = y(box_min(vp));
        float vpw = box_width(vp), vph = box_height(vp);

        // Enable scissor for canvas area (logical → physical px).
        {
            float s = g_dpi_scale;
            g_renderer->set_scissor_state(true, (int)(vpx*s), (int)((window_h-(vpy+vph))*s), (int)(vpw*s), (int)(vph*s));
        }

        float zoom = canvas.get_zoom();
        Vec2 pan = canvas.get_pan();

        // Calculate visible range in canvas coordinates
        float start_x = -x(pan) / zoom;
        float start_y = -y(pan) / zoom;
        float end_x = start_x + vpw / zoom;
        float end_y = start_y + vph / zoom;

        float spacing = grid.spacing;
        if (spacing * zoom < 5.0f) spacing = grid.major_spacing; // skip fine grid at low zoom

        // Minor grid lines
        for (float gx = std::floor(start_x / spacing) * spacing; gx <= end_x; gx += spacing) {
            Vec2 sp = canvas.canvas_to_screen(Vec2(gx, start_y));
            Vec2 ep = canvas.canvas_to_screen(Vec2(gx, end_y));
            bool major = (std::fmod(std::abs(gx), grid.major_spacing) < 0.01f);
            const Vec4& col = major ? grid.major_color : grid.color;
            g_renderer->draw_rect(x(sp), y(sp), 1.0f, y(ep) - y(sp), col.x, col.y, col.z, col.w);
        }
        for (float gy = std::floor(start_y / spacing) * spacing; gy <= end_y; gy += spacing) {
            Vec2 sp = canvas.canvas_to_screen(Vec2(start_x, gy));
            Vec2 ep = canvas.canvas_to_screen(Vec2(end_x, gy));
            bool major = (std::fmod(std::abs(gy), grid.major_spacing) < 0.01f);
            const Vec4& col = major ? grid.major_color : grid.color;
            g_renderer->draw_rect(x(sp), y(sp), x(ep) - x(sp), 1.0f, col.x, col.y, col.z, col.w);
        }

        // Draw origin axes (canvas 0,0) — slightly brighter than major grid
        Vec2 ox_s = canvas.canvas_to_screen(Vec2(0, start_y));
        Vec2 ox_e = canvas.canvas_to_screen(Vec2(0, end_y));
        g_renderer->draw_rect(x(ox_s), y(ox_s), 1.0f, y(ox_e)-y(ox_s), 0.35f, 0.35f, 0.4f, 0.8f);
        Vec2 oy_s = canvas.canvas_to_screen(Vec2(start_x, 0));
        Vec2 oy_e = canvas.canvas_to_screen(Vec2(end_x, 0));
        g_renderer->draw_rect(x(oy_s), y(oy_s), x(oy_e)-x(oy_s), 1.0f, 0.35f, 0.35f, 0.4f, 0.8f);

        g_renderer->set_scissor_state(false);
    }

    // Draw selection outlines
    const SelectionInfo& sel = canvas.get_selection();
    for (auto* w : sel.widgets) {
        Box b = w->get_bounds();
        Vec2 tl = canvas.canvas_to_screen(box_min(b));
        Vec2 br = canvas.canvas_to_screen(box_max(b));
        float tlx = x(tl), tly = y(tl), brx = x(br), bry = y(br);
        float sw = brx - tlx, sh = bry - tly;

        // Dashed selection outline (solid blue)
        g_renderer->draw_rect(tlx, tly, sw, 1.0f, 0.0f, 0.48f, 0.8f, 1.0f);
        g_renderer->draw_rect(tlx, bry, sw, 1.0f, 0.0f, 0.48f, 0.8f, 1.0f);
        g_renderer->draw_rect(tlx, tly, 1.0f, sh, 0.0f, 0.48f, 0.8f, 1.0f);
        g_renderer->draw_rect(brx, tly, 1.0f, sh, 0.0f, 0.48f, 0.8f, 1.0f);
    }

    // Draw resize handles
    std::vector<EditorCanvas::HandleRect> handles;
    canvas.get_selection_handles(handles);
    for (const auto& h : handles) {
        float hx = x(box_min(h.rect)), hy = y(box_min(h.rect));
        float hw = box_width(h.rect), hh = box_height(h.rect);
        if (h.hovered)
            g_renderer->draw_rect(hx, hy, hw, hh, 0.0f, 0.48f, 0.8f, 1.0f);
        else
            g_renderer->draw_rect(hx, hy, hw, hh, 1.0f, 1.0f, 1.0f, 0.9f);
        // Inner border
        g_renderer->draw_rect(hx+1, hy+1, hw-2, hh-2, 0.0f, 0.48f, 0.8f, 1.0f);
    }

    // Draw rubber band
    if (canvas.get_mode() == CanvasMode::RubberBand) {
        Box rb = canvas.get_rubber_band_rect();
        float rx = x(box_min(rb)), ry = y(box_min(rb));
        float rw = box_width(rb), rh = box_height(rb);
        g_renderer->draw_rect(rx, ry, rw, rh, 0.0f, 0.48f, 0.8f, 0.15f);
        // Border
        g_renderer->draw_rect(rx, ry, rw, 1.0f, 0.0f, 0.48f, 0.8f, 0.6f);
        g_renderer->draw_rect(rx, ry+rh, rw, 1.0f, 0.0f, 0.48f, 0.8f, 0.6f);
        g_renderer->draw_rect(rx, ry, 1.0f, rh, 0.0f, 0.48f, 0.8f, 0.6f);
        g_renderer->draw_rect(rx+rw, ry, 1.0f, rh, 0.0f, 0.48f, 0.8f, 0.6f);
    }
}

// ============================================================================
// Draw design canvas widgets (the user-designed widgets, transformed by zoom/pan)
// ============================================================================

static void draw_design_widgets(const GuiEditor& editor, IGuiContext* design_ctx,
                                Window* win, int window_h) {
    const EditorCanvas& canvas = const_cast<GuiEditor&>(editor).get_canvas();
    Box vp = canvas.get_viewport_bounds();
    float vpx = x(box_min(vp)), vpy = y(box_min(vp));
    float vpw = box_width(vp), vph = box_height(vp);

    // Clip to canvas viewport (logical → physical px).
    {
        float s = g_dpi_scale;
        g_renderer->set_scissor_state(true, (int)(vpx*s), (int)((window_h-(vpy+vph))*s), (int)(vpw*s), (int)(vph*s));
    }

    // Draw a canvas background
    g_renderer->draw_rect(vpx, vpy, vpw, vph, 0.16f, 0.16f, 0.17f, 1.0f);

    // Draw design widgets transformed by canvas zoom/pan
    IGuiWidget* root = design_ctx->get_root();
    if (!root) { g_renderer->set_scissor_state(false); return; }

    const WidgetRenderInfo& ri = design_ctx->get_render_info();
    using Pool = WidgetRenderInfo::DrawRef::Pool;

    float zoom = canvas.get_zoom();
    Vec2 pan = canvas.get_pan();
    float ox = vpx + x(pan);
    float oy = vpy + y(pan);

    for (const auto& ref : ri.get_draw_order()) {
        switch (ref.pool) {
            case Pool::Color: {
                const auto& cmd = ri.colors[ref.index];
                float px = x(box_min(cmd.dest)) * zoom + ox;
                float py = y(box_min(cmd.dest)) * zoom + oy;
                float pw = box_width(cmd.dest) * zoom;
                float ph = box_height(cmd.dest) * zoom;
                if (cmd.shape == DrawShape::Circle)
                    g_renderer->draw_circle(px+pw*.5f, py+ph*.5f, pw*.5f,
                                            cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                else
                    g_renderer->draw_rect(px, py, pw, ph,
                                          cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                break;
            }
            case Pool::Texture: {
                const auto& cmd = ri.textures[ref.index];
                if (cmd.atlas_layer < 0) break;
                float px = x(box_min(cmd.dest)) * zoom + ox;
                float py = y(box_min(cmd.dest)) * zoom + oy;
                float pw = box_width(cmd.dest) * zoom;
                float ph = box_height(cmd.dest) * zoom;
                float u0=x(box_min(cmd.uv)), v0=y(box_min(cmd.uv));
                float u1=x(box_max(cmd.uv)), v1=y(box_max(cmd.uv));
                g_renderer->draw_texture(cmd.atlas_layer, u0,v0,u1,v1, px,py,pw,ph,
                                         cmd.tint.x, cmd.tint.y, cmd.tint.z, cmd.tint.w);
                break;
            }
            default: break;
        }
    }

    g_renderer->set_scissor_state(false);
}

// ============================================================================
// Text measurer and rasterizer
// ============================================================================

struct FontTextMeasurer : ITextMeasurer {
    font::IFontFace* face = nullptr;
    Vec2 measure_text(const char* text, float, const char*) override {
        if (!text || !text[0] || !face) return {0,0};
        TextEntry e = get_text_entry(text, face);
        return {(float)e.width, (float)e.height};
    }
    float get_line_height(float font_size, const char*) override { return font_size * 1.2f; }
};

struct GuiTextRasterizer : IGuiTextRasterizer {
    font::IFontFace* face_ui = nullptr;
    font::IFontFace* face_small = nullptr;

    TextQuad rasterize(const char* text, float font_size, const char*) override {
        font::IFontFace* face = (font_size <= 10.f) ? face_small : face_ui;
        TextEntry e = get_text_entry(text, face);
        TextQuad q; q.atlas_layer=e.layer; q.u0=e.u0; q.v0=e.v0; q.u1=e.u1; q.v1=e.v1;
        q.width=e.width; q.height=e.height; return q;
    }
    float measure_advance(const char* text, int n, float font_size, const char*) override {
        font::IFontFace* face = (font_size <= 10.f) ? face_small : face_ui;
        return measure_text_width_n(text, n, face);
    }
    float get_time() const override { return g_time; }
};

// ============================================================================
// Keyboard handler for editor shortcuts
// ============================================================================

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

    // Init renderer
    QuadRenderer renderer;
    if (!renderer.init()) { printf("Failed to init renderer\n"); win->destroy(); return 1; }
    g_renderer = &renderer;

    // Font atlas with GL callbacks
    {
        GLint max_size, max_layers;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
        int tile_w = std::min(max_size, 4096);
        int tile_h = std::min(max_size, 4096);

        g_font_atlas = font::create_font_atlas();
        g_font_atlas->set_tile_size(tile_w, tile_h);
        g_font_atlas->set_max_layers(std::min(max_layers, 2048));
        g_font_atlas->set_callbacks(
            [](int tw, int th, int depth) -> uintptr_t {
                GLuint tex; glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tw, th, depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
                return (uintptr_t)tex;
            },
            [](uintptr_t old_handle, int tw, int th, int old_depth, int new_depth) -> uintptr_t {
                GLuint old_tex = (GLuint)old_handle, new_tex;
                glGenTextures(1, &new_tex);
                glBindTexture(GL_TEXTURE_2D_ARRAY, new_tex);
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tw, th, new_depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
                if (old_tex && old_depth > 0)
                    glCopyImageSubData(old_tex, GL_TEXTURE_2D_ARRAY, 0,0,0,0, new_tex, GL_TEXTURE_2D_ARRAY, 0,0,0,0, tw, th, old_depth);
                glDeleteTextures(1, &old_tex);
                return (uintptr_t)new_tex;
            },
            [](uintptr_t handle, const void* rgba8, int x, int y, int layer, int w, int h) {
                GLuint tex = (GLuint)handle;
                glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, layer, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            },
            [](uintptr_t handle) { GLuint tex = (GLuint)handle; if (tex) glDeleteTextures(1, &tex); }
        );
    }

    // Font system
    font::Result font_result;
    font::IFontLibrary* font_lib = font::create_font_library(font::FontBackend::Auto, &font_result);
    if (!font_lib) { printf("Failed to create font library\n"); renderer.destroy(); win->destroy(); return 1; }

    g_font_renderer = font::create_font_renderer(font_lib, &font_result);
    if (!g_font_renderer) { printf("Failed to create font renderer\n"); font::destroy_font_library(font_lib); renderer.destroy(); win->destroy(); return 1; }

    g_text_shaper = font::create_text_shaper(font_lib, &font_result);

    g_font_ui = font_lib->load_system_font(font::FontDescriptor::create("Segoe UI", 14.f), nullptr);
    if (!g_font_ui) g_font_ui = font_lib->load_system_font(font::FontDescriptor::create("Arial", 14.f), nullptr);
    if (!g_font_ui) g_font_ui = font_lib->get_default_font(14.f, nullptr);

    g_font_small = font_lib->load_system_font(font::FontDescriptor::create("Segoe UI", 12.f), nullptr);
    if (!g_font_small) g_font_small = font_lib->load_system_font(font::FontDescriptor::create("Arial", 12.f), nullptr);
    if (!g_font_small) g_font_small = font_lib->get_default_font(12.f, nullptr);

    if (!g_font_ui || !g_font_small) {
        printf("Failed to load fonts\n");
        font::destroy_font_renderer(g_font_renderer);
        font::destroy_font_library(font_lib);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    font::IFontFallbackChain* fallback = font::create_fallback_chain_with_defaults(font_lib, g_font_ui, 14.f);
    if (fallback) {
        g_font_renderer->set_fallback_chain(fallback);
        if (g_text_shaper) g_text_shaper->set_fallback_chain(fallback);
    }

    // Create editor GUI context
    GuiResult gresult;
    IGuiContext* editor_ctx = create_gui_context(&gresult);
    if (!editor_ctx) { printf("Failed to create GUI context\n"); return 1; }

    editor_ctx->attach_window(win);
    // Logical-extent layout: Viewport bounds and root bounds use logical
    // (CSS-style) px, so the editor's hardcoded sizes (MENUBAR_H = 26 etc)
    // keep their physical size on Hi-DPI. The renderer projects logical-px
    // space onto a physical-px glViewport.
    float dpi_scale = win->get_dpi_scale();
    g_dpi_scale = dpi_scale;
    int   sw_phys, sh_phys; win->get_size(&sw_phys, &sh_phys);
    float lw = sw_phys / dpi_scale, lh = sh_phys / dpi_scale;
    gui::Viewport vp; vp.id=0; vp.bounds=make_box(0,0,lw,lh); vp.scale=1.f;
    editor_ctx->add_viewport(vp);
    editor_ctx->get_root()->set_bounds(make_box(0,0,lw,lh));

    // Text measurer & rasterizer
    FontTextMeasurer text_measurer; text_measurer.face = g_font_ui;
    GuiTextRasterizer text_rasterizer;
    text_rasterizer.face_ui = g_font_ui;
    text_rasterizer.face_small = g_font_small;
    editor_ctx->set_text_rasterizer(&text_rasterizer);

    // Initialize the GUI editor
    GuiEditor gui_editor;
    if (!gui_editor.initialize(editor_ctx, win)) {
        printf("Failed to initialize GUI editor\n");
        destroy_gui_context(editor_ctx);
        font::destroy_font_renderer(g_font_renderer);
        font::destroy_font_library(font_lib);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    // Set up text rasterizer for design context too
    IGuiContext* design_ctx = gui_editor.get_design_context();
    if (design_ctx) {
        design_ctx->set_text_rasterizer(&text_rasterizer);
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

        // Update viewport (logical-px space).
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        editor_ctx->update_viewport(vp);
        editor_ctx->get_root()->set_bounds(make_box(0, 0, (float)sw, (float)sh));

        // Update editor
        static int frame_count = 0;
        bool log = (frame_count < 3);
        if (log) printf("[frame %d] update\n", frame_count);
        gui_editor.update(dt);

        // Also update design context frame
        if (design_ctx) {
            design_ctx->begin_frame(dt);
            design_ctx->end_frame();
        }

        // Render: glViewport covers physical px, projection covers logical
        // px. The vertex shader maps logical units → NDC → physical.
        glViewport(0, 0, sw_p, sh_p);
        glClearColor(0.12f, 0.12f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.set_projection(sw, sh);

        // 1. Draw design canvas widgets (transformed by zoom/pan) — must come
        //    before editor chrome so menus/popups render on top.
        if (log) printf("[frame %d] draw_design_widgets\n", frame_count);
        draw_design_widgets(gui_editor, design_ctx, win, sh);

        // 2. Draw canvas overlay (grid, selection handles, rubber band)
        if (log) printf("[frame %d] draw_canvas_overlay\n", frame_count);
        draw_canvas_overlay(gui_editor, sh);

        // 2b. Draw panel splitters
        {
            float top = GuiEditor::MENUBAR_H + GuiEditor::TOOLBAR_H;
            float bot = (float)sh - GuiEditor::STATUSBAR_H;
            float tree_x = gui_editor.get_tree_panel_w();
            float insp_x = (float)sw - gui_editor.get_inspector_w();
            // Splitter lines (1-px bright, 1-px dark = 2px visual handle)
            renderer.draw_rect(tree_x-1, top, 1, bot-top, 0.12f, 0.12f, 0.14f, 1.0f);
            renderer.draw_rect(tree_x,   top, 1, bot-top, 0.35f, 0.35f, 0.38f, 1.0f);
            renderer.draw_rect(insp_x-1, top, 1, bot-top, 0.12f, 0.12f, 0.14f, 1.0f);
            renderer.draw_rect(insp_x,   top, 1, bot-top, 0.35f, 0.35f, 0.38f, 1.0f);
        }

        // 3. Draw editor chrome last (menubar, toolbar, panels, overlays/menus on top)
        if (log) printf("[frame %d] get_render_info\n", frame_count);
        const WidgetRenderInfo& editor_ri = gui_editor.get_render_info(win);
        if (log) printf("[frame %d] draw_render_info (%zu draw cmds)\n", frame_count, editor_ri.get_draw_order().size());
        draw_render_info(editor_ri);

        // Flush everything
        if (log) printf("[frame %d] flush_all (atlas=%u)\n", frame_count, (unsigned)g_font_atlas->get_gpu_handle());
        g_renderer->flush_all((GLuint)g_font_atlas->get_gpu_handle());

        if (log) printf("[frame %d] present\n", frame_count);
        gfx->present();
        ++frame_count;
    }

    // Cleanup
    gui_editor.shutdown();
    g_text_cache.clear();
    font::destroy_font_atlas(g_font_atlas); g_font_atlas = nullptr;
    renderer.destroy(); g_renderer = nullptr;
    editor_ctx->detach_window(win);
    destroy_gui_context(editor_ctx);
    font::destroy_font_renderer(g_font_renderer); g_font_renderer = nullptr;
    if (g_text_shaper) { font::destroy_text_shaper(g_text_shaper); g_text_shaper = nullptr; }
    if (fallback) font::destroy_fallback_chain(fallback);
    font::destroy_font_library(font_lib);
    win->destroy();

    printf("GUI Editor closed.\n");
    return 0;
}
