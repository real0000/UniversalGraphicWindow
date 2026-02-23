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
static int g_window_h = 720; // for glScissor Y-flip

// Scissor helper: enable scissor rect (top-left origin, flipped for OpenGL)
static void push_scissor(float sx, float sy, float sw, float sh) {
    glEnable(GL_SCISSOR_TEST);
    int gl_y = g_window_h - (int)(sy + sh);
    glScissor((int)sx, gl_y, (int)sw, (int)sh);
}
static void pop_scissor() {
    glDisable(GL_SCISSOR_TEST);
}

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

    bool is_radio = btn->get_button_type() == ButtonType::Radio;
    bool is_check = btn->get_button_type() == ButtonType::Checkbox;

    if (is_radio || is_check) {
        // Radio/Checkbox: no rectangular background — only indicator + text

        // Focus indicator (dotted-style outline around whole widget)
        if (btn->has_focus()) {
            draw_rect_outline(px, py, pw, ph,
                              s.focus_border_color.x, s.focus_border_color.y,
                              s.focus_border_color.z);
        }

        if (is_check) {
            float bx = px + 4, by = py + ph/2 - 6;
            // Checkbox box background
            draw_rect(bx, by, 12, 12, s.background_color.x, s.background_color.y, s.background_color.z);
            draw_rect_outline(bx, by, 12, 12, s.border_color.x, s.border_color.y, s.border_color.z);
            if (btn->is_checked()) {
                draw_rect(bx + 3, by + 3, 6, 6, s.checked_color.x, s.checked_color.y, s.checked_color.z);
            }
        } else {
            // Radio circle
            float rcx = px + 10, rcy = py + ph/2;
            draw_circle(rcx, rcy, 6, s.border_color.x, s.border_color.y, s.border_color.z);
            draw_circle(rcx, rcy, 5, s.background_color.x, s.background_color.y, s.background_color.z);
            if (btn->is_checked()) {
                draw_circle(rcx, rcy, 3, s.checked_color.x, s.checked_color.y, s.checked_color.z);
            }
        }
    } else {
        // Normal/Toggle button: full rectangular background and border
        draw_rect_v4(px, py, pw, ph, bg_color);
        draw_rect_outline(px, py, pw, ph, s.border_color.x, s.border_color.y, s.border_color.z);

        if (btn->has_focus()) {
            draw_rect_outline(px - 1, py - 1, pw + 2, ph + 2,
                              s.focus_border_color.x, s.focus_border_color.y,
                              s.focus_border_color.z);
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

// Scrollbar constants
static const float SB_WIDTH = 10.0f;    // Scrollbar track width (wider for easier clicking)
static const float SB_MARGIN = 1.0f;    // Margin from widget edge

// Scrollbar hit-test: returns true if pos is in the scrollbar region of a widget
static bool scrollbar_hit_test(const Box& widget_bounds, float content_h, const Vec2& pos) {
    float bx = x(box_min(widget_bounds)), by = y(box_min(widget_bounds));
    float bw = box_width(widget_bounds), bh = box_height(widget_bounds);
    if (content_h <= bh) return false;  // No scrollbar needed
    float sb_x = bx + bw - SB_WIDTH - SB_MARGIN;
    return x(pos) >= sb_x && x(pos) <= bx + bw
        && y(pos) >= by && y(pos) <= by + bh;
}

// Compute scroll offset from mouse Y position on the scrollbar track
static float scrollbar_offset_from_mouse(const Box& widget_bounds, float content_h, float mouse_y) {
    float by = y(box_min(widget_bounds));
    float bh = box_height(widget_bounds);
    float max_scroll = content_h - bh;
    if (max_scroll <= 0) return 0;
    float thumb_ratio = bh / content_h;
    float thumb_h = bh * thumb_ratio;
    if (thumb_h < 16) thumb_h = 16;
    float track_range = bh - thumb_h;
    if (track_range <= 0) return 0;
    float rel_y = mouse_y - by - thumb_h * 0.5f;  // Center thumb on click
    float ratio = rel_y / track_range;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    return ratio * max_scroll;
}

// Scrollbar drag state
enum class ScrollDragTarget { None, TreeView, PropGrid, ListBox, EditBox, OutputEditBox };
static ScrollDragTarget g_scroll_drag = ScrollDragTarget::None;

// Auto scrollbar: draw scrollbar track+thumb on right edge when content overflows
static void draw_auto_scrollbar(float bx, float by, float bw, float bh,
                                  float scroll_offset, float content_h, float view_h,
                                  bool dragging = false) {
    if (content_h <= view_h) return;
    float sb_w = SB_WIDTH;
    float sb_x = bx + bw - sb_w - SB_MARGIN;
    draw_rect(sb_x, by, sb_w, bh, 0.12f, 0.12f, 0.13f, 0.6f);
    float thumb_ratio = view_h / content_h;
    float thumb_h = bh * thumb_ratio;
    if (thumb_h < 16) thumb_h = 16;
    float track_range = bh - thumb_h;
    float max_scroll = content_h - view_h;
    float pos_ratio = (max_scroll > 0) ? scroll_offset / max_scroll : 0;
    float thumb_y = by + track_range * pos_ratio;
    // Highlight thumb when dragging
    if (dragging)
        draw_rect(sb_x, thumb_y, sb_w, thumb_h, 0.6f, 0.6f, 0.65f, 0.9f);
    else
        draw_rect(sb_x, thumb_y, sb_w, thumb_h, 0.4f, 0.4f, 0.42f, 0.7f);
}

static void render_listbox(IGuiListBox* listbox) {
    auto b = listbox->get_bounds();
    float bx = x(box_min(b)), by = y(box_min(b));
    float bw = box_width(b), bh = box_height(b);

    const auto& s = listbox->get_list_box_style();
    float scroll_y = listbox->get_scroll_offset();
    float content_h = listbox->get_total_content_height();

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    push_scissor(bx, by, bw, bh);

    int count = listbox->get_item_count();
    int sel = listbox->get_selected_item();
    float row_h = s.row_height;
    for (int i = 0; i < count; i++) {
        float ry = by + i * row_h - scroll_y;
        if (ry + row_h < by || ry > by + bh) continue;

        Vec4 row_bg = (i == sel) ? s.selected_background
                    : (i % 2 == 0) ? s.row_background : s.row_alt_background;
        draw_rect_v4(bx, ry, bw, row_h, row_bg);

        Vec4 text_col = (i == sel) ? s.selected_text_color : s.text_color;
        const char* item_text = listbox->get_item_text(i);
        if (item_text && item_text[0]) {
            draw_text_vc(item_text, bx + s.item_padding, ry, row_h, text_col);
        }
    }

    draw_auto_scrollbar(bx, by, bw, bh, scroll_y, content_h, bh,
                        g_scroll_drag == ScrollDragTarget::ListBox);

    pop_scissor();

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
    float scroll_y = tree->get_scroll_offset();
    float content_h = tree->get_total_content_height();

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    // Scissor clip scrollable content
    push_scissor(bx, by, bw, bh);

    TreeNodeRenderItem items[64];
    int count = tree->get_visible_tree_items(items, 64);
    float row_h = s.row_height;

    for (int i = 0; i < count; i++) {
        float ry = by + i * row_h - scroll_y;
        if (ry + row_h < by || ry > by + bh) continue;

        Vec4 row_bg = items[i].selected ? s.selected_background
                    : items[i].hovered ? s.hover_background : s.row_background;
        draw_rect_v4(bx, ry, bw, row_h, row_bg);

        float indent = bx + items[i].depth * s.indent_width + 4;

        // Expand/collapse indicator (arrow shape)
        if (items[i].has_children) {
            float ex = indent, ey = ry + row_h/2 - 3;
            if (items[i].expanded) {
                draw_rect(ex, ey, 6, 2, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
                draw_rect(ex+1, ey+2, 4, 2, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
                draw_rect(ex+2, ey+4, 2, 2, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
            } else {
                draw_rect(ex, ey, 2, 6, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
                draw_rect(ex+2, ey+1, 2, 4, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
                draw_rect(ex+4, ey+2, 2, 2, s.icon_color.x, s.icon_color.y, s.icon_color.z, 0.8f);
            }
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

    // Auto-scrollbar (inside scissor so it clips too)
    draw_auto_scrollbar(bx, by, bw, bh, scroll_y, content_h, bh,
                        g_scroll_drag == ScrollDragTarget::TreeView);

    pop_scissor();

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
    float scroll_y = pg->get_scroll_offset();
    float content_h = pg->get_total_content_height();

    PropertyGridRenderInfo pgri;
    pg->get_property_grid_render_info(&pgri);

    draw_rect_v4(bx, by, bw, bh, s.row_background);

    // Scissor clip scrollable content
    push_scissor(bx, by, bw, bh);

    PropertyRenderItem items[32];
    int count = pg->get_visible_property_items(items, 32);

    for (int i = 0; i < count; i++) {
        float ry = by + i * row_h - scroll_y;
        if (ry + row_h < by || ry > by + bh) continue;

        if (items[i].is_category_header) {
            draw_rect_v4(bx, ry, bw, row_h, s.category_background);
            if (items[i].name && items[i].name[0]) {
                // Expand/collapse indicator for category
                const char* arrow = items[i].expanded ? "v" : ">";
                draw_text_vc(arrow, bx + 2, ry, row_h, s.category_text_color, g_font_small);
                draw_text_vc(items[i].name, bx + 14, ry, row_h, s.category_text_color, g_font_small);
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

    // Auto-scrollbar (inside scissor so it clips too)
    draw_auto_scrollbar(bx, by, bw, bh, scroll_y, content_h, bh,
                        g_scroll_drag == ScrollDragTarget::PropGrid);

    pop_scissor();

    // Focus border (outside scissor so it's not clipped)
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

static void render_editbox(IGuiEditBox* editbox, bool sb_dragging = false) {
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
    int first_vis = editbox->get_first_visible_line();
    float text_x = bx + (editbox->is_line_numbers_visible() ? s.gutter_width + s.padding : s.padding);

    float content_h = line_count * line_h;

    // Get selection range (normalized so sel_s <= sel_e)
    TextRange sel_range = editbox->get_selection();
    TextPosition sel_s = sel_range.start, sel_e = sel_range.end;
    if (sel_s.line > sel_e.line || (sel_s.line == sel_e.line && sel_s.column > sel_e.column))
        std::swap(sel_s, sel_e);
    bool has_sel = editbox->has_selection();

    push_scissor(bx, by, bw, bh);

    int vis_count = (line_h > 0) ? (int)(bh / line_h) + 2 : line_count;
    int end_line = std::min(first_vis + vis_count, line_count);

    for (int i = first_vis; i < end_line; i++) {
        float ly = by + (i - first_vis) * line_h;
        if (ly > by + bh) break;

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
            if (col_end == line_len && i != sel_e.line) ex += 6;
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

    // Scrollbar
    float scroll_offset = first_vis * line_h;
    draw_auto_scrollbar(bx, by, bw, bh, scroll_offset, content_h, bh, sb_dragging);

    pop_scissor();

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

    // Tab "Visuals" content: color picker + image
    w.picker = ctx->create_color_picker(ColorPickerMode::HSVSquare);
    w.picker->set_name("picker");
    w.picker->set_color(Vec4(0.2f, 0.6f, 1.0f, 1.0f));
    w.picker->set_alpha_enabled(true);

    w.image = ctx->create_image("textures/logo.png");
    w.image->set_name("image");
    w.image->set_tint(Vec4(0.4f, 0.7f, 1.0f, 0.9f));

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
    w.scrollbar->set_range(0, 100);
    w.scrollbar->set_value(30);
    w.scrollbar->set_page_size(25);

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

    // Sidebar split
    w.sidebar_split->set_bounds(make_box(0, top, sidebar_w, content_h));
    float sidebar_top_h = content_h * w.sidebar_split->get_split_ratio();
    float sidebar_bot_h = content_h - sidebar_top_h - 4;
    w.tree->set_bounds(make_box(0, top, sidebar_w, sidebar_top_h));
    w.propgrid->set_bounds(make_box(0, top + sidebar_top_h + 4, sidebar_w, sidebar_bot_h));

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
        float cx = tc_x + 8, cy = tc_y + 4;
        w.btn_normal->set_bounds(make_box(cx, cy, 130, 28));
        w.btn_toggle->set_bounds(make_box(cx + 140, cy, 90, 28));
        w.btn_check->set_bounds(make_box(cx, cy + 36, 140, 24));
        w.radio1->set_bounds(make_box(cx + 150, cy + 36, 100, 24));
        w.radio2->set_bounds(make_box(cx + 260, cy + 36, 100, 24));
        w.label->set_bounds(make_box(cx, cy + 68, tc_w - 16, 20));
        w.text_input->set_bounds(make_box(cx, cy + 94, tc_w * 0.6f, 24));
        w.slider_h->set_bounds(make_box(cx, cy + 128, tc_w * 0.7f, 20));
        w.slider_v->set_bounds(make_box(cx + tc_w * 0.7f + 16, cy + 68, 16, 80));
        w.prog_det->set_bounds(make_box(cx, cy + 158, tc_w * 0.7f, 16));
        w.prog_ind->set_bounds(make_box(cx, cy + 182, tc_w * 0.7f, 10));
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
// Scroll input - direct WM_MOUSEWHEEL interception via window subclass
// ============================================================================

#if defined(_WIN32)
static float g_scroll_accum = 0;
static WNDPROC g_orig_wndproc = nullptr;

static LRESULT CALLBACK ScrollSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_MOUSEWHEEL) {
        float delta = (float)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        g_scroll_accum += delta;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wparam, lparam);
}
#endif

// Forward mouse move to active tab's content widgets for hover state.
static void dispatch_tab_content_mouse_move(GuiWidgets& w, const Vec2& pos) {
    int tab = w.tabs->get_active_tab();
    if (tab == 1) {
        IGuiWidget* controls[] = {
            w.btn_normal, w.btn_toggle, w.btn_check,
            w.radio1, w.radio2, w.label, w.text_input,
            w.slider_h, w.slider_v, w.prog_det, w.prog_ind
        };
        for (auto* c : controls) { if (c) c->handle_mouse_move(pos); }
    } else if (tab == 2) {
        IGuiWidget* visuals[] = { w.picker, w.image };
        for (auto* c : visuals) { if (c) c->handle_mouse_move(pos); }
    }
    if (w.scrollbar) w.scrollbar->handle_mouse_move(pos);
    if (w.combo) w.combo->handle_mouse_move(pos);
}

// Dispatch mouse button event to active tab's content widgets.
// Returns true if any widget consumed it.
static bool dispatch_tab_content_mouse(GuiWidgets& w, gui::MouseButton btn, bool pressed, const Vec2& pos) {
    int tab = w.tabs->get_active_tab();
    if (tab == 0) {
        // Editor tab - editbox handled separately
        return false;
    } else if (tab == 1) {
        // Controls tab
        IGuiWidget* controls[] = {
            w.btn_normal, w.btn_toggle, w.btn_check,
            w.radio1, w.radio2, w.label, w.text_input,
            w.slider_h, w.slider_v, w.prog_det, w.prog_ind
        };
        for (auto* c : controls) {
            if (c && c->handle_mouse_button(btn, pressed, pos)) {
                // Enforce radio group mutual exclusion (radios have no shared parent)
                if (!pressed) {
                    IGuiButton* radios[] = { w.radio1, w.radio2 };
                    for (auto* r : radios) {
                        if (r != c && r->is_checked()) {
                            // If the clicked widget is a radio in the same group, uncheck others
                            auto* clicked_btn = (c->get_type() == gui::WidgetType::Button)
                                ? static_cast<IGuiButton*>(c) : nullptr;
                            if (clicked_btn && clicked_btn->get_button_type() == gui::ButtonType::Radio
                                && clicked_btn->get_radio_group() == r->get_radio_group()) {
                                r->set_checked(false);
                            }
                        }
                    }
                }
                return true;
            }
        }
    } else if (tab == 2) {
        // Visuals tab
        IGuiWidget* visuals[] = { w.picker, w.image };
        for (auto* c : visuals) {
            if (c && c->handle_mouse_button(btn, pressed, pos)) return true;
        }
    }
    // Scrollbar and combo are always checked
    if (w.scrollbar && w.scrollbar->handle_mouse_button(btn, pressed, pos)) return true;
    if (w.combo && w.combo->handle_mouse_button(btn, pressed, pos)) return true;
    return false;
}

// Editbox scroll helpers: convert between pixel offset and line index
static float editbox_content_height(IGuiEditBox* eb) {
    const auto& s = eb->get_editbox_style();
    return eb->get_line_count() * s.font_size * s.line_height;
}
static void editbox_set_scroll_from_pixel(IGuiEditBox* eb, float pixel_offset) {
    const auto& s = eb->get_editbox_style();
    float line_h = s.font_size * s.line_height;
    int line = (line_h > 0) ? (int)(pixel_offset / line_h) : 0;
    eb->set_first_visible_line(line);
}

static void forward_scroll_to_widgets(GuiWidgets& widgets, float scroll_dy, const Vec2& mpos) {
    if (scroll_dy == 0) return;
    if (widgets.tree->hit_test(mpos))
        widgets.tree->handle_mouse_scroll(0, scroll_dy);
    else if (widgets.propgrid->hit_test(mpos))
        widgets.propgrid->handle_mouse_scroll(0, scroll_dy);
    else if (widgets.listbox->hit_test(mpos))
        widgets.listbox->handle_mouse_scroll(0, scroll_dy);
    else if (widgets.editbox->hit_test(mpos))
        widgets.editbox->handle_mouse_scroll(0, scroll_dy);
    else if (widgets.output_editbox->hit_test(mpos))
        widgets.output_editbox->handle_mouse_scroll(0, scroll_dy);
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

    // Install window subclass for direct WM_MOUSEWHEEL interception
#if defined(_WIN32)
    g_orig_wndproc = (WNDPROC)SetWindowLongPtrW((HWND)win->native_handle(), GWLP_WNDPROC, (LONG_PTR)ScrollSubclassProc);
#endif

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
    IGuiEditBox* active_drag_editbox = nullptr;
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
        g_window_h = sh;

        // Update viewport on resize
        vp.bounds = make_box(0, 0, (float)sw, (float)sh);
        ctx->update_viewport(vp);

        // Update nested layout for the entire widget tree
        root->set_bounds(make_box(0, 0, (float)sw, (float)sh));
        layout_widgets(widgets, sw, sh);

        // Forward mouse input
        int mx, my;
        win->get_mouse_position(&mx, &my);
        Vec2 mpos = vec2((float)mx, (float)my);

        // Forward accumulated mouse scroll to widgets under cursor
#if defined(_WIN32)
        float scroll_dy = g_scroll_accum;
        g_scroll_accum = 0;
        forward_scroll_to_widgets(widgets, scroll_dy, mpos);
#endif

        bool dialog_modal = widgets.dialog->is_open() && widgets.dialog->is_modal();

        // Scrollbar drag handling (click and drag on scrollbar track/thumb)
        bool left_down = win->is_mouse_button_down(window::MouseButton::Left);
        if (left_down && !dialog_modal) {
            if (g_scroll_drag != ScrollDragTarget::None) {
                // Continue dragging - update scroll offset from mouse position
                if (g_scroll_drag == ScrollDragTarget::TreeView) {
                    float content_h = widgets.tree->get_total_content_height();
                    float offset = scrollbar_offset_from_mouse(widgets.tree->get_bounds(), content_h, y(mpos));
                    widgets.tree->set_scroll_offset(offset);
                } else if (g_scroll_drag == ScrollDragTarget::PropGrid) {
                    float content_h = widgets.propgrid->get_total_content_height();
                    float offset = scrollbar_offset_from_mouse(widgets.propgrid->get_bounds(), content_h, y(mpos));
                    widgets.propgrid->set_scroll_offset(offset);
                } else if (g_scroll_drag == ScrollDragTarget::ListBox) {
                    float content_h = widgets.listbox->get_total_content_height();
                    float offset = scrollbar_offset_from_mouse(widgets.listbox->get_bounds(), content_h, y(mpos));
                    widgets.listbox->set_scroll_offset(offset);
                } else if (g_scroll_drag == ScrollDragTarget::EditBox) {
                    float content_h = editbox_content_height(widgets.editbox);
                    float offset = scrollbar_offset_from_mouse(widgets.editbox->get_bounds(), content_h, y(mpos));
                    editbox_set_scroll_from_pixel(widgets.editbox, offset);
                } else if (g_scroll_drag == ScrollDragTarget::OutputEditBox) {
                    float content_h = editbox_content_height(widgets.output_editbox);
                    float offset = scrollbar_offset_from_mouse(widgets.output_editbox->get_bounds(), content_h, y(mpos));
                    editbox_set_scroll_from_pixel(widgets.output_editbox, offset);
                }
            } else if (!prev_left_down) {
                // Mouse just pressed - check if clicking on a scrollbar
                float tree_content_h = widgets.tree->get_total_content_height();
                float prop_content_h = widgets.propgrid->get_total_content_height();
                float list_content_h = widgets.listbox->get_total_content_height();
                float edit_content_h = editbox_content_height(widgets.editbox);
                float out_content_h = editbox_content_height(widgets.output_editbox);
                if (scrollbar_hit_test(widgets.tree->get_bounds(), tree_content_h, mpos)) {
                    g_scroll_drag = ScrollDragTarget::TreeView;
                    float offset = scrollbar_offset_from_mouse(widgets.tree->get_bounds(), tree_content_h, y(mpos));
                    widgets.tree->set_scroll_offset(offset);
                } else if (scrollbar_hit_test(widgets.propgrid->get_bounds(), prop_content_h, mpos)) {
                    g_scroll_drag = ScrollDragTarget::PropGrid;
                    float offset = scrollbar_offset_from_mouse(widgets.propgrid->get_bounds(), prop_content_h, y(mpos));
                    widgets.propgrid->set_scroll_offset(offset);
                } else if (scrollbar_hit_test(widgets.listbox->get_bounds(), list_content_h, mpos)) {
                    g_scroll_drag = ScrollDragTarget::ListBox;
                    float offset = scrollbar_offset_from_mouse(widgets.listbox->get_bounds(), list_content_h, y(mpos));
                    widgets.listbox->set_scroll_offset(offset);
                } else if (scrollbar_hit_test(widgets.editbox->get_bounds(), edit_content_h, mpos)) {
                    g_scroll_drag = ScrollDragTarget::EditBox;
                    float offset = scrollbar_offset_from_mouse(widgets.editbox->get_bounds(), edit_content_h, y(mpos));
                    editbox_set_scroll_from_pixel(widgets.editbox, offset);
                } else if (scrollbar_hit_test(widgets.output_editbox->get_bounds(), out_content_h, mpos)) {
                    g_scroll_drag = ScrollDragTarget::OutputEditBox;
                    float offset = scrollbar_offset_from_mouse(widgets.output_editbox->get_bounds(), out_content_h, y(mpos));
                    editbox_set_scroll_from_pixel(widgets.output_editbox, offset);
                }
            }
        }
        if (!left_down) {
            g_scroll_drag = ScrollDragTarget::None;
        }

        // Skip normal click handling if dragging a scrollbar
        if (left_down && !prev_left_down && g_scroll_drag != ScrollDragTarget::None)
            goto input_done;

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
                if (!menu_handled) {
                    // Dispatch to active tab content widgets first (they overlap main_split area)
                    bool tab_handled = dispatch_tab_content_mouse(widgets, gui::MouseButton::Left, true, mpos);
                    if (!tab_handled)
                        root->handle_mouse_button(gui::MouseButton::Left, true, mpos);
                }
                // Set focus on clicked widget (check tab content widgets first)
                IGuiWidget* hit = nullptr;
                {
                    int atab = widgets.tabs->get_active_tab();
                    if (atab == 0 && widgets.editbox->hit_test(mpos)) hit = widgets.editbox;
                    else if (atab == 1) {
                        IGuiWidget* ctrls[] = {
                            widgets.btn_normal, widgets.btn_toggle, widgets.btn_check,
                            widgets.radio1, widgets.radio2, widgets.text_input,
                            widgets.slider_h, widgets.slider_v
                        };
                        for (auto* c : ctrls) { if (c && c->hit_test(mpos)) { hit = c; break; } }
                    } else if (atab == 2) {
                        if (widgets.picker->hit_test(mpos)) hit = widgets.picker;
                        else if (widgets.image->hit_test(mpos)) hit = widgets.image;
                    }
                    if (!hit) hit = root->find_widget_at(mpos);
                }
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
        if (!left_down && prev_left_down && !dialog_modal) {
            dispatch_tab_content_mouse(widgets, gui::MouseButton::Left, false, mpos);
            root->handle_mouse_button(gui::MouseButton::Left, false, mpos);
        }
        if (!dialog_modal) {
            dispatch_tab_content_mouse_move(widgets, mpos);
            root->handle_mouse_move(mpos);
        }

        // Editbox mouse click-to-cursor and drag-select (for all editboxes)
        if (!dialog_modal) {
            // Determine which editbox was clicked (if any)
            IGuiEditBox* clicked_editbox = nullptr;
            if (left_down && !prev_left_down) {
                if (g_active_tab == 0 && widgets.editbox->hit_test(mpos))
                    clicked_editbox = widgets.editbox;
                else if (widgets.output_editbox->hit_test(mpos))
                    clicked_editbox = widgets.output_editbox;
            }
            if (clicked_editbox) {
                TextPosition pos = editbox_position_from_point(clicked_editbox, mpos);
                clicked_editbox->set_cursor_position(pos);
                clicked_editbox->clear_selection();
                TextRange sel; sel.start = pos; sel.end = pos;
                clicked_editbox->set_selection(sel);
                editbox_dragging = true;
                active_drag_editbox = clicked_editbox;
            }
            if (left_down && editbox_dragging && active_drag_editbox) {
                TextPosition pos = editbox_position_from_point(active_drag_editbox, mpos);
                TextRange sel = active_drag_editbox->get_selection();
                sel.end = pos;
                active_drag_editbox->set_selection(sel);
                active_drag_editbox->set_cursor_position(pos);
            }
            if (!left_down) {
                editbox_dragging = false;
                active_drag_editbox = nullptr;
            }
        }

        input_done:
        prev_left_down = left_down;

        // Right-click: show context menu on editbox or treeview
        {
            bool right_down = win->is_mouse_button_down(window::MouseButton::Right);
            if (right_down && !prev_right_down && !dialog_modal) {
                if ((g_active_tab == 0 && widgets.editbox->hit_test(mpos))
                    || widgets.output_editbox->hit_test(mpos)) {
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

        // ---- Render nested widget tree ----

        // Chrome (always rendered)
        render_menubar(widgets.menubar);
        render_toolbar(widgets.toolbar);
        render_statusbar(widgets.statusbar);

        // Main split: sidebar | center
        render_splitpanel(widgets.main_split);

        // Sidebar: tree / propgrid (nested inside main_split's left pane)
        render_splitpanel(widgets.sidebar_split);
        render_treeview(widgets.tree);
        render_propertygrid(widgets.propgrid);

        // Center: tabs / bottom (nested inside main_split's right pane)
        render_splitpanel(widgets.center_split);

        // Tab control + scrollbar
        render_tabcontrol(widgets.tabs);
        render_scrollbar(widgets.scrollbar);

        // Tab content (only render active tab's widgets)
        switch (g_active_tab) {
        case 0: // Editor tab
            render_editbox(widgets.editbox, g_scroll_drag == ScrollDragTarget::EditBox);
            break;
        case 1: // Controls tab
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
            break;
        case 2: // Visuals tab
            render_colorpicker(widgets.picker);
            render_image(widgets.image);
            break;
        }

        // Bottom panel: list+combo | output
        render_splitpanel(widgets.bottom_split);
        render_listbox(widgets.listbox);
        render_editbox(widgets.output_editbox, g_scroll_drag == ScrollDragTarget::OutputEditBox);

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
