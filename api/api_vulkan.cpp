/*
 * api_vulkan.cpp - Vulkan graphics implementation
 */

#include "window.hpp"

#if !defined(WINDOW_NO_VULKAN)

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#ifdef __linux__
#ifdef WINDOW_PLATFORM_X11
#include <X11/Xlib.h>
#define VK_USE_PLATFORM_XLIB_KHR
#endif
#ifdef WINDOW_PLATFORM_WAYLAND
#include <wayland-client.h>
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#endif

#ifdef __ANDROID__
#include <android/native_window.h>
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

// UWP uses Win32 surface extension since CoreWindow can be treated as native window
#ifdef WINDOW_PLATFORM_UWP
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace window {

//=============================================================================
// Vulkan Graphics Implementation
//=============================================================================

class GraphicsVulkan : public Graphics {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::string device_name;
    bool owns_instance = false;
    bool owns_device = true;
    uint32_t queue_family_index = 0;

    ~GraphicsVulkan() override {
        if (swapchain && device) vkDestroySwapchainKHR(device, swapchain, nullptr);
        if (device && owns_device) vkDestroyDevice(device, nullptr);
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance && owns_instance) vkDestroyInstance(instance, nullptr);
    }

    Backend get_backend() const override { return Backend::Vulkan; }
    const char* get_backend_name() const override { return "Vulkan"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        // Vulkan swapchain resize requires recreation
        // This is a simplified version - full implementation would recreate swapchain
        // For now, return true and expect user to handle swapchain recreation
        (void)width;
        (void)height;
        // Note: User should use vkGetPhysicalDeviceSurfaceCapabilitiesKHR and
        // recreate swapchain with new extent when window resizes
        return true;
    }

    void present() override {
        // Vulkan presentation requires command buffer submission
        // This is handled by the user via vkQueuePresentKHR
        // This method is a no-op for Vulkan as presentation is more complex
    }

    void make_current() override {
        // Vulkan doesn't have a "make current" concept like OpenGL
    }

    void* native_device() const override { return device; }
    void* native_context() const override { return graphics_queue; }
    void* native_swapchain() const override { return swapchain; }
};

//=============================================================================
// Vulkan Initialization Helpers
//=============================================================================

static VkInstance create_vulkan_instance(const std::vector<const char*>& extensions) {
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Window";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Window";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();

#ifdef _DEBUG
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    create_info.enabledLayerCount = 1;
    create_info.ppEnabledLayerNames = layers;
#endif

    VkInstance instance;
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return instance;
}

static VkPhysicalDevice select_physical_device(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0) return VK_NULL_HANDLE;

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

    // Prefer discrete GPU
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        // Check for graphics and present support
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queue_families.data());

        bool has_graphics = false;
        bool has_present = false;
        for (uint32_t i = 0; i < queue_count; i++) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) has_graphics = true;
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
            if (present_support) has_present = true;
        }

        if (has_graphics && has_present && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            return device;
        }
    }

    // Fallback to any suitable device
    for (const auto& device : devices) {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queue_families.data());

        for (uint32_t i = 0; i < queue_count; i++) {
            if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 present_support = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
                if (present_support) return device;
            }
        }
    }

    return VK_NULL_HANDLE;
}

static uint32_t find_graphics_queue_family(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, queue_families.data());

    for (uint32_t i = 0; i < queue_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            VkBool32 present_support = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
            if (present_support) return i;
        }
    }
    return UINT32_MAX;
}

static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice device, VkSurfaceKHR surface, const Config& config) {
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, formats.data());

    // For 64-bit HDR, prefer R16G16B16A16_SFLOAT
    if (config.color_bits >= 64) {
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                return fmt;
            }
        }
        // Fallback to RGBA16 UNORM if float not available
        for (const auto& fmt : formats) {
            if (fmt.format == VK_FORMAT_R16G16B16A16_UNORM) {
                return fmt;
            }
        }
    }

    // Prefer BGRA8 SRGB for standard color
    for (const auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmt;
        }
    }

    // Fallback to first format
    return formats[0];
}

