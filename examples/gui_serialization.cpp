/*
 * gui_serialization.cpp - GUI Serialization Example (OpenGL)
 *
 * Demonstrates saving and loading GUI widget trees with live rendering:
 *   - Builds an IDE-like layout and renders it with OpenGL
 *   - Ctrl+S: Save to JSON file
 *   - Ctrl+B: Save to binary file
 *   - Ctrl+L: Load from JSON file (replaces current layout)
 *   - Ctrl+K: Load from binary file
 *   - Status bar shows last operation result
 */

#include "window.hpp"
#include "gui/gui.hpp"
#include "gui/font/font.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include "input/input_keyboard.hpp"
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
// QuadRenderer (same as gui.cpp example)
// ============================================================================

class QuadRenderer {
public:
    GLuint program = 0, vao = 0, vbo = 0, ibo = 0;
    GLint  loc_projection = -1, loc_texture = -1;
    static const int MAX_VERTS = 8192 * 6;

    struct DrawCmd {
        GLuint first, count;
        GLenum primitive;
        bool scissor_active;
        int sc_x, sc_y, sc_w, sc_h;
    };
    struct IndirectCmd {
        GLuint count, instanceCount, first, baseInstance;
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

        glGenBuffers(1, &ibo);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ibo);
        glBufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(IndirectCmd)*4096, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        m_verts.reserve(512*6*9);
        m_draw_cmds.reserve(256);
        return true;
    }

    void destroy() {
        if (ibo) glDeleteBuffers(1, &ibo);
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
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(m_verts.size()*sizeof(float)), m_verts.data());

        std::vector<IndirectCmd> indirect;
        indirect.reserve(m_draw_cmds.size());
        for (auto& dc : m_draw_cmds) indirect.push_back({dc.count,1,dc.first,0});

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, ibo);
        glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, (GLsizeiptr)(indirect.size()*sizeof(IndirectCmd)), indirect.data());

        glUseProgram(program);
        if (atlas_tex) {
            glUniform1i(loc_texture, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex);
        }

        glBindVertexArray(vao);
        int i=0, n=(int)m_draw_cmds.size();
        while (i < n) {
            auto& dc = m_draw_cmds[i];
            if (dc.scissor_active) { glEnable(GL_SCISSOR_TEST); glScissor(dc.sc_x,dc.sc_y,dc.sc_w,dc.sc_h); }
            else glDisable(GL_SCISSOR_TEST);
            int j=i+1;
            while (j<n && m_draw_cmds[j].primitive==dc.primitive &&
                   m_draw_cmds[j].scissor_active==dc.scissor_active &&
                   m_draw_cmds[j].sc_x==dc.sc_x && m_draw_cmds[j].sc_y==dc.sc_y &&
                   m_draw_cmds[j].sc_w==dc.sc_w && m_draw_cmds[j].sc_h==dc.sc_h) ++j;
            glMultiDrawArraysIndirect(dc.primitive, (const void*)(GLintptr)(i*sizeof(IndirectCmd)), j-i, sizeof(IndirectCmd));
            i=j;
        }
        glDisable(GL_SCISSOR_TEST);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
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

static QuadRenderer*     g_renderer    = nullptr;
static font::IFontAtlas* g_font_atlas  = nullptr;
static float g_time = 0;
static int   g_window_h = 720;

// ============================================================================
// Text rendering (same pattern as gui.cpp)
// ============================================================================

struct TextEntry {
    int layer=-1, width=0, height=0;
    float u0=0, v0=0, u1=0, v1=0;
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
    g_text_cache[key] = entry;
    return entry;
}

static float measure_text_width_n(const char* text, int n, font::IFontFace* face = nullptr) {
    if (!text || n<=0) return 0;
    if (!face) face = g_font_ui;
    std::string sub(text, std::min(n, (int)strlen(text)));
    TextEntry e = get_text_entry(sub.c_str(), face);
    return (float)e.width;
}

// ============================================================================
// Draw render info (same as gui.cpp)
// ============================================================================

