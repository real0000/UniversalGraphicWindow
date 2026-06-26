/*
 * css_theme.cpp - CSS-look GUI theme showcase (OpenGL)
 *
 * Demonstrates renderer/style CssTheme: a "looks like a web page" skin made of
 * style presets. The same widget set is built twice, side by side:
 *
 *   - Whole tree   : CssTheme::light().apply(root)  -> the page-white CSS look
 *   - Partial apply: CssTheme::dark().apply(rightColumn)  re-themes only that
 *                    subtree, proving apply() can target part of the UI.
 *   - Per-widget   : set_button_style(theme.button_primary_style()) gives one
 *                    button the accent (blue) call-to-action look.
 *
 * Rendering reuses the OpenGL + glyph-atlas scaffold from examples/gui.cpp;
 * the theme only sets widget styles, so the ordinary renderer draws the result.
 */

#include "window.hpp"
#include "gui/gui.hpp"
#include "gui/font/font.hpp"
#include "renderer/style/css_theme.hpp"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
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

// ============================================================================
// Shaders (pos.xy | uvw.xyz, z<0 = solid | rgba) — identical to examples/gui.cpp
// ============================================================================
static const char* g_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec3 aTexCoord;
layout (location = 2) in vec4 aColor;
out vec3 TexCoord;
out vec4 vColor;
uniform mat4 uProjection;
void main() { gl_Position = uProjection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; vColor = aColor; }
)";

static const char* g_fragment_shader = R"(
#version 330 core
in vec3 TexCoord;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2DArray uTexture;
void main() {
    if (TexCoord.z >= 0.0) { float a = texture(uTexture, TexCoord).r; FragColor = vec4(vColor.rgb, vColor.a * a); }
    else                   { FragColor = vColor; }
}
)";

// ============================================================================
// QuadRenderer — deferred indirect-draw renderer with glyph-atlas support.
// (Trimmed copy of the proven renderer in examples/gui.cpp.)
// ============================================================================
class QuadRenderer {
public:
    GLuint program = 0, vao = 0, vbo = 0, ibo = 0;
    GLint  loc_projection = -1, loc_texture = -1;
    static const int MAX_VERTS = 8192 * 6;

    struct DrawCmd { GLuint first, count; GLenum primitive; bool scissor_active; int sc_x, sc_y, sc_w, sc_h; };
    struct IndirectCmd { GLuint count, instanceCount, first, baseInstance; };

