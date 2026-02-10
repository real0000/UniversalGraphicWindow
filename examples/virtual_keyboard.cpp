/*
 * virtual_keyboard.cpp - Virtual Keyboard Test Example
 *
 * Demonstrates the virtual keyboard system:
 *   - Showing/hiding the on-screen keyboard
 *   - Handling text input events
 *   - Different keyboard types (text, number, email, etc.)
 *   - Keyboard state monitoring
 */

#include "window.hpp"
#include "gui/vk/virtual_keyboard.hpp"
#include "gui/font/font.hpp"
#include "input/input_keyboard.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include "api/glad.h"

#if defined(_WIN32)
    // SwapBuffers is from Windows GDI
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/glx.h>
#endif

//=============================================================================
// Shader sources (same as font example)
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
// QuadRenderer class
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
        if (program) glDeleteProgram(program);
        vbo = vao = program = 0;
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

    void draw_rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
        float vertices[] = {
            x,     y,      0.0f, 0.0f,
            x + w, y,      1.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
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
        float vertices[] = {
            x,     y,      0.0f, 0.0f,
            x + w, y,      1.0f, 0.0f,
            x + w, y + h,  1.0f, 1.0f,
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
// Text Input Field (implements ITextInputDelegate and IKeyboardHandler)
//=============================================================================

class TextInputField : public vkeyboard::ITextInputDelegate, public window::input::IKeyboardHandler {
public:
    std::string text;
    int cursor_pos = 0;
    int selection_start = 0;
    int selection_length = 0;
    bool focused = false;

    vkeyboard::TextInputContext get_text_input_context() const override {
        vkeyboard::TextInputContext ctx;
        ctx.text = text;
        ctx.selection.start = cursor_pos;
        ctx.selection.length = selection_length;
        return ctx;
    }

    void insert_text(const char* new_text) override {
        if (selection_length > 0) {
            // Replace selection
            text.erase(selection_start, selection_length);
            cursor_pos = selection_start;
            selection_length = 0;
        }
        text.insert(cursor_pos, new_text);
        cursor_pos += static_cast<int>(strlen(new_text));
        printf("  [TextInput] Inserted: '%s' -> '%s'\n", new_text, text.c_str());
    }

    void delete_backward(int count) override {
        if (selection_length > 0) {
            text.erase(selection_start, selection_length);
            cursor_pos = selection_start;
            selection_length = 0;
        } else if (cursor_pos > 0) {
            int del = (count > cursor_pos) ? cursor_pos : count;
            text.erase(cursor_pos - del, del);
            cursor_pos -= del;
        }
        printf("  [TextInput] Delete backward -> '%s'\n", text.c_str());
    }

    void delete_forward(int count) override {
        if (selection_length > 0) {
            text.erase(selection_start, selection_length);
            cursor_pos = selection_start;
            selection_length = 0;
        } else if (cursor_pos < static_cast<int>(text.length())) {
            int remaining = static_cast<int>(text.length()) - cursor_pos;
            int del = (count > remaining) ? remaining : count;
            text.erase(cursor_pos, del);
        }
        printf("  [TextInput] Delete forward -> '%s'\n", text.c_str());
    }

    void replace_text(const vkeyboard::TextRange& range, const char* new_text) override {
        if (range.start >= 0 && range.start < static_cast<int>(text.length())) {
            text.erase(range.start, range.length);
            text.insert(range.start, new_text);
            cursor_pos = range.start + static_cast<int>(strlen(new_text));
        }
        printf("  [TextInput] Replace -> '%s'\n", text.c_str());
    }

    void set_selection(const vkeyboard::TextRange& selection) override {
        selection_start = selection.start;
        selection_length = selection.length;
        cursor_pos = selection.start;
    }

    bool has_text() const override {
        return !text.empty();
    }

    // IKeyboardHandler implementation
    const char* get_handler_id() const override { return "TextInputField"; }
    int get_priority() const override { return 100; }

    bool on_key(const window::KeyEvent& event) override {
        if (!focused) return false;

        if (event.type == window::EventType::KeyDown) {
            if (event.key == window::Key::Backspace) {
                delete_backward(1);
                return true;
            } else if (event.key == window::Key::Delete) {
                delete_forward(1);
                return true;
            }
        }
        return false;
    }

    // Flag to indicate virtual keyboard is handling input (to avoid duplicates)
    bool vk_input_active = false;

    bool on_char(const window::CharEvent& event) override {
        if (!focused) return false;

        // When virtual keyboard is active, it uses text delegate directly
        // Don't process WM_CHAR here to avoid duplicate characters
        if (vk_input_active) {
            return true;  // Consume but don't insert (VK already inserted via delegate)
        }

        // Convert codepoint to UTF-8 and insert (physical keyboard only)
        char utf8[5] = {};
        uint32_t cp = event.codepoint;

        if (cp < 0x80) {
            utf8[0] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            utf8[0] = static_cast<char>(0xC0 | (cp >> 6));
            utf8[1] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8[0] = static_cast<char>(0xE0 | (cp >> 12));
            utf8[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8[2] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8[0] = static_cast<char>(0xF0 | (cp >> 18));
            utf8[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8[3] = static_cast<char>(0x80 | (cp & 0x3F));
        }

        printf("  [Char] Received: U+%04X '%s'\n", cp, utf8);
        insert_text(utf8);
        return true;
    }
};

//=============================================================================
// Virtual Keyboard Event Handler
//=============================================================================

class KeyboardEventHandler : public vkeyboard::IVirtualKeyboardEventHandler {
public:
    bool keyboard_visible = false;
    vkeyboard::Box keyboard_frame;

    void on_keyboard_will_show(const vkeyboard::KeyboardEventData& data) override {
        printf("  [Event] Keyboard will show (height: %.0f)\n", window::math::box_height(data.frame));
    }

    void on_keyboard_did_show(const vkeyboard::KeyboardEventData& data) override {
        keyboard_visible = true;
        keyboard_frame = data.frame;
        printf("  [Event] Keyboard did show (frame: %.0f,%.0f %.0fx%.0f)\n",
               window::math::x(data.frame.min_corner()), window::math::y(data.frame.min_corner()),
               window::math::box_width(data.frame), window::math::box_height(data.frame));
    }

    void on_keyboard_will_hide(const vkeyboard::KeyboardEventData& data) override {
        printf("  [Event] Keyboard will hide\n");
        (void)data;
    }

    void on_keyboard_did_hide(const vkeyboard::KeyboardEventData& data) override {
        keyboard_visible = false;
        keyboard_frame = vkeyboard::Box(window::math::Vec2(0,0), window::math::Vec2(0,0));
        printf("  [Event] Keyboard did hide\n");
        (void)data;
    }

    void on_text_input(const vkeyboard::TextInputEventData& data) override {
        printf("  [Event] Text input: action=%d, text='%s'\n",
               static_cast<int>(data.action), data.text.c_str());
    }

    void on_text_committed(const char* text) override {
        printf("  [Event] Text committed: '%s'\n", text);
    }

    void on_return_pressed() override {
        printf("  [Event] Return pressed\n");
    }
};

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

static bool render_text_to_texture(font::IFontRenderer* renderer, font::IFontFace* font,
                                    const char* text, const font::Vec4& color,
                                    TextTexture* out_texture) {
    if (!text || text[0] == '\0') {
        out_texture->id = 0;
        out_texture->width = 0;
        out_texture->height = 0;
        return true;
    }

    font::RenderOptions render_opts;
    render_opts.antialias = font::AntiAliasMode::Grayscale;
    render_opts.output_format = font::PixelFormat::RGBA8;
    font::TextLayoutOptions layout;

    void* pixels = nullptr;
    int width = 0, height = 0;
    font::PixelFormat format;

    font::Result result = renderer->render_text(font, text, -1, color, render_opts, layout,
                                                 &pixels, &width, &height, &format);

    if (result != font::Result::Success || !pixels) {
        return false;
    }

    // Delete old texture if exists
    if (out_texture->id) {
        glDeleteTextures(1, &out_texture->id);
    }

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
    return true;
}

//=============================================================================
// Key state tracking for press detection
//=============================================================================

class KeyState {
public:
    bool prev_states[512] = {};

    bool is_pressed(window::Window* win, window::Key key) {
        int idx = static_cast<int>(key) & 0x1FF;
        bool current = win->is_key_down(key);
        bool was_down = prev_states[idx];
        prev_states[idx] = current;
        return current && !was_down;
    }
};

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("Virtual Keyboard Test Example\n");
    printf("==============================\n\n");

    // Check platform support
    printf("Platform: %s\n", vkeyboard::get_platform_name());
    printf("Virtual keyboard supported: %s\n",
           vkeyboard::is_virtual_keyboard_supported() ? "Yes" : "No");

    // Create window
    window::Config config;
    config.windows[0].title = "Virtual Keyboard Test - Press ESC to exit";
    config.windows[0].width = 800;
    config.windows[0].height = 600;
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

    // Initialize renderer
    QuadRenderer renderer;
    if (!renderer.init()) {
        printf("Failed to initialize renderer\n");
        win->destroy();
        return 1;
    }

    // Initialize font system
    font::Result font_result;
    font::IFontLibrary* font_library = font::create_font_library(font::FontBackend::Auto, &font_result);
    if (!font_library) {
        printf("Failed to create font library\n");
        renderer.destroy();
        win->destroy();
        return 1;
    }

    font::IFontRenderer* font_renderer = font::create_font_renderer(font_library, &font_result);
    font::IFontFace* font_ui = font_library->load_system_font(
        font::FontDescriptor::create("Arial", 24.0f), nullptr);
    if (!font_ui) {
        font_ui = font_library->get_default_font(24.0f, nullptr);
    }

    // Create virtual keyboard
    vkeyboard::IVirtualKeyboard* vk = vkeyboard::create_virtual_keyboard();
    if (!vk) {
        printf("Failed to create virtual keyboard\n");
        font::destroy_font_renderer(font_renderer);
        font::destroy_font_library(font_library);
        renderer.destroy();
        win->destroy();
        return 1;
    }

    vkeyboard::Result vk_result = vk->initialize();
    printf("Virtual keyboard initialized: %s\n", vkeyboard::result_to_string(vk_result));
    printf("  Supported: %s\n", vk->is_supported() ? "Yes" : "No");
    printf("  Available: %s\n", vk->is_available() ? "Yes" : "No");

    // Set target window so keyboard knows where to send input
    vk->set_target_window(win->native_handle());

    // Set up event handler and text input
    KeyboardEventHandler event_handler;
    TextInputField input_field;
    input_field.text = "Type here...";

    vk->set_event_handler(&event_handler);
    vk->set_text_input_delegate(&input_field);

    // Register keyboard handler to receive character input
    win->add_keyboard_handler(&input_field);

    // Get available keyboard layouts
    vkeyboard::KeyboardLayoutList layouts;
    if (vk->get_available_layouts(&layouts) == vkeyboard::Result::Success) {
        printf("Available keyboard layouts: %zu\n", layouts.layouts.size());
        for (size_t i = 0; i < layouts.layouts.size() && i < 5; i++) {
            printf("  [%zu] %s (%s)%s\n", i,
                   layouts.layouts[i].display_name.c_str(),
                   layouts.layouts[i].language_code.c_str(),
                   layouts.layouts[i].is_current ? " [current]" : "");
        }
    }

    printf("\n");
    printf("Controls:\n");
    printf("  1 - Show default keyboard\n");
    printf("  2 - Show number keyboard\n");
    printf("  3 - Show email keyboard\n");
    printf("  4 - Show password keyboard\n");
    printf("  5 - Show search keyboard\n");
    printf("  H - Hide keyboard\n");
    printf("  T - Toggle keyboard\n");
    printf("  C - Clear text\n");
    printf("  ESC - Exit\n");
    printf("\n");

    // Text textures
    TextTexture title_tex, input_tex, status_tex, help_tex;
    std::string last_input_text;
    std::string last_status;

    // Key state tracking
    KeyState keys;
    bool prev_mouse_down = false;

    // Button definitions
    struct Button {
        float x, y, w, h;
        const char* label;
    };
    Button buttons[] = {
        {50, 260, 120, 35, "Show KB"},
        {180, 260, 120, 35, "Hide KB"},
        {310, 260, 120, 35, "Clear"},
        {440, 260, 120, 35, "Exit"}
    };
    const int num_buttons = 4;

    // Button textures
    TextTexture btn_textures[4];
    render_text_to_texture(font_renderer, font_ui, "Show KB", font::Vec4(1, 1, 1, 1), &btn_textures[0]);
    render_text_to_texture(font_renderer, font_ui, "Hide KB", font::Vec4(1, 1, 1, 1), &btn_textures[1]);
    render_text_to_texture(font_renderer, font_ui, "Clear", font::Vec4(1, 1, 1, 1), &btn_textures[2]);
    render_text_to_texture(font_renderer, font_ui, "Exit", font::Vec4(1, 1, 1, 1), &btn_textures[3]);

    // Render static text
    render_text_to_texture(font_renderer, font_ui, "Virtual Keyboard Test",
                           font::Vec4(1, 1, 0, 1), &title_tex);
    render_text_to_texture(font_renderer, font_ui, "Click buttons below to control keyboard",
                           font::Vec4(180/255.0f, 180/255.0f, 180/255.0f, 1.0f), &help_tex);

    // Main loop
    while (!win->should_close()) {
        win->poll_events();
        vk->update();

        // Handle mouse clicks on buttons
        int mx, my;
        win->get_mouse_position(&mx, &my);
        bool mouse_down = win->is_mouse_button_down(window::MouseButton::Left);
        bool mouse_clicked = mouse_down && !prev_mouse_down;
        prev_mouse_down = mouse_down;

        bool should_exit = false;
        if (mouse_clicked) {
            for (int i = 0; i < num_buttons; i++) {
                const Button& b = buttons[i];
                if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                    printf("Button clicked: %s\n", b.label);
                    if (i == 0) {  // Show KB
                        printf("Showing keyboard...\n");
                        input_field.focused = true;
                        input_field.vk_input_active = true;  // VK handles text input
                        vk->begin_text_input();
                        vkeyboard::Result r = vk->show(vkeyboard::KeyboardConfig::default_config());
                        printf("  Result: %s\n", vkeyboard::result_to_string(r));
                    } else if (i == 1) {  // Hide KB
                        printf("Hiding keyboard...\n");
                        vk->hide();
                        vk->end_text_input();
                        input_field.focused = false;
                        input_field.vk_input_active = false;  // Back to physical keyboard
                    } else if (i == 2) {  // Clear
                        printf("Clearing text...\n");
                        input_field.text.clear();
                        input_field.cursor_pos = 0;
                    } else if (i == 3) {  // Exit
                        should_exit = true;
                        break;
                    }
                }
            }
        }

        if (should_exit) {
            break;
        }

        // ESC to exit
        if (keys.is_pressed(win, window::Key::Escape)) {
            break;
        }

        // Update input texture if text changed
        if (input_field.text != last_input_text) {
            last_input_text = input_field.text;
            std::string display_text = "Input: " + input_field.text;
            if (input_field.focused) {
                display_text += "_";  // Cursor
            }
            render_text_to_texture(font_renderer, font_ui, display_text.c_str(),
                                   font::Vec4(1, 1, 1, 1), &input_tex);
        }

        // Update status
        std::string status = "Keyboard: ";
        status += vkeyboard::keyboard_state_to_string(vk->get_state());
        if (vk->is_visible()) {
            char buf[64];
            snprintf(buf, sizeof(buf), " (height: %.0f)", vk->get_height());
            status += buf;
        }
        if (status != last_status) {
            last_status = status;
            render_text_to_texture(font_renderer, font_ui, status.c_str(),
                                   font::Vec4(0, 1, 0, 1), &status_tex);
        }

        // Render
        int win_width, win_height;
        win->get_size(&win_width, &win_height);
        glViewport(0, 0, win_width, win_height);

        glClearColor(0.15f, 0.15f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        renderer.set_projection(win_width, win_height);

        // Draw title
        if (title_tex.id) {
            float x = (win_width - title_tex.width) / 2.0f;
            renderer.draw_texture(title_tex.id, x, 30,
                                  static_cast<float>(title_tex.width),
                                  static_cast<float>(title_tex.height));
        }

        // Draw help text
        if (help_tex.id) {
            float x = (win_width - help_tex.width) / 2.0f;
            renderer.draw_texture(help_tex.id, x, 70,
                                  static_cast<float>(help_tex.width),
                                  static_cast<float>(help_tex.height));
        }

        // Draw input field background
        float input_y = 150.0f;
        renderer.draw_rect(50, input_y - 5, static_cast<float>(win_width - 100), 40,
                           0.2f, 0.2f, 0.25f, 1.0f);
        if (input_field.focused) {
            renderer.draw_rect(48, input_y - 7, static_cast<float>(win_width - 96), 44,
                               0.3f, 0.5f, 0.8f, 1.0f);
            renderer.draw_rect(50, input_y - 5, static_cast<float>(win_width - 100), 40,
                               0.2f, 0.2f, 0.25f, 1.0f);
        }

        // Draw input text
        if (input_tex.id) {
            renderer.draw_texture(input_tex.id, 60, input_y,
                                  static_cast<float>(input_tex.width),
                                  static_cast<float>(input_tex.height));
        }

        // Draw status
        if (status_tex.id) {
            renderer.draw_texture(status_tex.id, 50, 220,
                                  static_cast<float>(status_tex.width),
                                  static_cast<float>(status_tex.height));
        }

        // Draw buttons
        for (int i = 0; i < num_buttons; i++) {
            const Button& b = buttons[i];
            // Button background
            bool hovered = (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h);
            if (hovered) {
                renderer.draw_rect(b.x, b.y, b.w, b.h, 0.4f, 0.5f, 0.7f, 1.0f);
            } else {
                renderer.draw_rect(b.x, b.y, b.w, b.h, 0.3f, 0.3f, 0.4f, 1.0f);
            }
            // Button label
            if (btn_textures[i].id) {
                float tx = b.x + (b.w - btn_textures[i].width) / 2;
                float ty = b.y + (b.h - btn_textures[i].height) / 2;
                renderer.draw_texture(btn_textures[i].id, tx, ty,
                                      static_cast<float>(btn_textures[i].width),
                                      static_cast<float>(btn_textures[i].height));
            }
        }

        // Draw keyboard indicator if visible
        if (event_handler.keyboard_visible) {
            float kb_h = window::math::box_height(event_handler.keyboard_frame);
            float kb_y = static_cast<float>(win_height) - kb_h;
            renderer.draw_rect(0, kb_y, static_cast<float>(win_width), kb_h,
                               0.1f, 0.3f, 0.5f, 0.3f);
        }

        glDisable(GL_BLEND);
        swap_buffers(win, gfx);
    }

    // Cleanup
    title_tex.destroy();
    input_tex.destroy();
    status_tex.destroy();
    help_tex.destroy();
    for (int i = 0; i < num_buttons; i++) {
        btn_textures[i].destroy();
    }

    vk->shutdown();
    vkeyboard::destroy_virtual_keyboard(vk);

    if (font_ui) font_library->destroy_font(font_ui);
    font::destroy_font_renderer(font_renderer);
    font_library->shutdown();
    font::destroy_font_library(font_library);

    renderer.destroy();
    win->destroy();

    printf("Example complete!\n");
    return 0;
}
