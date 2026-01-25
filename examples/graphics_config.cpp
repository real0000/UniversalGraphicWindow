/*
 * graphics_config.cpp - Graphics configuration example
 *
 * This example demonstrates:
 *   - Enumerating graphics devices
 *   - Enumerating monitors and display modes
 *   - Saving/loading graphics configuration
 *   - Creating a window from a config file
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
        printf("[%d] %s%s\n", i, dev.name, dev.is_default ? " (Default)" : "");
        printf("    Vendor: %s (0x%04X)\n", dev.vendor, dev.vendor_id);
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
        printf("[%d] %s%s\n", i, mon.name, mon.is_primary ? " (Primary)" : "");
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
    strncpy(config.title, "My Game", window::MAX_DEVICE_NAME_LENGTH - 1);
    config.window_width = 1280;
    config.window_height = 720;
    config.fullscreen = false;
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
        printf("  Title: %s\n", loaded_config.title);
        printf("  Resolution: %dx%d\n", loaded_config.window_width, loaded_config.window_height);
        printf("  Fullscreen: %s\n", loaded_config.fullscreen ? "true" : "false");
        printf("  VSync: %s\n", loaded_config.vsync ? "true" : "false");
        printf("  MSAA: %dx\n", loaded_config.samples);
        printf("  Backend: %s\n", window::backend_to_string(loaded_config.backend));
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
    window::Window* win = window::Window::create_from_config(config_file, &result);

    if (!win) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return;
    }

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

int main() {
    printf("Graphics Configuration Example\n");
    printf("==============================\n\n");

    print_devices();
    print_monitors();
    demo_config_save_load();
    demo_window_from_config();

    printf("\nDone.\n");
    return 0;
}