    std::vector<float>   m_verts;
    std::vector<DrawCmd> m_draw_cmds;
    GLuint m_cur_first = 0;
    GLenum m_cur_prim  = GL_TRIANGLES;
    bool m_scissor_active = false;
    int  m_sc_x = 0, m_sc_y = 0, m_sc_w = 0, m_sc_h = 0;

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); printf("shader error: %s\n", log); }
        return s;
    }

    bool init() {
        GLuint vs = compile(GL_VERTEX_SHADER, g_vertex_shader);
        GLuint fs = compile(GL_FRAGMENT_SHADER, g_fragment_shader);
        program = glCreateProgram();
        glAttachShader(program, vs); glAttachShader(program, fs);
        glLinkProgram(program);
        GLint ok; glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) { char log[512]; glGetProgramInfoLog(program, 512, nullptr, log); printf("link error: %s\n", log); return false; }
        glDeleteShader(vs); glDeleteShader(fs);

        loc_projection = glGetUniformLocation(program, "uProjection");
        loc_texture    = glGetUniformLocation(program, "uTexture");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MAX_VERTS * 9 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)0);                  glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(2 * sizeof(float))); glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 9 * sizeof(float), (void*)(5 * sizeof(float))); glEnableVertexAttribArray(2);
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
            2.0f / width, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f / height, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 0.0f,
           -1.0f, 1.0f, 0.0f, 1.0f
        };
        glUseProgram(program);
        glUniformMatrix4fv(loc_projection, 1, GL_FALSE, proj);
    }

    void set_scissor_state(bool active, int x = 0, int y = 0, int w = 0, int h = 0) {
        if (active == m_scissor_active && (!active || (x == m_sc_x && y == m_sc_y && w == m_sc_w && h == m_sc_h))) return;
        close_batch();
        m_scissor_active = active; m_sc_x = x; m_sc_y = y; m_sc_w = w; m_sc_h = h;
    }

    void draw_rect(float px, float py, float pw, float ph, float r, float g, float b, float a = 1.0f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float v[] = {
            px,      py,      0,0,-1, r,g,b,a,  px + pw, py,      0,0,-1, r,g,b,a,  px + pw, py + ph, 0,0,-1, r,g,b,a,
            px,      py,      0,0,-1, r,g,b,a,  px + pw, py + ph, 0,0,-1, r,g,b,a,  px,      py + ph, 0,0,-1, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v + 54);
    }

    void draw_texture(int layer, float u0, float v0, float u1, float v1, float px, float py, float pw, float ph,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
        if (m_cur_prim != GL_TRIANGLES) { close_batch(); m_cur_prim = GL_TRIANGLES; }
        float lf = (float)layer;
        float v[] = {
            px,      py,      u0,v0,lf, r,g,b,a,  px + pw, py,      u1,v0,lf, r,g,b,a,  px + pw, py + ph, u1,v1,lf, r,g,b,a,
            px,      py,      u0,v0,lf, r,g,b,a,  px + pw, py + ph, u1,v1,lf, r,g,b,a,  px,      py + ph, u0,v1,lf, r,g,b,a,
        };
        m_verts.insert(m_verts.end(), v, v + 54);
    }

    void draw_circle(float cx, float cy, float radius, float r, float g, float b, float a = 1.0f) {
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

    void flush_all(GLuint atlas_tex) {
        close_batch();
        if (m_draw_cmds.empty()) { m_verts.clear(); return; }

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(m_verts.size() * sizeof(float)), m_verts.data());

        std::vector<IndirectCmd> indirect; indirect.reserve(m_draw_cmds.size());
        for (const DrawCmd& dc : m_draw_cmds) indirect.push_back({dc.count, 1, dc.first, 0});
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ibo);
        glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, (GLsizeiptr)(indirect.size() * sizeof(IndirectCmd)), indirect.data());

        glUseProgram(program);
        if (atlas_tex) { glUniform1i(loc_texture, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex); }

        glBindVertexArray(vao);
        int i = 0, n = (int)m_draw_cmds.size();
        while (i < n) {
            const DrawCmd& dc = m_draw_cmds[i];
            if (dc.scissor_active) { glEnable(GL_SCISSOR_TEST); glScissor(dc.sc_x, dc.sc_y, dc.sc_w, dc.sc_h); }
            else                   { glDisable(GL_SCISSOR_TEST); }
            int j = i + 1;
            while (j < n) {
                const DrawCmd& d = m_draw_cmds[j];
                if (d.primitive != dc.primitive || d.scissor_active != dc.scissor_active ||
                    d.sc_x != dc.sc_x || d.sc_y != dc.sc_y || d.sc_w != dc.sc_w || d.sc_h != dc.sc_h) break;
                ++j;
            }
            GLintptr offset = (GLintptr)(i * sizeof(IndirectCmd));
            glMultiDrawArraysIndirect(dc.primitive, (const void*)offset, j - i, sizeof(IndirectCmd));
            i = j;
        }
        glDisable(GL_SCISSOR_TEST);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        glBindVertexArray(0);

        m_verts.clear(); m_draw_cmds.clear();
        m_cur_first = 0; m_cur_prim = GL_TRIANGLES; m_scissor_active = false;
    }

private:
    void close_batch() {
        GLuint total = (GLuint)(m_verts.size() / 9);
        GLuint count = total - m_cur_first;
        if (count == 0) return;
        m_draw_cmds.push_back({m_cur_first, count, m_cur_prim, m_scissor_active, m_sc_x, m_sc_y, m_sc_w, m_sc_h});
        m_cur_first = total;
    }
};

// ============================================================================
// Glyph-atlas text plumbing (copied from examples/gui.cpp)
// ============================================================================
static QuadRenderer*             g_renderer  = nullptr;
static font::IGlyphAtlasManager* g_glyph_mgr = nullptr;
static font::ITextShaper*        g_shaper    = nullptr;
static font::IFontFace*          g_font_ui   = nullptr;
static GLuint g_atlas_tex       = 0;
static int    g_atlas_gl_layers = 0;
static float  g_time = 0;
static int    g_window_h = 720;
static float  g_dpi_scale = 1.0f;

