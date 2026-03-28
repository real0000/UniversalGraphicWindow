/*
 * gui.cpp - Complex Widget Tree Showcase (OpenGL)
 *
 * Demonstrates a deeply nested GUI widget tree with:
 *   - Nested split panels (sidebar | center, tree / propgrid, tabs / output)
 *   - Tab control with real content in each tab (editor, controls, color picker)
 *   - IDE-like layout: menubar, toolbar, sidebar, editor area, output panel
 *   - Context menus, modal dialogs, scrollbars
 */

#include "window.hpp"
#include "gui/gui.hpp"
#include "gui/font/font.hpp"
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

// Modern OpenGL via GLAD
#include "api/glad.h"

using namespace window;
using namespace window::math;
using namespace window::gui;

// ============================================================================
// Shader sources (with texture support)
// ============================================================================

static const char* g_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aTexCoord;  // (u, v, layer); layer < 0 = solid
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
// QuadRenderer - Frame-deferred indirect draw renderer with font atlas support
// ============================================================================

// Vertex layout: x, y, u, v, layer, r, g, b, a  (9 floats per vertex)
// layer < 0  → solid colour (no texture sample)
// layer >= 0 → sampler2DArray layer index
// All geometry recorded during frame traversal, ONE vertex upload + ONE indirect
// buffer upload per frame; grouped by (primitive, scissor) for multi-draw calls.
class QuadRenderer {
public:
    GLuint program = 0;
    GLuint vao     = 0;
    GLuint vbo     = 0;
    GLuint ibo     = 0;   // GL_DRAW_INDIRECT_BUFFER
    GLint  loc_projection = -1;
    GLint  loc_texture    = -1;

    static const int MAX_VERTS = 8192 * 6;

    // Per-draw metadata — no texture_id needed (atlas is always bound)
    struct DrawCmd {
        GLuint first, count;
        GLenum primitive;
        bool   scissor_active;
        int    sc_x, sc_y, sc_w, sc_h;
    };

    // Layout required by glMultiDrawArraysIndirect (GL spec)
    struct IndirectCmd {
        GLuint count;
        GLuint instanceCount; // always 1
        GLuint first;
        GLuint baseInstance;  // always 0
    };

    std::vector<float>   m_verts;
    std::vector<DrawCmd> m_draw_cmds;

    // Open-batch cursor
    GLuint m_cur_first = 0;
    GLenum m_cur_prim  = GL_TRIANGLES;

    // Scissor state updated by set_scissor_state()
    bool m_scissor_active = false;
    int  m_sc_x = 0, m_sc_y = 0, m_sc_w = 0, m_sc_h = 0;

    bool init() {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &g_vertex_shader, nullptr);
        glCompileShader(vs);
        GLint ok;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(vs, 512, nullptr, log); printf("VS error: %s\n", log); return false; }

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &g_fragment_shader, nullptr);
        glCompileShader(fs);
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(fs, 512, nullptr, log); printf("FS error: %s\n", log); return false; }

        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(program, 512, nullptr, log); printf("Link error: %s\n", log); return false; }
        glDeleteShader(vs);
        glDeleteShader(fs);

        loc_projection = glGetUniformLocation(program, "uProjection");
        loc_texture    = glGetUniformLocation(program, "uTexture");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_VERTS * 9 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        // Position  (loc 0): 2 floats, stride=9, offset=0
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        // TexCoord  (loc 1): 3 floats, stride=9, offset=2  (u, v, layer)
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        // Color     (loc 2): 4 floats, stride=9, offset=5
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        glGenBuffers(1, &ibo);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ibo);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(IndirectCmd) * 4096, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

        m_verts.reserve(512 * 6 * 9);
        m_draw_cmds.reserve(256);
        return true;
    }

    void destroy() {
        if (ibo) glDeleteBuffers(1, &ibo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }

    void set_projection(int width, int height) {
        float proj[16] = {
            2.0f / width, 0.0f,           0.0f, 0.0f,
            0.0f,        -2.0f / height,  0.0f, 0.0f,
            0.0f,         0.0f,          -1.0f, 0.0f,
           -1.0f,         1.0f,           0.0f, 1.0f
        };
        glUseProgram(program);
        glUniformMatrix4fv(loc_projection, 1, GL_FALSE, proj);
    }

    // Called by push/pop_scissor — records state change, no GL calls.
    void set_scissor_state(bool active, int x = 0, int y = 0, int w = 0, int h = 0) {
        if (active == m_scissor_active &&
            (!active || (x == m_sc_x && y == m_sc_y && w == m_sc_w && h == m_sc_h)))
            return;
        close_batch();
        m_scissor_active = active;
        m_sc_x = x; m_sc_y = y; m_sc_w = w; m_sc_h = h;
    }

    // Append a solid-colour rect (layer = -1 → no texture sample).
    void draw_rect(float px, float py, float pw, float ph,
                   float r, float g, float b, float a = 1.0f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float v[] = {
            px,      py,      0,0,-1, r,g,b,a,
            px + pw, py,      0,0,-1, r,g,b,a,
            px + pw, py + ph, 0,0,-1, r,g,b,a,
            px,      py,      0,0,-1, r,g,b,a,
            px + pw, py + ph, 0,0,-1, r,g,b,a,
            px,      py + ph, 0,0,-1, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v + 54);
    }

    // Append a textured quad from the atlas (layer >= 0; u0/v0/u1/v1 within the tile).
    // No batch close needed — same primitive, same atlas; batches freely with solid rects.
    void draw_texture(int layer, float u0, float v0, float u1, float v1,
                      float px, float py, float pw, float ph,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float lf = (float)layer;
        float v[] = {
            px,      py,      u0,v0,lf, r,g,b,a,
            px + pw, py,      u1,v0,lf, r,g,b,a,
            px + pw, py + ph, u1,v1,lf, r,g,b,a,
            px,      py,      u0,v0,lf, r,g,b,a,
            px + pw, py + ph, u1,v1,lf, r,g,b,a,
            px,      py + ph, u0,v1,lf, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v + 54);
    }

    // Append a filled circle (TRIANGLE_FAN, sealed as its own DrawCmd).
    void draw_circle(float cx, float cy, float radius,
                     float r, float g, float b, float a = 1.0f) {
        close_batch();
        m_cur_prim = GL_TRIANGLE_FAN;
        const int segs = 24;
        float verts[(segs + 2) * 9];
        verts[0]=cx; verts[1]=cy; verts[2]=0; verts[3]=0; verts[4]=-1; verts[5]=r; verts[6]=g; verts[7]=b; verts[8]=a;
        for (int i = 0; i <= segs; i++) {
            float angle = 2.0f * 3.14159265f * i / segs;
            int base = (i + 1) * 9;
            verts[base+0]=cx+radius*cosf(angle); verts[base+1]=cy+radius*sinf(angle);
            verts[base+2]=0; verts[base+3]=0; verts[base+4]=-1;
            verts[base+5]=r; verts[base+6]=g; verts[base+7]=b; verts[base+8]=a;
        }
        m_verts.insert(m_verts.end(), verts, verts + (segs + 2) * 9);
        close_batch();
        m_cur_prim = GL_TRIANGLES;
    }

    // Upload all frame geometry once and issue indirect draw calls.
    // atlas_tex: the GL_TEXTURE_2D_ARRAY texture from FontAtlas (0 = skip bind).
    // Call once per frame after all draw_* calls. Resets all recorded state.
    void flush_all(GLuint atlas_tex) {
        close_batch();
        if (m_draw_cmds.empty()) {
            m_verts.clear();
            return;
        }

        // 1. One vertex upload for the entire frame
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(m_verts.size() * sizeof(float)), m_verts.data());

        // 2. Build and upload indirect commands
        std::vector<IndirectCmd> indirect;
        indirect.reserve(m_draw_cmds.size());
        for (const DrawCmd& dc : m_draw_cmds)
            indirect.push_back({dc.count, 1, dc.first, 0});

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ibo);
        glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0,
                        (GLsizeiptr)(indirect.size() * sizeof(IndirectCmd)), indirect.data());

        // 3. Bind atlas texture once for the whole frame
        glUseProgram(program);
        if (atlas_tex) {
            glUniform1i(loc_texture, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex);
        }

        // 4. Issue draws, batching consecutive same-state/same-primitive commands
        glBindVertexArray(vao);
        int i = 0, n = (int)m_draw_cmds.size();
        while (i < n) {
            const DrawCmd& dc = m_draw_cmds[i];

            if (dc.scissor_active) {
                glEnable(GL_SCISSOR_TEST);
                glScissor(dc.sc_x, dc.sc_y, dc.sc_w, dc.sc_h);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }

            // Find run of consecutive DrawCmds sharing same primitive + scissor
            int j = i + 1;
            while (j < n) {
                const DrawCmd& d = m_draw_cmds[j];
                if (d.primitive      != dc.primitive      ||
                    d.scissor_active != dc.scissor_active  ||
                    d.sc_x != dc.sc_x || d.sc_y != dc.sc_y ||
                    d.sc_w != dc.sc_w || d.sc_h != dc.sc_h)
                    break;
                ++j;
            }

            GLintptr offset = (GLintptr)(i * sizeof(IndirectCmd));
            glMultiDrawArraysIndirect(dc.primitive, (const void*)offset, j - i, sizeof(IndirectCmd));
            i = j;
        }

        glDisable(GL_SCISSOR_TEST);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        glBindVertexArray(0);

        // Reset for next frame
        m_verts.clear();
        m_draw_cmds.clear();
        m_cur_first       = 0;
        m_cur_prim        = GL_TRIANGLES;
        m_scissor_active  = false;
    }

