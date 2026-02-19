/*
 * gui.cpp - Visual GUI Widget Showcase (OpenGL)
 *
 * Opens a window and renders all GUI widget types with native font rendering.
 * Demonstrates mouse interaction with hover/click state changes.
 */

#include "window.hpp"
#include "gui/gui.hpp"
#include "gui/font/font.hpp"
#include "input/input_keyboard.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
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
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;
uniform mat4 uProjection;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

static const char* g_fragment_shader = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;
uniform vec4 uColor;
uniform sampler2D uTexture;
uniform bool uUseTexture;
void main() {
    if (uUseTexture) {
        FragColor = texture(uTexture, TexCoord) * uColor;
    } else {
        FragColor = uColor;
    }
}
)";

// ============================================================================
// QuadRenderer - Modern OpenGL renderer with texture support
// ============================================================================

class QuadRenderer {
public:
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint loc_projection = -1;
    GLint loc_color = -1;
    GLint loc_texture = -1;
    GLint loc_use_texture = -1;

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
        loc_color = glGetUniformLocation(program, "uColor");
        loc_texture = glGetUniformLocation(program, "uTexture");
        loc_use_texture = glGetUniformLocation(program, "uUseTexture");

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // 128 verts * 4 floats (pos + texcoord) for circle fan
        glBufferData(GL_ARRAY_BUFFER, 128 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        // Position (location 0)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        // TexCoord (location 1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        return true;
    }

