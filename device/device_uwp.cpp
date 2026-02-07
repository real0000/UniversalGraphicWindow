/*
 * device_uwp.cpp - UWP device and monitor enumeration
 * Uses DXGI for GPU enumeration, DisplayInformation for monitors
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_UWP)

#include <dxgi1_4.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <cstring>
#include "../internal/utf8_util.hpp"

#pragma comment(lib, "dxgi.lib")

using namespace winrt;
using namespace Windows::Graphics::Display;

namespace window {

//=============================================================================
// Device Enumeration
//=============================================================================

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    out_devices->device_count = 0;

    // Create DXGI factory
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) {
        return 0;
    }

    // Enumerate adapters
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND && out_devices->device_count < MAX_DEVICES; i++) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            adapter->Release();
            continue;
        }

        GraphicsDeviceInfo& device = out_devices->devices[out_devices->device_count];

        // Convert wide string to narrow
        internal::wide_to_utf8(desc.Description, device.name, MAX_DEVICE_NAME_LENGTH);

        // Determine vendor name
        switch (desc.VendorId) {
            case 0x10DE: strncpy(device.vendor, "NVIDIA", MAX_DEVICE_NAME_LENGTH - 1); break;
            case 0x1002: strncpy(device.vendor, "AMD", MAX_DEVICE_NAME_LENGTH - 1); break;
            case 0x8086: strncpy(device.vendor, "Intel", MAX_DEVICE_NAME_LENGTH - 1); break;
            case 0x1414: strncpy(device.vendor, "Microsoft", MAX_DEVICE_NAME_LENGTH - 1); break;
            default: strncpy(device.vendor, "Unknown", MAX_DEVICE_NAME_LENGTH - 1); break;
        }

        device.device_id = desc.DeviceId;
        device.vendor_id = desc.VendorId;
        device.dedicated_video_memory = desc.DedicatedVideoMemory;
        device.dedicated_system_memory = desc.DedicatedSystemMemory;
        device.shared_system_memory = desc.SharedSystemMemory;
        device.device_index = static_cast<int>(i);
        device.is_default = (i == 0);

        // Set backend based on filter
        if (backend == Backend::Auto) {
            device.backend = get_default_backend();
        } else {
            device.backend = backend;
        }

        out_devices->device_count++;
        adapter->Release();
    }

    factory->Release();
    return out_devices->device_count;
}

//=============================================================================
// Monitor Enumeration
//=============================================================================

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

    try {
        // In UWP, we primarily work with DisplayInformation for the current view
        // Full multi-monitor enumeration requires DisplayMonitor API (Windows 10 1903+)
        DisplayInformation display_info = DisplayInformation::GetForCurrentView();

        MonitorInfo& monitor = out_monitors->monitors[0];

        // Get display name (UWP doesn't expose monitor names easily)
        strncpy(monitor.name, "Primary Display", MAX_DEVICE_NAME_LENGTH - 1);

        // Position is always 0,0 for the primary/current display in UWP
        monitor.x = 0;
        monitor.y = 0;

        // Get screen dimensions
        // Note: In UWP, we work with raw pixels, accounting for DPI scaling
        auto raw_pixels_per_view_pixel = display_info.RawPixelsPerViewPixel();
        auto screen_width_raw = static_cast<int>(display_info.ScreenWidthInRawPixels());
        auto screen_height_raw = static_cast<int>(display_info.ScreenHeightInRawPixels());

        monitor.width = screen_width_raw;
        monitor.height = screen_height_raw;

        // Refresh rate from ResolutionScale (approximate)
        // UWP doesn't directly expose refresh rate, default to 60
        monitor.refresh_rate = 60;

        monitor.is_primary = true;
        monitor.monitor_index = 0;

        // Add current resolution as a display mode
        monitor.mode_count = 1;
        monitor.modes[0].width = screen_width_raw;
        monitor.modes[0].height = screen_height_raw;
        monitor.modes[0].refresh_rate = 60;
        monitor.modes[0].bits_per_pixel = 32;
        monitor.modes[0].is_native = true;

        // Try to add common resolutions that fit within the screen
        int common_widths[] = { 1920, 1600, 1280, 1024, 800 };
        int common_heights[] = { 1080, 900, 720, 768, 600 };

        for (int i = 0; i < 5 && monitor.mode_count < MAX_DISPLAY_MODES; i++) {
            if (common_widths[i] <= screen_width_raw && common_heights[i] <= screen_height_raw) {
                // Avoid duplicates
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
    }
    catch (...) {
        // If DisplayInformation fails, return 0 monitors
        return 0;
    }

    return out_monitors->monitor_count;
}

} // namespace window

#endif // WINDOW_PLATFORM_UWP