private:
    // Seal the currently open vertex run into a DrawCmd record.
    void close_batch() {
        GLuint total = (GLuint)(m_verts.size() / 9);
        GLuint count = total - m_cur_first;
        if (count == 0) return;
        m_draw_cmds.push_back({m_cur_first, count, m_cur_prim,
                               m_scissor_active,
                               m_sc_x, m_sc_y, m_sc_w, m_sc_h});
        m_cur_first = total;
    }
};

static QuadRenderer*    g_renderer   = nullptr;
static font::IFontAtlas* g_font_atlas = nullptr;
static float g_time = 0; // for cursor blink
static int g_window_h = 720; // for glScissor Y-flip


// ============================================================================
// Drawing Helpers — emit directly to g_renderer
// ============================================================================

static void draw_rect(float px, float py, float pw, float ph,
                      float r, float g, float b, float a = 1.0f) {
    g_renderer->draw_rect(px, py, pw, ph, r, g, b, a);
}

static void draw_rect_v4(float px, float py, float pw, float ph, const Vec4& c) {
    draw_rect(px, py, pw, ph, c.x, c.y, c.z, c.w);
}

static void draw_box(const Box& b, const Vec4& c) {
    float px = x(box_min(b)), py = y(box_min(b));
    float pw = box_width(b), ph = box_height(b);
    draw_rect(px, py, pw, ph, c.x, c.y, c.z, c.w);
}

static void draw_rect_outline(float px, float py, float pw, float ph,
                               float r, float g, float b, float a = 1.0f) {
    float bw = 1.0f;
    draw_rect(px, py, pw, bw, r, g, b, a);
    draw_rect(px, py + ph - bw, pw, bw, r, g, b, a);
    draw_rect(px, py, bw, ph, r, g, b, a);
    draw_rect(px + pw - bw, py, bw, ph, r, g, b, a);
}

static void draw_circle(float cx, float cy, float radius,
                         float r, float g, float b, float a = 1.0f) {
    g_renderer->draw_circle(cx, cy, radius, r, g, b, a);
}

// Atlas-texture draw — used by text helpers during replay.
static void draw_texture_atlas(int layer, float u0, float v0, float u1, float v1,
                                float px, float py, float pw, float ph,
                                float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f) {
    if (layer < 0) return;
    g_renderer->draw_texture(layer, u0, v0, u1, v1, px, py, pw, ph, r, g, b, a);
}

// ============================================================================
// Text Rendering Cache
// ============================================================================

struct TextEntry {
    int   layer  = -1;         // atlas layer (-1 = invalid)
    int   width  = 0;
    int   height = 0;
    float u0 = 0, v0 = 0;     // UV extents within the atlas tile
    float u1 = 0, v1 = 0;
};

static font::IFontRenderer* g_font_renderer = nullptr;
static font::IFontFace* g_font_ui = nullptr;
static font::IFontFace* g_font_small = nullptr;
static std::unordered_map<std::string, TextEntry> g_text_cache;

static TextEntry get_text_entry(const char* text, font::IFontFace* face) {
    if (!text || !text[0] || !face || !g_font_renderer) return {};

    std::string key = std::string(text) + "|" + std::to_string((int)face->get_size());
    auto it = g_text_cache.find(key);
    if (it != g_text_cache.end()) return it->second;

    font::RenderOptions ropts;
    ropts.antialias = font::AntiAliasMode::Grayscale;
    ropts.output_format = font::PixelFormat::RGBA8;

    font::TextLayoutOptions lopts;

    void* pixels = nullptr;
    int w = 0, h = 0;
    font::PixelFormat fmt;
    Vec4 white(1.0f, 1.0f, 1.0f, 1.0f);

    font::Result r = g_font_renderer->render_text(face, text, -1, white, ropts, lopts,
                                                    &pixels, &w, &h, &fmt);
    if (r != font::Result::Success || !pixels || w <= 0 || h <= 0) {
        if (pixels) g_font_renderer->free_bitmap(pixels);
        return {};
    }

    font::AtlasEntry ar = g_font_atlas->add(pixels, w, h);
    g_font_renderer->free_bitmap(pixels);
    if (!ar.valid()) return {};

    TextEntry entry;
    entry.layer  = ar.layer;
    entry.width  = w;
    entry.height = h;
    entry.u0 = ar.u0; entry.v0 = ar.v0;
    entry.u1 = ar.u1; entry.v1 = ar.v1;
    g_text_cache[key] = entry;
    return entry;
}

static void draw_text(const char* text, float px, float py, const Vec4& color,
                      font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.layer < 0) return;
    draw_texture_atlas(e.layer, e.u0, e.v0, e.u1, e.v1,
                       px, py, (float)e.width, (float)e.height,
                       color.x, color.y, color.z, color.w);
}

// Draw text vertically centered in a rect
static void draw_text_vc(const char* text, float px, float py, float ph, const Vec4& color,
                         font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.layer < 0) return;
    float ty = py + (ph - e.height) / 2.0f;
    draw_texture_atlas(e.layer, e.u0, e.v0, e.u1, e.v1,
                       px, ty, (float)e.width, (float)e.height,
                       color.x, color.y, color.z, color.w);
}