// Helper to resolve swap mode from config
static SwapMode resolve_swap_mode(const Config& config) {
    if (config.swap_mode != SwapMode::Auto) {
        return config.swap_mode;
    }
    return config.vsync ? SwapMode::Fifo : SwapMode::Immediate;
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice device, VkSurfaceKHR surface, SwapMode swap_mode) {
    uint32_t mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &mode_count, modes.data());

    auto has_mode = [&modes](VkPresentModeKHR target) {
        for (const auto& mode : modes) {
            if (mode == target) return true;
        }
        return false;
    };

    switch (swap_mode) {
        case SwapMode::Immediate:
            if (has_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
            // Fallback to mailbox if immediate not available
            if (has_mode(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
            break;

        case SwapMode::Mailbox:
            if (has_mode(VK_PRESENT_MODE_MAILBOX_KHR)) return VK_PRESENT_MODE_MAILBOX_KHR;
            // Fallback to FIFO if mailbox not available
            break;

        case SwapMode::FifoRelaxed:
            if (has_mode(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            // Fallback to regular FIFO
            break;

        case SwapMode::Fifo:
        case SwapMode::Auto:
        default:
            break;
    }

    // FIFO is always supported
    return VK_PRESENT_MODE_FIFO_KHR;
}

static GraphicsVulkan* create_vulkan_graphics_common(VkInstance instance, VkSurfaceKHR surface,
                                                      int width, int height, const Config& config,
                                                      bool owns_instance,
                                                      const Graphics* shared_graphics = nullptr) {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    bool owns_device = true;

    // Check for shared Vulkan device
    if (shared_graphics && shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(shared_graphics);
        device = shared_vk->device;
        physical_device = shared_vk->physical_device;
        graphics_queue = shared_vk->graphics_queue;
        owns_device = false;
    } else {
        physical_device = select_physical_device(instance, surface);
        if (physical_device == VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            if (owns_instance) vkDestroyInstance(instance, nullptr);
            return nullptr;
        }

        uint32_t queue_family = find_graphics_queue_family(physical_device, surface);
        if (queue_family == UINT32_MAX) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            if (owns_instance) vkDestroyInstance(instance, nullptr);
            return nullptr;
        }

        // Create logical device
        float queue_priority = 1.0f;
        VkDeviceQueueCreateInfo queue_create_info = {};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;

        const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        VkDeviceCreateInfo device_create_info = {};
        device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_create_info.queueCreateInfoCount = 1;
        device_create_info.pQueueCreateInfos = &queue_create_info;
        device_create_info.enabledExtensionCount = 1;
        device_create_info.ppEnabledExtensionNames = device_extensions;

        if (vkCreateDevice(physical_device, &device_create_info, nullptr, &device) != VK_SUCCESS) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            if (owns_instance) vkDestroyInstance(instance, nullptr);
            return nullptr;
        }

        vkGetDeviceQueue(device, queue_family, 0, &graphics_queue);
    }

    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities);

    VkExtent2D extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, extent.width));
    extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, extent.height));

    uint32_t image_count = config.back_buffers;
    if (image_count < capabilities.minImageCount) image_count = capabilities.minImageCount;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSurfaceFormatKHR surface_format = choose_surface_format(physical_device, surface, config);
    VkPresentModeKHR present_mode = choose_present_mode(physical_device, surface, resolve_swap_mode(config));

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = VK_TRUE;

    VkSwapchainKHR swapchain;
    if (vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain) != VK_SUCCESS) {
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    // Get device name
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device, &props);

    GraphicsVulkan* gfx = new GraphicsVulkan();
    gfx->instance = instance;
    gfx->physical_device = physical_device;
    gfx->device = device;
    gfx->graphics_queue = graphics_queue;
    gfx->surface = surface;
    gfx->swapchain = swapchain;
    gfx->device_name = props.deviceName;
    gfx->owns_instance = owns_instance;
    gfx->owns_device = owns_device;

    return gfx;
}

//=============================================================================
// Platform-Specific Creation Functions
//=============================================================================

#ifdef VK_USE_PLATFORM_WIN32_KHR
Graphics* create_vulkan_graphics_win32(void* hwnd, int width, int height, const Config& config) {
    // Use shared instance if available
    VkInstance instance = VK_NULL_HANDLE;
    bool owns_instance = true;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(config.shared_graphics);
        instance = shared_vk->instance;
        owns_instance = false;
    } else {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        };
        instance = create_vulkan_instance(extensions);
        if (instance == VK_NULL_HANDLE) return nullptr;
    }

    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hwnd = static_cast<HWND>(hwnd);
    surface_info.hinstance = GetModuleHandle(nullptr);

    VkSurfaceKHR surface;
    if (vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return create_vulkan_graphics_common(instance, surface, width, height, config, owns_instance, config.shared_graphics);
}
#endif

