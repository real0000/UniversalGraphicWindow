/*
 * test_window.cpp - Unit tests for window library
 *
 * This test file contains basic unit tests that can run headless (without a display)
 * by testing utility functions and type definitions.
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>

// Simple test framework
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    printf("Running %s... ", #name); \
    try { \
        test_##name(); \
        printf("PASSED\n"); \
        g_tests_passed++; \
    } catch (...) { \
        printf("FAILED (exception)\n"); \
        g_tests_failed++; \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED at line %d: %s != %s\n", __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAILED at line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

//=============================================================================
// Tests for utility functions
//=============================================================================

TEST(result_to_string) {
    ASSERT_STREQ(window::result_to_string(window::Result::Success), "Success");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorUnknown), "Unknown error");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorPlatformInit), "Platform initialization failed");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorWindowCreation), "Window creation failed");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorGraphicsInit), "Graphics initialization failed");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorNotSupported), "Not supported");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorInvalidParameter), "Invalid parameter");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorOutOfMemory), "Out of memory");
    ASSERT_STREQ(window::result_to_string(window::Result::ErrorDeviceLost), "Device lost");
}

TEST(backend_to_string) {
    ASSERT_STREQ(window::backend_to_string(window::Backend::Auto), "Auto");
    ASSERT_STREQ(window::backend_to_string(window::Backend::OpenGL), "OpenGL");
    ASSERT_STREQ(window::backend_to_string(window::Backend::Vulkan), "Vulkan");
    ASSERT_STREQ(window::backend_to_string(window::Backend::D3D11), "Direct3D 11");
    ASSERT_STREQ(window::backend_to_string(window::Backend::D3D12), "Direct3D 12");
    ASSERT_STREQ(window::backend_to_string(window::Backend::Metal), "Metal");
}

TEST(is_backend_supported) {
    // Auto should always be supported
    ASSERT(window::is_backend_supported(window::Backend::Auto));

    // Platform-specific backends
#if defined(WINDOW_SUPPORT_OPENGL)
    ASSERT(window::is_backend_supported(window::Backend::OpenGL));
#endif
#if defined(WINDOW_SUPPORT_D3D11)
    ASSERT(window::is_backend_supported(window::Backend::D3D11));
#endif
#if defined(WINDOW_SUPPORT_D3D12)
    ASSERT(window::is_backend_supported(window::Backend::D3D12));
#endif
#if defined(WINDOW_SUPPORT_VULKAN)
    ASSERT(window::is_backend_supported(window::Backend::Vulkan));
#endif
#if defined(WINDOW_SUPPORT_METAL)
    ASSERT(window::is_backend_supported(window::Backend::Metal));
#endif
}

TEST(get_default_backend) {
    window::Backend default_backend = window::get_default_backend();

    // Default backend should be supported
    ASSERT(window::is_backend_supported(default_backend));

    // Should not be Auto
    ASSERT(default_backend != window::Backend::Auto);
}

//=============================================================================
// Tests for Config struct defaults
//=============================================================================

TEST(config_defaults) {
    window::Config config;

    ASSERT_STREQ(config.title, "Window");
    ASSERT_EQ(config.width, 800);
    ASSERT_EQ(config.height, 600);
    ASSERT_EQ(config.x, -1);
    ASSERT_EQ(config.y, -1);
    ASSERT(config.resizable == true);
    ASSERT(config.visible == true);
    ASSERT(config.vsync == true);
    ASSERT_EQ(config.samples, 1);
    ASSERT_EQ(config.red_bits, 8);
    ASSERT_EQ(config.green_bits, 8);
    ASSERT_EQ(config.blue_bits, 8);
    ASSERT_EQ(config.alpha_bits, 8);
    ASSERT_EQ(config.depth_bits, 24);
    ASSERT_EQ(config.stencil_bits, 8);
    ASSERT_EQ(config.back_buffers, 2);
    ASSERT(config.backend == window::Backend::Auto);
    ASSERT(config.shared_graphics == nullptr);
}

TEST(config_custom) {
    window::Config config;
    config.title = "Custom Title";
    config.width = 1920;
    config.height = 1080;
    config.x = 100;
    config.y = 200;
    config.resizable = false;
    config.visible = false;
    config.vsync = false;
    config.samples = 4;
    config.backend = window::Backend::OpenGL;

    ASSERT_STREQ(config.title, "Custom Title");
    ASSERT_EQ(config.width, 1920);
    ASSERT_EQ(config.height, 1080);
    ASSERT_EQ(config.x, 100);
    ASSERT_EQ(config.y, 200);
    ASSERT(config.resizable == false);
    ASSERT(config.visible == false);
    ASSERT(config.vsync == false);
    ASSERT_EQ(config.samples, 4);
    ASSERT(config.backend == window::Backend::OpenGL);
}

//=============================================================================
// Tests for ExternalWindowConfig struct
//=============================================================================

TEST(external_config_defaults) {
    window::ExternalWindowConfig config;

    ASSERT(config.native_handle == nullptr);
    ASSERT(config.native_display == nullptr);
    ASSERT_EQ(config.width, 0);
    ASSERT_EQ(config.height, 0);
    ASSERT(config.vsync == true);
    ASSERT_EQ(config.samples, 1);
    ASSERT_EQ(config.red_bits, 8);
    ASSERT_EQ(config.green_bits, 8);
    ASSERT_EQ(config.blue_bits, 8);
    ASSERT_EQ(config.alpha_bits, 8);
    ASSERT_EQ(config.depth_bits, 24);
    ASSERT_EQ(config.stencil_bits, 8);
    ASSERT_EQ(config.back_buffers, 2);
    ASSERT(config.backend == window::Backend::Auto);
    ASSERT(config.shared_graphics == nullptr);
}

TEST(external_config_custom) {
    window::ExternalWindowConfig config;
    config.native_handle = reinterpret_cast<void*>(0x12345678);
    config.native_display = reinterpret_cast<void*>(0x87654321);
    config.width = 1280;
    config.height = 720;
    config.vsync = false;
    config.samples = 4;
    config.backend = window::Backend::D3D11;

    ASSERT(config.native_handle == reinterpret_cast<void*>(0x12345678));
    ASSERT(config.native_display == reinterpret_cast<void*>(0x87654321));
    ASSERT_EQ(config.width, 1280);
    ASSERT_EQ(config.height, 720);
    ASSERT(config.vsync == false);
    ASSERT_EQ(config.samples, 4);
    ASSERT(config.backend == window::Backend::D3D11);
}

TEST(external_graphics_null_handle) {
    // Creating graphics with null handle should fail
    window::ExternalWindowConfig config;
    config.width = 800;
    config.height = 600;
    // native_handle is nullptr

    window::Result result;
    window::Graphics* gfx = window::Graphics::create(config, &result);

    ASSERT(gfx == nullptr);
    ASSERT(result == window::Result::ErrorInvalidParameter);
}

TEST(external_graphics_invalid_size) {
    // Creating graphics with invalid size should fail
    window::ExternalWindowConfig config;
    config.native_handle = reinterpret_cast<void*>(0x12345678);  // Fake handle
    config.width = 0;  // Invalid
    config.height = 600;

    window::Result result;
    window::Graphics* gfx = window::Graphics::create(config, &result);

    ASSERT(gfx == nullptr);
    ASSERT(result == window::Result::ErrorInvalidParameter);
}

//=============================================================================
// Tests for enum values
//=============================================================================

TEST(result_enum_values) {
    // Ensure Result::Success is 0
    ASSERT_EQ(static_cast<int>(window::Result::Success), 0);

    // Ensure all error codes are distinct
    ASSERT(window::Result::ErrorUnknown != window::Result::Success);
    ASSERT(window::Result::ErrorPlatformInit != window::Result::ErrorUnknown);
    ASSERT(window::Result::ErrorWindowCreation != window::Result::ErrorPlatformInit);
    ASSERT(window::Result::ErrorGraphicsInit != window::Result::ErrorWindowCreation);
    ASSERT(window::Result::ErrorNotSupported != window::Result::ErrorGraphicsInit);
    ASSERT(window::Result::ErrorInvalidParameter != window::Result::ErrorNotSupported);
    ASSERT(window::Result::ErrorOutOfMemory != window::Result::ErrorInvalidParameter);
    ASSERT(window::Result::ErrorDeviceLost != window::Result::ErrorOutOfMemory);
}

TEST(backend_enum_values) {
    // Ensure Backend::Auto is 0
    ASSERT_EQ(static_cast<int>(window::Backend::Auto), 0);

    // Ensure all backends are distinct
    ASSERT(window::Backend::OpenGL != window::Backend::Auto);
    ASSERT(window::Backend::Vulkan != window::Backend::OpenGL);
    ASSERT(window::Backend::D3D11 != window::Backend::Vulkan);
    ASSERT(window::Backend::D3D12 != window::Backend::D3D11);
    ASSERT(window::Backend::Metal != window::Backend::D3D12);
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== Window Library Unit Tests ===\n\n");

    // Utility function tests
    RUN_TEST(result_to_string);
    RUN_TEST(backend_to_string);
    RUN_TEST(is_backend_supported);
    RUN_TEST(get_default_backend);

    // Config tests
    RUN_TEST(config_defaults);
    RUN_TEST(config_custom);

    // ExternalWindowConfig tests
    RUN_TEST(external_config_defaults);
    RUN_TEST(external_config_custom);
    RUN_TEST(external_graphics_null_handle);
    RUN_TEST(external_graphics_invalid_size);

    // Enum tests
    RUN_TEST(result_enum_values);
    RUN_TEST(backend_enum_values);

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
