/*
 * device_apple.mm - macOS/iOS device and monitor enumeration
 * Uses IOKit for GPU enumeration, Core Graphics for monitors
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_MACOS) || defined(WINDOW_PLATFORM_IOS)

#import <Foundation/Foundation.h>

#if defined(WINDOW_PLATFORM_MACOS)
#import <Cocoa/Cocoa.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#else
#import <UIKit/UIKit.h>
#endif

#include <cstring>

namespace window {

//=============================================================================
// Device Enumeration
//=============================================================================

int enumerate_devices(Backend backend, DeviceEnumeration* out_devices) {
    if (!out_devices) return 0;

    out_devices->device_count = 0;

#if defined(WINDOW_PLATFORM_MACOS)
    // Use IOKit to enumerate GPUs on macOS
    io_iterator_t iterator;
    kern_return_t result = IOServiceGetMatchingServices(
        kIOMasterPortDefault,
        IOServiceMatching(kIOAcceleratorClassName),
        &iterator
    );

    if (result != KERN_SUCCESS) {
        return 0;
    }

    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL && out_devices->device_count < MAX_DEVICES) {
        GraphicsDeviceInfo& device = out_devices->devices[out_devices->device_count];

        // Get device properties
        CFMutableDictionaryRef properties = nullptr;
        if (IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, kNilOptions) == KERN_SUCCESS) {
            // Get device name
            CFStringRef name = (CFStringRef)CFDictionaryGetValue(properties, CFSTR("model"));
            if (name) {
                CFStringGetCString(name, device.name, MAX_DEVICE_NAME_LENGTH, kCFStringEncodingUTF8);
            } else {
                strncpy(device.name, "Unknown GPU", MAX_DEVICE_NAME_LENGTH - 1);
            }

            // Get vendor ID
            CFNumberRef vendor = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("vendor-id"));
            if (vendor) {
                CFNumberGetValue(vendor, kCFNumberSInt32Type, &device.vendor_id);
            }

            // Get device ID
            CFNumberRef deviceId = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("device-id"));
            if (deviceId) {
                CFNumberGetValue(deviceId, kCFNumberSInt32Type, &device.device_id);
            }

            // Determine vendor name from vendor ID
            switch (device.vendor_id) {
                case 0x106B: strncpy(device.vendor, "Apple", MAX_DEVICE_NAME_LENGTH - 1); break;
                case 0x10DE: strncpy(device.vendor, "NVIDIA", MAX_DEVICE_NAME_LENGTH - 1); break;
                case 0x1002: strncpy(device.vendor, "AMD", MAX_DEVICE_NAME_LENGTH - 1); break;
                case 0x8086: strncpy(device.vendor, "Intel", MAX_DEVICE_NAME_LENGTH - 1); break;
                default: strncpy(device.vendor, "Unknown", MAX_DEVICE_NAME_LENGTH - 1); break;
            }

            // Get VRAM size if available
            CFNumberRef vram = (CFNumberRef)CFDictionaryGetValue(properties, CFSTR("VRAM,totalMB"));
            if (vram) {
                int vramMB = 0;
                CFNumberGetValue(vram, kCFNumberSInt32Type, &vramMB);
                device.dedicated_video_memory = (uint64_t)vramMB * 1024 * 1024;
            }

            CFRelease(properties);
        }

        device.device_index = out_devices->device_count;
        device.is_default = (out_devices->device_count == 0);

        if (backend == Backend::Auto) {
            device.backend = get_default_backend();
        } else {
            device.backend = backend;
        }

        out_devices->device_count++;
        IOObjectRelease(service);
    }

    IOObjectRelease(iterator);
#else // iOS
    // On iOS, we have a single GPU (Apple Silicon)
    GraphicsDeviceInfo& device = out_devices->devices[0];

    strncpy(device.name, "Apple GPU", MAX_DEVICE_NAME_LENGTH - 1);
    strncpy(device.vendor, "Apple", MAX_DEVICE_NAME_LENGTH - 1);
    device.vendor_id = 0x106B;
    device.device_id = 0;
    device.dedicated_video_memory = 0; // Shared memory on iOS
    device.dedicated_system_memory = 0;
    device.shared_system_memory = [NSProcessInfo processInfo].physicalMemory;
    device.device_index = 0;
    device.is_default = true;

    if (backend == Backend::Auto) {
        device.backend = get_default_backend();
    } else {
        device.backend = backend;
    }

    out_devices->device_count = 1;
#endif

    return out_devices->device_count;
}

//=============================================================================
// Monitor Enumeration
//=============================================================================

int enumerate_monitors(MonitorEnumeration* out_monitors) {
    if (!out_monitors) return 0;

    out_monitors->monitor_count = 0;

#if defined(WINDOW_PLATFORM_MACOS)
    // Enumerate monitors using NSScreen
    NSArray<NSScreen*>* screens = [NSScreen screens];
    NSScreen* mainScreen = [NSScreen mainScreen];

    for (NSScreen* screen in screens) {
        if (out_monitors->monitor_count >= MAX_MONITORS) break;

        MonitorInfo& monitor = out_monitors->monitors[out_monitors->monitor_count];

        // Get screen name
        NSString* name = [screen localizedName];
        if (name) {
            strncpy(monitor.name, [name UTF8String], MAX_DEVICE_NAME_LENGTH - 1);
        } else {
            snprintf(monitor.name, MAX_DEVICE_NAME_LENGTH, "Display %d", out_monitors->monitor_count);
        }

        // Get frame
        NSRect frame = [screen frame];
        monitor.x = (int)frame.origin.x;
        monitor.y = (int)frame.origin.y;
        monitor.width = (int)frame.size.width;
        monitor.height = (int)frame.size.height;

        monitor.is_primary = (screen == mainScreen);
        monitor.monitor_index = out_monitors->monitor_count;

        // Get refresh rate using Core Graphics
        CGDirectDisplayID displayID = [[[screen deviceDescription] objectForKey:@"NSScreenNumber"] unsignedIntValue];
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(displayID);
        if (mode) {
            monitor.refresh_rate = (int)CGDisplayModeGetRefreshRate(mode);
            if (monitor.refresh_rate == 0) {
                monitor.refresh_rate = 60; // Default for LCD
            }
            CGDisplayModeRelease(mode);
        } else {
            monitor.refresh_rate = 60;
        }

        // Enumerate display modes
        monitor.mode_count = 0;
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(displayID, nullptr);
        if (modes) {
            CFIndex count = CFArrayGetCount(modes);
            for (CFIndex i = 0; i < count && monitor.mode_count < MAX_DISPLAY_MODES; i++) {
                CGDisplayModeRef modeRef = (CGDisplayModeRef)CFArrayGetValueAtIndex(modes, i);

                int width = (int)CGDisplayModeGetWidth(modeRef);
                int height = (int)CGDisplayModeGetHeight(modeRef);
                int refresh = (int)CGDisplayModeGetRefreshRate(modeRef);
                if (refresh == 0) refresh = 60;

                // Check for duplicates
                bool duplicate = false;
                for (int j = 0; j < monitor.mode_count; j++) {
                    if (monitor.modes[j].width == width &&
                        monitor.modes[j].height == height &&
                        monitor.modes[j].refresh_rate == refresh) {
                        duplicate = true;
                        break;
                    }
                }

                if (!duplicate) {
                    DisplayMode& dm = monitor.modes[monitor.mode_count];
                    dm.width = width;
                    dm.height = height;
                    dm.refresh_rate = refresh;
                    dm.bits_per_pixel = 32;
                    dm.is_native = (width == monitor.width && height == monitor.height);
                    monitor.mode_count++;
                }
            }
            CFRelease(modes);
        }

        out_monitors->monitor_count++;
    }
#else // iOS
    // On iOS, we have a single screen
    MonitorInfo& monitor = out_monitors->monitors[0];

    UIScreen* mainScreen = [UIScreen mainScreen];

    strncpy(monitor.name, "Main Display", MAX_DEVICE_NAME_LENGTH - 1);

    CGRect bounds = [mainScreen bounds];
    CGFloat scale = [mainScreen scale];

    monitor.x = 0;
    monitor.y = 0;
    monitor.width = (int)(bounds.size.width * scale);
    monitor.height = (int)(bounds.size.height * scale);
    monitor.refresh_rate = 60;
    monitor.is_primary = true;
    monitor.monitor_index = 0;

    // Single mode - native resolution
    monitor.mode_count = 1;
    monitor.modes[0].width = monitor.width;
    monitor.modes[0].height = monitor.height;
    monitor.modes[0].refresh_rate = 60;
    monitor.modes[0].bits_per_pixel = 32;
    monitor.modes[0].is_native = true;

    out_monitors->monitor_count = 1;
#endif

    return out_monitors->monitor_count;
}

} // namespace window

#endif // WINDOW_PLATFORM_MACOS || WINDOW_PLATFORM_IOS
