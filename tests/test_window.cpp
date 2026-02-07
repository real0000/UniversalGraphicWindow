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

#define ASSERT_STR_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED at line %d: \"%s\" != \"%s\"\n", __LINE__, (a).c_str(), (b)); \
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

    ASSERT_STR_EQ(config.windows[0].title, "Window");
    ASSERT_EQ(config.windows[0].width, 800);
    ASSERT_EQ(config.windows[0].height, 600);
    ASSERT_EQ(config.windows[0].x, -1);
    ASSERT_EQ(config.windows[0].y, -1);
    ASSERT(config.windows[0].visible == true);
    ASSERT(config.vsync == true);
    ASSERT_EQ(config.samples, 1);
    ASSERT_EQ(config.color_bits, 32);
    ASSERT_EQ(config.depth_bits, 24);
    ASSERT_EQ(config.stencil_bits, 8);
    ASSERT_EQ(config.back_buffers, 2);
    ASSERT(config.backend == window::Backend::Auto);
    ASSERT(config.shared_graphics == nullptr);
}

TEST(config_custom) {
    window::Config config;
    config.windows[0].title = "Custom Title";
    config.windows[0].width = 1920;
    config.windows[0].height = 1080;
    config.windows[0].x = 100;
    config.windows[0].y = 200;
    config.windows[0].visible = false;
    config.vsync = false;
    config.samples = 4;
    config.backend = window::Backend::OpenGL;

    ASSERT_STR_EQ(config.windows[0].title, "Custom Title");
    ASSERT_EQ(config.windows[0].width, 1920);
    ASSERT_EQ(config.windows[0].height, 1080);
    ASSERT_EQ(config.windows[0].x, 100);
    ASSERT_EQ(config.windows[0].y, 200);
    ASSERT(config.windows[0].visible == false);
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
// Tests for WindowStyle flags
//=============================================================================

TEST(window_style_defaults) {
    // Check that WindowStyle::Default has expected flags
    window::WindowStyle def = window::WindowStyle::Default;

    ASSERT(window::has_style(def, window::WindowStyle::TitleBar));
    ASSERT(window::has_style(def, window::WindowStyle::Border));
    ASSERT(window::has_style(def, window::WindowStyle::CloseButton));
    ASSERT(window::has_style(def, window::WindowStyle::MinimizeButton));
    ASSERT(window::has_style(def, window::WindowStyle::MaximizeButton));
    ASSERT(window::has_style(def, window::WindowStyle::Resizable));
    ASSERT(!window::has_style(def, window::WindowStyle::Fullscreen));
    ASSERT(!window::has_style(def, window::WindowStyle::AlwaysOnTop));
}

TEST(window_style_operators) {
    using namespace window;

    // Test OR operator
    WindowStyle style = WindowStyle::TitleBar | WindowStyle::Border;
    ASSERT(has_style(style, WindowStyle::TitleBar));
    ASSERT(has_style(style, WindowStyle::Border));
    ASSERT(!has_style(style, WindowStyle::Resizable));

    // Test AND operator
    WindowStyle masked = style & WindowStyle::TitleBar;
    ASSERT(has_style(masked, WindowStyle::TitleBar));
    ASSERT(!has_style(masked, WindowStyle::Border));

    // Test NOT operator
    WindowStyle inverted = ~WindowStyle::TitleBar;
    ASSERT(!has_style(inverted, WindowStyle::TitleBar));

    // Test |= operator
    style |= WindowStyle::Resizable;
    ASSERT(has_style(style, WindowStyle::Resizable));

    // Test &= operator
    style &= ~WindowStyle::Border;
    ASSERT(!has_style(style, WindowStyle::Border));
}

TEST(window_style_combinations) {
    using namespace window;

    // Test Borderless
    ASSERT(WindowStyle::Borderless == WindowStyle::None);

    // Test FixedSize
    WindowStyle fixed = WindowStyle::FixedSize;
    ASSERT(has_style(fixed, WindowStyle::TitleBar));
    ASSERT(has_style(fixed, WindowStyle::Border));
    ASSERT(has_style(fixed, WindowStyle::CloseButton));
    ASSERT(has_style(fixed, WindowStyle::MinimizeButton));
    ASSERT(!has_style(fixed, WindowStyle::MaximizeButton));
    ASSERT(!has_style(fixed, WindowStyle::Resizable));

    // Test FullscreenBorderless
    WindowStyle fs = WindowStyle::FullscreenBorderless;
    ASSERT(has_style(fs, WindowStyle::Fullscreen));
    ASSERT(!has_style(fs, WindowStyle::TitleBar));
    ASSERT(!has_style(fs, WindowStyle::Border));
}

TEST(config_style_default) {
    window::Config config;

    // Default style should be Default
    ASSERT(config.windows[0].style == window::WindowStyle::Default);
}

//=============================================================================
// Tests for GraphicsConfig
//=============================================================================

TEST(graphics_config_defaults) {
    window::GraphicsConfig config;

    // Window defaults
    ASSERT_STR_EQ(config.windows[0].title, "Window");
    ASSERT_EQ(config.windows[0].x, -1);
    ASSERT_EQ(config.windows[0].y, -1);
    ASSERT_EQ(config.windows[0].width, 800);
    ASSERT_EQ(config.windows[0].height, 600);
    ASSERT(config.windows[0].style == window::WindowStyle::Default);
    ASSERT_EQ(config.windows[0].monitor_index, 0);
    ASSERT(config.windows[0].fullscreen == false);
    ASSERT_EQ(config.window_count, 1);

    // Graphics defaults
    ASSERT(config.backend == window::Backend::Auto);
    ASSERT_EQ(config.device_index, -1);
    ASSERT(config.vsync == true);
    ASSERT_EQ(config.samples, 1);
    ASSERT_EQ(config.back_buffers, 2);
    ASSERT_EQ(config.color_bits, 32);
    ASSERT_EQ(config.depth_bits, 24);
    ASSERT_EQ(config.stencil_bits, 8);
}

TEST(graphics_config_alias) {
    // GraphicsConfig is now an alias for Config
    window::GraphicsConfig config;
    config.windows[0].width = 1920;
    config.windows[0].height = 1080;
    config.vsync = false;
    config.samples = 4;
    config.backend = window::Backend::OpenGL;

    // Config and GraphicsConfig should be the same type
    window::Config& config_ref = config;

    ASSERT_EQ(config_ref.windows[0].width, 1920);
    ASSERT_EQ(config_ref.windows[0].height, 1080);
    ASSERT(config_ref.vsync == false);
    ASSERT_EQ(config_ref.samples, 4);
    ASSERT(config_ref.backend == window::Backend::OpenGL);
}

TEST(graphics_config_validate) {
    window::GraphicsConfig config;

    // Set some invalid values
    config.windows[0].width = -1;
    config.windows[0].height = 0;
    config.samples = 7;  // Not power of 2
    config.back_buffers = 10;
    config.color_bits = 48;
    config.depth_bits = 12;
    config.stencil_bits = 4;

    // Validate should fix them
    bool all_valid = config.validate();

    ASSERT(all_valid == false);  // Should return false since we had invalid values
    ASSERT_EQ(config.windows[0].width, 800);
    ASSERT_EQ(config.windows[0].height, 600);
    ASSERT_EQ(config.samples, 1);
    ASSERT_EQ(config.back_buffers, 2);
    ASSERT_EQ(config.color_bits, 32);
    ASSERT_EQ(config.depth_bits, 24);
    ASSERT_EQ(config.stencil_bits, 8);
}

TEST(graphics_config_save_load) {
    // Create a config
    window::GraphicsConfig save_config;
    save_config.windows[0].title = "Test Window";
    save_config.windows[0].width = 1280;
    save_config.windows[0].height = 720;
    save_config.vsync = false;
    save_config.samples = 4;
    save_config.backend = window::Backend::D3D11;
    save_config.windows[0].fullscreen = true;

    // Save to temp file
    const char* temp_file = "test_config.ini";
    bool save_result = save_config.save(temp_file);
    ASSERT(save_result == true);

    // Load it back
    window::GraphicsConfig load_config;
    bool load_result = window::GraphicsConfig::load(temp_file, &load_config);
    ASSERT(load_result == true);

    // Verify values match
    ASSERT_STR_EQ(load_config.windows[0].title, "Test Window");
    ASSERT_EQ(load_config.windows[0].width, 1280);
    ASSERT_EQ(load_config.windows[0].height, 720);
    ASSERT(load_config.vsync == false);
    ASSERT_EQ(load_config.samples, 4);
    ASSERT(load_config.backend == window::Backend::D3D11);
    ASSERT(load_config.windows[0].fullscreen == true);

    // Cleanup
    remove(temp_file);
}

TEST(graphics_config_load_nonexistent) {
    window::GraphicsConfig config;
    bool result = window::GraphicsConfig::load("nonexistent_file.ini", &config);
    ASSERT(result == false);
}

//=============================================================================
// Tests for Device/Monitor Enumeration
//=============================================================================

TEST(enumerate_devices) {
    window::DeviceEnumeration devices;
    int count = window::enumerate_devices(window::Backend::Auto, &devices);

    // Should find at least one device on a Windows system
    ASSERT(count >= 0);  // Might be 0 on systems without GPU
    ASSERT_EQ(count, devices.device_count);

    // If devices found, check they have valid data
    for (int i = 0; i < devices.device_count; i++) {
        ASSERT(devices.devices[i].name[0] != '\0');  // Name should not be empty
        ASSERT(devices.devices[i].device_index == i);
    }
}

TEST(enumerate_monitors) {
    window::MonitorEnumeration monitors;
    int count = window::enumerate_monitors(&monitors);

    // Should find at least one monitor
    ASSERT(count >= 1);
    ASSERT_EQ(count, monitors.monitor_count);

    // Check for primary monitor
    bool found_primary = false;
    for (int i = 0; i < monitors.monitor_count; i++) {
        if (monitors.monitors[i].is_primary) {
            found_primary = true;
        }
        // Each monitor should have at least one display mode
        ASSERT(monitors.monitors[i].mode_count >= 1);
    }
    ASSERT(found_primary == true);
}

TEST(get_primary_monitor) {
    window::MonitorInfo monitor;
    bool result = window::get_primary_monitor(&monitor);

    ASSERT(result == true);
    ASSERT(monitor.is_primary == true);
    ASSERT(monitor.width > 0);
    ASSERT(monitor.height > 0);
}

TEST(find_display_mode) {
    window::MonitorInfo monitor;
    if (!window::get_primary_monitor(&monitor)) {
        // Skip test if no monitor available
        return;
    }

    // Find the native resolution
    window::DisplayMode mode;
    bool found = window::find_display_mode(monitor, monitor.width, monitor.height, 0, &mode);

    ASSERT(found == true);
    ASSERT_EQ(mode.width, monitor.width);
    ASSERT_EQ(mode.height, monitor.height);
}

//=============================================================================
// Tests for Event System
//=============================================================================

TEST(key_enum_values) {
    using namespace window;

    // Letters should match ASCII
    ASSERT_EQ(static_cast<int>(Key::A), 'A');
    ASSERT_EQ(static_cast<int>(Key::Z), 'Z');

    // Numbers should match ASCII
    ASSERT_EQ(static_cast<int>(Key::Num0), '0');
    ASSERT_EQ(static_cast<int>(Key::Num9), '9');

    // Function keys should be sequential
    ASSERT_EQ(static_cast<int>(Key::F2), static_cast<int>(Key::F1) + 1);
    ASSERT_EQ(static_cast<int>(Key::F12), static_cast<int>(Key::F1) + 11);
}

TEST(key_mod_operators) {
    using namespace window;

    // Test OR operator
    KeyMod mods = KeyMod::Shift | KeyMod::Control;
    ASSERT(has_mod(mods, KeyMod::Shift));
    ASSERT(has_mod(mods, KeyMod::Control));
    ASSERT(!has_mod(mods, KeyMod::Alt));

    // Test AND operator
    KeyMod masked = mods & KeyMod::Shift;
    ASSERT(has_mod(masked, KeyMod::Shift));
    ASSERT(!has_mod(masked, KeyMod::Control));

    // Test None
    ASSERT(!has_mod(KeyMod::None, KeyMod::Shift));
    ASSERT(!has_mod(KeyMod::None, KeyMod::Control));
}

TEST(key_to_string) {
    using namespace window;

    ASSERT_STREQ(key_to_string(Key::A), "A");
    ASSERT_STREQ(key_to_string(Key::Z), "Z");
    ASSERT_STREQ(key_to_string(Key::Num0), "0");
    ASSERT_STREQ(key_to_string(Key::F1), "F1");
    ASSERT_STREQ(key_to_string(Key::F12), "F12");
    ASSERT_STREQ(key_to_string(Key::Escape), "Escape");
    ASSERT_STREQ(key_to_string(Key::Space), "Space");
    ASSERT_STREQ(key_to_string(Key::Enter), "Enter");
    ASSERT_STREQ(key_to_string(Key::LeftShift), "LeftShift");
    ASSERT_STREQ(key_to_string(Key::RightControl), "RightControl");
    ASSERT_STREQ(key_to_string(Key::Unknown), "Unknown");
}

TEST(mouse_button_to_string) {
    using namespace window;

    ASSERT_STREQ(mouse_button_to_string(MouseButton::Left), "Left");
    ASSERT_STREQ(mouse_button_to_string(MouseButton::Right), "Right");
    ASSERT_STREQ(mouse_button_to_string(MouseButton::Middle), "Middle");
    ASSERT_STREQ(mouse_button_to_string(MouseButton::X1), "X1");
    ASSERT_STREQ(mouse_button_to_string(MouseButton::X2), "X2");
    ASSERT_STREQ(mouse_button_to_string(MouseButton::Unknown), "Unknown");
}

TEST(event_type_to_string) {
    using namespace window;

    ASSERT_STREQ(event_type_to_string(EventType::None), "None");
    ASSERT_STREQ(event_type_to_string(EventType::WindowClose), "WindowClose");
    ASSERT_STREQ(event_type_to_string(EventType::WindowResize), "WindowResize");
    ASSERT_STREQ(event_type_to_string(EventType::KeyDown), "KeyDown");
    ASSERT_STREQ(event_type_to_string(EventType::KeyUp), "KeyUp");
    ASSERT_STREQ(event_type_to_string(EventType::MouseMove), "MouseMove");
    ASSERT_STREQ(event_type_to_string(EventType::MouseDown), "MouseDown");
    ASSERT_STREQ(event_type_to_string(EventType::MouseWheel), "MouseWheel");
    ASSERT_STREQ(event_type_to_string(EventType::DropFile), "DropFile");
}

TEST(event_structs_defaults) {
    using namespace window;

    // KeyEvent
    KeyEvent key_event;
    ASSERT(key_event.type == EventType::None);
    ASSERT(key_event.window == nullptr);
    ASSERT(key_event.key == Key::Unknown);
    ASSERT(key_event.modifiers == KeyMod::None);
    ASSERT(key_event.scancode == 0);
    ASSERT(key_event.repeat == false);

    // MouseButtonEvent
    MouseButtonEvent mouse_event;
    ASSERT(mouse_event.type == EventType::None);
    ASSERT(mouse_event.button == MouseButton::Unknown);
    ASSERT(mouse_event.x == 0);
    ASSERT(mouse_event.y == 0);
    ASSERT(mouse_event.clicks == 1);

    // MouseWheelEvent
    MouseWheelEvent scroll_event;
    ASSERT(scroll_event.dx == 0.0f);
    ASSERT(scroll_event.dy == 0.0f);

    // WindowResizeEvent
    WindowResizeEvent resize_event;
    ASSERT(resize_event.width == 0);
    ASSERT(resize_event.height == 0);
    ASSERT(resize_event.minimized == false);
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

    // WindowStyle tests
    RUN_TEST(window_style_defaults);
    RUN_TEST(window_style_operators);
    RUN_TEST(window_style_combinations);
    RUN_TEST(config_style_default);

    // GraphicsConfig tests
    RUN_TEST(graphics_config_defaults);
    RUN_TEST(graphics_config_alias);
    RUN_TEST(graphics_config_validate);
    RUN_TEST(graphics_config_save_load);
    RUN_TEST(graphics_config_load_nonexistent);

    // Device/Monitor enumeration tests
    RUN_TEST(enumerate_devices);
    RUN_TEST(enumerate_monitors);
    RUN_TEST(get_primary_monitor);
    RUN_TEST(find_display_mode);

    // Event system tests
    RUN_TEST(key_enum_values);
    RUN_TEST(key_mod_operators);
    RUN_TEST(key_to_string);
    RUN_TEST(mouse_button_to_string);
    RUN_TEST(event_type_to_string);
    RUN_TEST(event_structs_defaults);

    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
