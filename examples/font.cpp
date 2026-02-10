/*
 * font.cpp - Font rendering example
 *
 * Demonstrates the font system:
 *   - Loading system fonts
 *   - Rendering text to OpenGL textures
 *   - Multiple text styles and colors
 *   - Modern OpenGL shader-based rendering
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

// Include GLAD for modern OpenGL
#include "api/glad.h"

#if defined(_WIN32)
    // SwapBuffers is from Windows GDI
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/glx.h>
#endif

//=============================================================================
// Shader sources
//=============================================================================

static const char* vertex_shader_source = R"(
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

static const char* fragment_shader_source = R"(
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

//=============================================================================
// Shader utilities
//=============================================================================

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        printf("Shader compilation error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint create_shader_program(const char* vs_source, const char* fs_source) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_source);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        printf("Shader link error: %s\n", log);
        glDeleteProgram(program);
        program = 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}

//=============================================================================
// Renderer class for modern OpenGL
//=============================================================================

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
        program = create_shader_program(vertex_shader_source, fragment_shader_source);
        if (!program) return false;

        loc_projection = glGetUniformLocation(program, "uProjection");
        loc_color = glGetUniformLocation(program, "uColor");
        loc_texture = glGetUniformLocation(program, "uTexture");
        loc_use_texture = glGetUniformLocation(program, "uUseTexture");

        // Create VAO and VBO for quad rendering
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // Reserve space for 6 vertices (2 triangles) with pos(2) + texcoord(2)
        glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

        // Position attribute (location 0)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // TexCoord attribute (location 1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
        return true;
    }

    void destroy() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
        vbo = vao = program = 0;
    }

    void set_projection(int width, int height) {
        // Orthographic projection matrix (top-left origin)
        float proj[16] = {
            2.0f / width, 0.0f,           0.0f, 0.0f,
            0.0f,        -2.0f / height,  0.0f, 0.0f,
            0.0f,         0.0f,          -1.0f, 0.0f,
           -1.0f,         1.0f,           0.0f, 1.0f
        };
        glUseProgram(program);
        glUniformMatrix4fv(loc_projection, 1, GL_FALSE, proj);
    }

    void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
        // Quad vertices: position(2) + texcoord(2)
        float vertices[] = {
            // Triangle 1
            x,     y,      0.0f, 0.0f,
            x + w, y,      1.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
            // Triangle 2
            x,     y,      0.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
            x,     y + h,  0.0f, 1.0f
        };

        glUseProgram(program);
        glUniform4f(loc_color, r, g, b, a);
        glUniform1i(loc_use_texture, 0);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    void draw_texture(GLuint texture_id, float x, float y, float w, float h, float alpha = 1.0f) {
        // Quad vertices: position(2) + texcoord(2)
        float vertices[] = {
            // Triangle 1
            x,     y,      0.0f, 0.0f,
            x + w, y,      1.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
            // Triangle 2
            x,     y,      0.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
            x,     y + h,  0.0f, 1.0f
        };

        glUseProgram(program);
        glUniform4f(loc_color, 1.0f, 1.0f, 1.0f, alpha);
        glUniform1i(loc_use_texture, 1);
        glUniform1i(loc_texture, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture_id);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};

//=============================================================================
// Text texture helper
//=============================================================================

struct TextTexture {
    GLuint id = 0;
    int width = 0;
    int height = 0;

    void destroy() {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
        }
    }
};

// Create OpenGL texture from rendered text
static bool create_text_texture(font::IFontRenderer* renderer, font::IFontFace* font,
                                 const char* text, const window::math::Vec4& color,
                                 const font::TextLayoutOptions& layout_opts,
                                 TextTexture* out_texture) {
    font::RenderOptions render_opts;
    render_opts.antialias = font::AntiAliasMode::Grayscale;
    render_opts.output_format = font::PixelFormat::RGBA8;

    void* pixels = nullptr;
    int width = 0, height = 0;
    font::PixelFormat format;

    font::Result result = renderer->render_text(font, text, -1, color, render_opts, layout_opts,
                                                 &pixels, &width, &height, &format);

    if (result != font::Result::Success || !pixels || width <= 0 || height <= 0) {
        printf("  Failed to render text: %s\n", font::result_to_string(result));
        if (pixels) renderer->free_bitmap(pixels);
        return false;
    }

    printf("  Rendered '%s': %dx%d\n", text, width, height);

    // Create OpenGL texture
    GLuint texture_id = 0;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    renderer->free_bitmap(pixels);

    out_texture->id = texture_id;
    out_texture->width = width;
    out_texture->height = height;

    printf("  Created texture ID=%u\n", texture_id);
    return true;
}

//=============================================================================
// Platform-specific swap buffers
//=============================================================================

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

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("Font Rendering Example\n");
    printf("======================\n\n");

    // Create OpenGL window
    window::Config config;
    config.windows[0].title = "Font Rendering Example - Press ESC to exit";
    config.windows[0].width = 900;
    config.windows[0].height = 700;
    config.backend = window::Backend::OpenGL;

    window::Result win_result;
    auto windows = window::Window::create(config, &win_result);

    if (win_result != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(win_result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();
    printf("Window created (Backend: %s)\n", gfx->get_backend_name());
    printf("OpenGL Version: %s\n", glGetString(GL_VERSION));
    printf("OpenGL Renderer: %s\n", glGetString(GL_RENDERER));

    // Initialize quad renderer
    QuadRenderer renderer;
    if (!renderer.init()) {
        printf("Failed to initialize renderer\n");
        win->destroy();
        return 1;
    }

    // Create font library
    font::Result font_result;
    font::IFontLibrary* library = font::create_font_library(font::FontBackend::Auto, &font_result);

    if (!library) {
        printf("Failed to create font library: %s\n", font::result_to_string(font_result));
        renderer.destroy();
        win->destroy();
        return 1;
    }

    printf("Font library created (backend: %s)\n",
           font::font_backend_to_string(library->get_backend()));

    // Create font renderer
    font::IFontRenderer* font_renderer = font::create_font_renderer(library, &font_result);
    if (!font_renderer) {
        printf("Failed to create font renderer\n");
        font::destroy_font_library(library);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    // Load fonts
    printf("Loading fonts...\n");
    font::IFontFace* font_title = library->load_system_font(
        font::FontDescriptor::create("Arial", 48.0f), nullptr);
    font::IFontFace* font_body = library->load_system_font(
        font::FontDescriptor::create("Arial", 24.0f), nullptr);

    if (!font_title) {
        printf("Arial not found, using default font\n");
        font_title = library->get_default_font(48.0f, nullptr);
    }
    if (!font_body) {
        font_body = library->get_default_font(24.0f, nullptr);
    }

    if (!font_title || !font_body) {
        printf("Failed to load fonts\n");
        font::destroy_font_renderer(font_renderer);
        font::destroy_font_library(library);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    printf("Fonts loaded: %s (size=%.0f)\n", font_title->get_family_name(), font_title->get_size());

    // Create text textures
    std::vector<TextTexture> textures;
    TextTexture tex;
    font::TextLayoutOptions layout;

    printf("\nCreating text textures...\n");

    // Title (yellow)
    if (create_text_texture(font_renderer, font_title, "Hello World!",
                            window::math::Vec4(1, 1, 0, 1), layout, &tex)) {
        textures.push_back(tex);
    }

    // Subtitle (white)
    if (create_text_texture(font_renderer, font_body, "Font Rendering Demo",
                            window::math::Vec4(1, 1, 1, 1), layout, &tex)) {
        textures.push_back(tex);
    }

    // Cyan text
    if (create_text_texture(font_renderer, font_body, "The quick brown fox jumps over the lazy dog.",
                            window::math::Vec4(0, 1, 1, 1), layout, &tex)) {
        textures.push_back(tex);
    }

    // Green text
    if (create_text_texture(font_renderer, font_body, "OpenGL + DirectWrite Text Rendering",
                            window::math::Vec4(0, 1, 0, 1), layout, &tex)) {
        textures.push_back(tex);
    }

    printf("\nCreated %zu text textures\n", textures.size());

    // Create a test gradient texture
    TextTexture test_tex;
    {
        const int tw = 128, th = 64;
        uint8_t* test_pixels = new uint8_t[tw * th * 4];
        for (int y = 0; y < th; ++y) {
            for (int x = 0; x < tw; ++x) {
                int i = (y * tw + x) * 4;
                test_pixels[i + 0] = 255;
                test_pixels[i + 1] = static_cast<uint8_t>(x * 2);
                test_pixels[i + 2] = 0;
                test_pixels[i + 3] = 255;
            }
        }

        GLuint test_id = 0;
        glGenTextures(1, &test_id);
        glBindTexture(GL_TEXTURE_2D, test_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, test_pixels);
        delete[] test_pixels;

        test_tex.id = test_id;
        test_tex.width = tw;
        test_tex.height = th;
        printf("Created test gradient texture ID=%u\n", test_id);
    }

    printf("\nPress ESC to exit...\n");

    // Main render loop
    while (!win->should_close()) {
        win->poll_events();

        if (win->is_key_down(window::Key::Escape)) {
            break;
        }

        int win_width, win_height;
        win->get_size(&win_width, &win_height);
        glViewport(0, 0, win_width, win_height);

        // Clear with dark blue
        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Enable blending for text
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Set up projection
        renderer.set_projection(win_width, win_height);

        // Draw test rectangle (red)
        renderer.draw_rect(10, 10, 100, 50, 1.0f, 0.0f, 0.0f, 1.0f);

        // Draw test gradient texture
        renderer.draw_texture(test_tex.id, 120, 10,
                              static_cast<float>(test_tex.width),
                              static_cast<float>(test_tex.height), 1.0f);

        // Draw text textures
        float y = 100.0f;
        for (size_t i = 0; i < textures.size(); i++) {
            const TextTexture& t = textures[i];
            float x = 50.0f;

            // Center first texture (title)
            if (i == 0) {
                x = (win_width - t.width) / 2.0f;
            }

            // Draw background for visibility
            renderer.draw_rect(x - 2, y - 2,
                               static_cast<float>(t.width + 4),
                               static_cast<float>(t.height + 4),
                               0.2f, 0.2f, 0.3f, 0.5f);

            // Draw text texture
            renderer.draw_texture(t.id, x, y,
                                  static_cast<float>(t.width),
                                  static_cast<float>(t.height), 1.0f);
            y += t.height + 20.0f;
        }

        glDisable(GL_BLEND);
        swap_buffers(win, gfx);
    }

    // Cleanup
    test_tex.destroy();
    for (TextTexture& t : textures) {
        t.destroy();
    }

    if (font_title) library->destroy_font(font_title);
    if (font_body) library->destroy_font(font_body);

    font::destroy_font_renderer(font_renderer);
    library->shutdown();
    font::destroy_font_library(library);
    renderer.destroy();
    win->destroy();

    printf("Example complete!\n");
    return 0;
}
