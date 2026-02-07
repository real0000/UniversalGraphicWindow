/*
 * graphics_config.cpp - Graphics configuration example
 *
 * This example demonstrates:
 *   - Enumerating graphics devices
 *   - Enumerating monitors and display modes
 *   - Saving/loading graphics configuration
 *   - Creating a window from a config file
 *   - Creating multiple windows from a config
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>

void print_devices() {
    printf("=== Graphics Devices ===\n\n");

    window::DeviceEnumeration devices;
    int count = window::enumerate_devices(window::Backend::Auto, &devices);

    if (count == 0) {
        printf("No graphics devices found.\n");
        return;
    }

    for (int i = 0; i < devices.device_count; i++) {
        const window::GraphicsDeviceInfo& dev = devices.devices[i];
        printf("[%d] %s%s\n", i, dev.name.c_str(), dev.is_default ? " (Default)" : "");
        printf("    Vendor: %s (0x%04X)\n", dev.vendor.c_str(), dev.vendor_id);
        printf("    Device ID: 0x%04X\n", dev.device_id);
        printf("    Dedicated VRAM: %.0f MB\n", dev.dedicated_video_memory / (1024.0 * 1024.0));
        printf("    Shared Memory: %.0f MB\n", dev.shared_system_memory / (1024.0 * 1024.0));
        printf("\n");
    }
}

void print_monitors() {
    printf("=== Monitors ===\n\n");

    window::MonitorEnumeration monitors;
    int count = window::enumerate_monitors(&monitors);

    if (count == 0) {
        printf("No monitors found.\n");
        return;
    }

    for (int i = 0; i < monitors.monitor_count; i++) {
        const window::MonitorInfo& mon = monitors.monitors[i];
        printf("[%d] %s%s\n", i, mon.name.c_str(), mon.is_primary ? " (Primary)" : "");
        printf("    Position: %d, %d\n", mon.x, mon.y);
        printf("    Resolution: %dx%d @ %d Hz\n", mon.width, mon.height, mon.refresh_rate);
        printf("    Display Modes: %d\n", mon.mode_count);

        // Print a few common resolutions
        printf("    Common resolutions:\n");
        int common_widths[] = {1920, 2560, 3840, 1280, 1600};
        int common_heights[] = {1080, 1440, 2160, 720, 900};
        for (int j = 0; j < 5; j++) {
            window::DisplayMode mode;
            if (window::find_display_mode(mon, common_widths[j], common_heights[j], 0, &mode)) {
                printf("      %dx%d @ %d Hz%s\n", mode.width, mode.height, mode.refresh_rate,
                       mode.is_native ? " (Native)" : "");
            }
        }
        printf("\n");
    }
}

void demo_config_save_load() {
    printf("=== Config Save/Load Demo ===\n\n");

    // Create a custom config
    window::GraphicsConfig config;
    config.windows[0].title = "My Game";
    config.windows[0].width = 1280;
    config.windows[0].height = 720;
    config.windows[0].fullscreen = false;
    config.vsync = true;
    config.samples = 4;
    config.backend = window::Backend::Auto;

    // Save it
    const char* config_file = "game_config.ini";
    if (config.save(config_file)) {
        printf("Configuration saved to %s\n", config_file);
    } else {
        printf("Failed to save configuration!\n");
        return;
    }

    // Load it back
    window::GraphicsConfig loaded_config;
    if (window::GraphicsConfig::load(config_file, &loaded_config)) {
        printf("Configuration loaded successfully:\n");
        printf("  Title: %s\n", loaded_config.windows[0].title.c_str());
        printf("  Resolution: %dx%d\n", loaded_config.windows[0].width, loaded_config.windows[0].height);
        printf("  Fullscreen: %s\n", loaded_config.windows[0].fullscreen ? "true" : "false");
        printf("  VSync: %s\n", loaded_config.vsync ? "true" : "false");
        printf("  MSAA: %dx\n", loaded_config.samples);
        printf("  Backend: %s\n", window::backend_to_string(loaded_config.backend));
        printf("  Window count: %d\n", loaded_config.window_count);
    } else {
        printf("Failed to load configuration!\n");
    }

    printf("\n");
}

void demo_window_from_config() {
    printf("=== Window from Config Demo ===\n\n");

    const char* config_file = "game_config.ini";

    // Create window from config file
    window::Result result;
    auto windows = window::Window::create_from_config(config_file, &result);

    if (windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return;
    }

    window::Window* win = windows[0];
    printf("Window created successfully!\n");
    printf("  Title: %s\n", win->get_title());
    printf("  Size: %dx%d\n", win->get_width(), win->get_height());
    printf("  Backend: %s\n", win->graphics()->get_backend_name());
    printf("  Device: %s\n", win->graphics()->get_device_name());

    // Run for a few frames
    int frames = 0;
    while (!win->should_close() && frames < 60) {
        win->poll_events();
        win->graphics()->present();
        frames++;
    }

    win->destroy();
    printf("\nWindow closed after %d frames.\n", frames);
}

void demo_multi_window() {
    printf("=== Multi-Window Demo ===\n\n");

    // Create a config with multiple windows
    window::GraphicsConfig config;
    config.backend = window::Backend::Auto;
    config.vsync = true;
    config.samples = 1;

    // First window (already exists by default)
    config.windows[0].name = "main";
    config.windows[0].title = "Main Window";
    config.windows[0].x = 100;
    config.windows[0].y = 100;
    config.windows[0].width = 800;
    config.windows[0].height = 600;

    // Add second window
    window::WindowConfigEntry secondary;
    secondary.name = "secondary";
    secondary.title = "Secondary Window";
    secondary.x = 950;
    secondary.y = 100;
    secondary.width = 640;
    secondary.height = 480;
    config.add_window(secondary);

    // Save the multi-window config
    config.save("multi_window_config.ini");
    printf("Saved multi-window configuration.\n");

    // Create windows
    window::Result result;
    std::vector<window::Window*> windows = window::Window::create(config, &result);

    if (windows.empty()) {
        printf("Failed to create windows: %s\n", window::result_to_string(result));
        return;
    }

    printf("Created %zu windows:\n", windows.size());
    for (size_t i = 0; i < windows.size(); i++) {
        printf("  [%zu] %s (%dx%d)\n", i, windows[i]->get_title(),
               windows[i]->get_width(), windows[i]->get_height());
    }

    // Run for a few frames
    int frames = 0;
    bool any_open = true;
    while (any_open && frames < 120) {
        any_open = false;
        for (window::Window* w : windows) {
            w->poll_events();
            if (!w->should_close()) {
                any_open = true;
                w->graphics()->present();
            }
        }
        frames++;
    }

    // Cleanup
    for (window::Window* w : windows) {
        w->destroy();
    }

    printf("\nWindows closed after %d frames.\n", frames);
}

int main() {
    printf("Graphics Configuration Example\n");
    printf("==============================\n\n");

    print_devices();
    print_monitors();
    demo_config_save_load();
    demo_window_from_config();
    demo_multi_window();

    printf("\nDone.\n");
    return 0;
}