static void shape_to_quads(const char* text, float size, std::vector<IGuiTextRasterizer::GlyphQuad>& out,
                           float& out_w, float& out_h) {
    out.clear(); out_w = 0; out_h = 0;
    if (!text || !text[0] || !g_shaper || !g_glyph_mgr) return;
    font::IFontFace* primary = g_glyph_mgr->get_font(0);
    if (!primary) return;
    primary->set_size(size);
    std::vector<font::PositionedGlyph> glyphs;
    g_shaper->shape_text(primary, text, -1, glyphs, font::TextLayoutOptions());
    const font::FontMetrics& fm = primary->get_metrics();
    float ascent = fm.ascender, max_x = 0;
    for (const auto& pg : glyphs) {
        float end_x = pg.x + pg.advance;
        if (end_x > max_x) max_x = end_x;
        const font::GlyphSlot* s = g_glyph_mgr->acquire(pg.font_index, pg.glyph_index, size);
        if (!s || s->pw <= 0 || s->ph <= 0) continue;
        IGuiTextRasterizer::GlyphQuad q;
        q.atlas_layer = s->layer;
        q.x = pg.x + s->bearing_x; q.y = ascent - s->bearing_y;
        q.w = (float)s->pw; q.h = (float)s->ph;
        q.u0 = s->u0; q.v0 = s->v0; q.u1 = s->u1; q.v1 = s->v1;
        out.push_back(q);
    }
    out_w = max_x;
    out_h = fm.ascender - fm.descender;
}

static float measure_first_n_advance(const char* text, int n, float size) {
    if (!text || n <= 0) return 0.0f;
    int len = (int)strlen(text);
    if (n > len) n = len;
    std::string sub(text, n);
    std::vector<IGuiTextRasterizer::GlyphQuad> q; float w = 0, h = 0;
    shape_to_quads(sub.c_str(), size, q, w, h);
    return w;
}

static void sync_atlas_to_gpu() {
    if (!g_glyph_mgr) return;
    const int W = g_glyph_mgr->width(), H = g_glyph_mgr->height(), layers = g_glyph_mgr->layer_count();
    if (layers < 1) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (g_atlas_tex == 0 || layers > g_atlas_gl_layers) {
        if (g_atlas_tex) glDeleteTextures(1, &g_atlas_tex);
        glGenTextures(1, &g_atlas_tex);
        glBindTexture(GL_TEXTURE_2D_ARRAY, g_atlas_tex);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8, W, H, layers, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        for (int l = 0; l < layers; ++l) {
            const uint8_t* d = g_glyph_mgr->layer_data(l);
            if (d) glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, l, W, H, 1, GL_RED, GL_UNSIGNED_BYTE, d);
        }
        g_atlas_gl_layers = layers;
        std::vector<font::GlyphDirtyRegion> drop; g_glyph_mgr->take_dirty_regions(drop);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        return;
    }
    std::vector<font::GlyphDirtyRegion> dirty;
    g_glyph_mgr->take_dirty_regions(dirty);
    if (dirty.empty()) return;
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_atlas_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, W);
    for (const auto& r : dirty) {
        const uint8_t* base = g_glyph_mgr->layer_data(r.layer);
        if (!base) continue;
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, r.x, r.y, r.layer, r.w, r.h, 1, GL_RED, GL_UNSIGNED_BYTE, base + (size_t)r.y * W + r.x);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

static void cleanup_glyph_atlas() {
    if (g_atlas_tex) { glDeleteTextures(1, &g_atlas_tex); g_atlas_tex = 0; }
    g_atlas_gl_layers = 0;
}

