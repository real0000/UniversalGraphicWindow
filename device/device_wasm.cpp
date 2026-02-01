/*
 * device_wasm.cpp - Device and monitor enumeration for WebAssembly/Emscripten
 */

#ifdef WINDOW_PLATFORM_WASM

#include "window.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

namespace window {

//=============================================================================
// Device Enumeration
//=============================================================================

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    memset(out_devices, 0, sizeof(DeviceEnumeration));

    // In WebGL, there's typically just one "device" - the browser's WebGL implementation
    if (backend == Backend::Auto || backend == Backend::OpenGL) {
        DeviceInfo& device = out_devices->devices[0];
        device.device_index = 0;

        // Get device info via JavaScript
        const char* renderer = (const char*)EM_ASM_PTR({
            try {
                var canvas = document.createElement('canvas');
                var gl = canvas.getContext('webgl2') || canvas.getContext('webgl');
                if (gl) {
                    var debugInfo = gl.getExtension('WEBGL_debug_renderer_info');
                    if (debugInfo) {
                        var renderer = gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL);
                        var len = lengthBytesUTF8(renderer) + 1;
                        var ptr = _malloc(len);
                        stringToUTF8(renderer, ptr, len);
                        return ptr;
                    }
                }
            } catch(e) {}
            var fallback = "WebGL";
            var len = lengthBytesUTF8(fallback) + 1;
            var ptr = _malloc(len);
            stringToUTF8(fallback, ptr, len);
            return ptr;
        });

        if (renderer) {
            strncpy(device.name, renderer, MAX_DEVICE_NAME_LENGTH - 1);
            device.name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';
            free((void*)renderer);
        } else {
            strncpy(device.name, "WebGL", MAX_DEVICE_NAME_LENGTH - 1);
        }

        device.vendor_id = 0;
        device.device_id = 0;
        device.is_discrete = true;  // Assume discrete GPU
        device.supports_vulkan = false;
        device.supports_opengl = true;
        device.supports_d3d11 = false;
        device.supports_d3d12 = false;
        device.supports_metal = false;

        // Estimate VRAM (not directly accessible in WebGL)
        device.dedicated_video_memory = 0;
        device.dedicated_system_memory = 0;
        device.shared_system_memory = 0;

        out_devices->device_count = 1;
    }

    return out_devices->device_count;
}

//=============================================================================
// Monitor Enumeration
//=============================================================================

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    memset(out_monitors, 0, sizeof(MonitorEnumeration));

    // In the browser, we have one "monitor" - the screen
    MonitorInfo& monitor = out_monitors->monitors[0];

    // Get screen info via JavaScript
    int screen_width = EM_ASM_INT({
        return window.screen.width;
    });

    int screen_height = EM_ASM_INT({
        return window.screen.height;
    });

    double device_pixel_ratio = EM_ASM_DOUBLE({
        return window.devicePixelRatio || 1.0;
    });

    strncpy(monitor.name, "Browser Window", MAX_DEVICE_NAME_LENGTH - 1);
    monitor.name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';

    monitor.x = 0;
    monitor.y = 0;
    monitor.width = screen_width;
    monitor.height = screen_height;
    monitor.work_x = 0;
    monitor.work_y = 0;
    monitor.work_width = screen_width;
    monitor.work_height = screen_height;
    monitor.is_primary = true;

    // DPI from device pixel ratio
    monitor.dpi_x = static_cast<int>(96.0 * device_pixel_ratio);
    monitor.dpi_y = static_cast<int>(96.0 * device_pixel_ratio);
    monitor.scale_factor = static_cast<float>(device_pixel_ratio);

    // Add display mode
    monitor.mode_count = 1;
    monitor.modes[0].width = screen_width;
    monitor.modes[0].height = screen_height;
    monitor.modes[0].refresh_rate = 60;  // Assume 60 Hz
    monitor.modes[0].bits_per_pixel = 32;

    // Check for additional common resolutions that might be available
    struct CommonMode {
        int width;
        int height;
    };
    static const CommonMode common_modes[] = {
        {1920, 1080},
        {1280, 720},
        {1600, 900},
        {2560, 1440},
        {3840, 2160},
    };

    for (const auto& mode : common_modes) {
        if (mode.width <= screen_width && mode.height <= screen_height &&
            monitor.mode_count < MAX_DISPLAY_MODES) {
            bool duplicate = false;
            for (int i = 0; i < monitor.mode_count; i++) {
                if (monitor.modes[i].width == mode.width &&
                    monitor.modes[i].height == mode.height) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                DisplayMode& dm = monitor.modes[monitor.mode_count];
                dm.width = mode.width;
                dm.height = mode.height;
                dm.refresh_rate = 60;
                dm.bits_per_pixel = 32;
                monitor.mode_count++;
            }
        }
    }

    out_monitors->monitor_count = 1;
    return 1;
}

} // namespace window

#endif // WINDOW_PLATFORM_WASM