#ifdef VK_USE_PLATFORM_XLIB_KHR
Graphics* create_vulkan_graphics_xlib(void* display, unsigned long xwindow, int width, int height, const Config& config) {
    // Use shared instance if available
    VkInstance instance = VK_NULL_HANDLE;
    bool owns_instance = true;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(config.shared_graphics);
        instance = shared_vk->instance;
        owns_instance = false;
    } else {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_XLIB_SURFACE_EXTENSION_NAME
        };
        instance = create_vulkan_instance(extensions);
        if (instance == VK_NULL_HANDLE) return nullptr;
    }

    VkXlibSurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_info.dpy = static_cast<Display*>(display);
    surface_info.window = static_cast< ::Window>(xwindow);

    VkSurfaceKHR surface;
    if (vkCreateXlibSurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return create_vulkan_graphics_common(instance, surface, width, height, config, owns_instance, config.shared_graphics);
}
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
Graphics* create_vulkan_graphics_wayland(void* display, void* wl_surface, int width, int height, const Config& config) {
    // Use shared instance if available
    VkInstance instance = VK_NULL_HANDLE;
    bool owns_instance = true;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(config.shared_graphics);
        instance = shared_vk->instance;
        owns_instance = false;
    } else {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
        };
        instance = create_vulkan_instance(extensions);
        if (instance == VK_NULL_HANDLE) return nullptr;
    }

    VkWaylandSurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    surface_info.display = static_cast<struct wl_display*>(display);
    surface_info.surface = static_cast<struct wl_surface*>(wl_surface);

    VkSurfaceKHR surface;
    if (vkCreateWaylandSurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return create_vulkan_graphics_common(instance, surface, width, height, config, owns_instance, config.shared_graphics);
}
#endif

#ifdef VK_USE_PLATFORM_ANDROID_KHR
Graphics* create_vulkan_graphics_android(void* native_window, int width, int height, const Config& config) {
    // Use shared instance if available
    VkInstance instance = VK_NULL_HANDLE;
    bool owns_instance = true;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(config.shared_graphics);
        instance = shared_vk->instance;
        owns_instance = false;
    } else {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
        };
        instance = create_vulkan_instance(extensions);
        if (instance == VK_NULL_HANDLE) return nullptr;
    }

    VkAndroidSurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surface_info.window = static_cast<ANativeWindow*>(native_window);

    VkSurfaceKHR surface;
    if (vkCreateAndroidSurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return create_vulkan_graphics_common(instance, surface, width, height, config, owns_instance, config.shared_graphics);
}
#endif

#if defined(WINDOW_PLATFORM_UWP) && defined(VK_USE_PLATFORM_WIN32_KHR)
Graphics* create_vulkan_graphics_corewindow(void* core_window, int width, int height, const Config& config) {
    // Use shared instance if available
    VkInstance instance = VK_NULL_HANDLE;
    bool owns_instance = true;
    if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Vulkan) {
        const GraphicsVulkan* shared_vk = static_cast<const GraphicsVulkan*>(config.shared_graphics);
        instance = shared_vk->instance;
        owns_instance = false;
    } else {
        std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        };
        instance = create_vulkan_instance(extensions);
        if (instance == VK_NULL_HANDLE) return nullptr;
    }

    // For UWP, core_window is IUnknown* that can be used with Win32 surface
    // Note: Vulkan on UWP requires the swapchain to be created with VK_KHR_win32_surface
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_info.hwnd = static_cast<HWND>(core_window);
    surface_info.hinstance = GetModuleHandle(nullptr);

    VkSurfaceKHR surface;
    if (vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface) != VK_SUCCESS) {
        if (owns_instance) vkDestroyInstance(instance, nullptr);
        return nullptr;
    }

    return create_vulkan_graphics_common(instance, surface, width, height, config, owns_instance, config.shared_graphics);
}
#endif

} // namespace window

#endif // !WINDOW_NO_VULKAN