static void draw_render_info(const gui::WidgetRenderInfo& ri) {
    using Pool = gui::WidgetRenderInfo::DrawRef::Pool;
    bool scissor_on = false;
    for (const auto& ref : ri.get_draw_order()) {
        if (ref.clip_changed) {
            if (!box_is_empty(ref.clip)) {
                float bx=x(box_min(ref.clip)), by=y(box_min(ref.clip));
                float bw=box_width(ref.clip), bh=box_height(ref.clip);
                g_renderer->set_scissor_state(true, (int)bx, g_window_h-(int)(by+bh), (int)bw, (int)bh);
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
// Text measurer and rasterizer for GUI context
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
// Build sample IDE-like layout
// ============================================================================

struct LayoutWidgets {
    IGuiMenuBar*    menubar;
    IGuiToolbar*    toolbar;
    IGuiStatusBar*  statusbar;
    IGuiSplitPanel* main_split;
    IGuiTreeView*   tree;
    IGuiSplitPanel* content_split;
    IGuiTabControl* tabs;
    IGuiEditBox*    editor;
    IGuiListBox*    output;
};

static LayoutWidgets build_layout(IGuiContext* ctx) {
    LayoutWidgets w = {};
    IGuiWidget* root = ctx->get_root();

    // Menu bar
    w.menubar = ctx->create_menu_bar();
    w.menubar->set_name("menubar");
    w.menubar->set_bounds(make_box(0, 0, 1280, 26));

    IGuiMenu* file_menu = ctx->create_menu();
    file_menu->add_item("Save JSON",   nullptr, "Ctrl+S");
    file_menu->add_item("Save Binary", nullptr, "Ctrl+B");
    file_menu->add_separator();
    file_menu->add_item("Load JSON",   nullptr, "Ctrl+L");
    file_menu->add_item("Load Binary", nullptr, "Ctrl+K");
    file_menu->add_separator();
    file_menu->add_item("Exit",        nullptr, "Alt+F4");

    IGuiMenu* edit_menu = ctx->create_menu();
    edit_menu->add_item("Undo", nullptr, "Ctrl+Z");
    edit_menu->add_item("Redo", nullptr, "Ctrl+Y");
    edit_menu->add_separator();
    edit_menu->add_item("Cut",   nullptr, "Ctrl+X");
    edit_menu->add_item("Copy",  nullptr, "Ctrl+C");
    edit_menu->add_item("Paste", nullptr, "Ctrl+V");

    w.menubar->add_menu("File", file_menu);
    w.menubar->add_menu("Edit", edit_menu);
    root->add_child(w.menubar);

    // Toolbar
    w.toolbar = ctx->create_toolbar(ToolbarOrientation::Horizontal);
    w.toolbar->set_name("toolbar");
    w.toolbar->set_bounds(make_box(0, 26, 1280, 32));
    w.toolbar->add_button("save_json", "Save JSON");
    w.toolbar->add_button("save_bin",  "Save Bin");
    w.toolbar->add_separator();
    w.toolbar->add_button("load_json", "Load JSON");
    w.toolbar->add_button("load_bin",  "Load Bin");
    root->add_child(w.toolbar);

    // Status bar
    w.statusbar = ctx->create_status_bar();
    w.statusbar->set_name("statusbar");
    w.statusbar->set_bounds(make_box(0, 696, 1280, 24));
    w.statusbar->add_panel("Ready - Ctrl+S: Save JSON | Ctrl+B: Save Binary | Ctrl+L: Load JSON | Ctrl+K: Load Binary", StatusBarPanelSizeMode::Fill);
    w.statusbar->add_panel("Widgets: 0", StatusBarPanelSizeMode::Auto);
    root->add_child(w.statusbar);

    // Main split: sidebar (tree) | content
    w.main_split = ctx->create_split_panel(SplitOrientation::Horizontal);
    w.main_split->set_name("main_split");
    w.main_split->set_split_position(250.f);
    w.main_split->set_first_min_size(150.f);
    w.main_split->set_second_min_size(400.f);
    root->add_child(w.main_split);

    // Tree view
    w.tree = ctx->create_tree_view();
    w.tree->set_name("project_tree");
    int proj = w.tree->add_node(-1, "Project");
    int src = w.tree->add_node(proj, "src");
    w.tree->add_node(src, "main.cpp");
    w.tree->add_node(src, "app.cpp");
    w.tree->add_node(src, "app.hpp");
    int inc = w.tree->add_node(proj, "include");
    w.tree->add_node(inc, "config.hpp");
    w.tree->add_node(inc, "types.hpp");
    int res = w.tree->add_node(proj, "resources");
    w.tree->add_node(res, "icon.png");
    w.tree->add_node(res, "style.css");
    w.tree->set_node_expanded(proj, true);
    w.tree->set_node_expanded(src, true);
    w.main_split->set_first_panel(w.tree);

    // Content split: tabs | output
    w.content_split = ctx->create_split_panel(SplitOrientation::Vertical);
    w.content_split->set_name("content_split");
    w.content_split->set_split_ratio(0.7f);
    w.main_split->set_second_panel(w.content_split);

    // Tab control
    w.tabs = ctx->create_tab_control(TabPosition::Top);
    w.tabs->set_name("editor_tabs");

    // Tab 1: editor
    w.editor = ctx->create_editbox();
    w.editor->set_name("code_editor");
    w.editor->set_text(
        "#include <iostream>\n"
        "\n"
        "int main() {\n"
        "    std::cout << \"Hello, serialization!\" << std::endl;\n"
        "    return 0;\n"
        "}\n"
    );
    int tab0 = w.tabs->add_tab("main.cpp");
    w.tabs->set_tab_content(tab0, w.editor);

    // Tab 2: property grid
    IGuiPropertyGrid* props = ctx->create_property_grid();
    props->set_name("properties");
    props->add_property("General", "Name",   PropertyType::String);
    props->add_property("General", "Width",  PropertyType::Int);
    props->add_property("General", "Height", PropertyType::Int);
    props->add_property("Style",   "Color",  PropertyType::Color);
    int tab1 = w.tabs->add_tab("Properties");
    w.tabs->set_tab_content(tab1, props);

    w.tabs->set_active_tab(0);
    w.content_split->set_first_panel(w.tabs);

    // Output list
    w.output = ctx->create_list_box();
    w.output->set_name("output_list");
    w.output->add_item("[INFO] GUI Serialization Example started");
    w.output->add_item("[INFO] Press Ctrl+S to save layout as JSON");
    w.output->add_item("[INFO] Press Ctrl+B to save layout as binary");
    w.output->add_item("[INFO] Press Ctrl+L to load layout from JSON");
    w.output->add_item("[INFO] Press Ctrl+K to load layout from binary");
    w.content_split->set_second_panel(w.output);

    return w;
}

// Count widgets recursively
static int count_widgets(IGuiWidget* w) {
    if (!w) return 0;
    int c = 1;
    for (int i = 0; i < w->get_child_count(); i++)
        c += count_widgets(w->get_child(i));
    return c;
}

// Layout widgets based on window size
static void layout_widgets(LayoutWidgets& w, int sw, int sh) {
    float fw = (float)sw, fh = (float)sh;

    w.menubar->set_bounds(make_box(0, 0, fw, 26));
    w.toolbar->set_bounds(make_box(0, 26, fw, 32));
    w.statusbar->set_bounds(make_box(0, fh - 24, fw, 24));

    float content_y = 58.f;
    float content_h = fh - 58 - 24;
    w.main_split->set_bounds(make_box(0, content_y, fw, content_h));
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== GUI Serialization Example (OpenGL) ===\n");

    // Create window
    Config config;
    config.windows[0].title = "GUI Serialization Example";
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
    printf("Window: %s (%s)\n", gfx->get_backend_name(), gfx->get_device_name());

    // Init renderer
    QuadRenderer renderer;
    if (!renderer.init()) { printf("Failed to init renderer\n"); win->destroy(); return 1; }

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
    g_renderer = &renderer;

    // Font system
    font::Result font_result;
    font::IFontLibrary* font_lib = font::create_font_library(font::FontBackend::Auto, &font_result);
    if (!font_lib) { printf("Failed to create font library\n"); renderer.destroy(); win->destroy(); return 1; }

    g_font_renderer = font::create_font_renderer(font_lib, &font_result);
    if (!g_font_renderer) { printf("Failed to create font renderer\n"); font::destroy_font_library(font_lib); renderer.destroy(); win->destroy(); return 1; }

    g_font_ui = font_lib->load_system_font(font::FontDescriptor::create("Segoe UI", 14.f), nullptr);
    if (!g_font_ui) g_font_ui = font_lib->load_system_font(font::FontDescriptor::create("Arial", 14.f), nullptr);
    if (!g_font_ui) g_font_ui = font_lib->get_default_font(14.f, nullptr);

    g_font_small = font_lib->load_system_font(font::FontDescriptor::create("Segoe UI", 12.f), nullptr);
    if (!g_font_small) g_font_small = font_lib->load_system_font(font::FontDescriptor::create("Arial", 12.f), nullptr);
    if (!g_font_small) g_font_small = font_lib->get_default_font(12.f, nullptr);

    if (!g_font_ui || !g_font_small) { printf("Failed to load fonts\n"); font::destroy_font_renderer(g_font_renderer); font::destroy_font_library(font_lib); renderer.destroy(); win->destroy(); return 1; }

    font::IFontFallbackChain* fallback = font::create_fallback_chain_with_defaults(font_lib, g_font_ui, 14.f);
    if (fallback) g_font_renderer->set_fallback_chain(fallback);

    // GUI context
    GuiResult gresult;
    IGuiContext* ctx = create_gui_context(&gresult);
    if (!ctx) { printf("Failed to create GUI context\n"); return 1; }

    ctx->attach_window(win);
    gui::Viewport vp; vp.id=0; vp.bounds=make_box(0,0,1280,720); vp.scale=1.f;
    ctx->add_viewport(vp);
    ctx->get_root()->set_bounds(make_box(0,0,1280,720));

    // Text measurer & rasterizer
    FontTextMeasurer text_measurer; text_measurer.face = g_font_ui;
    GuiTextRasterizer text_rasterizer; text_rasterizer.face_ui = g_font_ui; text_rasterizer.face_small = g_font_small;
    ctx->set_text_rasterizer(&text_rasterizer);

    // Build layout
    LayoutWidgets lw = build_layout(ctx);
    lw.editor->set_text_measurer(&text_measurer);

    printf("Layout built: %d widgets\n", count_widgets(ctx->get_root()));
    printf("Ctrl+S: Save JSON | Ctrl+B: Save Binary | Ctrl+L: Load JSON | Ctrl+K: Load Binary\n");

    // Serialization state
    std::string status_msg = "Ready";
    const char* json_path = "layout.json";
    const char* bin_path  = "layout.bin";

    // Keyboard handler for save/load shortcuts
    struct SerializationKeyHandler : public input::IKeyboardHandler {
        IGuiContext* ctx;
        LayoutWidgets* lw;
        std::string* status_msg;
        const char* json_path;
        const char* bin_path;
        FontTextMeasurer* text_measurer;

        const char* get_handler_id() const override { return "serialization_shortcuts"; }
        int get_priority() const override { return 100; }

        bool on_key(const KeyEvent& event) override {
            if (event.type != EventType::KeyDown || event.repeat) return false;
            if ((static_cast<uint8_t>(event.modifiers) & static_cast<uint8_t>(KeyMod::Control)) == 0) return false;

            GuiSerializeResult r;
            switch (event.key) {
            case Key::S: // Save JSON
                r = gui_save(ctx, json_path, GuiSerializeFormat::Json);
                if (r == GuiSerializeResult::Success) {
                    *status_msg = "Saved JSON to " + std::string(json_path);
                    lw->output->add_item(("[OK] Saved JSON: " + std::string(json_path)).c_str());
                } else {
                    *status_msg = std::string("Save JSON failed: ") + gui_serialize_result_to_string(r);
                    lw->output->add_item(("[FAIL] Save JSON: " + std::string(gui_serialize_result_to_string(r))).c_str());
                }
                return true;

            case Key::B: // Save binary
                r = gui_save(ctx, bin_path, GuiSerializeFormat::Binary);
                if (r == GuiSerializeResult::Success) {
                    *status_msg = "Saved binary to " + std::string(bin_path);
                    lw->output->add_item(("[OK] Saved binary: " + std::string(bin_path)).c_str());
                } else {
                    *status_msg = std::string("Save binary failed: ") + gui_serialize_result_to_string(r);
                    lw->output->add_item(("[FAIL] Save binary: " + std::string(gui_serialize_result_to_string(r))).c_str());
                }
                return true;

            case Key::L: { // Load JSON
                r = gui_load(ctx, json_path, GuiSerializeFormat::Json);
                if (r == GuiSerializeResult::Success) {
                    int wc = count_widgets(ctx->get_root());
                    *status_msg = "Loaded JSON (" + std::to_string(wc) + " widgets)";
                    *lw = build_layout(ctx);
                    lw->editor->set_text_measurer(text_measurer);
                    lw->output->add_item(("[OK] Loaded JSON: " + std::to_string(wc) + " widgets").c_str());
                } else {
                    *status_msg = std::string("Load JSON failed: ") + gui_serialize_result_to_string(r);
                }
                return true;
            }

            case Key::K: { // Load binary
                r = gui_load(ctx, bin_path, GuiSerializeFormat::Binary);
                if (r == GuiSerializeResult::Success) {
                    int wc = count_widgets(ctx->get_root());
                    *status_msg = "Loaded binary (" + std::to_string(wc) + " widgets)";
                    *lw = build_layout(ctx);
                    lw->editor->set_text_measurer(text_measurer);
                    lw->output->add_item(("[OK] Loaded binary: " + std::to_string(wc) + " widgets").c_str());
                } else {
                    *status_msg = std::string("Load binary failed: ") + gui_serialize_result_to_string(r);
                }
                return true;
            }
            default: break;
            }
            return false;
        }
    };

    SerializationKeyHandler key_handler;
    key_handler.ctx = ctx;
    key_handler.lw = &lw;
    key_handler.status_msg = &status_msg;
    key_handler.json_path = json_path;
    key_handler.bin_path = bin_path;
    key_handler.text_measurer = &text_measurer;
    win->add_keyboard_handler(&key_handler);

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

        int sw, sh;
        win->get_size(&sw, &sh);
        g_window_h = sh;

        // Update viewport
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        ctx->update_viewport(vp);
        ctx->get_root()->set_bounds(make_box(0, 0, (float)sw, (float)sh));
        layout_widgets(lw, sw, sh);

        // Update status bar
        int wcount = count_widgets(ctx->get_root());
        lw.statusbar->set_panel_text(0, status_msg.c_str());
        lw.statusbar->set_panel_text(1, (std::string("Widgets: ") + std::to_string(wcount)).c_str());

        // GUI frame
        ctx->begin_frame(dt);
        ctx->end_frame();

        // Render
        glViewport(0, 0, sw, sh);
        glClearColor(0.12f, 0.12f, 0.13f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderer.set_projection(sw, sh);

        draw_render_info(ctx->get_render_info());
        g_renderer->flush_all((GLuint)g_font_atlas->get_gpu_handle());

        gfx->present();
    }

    // Cleanup
    g_text_cache.clear();
    font::destroy_font_atlas(g_font_atlas); g_font_atlas = nullptr;
    renderer.destroy(); g_renderer = nullptr;
    ctx->detach_window(win);
    destroy_gui_context(ctx);
    font::destroy_font_renderer(g_font_renderer); g_font_renderer = nullptr;
    if (fallback) font::destroy_fallback_chain(fallback);
    font::destroy_font_library(font_lib);
    win->destroy();

    printf("Done.\n");
    return 0;
}
