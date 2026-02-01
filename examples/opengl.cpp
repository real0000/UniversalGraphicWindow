/*
 * opengl.cpp - OpenGL window example
 */

#include "window.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <GL/gl.h>
#elif defined(__APPLE__)
    #include <OpenGL/gl3.h>
#else
    #include <GL/gl.h>
    #include <GL/glx.h>
#endif

int main() {
    window::Config config;
    strncpy(config.windows[0].title, "OpenGL Example", window::MAX_DEVICE_NAME_LENGTH - 1);
    config.windows[0].width = 800;
    config.windows[0].height = 600;
    config.backend = window::Backend::OpenGL;

    window::Result result;
    auto windows = window::Window::create(config, &result);

    if (result != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();

    printf("OpenGL context created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());
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

        // Swap buffers using native handles
#if defined(_WIN32)
        HDC hdc = static_cast<HDC>(gfx->native_swapchain());
        SwapBuffers(hdc);
#elif defined(__APPLE__)
        // On macOS, the context handles swap automatically with NSOpenGLView
        // Or use CGLFlushDrawable if using CGL directly
#else
        // Linux X11/GLX
        Display* display = static_cast<Display*>(gfx->native_swapchain());
        Window x_window = reinterpret_cast<Window>(win->native_handle());
        glXSwapBuffers(display, x_window);
#endif

        time += 0.016f;
    }

    win->destroy();
    return 0;
}