static void draw_render_info(const gui::WidgetRenderInfo& ri) {
    using Pool = gui::WidgetRenderInfo::DrawRef::Pool;
    bool scissor_on = false;
    for (const auto& ref : ri.get_draw_order()) {
        if (ref.clip_changed) {
            if (!box_is_empty(ref.clip)) {
                float s = g_dpi_scale;
                float bx = x(box_min(ref.clip)), by = y(box_min(ref.clip));
                float bw = box_width(ref.clip),  bh = box_height(ref.clip);
                g_renderer->set_scissor_state(true, (int)(bx*s), (int)((g_window_h - (by + bh))*s), (int)(bw*s), (int)(bh*s));
                scissor_on = true;
            } else { g_renderer->set_scissor_state(false); scissor_on = false; }
        }
        switch (ref.pool) {
            case Pool::Color: {
                const auto& cmd = ri.colors[ref.index];
                float px = x(box_min(cmd.dest)), py = y(box_min(cmd.dest));
                float pw = box_width(cmd.dest),  ph = box_height(cmd.dest);
                if (cmd.shape == gui::DrawShape::Circle)
                    g_renderer->draw_circle(px + pw*0.5f, py + ph*0.5f, pw*0.5f, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                else
                    g_renderer->draw_rect(px, py, pw, ph, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w);
                break;
            }
            case Pool::Texture: {
                const auto& cmd = ri.textures[ref.index];
                if (cmd.atlas_layer < 0) break;
                float px = x(box_min(cmd.dest)), py = y(box_min(cmd.dest));
                float pw = box_width(cmd.dest),  ph = box_height(cmd.dest);
                float u0 = x(box_min(cmd.uv)),   v0 = y(box_min(cmd.uv));
                float u1 = x(box_max(cmd.uv)),   v1 = y(box_max(cmd.uv));
                g_renderer->draw_texture(cmd.atlas_layer, u0, v0, u1, v1, px, py, pw, ph, cmd.tint.x, cmd.tint.y, cmd.tint.z, cmd.tint.w);
                break;
            }
            default: break;
        }
    }
    if (scissor_on) g_renderer->set_scissor_state(false);
}

struct FontTextMeasurer : ITextMeasurer {
    math::Vec2 measure_text(const char* text, float font_size, const char*) override {
        if (!text || !text[0]) return {0, 0};
        std::vector<IGuiTextRasterizer::GlyphQuad> q; float w = 0, h = 0;
        shape_to_quads(text, font_size, q, w, h);
        return {w, h};
    }
    float get_line_height(float font_size, const char*) override { return font_size * 1.2f; }
};

struct GuiTextRasterizer : IGuiTextRasterizer {
    TextQuad rasterize(const char*, float, const char*) override { return TextQuad{}; }
    bool rasterize_glyphs(const char* text, float font_size, const char*, std::vector<GlyphQuad>& out_quads,
                          float* out_w, float* out_h) override {
        float w = 0, h = 0;
        shape_to_quads(text, font_size, out_quads, w, h);
        if (out_w) *out_w = w; if (out_h) *out_h = h;
        return !out_quads.empty();
    }
    float measure_advance(const char* text, int n, float font_size, const char*) override {
        return measure_first_n_advance(text, n, font_size);
    }
    float get_time() const override { return g_time; }
};

// ============================================================================
// Demo widget column — the same control set built once per side
// ============================================================================
struct DemoColumn {
    IGuiWidget* panel = nullptr;
    IGuiButton* primary_btn = nullptr;
    std::vector<std::pair<IGuiWidget*, float>> rows;   // {widget, row height}
};

static DemoColumn build_column(IGuiContext* ctx, const char* title, int radio_group) {
    DemoColumn c;
    c.panel = ctx->create_widget(WidgetType::Panel);
    c.panel->set_clip_enabled(true);

    auto add = [&](IGuiWidget* w, float h) { c.panel->add_child(w); c.rows.push_back({w, h}); };

    IGuiLabel* heading = ctx->create_label(title);                       add(heading, 24);

    IGuiButton* b1 = ctx->create_button(ButtonType::Normal); b1->set_text("Normal button");        add(b1, 30);
    c.primary_btn  = ctx->create_button(ButtonType::Normal); c.primary_btn->set_text("Primary action"); add(c.primary_btn, 30);

    IGuiButton* chk = ctx->create_button(ButtonType::Checkbox); chk->set_text("Enable feature"); chk->set_checked(true); add(chk, 24);

    IGuiButton* r1 = ctx->create_button(ButtonType::Radio); r1->set_text("Option A"); r1->set_radio_group(radio_group); r1->set_checked(true);
    IGuiButton* r2 = ctx->create_button(ButtonType::Radio); r2->set_text("Option B"); r2->set_radio_group(radio_group);
    r1->add_radio_peer(r2); r2->add_radio_peer(r1);
    add(r1, 22); add(r2, 22);

    IGuiSlider* sl = ctx->create_slider(SliderOrientation::Horizontal); sl->set_range(0, 100); sl->set_value(60); add(sl, 24);

    IGuiProgressBar* pb = ctx->create_progress_bar(ProgressBarMode::Determinate); pb->set_value(0.65f); add(pb, 18);

    IGuiTextInput* ti = ctx->create_text_input("Type here..."); add(ti, 28);

    IGuiComboBox* cb = ctx->create_combo_box(); cb->set_placeholder("Choose...");
    cb->add_item("First"); cb->add_item("Second"); cb->add_item("Third"); cb->set_selected_item(0); add(cb, 28);

    IGuiListBox* lb = ctx->create_list_box();
    lb->add_item("List item one"); lb->add_item("List item two"); lb->add_item("List item three"); lb->add_item("List item four");
    lb->set_selected_item(1); add(lb, 110);

    return c;
}

static void layout_column(DemoColumn& c, float px, float py, float pw, float ph) {
    c.panel->set_bounds(make_box(px, py, pw, ph));
    const float pad = 12.0f, gap = 8.0f, iw = pw - 2 * pad;
    float cy = py + pad;
    for (auto& row : c.rows) {
        row.first->set_bounds(make_box(px + pad, cy, iw, row.second));
        cy += row.second + gap;
    }
}

// ============================================================================
// Main
// ============================================================================
int main() {
    printf("=== CSS-look GUI Theme Showcase ===\n");

    Config config;
    config.windows[0].title  = "CSS Theme — left: light (full tree), right: dark (partial apply)";
    config.windows[0].width  = 1100;
    config.windows[0].height = 680;
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

    QuadRenderer renderer;
    if (!renderer.init()) { printf("Failed to init OpenGL renderer\n"); win->destroy(); return 1; }
    g_renderer = &renderer;

    // Font system + RAM glyph atlas (GPU mirror is an R8 texture array).
    font::Result font_result;
    font::IFontLibrary* font_library = font::create_font_library(font::FontBackend::Auto, &font_result);
    if (!font_library) { printf("Failed to create font library\n"); renderer.destroy(); win->destroy(); return 1; }
    {
        GLint max_size = 2048, max_layers = 256;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
        font::GlyphAtlasConfig acfg;
        acfg.layer_width  = std::min(max_size, 2048);
        acfg.layer_height = std::min(max_size, 2048);
        acfg.max_layers   = std::min(max_layers, 256);
        g_glyph_mgr = font::create_glyph_atlas_manager(font_library, acfg);
    }
    g_font_ui = font_library->load_system_font(font::FontDescriptor::create("Segoe UI", 14.0f), nullptr);
    if (!g_font_ui) g_font_ui = font_library->load_system_font(font::FontDescriptor::create("Arial", 14.0f), nullptr);
    if (!g_font_ui) g_font_ui = font_library->get_default_font(14.0f, nullptr);
    if (!g_font_ui || !g_glyph_mgr) {
        printf("Failed to load fonts\n");
        font::destroy_glyph_atlas_manager(g_glyph_mgr); font::destroy_font_library(font_library);
        renderer.destroy(); win->destroy(); return 1;
    }
    g_glyph_mgr->add_font(g_font_ui, true);
    g_shaper = font::create_text_shaper(font_library, &font_result);
    if (g_shaper) g_shaper->set_fallback_chain(g_glyph_mgr->fallback_chain());

    GuiResult gresult;
    IGuiContext* ctx = create_gui_context(&gresult);
    if (!ctx || gresult != GuiResult::Success) {
        printf("Failed to create GUI context\n");
        font::destroy_text_shaper(g_shaper); font::destroy_glyph_atlas_manager(g_glyph_mgr);
        font::destroy_font_library(font_library); renderer.destroy(); win->destroy(); return 1;
    }
    ctx->attach_window(win);

    float dpi_scale = win->get_dpi_scale();
    g_dpi_scale = dpi_scale;
    int sw_phys, sh_phys; win->get_size(&sw_phys, &sh_phys);
    float lw = sw_phys / dpi_scale, lh = sh_phys / dpi_scale;
    gui::Viewport vp; vp.id = 0; vp.bounds = make_box(0, 0, lw, lh); vp.scale = 1.0f;
    ctx->add_viewport(vp);

    IGuiWidget* root = ctx->get_root();
    root->set_bounds(make_box(0, 0, lw, lh));

    // ---- Chrome + two demo columns ----
    IGuiMenuBar* menubar = ctx->create_menu_bar();
    {
        IGuiMenu* file = ctx->create_menu();
        file->add_item("New", nullptr, "Ctrl+N"); file->add_item("Open", nullptr, "Ctrl+O");
        file->add_separator(); file->add_item("Exit", nullptr, "Alt+F4");
        IGuiMenu* edit = ctx->create_menu();
        edit->add_item("Undo", nullptr, "Ctrl+Z"); edit->add_item("Redo", nullptr, "Ctrl+Y");
        IGuiMenu* view = ctx->create_menu();
        view->add_radio_item("Light", 1, true); view->add_radio_item("Dark", 1, false);
        menubar->add_menu("File", file); menubar->add_menu("Edit", edit); menubar->add_menu("View", view);
    }
    root->add_child(menubar);

    IGuiStatusBar* statusbar = ctx->create_status_bar();
    statusbar->add_panel("CssTheme demo", StatusBarPanelSizeMode::Fill);
    statusbar->add_panel("light + dark", StatusBarPanelSizeMode::Auto);
    statusbar->add_panel("UTF-8", StatusBarPanelSizeMode::Auto);
    root->add_child(statusbar);

    DemoColumn left  = build_column(ctx, "Light theme (whole tree)",   1);
    DemoColumn right = build_column(ctx, "Dark theme (partial apply)", 2);
    root->add_child(left.panel);
    root->add_child(right.panel);

    // ====================================================================
    // CssTheme — the point of this example.
    // ====================================================================
    // 1) Theme the entire widget tree with the light (page-white) CSS look.
    CssTheme light = CssTheme::light();
    int styled = light.apply(root);
    printf("CssTheme::light().apply(root) styled %d widgets\n", styled);

    // 2) Partial application: re-theme ONLY the right column's subtree with the
    //    dark palette. apply() on a subtree root touches just that subtree.
    CssTheme dark = CssTheme::dark();
    int restyled = dark.apply(right.panel);
    printf("CssTheme::dark().apply(rightColumn) restyled %d widgets\n", restyled);

    // 3) Per-widget: give each column's "Primary action" the accent CTA look.
    left.primary_btn->set_button_style(light.button_primary_style());
    right.primary_btn->set_button_style(dark.button_primary_style());

    // Text plumbing so get_render_info() flattens Text/Slice9 and editfields measure.
    GuiTextRasterizer text_rasterizer;
    ctx->set_text_rasterizer(&text_rasterizer);
    FontTextMeasurer font_measurer;

    printf("Left column = light CSS look; right column = dark (partial). Close window to exit.\n\n");

    auto start_time = std::chrono::high_resolution_clock::now();
    float prev_time = 0;

    while (!win->should_close()) {
        win->poll_events();

        auto now = std::chrono::high_resolution_clock::now();
        float current_time = std::chrono::duration<float>(now - start_time).count();
        float dt = current_time - prev_time; prev_time = current_time; g_time = current_time;

        int sw_p, sh_p; win->get_size(&sw_p, &sh_p);
        dpi_scale = win->get_dpi_scale(); g_dpi_scale = dpi_scale;
        int sw = (int)(sw_p / dpi_scale), sh = (int)(sh_p / dpi_scale);
        g_window_h = sh;

        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        ctx->update_viewport(vp);
        root->set_bounds(make_box(0, 0, (float)sw, (float)sh));

        // Layout: menubar on top, statusbar at bottom, two equal columns between.
        menubar->set_bounds(make_box(0, 0, (float)sw, 26));
        statusbar->set_bounds(make_box(0, (float)sh - 24, (float)sw, 24));
        const float top = 34.0f, bottom = (float)sh - 24.0f, margin = 16.0f, gap = 16.0f;
        const float col_w = ((float)sw - 2 * margin - gap) * 0.5f;
        const float col_h = bottom - top - margin;
        layout_column(left,  margin,                 top, col_w, col_h);
        layout_column(right, margin + col_w + gap,   top, col_w, col_h);

        ctx->begin_frame(dt);
        ctx->end_frame();

        g_glyph_mgr->begin_frame();
        glViewport(0, 0, sw_p, sh_p);
        glClearColor(0.93f, 0.94f, 0.95f, 1.0f);   // light page backdrop
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.set_projection(sw, sh);

        draw_render_info(ctx->get_render_info());
        sync_atlas_to_gpu();
        g_renderer->flush_all(g_atlas_tex);

        static unsigned frame_counter = 0;
        if ((++frame_counter % 240) == 0) g_glyph_mgr->collect_garbage();

        gfx->present();
        (void)font_measurer;
    }

    cleanup_glyph_atlas();
    renderer.destroy(); g_renderer = nullptr;
    destroy_gui_context(ctx);
    font::destroy_text_shaper(g_shaper); g_shaper = nullptr;
    font::destroy_glyph_atlas_manager(g_glyph_mgr); g_glyph_mgr = nullptr;
    font::destroy_font_library(font_library);
    win->destroy();

    printf("Window closed.\n");
    return 0;
}
