/*
 * vulkan.cpp - Vulkan window example
 */

#include "window.hpp"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <vector>
#include <cstring>

int main() {
    // Get required extensions
    uint32_t ext_count;
    const char** required_exts = window::Window::get_required_vulkan_extensions(&ext_count);

    printf("Required Vulkan extensions:\n");
    for (uint32_t i = 0; i < ext_count; i++) {
        printf("  %s\n", required_exts[i]);
    }

    // Create Vulkan instance
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Vulkan Example";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = ext_count;
    create_info.ppEnabledExtensionNames = required_exts;

#ifdef __APPLE__
    create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    std::vector<const char*> extensions(required_exts, required_exts + ext_count);
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
#endif

    VkInstance instance;
    VkResult vk_result = vkCreateInstance(&create_info, nullptr, &instance);
    if (vk_result != VK_SUCCESS) {
        printf("Failed to create Vulkan instance: %d\n", vk_result);
        return 1;
    }

    printf("Vulkan instance created!\n");

    // Create window
    window::Config config;
    config.title = "Vulkan Example";
    config.width = 800;
    config.height = 600;
    config.graphics_api = window::GraphicsAPI::Vulkan;

    window::Result result;
    window::Window* win = window::Window::create(config, &result);

    if (result != window::Result::Success) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    // Create surface
    VkSurfaceKHR surface;
    if (!win->create_vulkan_surface(instance, &surface)) {
        printf("Failed to create Vulkan surface!\n");
        win->destroy();
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    printf("Vulkan surface created!\n");

    // Enumerate physical devices
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    if (device_count > 0) {
        std::vector<VkPhysicalDevice> devices(device_count);
        vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

        printf("Found %u physical device(s):\n", device_count);
        for (uint32_t i = 0; i < device_count; i++) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);
            printf("  %u: %s\n", i, props.deviceName);
        }
    }

    // Main loop
    while (!win->should_close()) {
        win->poll_events();
    }

    // Cleanup
    vkDestroySurfaceKHR(instance, surface, nullptr);
    win->destroy();
    vkDestroyInstance(instance, nullptr);

    printf("Vulkan resources cleaned up.\n");
    return 0;
}
