/*
 * api_opengl_wasm.cpp - WebGL graphics backend for Emscripten
 */

#if defined(WINDOW_PLATFORM_WASM) && defined(WINDOW_SUPPORT_OPENGL)

#include "window.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdio>
#include <string>

namespace window {

class WasmGraphics : public Graphics {
public:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    std::string canvas_id;
    int width = 0;
    int height = 0;
    bool vsync = true;

    const char* get_backend_name() const override {
        return "WebGL";
    }

    const char* get_device_name() const override {
        // Get WebGL renderer string
        static char device_name[256] = "WebGL";
        const char* renderer = (const char*)glGetString(GL_RENDERER);
        if (renderer) {
            strncpy(device_name, renderer, sizeof(device_name) - 1);
            device_name[sizeof(device_name) - 1] = '\0';
        }
        return device_name;
    }

    void* native_device() const override {
        return (void*)(intptr_t)context;
    }

    void* native_swapchain() const override {
        return (void*)canvas_id.c_str();
    }

    void destroy() override {
        if (context) {
            emscripten_webgl_destroy_context(context);
            context = 0;
        }
        delete this;
    }
};

Graphics* create_webgl_graphics(const char* canvas_id, int width, int height, const Config& config) {
    WasmGraphics* gfx = new WasmGraphics();
    gfx->canvas_id = canvas_id;
    gfx->width = width;
    gfx->height = height;
    gfx->vsync = config.vsync;

    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);

    // Set context attributes from config
    attrs.alpha = true;
    attrs.depth = (config.depth_bits > 0);
    attrs.stencil = (config.stencil_bits > 0);
    attrs.antialias = (config.samples > 1);
    attrs.premultipliedAlpha = true;
    attrs.preserveDrawingBuffer = false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attrs.failIfMajorPerformanceCaveat = false;

    // Try WebGL 2.0 first (OpenGL ES 3.0)
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;

    gfx->context = emscripten_webgl_create_context(canvas_id, &attrs);

    // Fall back to WebGL 1.0 if WebGL 2.0 is not available
    if (gfx->context <= 0) {
        attrs.majorVersion = 1;
        attrs.minorVersion = 0;
        gfx->context = emscripten_webgl_create_context(canvas_id, &attrs);
    }

    if (gfx->context <= 0) {
        printf("Failed to create WebGL context\n");
        delete gfx;
        return nullptr;
    }

    // Make context current
    EMSCRIPTEN_RESULT result = emscripten_webgl_make_context_current(gfx->context);
    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
        printf("Failed to make WebGL context current\n");
        emscripten_webgl_destroy_context(gfx->context);
        delete gfx;
        return nullptr;
    }

    // Print WebGL info
    printf("WebGL initialized:\n");
    printf("  Version: %s\n", glGetString(GL_VERSION));
    printf("  Vendor: %s\n", glGetString(GL_VENDOR));
    printf("  Renderer: %s\n", glGetString(GL_RENDERER));
    printf("  GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    return gfx;
}

//=============================================================================
// Graphics::create for external windows (not typically used in WASM)
//=============================================================================

Graphics* Graphics::create(const ExternalWindowConfig& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    // In WASM, we typically create graphics with the window
    // This is a fallback for external canvas creation
    if (!config.native_handle) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    const char* canvas_id = static_cast<const char*>(config.native_handle);

    // Convert ExternalWindowConfig to Config
    Config internal_config;
    internal_config.windows[0].width = config.width;
    internal_config.windows[0].height = config.height;
    internal_config.vsync = config.vsync;
    internal_config.samples = config.samples;
    internal_config.color_bits = config.red_bits + config.green_bits + config.blue_bits + config.alpha_bits;
    internal_config.depth_bits = config.depth_bits;
    internal_config.stencil_bits = config.stencil_bits;
    internal_config.back_buffers = config.back_buffers;
    internal_config.backend = config.backend;

    Graphics* gfx = create_webgl_graphics(canvas_id, config.width, config.height, internal_config);
    if (!gfx) {
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    set_result(Result::Success);
    return gfx;
}

} // namespace window

#endif // WINDOW_PLATFORM_WASM && WINDOW_SUPPORT_OPENGL
