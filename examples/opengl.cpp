/*
 * opengl.cpp - OpenGL window example
 */

#include "window.hpp"
#include <cstdio>
#include <cmath>

#if defined(_WIN32)
    #include <windows.h>
    #include <GL/gl.h>
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/gl.h>
#endif

int main() {
    window::Config config;
    config.title = "OpenGL Example";
    config.width = 800;
    config.height = 600;
    config.graphics_api = window::GraphicsAPI::OpenGL;
    config.opengl.major_version = 3;
    config.opengl.minor_version = 3;
    config.opengl.core_profile = true;

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    printf("OpenGL context created!\n");
    printf("Vendor: %s\n", glGetString(GL_VENDOR));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    printf("Version: %s\n", glGetString(GL_VERSION));

    float time = 0.0f;

    while (!win->should_close()) {
        win->poll_events();

        int width, height;
        win->get_size(&width, &height);
        glViewport(0, 0, width, height);

        // Animate background color
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;
        glClearColor(r * 0.3f, g * 0.3f, b * 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        win->swap_buffers();
        time += 0.016f;
    }

    win->destroy();
    return 0;
}
