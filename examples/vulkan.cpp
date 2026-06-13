/*
 * vulkan.cpp - Vulkan window example
 *
 * The library creates the instance, device, and swapchain; this example owns
 * the per-frame work: acquire a swapchain image, clear it through a render
 * pass (the swapchain images are COLOR_ATTACHMENT-only, so a loadOp=CLEAR
 * pass is the correct way to clear them), submit, and present.
 */

#include "window.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#if defined(WINDOW_PLATFORM_WIN32) || defined(WINDOW_PLATFORM_X11) || defined(WINDOW_PLATFORM_WAYLAND) || defined(WINDOW_PLATFORM_MACOS)

#include <vulkan/vulkan.h>

int main() {
    // Create window with Vulkan backend
    window::Config config;
    config.windows[0].title = "Vulkan Example";
    config.windows[0].width = 800;
    config.windows[0].height = 600;
    config.backend = window::Backend::Vulkan;

    window::Result result;
    auto windows = window::Window::create(config, &result);

    if (result != window::Result::Success || windows.empty()) {
        printf("Failed to create window: %s\n", window::result_to_string(result));
        return 1;
    }

    window::Window* win = windows[0];
    window::Graphics* gfx = win->graphics();

    printf("Vulkan context created!\n");
    printf("Backend: %s\n", gfx->get_backend_name());
    printf("Device: %s\n", gfx->get_device_name());

    window::VulkanGraphicsInfo info;
    if (!gfx->get_vulkan_info(&info) || !info.swapchain) {
        printf("get_vulkan_info() failed\n");
        win->destroy();
        return 1;
    }

    VkPhysicalDevice physical_device = static_cast<VkPhysicalDevice>(info.physical_device);
    VkDevice device = static_cast<VkDevice>(info.device);
    VkQueue queue = static_cast<VkQueue>(info.graphics_queue);
    VkSurfaceKHR surface = static_cast<VkSurfaceKHR>(info.surface);
    VkSwapchainKHR swapchain = static_cast<VkSwapchainKHR>(info.swapchain);
    VkFormat format = static_cast<VkFormat>(info.swapchain_format);

    printf("VkDevice: %p\n", (void*)device);
    printf("VkSwapchainKHR: %p\n", (void*)swapchain);
    printf("Queue family: %u, swapchain format: %u\n", info.graphics_queue_family, info.swapchain_format);

    // Swapchain images
    uint32_t image_count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    std::vector<VkImage> images(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data());

    // Swapchain extent (the library created it at the window's client size)
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);
    VkExtent2D extent = caps.currentExtent;

    // Render pass: single color attachment, cleared on load, presented after
    VkAttachmentDescription attachment = {};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref = {};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo pass_info = {};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 1;
    pass_info.pDependencies = &dependency;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &pass_info, nullptr, &render_pass) != VK_SUCCESS) {
        printf("vkCreateRenderPass failed\n");
        win->destroy();
        return 1;
    }

    // Image views + framebuffers, one per swapchain image
    std::vector<VkImageView> views(image_count);
    std::vector<VkFramebuffer> framebuffers(image_count);
    for (uint32_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &view_info, nullptr, &views[i]);

        VkFramebufferCreateInfo fb_info = {};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = render_pass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &views[i];
        fb_info.width = extent.width;
        fb_info.height = extent.height;
        fb_info.layers = 1;
        vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffers[i]);
    }

    // Command pool + one resettable command buffer (single frame in flight)
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = info.graphics_queue_family;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &pool_info, nullptr, &command_pool);

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &alloc_info, &cmd);

    VkSemaphoreCreateInfo sem_info = {};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore sem_acquire = VK_NULL_HANDLE, sem_render = VK_NULL_HANDLE;
    vkCreateSemaphore(device, &sem_info, nullptr, &sem_acquire);
    vkCreateSemaphore(device, &sem_info, nullptr, &sem_render);

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device, &fence_info, nullptr, &fence);

    float time = 0.0f;
    bool swapchain_ok = true;

    while (!win->should_close()) {
        win->poll_events();
        if (!swapchain_ok) continue;  // window keeps responding; rendering stopped

        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

        uint32_t image_index = 0;
        VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                 sem_acquire, VK_NULL_HANDLE, &image_index);
        if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
            // The library does not recreate the swapchain on resize yet
            printf("Swapchain out of date (window resized?) - rendering stopped\n");
            swapchain_ok = false;
            continue;
        }

        vkResetFences(device, 1, &fence);

        // Animate clear color (same pattern as the other backend examples)
        float r = (sinf(time) + 1.0f) * 0.5f;
        float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
        float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;
        VkClearValue clear_value = {};
        clear_value.color = { { r * 0.3f, g * 0.3f, b * 0.3f, 1.0f } };

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin_info);

        VkRenderPassBeginInfo rp_begin = {};
        rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp_begin.renderPass = render_pass;
        rp_begin.framebuffer = framebuffers[image_index];
        rp_begin.renderArea.extent = extent;
        rp_begin.clearValueCount = 1;
        rp_begin.pClearValues = &clear_value;
        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmd);

        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &sem_acquire;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &sem_render;
        vkQueueSubmit(queue, 1, &submit, fence);

        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &sem_render;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image_index;
        VkResult presented = vkQueuePresentKHR(queue, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR) {
            printf("Swapchain out of date on present - rendering stopped\n");
            swapchain_ok = false;
        }

        time += 0.016f;
    }

    // Cleanup
    vkDeviceWaitIdle(device);
    vkDestroyFence(device, fence, nullptr);
    vkDestroySemaphore(device, sem_render, nullptr);
    vkDestroySemaphore(device, sem_acquire, nullptr);
    vkDestroyCommandPool(device, command_pool, nullptr);
    for (uint32_t i = 0; i < image_count; i++) {
        vkDestroyFramebuffer(device, framebuffers[i], nullptr);
        vkDestroyImageView(device, views[i], nullptr);
    }
    vkDestroyRenderPass(device, render_pass, nullptr);

    win->destroy();
    printf("Vulkan resources cleaned up.\n");
    return 0;
}

#else

int main() {
    printf("Vulkan example is not available on this platform.\n");
    return 0;
}

#endif
