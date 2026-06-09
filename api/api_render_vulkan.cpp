// api_render_vulkan.cpp — Vulkan implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp).
//
// Status: scaffold. The creators are wired into the dispatcher (api_render.cpp);
// the real device/commander is built on UGW's GraphicsVulkan (which must expose
// VkInstance / VkPhysicalDevice / VkDevice / graphics queue + family / surface /
// swapchain images+format+extent — extend window.hpp's Vulkan accessors first).
// Until the implementation lands, the creators report ErrorNotSupported so the
// abstraction degrades gracefully instead of crashing.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

namespace window {

#if defined(WINDOW_SUPPORT_VULKAN)
// TODO(phase7-backends): real Vulkan GraphicDevice/GraphicCommander.
//  - device: VMA-less suballocator or one VkDeviceMemory per resource; buffers,
//    textures(+views, immutable), samplers, SPIR-V shaders, graphics/compute/mesh
//    pipelines + VkPipelineLayout (root signature) + VkDescriptorSetLayout/Pool/Set,
//    VkRenderPass-or-dynamic-rendering targets, timeline + binary semaphores, fences,
//    query pools, copy/blit/resolve, sparse (VK_KHR), ray tracing (VK_KHR_*).
//  - commander: a VkCommandBuffer recorder; submit to the queue (+ signal fence/
//    timeline); integrate with GraphicsVulkan's swapchain acquire/present.
#endif

GraphicDevice* create_device_vulkan(Graphics* context, Result* out_result) {
    (void)context;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
GraphicCommander* create_commander_vulkan(Graphics* context, GraphicDevice* device,
                                          QueueType queue, Result* out_result) {
    (void)context; (void)device; (void)queue;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void submit_commander_vulkan(Graphics* context, GraphicCommander* commander,
                             FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    (void)context; (void)commander; (void)fence; (void)timeline; (void)value;
}

} // namespace window
