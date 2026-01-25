/*
 * device_linux.cpp - Linux (X11/Wayland) device and monitor enumeration
 * Uses /sys/class/drm for GPU enumeration, Xrandr/Wayland protocols for monitors
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_X11) || defined(WINDOW_PLATFORM_WAYLAND)

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef WINDOW_PLATFORM_X11
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#endif

namespace window {

//=============================================================================
// Device Enumeration (Linux - uses /sys/class/drm)
//=============================================================================

// Helper to read a sysfs file
static bool read_sysfs_string(const char* path, char* buffer, size_t buffer_size) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    if (fgets(buffer, buffer_size, f)) {
        // Remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        fclose(f);
        return true;
    }

    fclose(f);
    return false;
}

static bool read_sysfs_hex(const char* path, uint32_t* value) {
    char buffer[64];
    if (!read_sysfs_string(path, buffer, sizeof(buffer))) return false;
    *value = (uint32_t)strtoul(buffer, nullptr, 16);
    return true;
}

static bool read_sysfs_dec(const char* path, uint64_t* value) {
    char buffer[64];
    if (!read_sysfs_string(path, buffer, sizeof(buffer))) return false;
    *value = strtoull(buffer, nullptr, 10);
    return true;
}

static const char* vendor_id_to_name(uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        default: return "Unknown";
    }
}

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    out_devices->device_count = 0;

    // Enumerate DRM devices from /sys/class/drm
    DIR* dir = opendir("/sys/class/drm");
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && out_devices->device_count < MAX_DEVICES) {
        // Look for card* entries (not renderD*)
        if (strncmp(entry->d_name, "card", 4) != 0) continue;
        if (strchr(entry->d_name, '-') != nullptr) continue; // Skip card0-HDMI-A-1 etc.

        char path[512];
        GraphicsDeviceInfo& device = out_devices->devices[out_devices->device_count];

        // Get device path
        snprintf(path, sizeof(path), "/sys/class/drm/%s/device", entry->d_name);

        // Check if device directory exists
        DIR* dev_dir = opendir(path);
        if (!dev_dir) continue;
        closedir(dev_dir);

        // Read vendor ID
        char vendor_path[512];
        snprintf(vendor_path, sizeof(vendor_path), "%s/vendor", path);
        if (!read_sysfs_hex(vendor_path, &device.vendor_id)) {
            device.vendor_id = 0;
        }

        // Read device ID
        char device_path[512];
        snprintf(device_path, sizeof(device_path), "%s/device", path);
        if (!read_sysfs_hex(device_path, &device.device_id)) {
            device.device_id = 0;
        }

        // Set vendor name
        strncpy(device.vendor, vendor_id_to_name(device.vendor_id), MAX_DEVICE_NAME_LENGTH - 1);

        // Try to get device name from various sources
        bool got_name = false;

        // Try debugfs first (requires root)
        char debugfs_path[512];
        snprintf(debugfs_path, sizeof(debugfs_path), "/sys/kernel/debug/dri/%s/name",
                 entry->d_name + 4); // Skip "card" prefix
        if (read_sysfs_string(debugfs_path, device.name, MAX_DEVICE_NAME_LENGTH)) {
            got_name = true;
        }

        // Try uevent for device name
        if (!got_name) {
            char uevent_path[512];
            snprintf(uevent_path, sizeof(uevent_path), "%s/uevent", path);
            FILE* f = fopen(uevent_path, "r");
            if (f) {
                char line[256];
                while (fgets(line, sizeof(line), f)) {
                    if (strncmp(line, "PCI_SLOT_NAME=", 14) == 0) {
                        // Use PCI slot as name fallback
                        snprintf(device.name, MAX_DEVICE_NAME_LENGTH, "%s GPU (%s)",
                                 device.vendor, line + 14);
                        // Remove newline
                        char* nl = strchr(device.name, '\n');
                        if (nl) *nl = '\0';
                        got_name = true;
                        break;
                    }
                }
                fclose(f);
            }
        }

        // Fallback name
        if (!got_name) {
            snprintf(device.name, MAX_DEVICE_NAME_LENGTH, "%s Graphics (%s)",
                     device.vendor, entry->d_name);
        }

        // Try to get memory info (may not be available)
        // NVIDIA and AMD expose this differently
        device.dedicated_video_memory = 0;
        device.dedicated_system_memory = 0;
        device.shared_system_memory = 0;

        // For AMD, try to read mem_info_vram_total
        char mem_path[512];
        snprintf(mem_path, sizeof(mem_path), "%s/mem_info_vram_total", path);
        read_sysfs_dec(mem_path, &device.dedicated_video_memory);

        // Set index
        device.device_index = out_devices->device_count;
        device.is_default = (out_devices->device_count == 0);

        // Set backend
        if (backend == Backend::Auto) {
            device.backend = get_default_backend();
        } else {
            device.backend = backend;
        }

        out_devices->device_count++;
    }

    closedir(dir);
    return out_devices->device_count;
}

//=============================================================================
// Monitor Enumeration
//=============================================================================

#ifdef WINDOW_PLATFORM_X11

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

    Display* display = XOpenDisplay(nullptr);
    if (!display) return 0;

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    // Check if XRandR is available
    int event_base, error_base;
    if (!XRRQueryExtension(display, &event_base, &error_base)) {
        // Fallback: use default screen info
        MonitorInfo& monitor = out_monitors->monitors[0];
        strncpy(monitor.name, "Default", MAX_DEVICE_NAME_LENGTH - 1);
        monitor.x = 0;
        monitor.y = 0;
        monitor.width = DisplayWidth(display, screen);
        monitor.height = DisplayHeight(display, screen);
        monitor.refresh_rate = 60;
        monitor.is_primary = true;
        monitor.monitor_index = 0;

        // Add single mode
        monitor.mode_count = 1;
        monitor.modes[0].width = monitor.width;
        monitor.modes[0].height = monitor.height;
        monitor.modes[0].refresh_rate = 60;
        monitor.modes[0].bits_per_pixel = DefaultDepth(display, screen);
        monitor.modes[0].is_native = true;

        out_monitors->monitor_count = 1;
        XCloseDisplay(display);
        return 1;
    }

    // Get screen resources
    XRRScreenResources* resources = XRRGetScreenResources(display, root);
    if (!resources) {
        XCloseDisplay(display);
        return 0;
    }

    // Find primary output
    RROutput primary_output = XRRGetOutputPrimary(display, root);

    // Enumerate outputs
    for (int i = 0; i < resources->noutput && out_monitors->monitor_count < MAX_MONITORS; i++) {
        XRROutputInfo* output = XRRGetOutputInfo(display, resources, resources->outputs[i]);
        if (!output) continue;

        // Only include connected outputs with a CRTC
        if (output->connection != RR_Connected || output->crtc == None) {
            XRRFreeOutputInfo(output);
            continue;
        }

        XRRCrtcInfo* crtc = XRRGetCrtcInfo(display, resources, output->crtc);
        if (!crtc) {
            XRRFreeOutputInfo(output);
            continue;
        }

        MonitorInfo& monitor = out_monitors->monitors[out_monitors->monitor_count];

        // Copy name
        strncpy(monitor.name, output->name, MAX_DEVICE_NAME_LENGTH - 1);

        // Position and size from CRTC
        monitor.x = crtc->x;
        monitor.y = crtc->y;
        monitor.width = crtc->width;
        monitor.height = crtc->height;

        // Check if primary
        monitor.is_primary = (resources->outputs[i] == primary_output);
        monitor.monitor_index = out_monitors->monitor_count;

        // Get current refresh rate
        monitor.refresh_rate = 60; // Default
        for (int m = 0; m < resources->nmode; m++) {
            if (resources->modes[m].id == crtc->mode) {
                if (resources->modes[m].hTotal && resources->modes[m].vTotal) {
                    monitor.refresh_rate = (int)(
                        (double)resources->modes[m].dotClock /
                        ((double)resources->modes[m].hTotal * (double)resources->modes[m].vTotal)
                    );
                }
                break;
            }
        }

        // Enumerate supported modes
        monitor.mode_count = 0;
        for (int m = 0; m < output->nmode && monitor.mode_count < MAX_DISPLAY_MODES; m++) {
            // Find mode info
            for (int r = 0; r < resources->nmode; r++) {
                if (resources->modes[r].id == output->modes[m]) {
                    XRRModeInfo& mode_info = resources->modes[r];

                    // Calculate refresh rate
                    int refresh = 60;
                    if (mode_info.hTotal && mode_info.vTotal) {
                        refresh = (int)(
                            (double)mode_info.dotClock /
                            ((double)mode_info.hTotal * (double)mode_info.vTotal)
                        );
                    }

                    // Check for duplicates
                    bool duplicate = false;
                    for (int d = 0; d < monitor.mode_count; d++) {
                        if (monitor.modes[d].width == (int)mode_info.width &&
                            monitor.modes[d].height == (int)mode_info.height &&
                            monitor.modes[d].refresh_rate == refresh) {
                            duplicate = true;
                            break;
                        }
                    }

                    if (!duplicate) {
                        DisplayMode& mode = monitor.modes[monitor.mode_count];
                        mode.width = mode_info.width;
                        mode.height = mode_info.height;
                        mode.refresh_rate = refresh;
                        mode.bits_per_pixel = DefaultDepth(display, screen);
                        mode.is_native = (mode.width == monitor.width && mode.height == monitor.height);
                        monitor.mode_count++;
                    }
                    break;
                }
            }
        }

        XRRFreeCrtcInfo(crtc);
        XRRFreeOutputInfo(output);
        out_monitors->monitor_count++;
    }

    XRRFreeScreenResources(resources);
    XCloseDisplay(display);

    return out_monitors->monitor_count;
}

#endif // WINDOW_PLATFORM_X11

#ifdef WINDOW_PLATFORM_WAYLAND

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

    // Wayland doesn't have a standard way to enumerate all monitors without
    // a running compositor connection. For now, provide a basic fallback.
    // Full enumeration requires wl_output events during compositor connection.

    // Try to get info from environment or return default
    MonitorInfo& monitor = out_monitors->monitors[0];
    strncpy(monitor.name, "Wayland Display", MAX_DEVICE_NAME_LENGTH - 1);
    monitor.x = 0;
    monitor.y = 0;

    // Try to get screen dimensions from environment
    const char* width_str = getenv("WAYLAND_DISPLAY_WIDTH");
    const char* height_str = getenv("WAYLAND_DISPLAY_HEIGHT");

    if (width_str && height_str) {
        monitor.width = atoi(width_str);
        monitor.height = atoi(height_str);
    } else {
        // Default to common resolution
        monitor.width = 1920;
        monitor.height = 1080;
    }

    monitor.refresh_rate = 60;
    monitor.is_primary = true;
    monitor.monitor_index = 0;

    // Add default mode
    monitor.mode_count = 1;
    monitor.modes[0].width = monitor.width;
    monitor.modes[0].height = monitor.height;
    monitor.modes[0].refresh_rate = 60;
    monitor.modes[0].bits_per_pixel = 32;
    monitor.modes[0].is_native = true;

    // Add common resolutions
    int common_widths[] = { 1920, 1600, 1280, 1024 };
    int common_heights[] = { 1080, 900, 720, 768 };

    for (int i = 0; i < 4 && monitor.mode_count < MAX_DISPLAY_MODES; i++) {
        if (common_widths[i] <= monitor.width && common_heights[i] <= monitor.height) {
            bool duplicate = false;
            for (int j = 0; j < monitor.mode_count; j++) {
                if (monitor.modes[j].width == common_widths[i] &&
                    monitor.modes[j].height == common_heights[i]) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                DisplayMode& mode = monitor.modes[monitor.mode_count];
                mode.width = common_widths[i];
                mode.height = common_heights[i];
                mode.refresh_rate = 60;
                mode.bits_per_pixel = 32;
                mode.is_native = false;
                monitor.mode_count++;
            }
        }
    }

    out_monitors->monitor_count = 1;
    return out_monitors->monitor_count;
}

#endif // WINDOW_PLATFORM_WAYLAND

} // namespace window

#endif // WINDOW_PLATFORM_X11 || WINDOW_PLATFORM_WAYLAND
