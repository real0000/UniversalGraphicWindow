/*
 * test_font.cpp - Visual font fallback test
 *
 * Opens an OpenGL window and renders a mixed-language string covering:
 * Latin, Simplified Chinese, Traditional Chinese, Japanese, Korean,
 * Arabic, Devanagari, Thai -- using the font fallback chain.
 *
 * Press ESC to exit.
 */

#include "window.hpp"
#include "gui/font/font.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include "api/glad.h"

#if defined(_WIN32)
    // SwapBuffers from Windows GDI
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/glx.h>
#endif

// ============================================================================
// Multi-language test strings (UTF-8 hex escapes, no raw non-ASCII)
// ============================================================================

struct ScriptEntry {
    const char* label;
    const char* text;
    float r, g, b;
};

static ScriptEntry g_scripts[] = {
    // label           text (UTF-8)                                                              R     G     B
    {"Latin",          "Hello World! The quick brown fox jumps over the lazy dog.",               1.0f, 1.0f, 1.0f},
    {"CJK Simplified", "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95 "
                       "\xe5\xad\x97\xe4\xbd\x93\xe5\x9b\x9e\xe9\x80\x80\xe6\xb5\x8b\xe8\xaf\x95",
                                                                                                1.0f, 0.9f, 0.3f},
    {"CJK Traditional","\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87\xe5\xad\x97\xe9\xab\x94",
                                                                                                1.0f, 0.7f, 0.2f},
    {"Japanese",       "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf\xe4\xb8\x96\xe7\x95\x8c",
                                                                                                0.3f, 1.0f, 0.8f},
    {"Korean",         "\xec\x95\x88\xeb\x85\x95\xed\x95\x98\xec\x84\xb8\xec\x9a\x94 "
                       "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4",
                                                                                                0.4f, 0.8f, 1.0f},
    {"Arabic",         "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85",
                                                                                                0.9f, 0.5f, 1.0f},
    {"Devanagari",     "\xe0\xa4\xa8\xe0\xa4\xae\xe0\xa4\xb8\xe0\xa5\x8d\xe0\xa4\xa4\xe0\xa5\x87 "
                       "\xe0\xa4\xa6\xe0\xa5\x81\xe0\xa4\xa8\xe0\xa4\xbf\xe0\xa4\xaf\xe0\xa4\xbe",
                                                                                                1.0f, 0.6f, 0.4f},
    {"Thai",           "\xe0\xb8\xaa\xe0\xb8\xa7\xe0\xb8\xb1\xe0\xb8\xaa\xe0\xb8\x94\xe0\xb8\xb5 "
                       "\xe0\xb8\x8a\xe0\xb8\xb2\xe0\xb8\xa7\xe0\xb9\x82\xe0\xb8\xa5\xe0\xb8\x81",
                                                                                                0.5f, 1.0f, 0.5f},
    {"Mixed All",      "Hello \xe4\xb8\xad\xe6\x96\x87 "
                       "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf "
                       "\xec\x95\x88\xeb\x85\x95 "
                       "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 "
                       "\xe0\xa4\xa8\xe0\xa4\xae\xe0\xa4\xb8\xe0\xa5\x8d\xe0\xa4\xa4\xe0\xa5\x87 "
                       "\xe0\xb8\xaa\xe0\xb8\xa7\xe0\xb8\xb1\xe0\xb8\xaa\xe0\xb8\x94\xe0\xb8\xb5",
                                                                                                1.0f, 1.0f, 0.0f},
};

static const int NUM_SCRIPTS = sizeof(g_scripts) / sizeof(g_scripts[0]);

// ============================================================================
// Shader sources
// ============================================================================

static const char* vs_src = R"(
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