// Draw text centered horizontally and vertically in a rect
static void draw_text_center(const char* text, float px, float py, float pw, float ph,
                              const Vec4& color, font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.layer < 0) return;
    float tx = px + (pw - e.width) / 2.0f;
    float ty = py + (ph - e.height) / 2.0f;
    draw_texture_atlas(e.layer, e.u0, e.v0, e.u1, e.v1,
                       tx, ty, (float)e.width, (float)e.height,
                       color.x, color.y, color.z, color.w);
}

// Measure text width (approximate using cached texture width)
static float measure_text_width(const char* text, font::IFontFace* face = nullptr) {
    if (!text || !text[0]) return 0;
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    return (float)e.width;
}

// Measure width of first n chars
static float measure_text_width_n(const char* text, int n, font::IFontFace* face = nullptr) {
    if (!text || n <= 0) return 0;
    std::string sub(text, std::min(n, (int)strlen(text)));
    return measure_text_width(sub.c_str(), face);
}

// Compute text advance using per-glyph advance widths (matches how renderer positions glyphs)
static float measure_text_advance_n(const char* text, int n, font::IFontFace* face = nullptr) {
    if (!text || n <= 0 || !face) return 0.0f;
    int len = (int)strlen(text);
    int count = std::min(n, len);
    float advance = 0.0f;
    uint32_t prev_gi = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t cp = (uint8_t)text[i];
        uint32_t gi = face->get_glyph_index(cp);
        if (prev_gi != 0)
            advance += face->get_kerning(prev_gi, gi);
        font::GlyphMetrics gm;
        if (face->get_glyph_metrics(gi, &gm))
            advance += gm.advance_x;
        prev_gi = gi;
    }
    return advance;
}

static void cleanup_text_cache() {
    g_text_cache.clear(); // atlas textures are owned by g_font_atlas, destroyed in main cleanup
}

// ============================================================================
// Draw flattened render info — only Color + Texture commands after flatten().
// ============================================================================

