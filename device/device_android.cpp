/*
 * device_android.cpp - Android device and monitor enumeration
 * Uses EGL for GPU enumeration, DisplayMetrics for screen info
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_ANDROID)

#include <android/native_window.h>
#include <android/native_activity.h>
#include <android/configuration.h>
#include <EGL/egl.h>
#include <cstring>
#include <cstdlib>

namespace window {

//=============================================================================
// Device Enumeration
//=============================================================================

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    out_devices->device_count = 0;

    // On Android, we use EGL to get GPU info
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return 0;
    }

    EGLint majorVersion, minorVersion;
    if (!eglInitialize(display, &majorVersion, &minorVersion)) {
        return 0;
    }

    GraphicsDeviceInfo& device = out_devices->devices[0];

    // Get vendor and renderer strings
    const char* vendor = eglQueryString(display, EGL_VENDOR);
    const char* renderer = eglQueryString(display, EGL_EXTENSIONS);

    if (vendor) {
        strncpy(device.vendor, vendor, MAX_DEVICE_NAME_LENGTH - 1);

        // Try to determine vendor ID from name
        if (strstr(vendor, "Qualcomm") || strstr(vendor, "Adreno")) {
            device.vendor_id = 0x5143; // Qualcomm
        } else if (strstr(vendor, "ARM") || strstr(vendor, "Mali")) {
            device.vendor_id = 0x13B5; // ARM
        } else if (strstr(vendor, "Imagination") || strstr(vendor, "PowerVR")) {
            device.vendor_id = 0x1010; // Imagination Technologies
        } else if (strstr(vendor, "NVIDIA")) {
            device.vendor_id = 0x10DE;
        } else {
            device.vendor_id = 0;
        }
    } else {
        strncpy(device.vendor, "Unknown", MAX_DEVICE_NAME_LENGTH - 1);
    }

    // Create a device name from vendor
    if (device.vendor_id == 0x5143) {
        strncpy(device.name, "Qualcomm Adreno GPU", MAX_DEVICE_NAME_LENGTH - 1);
    } else if (device.vendor_id == 0x13B5) {
        strncpy(device.name, "ARM Mali GPU", MAX_DEVICE_NAME_LENGTH - 1);
    } else if (device.vendor_id == 0x1010) {
        strncpy(device.name, "Imagination PowerVR GPU", MAX_DEVICE_NAME_LENGTH - 1);
    } else if (device.vendor_id == 0x10DE) {
        strncpy(device.name, "NVIDIA Tegra GPU", MAX_DEVICE_NAME_LENGTH - 1);
    } else {
        strncpy(device.name, "Mobile GPU", MAX_DEVICE_NAME_LENGTH - 1);
    }

    device.device_id = 0;
    device.dedicated_video_memory = 0; // Shared memory on mobile
    device.dedicated_system_memory = 0;
    device.shared_system_memory = 0;
    device.device_index = 0;
    device.is_default = true;

    if (backend == Backend::Auto) {
        device.backend = get_default_backend();
    } else {
        device.backend = backend;
    }

    out_devices->device_count = 1;

    eglTerminate(display);

    return out_devices->device_count;
}

//=============================================================================
// Monitor Enumeration
//=============================================================================

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

    // On Android, we typically have one display
    // Full display info requires JNI access to WindowManager
    MonitorInfo& monitor = out_monitors->monitors[0];

    strncpy(monitor.name, "Android Display", MAX_DEVICE_NAME_LENGTH - 1);

    // Default values - will be updated when window is created
    // and we have access to ANativeWindow
    monitor.x = 0;
    monitor.y = 0;

    // Try to get screen dimensions from environment or use defaults
    const char* width_str = getenv("ANDROID_DISPLAY_WIDTH");
    const char* height_str = getenv("ANDROID_DISPLAY_HEIGHT");

    if (width_str && height_str) {
        monitor.width = atoi(width_str);
        monitor.height = atoi(height_str);
    } else {
        // Common default for modern Android devices
        monitor.width = 1080;
        monitor.height = 1920;
    }

    monitor.refresh_rate = 60;
    monitor.is_primary = true;
    monitor.monitor_index = 0;

    // Single native mode
    monitor.mode_count = 1;
    monitor.modes[0].width = monitor.width;
    monitor.modes[0].height = monitor.height;
    monitor.modes[0].refresh_rate = 60;
    monitor.modes[0].bits_per_pixel = 32;
    monitor.modes[0].is_native = true;

    out_monitors->monitor_count = 1;

    return out_monitors->monitor_count;
}

} // namespace window

#endif // WINDOW_PLATFORM_ANDROID