static const char* fs_src = R"(
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
// GL helpers
// ============================================================================

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &source, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        printf("Shader error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint create_program(const char* vs, const char* fs) {
    GLuint v = compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = compile_shader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) { if (v) glDeleteShader(v); if (f) glDeleteShader(f); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

struct Renderer {
    GLuint prog = 0, vao = 0, vbo = 0;
    GLint loc_proj = -1, loc_color = -1, loc_tex = -1, loc_use_tex = -1;

    bool init() {
        prog = create_program(vs_src, fs_src);
        if (!prog) return false;
        loc_proj    = glGetUniformLocation(prog, "uProjection");
        loc_color   = glGetUniformLocation(prog, "uColor");
        loc_tex     = glGetUniformLocation(prog, "uTexture");
        loc_use_tex = glGetUniformLocation(prog, "uUseTexture");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        return true;
    }

    void destroy() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
    }

    void set_projection(int w, int h) {
        float proj[16] = {
            2.0f/w, 0,        0, 0,
            0,     -2.0f/h,   0, 0,
            0,      0,       -1, 0,
           -1,      1,        0, 1
        };
        glUseProgram(prog);
        glUniformMatrix4fv(loc_proj, 1, GL_FALSE, proj);
    }

    void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
        float verts[] = {
            x,   y,   0,0, x+w, y,   1,0, x+w, y+h, 1,1,
            x,   y,   0,0, x+w, y+h, 1,1, x,   y+h, 0,1
        };
        glUseProgram(prog);
        glUniform4f(loc_color, r, g, b, a);
        glUniform1i(loc_use_tex, 0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void draw_texture(GLuint tex, float x, float y, float w, float h) {
        float verts[] = {
            x,   y,   0,0, x+w, y,   1,0, x+w, y+h, 1,1,
            x,   y,   0,0, x+w, y+h, 1,1, x,   y+h, 0,1
        };
        glUseProgram(prog);
        glUniform4f(loc_color, 1, 1, 1, 1);
        glUniform1i(loc_use_tex, 1);
        glUniform1i(loc_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};

// ============================================================================
// Text texture
// ============================================================================

struct TextTex {
    GLuint id = 0;
    int w = 0, h = 0;
    void destroy() { if (id) { glDeleteTextures(1, &id); id = 0; } }
};

static bool render_text_to_gl(font::IFontRenderer* fr, font::IFontFace* face,
                               const char* text, const window::math::Vec4& color,
                               TextTex* out) {
    font::RenderOptions ro;
    ro.antialias = font::AntiAliasMode::Grayscale;
    ro.output_format = font::PixelFormat::RGBA8;
    font::TextLayoutOptions lo;

    void* pixels = nullptr;
    int w = 0, h = 0;
    font::PixelFormat fmt;
    font::Result r = fr->render_text(face, text, -1, color, ro, lo, &pixels, &w, &h, &fmt);
    if (r != font::Result::Success || !pixels || w <= 0 || h <= 0) {
        if (pixels) fr->free_bitmap(pixels);
        return false;
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
    fr->free_bitmap(pixels);

    out->id = tex;
    out->w = w;
    out->h = h;
    return true;
}

// ============================================================================
// Platform swap
// ============================================================================

static void swap_buffers(window::Window* win, window::Graphics* gfx) {
#if defined(_WIN32)
    (void)win;
    HDC hdc = static_cast<HDC>(gfx->native_swapchain());
    SwapBuffers(hdc);
#elif defined(__APPLE__)
    (void)win; (void)gfx;
#else
    Display* display = static_cast<Display*>(gfx->native_swapchain());
    ::Window x_window = reinterpret_cast<::Window>(win->native_handle());
    glXSwapBuffers(display, x_window);
#endif
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Font Fallback Visual Test ===\n\n");

    // --- Create window ---
    window::Config config;
    config.windows[0].title = "Font Fallback Test - ESC to exit";
    config.windows[0].width = 1024;
    config.windows[0].height = 720;
    config.backend = window::Backend::OpenGL;

    window::Result wr;
    auto windows = window::Window::create(config, &wr);
    if (wr != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(wr));
        return 1;
    }
    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();

    // --- Init GL renderer ---
    Renderer rdr;
    if (!rdr.init()) {
        printf("Failed to init GL renderer\n");
        win->destroy();
        return 1;
    }

    // --- Init font system ---
    font::Result fr;
    font::IFontLibrary* lib = font::create_font_library(font::FontBackend::Auto, &fr);
    if (!lib) {
        printf("Failed to create font library: %s\n", font::result_to_string(fr));
        rdr.destroy(); win->destroy();
        return 1;
    }

    font::IFontFace* face = lib->get_default_font(24.0f, nullptr);
    if (!face) {
        printf("Failed to load default font\n");
        font::destroy_font_library(lib); rdr.destroy(); win->destroy();
        return 1;
    }
    printf("Primary font: %s\n", face->get_family_name());

    // Create fallback chain
    font::IFontFallbackChain* chain = font::create_fallback_chain_with_defaults(lib, face, 24.0f);
    printf("Fallback chain: %d fonts\n", chain ? chain->get_font_count() : 1);
    if (chain) {
        for (int i = 0; i < chain->get_font_count(); i++) {
            font::IFontFace* f = chain->get_font(i);
            if (f) printf("  [%d] %s\n", i, f->get_family_name());
        }
    }

    // Create font renderer with fallback
    font::IFontRenderer* font_rdr = font::create_font_renderer(lib, nullptr);
    if (!font_rdr) {
        printf("Failed to create font renderer\n");
        font::destroy_fallback_chain(chain);
        font::destroy_font_library(lib); rdr.destroy(); win->destroy();
        return 1;
    }
    font_rdr->set_fallback_chain(chain);

    // Also create a label font (smaller, for script labels)
    font::IFontFace* label_face = lib->get_default_font(14.0f, nullptr);

    // --- Render all script textures ---
    struct Row {
        TextTex label_tex;
        TextTex text_tex;
        const char* label;
        bool ok;
    };
    std::vector<Row> rows(NUM_SCRIPTS);

    printf("\nRendering scripts:\n");
    for (int i = 0; i < NUM_SCRIPTS; i++) {
        auto& s = g_scripts[i];
        auto& row = rows[i];
        row.label = s.label;

        // Render label (white, smaller font)
        char label_buf[64];
        snprintf(label_buf, sizeof(label_buf), "%-16s", s.label);
        window::math::Vec4 white(1, 1, 1, 0.6f);
        if (label_face) {
            render_text_to_gl(font_rdr, label_face, label_buf, white, &row.label_tex);
        }

        // Render script text (colored)
        window::math::Vec4 color(s.r, s.g, s.b, 1.0f);
        row.ok = render_text_to_gl(font_rdr, face, s.text, color, &row.text_tex);

        printf("  %-16s %dx%d  %s\n", s.label,
               row.text_tex.w, row.text_tex.h,
               row.ok ? "OK" : "FAILED");
    }

    // --- Render title ---
    font::IFontFace* title_face = lib->get_default_font(32.0f, nullptr);
    TextTex title_tex;
    if (title_face) {
        window::math::Vec4 yellow(1, 1, 0, 1);
        render_text_to_gl(font_rdr, title_face, "Font Fallback Test", yellow, &title_tex);
    }

    // --- Render info line ---
    TextTex info_tex;
    {
        char info_buf[128];
        snprintf(info_buf, sizeof(info_buf), "Primary: %s | Fallback fonts: %d | Press ESC to exit",
                 face->get_family_name(), chain ? chain->get_font_count() : 1);
        window::math::Vec4 gray(0.7f, 0.7f, 0.7f, 1.0f);
        if (label_face)
            render_text_to_gl(font_rdr, label_face, info_buf, gray, &info_tex);
    }

    printf("\nWindow open. Press ESC to exit.\n");

    // --- Main loop ---
    while (!win->should_close()) {
        win->poll_events();
        if (win->is_key_down(window::Key::Escape)) break;

        int ww, wh;
        win->get_size(&ww, &wh);
        glViewport(0, 0, ww, wh);
        glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        rdr.set_projection(ww, wh);

        float y = 20.0f;
        float left_margin = 30.0f;
        float text_x = 200.0f;

        // Title
        if (title_tex.id) {
            float tx = (ww - title_tex.w) / 2.0f;
            rdr.draw_texture(title_tex.id, tx, y, (float)title_tex.w, (float)title_tex.h);
            y += title_tex.h + 10.0f;
        }

        // Info line
        if (info_tex.id) {
            float ix = (ww - info_tex.w) / 2.0f;
            rdr.draw_texture(info_tex.id, ix, y, (float)info_tex.w, (float)info_tex.h);
            y += info_tex.h + 20.0f;
        }

        // Separator line
        rdr.draw_rect(left_margin, y, (float)(ww - 2 * left_margin), 1, 0.3f, 0.3f, 0.5f, 1.0f);
        y += 10.0f;

        // Script rows
        for (int i = 0; i < NUM_SCRIPTS; i++) {
            auto& row = rows[i];
            float row_h = 0;

            // Background for alternating rows
            if (i % 2 == 0) {
                rdr.draw_rect(left_margin - 10, y - 4,
                              (float)(ww - 2 * left_margin + 20),
                              36.0f, 0.12f, 0.12f, 0.22f, 0.5f);
            }

            // Label
            if (row.label_tex.id) {
                rdr.draw_texture(row.label_tex.id, left_margin, y,
                                 (float)row.label_tex.w, (float)row.label_tex.h);
                if (row.label_tex.h > row_h) row_h = (float)row.label_tex.h;
            }

            // Text
            if (row.text_tex.id) {
                rdr.draw_texture(row.text_tex.id, text_x, y,
                                 (float)row.text_tex.w, (float)row.text_tex.h);
                if (row.text_tex.h > row_h) row_h = (float)row.text_tex.h;
            } else {
                // Show "[no font]" for failed renders
                rdr.draw_rect(text_x, y + 4, 80, 16, 0.5f, 0.2f, 0.2f, 1.0f);
            }

            y += row_h + 12.0f;
        }

        // Bottom separator
        rdr.draw_rect(left_margin, y, (float)(ww - 2 * left_margin), 1, 0.3f, 0.3f, 0.5f, 1.0f);

        glDisable(GL_BLEND);
        swap_buffers(win, gfx);
    }

    // --- Cleanup ---
    title_tex.destroy();
    info_tex.destroy();
    for (auto& row : rows) {
        row.label_tex.destroy();
        row.text_tex.destroy();
    }

    font::destroy_font_renderer(font_rdr);
    if (chain) font::destroy_fallback_chain(chain);
    font::destroy_font_library(lib);
    rdr.destroy();
    win->destroy();

    printf("Test complete.\n");
    return 0;
}