    void destroy() {
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

    void draw_rect(float px, float py, float pw, float ph,
                   float r, float g, float b, float a = 1.0f) {
        float verts[] = {
            px,      py,      0, 0,
            px + pw, py,      0, 0,
            px + pw, py + ph, 0, 0,
            px,      py,      0, 0,
            px + pw, py + ph, 0, 0,
            px,      py + ph, 0, 0
        };
        glUseProgram(program);
        glUniform4f(loc_color, r, g, b, a);
        glUniform1i(loc_use_texture, 0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void draw_texture(GLuint tex_id, float px, float py, float pw, float ph,
                      float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) {
        float verts[] = {
            px,      py,      0.0f, 0.0f,
            px + pw, py,      1.0f, 0.0f,
            px + pw, py + ph, 1.0f, 1.0f,
            px,      py,      0.0f, 0.0f,
            px + pw, py + ph, 1.0f, 1.0f,
            px,      py + ph, 0.0f, 1.0f
        };
        glUseProgram(program);
        glUniform4f(loc_color, r, g, b, a);
        glUniform1i(loc_use_texture, 1);
        glUniform1i(loc_texture, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_id);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void draw_circle(float cx, float cy, float radius,
                     float r, float g, float b, float a = 1.0f) {
        const int segs = 24;
        float verts[(segs + 2) * 4];
        verts[0] = cx; verts[1] = cy; verts[2] = 0; verts[3] = 0;
        for (int i = 0; i <= segs; i++) {
            float angle = 2.0f * 3.14159265f * i / segs;
            int base = (i + 1) * 4;
            verts[base]     = cx + radius * cosf(angle);
            verts[base + 1] = cy + radius * sinf(angle);
            verts[base + 2] = 0;
            verts[base + 3] = 0;
        }
        glUseProgram(program);
        glUniform4f(loc_color, r, g, b, a);
        glUniform1i(loc_use_texture, 0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (segs + 2) * 4 * sizeof(float), verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, segs + 2);
        glBindVertexArray(0);
    }
};

static QuadRenderer* g_renderer = nullptr;
static float g_time = 0; // for cursor blink

// ============================================================================
// Drawing Helpers (delegate to QuadRenderer)
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

// ============================================================================
// Text Rendering Cache
// ============================================================================

struct TextEntry {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    g_font_renderer->free_bitmap(pixels);

    TextEntry entry{tex, w, h};
    g_text_cache[key] = entry;
    return entry;
}

static void draw_text(const char* text, float px, float py, const Vec4& color,
                      font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.texture == 0) return;
    g_renderer->draw_texture(e.texture, px, py, (float)e.width, (float)e.height,
                             color.x, color.y, color.z, color.w);
}

// Draw text vertically centered in a rect
static void draw_text_vc(const char* text, float px, float py, float ph, const Vec4& color,
                         font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.texture == 0) return;
    float ty = py + (ph - e.height) / 2.0f;
    g_renderer->draw_texture(e.texture, px, ty, (float)e.width, (float)e.height,
                             color.x, color.y, color.z, color.w);
}

// Draw text centered horizontally and vertically in a rect
static void draw_text_center(const char* text, float px, float py, float pw, float ph,
                              const Vec4& color, font::IFontFace* face = nullptr) {
    if (!face) face = g_font_ui;
    TextEntry e = get_text_entry(text, face);
    if (e.texture == 0) return;
    float tx = px + (pw - e.width) / 2.0f;
    float ty = py + (ph - e.height) / 2.0f;
    g_renderer->draw_texture(e.texture, tx, ty, (float)e.width, (float)e.height,
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

static void cleanup_text_cache() {
    for (auto& pair : g_text_cache) {
        if (pair.second.texture) glDeleteTextures(1, &pair.second.texture);
    }
    g_text_cache.clear();
}

// ============================================================================
// Widget-Specific Renderers
// ============================================================================

static void render_button(IGuiButton* btn) {
    auto b = btn->get_bounds();
    float px = x(box_min(b)), py = y(box_min(b));
    float pw = box_width(b), ph = box_height(b);

    const auto& s = btn->get_button_style();
    Vec4 bg_color;

    switch (btn->get_state()) {
        case WidgetState::Pressed:  bg_color = s.pressed_color; break;
        case WidgetState::Hovered:  bg_color = s.hover_color; break;
        case WidgetState::Disabled: bg_color = s.disabled_color; break;
        default:
            bg_color = (btn->is_checked()) ? s.checked_color : s.background_color;
            break;
    }

    // Background
    draw_rect_v4(px, py, pw, ph, bg_color);

    // Border
    draw_rect_outline(px, py, pw, ph, s.border_color.x, s.border_color.y, s.border_color.z);

    // Focus border
    if (btn->has_focus()) {
        draw_rect_outline(px - 1, py - 1, pw + 2, ph + 2,
                          s.focus_border_color.x, s.focus_border_color.y,
                          s.focus_border_color.z);
    }

    // Checkbox indicator
    if (btn->get_button_type() == ButtonType::Checkbox) {
        float cx = px + 4, cy = py + ph/2 - 6;
        draw_rect_outline(cx, cy, 12, 12, s.text_color.x, s.text_color.y, s.text_color.z);
        if (btn->is_checked()) {
            draw_rect(cx + 3, cy + 3, 6, 6, s.checked_color.x, s.checked_color.y, s.checked_color.z);
        }
    }

    // Radio indicator
    if (btn->get_button_type() == ButtonType::Radio) {
        float cx = px + 10, cy = py + ph/2;
        draw_circle(cx, cy, 6, s.border_color.x, s.border_color.y, s.border_color.z);
        draw_circle(cx, cy, 5, bg_color.x, bg_color.y, bg_color.z);
        if (btn->is_checked()) {
            draw_circle(cx, cy, 3, s.checked_color.x, s.checked_color.y, s.checked_color.z);
        }
    }

    // Text
    if (btn->get_text() && btn->get_text()[0]) {
        if (btn->get_button_type() == ButtonType::Checkbox || btn->get_button_type() == ButtonType::Radio) {
            draw_text_vc(btn->get_text(), px + 22, py, ph, s.text_color);
        } else {
            draw_text_center(btn->get_text(), px, py, pw, ph, s.text_color);
        }
    }
}

static void render_slider(IGuiSlider* slider) {
    SliderRenderInfo sri;
    slider->get_slider_render_info(&sri);

    auto b = sri.bounds;
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    bool horiz = (sri.orientation == SliderOrientation::Horizontal);
    float track_h = sri.style.track_height;
    float tr = sri.style.thumb_radius;

    if (horiz) {
        float cy = by + bh / 2.0f;
        draw_rect_v4(bx, cy - track_h/2, bw, track_h, sri.style.track_color);
        float fill_w = bw * sri.normalized;
        draw_rect_v4(bx, cy - track_h/2, fill_w, track_h, sri.style.track_fill_color);
        float tx = bx + bw * sri.normalized;
        const Vec4& tc = (sri.thumb_state == WidgetState::Pressed) ? sri.style.thumb_pressed_color
                       : (sri.thumb_state == WidgetState::Hovered) ? sri.style.thumb_hover_color
                       : sri.style.thumb_color;
        draw_circle(tx, cy, tr, tc.x, tc.y, tc.z);
    } else {
        float cx = bx + bw / 2.0f;
        draw_rect_v4(cx - track_h/2, by, track_h, bh, sri.style.track_color);
        float fill_h = bh * sri.normalized;
        draw_rect_v4(cx - track_h/2, by + bh - fill_h, track_h, fill_h, sri.style.track_fill_color);
        float ty = by + bh * (1.0f - sri.normalized);
        const Vec4& tc = (sri.thumb_state == WidgetState::Pressed) ? sri.style.thumb_pressed_color
                       : (sri.thumb_state == WidgetState::Hovered) ? sri.style.thumb_hover_color
                       : sri.style.thumb_color;
        draw_circle(cx, ty, tr, tc.x, tc.y, tc.z);
    }
}

static void render_progress_bar(IGuiProgressBar* prog) {
    ProgressBarRenderInfo pri;
    prog->get_progress_bar_render_info(&pri);

    auto b = pri.bounds;
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    draw_rect_v4(bx, by, bw, bh, pri.style.track_color);
    if (pri.mode == ProgressBarMode::Determinate) {
        draw_rect_v4(bx, by, bw * pri.value, bh, pri.style.fill_color);
    } else {
        float phase = pri.animation_phase;
        float iw = bw * pri.style.indeterminate_width;
        float ix = bx + (bw - iw) * phase;
        draw_rect_v4(ix, by, iw, bh, pri.style.indeterminate_color);
    }
}

static void render_listbox(IGuiListBox* listbox) {
    auto b = listbox->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = listbox->get_list_box_style();

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    int count = listbox->get_item_count();
    int sel = listbox->get_selected_item();
    float row_h = s.row_height;
    for (int i = 0; i < count && (i * row_h) < bh; i++) {
        float ry = by + i * row_h;
        Vec4 row_bg = (i == sel) ? s.selected_background
                    : (i % 2 == 0) ? s.row_background : s.row_alt_background;
        draw_rect_v4(bx, ry, bw, row_h, row_bg);

        Vec4 text_col = (i == sel) ? s.selected_text_color : s.text_color;
        const char* item_text = listbox->get_item_text(i);
        if (item_text && item_text[0]) {
            draw_text_vc(item_text, bx + s.item_padding, ry, row_h, text_col);
        }
    }

    draw_rect_outline(bx, by, bw, bh, 0.25f, 0.25f, 0.27f);
}

static void render_combobox(IGuiComboBox* combo) {
    auto b = combo->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = combo->get_combo_box_style();

    Vec4 bg = combo->is_open() ? s.open_background : s.background_color;
    draw_rect_v4(bx, by, bw, bh, bg);
    draw_rect_outline(bx, by, bw, bh, s.dropdown_border_color.x, s.dropdown_border_color.y, s.dropdown_border_color.z);

    // Selected text or placeholder
    int sel = combo->get_selected_item();
    const char* text = (sel >= 0) ? combo->get_item_text(sel) : combo->get_placeholder();
    Vec4 text_col = (sel >= 0) ? s.text_color : s.placeholder_color;
    if (text && text[0]) {
        draw_text_vc(text, bx + 8, by, bh, text_col);
    }

    // Arrow
    float ax = bx + bw - 18, ay = by + bh/2 - 3;
    draw_rect(ax, ay, 8, 6, s.arrow_color.x, s.arrow_color.y, s.arrow_color.z);

    // Dropdown list
    if (combo->is_open()) {
        int count = combo->get_item_count();
        float drop_h = std::min(s.dropdown_max_height, (float)count * s.item_height);
        float dy = by + bh;

        draw_rect_v4(bx, dy, bw, drop_h, s.dropdown_background);

        for (int i = 0; i < count && (i * s.item_height) < drop_h; i++) {
            float ry = dy + i * s.item_height;
            bool is_sel = (combo->get_item_text(i) && sel == i);
            Vec4 row_bg = is_sel ? s.item_selected_background : s.dropdown_background;
            draw_rect_v4(bx, ry, bw, s.item_height, row_bg);

            const char* item_text = combo->get_item_text(i);
            Vec4 item_col = is_sel ? s.item_selected_text_color : s.item_text_color;
            if (item_text && item_text[0]) {
                draw_text_vc(item_text, bx + s.item_padding, ry, s.item_height, item_col);
            }
        }

        draw_rect_outline(bx, dy, bw, drop_h,
                          s.dropdown_border_color.x, s.dropdown_border_color.y, s.dropdown_border_color.z);
    }
}

static void render_treeview(IGuiTreeView* tree) {
    auto b = tree->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = tree->get_tree_view_style();

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    TreeNodeRenderItem items[20];
    int count = tree->get_visible_tree_items(items, 20);
    float row_h = s.row_height;

    for (int i = 0; i < count && (i * row_h) < bh; i++) {
        float ry = by + i * row_h;
        Vec4 row_bg = items[i].selected ? s.selected_background
                    : items[i].hovered ? s.hover_background : s.row_background;
        draw_rect_v4(bx, ry, bw, row_h, row_bg);

        float indent = bx + items[i].depth * s.indent_width + 4;

        // Expand/collapse indicator
        if (items[i].has_children) {
            float ex = indent, ey = ry + row_h/2 - 3;
            draw_rect(ex, ey, 6, 6, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
        }

        // Icon placeholder
        draw_rect(indent + 12, ry + row_h/2 - 4, 8, 8,
                  s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.5f);

        // Node text
        if (items[i].text && items[i].text[0]) {
            Vec4 text_col = items[i].selected ? Vec4(1.0f, 1.0f, 1.0f, 1.0f) : s.text_color;
            draw_text_vc(items[i].text, indent + 24, ry, row_h, text_col, g_font_small);
        }
    }

    draw_rect_outline(bx, by, bw, bh, 0.25f, 0.25f, 0.27f);
}

static void render_tabcontrol(IGuiTabControl* tabs) {
    auto b = tabs->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = tabs->get_tab_style();

    draw_rect_v4(bx, by, bw, s.tab_height, s.tab_bar_background);

    TabRenderItem tab_items[10];
    int count = tabs->get_visible_tab_items(tab_items, 10);
    for (int i = 0; i < count; i++) {
        auto& t = tab_items[i];
        Vec4 tab_bg = t.active ? s.tab_active_background
                    : t.hovered ? s.tab_hover_background : s.tab_background;
        Vec4 text_col = t.active ? s.tab_active_text_color : s.tab_text_color;

        float tw = 80.0f;
        float tx = bx + i * tw;
        draw_rect_v4(tx, by, tw, s.tab_height, tab_bg);

        if (t.active) {
            draw_rect_v4(tx, by + s.tab_height - s.indicator_height, tw, s.indicator_height,
                         s.indicator_color);
        }

        // Tab title
        if (t.text && t.text[0]) {
            draw_text_center(t.text, tx, by, tw, s.tab_height, text_col, g_font_small);
        }
    }

    draw_rect_v4(bx, by + s.tab_height, bw, bh - s.tab_height, s.tab_active_background);
    draw_rect_outline(bx, by, bw, bh, 0.25f, 0.25f, 0.27f);
}

static void render_scrollbar(IGuiScrollBar* sb) {
    auto b = sb->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = sb->get_scrollbar_style();

    draw_rect_v4(bx, by, bw, bh, s.track_color);

    float range = sb->get_max_value() - sb->get_min_value();
    float page = sb->get_page_size();
    float total = range + page;
    if (total > 0) {
        float thumb_ratio = page / total;
        float thumb_h = bh * thumb_ratio;
        if (thumb_h < s.thumb_min_length) thumb_h = s.thumb_min_length;
        float track_range = bh - thumb_h;
        float pos_ratio = (range > 0) ? (sb->get_value() - sb->get_min_value()) / range : 0;
        float thumb_y = by + track_range * pos_ratio;

        const Vec4& tc = sb->is_thumb_pressed() ? s.thumb_pressed_color
                       : sb->is_thumb_hovered() ? s.thumb_hover_color
                       : s.thumb_color;
        draw_rect_v4(bx + 1, thumb_y, bw - 2, thumb_h, tc);
    }
}

static void render_propertygrid(IGuiPropertyGrid* pg) {
    auto b = pg->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = pg->get_property_grid_style();
    float row_h = pg->get_row_height();
    float name_w = pg->get_name_column_width();

    PropertyGridRenderInfo pgri;
    pg->get_property_grid_render_info(&pgri);

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    PropertyRenderItem items[20];
    int count = pg->get_visible_property_items(items, 20);

    for (int i = 0; i < count && (i * row_h) < bh; i++) {
        float ry = by + i * row_h;

        if (items[i].is_category_header) {
            draw_rect_v4(bx, ry, bw, row_h, s.category_background);
            if (items[i].name && items[i].name[0]) {
                draw_text_vc(items[i].name, bx + 8, ry, row_h, s.category_text_color, g_font_small);
            }
        } else {
            Vec4 row_bg = items[i].selected ? s.selected_background
                        : (i % 2 == 0) ? s.row_background : s.row_alt_background;
            draw_rect_v4(bx, ry, bw, row_h, row_bg);

            // Name column
            if (items[i].name && items[i].name[0]) {
                draw_text_vc(items[i].name, bx + 8 + items[i].depth * s.indent_width, ry, row_h,
                             s.name_text_color, g_font_small);
            }

            // Separator
            draw_rect(bx + name_w, ry, 1, row_h,
                      s.separator_color.x, s.separator_color.y, s.separator_color.z);

            // Value column
            bool editing = (pgri.editing_property == items[i].property_id && items[i].property_id >= 0);
            float val_x = bx + name_w + 2;
            float val_w = bw - name_w - 2;
            if (editing) {
                // Edit mode: show input background, edit buffer, and cursor
                draw_rect(val_x, ry + 1, val_w, row_h - 2, 0.10f, 0.10f, 0.10f);
                draw_rect_outline(val_x, ry + 1, val_w, row_h - 2, 0.0f, 0.48f, 0.8f);
                draw_text_vc(pgri.edit_buffer, val_x + 6, ry, row_h, s.value_text_color, g_font_small);
                if (fmodf(g_time, 1.0f) < 0.5f) {
                    float cx = val_x + 6 + measure_text_width(pgri.edit_buffer, g_font_small);
                    draw_rect(cx, ry + 4, 1, row_h - 8, s.value_text_color.x, s.value_text_color.y, s.value_text_color.z);
                }
            } else {
                const char* val_str = pg->get_string_value(items[i].property_id);
                if (val_str && val_str[0]) {
                    draw_text_vc(val_str, val_x + 6, ry, row_h,
                                 s.value_text_color, g_font_small);
                }
            }
        }
    }

    // Focus border
    if (pg->has_focus()) {
        draw_rect_outline(bx - 1, by - 1, bw + 2, bh + 2, 0.0f, 0.48f, 0.8f);
    }

    draw_rect_outline(bx, by, bw, bh, 0.25f, 0.25f, 0.27f);
}

static void hue_to_rgb(float h, float& rr, float& gg, float& bb) {
    float hp = h / 60.0f;
    float x_val = 1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f);
    if (hp < 1)      { rr=1; gg=x_val; bb=0; }
    else if (hp < 2) { rr=x_val; gg=1; bb=0; }
    else if (hp < 3) { rr=0; gg=1; bb=x_val; }
    else if (hp < 4) { rr=0; gg=x_val; bb=1; }
    else if (hp < 5) { rr=x_val; gg=0; bb=1; }
    else             { rr=1; gg=0; bb=x_val; }
}

// Color picker gradient textures
static GLuint g_sv_texture = 0;
static GLuint g_hue_texture = 0;
static float g_sv_cached_hue = -1.0f;

static void ensure_hue_texture() {
    if (g_hue_texture) return;
    const int h = 256;
    uint8_t pixels[h * 3];
    for (int i = 0; i < h; i++) {
        float hue = i / (float)(h - 1) * 360.0f;
        float rr, gg, bb;
        hue_to_rgb(hue, rr, gg, bb);
        pixels[i * 3 + 0] = (uint8_t)(rr * 255);
        pixels[i * 3 + 1] = (uint8_t)(gg * 255);
        pixels[i * 3 + 2] = (uint8_t)(bb * 255);
    }
    glGenTextures(1, &g_hue_texture);
    glBindTexture(GL_TEXTURE_2D, g_hue_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
}

static void update_sv_texture(float hue) {
    if (g_sv_texture && fabsf(hue - g_sv_cached_hue) < 0.01f) return;

    const int sz = 128;
    static uint8_t pixels[sz * sz * 3];
    float hr, hg, hb;
    hue_to_rgb(hue, hr, hg, hb);
    for (int yi = 0; yi < sz; yi++) {
        float val_f = 1.0f - yi / (float)(sz - 1);
        for (int xi = 0; xi < sz; xi++) {
            float sat_f = xi / (float)(sz - 1);
            int idx = (yi * sz + xi) * 3;
            pixels[idx + 0] = (uint8_t)((1.0f - sat_f + sat_f * hr) * val_f * 255);
            pixels[idx + 1] = (uint8_t)((1.0f - sat_f + sat_f * hg) * val_f * 255);
            pixels[idx + 2] = (uint8_t)((1.0f - sat_f + sat_f * hb) * val_f * 255);
        }
    }
    if (!g_sv_texture) {
        glGenTextures(1, &g_sv_texture);
        glBindTexture(GL_TEXTURE_2D, g_sv_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sz, sz, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    } else {
        glBindTexture(GL_TEXTURE_2D, g_sv_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sz, sz, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    }
    g_sv_cached_hue = hue;
}

static void render_colorpicker(IGuiColorPicker* picker) {
    auto b = picker->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = picker->get_color_picker_style();

    draw_rect_v4(bx, by, bw, bh, s.background_color);
    draw_rect_outline(bx, by, bw, bh, s.border_color.x, s.border_color.y, s.border_color.z);

    int nch = picker->is_alpha_enabled() ? 4 : 3;
    float rgba_area = 25 + nch * 18 + 20;  // preview + RGBA rows + hex + padding
    float sq_size = bw - 30;
    if (sq_size > bh - rgba_area) sq_size = bh - rgba_area;
    if (sq_size < 40) sq_size = 40;
    float sq_x = bx + 5, sq_y = by + 5;

    float hue_w = 16;
    float hue_x = bx + bw - hue_w - 5;
    float hue_h = sq_size;

    // Hue strip (texture-based gradient)
    ensure_hue_texture();
    g_renderer->draw_texture(g_hue_texture, hue_x, sq_y, hue_w, hue_h);

    // Hue indicator
    float hue_ind_y = sq_y + (picker->get_hue() / 360.0f) * hue_h;
    draw_rect(hue_x - 2, hue_ind_y - 1, hue_w + 4, 3, 1, 1, 1);

    // SV square (texture-based gradient)
    float sv_size = sq_size < (hue_x - sq_x - 5) ? sq_size : (hue_x - sq_x - 5);
    update_sv_texture(picker->get_hue());
    g_renderer->draw_texture(g_sv_texture, sq_x, sq_y, sv_size, sv_size);

    // SV crosshair
    float cx = sq_x + picker->get_saturation() * sv_size;
    float cy = sq_y + (1.0f - picker->get_brightness()) * sv_size;
    draw_rect(cx - 4, cy, 9, 1, 1, 1, 1);
    draw_rect(cx, cy - 4, 1, 9, 1, 1, 1);

    // Preview (current | previous)
    auto col = picker->get_color();
    float prev_y = sq_y + sv_size + 5;
    draw_rect_v4(sq_x, prev_y, sv_size / 2, 20, col);
    draw_rect_v4(sq_x + sv_size / 2, prev_y, sv_size / 2, 20, picker->get_previous_color());

    // RGBA input fields
    ColorPickerRenderInfo cpri;
    picker->get_color_picker_render_info(&cpri);
    float input_y = prev_y + 25;
    float input_w = sv_size / 2.0f;
    const char* labels[4] = {"R:", "G:", "B:", "A:"};
    float vals[4] = {col.x * 255, col.y * 255, col.z * 255, col.w * 255};
    for (int ch = 0; ch < nch; ch++) {
        float iy = input_y + ch * 18;
        bool editing = (cpri.editing_channel == ch);
        // Label
        draw_text_vc(labels[ch], bx + 5, iy, 16, s.label_color, g_font_small);
        // Input box (highlight border when editing)
        draw_rect_v4(bx + 30, iy, input_w, 16, s.input_background);
        if (editing)
            draw_rect_outline(bx + 30, iy, input_w, 16, 0.0f, 0.48f, 0.8f);
        else
            draw_rect_outline(bx + 30, iy, input_w, 16, 0.3f, 0.3f, 0.35f);
        // Value text (show edit buffer when editing, otherwise show current value)
        if (editing) {
            draw_text_vc(cpri.edit_buffer, bx + 34, iy, 16, s.input_text_color, g_font_small);
            // Blinking cursor
            if (fmodf(g_time, 1.0f) < 0.5f) {
                float cx = bx + 34 + measure_text_width(cpri.edit_buffer, g_font_small);
                draw_rect(cx, iy + 2, 1, 12, s.input_text_color.x, s.input_text_color.y, s.input_text_color.z);
            }
        } else {
            char vbuf[16];
            std::snprintf(vbuf, sizeof(vbuf), "%d", (int)vals[ch]);
            draw_text_vc(vbuf, bx + 34, iy, 16, s.input_text_color, g_font_small);
        }
    }

    // Hex display
    float hex_y = input_y + nch * 18 + 2;
    if (picker->is_hex_input_visible() && hex_y + 16 < by + bh) {
        draw_text_vc(picker->get_hex_string(), bx + 5, hex_y, 16, s.label_color, g_font_small);
    }
}

static void render_toolbar(IGuiToolbar* toolbar) {
    auto b = toolbar->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = toolbar->get_toolbar_style();

    draw_rect_v4(bx, by, bw, bh, s.background_color);

    ToolbarItemRenderInfo items[16];
    int count = toolbar->get_visible_toolbar_items(items, 16);
    float ix = bx + s.toolbar_padding;

    for (int i = 0; i < count; i++) {
        if (items[i].type == ToolbarItemType::Separator) {
            draw_rect(ix + s.separator_padding, by + 4, s.separator_width, bh - 8,
                      s.separator_color.x, s.separator_color.y, s.separator_color.z);
            ix += s.separator_width + s.separator_padding * 2;
        } else {
            Vec4 btn_bg = items[i].pressed ? s.button_pressed_color
                        : items[i].toggled ? s.button_toggled_color
                        : items[i].hovered ? s.button_hover_color
                        : s.button_color;
            draw_rect_v4(ix, by + s.button_padding, s.button_size, s.button_size, btn_bg);
            // Toolbar button text (tooltip)
            Vec4 icon_col = items[i].enabled ? s.icon_color : s.icon_disabled_color;
            if (items[i].tooltip_text && items[i].tooltip_text[0]) {
                draw_text_center(items[i].tooltip_text, ix, by + s.button_padding,
                                 s.button_size, s.button_size, icon_col, g_font_small);
            } else {
                draw_rect(ix + 6, by + s.button_padding + 6, s.icon_size, s.icon_size,
                          icon_col.x, icon_col.y, icon_col.z, 0.6f);
            }
            ix += s.button_size + s.button_padding;
        }
    }
}

static void render_statusbar(IGuiStatusBar* statusbar) {
    auto b = statusbar->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = statusbar->get_status_bar_style();

    draw_rect_v4(bx, by, bw, bh, s.background_color);

    StatusBarPanelRenderInfo panels[8];
    int count = statusbar->get_visible_status_bar_panels(panels, 8);
    float px_offset = bx + 8;
    float panel_w = bw / (count > 0 ? count : 1);

    for (int i = 0; i < count; i++) {
        float pw = panel_w;
        if (panels[i].text && panels[i].text[0]) {
            draw_text_vc(panels[i].text, px_offset, by, bh, s.text_color, g_font_small);
        }

        if (i < count - 1) {
            draw_rect(px_offset + pw - 1, by + 4, s.separator_width, bh - 8,
                      s.separator_color.x, s.separator_color.y, s.separator_color.z);
        }
        px_offset += pw;
    }
}

static void render_menubar(IGuiMenuBar* menubar) {
    auto b = menubar->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = menubar->get_menu_bar_style();

    draw_rect_v4(bx, by, bw, bh, s.background_color);

    MenuBarItemRenderInfo items[8];
    int count = menubar->get_visible_menu_bar_items(items, 8);
    float mx = bx + 8;

    for (int i = 0; i < count; i++) {
        Vec4 item_bg = items[i].open ? s.item_open_background
                     : items[i].hovered ? s.item_hover_background
                     : s.background_color;
        Vec4 text_col = items[i].open ? s.item_hover_text_color
                      : items[i].hovered ? s.item_hover_text_color : s.item_text_color;

        float iw = 60.0f;
        draw_rect_v4(mx, by, iw, bh, item_bg);
        if (items[i].text && items[i].text[0]) {
            draw_text_center(items[i].text, mx, by, iw, bh, text_col, g_font_small);
        }
        mx += iw;
    }
}

static Vec2 g_mouse_pos_for_menu; // set each frame for menu hover

static void render_menu_dropdown(IGuiMenu* menu) {
    if (!menu || !menu->is_open()) return;

    auto b = menu->get_bounds();
    float mx = x(box_min(b)), my = y(box_min(b));
    const auto& ms = menu->get_menu_style();

    MenuItemRenderInfo items[16];
    int count = menu->get_visible_menu_items(items, 16);

    // Calculate total height
    float total_h = 0;
    for (int i = 0; i < count; i++) {
        total_h += (items[i].type == MenuItemType::Separator) ? ms.separator_height : ms.item_height;
    }
    float mw = ms.min_width;

    // Background + border
    draw_rect_v4(mx, my, mw, total_h, ms.background_color);
    draw_rect_outline(mx, my, mw, total_h, ms.border_color.x, ms.border_color.y, ms.border_color.z);

    float iy = my;
    for (int i = 0; i < count; i++) {
        if (items[i].type == MenuItemType::Separator) {
            float sep_y = iy + ms.separator_height / 2;
            draw_rect_v4(mx + 4, sep_y, mw - 8, 1, ms.separator_color);
            iy += ms.separator_height;
        } else {
            // Hover highlight
            Box item_box = make_box(mx, iy, mw, ms.item_height);
            bool hovered = items[i].enabled && box_contains(item_box, g_mouse_pos_for_menu);
            if (hovered) {
                draw_rect_v4(mx, iy, mw, ms.item_height, ms.item_hover_background);
            }

            Vec4 text_col = items[i].enabled ? (hovered ? ms.item_hover_text_color : ms.item_text_color) : ms.item_disabled_text_color;

            // Checkbox/Radio indicator
            if (items[i].type == MenuItemType::Checkbox || items[i].type == MenuItemType::Radio) {
                if (items[i].checked) {
                    draw_text_vc("\xe2\x9c\x93", mx + 4, iy, ms.item_height, ms.check_color, g_font_small);
                }
            }

            // Item text
            if (items[i].text && items[i].text[0]) {
                draw_text_vc(items[i].text, mx + ms.icon_column_width, iy, ms.item_height, text_col, g_font_small);
            }

            // Shortcut text
            if (items[i].shortcut_text && items[i].shortcut_text[0]) {
                float sw = measure_text_width(items[i].shortcut_text, g_font_small);
                draw_text_vc(items[i].shortcut_text, mx + mw - sw - 8, iy, ms.item_height,
                             ms.shortcut_text_color, g_font_small);
            }

            iy += ms.item_height;
        }
    }
}

static void render_menu_overlays(IGuiMenuBar* menubar) {
    MenuBarItemRenderInfo items[8];
    int count = menubar->get_visible_menu_bar_items(items, 8);
    for (int i = 0; i < count; i++) {
        if (items[i].open) {
            IGuiMenu* menu = menubar->get_menu(items[i].item_id);
            render_menu_dropdown(menu);
        }
    }
}

static void render_splitpanel(IGuiSplitPanel* split) {
    SplitPanelRenderInfo sri;
    split->get_split_panel_render_info(&sri);

    draw_box(sri.first_panel_rect, Vec4(0.18f, 0.18f, 0.19f, 1.0f));
    draw_box(sri.second_panel_rect, Vec4(0.15f, 0.15f, 0.16f, 1.0f));
    const Vec4& sc = sri.splitter_hovered ? sri.style.splitter_hover_color
                   : sri.splitter_dragging ? sri.style.splitter_drag_color
                   : sri.style.splitter_color;
    draw_box(sri.splitter_rect, sc);

    // Panel labels
    Vec4 lc(0.6f, 0.6f, 0.6f, 1.0f);
    draw_text_center("Left", x(box_min(sri.first_panel_rect)), y(box_min(sri.first_panel_rect)),
                     box_width(sri.first_panel_rect), box_height(sri.first_panel_rect), lc, g_font_small);
    draw_text_center("Right", x(box_min(sri.second_panel_rect)), y(box_min(sri.second_panel_rect)),
                     box_width(sri.second_panel_rect), box_height(sri.second_panel_rect), lc, g_font_small);
}

static void render_label(IGuiLabel* label) {
    auto b = label->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& ls = label->get_label_style();
    const char* text = label->get_text();
    if (text && text[0]) {
        draw_text_vc(text, bx + 4, by, bh, ls.text_color);
    }
}

static void render_textinput(IGuiTextInput* input) {
    auto b = input->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    draw_rect(bx, by, bw, bh, 0.12f, 0.12f, 0.12f);
    draw_rect_outline(bx, by, bw, bh, 0.25f, 0.25f, 0.27f);

    if (input->has_focus()) {
        draw_rect_outline(bx - 1, by - 1, bw + 2, bh + 2, 0.0f, 0.48f, 0.8f);
    }

    const char* text = input->get_text();
    if (text && text[0]) {
        draw_text_vc(text, bx + 6, by, bh, Vec4(0.94f, 0.94f, 0.94f, 1.0f));
    } else {
        const char* ph = input->get_placeholder();
        if (ph && ph[0]) {
            draw_text_vc(ph, bx + 6, by, bh, Vec4(0.5f, 0.5f, 0.5f, 0.7f));
        }
    }

    // Blinking cursor
    if (input->has_focus() && fmodf(g_time, 1.0f) < 0.5f) {
        float cx = bx + 6 + measure_text_width_n(input->get_text(), input->get_cursor_position());
        draw_rect(cx, by + 4, 1, bh - 8, 0.94f, 0.94f, 0.94f);
    }
}

// Convert mouse position to editbox TextPosition
static TextPosition editbox_position_from_point(IGuiEditBox* editbox, const Vec2& point) {
    auto b = editbox->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    const auto& s = editbox->get_editbox_style();
    float line_h = s.font_size * s.line_height;
    float text_x = bx + (editbox->is_line_numbers_visible() ? s.gutter_width + s.padding : s.padding);

    int line = (int)((y(point) - by) / line_h);
    line = std::max(0, std::min(line, editbox->get_line_count() - 1));

    const char* line_text = editbox->get_line(line);
    int len = line_text ? (int)strlen(line_text) : 0;
    float mx = x(point) - text_x;

    // Binary search for the column
    int col = 0;
    for (int i = 1; i <= len; i++) {
        float w = measure_text_width_n(line_text, i, g_font_small);
        float prev_w = measure_text_width_n(line_text, i - 1, g_font_small);
        if (mx < (prev_w + w) * 0.5f) break;
        col = i;
    }

    return {line, col};
}

static void render_editbox(IGuiEditBox* editbox) {
    auto b = editbox->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = editbox->get_editbox_style();

    draw_rect_v4(bx, by, bw, bh, s.background_color);

    if (editbox->is_line_numbers_visible()) {
        draw_rect_v4(bx, by, s.gutter_width, bh, s.line_number_background);
        draw_rect(bx + s.gutter_width, by, 1, bh,
                  s.gutter_border_color.x, s.gutter_border_color.y, s.gutter_border_color.z);
    }

    float line_h = s.font_size * s.line_height;
    int line_count = editbox->get_line_count();
    float text_x = bx + (editbox->is_line_numbers_visible() ? s.gutter_width + s.padding : s.padding);

    // Get selection range (normalized so sel_s <= sel_e)
    TextRange sel_range = editbox->get_selection();
    TextPosition sel_s = sel_range.start, sel_e = sel_range.end;
    if (sel_s.line > sel_e.line || (sel_s.line == sel_e.line && sel_s.column > sel_e.column))
        std::swap(sel_s, sel_e);
    bool has_sel = editbox->has_selection();

    for (int i = 0; i < line_count && (i * line_h) < bh; i++) {
        float ly = by + i * line_h;

        // Line number
        if (editbox->is_line_numbers_visible()) {
            char num[16];
            snprintf(num, sizeof(num), "%d", i + 1);
            draw_text_vc(num, bx + 4, ly, line_h, s.line_number_color, g_font_small);
        }

        const char* line_text = editbox->get_line(i);
        int line_len = line_text ? (int)strlen(line_text) : 0;

        // Selection highlight on this line
        if (has_sel && i >= sel_s.line && i <= sel_e.line) {
            int col_start = (i == sel_s.line) ? sel_s.column : 0;
            int col_end = (i == sel_e.line) ? sel_e.column : line_len;
            float sx = text_x + measure_text_width_n(line_text ? line_text : "", col_start, g_font_small);
            float ex = text_x + measure_text_width_n(line_text ? line_text : "", col_end, g_font_small);
            if (col_end == line_len && i != sel_e.line) ex += 6; // extend past line end for multi-line
            draw_rect_v4(sx, ly, ex - sx, line_h, s.selection_color);
        }

        // Line text
        if (line_text && line_text[0]) {
            draw_text_vc(line_text, text_x, ly, line_h, s.text_color, g_font_small);
        }

        // Cursor on this line
        auto cpos = editbox->get_cursor_position();
        if (editbox->has_focus() && i == cpos.line && fmodf(g_time, 1.0f) < 0.5f) {
            float cx = text_x + measure_text_width_n(line_text ? line_text : "", cpos.column, g_font_small);
            draw_rect(cx, ly + 2, 1, line_h - 4, s.text_color.x, s.text_color.y, s.text_color.z);
        }
    }

    // Focus border
    if (editbox->has_focus()) {
        draw_rect_outline(bx - 1, by - 1, bw + 2, bh + 2, 0.0f, 0.48f, 0.8f);
    }

    draw_rect_outline(bx, by, bw, bh, s.border_color.x, s.border_color.y, s.border_color.z);
}

static void render_image(IGuiImage* image) {
    auto b = image->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    auto tint = image->get_tint();

    draw_rect(bx, by, bw, bh, 0.2f * tint.x, 0.2f * tint.y, 0.2f * tint.z, tint.w);
    draw_rect(bx, by + bh/2 - 1, bw, 2, tint.x * 0.5f, tint.y * 0.5f, tint.z * 0.5f, 0.5f);
    draw_rect(bx + bw/2 - 1, by, 2, bh, tint.x * 0.5f, tint.y * 0.5f, tint.z * 0.5f, 0.5f);
    draw_rect_outline(bx, by, bw, bh, 0.3f, 0.3f, 0.3f);
}

// Helper: get dialog button labels and results based on DialogButtons enum
static int get_dialog_button_info(DialogButtons buttons, const char* labels[], DialogResult results[], int max) {
    switch (buttons) {
        case DialogButtons::OK:
            labels[0] = "OK"; results[0] = DialogResult::OK; return 1;
        case DialogButtons::OKCancel:
            labels[0] = "OK"; results[0] = DialogResult::OK;
            labels[1] = "Cancel"; results[1] = DialogResult::Cancel; return 2;
        case DialogButtons::YesNo:
            labels[0] = "Yes"; results[0] = DialogResult::Yes;
            labels[1] = "No"; results[1] = DialogResult::No; return 2;
        case DialogButtons::YesNoCancel:
            labels[0] = "Yes"; results[0] = DialogResult::Yes;
            labels[1] = "No"; results[1] = DialogResult::No;
            labels[2] = "Cancel"; results[2] = DialogResult::Cancel; return 3;
        case DialogButtons::RetryCancel:
            labels[0] = "Retry"; results[0] = DialogResult::Retry;
            labels[1] = "Cancel"; results[1] = DialogResult::Cancel; return 2;
        case DialogButtons::AbortRetryIgnore:
            labels[0] = "Abort"; results[0] = DialogResult::Abort;
            labels[1] = "Retry"; results[1] = DialogResult::Retry;
            labels[2] = "Cancel"; results[2] = DialogResult::Cancel; return 3;
        default: return 0;
    }
}

// Dialog button rects (for hit testing and rendering)
static int get_dialog_button_rects(IGuiDialog* dialog, Box* out_rects, int max) {
    auto b = dialog->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);
    const auto& s = dialog->get_dialog_style();

    int count = dialog->get_button_count();
    if (count <= 0 || count > max) return 0;

    float btn_w = 75, btn_h = 26, btn_gap = 8;
    float total_w = count * btn_w + (count - 1) * btn_gap;
    float start_x = bx + bw - total_w - s.padding;
    float btn_y = by + bh - s.button_area_height + (s.button_area_height - btn_h) / 2.0f;

    for (int i = 0; i < count; i++) {
        out_rects[i] = make_box(start_x + i * (btn_w + btn_gap), btn_y, btn_w, btn_h);
    }
    return count;
}

// Close button rect (top-right X)
static Box get_dialog_close_button_rect(IGuiDialog* dialog) {
    auto b = dialog->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b);
    float tbh = dialog->get_dialog_style().title_bar_height;
    float sz = 16;
    return make_box(bx + bw - sz - 8, by + (tbh - sz) / 2.0f, sz, sz);
}

static void render_dialog(IGuiDialog* dialog, const Vec2& mouse_pos) {
    if (!dialog->is_open()) return;

    DialogRenderInfo dri;
    dialog->get_dialog_render_info(&dri);

    auto b = dri.bounds;
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    if (dri.is_modal) {
        draw_rect(0, 0, 4096, 4096,
                  dri.style.overlay_color.x, dri.style.overlay_color.y,
                  dri.style.overlay_color.z, dri.style.overlay_color.w);
    }

    // Shadow
    draw_rect(bx + dri.style.shadow_offset, by + dri.style.shadow_offset, bw, bh,
              dri.style.shadow_color.x, dri.style.shadow_color.y,
              dri.style.shadow_color.z, dri.style.shadow_color.w);

    // Background
    draw_rect_v4(bx, by, bw, bh, dri.style.background_color);

    // Title bar
    draw_rect_v4(bx, by, bw, dri.style.title_bar_height, dri.style.title_bar_color);

    // Title text
    const char* title = dialog->get_title();
    if (title && title[0]) {
        draw_text_vc(title, bx + 12, by, dri.style.title_bar_height, dri.style.title_text_color);
    }

    // Close button (X)
    if (dri.show_close_button) {
        Box cb = get_dialog_close_button_rect(dialog);
        float cx = x(box_min(cb)), cy = y(box_min(cb));
        float csz = box_width(cb);
        bool hovered = box_contains(cb, mouse_pos);
        if (hovered) {
            draw_rect(cx, cy, csz, csz, 0.8f, 0.2f, 0.2f, 0.7f);
        }
        Vec4 xc = hovered ? Vec4(1,1,1,1) : dri.style.title_text_color;
        // Draw X with two crossing lines (approximate with small rects)
        for (int i = 0; i < (int)csz - 4; i++) {
            float t = (float)i / (csz - 5);
            draw_rect(cx + 2 + i, cy + 2 + i, 1, 1, xc.x, xc.y, xc.z, xc.w);
            draw_rect(cx + csz - 3 - i, cy + 2 + i, 1, 1, xc.x, xc.y, xc.z, xc.w);
        }
    }

    // Button area separator line
    float btn_area_y = by + bh - dri.style.button_area_height;
    draw_rect(bx, btn_area_y, bw, 1, dri.style.border_color.x, dri.style.border_color.y, dri.style.border_color.z);

    // Buttons
    const char* labels[8]; DialogResult results[8];
    int btn_count = get_dialog_button_info(dialog->get_buttons(), labels, results, 8);
    Box btn_rects[8];
    get_dialog_button_rects(dialog, btn_rects, 8);

    for (int i = 0; i < btn_count; i++) {
        float rx = x(box_min(btn_rects[i])), ry = y(box_min(btn_rects[i]));
        float rw = box_width(btn_rects[i]), rh = box_height(btn_rects[i]);
        bool hovered = box_contains(btn_rects[i], mouse_pos);

        Vec4 btn_bg = hovered ? color_rgba8(63, 63, 70) : color_rgba8(51, 51, 55);
        draw_rect_v4(rx, ry, rw, rh, btn_bg);
        draw_rect_outline(rx, ry, rw, rh, dri.style.border_color.x, dri.style.border_color.y, dri.style.border_color.z);
        if (labels[i]) {
            draw_text_center(labels[i], rx, ry, rw, rh, dri.style.title_text_color, g_font_small);
        }
    }

    // Border
    draw_rect_outline(bx, by, bw, bh, dri.style.border_color.x, dri.style.border_color.y, dri.style.border_color.z);
}

// ============================================================================
// Generic Widget Renderer (fallback for unhandled types)
// ============================================================================

static void render_generic_widget(IGuiWidget* w) {
    WidgetRenderInfo ri;
    w->get_render_info(nullptr, &ri);
    for (const auto& entry : ri.textures) {
        if (entry.source_type == TextureSourceType::Generated) {
            draw_box(entry.dest_rect, entry.solid_color);
        }
    }
}

// ============================================================================
// Main Render Dispatch
// ============================================================================

static void render_widget(IGuiWidget* w) {
    if (!w->is_visible()) return;

    switch (w->get_type()) {
        case WidgetType::Button:
            render_button(static_cast<IGuiButton*>(w));
            break;
        case WidgetType::Label:
            render_label(static_cast<IGuiLabel*>(w));
            break;
        case WidgetType::TextInput:
            render_textinput(static_cast<IGuiTextInput*>(w));
            break;
        case WidgetType::Slider:
            render_slider(static_cast<IGuiSlider*>(w));
            break;
        case WidgetType::ProgressBar:
            render_progress_bar(static_cast<IGuiProgressBar*>(w));
            break;
        case WidgetType::ListBox:
            render_listbox(static_cast<IGuiListBox*>(w));
            break;
        case WidgetType::ComboBox:
            render_combobox(static_cast<IGuiComboBox*>(w));
            break;
        case WidgetType::TreeView:
            render_treeview(static_cast<IGuiTreeView*>(w));
            break;
        case WidgetType::TabControl:
            render_tabcontrol(static_cast<IGuiTabControl*>(w));
            break;
        case WidgetType::Image:
            render_image(static_cast<IGuiImage*>(w));
            break;
        case WidgetType::ScrollArea:
            render_generic_widget(w);
            break;
        default:
            render_generic_widget(w);
            break;
    }
}

// ============================================================================
// Widget Setup
// ============================================================================

struct GuiWidgets {
    IGuiMenuBar* menubar;
    IGuiToolbar* toolbar;
    IGuiStatusBar* statusbar;
    IGuiButton* btn_normal;
    IGuiButton* btn_toggle;
    IGuiButton* btn_check;
    IGuiButton* radio1;
    IGuiButton* radio2;
    IGuiLabel* label;
    IGuiTextInput* text_input;
    IGuiEditBox* editbox;
    IGuiSlider* slider_h;
    IGuiSlider* slider_v;
    IGuiProgressBar* prog_det;
    IGuiProgressBar* prog_ind;
    IGuiListBox* listbox;
    IGuiComboBox* combo;
    IGuiTreeView* tree;
    IGuiTabControl* tabs;
    IGuiScrollBar* scrollbar;
    IGuiPropertyGrid* propgrid;
    IGuiColorPicker* picker;
    IGuiImage* image;
    IGuiSplitPanel* split;
    IGuiDockPanel* dock;
    IGuiDialog* dialog;
    IGuiMenu* editbox_context_menu;
    IGuiMenu* tree_context_menu;
};

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

    IGuiMenu* view_menu = ctx->create_menu();
    view_menu->add_checkbox_item("Sidebar", true);
    view_menu->add_checkbox_item("Status Bar", true);

    w.menubar->add_menu("File", file_menu);
    w.menubar->add_menu("Edit", edit_menu);
    w.menubar->add_menu("View", view_menu);
    root->add_child(w.menubar);

    // ---- Toolbar ----
    w.toolbar = ctx->create_toolbar(ToolbarOrientation::Horizontal);
    w.toolbar->set_name("toolbar");
    w.toolbar->set_bounds(make_box(0, 26, 1280, 32));
    w.toolbar->add_button("new", "New");
    w.toolbar->add_button("open", "Open");
    w.toolbar->add_button("save", "Save");
    w.toolbar->add_separator();
    w.toolbar->add_toggle_button("bold", "Bold", false);
    w.toolbar->add_toggle_button("italic", "Italic", false);
    w.toolbar->add_separator();
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
    root->add_child(w.statusbar);

    float col1_x = 15, col2_x = 320, col3_x = 640;
    float top_y = 68;

    // ---- Column 1: Buttons, Label, TextInput, Slider, Progress ----

    w.btn_normal = ctx->create_button(ButtonType::Normal);
    w.btn_normal->set_name("btn_normal");
    w.btn_normal->set_text("Normal Button");
    w.btn_normal->set_bounds(make_box(col1_x, top_y, 130, 28));
    root->add_child(w.btn_normal);

    w.btn_toggle = ctx->create_button(ButtonType::Toggle);
    w.btn_toggle->set_name("btn_toggle");
    w.btn_toggle->set_text("Toggle");
    w.btn_toggle->set_bounds(make_box(col1_x + 140, top_y, 80, 28));
    root->add_child(w.btn_toggle);

    w.btn_check = ctx->create_button(ButtonType::Checkbox);
    w.btn_check->set_name("btn_check");
    w.btn_check->set_text("Check Me");
    w.btn_check->set_checked(true);
    w.btn_check->set_bounds(make_box(col1_x, top_y + 36, 120, 24));
    root->add_child(w.btn_check);

    w.radio1 = ctx->create_button(ButtonType::Radio);
    w.radio1->set_name("radio1");
    w.radio1->set_text("Option A");
    w.radio1->set_radio_group(1);
    w.radio1->set_checked(true);
    w.radio1->set_bounds(make_box(col1_x + 130, top_y + 36, 90, 24));
    root->add_child(w.radio1);

    w.radio2 = ctx->create_button(ButtonType::Radio);
    w.radio2->set_name("radio2");
    w.radio2->set_text("Option B");
    w.radio2->set_radio_group(1);
    w.radio2->set_bounds(make_box(col1_x + 225, top_y + 36, 90, 24));
    root->add_child(w.radio2);

    w.label = ctx->create_label("Hello, GUI Widget System!");
    w.label->set_name("label");
    w.label->set_bounds(make_box(col1_x, top_y + 72, 280, 20));
    root->add_child(w.label);

    w.text_input = ctx->create_text_input("Type here...");
    w.text_input->set_name("text_input");
    w.text_input->set_bounds(make_box(col1_x, top_y + 100, 280, 24));
    root->add_child(w.text_input);

    w.slider_h = ctx->create_slider(SliderOrientation::Horizontal);
    w.slider_h->set_name("slider_h");
    w.slider_h->set_range(0, 100);
    w.slider_h->set_value(65);
    w.slider_h->set_bounds(make_box(col1_x, top_y + 136, 280, 20));
    root->add_child(w.slider_h);

    w.slider_v = ctx->create_slider(SliderOrientation::Vertical);
    w.slider_v->set_name("slider_v");
    w.slider_v->set_range(0, 1);
    w.slider_v->set_value(0.7f);
    w.slider_v->set_bounds(make_box(col1_x + 285, top_y + 68, 16, 120));
    root->add_child(w.slider_v);

    w.prog_det = ctx->create_progress_bar(ProgressBarMode::Determinate);
    w.prog_det->set_name("prog_det");
    w.prog_det->set_value(0.72f);
    w.prog_det->set_bounds(make_box(col1_x, top_y + 168, 280, 16));
    root->add_child(w.prog_det);

    w.prog_ind = ctx->create_progress_bar(ProgressBarMode::Indeterminate);
    w.prog_ind->set_name("prog_ind");
    w.prog_ind->set_bounds(make_box(col1_x, top_y + 192, 280, 10));
    root->add_child(w.prog_ind);

    // ---- Column 1 lower: EditBox ----
    w.editbox = ctx->create_editbox();
    w.editbox->set_name("editbox");
    w.editbox->set_bounds(make_box(col1_x, top_y + 216, 290, 130));
    w.editbox->set_text("Line 1: Hello World\nLine 2: Multi-line editor\nLine 3: With line numbers\nLine 4: And syntax highlighting\nLine 5: Scrollable content");
    w.editbox->set_line_numbers_visible(true);
    root->add_child(w.editbox);

    // ---- Column 1 bottom: Image ----
    w.image = ctx->create_image("textures/logo.png");
    w.image->set_name("image");
    w.image->set_bounds(make_box(col1_x, top_y + 356, 80, 80));
    w.image->set_tint(Vec4(0.4f, 0.7f, 1.0f, 0.9f));
    root->add_child(w.image);

    // ---- Column 2: ListBox, ComboBox, TreeView ----

    w.listbox = ctx->create_list_box();
    w.listbox->set_name("listbox");
    w.listbox->set_bounds(make_box(col2_x, top_y, 160, 140));
    w.listbox->add_item("Apple");
    w.listbox->add_item("Banana");
    w.listbox->add_item("Cherry");
    w.listbox->add_item("Date");
    w.listbox->add_item("Elderberry");
    w.listbox->set_selected_item(1);
    root->add_child(w.listbox);

    w.combo = ctx->create_combo_box();
    w.combo->set_name("combo");
    w.combo->set_placeholder("Select...");
    w.combo->set_bounds(make_box(col2_x + 170, top_y, 150, 26));
    w.combo->add_item("Red");
    w.combo->add_item("Green");
    w.combo->add_item("Blue");
    w.combo->set_selected_item(0);
    root->add_child(w.combo);

    w.tree = ctx->create_tree_view();
    w.tree->set_name("tree");
    w.tree->set_bounds(make_box(col2_x, top_y + 150, 200, 180));
    int rn = w.tree->add_node(-1, "Project");
    int sn = w.tree->add_node(rn, "src");
    w.tree->add_node(sn, "main.cpp");
    w.tree->add_node(sn, "utils.cpp");
    int in = w.tree->add_node(rn, "include");
    w.tree->add_node(in, "app.hpp");
    w.tree->add_node(rn, "CMakeLists.txt");
    w.tree->set_node_expanded(rn, true);
    w.tree->set_node_expanded(sn, true);
    root->add_child(w.tree);

    // ---- Column 2 lower: PropertyGrid ----
    w.propgrid = ctx->create_property_grid();
    w.propgrid->set_name("propgrid");
    w.propgrid->set_bounds(make_box(col2_x, top_y + 340, 300, 180));
    int p1 = w.propgrid->add_property("Transform", "Position X", PropertyType::Float);
    w.propgrid->set_float_value(p1, 100.0f);
    int p2 = w.propgrid->add_property("Transform", "Position Y", PropertyType::Float);
    w.propgrid->set_float_value(p2, 200.0f);
    int p3 = w.propgrid->add_property("Transform", "Rotation", PropertyType::Range);
    w.propgrid->set_range_limits(p3, 0, 360);
    w.propgrid->set_float_value(p3, 45.0f);
    int p4 = w.propgrid->add_property("Appearance", "Visible", PropertyType::Bool);
    w.propgrid->set_bool_value(p4, true);
    int p5 = w.propgrid->add_property("Appearance", "Color", PropertyType::Color);
    w.propgrid->set_vec4_value(p5, Vec4(1.0f, 0.5f, 0.0f, 1.0f));
    root->add_child(w.propgrid);

    // ---- Column 3: TabControl ----
    w.tabs = ctx->create_tab_control(TabPosition::Top);
    w.tabs->set_name("tabs");
    w.tabs->set_bounds(make_box(col3_x, top_y, 300, 180));
    w.tabs->set_fixed_tab_width(80.0f);
    w.tabs->add_tab("General");
    w.tabs->add_tab("Advanced");
    w.tabs->add_tab("About");
    root->add_child(w.tabs);

    // ---- Column 3: ScrollBar ----
    w.scrollbar = ctx->create_scroll_bar(ScrollBarOrientation::Vertical);
    w.scrollbar->set_name("scrollbar");
    w.scrollbar->set_bounds(make_box(col3_x + 310, top_y, 14, 180));
    w.scrollbar->set_range(0, 100);
    w.scrollbar->set_value(30);
    w.scrollbar->set_page_size(25);
    root->add_child(w.scrollbar);

    // ---- Column 3 lower: ColorPicker ----
    w.picker = ctx->create_color_picker(ColorPickerMode::HSVSquare);
    w.picker->set_name("picker");
    w.picker->set_bounds(make_box(col3_x, top_y + 190, 220, 280));
    w.picker->set_color(Vec4(1.0f, 0.5f, 0.2f, 1.0f));
    w.picker->set_alpha_enabled(true);
    root->add_child(w.picker);

    // ---- Column 3: SplitPanel ----
    w.split = ctx->create_split_panel(SplitOrientation::Horizontal);
    w.split->set_name("split");
    w.split->set_bounds(make_box(col3_x, top_y + 480, 300, 100));
    w.split->set_split_ratio(0.4f);
    IGuiLabel* split_l = ctx->create_label("Left");
    IGuiLabel* split_r = ctx->create_label("Right");
    w.split->set_first_panel(split_l);
    w.split->set_second_panel(split_r);
    root->add_child(w.split);

    // ---- EditBox Context Menu ----
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

    // ---- TreeView Context Menu ----
    w.tree_context_menu = ctx->create_menu();
    w.tree_context_menu->set_name("tree_context_menu");
    w.tree_context_menu->set_bounds(make_box(0, 0, 180, 200));
    w.tree_context_menu->add_item("Expand All");
    w.tree_context_menu->add_item("Collapse All");
    w.tree_context_menu->add_separator();
    w.tree_context_menu->add_item("Add Node");
    w.tree_context_menu->add_item("Remove Node");
    w.tree_context_menu->add_separator();
    w.tree_context_menu->add_item("Rename", nullptr, "F2");

    // ---- Dialog (shown on demand) ----
    w.dialog = ctx->create_dialog("Save Changes?", DialogButtons::YesNoCancel);
    w.dialog->set_name("dialog");
    w.dialog->set_modal(true);
    w.dialog->set_draggable(true);
    w.dialog->set_bounds(make_box(400, 240, 300, 160));
    w.dialog->show();

    return w;
}

// ============================================================================
// Keyboard Handler - forwards key/char events to focused GUI widget
// ============================================================================

static IGuiContext* g_gui_ctx = nullptr;

class GuiKeyboardHandler : public input::IKeyboardHandler {
public:
    const char* get_handler_id() const override { return "gui"; }
    int get_priority() const override { return 100; }

    bool on_key(const KeyEvent& event) override {
        if (!g_gui_ctx) return false;
        IGuiWidget* focused = g_gui_ctx->get_focused_widget();
        if (!focused) return false;

        bool pressed = (event.type == EventType::KeyDown);
        int mods = static_cast<int>(event.modifiers);
        return focused->handle_key(static_cast<int>(event.key), pressed, mods);
    }

    bool on_char(const CharEvent& event) override {
        if (!g_gui_ctx) return false;
        IGuiWidget* focused = g_gui_ctx->get_focused_widget();
        if (!focused) return false;

        // Convert codepoint to UTF-8
        char buf[8] = {};
        uint32_t cp = event.codepoint;
        if (cp < 0x80) { buf[0] = (char)cp; }
        else if (cp < 0x800) { buf[0] = (char)(0xC0|(cp>>6)); buf[1] = (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { buf[0] = (char)(0xE0|(cp>>12)); buf[1] = (char)(0x80|((cp>>6)&0x3F)); buf[2] = (char)(0x80|(cp&0x3F)); }
        else { buf[0] = (char)(0xF0|(cp>>18)); buf[1] = (char)(0x80|((cp>>12)&0x3F)); buf[2] = (char)(0x80|((cp>>6)&0x3F)); buf[3] = (char)(0x80|(cp&0x3F)); }

        if (buf[0] && cp >= 32) // skip control chars
            return focused->handle_text_input(buf);
        return false;
    }
};

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Visual GUI Widget Showcase ===\n");

    // Create OpenGL window
    Config config;
    config.windows[0].title = "GUI Widget Showcase";
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

    g_gui_ctx = ctx;

    // Register keyboard handler for GUI text input
    GuiKeyboardHandler gui_kb_handler;
    auto* kb_dispatcher = win->get_keyboard_dispatcher();
    if (kb_dispatcher) {
        kb_dispatcher->add_handler(&gui_kb_handler);
    }

    gui::Viewport vp;
    vp.id = 0;
    vp.bounds = make_box(0, 0, 1280, 720);
    vp.scale = 1.0f;
    ctx->add_viewport(vp);

    IGuiWidget* root = ctx->get_root();
    root->set_bounds(make_box(0, 0, 1280, 720));

    // Setup all widgets
    GuiWidgets widgets = setup_widgets(ctx);
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

    bool prev_left_down = false;
    bool prev_right_down = false;
    bool editbox_dragging = false;
    IGuiMenu* active_context_menu = nullptr;
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

        // Update viewport on resize
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        ctx->update_viewport(vp);

        // Update full-width widgets to track window size
        root->set_bounds(make_box(0, 0, (float)sw, (float)sh));
        widgets.menubar->set_bounds(make_box(0, 0, (float)sw, 26));
        widgets.toolbar->set_bounds(make_box(0, 26, (float)sw, 32));
        widgets.statusbar->set_bounds(make_box(0, (float)sh - 24, (float)sw, 24));

        // Forward mouse input
        int mx, my;
        win->get_mouse_position(&mx, &my);
        Vec2 mpos = vec2((float)mx, (float)my);

        bool dialog_modal = widgets.dialog->is_open() && widgets.dialog->is_modal();

        bool left_down = win->is_mouse_button_down(window::MouseButton::Left);
        if (left_down && !prev_left_down) {
            if (dialog_modal) {
                // When modal dialog is open, only handle dialog interactions
                Box close_rect = get_dialog_close_button_rect(widgets.dialog);
                if (box_contains(close_rect, mpos) && widgets.dialog->has_close_button()) {
                    widgets.dialog->hide();
                } else {
                    // Check dialog buttons
                    const char* labels[8]; DialogResult results[8];
                    int btn_count = get_dialog_button_info(widgets.dialog->get_buttons(), labels, results, 8);
                    Box btn_rects[8];
                    get_dialog_button_rects(widgets.dialog, btn_rects, 8);
                    for (int i = 0; i < btn_count; i++) {
                        if (box_contains(btn_rects[i], mpos)) {
                            widgets.dialog->hide();
                            break;
                        }
                    }
                }
            } else {
                // Normal input dispatch
                root->handle_mouse_move(mpos);
                // Close context menu if clicking outside it
                if (active_context_menu && active_context_menu->is_open()) {
                    // Check if clicked on a menu item
                    auto cmb = active_context_menu->get_bounds();
                    float cmx = x(box_min(cmb)), cmy = y(box_min(cmb));
                    const auto& cms = active_context_menu->get_menu_style();
                    MenuItemRenderInfo cm_items[16];
                    int cm_count = active_context_menu->get_visible_menu_items(cm_items, 16);
                    float cm_iy = cmy;
                    int clicked_item = -1;
                    for (int i = 0; i < cm_count; i++) {
                        float ih = (cm_items[i].type == MenuItemType::Separator) ? cms.separator_height : cms.item_height;
                        if (cm_items[i].type != MenuItemType::Separator) {
                            Box item_box = make_box(cmx, cm_iy, cms.min_width, ih);
                            if (box_contains(item_box, mpos) && cm_items[i].enabled) {
                                clicked_item = cm_items[i].item_id;
                                // Toggle checkbox items
                                if (cm_items[i].type == MenuItemType::Checkbox) {
                                    active_context_menu->set_item_checked(clicked_item, !cm_items[i].checked);
                                }
                                break;
                            }
                        }
                        cm_iy += ih;
                    }
                    active_context_menu->hide();
                    active_context_menu = nullptr;
                    if (clicked_item >= 0) goto input_done;
                }
                // Close combobox dropdown if clicking outside it
                if (widgets.combo->is_open() && !widgets.combo->hit_test(mpos)) {
                    widgets.combo->close();
                }
                // Handle menu bar clicks (including open dropdown) before root dispatch
                bool menu_handled = widgets.menubar->handle_mouse_button(gui::MouseButton::Left, true, mpos);
                if (!menu_handled)
                    root->handle_mouse_button(gui::MouseButton::Left, true, mpos);
                // Set focus on clicked widget
                IGuiWidget* hit = root->find_widget_at(mpos);
                if (hit && hit->is_focusable()) {
                    IGuiWidget* prev_focus = ctx->get_focused_widget();
                    if (prev_focus && prev_focus != hit) prev_focus->set_focus(false);
                    ctx->set_focused_widget(hit);
                    hit->set_focus(true);
                } else if (!hit || !hit->is_focusable()) {
                    IGuiWidget* prev_focus = ctx->get_focused_widget();
                    if (prev_focus) { prev_focus->set_focus(false); ctx->clear_focus(); }
                }
            }
        }
        if (!left_down && prev_left_down && !dialog_modal)
            root->handle_mouse_button(gui::MouseButton::Left, false, mpos);
        if (!dialog_modal)
            root->handle_mouse_move(mpos);

        // Editbox mouse click-to-cursor and drag-select
        if (!dialog_modal) {
            if (left_down && !prev_left_down && widgets.editbox->hit_test(mpos)) {
                // Click: set cursor and start selection
                TextPosition pos = editbox_position_from_point(widgets.editbox, mpos);
                widgets.editbox->set_cursor_position(pos);
                widgets.editbox->clear_selection();
                TextRange sel; sel.start = pos; sel.end = pos;
                widgets.editbox->set_selection(sel);
                editbox_dragging = true;
            }
            if (left_down && editbox_dragging) {
                // Drag: extend selection
                TextPosition pos = editbox_position_from_point(widgets.editbox, mpos);
                TextRange sel = widgets.editbox->get_selection();
                sel.end = pos;
                widgets.editbox->set_selection(sel);
                widgets.editbox->set_cursor_position(pos);
            }
            if (!left_down) {
                editbox_dragging = false;
            }
        }

        input_done:
        prev_left_down = left_down;

        // Right-click: show context menu on editbox or treeview
        {
            bool right_down = win->is_mouse_button_down(window::MouseButton::Right);
            if (right_down && !prev_right_down && !dialog_modal) {
                if (widgets.editbox->hit_test(mpos)) {
                    active_context_menu = widgets.editbox_context_menu;
                    active_context_menu->show_at(mpos);
                } else if (widgets.tree->hit_test(mpos)) {
                    active_context_menu = widgets.tree_context_menu;
                    active_context_menu->show_at(mpos);
                }
            }
            prev_right_down = right_down;
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

        // Set mouse pos for menu hover rendering
        g_mouse_pos_for_menu = mpos;

        // Render all widgets by type
        render_menubar(widgets.menubar);
        render_toolbar(widgets.toolbar);
        render_statusbar(widgets.statusbar);


        // Column 1
        render_button(widgets.btn_normal);
        render_button(widgets.btn_toggle);
        render_button(widgets.btn_check);
        render_button(widgets.radio1);
        render_button(widgets.radio2);
        render_label(widgets.label);
        render_textinput(widgets.text_input);
        render_slider(widgets.slider_h);
        render_slider(widgets.slider_v);
        render_progress_bar(widgets.prog_det);
        render_progress_bar(widgets.prog_ind);
        render_editbox(widgets.editbox);
        render_image(widgets.image);

        // Column 2
        render_listbox(widgets.listbox);
        render_treeview(widgets.tree);
        render_propertygrid(widgets.propgrid);

        // Column 3
        render_tabcontrol(widgets.tabs);
        render_scrollbar(widgets.scrollbar);
        render_colorpicker(widgets.picker);
        render_splitpanel(widgets.split);

        // Overlays (dropdown, menu, context menu, dialog)
        render_combobox(widgets.combo);
        render_menu_overlays(widgets.menubar);
        if (active_context_menu) render_menu_dropdown(active_context_menu);
        render_dialog(widgets.dialog, mpos);

        gfx->present();
    }

    // Cleanup
    if (g_sv_texture) { glDeleteTextures(1, &g_sv_texture); g_sv_texture = 0; }
    if (g_hue_texture) { glDeleteTextures(1, &g_hue_texture); g_hue_texture = 0; }
    cleanup_text_cache();
    renderer.destroy();
    g_renderer = nullptr;
    destroy_gui_context(ctx);
    font::destroy_font_renderer(g_font_renderer);
    g_font_renderer = nullptr;
    font::destroy_font_library(font_library);
    win->destroy();

    printf("Window closed.\n");
    return 0;
}
