/*
 * device_win32.cpp - Win32 device and monitor enumeration
 * Uses DXGI for GPU enumeration, EnumDisplayMonitors for monitors
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <dxgi.h>
#include <cstring>
#include "../internal/utf8_util.hpp"

#pragma comment(lib, "dxgi.lib")

namespace window {

//=============================================================================
// Device Enumeration
//=============================================================================

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    out_devices->device_count = 0;

    // Create DXGI factory
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) {
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

// Callback data for monitor enumeration
struct MonitorEnumData {
    MonitorEnumeration* enumeration;
};

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    (void)hdcMonitor;
    (void)lprcMonitor;

    MonitorEnumData* data = reinterpret_cast<MonitorEnumData*>(dwData);

    if (data->enumeration->monitor_count >= MAX_MONITORS) {
        return FALSE;
    }

    MonitorInfo& monitor = data->enumeration->monitors[data->enumeration->monitor_count];

    MONITORINFOEXA monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFOEXA);
    if (GetMonitorInfoA(hMonitor, &monitorInfo)) {
        strncpy(monitor.name, monitorInfo.szDevice, MAX_DEVICE_NAME_LENGTH - 1);
        monitor.x = monitorInfo.rcMonitor.left;
        monitor.y = monitorInfo.rcMonitor.top;
        monitor.width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
        monitor.height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        monitor.is_primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
        monitor.monitor_index = data->enumeration->monitor_count;

        // Get current display settings for refresh rate
        DEVMODEA devMode;
        devMode.dmSize = sizeof(DEVMODEA);
        if (EnumDisplaySettingsA(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
            monitor.refresh_rate = static_cast<int>(devMode.dmDisplayFrequency);
        }

        // Enumerate display modes
        monitor.mode_count = 0;
        DEVMODEA modeInfo;
        modeInfo.dmSize = sizeof(DEVMODEA);

        for (DWORD modeNum = 0; EnumDisplaySettingsA(monitorInfo.szDevice, modeNum, &modeInfo) && monitor.mode_count < MAX_DISPLAY_MODES; modeNum++) {
            // Only include modes with 32-bit or 16-bit color
            if (modeInfo.dmBitsPerPel != 32 && modeInfo.dmBitsPerPel != 16) continue;

            // Check if this mode is already in the list (avoid duplicates)
            bool duplicate = false;
            for (int i = 0; i < monitor.mode_count; i++) {
                if (monitor.modes[i].width == static_cast<int>(modeInfo.dmPelsWidth) &&
                    monitor.modes[i].height == static_cast<int>(modeInfo.dmPelsHeight) &&
                    monitor.modes[i].refresh_rate == static_cast<int>(modeInfo.dmDisplayFrequency) &&
                    monitor.modes[i].bits_per_pixel == static_cast<int>(modeInfo.dmBitsPerPel)) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                DisplayMode& mode = monitor.modes[monitor.mode_count];
                mode.width = static_cast<int>(modeInfo.dmPelsWidth);
                mode.height = static_cast<int>(modeInfo.dmPelsHeight);
                mode.refresh_rate = static_cast<int>(modeInfo.dmDisplayFrequency);
                mode.bits_per_pixel = static_cast<int>(modeInfo.dmBitsPerPel);
                mode.is_native = (mode.width == monitor.width && mode.height == monitor.height);
                monitor.mode_count++;
            }
        }

        data->enumeration->monitor_count++;
    }

    return TRUE;
}

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

    MonitorEnumData data;
    data.enumeration = out_monitors;

    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&data));

    return out_monitors->monitor_count;
}

} // namespace window

#endif // WINDOW_PLATFORM_WIN32