static void draw_render_info(const gui::WidgetRenderInfo& ri) {
    using namespace math;
    using Pool = gui::WidgetRenderInfo::DrawRef::Pool;
    bool scissor_on = false;

    for (const auto& ref : ri.get_draw_order()) {
        if (ref.clip_changed) {
            if (!box_is_empty(ref.clip)) {
                float bx = x(box_min(ref.clip)), by = y(box_min(ref.clip));
                float bw = box_width(ref.clip),  bh = box_height(ref.clip);
                g_renderer->set_scissor_state(true, (int)bx, g_window_h - (int)(by + bh), (int)bw, (int)bh);
                scissor_on = true;
            } else {
                g_renderer->set_scissor_state(false);
                scissor_on = false;
            }
        }

        switch (ref.pool) {
            case Pool::Color: {
                const auto& cmd = ri.colors[ref.index];
                float px = x(box_min(cmd.dest)), py = y(box_min(cmd.dest));
                float pw = box_width(cmd.dest),  ph = box_height(cmd.dest);
                if (cmd.shape == gui::DrawShape::Circle)
                    g_renderer->draw_circle(px + pw*0.5f, py + ph*0.5f, pw*0.5f,
                                            cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                else
                    g_renderer->draw_rect(px, py, pw, ph,
                                          cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                break;
            }
            case Pool::Texture: {
                const auto& cmd = ri.textures[ref.index];
                if (cmd.atlas_layer < 0) break;
                float px = x(box_min(cmd.dest)), py = y(box_min(cmd.dest));
                float pw = box_width(cmd.dest),  ph = box_height(cmd.dest);
                float u0 = x(box_min(cmd.uv)),   v0 = y(box_min(cmd.uv));
                float u1 = x(box_max(cmd.uv)),   v1 = y(box_max(cmd.uv));
                g_renderer->draw_texture(cmd.atlas_layer, u0, v0, u1, v1,
                                         px, py, pw, ph,
                                         cmd.tint.x, cmd.tint.y, cmd.tint.z, cmd.tint.w);
                break;
            }
            default: break; // Slice9 and Text already flattened away
        }
    }

    if (scissor_on)
        g_renderer->set_scissor_state(false);
}

// ============================================================================
// Widget Setup - Complex Nested Widget Tree
// ============================================================================
//
// Layout:
//   root
//   +-- menubar
//   +-- toolbar
//   +-- main_split (Horizontal: sidebar | center_area)
//   |   +-- sidebar_split (Vertical: tree | propgrid)
//   |   |   +-- tree
//   |   |   +-- propgrid
//   |   +-- center_split (Vertical: tabs | bottom_split)
//   |       +-- tabs (3 tabs, each with real content)
//   |       |   +-- tab "Editor": editbox
//   |       |   +-- tab "Controls": buttons, sliders, progress in a panel
//   |       |   +-- tab "Visuals": color picker + image
//   |       +-- bottom_split (Horizontal: listbox+combo | output_editbox)
//   |           +-- list_panel (listbox + combobox)
//   |           +-- output_editbox
//   +-- statusbar
//   +-- dialog (overlay)
//   +-- context menus (overlay)
//

struct GuiWidgets {
    // Chrome
    IGuiMenuBar* menubar;
    IGuiToolbar* toolbar;
    IGuiStatusBar* statusbar;

    // Main layout splits
    IGuiSplitPanel* main_split;       // sidebar | center
    IGuiSplitPanel* sidebar_split;    // tree / propgrid
    IGuiSplitPanel* center_split;     // tabs / bottom
    IGuiSplitPanel* bottom_split;     // list_panel | output

    // Sidebar
    IGuiTreeView* tree;
    IGuiPropertyGrid* propgrid;

    // Tab control with content
    IGuiTabControl* tabs;

    // Tab "Editor" content
    IGuiEditBox* editbox;

    // Tab "Controls" content (panel with children)
    IGuiButton* btn_normal;
    IGuiButton* btn_toggle;
    IGuiButton* btn_check;
    IGuiButton* radio1;
    IGuiButton* radio2;
    IGuiLabel* label;
    IGuiTextInput* text_input;
    IGuiSlider* slider_h;
    IGuiSlider* slider_v;
    IGuiProgressBar* prog_det;
    IGuiProgressBar* prog_ind;

    // Sizers for Tab "Controls" layout
    IBoxSizer* controls_sizer  = nullptr;  // root VBox
    IBoxSizer* ctrl_row_btns   = nullptr;  // row 0: Normal + Toggle buttons
    IBoxSizer* ctrl_row_checks = nullptr;  // row 1: Checkbox + Radio buttons
    IBoxSizer* ctrl_row_text   = nullptr;  // row 2: text input (proportional)
    IBoxSizer* ctrl_row_slides = nullptr;  // row 3: H-slider + V-slider

    // Tab "Visuals" content
    IGuiColorPicker* picker;
    IGuiImage* image;

    // Bottom panel
    IGuiListBox* listbox;
    IGuiComboBox* combo;
    IGuiEditBox* output_editbox;
    IGuiScrollBar* scrollbar;

    // Overlays
    IGuiDialog* dialog;
    IGuiMenu* editbox_context_menu;
    IGuiMenu* tree_context_menu;
};

// Active tab index tracked for content layout
static int g_active_tab = 0;

static GuiWidgets setup_widgets(IGuiContext* ctx) {
    GuiWidgets w = {};
    IGuiWidget* root = ctx->get_root();

    // ---- Menu Bar (top) ----
    w.menubar = ctx->create_menu_bar();
    w.menubar->set_name("menubar");
    w.menubar->set_bounds(make_box(0, 0, 1280, 26));

    IGuiMenu* file_menu = ctx->create_menu();
    file_menu->add_item("New", nullptr, "Ctrl+N");
    file_menu->add_item("Open", nullptr, "Ctrl+O");
    file_menu->add_item("Save", nullptr, "Ctrl+S");
    file_menu->add_separator();
    file_menu->add_item("Exit", nullptr, "Alt+F4");

    IGuiMenu* edit_menu = ctx->create_menu();
    edit_menu->add_item("Undo", nullptr, "Ctrl+Z");
    edit_menu->add_item("Redo", nullptr, "Ctrl+Y");
    edit_menu->add_separator();
    edit_menu->add_item("Cut", nullptr, "Ctrl+X");
    edit_menu->add_item("Copy", nullptr, "Ctrl+C");
    edit_menu->add_item("Paste", nullptr, "Ctrl+V");

    IGuiMenu* view_menu = ctx->create_menu();
    view_menu->add_checkbox_item("Sidebar", true);
    view_menu->add_checkbox_item("Output Panel", true);
    view_menu->add_checkbox_item("Status Bar", true);
    view_menu->add_separator();
    view_menu->add_radio_item("Theme: Dark", 1, true);
    view_menu->add_radio_item("Theme: Light", 1, false);

    IGuiMenu* help_menu = ctx->create_menu();
    help_menu->add_item("Documentation", nullptr, "F1");
    help_menu->add_item("About");

    w.menubar->add_menu("File", file_menu);
    w.menubar->add_menu("Edit", edit_menu);
    w.menubar->add_menu("View", view_menu);
    w.menubar->add_menu("Help", help_menu);
    root->add_child(w.menubar);

    // ---- Toolbar ----
    w.toolbar = ctx->create_toolbar(ToolbarOrientation::Horizontal);
    w.toolbar->set_name("toolbar");
    w.toolbar->set_bounds(make_box(0, 26, 1280, 32));
    w.toolbar->add_button("new", "New");
    w.toolbar->add_button("open", "Open");
    w.toolbar->add_button("save", "Save");
    w.toolbar->add_separator();
    w.toolbar->add_button("undo", "Undo");
    w.toolbar->add_button("redo", "Redo");
    w.toolbar->add_separator();
    w.toolbar->add_toggle_button("bold", "B", false);
    w.toolbar->add_toggle_button("italic", "I", false);
    w.toolbar->add_separator();
    w.toolbar->add_button("build", "Build");
    w.toolbar->add_button("run", "Run");
    root->add_child(w.toolbar);

    // ---- Status Bar (bottom) ----
    w.statusbar = ctx->create_status_bar();
    w.statusbar->set_name("statusbar");
    w.statusbar->set_bounds(make_box(0, 696, 1280, 24));
    w.statusbar->add_panel("Ready", StatusBarPanelSizeMode::Fill);
    w.statusbar->add_panel("Ln 1, Col 1", StatusBarPanelSizeMode::Auto);
    w.statusbar->add_panel("UTF-8", StatusBarPanelSizeMode::Auto);
    w.statusbar->add_panel("Spaces: 4", StatusBarPanelSizeMode::Auto);
    w.statusbar->add_panel("C++", StatusBarPanelSizeMode::Auto);
    root->add_child(w.statusbar);

    // ================================================================
    // Main content area: main_split (sidebar | center)
    // ================================================================
    w.main_split = ctx->create_split_panel(SplitOrientation::Horizontal);
    w.main_split->set_name("main_split");
    w.main_split->set_split_ratio(0.22f);
    w.main_split->set_first_min_size(150.0f);
    w.main_split->set_second_min_size(400.0f);
    root->add_child(w.main_split);

    // ---- Sidebar: vertical split (tree / propgrid) ----
    w.sidebar_split = ctx->create_split_panel(SplitOrientation::Vertical);
    w.sidebar_split->set_name("sidebar_split");
    w.sidebar_split->set_split_ratio(0.55f);
    w.main_split->set_first_panel(w.sidebar_split);

    // Tree view with deep hierarchy
    w.tree = ctx->create_tree_view();
    w.tree->set_name("tree");
    {
        int proj = w.tree->add_node(-1, "MyProject");
        int src = w.tree->add_node(proj, "src");
        int core = w.tree->add_node(src, "core");
        w.tree->add_node(core, "engine.cpp");
        w.tree->add_node(core, "engine.hpp");
        w.tree->add_node(core, "renderer.cpp");
        int ui = w.tree->add_node(src, "ui");
        w.tree->add_node(ui, "widget.cpp");
        w.tree->add_node(ui, "layout.cpp");
        w.tree->add_node(ui, "style.cpp");
        int util = w.tree->add_node(src, "util");
        w.tree->add_node(util, "math.hpp");
        w.tree->add_node(util, "string.hpp");
        int inc = w.tree->add_node(proj, "include");
        w.tree->add_node(inc, "app.hpp");
        w.tree->add_node(inc, "config.hpp");
        int res = w.tree->add_node(proj, "resources");
        int tex = w.tree->add_node(res, "textures");
        w.tree->add_node(tex, "logo.png");
        w.tree->add_node(tex, "icons.png");
        int shd = w.tree->add_node(res, "shaders");
        w.tree->add_node(shd, "basic.vert");
        w.tree->add_node(shd, "basic.frag");
        w.tree->add_node(proj, "CMakeLists.txt");
        w.tree->add_node(proj, "README.md");
        w.tree->set_node_expanded(proj, true);
        w.tree->set_node_expanded(src, true);
        w.tree->set_node_expanded(core, true);
        w.tree->set_node_expanded(res, true);
    }
    w.sidebar_split->set_first_panel(w.tree);

    // Property grid
    w.propgrid = ctx->create_property_grid();
    w.propgrid->set_name("propgrid");
    w.propgrid->set_name_column_width(120.0f);
    {
        int p1 = w.propgrid->add_property("Transform", "Position X", PropertyType::Float);
        w.propgrid->set_float_value(p1, 128.0f);
        int p2 = w.propgrid->add_property("Transform", "Position Y", PropertyType::Float);
        w.propgrid->set_float_value(p2, 256.0f);
        int p3 = w.propgrid->add_property("Transform", "Scale", PropertyType::Float);
        w.propgrid->set_float_value(p3, 1.0f);
        int p4 = w.propgrid->add_property("Transform", "Rotation", PropertyType::Range);
        w.propgrid->set_range_limits(p4, 0, 360);
        w.propgrid->set_float_value(p4, 45.0f);
        int p5 = w.propgrid->add_property("Appearance", "Visible", PropertyType::Bool);
        w.propgrid->set_bool_value(p5, true);
        int p6 = w.propgrid->add_property("Appearance", "Opacity", PropertyType::Range);
        w.propgrid->set_range_limits(p6, 0, 100);
        w.propgrid->set_float_value(p6, 100.0f);
        int p7 = w.propgrid->add_property("Appearance", "Color", PropertyType::Color);
        w.propgrid->set_vec4_value(p7, Vec4(1.0f, 0.5f, 0.0f, 1.0f));
        int p8 = w.propgrid->add_property("Appearance", "Name", PropertyType::String);
        w.propgrid->set_string_value(p8, "Sprite01");
        int p9 = w.propgrid->add_property("Physics", "Mass", PropertyType::Float);
        w.propgrid->set_float_value(p9, 10.0f);
        int p10 = w.propgrid->add_property("Physics", "Friction", PropertyType::Range);
        w.propgrid->set_range_limits(p10, 0, 1);
        w.propgrid->set_float_value(p10, 0.3f);
    }
    w.sidebar_split->set_second_panel(w.propgrid);

    // ================================================================
    // Center area: vertical split (tabs / bottom_panel)
    // ================================================================
    w.center_split = ctx->create_split_panel(SplitOrientation::Vertical);
    w.center_split->set_name("center_split");
    w.center_split->set_split_ratio(0.65f);
    w.center_split->set_second_min_size(100.0f);
    w.main_split->set_second_panel(w.center_split);

    // ---- Tab control (top of center) ----
    w.tabs = ctx->create_tab_control(TabPosition::Top);
    w.tabs->set_name("tabs");
    w.tabs->set_fixed_tab_width(90.0f);
    w.tabs->add_tab("Editor");
    w.tabs->add_tab("Controls");
    w.tabs->add_tab("Visuals");
    w.center_split->set_first_panel(w.tabs);

    // Tab "Editor" content: editbox (fills tab content area)
    w.editbox = ctx->create_editbox();
    w.editbox->set_name("editbox");
    w.editbox->set_text(
        "#include <iostream>\n"
        "#include \"engine.hpp\"\n"
        "\n"
        "int main() {\n"
        "    Engine engine;\n"
        "    engine.init(1280, 720);\n"
        "\n"
        "    while (engine.running()) {\n"
        "        engine.poll_events();\n"
        "        engine.update();\n"
        "        engine.render();\n"
        "    }\n"
        "\n"
        "    engine.shutdown();\n"
        "    return 0;\n"
        "}"
    );
    w.editbox->set_line_numbers_visible(true);

    // Tab "Controls" content: buttons, sliders, progress bars
    w.btn_normal = ctx->create_button(ButtonType::Normal);
    w.btn_normal->set_name("btn_normal");
    w.btn_normal->set_text("Normal Button");

    w.btn_toggle = ctx->create_button(ButtonType::Toggle);
    w.btn_toggle->set_name("btn_toggle");
    w.btn_toggle->set_text("Toggle");

    w.btn_check = ctx->create_button(ButtonType::Checkbox);
    w.btn_check->set_name("btn_check");
    w.btn_check->set_text("Enable Feature");
    w.btn_check->set_checked(true);

    w.radio1 = ctx->create_button(ButtonType::Radio);
    w.radio1->set_name("radio1");
    w.radio1->set_text("Mode A");
    w.radio1->set_radio_group(1);
    w.radio1->set_checked(true);

    w.radio2 = ctx->create_button(ButtonType::Radio);
    w.radio2->set_name("radio2");
    w.radio2->set_text("Mode B");
    w.radio2->set_radio_group(1);
    // Wire mutual peers: standalone overlays have no shared parent, so explicit peer list is needed
    w.radio1->add_radio_peer(w.radio2);
    w.radio2->add_radio_peer(w.radio1);

    w.label = ctx->create_label("Adjust parameters below:");
    w.label->set_name("label");

    w.text_input = ctx->create_text_input("Search...");
    w.text_input->set_name("text_input");

    w.slider_h = ctx->create_slider(SliderOrientation::Horizontal);
    w.slider_h->set_name("slider_h");
    w.slider_h->set_range(0, 100);
    w.slider_h->set_value(65);

    w.slider_v = ctx->create_slider(SliderOrientation::Vertical);
    w.slider_v->set_name("slider_v");
    w.slider_v->set_range(0, 1);
    w.slider_v->set_value(0.7f);

    w.prog_det = ctx->create_progress_bar(ProgressBarMode::Determinate);
    w.prog_det->set_name("prog_det");
    w.prog_det->set_value(0.72f);

    w.prog_ind = ctx->create_progress_bar(ProgressBarMode::Indeterminate);
    w.prog_ind->set_name("prog_ind");

    // --- Build sizer tree for "Controls" tab ---

    // Row 0: Normal + Toggle buttons, then stretch
    w.ctrl_row_btns = create_box_sizer(LayoutDirection::Horizontal);
    w.ctrl_row_btns->set_gap(6.0f);
    w.ctrl_row_btns->add(w.btn_normal);
    w.ctrl_row_btns->find_item(w.btn_normal)->fixed_size = Vec2(130, 28);
    w.ctrl_row_btns->add(w.btn_toggle);
    w.ctrl_row_btns->find_item(w.btn_toggle)->fixed_size = Vec2(90, 28);
    w.ctrl_row_btns->add_stretch(1);

    // Row 1: Checkbox + two radio buttons, then stretch
    w.ctrl_row_checks = create_box_sizer(LayoutDirection::Horizontal);
    w.ctrl_row_checks->set_gap(4.0f);
    w.ctrl_row_checks->add(w.btn_check);
    w.ctrl_row_checks->find_item(w.btn_check)->fixed_size = Vec2(140, 24);
    w.ctrl_row_checks->add(w.radio1);
    w.ctrl_row_checks->find_item(w.radio1)->fixed_size = Vec2(100, 24);
    w.ctrl_row_checks->add(w.radio2);
    w.ctrl_row_checks->find_item(w.radio2)->fixed_size = Vec2(100, 24);
    w.ctrl_row_checks->add_stretch(1);

    // Row 2: text input takes 60%, remaining 40% is blank stretch
    w.ctrl_row_text = create_box_sizer(LayoutDirection::Horizontal);
    w.ctrl_row_text->add(w.text_input, 3, SizerFlag::Expand);
    w.ctrl_row_text->find_item(w.text_input)->fixed_size = Vec2(60, 24);
    w.ctrl_row_text->add_stretch(2);

    // Row 3: H-slider expands proportionally, V-slider is fixed 16x80
    w.ctrl_row_slides = create_box_sizer(LayoutDirection::Horizontal);
    w.ctrl_row_slides->set_gap(8.0f);
    w.ctrl_row_slides->add(w.slider_h, 1, SizerFlag::Center);
    w.ctrl_row_slides->find_item(w.slider_h)->fixed_size = Vec2(60, 20);
    w.ctrl_row_slides->add(w.slider_v);
    w.ctrl_row_slides->find_item(w.slider_v)->fixed_size = Vec2(16, 80);

    // Root VBox: stacks all rows with uniform padding and gap
    w.controls_sizer = create_box_sizer(LayoutDirection::Vertical);
    w.controls_sizer->set_padding(8.0f);
    w.controls_sizer->set_gap(8.0f);
    w.controls_sizer->add(w.ctrl_row_btns,   0, SizerFlag::Expand);
    w.controls_sizer->add(w.ctrl_row_checks, 0, SizerFlag::Expand);
    w.controls_sizer->add(w.label,           0, SizerFlag::Expand);
    w.controls_sizer->find_item(w.label)->fixed_size = Vec2(0, 20);
    w.controls_sizer->add(w.ctrl_row_text,   0, SizerFlag::Expand);
    w.controls_sizer->add(w.ctrl_row_slides, 0, SizerFlag::Expand);
    w.controls_sizer->add(w.prog_det,        0, SizerFlag::Expand);
    w.controls_sizer->find_item(w.prog_det)->fixed_size = Vec2(0, 16);
    w.controls_sizer->add(w.prog_ind,        0, SizerFlag::Expand);
    w.controls_sizer->find_item(w.prog_ind)->fixed_size = Vec2(0, 10);

    // Tab "Visuals" content: color picker + image
    w.picker = ctx->create_color_picker(ColorPickerMode::HSVSquare);
    w.picker->set_name("picker");
    w.picker->set_color(Vec4(0.2f, 0.6f, 1.0f, 1.0f));
    w.picker->set_alpha_enabled(true);

    w.image = ctx->create_image("textures/logo.png");
    w.image->set_name("image");
    w.image->set_tint(Vec4(0.4f, 0.7f, 1.0f, 0.9f));
    // Enable 9-slice so the image widget renders as a framed panel.
    // The 12 px borders create rounded-looking corners when draw_slice9_solid
    // emits the 8 border cells; the centre is left transparent (Hidden).
    w.image->set_use_slice9(true);
    w.image->set_slice_border(gui::SliceBorder::uniform(12.0f));
    w.image->set_slice_center_mode(gui::SliceCenterMode::Hidden);

    // ---- Bottom panel: horizontal split (list | output) ----
    w.bottom_split = ctx->create_split_panel(SplitOrientation::Horizontal);
    w.bottom_split->set_name("bottom_split");
    w.bottom_split->set_split_ratio(0.35f);
    w.bottom_split->set_first_min_size(120.0f);
    w.center_split->set_second_panel(w.bottom_split);

    // Left of bottom: listbox + combo stacked
    w.listbox = ctx->create_list_box();
    w.listbox->set_name("listbox");
    w.listbox->add_item("Build Started");
    w.listbox->add_item("Compiling main.cpp");
    w.listbox->add_item("Compiling engine.cpp");
    w.listbox->add_item("Compiling renderer.cpp");
    w.listbox->add_item("Compiling widget.cpp");
    w.listbox->add_item("Linking...");
    w.listbox->add_item("Build Succeeded");
    w.listbox->set_selected_item(6);
    // Note: combo is positioned inside the list panel area
    w.combo = ctx->create_combo_box();
    w.combo->set_name("combo");
    w.combo->set_placeholder("Filter...");
    w.combo->add_item("All");
    w.combo->add_item("Errors");
    w.combo->add_item("Warnings");
    w.combo->add_item("Info");
    w.combo->set_selected_item(0);
    // We'll use listbox as the split panel content, combo overlaid
    w.bottom_split->set_first_panel(w.listbox);

    // Right of bottom: output editbox
    w.output_editbox = ctx->create_editbox();
    w.output_editbox->set_name("output_editbox");
    w.output_editbox->set_text(
        "[14:32:01] Build started...\n"
        "[14:32:01] Compiling main.cpp\n"
        "[14:32:02] Compiling engine.cpp\n"
        "[14:32:02] Compiling renderer.cpp\n"
        "[14:32:03] Compiling widget.cpp\n"
        "[14:32:03] Linking output.exe\n"
        "[14:32:04] Build succeeded (0 errors, 0 warnings)\n"
    );
    w.output_editbox->set_line_numbers_visible(false);
    w.bottom_split->set_second_panel(w.output_editbox);

    // Scrollbar (standalone, next to tabs)
    w.scrollbar = ctx->create_scroll_bar(ScrollBarOrientation::Vertical);
    w.scrollbar->set_name("scrollbar");
    w.scrollbar->set_range(0, 0);
    w.scrollbar->set_value(0);

    // ---- Context Menus ----
    w.editbox_context_menu = ctx->create_menu();
    w.editbox_context_menu->set_name("editbox_context_menu");
    w.editbox_context_menu->set_bounds(make_box(0, 0, 180, 200));
    w.editbox_context_menu->add_item("Cut", nullptr, "Ctrl+X");
    w.editbox_context_menu->add_item("Copy", nullptr, "Ctrl+C");
    w.editbox_context_menu->add_item("Paste", nullptr, "Ctrl+V");
    w.editbox_context_menu->add_separator();
    w.editbox_context_menu->add_item("Select All", nullptr, "Ctrl+A");
    w.editbox_context_menu->add_separator();
    w.editbox_context_menu->add_checkbox_item("Word Wrap", false);
    w.editbox_context_menu->add_checkbox_item("Line Numbers", true);

    w.tree_context_menu = ctx->create_menu();
    w.tree_context_menu->set_name("tree_context_menu");
    w.tree_context_menu->set_bounds(make_box(0, 0, 180, 200));
    w.tree_context_menu->add_item("Expand All");
    w.tree_context_menu->add_item("Collapse All");
    w.tree_context_menu->add_separator();
    w.tree_context_menu->add_item("New File", nullptr, "Ctrl+N");
    w.tree_context_menu->add_item("New Folder");
    w.tree_context_menu->add_separator();
    w.tree_context_menu->add_item("Rename", nullptr, "F2");
    w.tree_context_menu->add_item("Delete", nullptr, "Del");


    // ---- Dialog ----
    w.dialog = ctx->create_dialog("Save Changes?", DialogButtons::YesNoCancel);
    w.dialog->set_name("dialog");
    w.dialog->set_modal(true);
    w.dialog->set_draggable(true);
    w.dialog->set_bounds(make_box(400, 240, 320, 160));
    w.dialog->show();

    return w;
}

// ============================================================================
// Layout update - compute bounds for the nested widget tree
// ============================================================================

static void layout_widgets(GuiWidgets& w, int sw, int sh) {
    float top = 58.0f;   // below menubar + toolbar
    float bot = (float)sh - 24.0f; // above statusbar
    float content_h = bot - top;
    float content_w = (float)sw;

    // Chrome
    w.menubar->set_bounds(make_box(0, 0, (float)sw, 26));
    w.toolbar->set_bounds(make_box(0, 26, (float)sw, 32));
    w.statusbar->set_bounds(make_box(0, bot, (float)sw, 24));

    // Main split fills content area
    w.main_split->set_bounds(make_box(0, top, content_w, content_h));

    // Compute split positions
    float sidebar_w = content_w * w.main_split->get_split_ratio();
    float center_w = content_w - sidebar_w - 4; // 4 = splitter
    float center_x = sidebar_w + 4;

    // Sidebar split — set_bounds triggers recalculate_layout which sizes tree/propgrid
    w.sidebar_split->set_bounds(make_box(0, top, sidebar_w, content_h));

    // Center split
    w.center_split->set_bounds(make_box(center_x, top, center_w, content_h));
    float tabs_h = content_h * w.center_split->get_split_ratio();
    float bottom_h = content_h - tabs_h - 4;
    float bottom_y = top + tabs_h + 4;

    // Tabs
    w.tabs->set_bounds(make_box(center_x, top, center_w - 14, tabs_h));
    w.scrollbar->set_bounds(make_box(center_x + center_w - 14, top, 14, tabs_h));

    // Tab content area (inside tabs, below tab bar)
    float tab_bar_h = 30.0f;
    float tc_x = center_x + 4;
    float tc_y = top + tab_bar_h + 2;
    float tc_w = center_w - 22; // minus scrollbar and padding
    float tc_h = tabs_h - tab_bar_h - 6;

    // Layout active tab content; hide inactive tab widgets with zero bounds
    static const Box HIDDEN = make_box(0, 0, 0, 0);
    g_active_tab = w.tabs->get_active_tab();

    // Tab 0: Editor
    if (g_active_tab == 0)
        w.editbox->set_bounds(make_box(tc_x, tc_y, tc_w, tc_h));
    else
        w.editbox->set_bounds(HIDDEN);

    // Tab 1: Controls
    if (g_active_tab == 1) {
        w.controls_sizer->set_bounds(make_box(tc_x, tc_y, tc_w, tc_h));
        w.controls_sizer->layout();
    } else {
        w.btn_normal->set_bounds(HIDDEN);
        w.btn_toggle->set_bounds(HIDDEN);
        w.btn_check->set_bounds(HIDDEN);
        w.radio1->set_bounds(HIDDEN);
        w.radio2->set_bounds(HIDDEN);
        w.label->set_bounds(HIDDEN);
        w.text_input->set_bounds(HIDDEN);
        w.slider_h->set_bounds(HIDDEN);
        w.slider_v->set_bounds(HIDDEN);
        w.prog_det->set_bounds(HIDDEN);
        w.prog_ind->set_bounds(HIDDEN);
    }

    // Tab 2: Visuals
    if (g_active_tab == 2) {
        float picker_w = std::min(tc_w * 0.55f, 240.0f);
        float picker_h = std::min(tc_h, 300.0f);
        w.picker->set_bounds(make_box(tc_x + 4, tc_y + 4, picker_w, picker_h));
        float img_x = tc_x + picker_w + 12;
        float img_size = std::min(tc_w - picker_w - 20, tc_h - 8);
        if (img_size < 40) img_size = 40;
        w.image->set_bounds(make_box(img_x, tc_y + 4, img_size, img_size));
    } else {
        w.picker->set_bounds(HIDDEN);
        w.image->set_bounds(HIDDEN);
    }

    // Bottom split
    w.bottom_split->set_bounds(make_box(center_x, bottom_y, center_w, bottom_h));
    float list_w = center_w * w.bottom_split->get_split_ratio();
    float output_w = center_w - list_w - 4;
    float output_x = center_x + list_w + 4;

    w.listbox->set_bounds(make_box(center_x, bottom_y + 28, list_w, bottom_h - 28));
    w.combo->set_bounds(make_box(center_x, bottom_y, list_w, 26));
    w.output_editbox->set_bounds(make_box(output_x, bottom_y, output_w, bottom_h));
}

// ============================================================================
// Event handlers: wire right-click to context menus via widget callbacks
// ============================================================================

// ITextMeasurer impl: uses actual font rendering to give editbox precise click-to-column mapping
struct FontTextMeasurer : ITextMeasurer {
    font::IFontFace* face = nullptr;
    math::Vec2 measure_text(const char* text, float /*font_size*/, const char* /*font_name*/) override {
        if (!text || !text[0] || !face) return {0, 0};
        TextEntry e = get_text_entry(text, face);
        return {(float)e.width, (float)e.height};
    }
    float get_line_height(float font_size, const char* /*font_name*/) override {
        return font_size * 1.2f;
    }
};

// IGuiTextRasterizer impl: wraps the existing text cache + font renderer
// so that the GUI system can flatten TextCmd into textured quads internally.
struct GuiTextRasterizer : IGuiTextRasterizer {
    font::IFontFace* face_ui    = nullptr;
    font::IFontFace* face_small = nullptr;

    TextQuad rasterize(const char* text, float font_size, const char* /*font_name*/) override {
        font::IFontFace* face = (font_size <= 10.0f) ? face_small : face_ui;
        TextEntry e = get_text_entry(text, face);
        TextQuad q;
        q.atlas_layer = e.layer;
        q.u0 = e.u0; q.v0 = e.v0;
        q.u1 = e.u1; q.v1 = e.v1;
        q.width = e.width; q.height = e.height;
        return q;
    }

    float measure_advance(const char* text, int n, float font_size,
                          const char* /*font_name*/) override {
        font::IFontFace* face = (font_size <= 10.0f) ? face_small : face_ui;
        return measure_text_width_n(text, n, face);
    }

    float get_time() const override { return g_time; }
};

struct EditboxContextMenuHandler : IEditBoxEventHandler {
    IGuiMenu* menu = nullptr;
    void on_text_changed() override {}
    void on_cursor_moved(const TextPosition&) override {}
    void on_selection_changed(const TextRange&) override {}
    void on_right_click(const math::Vec2& pos) override { if (menu) menu->show_at(pos); }
};

struct TreeContextMenuHandler : ITreeViewEventHandler {
    IGuiMenu* menu = nullptr;
    void on_node_selected(int) override {}
    void on_node_expanded(int, bool) override {}
    void on_node_double_clicked(int) override {}
    void on_right_click(const math::Vec2& pos) override { if (menu) menu->show_at(pos); }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Complex Widget Tree Showcase ===\n");

    // Create OpenGL window
    Config config;
    config.windows[0].title = "Complex Widget Tree";
    config.windows[0].width = 1280;
    config.windows[0].height = 720;
    config.backend = Backend::OpenGL;

    Result result;
    auto windows = Window::create(config, &result);
    if (result != Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", result_to_string(result));
        return 1;
    }

    Window* win = windows[0];
    Graphics* gfx = win->graphics();
    printf("Window created: %s (%s)\n", gfx->get_backend_name(), gfx->get_device_name());


    // Initialize modern OpenGL renderer
    QuadRenderer renderer;
    if (!renderer.init()) {
        printf("Failed to init OpenGL renderer\n");
        win->destroy();
        return 1;
    }
    // Create font atlas using font system; wire up GL texture-array callbacks
    {
        GLint max_size, max_layers;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE,         &max_size);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
        int tile_w  = std::min(max_size, 4096);
        int tile_h  = std::min(max_size, 4096);
        int max_dep = std::min(max_layers, 2048);
        printf("FontAtlas: tile %d×%d, GL_MAX_TEXTURE_SIZE=%d, max_layers=%d\n",
               tile_w, tile_h, max_size, max_layers);

        g_font_atlas = font::create_font_atlas();
        g_font_atlas->set_tile_size(tile_w, tile_h);
        g_font_atlas->set_max_layers(max_dep);
        g_font_atlas->set_callbacks(
            // InitCallback: create GL_TEXTURE_2D_ARRAY
            [](int tw, int th, int depth) -> uintptr_t {
                GLuint tex;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tw, th, depth,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
                return (uintptr_t)tex;
            },
            // GrowCallback: allocate new texture, copy old layers GPU-side (GL 4.3)
            [](uintptr_t old_handle, int tw, int th, int old_depth, int new_depth) -> uintptr_t {
                GLuint old_tex = (GLuint)old_handle;
                GLuint new_tex;
                glGenTextures(1, &new_tex);
                glBindTexture(GL_TEXTURE_2D_ARRAY, new_tex);
                glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tw, th, new_depth,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
                if (old_tex && old_depth > 0)
                    glCopyImageSubData(old_tex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,
                                       new_tex, GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0,
                                       tw, th, old_depth);
                glDeleteTextures(1, &old_tex);
                return (uintptr_t)new_tex;
            },
            // UploadCallback: glTexSubImage3D
            [](uintptr_t handle, const void* rgba8, int x, int y, int layer, int w, int h) {
                GLuint tex = (GLuint)handle;
                glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                                x, y, layer, w, h, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
                glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            },
            // DestroyCallback: delete texture
            [](uintptr_t handle) {
                GLuint tex = (GLuint)handle;
                if (tex) glDeleteTextures(1, &tex);
            }
        );
    }
    g_renderer = &renderer;

    // Initialize font system
    font::Result font_result;
    font::IFontLibrary* font_library = font::create_font_library(font::FontBackend::Auto, &font_result);
    if (!font_library) {
        printf("Failed to create font library: %s\n", font::result_to_string(font_result));
        renderer.destroy();
        win->destroy();
        return 1;
    }
    printf("Font backend: %s\n", font::font_backend_to_string(font_library->get_backend()));

    g_font_renderer = font::create_font_renderer(font_library, &font_result);
    if (!g_font_renderer) {
        printf("Failed to create font renderer\n");
        font::destroy_font_library(font_library);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    // Load UI fonts
    g_font_ui = font_library->load_system_font(font::FontDescriptor::create("Segoe UI", 14.0f), nullptr);
    if (!g_font_ui)
        g_font_ui = font_library->load_system_font(font::FontDescriptor::create("Arial", 14.0f), nullptr);
    if (!g_font_ui)
        g_font_ui = font_library->get_default_font(14.0f, nullptr);

    g_font_small = font_library->load_system_font(font::FontDescriptor::create("Segoe UI", 12.0f), nullptr);
    if (!g_font_small)
        g_font_small = font_library->load_system_font(font::FontDescriptor::create("Arial", 12.0f), nullptr);
    if (!g_font_small)
        g_font_small = font_library->get_default_font(12.0f, nullptr);

    if (!g_font_ui || !g_font_small) {
        printf("Failed to load fonts\n");
        font::destroy_font_renderer(g_font_renderer);
        font::destroy_font_library(font_library);
        renderer.destroy();
        win->destroy();
        return 1;
    }
    printf("Font loaded: %s (%.0fpt)\n", g_font_ui->get_family_name(), g_font_ui->get_size());

    // Create font fallback chain for multi-language support
    font::IFontFallbackChain* fallback_chain =
        font::create_fallback_chain_with_defaults(font_library, g_font_ui, 14.0f);
    if (fallback_chain) {
        g_font_renderer->set_fallback_chain(fallback_chain);
        printf("Font fallback chain: %d fonts\n", fallback_chain->get_font_count());
    }

    // Create GUI context
    GuiResult gresult;
    IGuiContext* ctx = create_gui_context(&gresult);
    if (!ctx || gresult != GuiResult::Success) {
        printf("Failed to create GUI context\n");
        font::destroy_font_renderer(g_font_renderer);
        font::destroy_font_library(font_library);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    // Wire all platform input (mouse move/button/wheel, key/char) into the widget tree
    ctx->attach_window(win);

    gui::Viewport vp;
    vp.id = 0;
    vp.bounds = make_box(0, 0, 1280, 720);
    vp.scale = 1.0f;
    ctx->add_viewport(vp);

    IGuiWidget* root = ctx->get_root();
    root->set_bounds(make_box(0, 0, 1280, 720));

    // Setup all widgets
    GuiWidgets widgets = setup_widgets(ctx);

    // Register all standalone/overlay widgets with the context.
    // ctx->get_render_info() will include them (skipping hidden ones) after the root tree.
    ctx->add_overlay(widgets.scrollbar);
    ctx->add_overlay(widgets.editbox);
    ctx->add_overlay(widgets.btn_normal);
    ctx->add_overlay(widgets.btn_toggle);
    ctx->add_overlay(widgets.btn_check);
    ctx->add_overlay(widgets.radio1);
    ctx->add_overlay(widgets.radio2);
    ctx->add_overlay(widgets.label);
    ctx->add_overlay(widgets.text_input);
    ctx->add_overlay(widgets.slider_h);
    ctx->add_overlay(widgets.slider_v);
    ctx->add_overlay(widgets.prog_det);
    ctx->add_overlay(widgets.prog_ind);
    ctx->add_overlay(widgets.picker);
    ctx->add_overlay(widgets.image);
    ctx->add_overlay(widgets.combo);
    ctx->add_overlay(widgets.editbox_context_menu);
    ctx->add_overlay(widgets.tree_context_menu);
    ctx->add_overlay(widgets.dialog);

    printf("Widgets created: %d children in root\n", root->get_child_count());
    printf("Hover and click widgets to see state changes.\n");
    printf("Close window to exit.\n\n");

    // Animation
    IGuiAnimationManager* anim_mgr = ctx->get_animation_manager();
    IGuiAnimation* anim = anim_mgr->create_animation();
    anim->set_target(widgets.prog_ind);
    anim->set_target_property(AnimationTarget::Opacity);
    anim->animate_from_to(Vec4(0,0,0,0), Vec4(1,0,0,0), 2.0f);
    anim->set_loop_mode(AnimationLoop::PingPong);
    anim->start();

    // Wire text measurer so editbox click-to-cursor uses actual glyph metrics
    FontTextMeasurer font_measurer;
    font_measurer.face = g_font_ui;
    widgets.editbox->set_text_measurer(&font_measurer);
    widgets.output_editbox->set_text_measurer(&font_measurer);

    // Wire text rasterizer so get_render_info() flattens Text/Slice9 into Color+Texture
    GuiTextRasterizer text_rasterizer;
    text_rasterizer.face_ui    = g_font_ui;
    text_rasterizer.face_small = g_font_small;
    ctx->set_text_rasterizer(&text_rasterizer);

    // Register context-menu callbacks: widgets fire on_right_click → show appropriate menu
    EditboxContextMenuHandler editbox_handler;
    editbox_handler.menu = widgets.editbox_context_menu;
    widgets.editbox->set_editbox_event_handler(&editbox_handler);

    EditboxContextMenuHandler output_editbox_handler;
    output_editbox_handler.menu = widgets.editbox_context_menu;
    widgets.output_editbox->set_editbox_event_handler(&output_editbox_handler);

    TreeContextMenuHandler tree_handler;
    tree_handler.menu = widgets.tree_context_menu;
    widgets.tree->set_tree_event_handler(&tree_handler);

    auto start_time = std::chrono::high_resolution_clock::now();
    float prev_time = 0;

    // Main loop
    while (!win->should_close()) {
        win->poll_events();

        auto now = std::chrono::high_resolution_clock::now();
        float current_time = std::chrono::duration<float>(now - start_time).count();
        float dt = current_time - prev_time;
        prev_time = current_time;
        g_time = current_time;

        // Get window size
        int sw, sh;
        win->get_size(&sw, &sh);
        g_window_h = sh;

        // Update viewport on resize
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        ctx->update_viewport(vp);

        // Update nested layout for the entire widget tree
        root->set_bounds(make_box(0, 0, (float)sw, (float)sh));
        layout_widgets(widgets, sw, sh);

        // Sync standalone scrollbar with editbox scroll position
        {
            auto* eb = widgets.editbox;
            auto* sb = widgets.scrollbar;
            int total   = eb->get_line_count();
            int visible = eb->get_visible_line_count();
            int max_line = total > visible ? total - visible : 0;
            sb->set_range(0.0f, (float)max_line);
            sb->set_page_size((float)visible);
            if (sb->is_thumb_pressed()) {
                eb->set_first_visible_line((int)sb->get_value());
            } else {
                sb->set_value((float)eb->get_first_visible_line());
            }
        }

        // Update GUI frame
        ctx->begin_frame(dt);
        ctx->end_frame();

        // Slowly animate determinate progress bar
        widgets.prog_det->set_value(fmodf(current_time * 0.05f, 1.0f));

        // ---- Render ----
        glViewport(0, 0, sw, sh);
        glClearColor(0.12f, 0.12f, 0.13f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.set_projection(sw, sh);


        // Collect all widget draw commands and replay in a single batch.
        draw_render_info(ctx->get_render_info());
        g_renderer->flush_all((GLuint)g_font_atlas->get_gpu_handle());

        gfx->present();
    }

    // Cleanup
    cleanup_text_cache();
    font::destroy_font_atlas(g_font_atlas);
    g_font_atlas = nullptr;
    renderer.destroy();
    g_renderer = nullptr;
    destroy_gui_context(ctx);
    font::destroy_font_renderer(g_font_renderer);
    g_font_renderer = nullptr;
    if (fallback_chain) {
        font::destroy_fallback_chain(fallback_chain);
        fallback_chain = nullptr;
    }
    font::destroy_font_library(font_library);
    win->destroy();

    printf("Window closed.\n");
    return 0;
}
