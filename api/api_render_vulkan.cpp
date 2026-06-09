// api_render_vulkan.cpp — Vulkan implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp), built on the VkInstance/
// VkPhysicalDevice/VkDevice/queue exposed by UGW's GraphicsVulkan
// (Graphics::get_vulkan_info). Wired into the dispatcher in api_render.cpp.
//
// Scope: resources, memory, copies, readback, render-target clears, fences,
// timeline + binary semaphores, queries, pipeline cache, descriptor sets /
// pipeline layouts, graphics/compute pipelines, draws and barriers are
// implemented. Mesh shaders, ray tracing and sparse residency require optional
// extensions/features and log a one-time "unsupported" when absent (parity with
// the GL backend). Memory uses one allocation per resource (no suballocator);
// buffers are host-visible for simple map/readback.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

#if defined(WINDOW_SUPPORT_VULKAN)

#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <vector>

namespace window {
namespace {

void vk_unsupported(const char* what) {
    static std::unordered_map<const char*, bool> seen;
    if (!seen[what]) { seen[what] = true; std::fprintf(stderr, "[UGW/Vulkan] %s not supported (no-op)\n", what); }
}
#define VK_OK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) std::fprintf(stderr, "[UGW/Vulkan] %s -> %d\n", #x, int(_r)); } while (0)

// Handle store: stable integer ids with a free list (mirrors the GL backend).
template <class T> struct Pool {
    std::vector<T> items; std::vector<int> free_list;
    int alloc(T v) { if (!free_list.empty()) { int i = free_list.back(); free_list.pop_back(); items[i] = std::move(v); return i; } items.push_back(std::move(v)); return int(items.size()) - 1; }
    T* get(int id) { return (id >= 0 && id < int(items.size())) ? &items[id] : nullptr; }
    void release(int id) { if (id >= 0 && id < int(items.size())) free_list.push_back(id); }
};

//-----------------------------------------------------------------------------
// Format maps
//-----------------------------------------------------------------------------
VkFormat tex_format(TextureFormat f) {
    switch (f) {
        case TextureFormat::R8_UNORM:          return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8_UNORM:         return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::RGBA8_UNORM_SRGB:  return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::BGRA8_UNORM:       return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::R16_FLOAT:         return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::RGBA16_FLOAT:      return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R32_FLOAT:         return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::RGBA32_FLOAT:      return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::D32_FLOAT:         return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        default:                               return VK_FORMAT_R8G8B8A8_UNORM;
    }
}
uint32_t format_bytes(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8_UNORM: return 1;
        case VK_FORMAT_R8G8_UNORM: return 2;
        case VK_FORMAT_R16_SFLOAT: return 2;
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_R32_SFLOAT:
        case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D24_UNORM_S8_UINT: return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 4;
    }
}
VkFormat vertex_format(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Int1:   return VK_FORMAT_R32_SINT;
        case VertexFormat::Int2:   return VK_FORMAT_R32G32_SINT;
        case VertexFormat::Int4:   return VK_FORMAT_R32G32B32A32_SINT;
        case VertexFormat::UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Byte4N:  return VK_FORMAT_R8G8B8A8_SNORM;
        case VertexFormat::Half2:  return VK_FORMAT_R16G16_SFLOAT;
        case VertexFormat::Half4:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case VertexFormat::RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}
VkShaderStageFlagBits shader_stage(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex:      return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment:    return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Geometry:    return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEval:    return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        case ShaderStage::Compute:     return VK_SHADER_STAGE_COMPUTE_BIT;
        default:                       return VK_SHADER_STAGE_VERTEX_BIT;
    }
}
VkDescriptorType desc_type(BindingType t) {
    switch (t) {
        case BindingType::UniformBuffer:        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case BindingType::StorageBuffer:        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case BindingType::SampledTexture:       return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case BindingType::StorageTexture:       return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case BindingType::Sampler:              return VK_DESCRIPTOR_TYPE_SAMPLER;
        case BindingType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:                                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}
VkShaderStageFlags stage_flags(uint32_t bits) {
    VkShaderStageFlags f = 0;
    if (bits & STAGE_VERTEX)       f |= VK_SHADER_STAGE_VERTEX_BIT;
    if (bits & STAGE_FRAGMENT)     f |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (bits & STAGE_GEOMETRY)     f |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (bits & STAGE_TESS_CONTROL) f |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (bits & STAGE_TESS_EVAL)    f |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (bits & STAGE_COMPUTE)      f |= VK_SHADER_STAGE_COMPUTE_BIT;
    return f ? f : VK_SHADER_STAGE_ALL;
}

//-----------------------------------------------------------------------------
// Resource records
//-----------------------------------------------------------------------------
struct VKBuffer  { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize size = 0; void* mapped = nullptr; };
struct VKTexture { VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
                   VkFormat format = VK_FORMAT_UNDEFINED; VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                   int w = 0, h = 0, layers = 1, levels = 1; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; bool owns = true; };
struct VKSampler { VkSampler sampler = VK_NULL_HANDLE; };
struct VKShader  { VkShaderModule mod = VK_NULL_HANDLE; ShaderStage stage = ShaderStage::Vertex; };
struct VKPipeline{ VkPipeline pipeline = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; bool owns_layout = false;
                   VkRenderPass render_pass = VK_NULL_HANDLE; VkPipelineBindPoint bind = VK_PIPELINE_BIND_POINT_GRAPHICS; };
struct VKRenderTarget { int color_tex = -1; int depth_tex = -1; };
struct VKFence   { VkFence fence = VK_NULL_HANDLE; };
struct VKSem     { VkSemaphore sem = VK_NULL_HANDLE; };
struct VKTimeline{ VkSemaphore sem = VK_NULL_HANDLE; };
struct VKQuery   { VkQueryPool pool = VK_NULL_HANDLE; VkQueryType type = VK_QUERY_TYPE_TIMESTAMP; };
struct VKDescSetLayout  { VkDescriptorSetLayout layout = VK_NULL_HANDLE; DescriptorSetLayoutDesc desc; };
struct VKPipelineLayout { VkPipelineLayout layout = VK_NULL_HANDLE; };
struct VKDescriptorSet  { VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; DescriptorSetLayoutHandle layout; };
struct VKPipelineCache  { VkPipelineCache cache = VK_NULL_HANDLE; };
struct VKAccelStruct    { int dummy = 0; };

class VKCommander;

//=============================================================================
// VKDevice
//=============================================================================
class VKDevice : public GraphicDevice {
public:
    VkInstance       instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice         dev = VK_NULL_HANDLE;
    VkQueue          queue = VK_NULL_HANDLE;
    uint32_t         queue_family = 0;
    VkCommandPool    pool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props{};
    bool             has_debug_utils = false;
    // Debug-utils are extension entry points — loaded via the loader, not linked.
    PFN_vkSetDebugUtilsObjectNameEXT  p_set_name = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT  p_begin_label = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT    p_end_label = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT p_insert_label = nullptr;

    explicit VKDevice(const VulkanGraphicsInfo& gi) {
        instance = (VkInstance)gi.instance; phys = (VkPhysicalDevice)gi.physical_device;
        dev = (VkDevice)gi.device; queue = (VkQueue)gi.graphics_queue; queue_family = gi.graphics_queue_family;
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queue_family;
        VK_OK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
        p_set_name     = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
        p_begin_label  = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
        p_end_label    = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");
        p_insert_label = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT");
        has_debug_utils = (p_set_name != nullptr);
    }
    ~VKDevice() override { if (pool) vkDestroyCommandPool(dev, pool, nullptr); }

    Backend get_backend() const override { return Backend::Vulkan; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (!out) return;
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(phys, &p);
        const auto& l = p.limits;
        out->max_texture_size = int(l.maxImageDimension2D);
        out->max_texture_array_layers = int(l.maxImageArrayLayers);
        out->max_color_attachments = int(l.maxColorAttachments);
        out->max_viewports = int(l.maxViewports);
        out->max_vertex_attributes = int(l.maxVertexInputAttributes);
        out->max_uniform_buffer_range = int(l.maxUniformBufferRange);
        out->max_storage_buffer_range = int(l.maxStorageBufferRange);
        out->max_push_constant_size = int(l.maxPushConstantsSize);
        out->max_bound_descriptor_sets = int(l.maxBoundDescriptorSets);
        out->min_uniform_buffer_offset_alignment = int(l.minUniformBufferOffsetAlignment);
        out->min_storage_buffer_offset_alignment = int(l.minStorageBufferOffsetAlignment);
        out->min_texel_buffer_offset_alignment = int(l.minTexelBufferOffsetAlignment);
        out->compute_shaders = true; out->indirect_draw = true; out->multi_draw_indirect = true;
        out->instancing = true; out->base_vertex_draw = true; out->timestamp_query = l.timestampComputeAndGraphics;
    }

    // ---- helpers ------------------------------------------------------------
    uint32_t find_mem(uint32_t type_bits, VkMemoryPropertyFlags want) const {
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
            if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags & want) == want) return i;
        return 0;
    }
    void immediate(const std::function<void(VkCommandBuffer)>& fn) {
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer cb; VK_OK(vkAllocateCommandBuffers(dev, &ai, &cb));
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi); fn(cb); vkEndCommandBuffer(cb);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        VkFence f; VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        vkCreateFence(dev, &fci, nullptr, &f);
        vkQueueSubmit(queue, 1, &si, f);
        vkWaitForFences(dev, 1, &f, VK_TRUE, ~0ull);
        vkDestroyFence(dev, f, nullptr);
        vkFreeCommandBuffers(dev, pool, 1, &cb);
    }
    static void barrier(VkCommandBuffer cb, VKTexture* t, VkImageLayout newL) {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.oldLayout = t->layout; b.newLayout = newL; b.image = t->image;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = { t->aspect, 0, (uint32_t)t->levels, 0, (uint32_t)t->layers };
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        t->layout = newL;
    }

    // ---- buffers ------------------------------------------------------------
    BufferHandle create_buffer(const BufferDesc& d) override {
        VKBuffer b; b.size = d.size ? d.size : 4;
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        switch (d.type) {
            case BufferType::Vertex:   usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT; break;
            case BufferType::Index:    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; break;
            case BufferType::Uniform:  usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT; break;
            case BufferType::Storage:  usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; break;
            case BufferType::Indirect: usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT; break;
        }
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = b.size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_OK(vkCreateBuffer(dev, &bi, nullptr, &b.buf));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, b.buf, &req);
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mi.allocationSize = req.size;
        mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_OK(vkAllocateMemory(dev, &mi, nullptr, &b.mem));
        vkBindBufferMemory(dev, b.buf, b.mem, 0);
        if (d.initial_data) { void* p = nullptr; vkMapMemory(dev, b.mem, 0, b.size, 0, &p); std::memcpy(p, d.initial_data, d.size); vkUnmapMemory(dev, b.mem); }
        int id = buffers_.alloc(b);
        if (d.debug_name) set_name(VK_OBJECT_TYPE_BUFFER, (uint64_t)b.buf, d.debug_name);
        return { id };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !data) return;
        void* p = nullptr; vkMapMemory(dev, b->mem, offset, size, 0, &p); std::memcpy(p, data, size); vkUnmapMemory(dev, b->mem);
    }
    void destroy_buffer(BufferHandle h) override { auto* b = buffers_.get(h.id); if (!b) return; if (b->buf) vkDestroyBuffer(dev, b->buf, nullptr); if (b->mem) vkFreeMemory(dev, b->mem, nullptr); buffers_.release(h.id); }

    // ---- textures -----------------------------------------------------------
    TextureHandle create_texture(const TextureDesc& d) override {
        VKTexture t; t.format = tex_format(d.format); t.w = d.width; t.h = d.height;
        t.layers = (d.array_layers > 1 || d.array_texture) ? (d.array_layers > 1 ? d.array_layers : 1) : 1;
        t.levels = d.mip_levels > 0 ? d.mip_levels : 1;
        if (d.mip_levels == 0) { int s = d.width > d.height ? d.width : d.height; t.levels = 1; while (s > 1) { s >>= 1; ++t.levels; } }
        const bool depth = (d.usage & TEXTURE_USAGE_DEPTH_STENCIL) != 0;
        t.aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (d.usage & TEXTURE_USAGE_SAMPLED)       usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (d.usage & TEXTURE_USAGE_RENDER_TARGET) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (d.usage & TEXTURE_USAGE_DEPTH_STENCIL) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (d.usage & TEXTURE_USAGE_STORAGE)       usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ii.imageType = VK_IMAGE_TYPE_2D; ii.format = t.format;
        ii.extent = { (uint32_t)d.width, (uint32_t)d.height, 1 };
        ii.mipLevels = t.levels; ii.arrayLayers = t.layers; ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = usage; ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (d.cube) { ii.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; ii.arrayLayers = 6; t.layers = 6; }
        VK_OK(vkCreateImage(dev, &ii, nullptr, &t.image));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, t.image, &req);
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mi.allocationSize = req.size; mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_OK(vkAllocateMemory(dev, &mi, nullptr, &t.mem));
        vkBindImageMemory(dev, t.image, t.mem, 0);
        t.view = make_view(t, t.format, (t.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D), 0, t.levels, 0, t.layers);
        if (d.initial_data) upload(t, d.initial_data);
        int id = textures_.alloc(t);
        if (d.debug_name) set_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)t.image, d.debug_name);
        return { id };
    }
    VkImageView make_view(const VKTexture& t, VkFormat fmt, VkImageViewType type, int base_mip, int levels, int base_layer, int layers) {
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = t.image; vi.viewType = type; vi.format = fmt;
        vi.subresourceRange = { t.aspect, (uint32_t)base_mip, (uint32_t)levels, (uint32_t)base_layer, (uint32_t)layers };
        VkImageView v = VK_NULL_HANDLE; VK_OK(vkCreateImageView(dev, &vi, nullptr, &v)); return v;
    }
    void upload(VKTexture& t, const void* data) {
        const VkDeviceSize bytes = VkDeviceSize(t.w) * t.h * format_bytes(t.format) * t.layers;
        VKBuffer stage = make_staging(bytes, data);
        immediate([&](VkCommandBuffer cb) {
            barrier(cb, &t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy r{}; r.imageSubresource = { t.aspect, 0, 0, (uint32_t)t.layers };
            r.imageExtent = { (uint32_t)t.w, (uint32_t)t.h, 1 };
            vkCmdCopyBufferToImage(cb, stage.buf, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            barrier(cb, &t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        free_staging(stage);
    }
    void update_texture(TextureHandle h, const TextureRegion& reg, const void* data) override {
        auto* t = textures_.get(h.id); if (!t || !data) return;
        const VkDeviceSize bytes = VkDeviceSize(reg.width) * reg.height * format_bytes(t->format);
        VKBuffer stage = make_staging(bytes, data);
        immediate([&](VkCommandBuffer cb) {
            barrier(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy r{}; r.imageSubresource = { t->aspect, (uint32_t)reg.mip, (uint32_t)reg.layer, 1 };
            r.imageOffset = { reg.x, reg.y, 0 }; r.imageExtent = { (uint32_t)reg.width, (uint32_t)reg.height, 1 };
            vkCmdCopyBufferToImage(cb, stage.buf, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            barrier(cb, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
        free_staging(stage);
    }
    void generate_mipmaps(TextureHandle) override { vk_unsupported("generate_mipmaps"); }
    void destroy_texture(TextureHandle h) override {
        auto* t = textures_.get(h.id); if (!t) return;
        if (t->view) vkDestroyImageView(dev, t->view, nullptr);
        if (t->owns && t->image) vkDestroyImage(dev, t->image, nullptr);
        if (t->owns && t->mem) vkFreeMemory(dev, t->mem, nullptr);
        textures_.release(h.id);
    }
    VKBuffer make_staging(VkDeviceSize bytes, const void* data) {
        VKBuffer s; s.size = bytes ? bytes : 4;
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = s.size; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(dev, &bi, nullptr, &s.buf);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, s.buf, &req);
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.allocationSize = req.size;
        mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(dev, &mi, nullptr, &s.mem); vkBindBufferMemory(dev, s.buf, s.mem, 0);
        if (data) { void* p = nullptr; vkMapMemory(dev, s.mem, 0, s.size, 0, &p); std::memcpy(p, data, size_t(bytes)); vkUnmapMemory(dev, s.mem); }
        return s;
    }
    void free_staging(VKBuffer& s) { if (s.buf) vkDestroyBuffer(dev, s.buf, nullptr); if (s.mem) vkFreeMemory(dev, s.mem, nullptr); }

    // ---- samplers -----------------------------------------------------------
    SamplerHandle create_sampler(const SamplerState& s) override {
        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = s.mag_filter == FilterMode::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        si.minFilter = s.min_filter == FilterMode::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.maxLod = VK_LOD_CLAMP_NONE;
        VKSampler smp; VK_OK(vkCreateSampler(dev, &si, nullptr, &smp.sampler));
        return { samplers_.alloc(smp) };
    }
    void destroy_sampler(SamplerHandle h) override { auto* s = samplers_.get(h.id); if (s && s->sampler) vkDestroySampler(dev, s->sampler, nullptr); samplers_.release(h.id); }

    // ---- shaders (SPIR-V) ---------------------------------------------------
    ShaderHandle create_shader(const ShaderDesc& d) override {
        if (d.language != ShaderLanguage::SPIRV) { vk_unsupported("non-SPIR-V shader (compile to SPIR-V)"); return { -1 }; }
        VkShaderModuleCreateInfo mi{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        mi.codeSize = d.code_size; mi.pCode = static_cast<const uint32_t*>(d.code);
        VKShader sh; sh.stage = d.stage; VK_OK(vkCreateShaderModule(dev, &mi, nullptr, &sh.mod));
        if (!sh.mod) return { -1 };
        return { shaders_.alloc(sh) };
    }
    void destroy_shader(ShaderHandle h) override { auto* s = shaders_.get(h.id); if (s && s->mod) vkDestroyShaderModule(dev, s->mod, nullptr); shaders_.release(h.id); }

    // ---- pipeline layout / descriptor sets ----------------------------------
    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override {
        std::vector<VkDescriptorSetLayoutBinding> b(d.binding_count);
        for (int i = 0; i < d.binding_count; ++i)
            b[i] = { d.bindings[i].binding, desc_type(d.bindings[i].type), d.bindings[i].count, stage_flags(d.bindings[i].stages), nullptr };
        VkDescriptorSetLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        ci.bindingCount = (uint32_t)b.size(); ci.pBindings = b.data();
        VKDescSetLayout l; l.desc = d; VK_OK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &l.layout));
        return { dsls_.alloc(l) };
    }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { auto* l = dsls_.get(h.id); if (l && l->layout) vkDestroyDescriptorSetLayout(dev, l->layout, nullptr); dsls_.release(h.id); }
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& d) override {
        std::vector<VkDescriptorSetLayout> sets;
        for (int i = 0; i < d.set_layout_count; ++i) if (auto* l = dsls_.get(d.set_layouts[i].id)) sets.push_back(l->layout);
        std::vector<VkPushConstantRange> pcs;
        // Use VK_SHADER_STAGE_ALL so it matches vkCmdPushConstants (which pushes with
        // ALL — the commander doesn't track per-range stages).
        for (int i = 0; i < d.push_constant_count; ++i) pcs.push_back({ VK_SHADER_STAGE_ALL, d.push_constants[i].offset, d.push_constants[i].size });
        VkPipelineLayoutCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        ci.setLayoutCount = (uint32_t)sets.size(); ci.pSetLayouts = sets.data();
        ci.pushConstantRangeCount = (uint32_t)pcs.size(); ci.pPushConstantRanges = pcs.data();
        VKPipelineLayout l; VK_OK(vkCreatePipelineLayout(dev, &ci, nullptr, &l.layout));
        return { plls_.alloc(l) };
    }
    void destroy_pipeline_layout(PipelineLayoutHandle h) override { auto* l = plls_.get(h.id); if (l && l->layout) vkDestroyPipelineLayout(dev, l->layout, nullptr); plls_.release(h.id); }
    DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& d) override {
        auto* l = dsls_.get(d.layout.id); if (!l) return { -1 };
        // A small pool sized to this set's writes.
        std::vector<VkDescriptorPoolSize> sizes;
        for (int i = 0; i < d.write_count; ++i) sizes.push_back({ desc_type(d.writes[i].type), 1 });
        if (sizes.empty()) sizes.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 });
        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.maxSets = 1; pci.poolSizeCount = (uint32_t)sizes.size(); pci.pPoolSizes = sizes.data();
        VKDescriptorSet ds; ds.layout = d.layout;
        VK_OK(vkCreateDescriptorPool(dev, &pci, nullptr, &ds.pool));
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = ds.pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &l->layout;
        VK_OK(vkAllocateDescriptorSets(dev, &ai, &ds.set));
        int id = dsets_.alloc(ds);
        write_set(id, d);
        return { id };
    }
    void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& d) override { write_set(h.id, d); }
    void write_set(int id, const DescriptorSetDesc& d) {
        auto* ds = dsets_.get(id); if (!ds) return;
        std::vector<VkWriteDescriptorSet> writes; std::vector<VkDescriptorBufferInfo> bufs(d.write_count); std::vector<VkDescriptorImageInfo> imgs(d.write_count);
        for (int i = 0; i < d.write_count; ++i) {
            const auto& w = d.writes[i];
            VkWriteDescriptorSet ws{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            ws.dstSet = ds->set; ws.dstBinding = w.binding; ws.descriptorCount = 1; ws.descriptorType = desc_type(w.type);
            if (w.type == BindingType::UniformBuffer || w.type == BindingType::StorageBuffer) {
                auto* b = buffers_.get(w.buffer.id); if (!b) continue;
                bufs[i] = { b->buf, w.buffer_offset, w.buffer_size ? w.buffer_size : VK_WHOLE_SIZE }; ws.pBufferInfo = &bufs[i];
            } else {
                auto* t = textures_.get(w.texture.id); auto* s = samplers_.get(w.sampler.id);
                imgs[i] = { s ? s->sampler : VK_NULL_HANDLE, t ? t->view : VK_NULL_HANDLE,
                            (w.type == BindingType::StorageTexture) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                ws.pImageInfo = &imgs[i];
            }
            writes.push_back(ws);
        }
        if (!writes.empty()) vkUpdateDescriptorSets(dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
    void destroy_descriptor_set(DescriptorSetHandle h) override { auto* ds = dsets_.get(h.id); if (ds && ds->pool) vkDestroyDescriptorPool(dev, ds->pool, nullptr); dsets_.release(h.id); }

    // ---- pipelines ----------------------------------------------------------
    VkRenderPass make_render_pass(const PipelineDesc& d) {
        VkAttachmentDescription color{}; color.format = tex_format(d.color_formats[0]); color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = 1; ci.pAttachments = &color; ci.subpassCount = 1; ci.pSubpasses = &sub;
        VkRenderPass rp = VK_NULL_HANDLE; VK_OK(vkCreateRenderPass(dev, &ci, nullptr, &rp)); return rp;
    }
    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        VKPipeline p;
        auto* pl = plls_.get(d.layout.id);
        if (pl) p.layout = pl->layout;
        else { VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO }; vkCreatePipelineLayout(dev, &lci, nullptr, &p.layout); p.owns_layout = true; }
        VkPipelineCache cache = VK_NULL_HANDLE; if (auto* c = pcaches_.get(d.cache.id)) cache = c->cache;

        if (d.compute_shader.valid()) {
            p.bind = VK_PIPELINE_BIND_POINT_COMPUTE;
            auto* cs = shaders_.get(d.compute_shader.id); if (!cs) return { -1 };
            VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            ci.stage.module = cs->mod; ci.stage.pName = "main"; ci.layout = p.layout;
            VK_OK(vkCreateComputePipelines(dev, cache, 1, &ci, nullptr, &p.pipeline));
            return p.pipeline ? PipelineHandle{ pipelines_.alloc(p) } : PipelineHandle{ -1 };
        }
        if (d.mesh_shader.valid()) { vk_unsupported("mesh-shader pipelines"); return { -1 }; }

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        auto add = [&](ShaderHandle h) { if (auto* s = shaders_.get(h.id)) { VkPipelineShaderStageCreateInfo si{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }; si.stage = shader_stage(s->stage); si.module = s->mod; si.pName = "main"; stages.push_back(si); } };
        add(d.vertex_shader); add(d.fragment_shader);
        if (d.geometry_shader.valid()) add(d.geometry_shader);
        if (d.tess_control_shader.valid()) add(d.tess_control_shader);
        if (d.tess_eval_shader.valid()) add(d.tess_eval_shader);

        const VertexLayout& vl = d.vertex_layout;
        std::vector<VkVertexInputBindingDescription> binds;
        for (int i = 0; i < vl.buffer_count; ++i)
            binds.push_back({ (uint32_t)i, vl.strides[i], vl.input_rates[i] == VertexInputRate::PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX });
        std::vector<VkVertexInputAttributeDescription> attrs;
        for (int i = 0; i < vl.attribute_count; ++i) attrs.push_back({ vl.attributes[i].location, vl.attributes[i].buffer_slot, vertex_format(vl.attributes[i].format), vl.attributes[i].offset });
        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = (uint32_t)binds.size(); vi.pVertexBindingDescriptions = binds.data();
        vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size(); vi.pVertexAttributeDescriptions = attrs.data();
        VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO }; vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = d.rasterizer.fill_mode == FillMode::Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
        rs.cullMode = d.rasterizer.cull_mode == CullMode::None ? VK_CULL_MODE_NONE : (d.rasterizer.cull_mode == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
        rs.frontFace = d.rasterizer.front_face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xF; cba.blendEnable = d.blend.enabled ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; cba.colorBlendOp = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; cba.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO }; cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkPipelineDepthStencilStateCreateInfo dss{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        dss.depthTestEnable = d.depth_stencil.depth_enable; dss.depthWriteEnable = d.depth_stencil.depth_write; dss.depthCompareOp = VK_COMPARE_OP_LESS;
        VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO }; ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
        p.render_pass = make_render_pass(d);
        VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        ci.stageCount = (uint32_t)stages.size(); ci.pStages = stages.data();
        ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia; ci.pViewportState = &vp; ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms; ci.pColorBlendState = &cb; ci.pDepthStencilState = &dss; ci.pDynamicState = &ds;
        ci.layout = p.layout; ci.renderPass = p.render_pass;
        VK_OK(vkCreateGraphicsPipelines(dev, cache, 1, &ci, nullptr, &p.pipeline));
        return p.pipeline ? PipelineHandle{ pipelines_.alloc(p) } : PipelineHandle{ -1 };
    }
    void destroy_pipeline(PipelineHandle h) override {
        auto* p = pipelines_.get(h.id); if (!p) return;
        if (p->pipeline) vkDestroyPipeline(dev, p->pipeline, nullptr);
        if (p->render_pass) vkDestroyRenderPass(dev, p->render_pass, nullptr);
        if (p->owns_layout && p->layout) vkDestroyPipelineLayout(dev, p->layout, nullptr);
        pipelines_.release(h.id);
    }

    // ---- render targets -----------------------------------------------------
    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format;
        td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET;
        TextureHandle tex = create_texture(td);
        VKRenderTarget rt; rt.color_tex = tex.id;
        return { rts_.alloc(rt) };
    }
    RenderTargetHandle create_depth_target(const DepthStencilDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.usage = TEXTURE_USAGE_DEPTH_STENCIL;
        TextureHandle tex = create_texture(td);
        VKRenderTarget rt; rt.depth_tex = tex.id;
        return { rts_.alloc(rt) };
    }
    TextureHandle render_target_texture(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return { -1 }; return { rt->color_tex >= 0 ? rt->color_tex : rt->depth_tex }; }
    void destroy_render_target(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return; if (rt->color_tex >= 0) destroy_texture({ rt->color_tex }); if (rt->depth_tex >= 0) destroy_texture({ rt->depth_tex }); rts_.release(h.id); }

    // ---- texture views ------------------------------------------------------
    TextureHandle create_texture_view(const TextureViewDesc& d) override {
        auto* src = textures_.get(d.texture.id); if (!src) return { -1 };
        VKTexture v = *src; v.owns = false;   // aliases the source image/memory
        VkFormat fmt = (d.format == TextureFormat::Unknown) ? src->format : tex_format(d.format);
        int levels = d.mip_count ? d.mip_count : src->levels - d.base_mip;
        int layers = d.layer_count ? d.layer_count : src->layers - d.base_layer;
        v.format = fmt; v.levels = levels; v.layers = layers;
        v.view = make_view(*src, fmt, d.cube ? VK_IMAGE_VIEW_TYPE_CUBE : (layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D), d.base_mip, levels, d.base_layer, layers);
        return { textures_.alloc(v) };
    }

    // ---- fences / semaphores / timeline -------------------------------------
    FenceHandle create_fence(bool signaled) override {
        VkFenceCreateInfo ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; if (signaled) ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VKFence f; VK_OK(vkCreateFence(dev, &ci, nullptr, &f.fence)); return { fences_.alloc(f) };
    }
    void destroy_fence(FenceHandle h) override { auto* f = fences_.get(h.id); if (f && f->fence) vkDestroyFence(dev, f->fence, nullptr); fences_.release(h.id); }
    bool wait_fence(FenceHandle h, uint64_t timeout) override { auto* f = fences_.get(h.id); if (!f) return false; return vkWaitForFences(dev, 1, &f->fence, VK_TRUE, timeout) == VK_SUCCESS; }
    bool get_fence_status(FenceHandle h) override { auto* f = fences_.get(h.id); return f && vkGetFenceStatus(dev, f->fence) == VK_SUCCESS; }
    void reset_fence(FenceHandle h) override { auto* f = fences_.get(h.id); if (f) vkResetFences(dev, 1, &f->fence); }
    SemaphoreHandle create_semaphore() override { VkSemaphoreCreateInfo ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }; VKSem s; VK_OK(vkCreateSemaphore(dev, &ci, nullptr, &s.sem)); return { sems_.alloc(s) }; }
    void destroy_semaphore(SemaphoreHandle h) override { auto* s = sems_.get(h.id); if (s && s->sem) vkDestroySemaphore(dev, s->sem, nullptr); sems_.release(h.id); }
    void wait_idle() override { vkDeviceWaitIdle(dev); }

    TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial) override {
        VkSemaphoreTypeCreateInfo ti{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
        ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE; ti.initialValue = initial;
        VkSemaphoreCreateInfo ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO }; ci.pNext = &ti;
        VKTimeline t; VkResult r = vkCreateSemaphore(dev, &ci, nullptr, &t.sem);
        if (r != VK_SUCCESS) { vk_unsupported("timeline semaphore (enable VK 1.2 feature)"); return { -1 }; }
        return { timelines_.alloc(t) };
    }
    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override { auto* t = timelines_.get(h.id); if (t && t->sem) vkDestroySemaphore(dev, t->sem, nullptr); timelines_.release(h.id); }
    void signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value) override {
        auto* t = timelines_.get(h.id); if (!t || !vkSignalSemaphore) return;
        VkSemaphoreSignalInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO }; si.semaphore = t->sem; si.value = value; vkSignalSemaphore(dev, &si);
    }
    bool wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value, uint64_t timeout) override {
        auto* t = timelines_.get(h.id); if (!t || !vkWaitSemaphores) return false;
        VkSemaphoreWaitInfo wi{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO }; wi.semaphoreCount = 1; wi.pSemaphores = &t->sem; wi.pValues = &value;
        return vkWaitSemaphores(dev, &wi, timeout) == VK_SUCCESS;
    }
    uint64_t get_timeline_value(TimelineSemaphoreHandle h) override {
        auto* t = timelines_.get(h.id); if (!t || !vkGetSemaphoreCounterValue) return 0;
        uint64_t v = 0; vkGetSemaphoreCounterValue(dev, t->sem, &v); return v;
    }

    // ---- queries ------------------------------------------------------------
    QueryHandle create_query(QueryType type) override {
        VKQuery q; q.type = type == QueryType::Occlusion ? VK_QUERY_TYPE_OCCLUSION : (type == QueryType::PipelineStatistics ? VK_QUERY_TYPE_PIPELINE_STATISTICS : VK_QUERY_TYPE_TIMESTAMP);
        VkQueryPoolCreateInfo ci{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO }; ci.queryType = q.type; ci.queryCount = 1;
        if (q.type == VK_QUERY_TYPE_PIPELINE_STATISTICS) ci.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
        VK_OK(vkCreateQueryPool(dev, &ci, nullptr, &q.pool)); return { queries_.alloc(q) };
    }
    void destroy_query(QueryHandle h) override { auto* q = queries_.get(h.id); if (q && q->pool) vkDestroyQueryPool(dev, q->pool, nullptr); queries_.release(h.id); }
    bool get_query_result(QueryHandle h, uint64_t* out, bool wait) override {
        auto* q = queries_.get(h.id); if (!q) return false;
        uint64_t v = 0; VkResult r = vkGetQueryPoolResults(dev, q->pool, 0, 1, sizeof(v), &v, sizeof(v), VK_QUERY_RESULT_64_BIT | (wait ? VK_QUERY_RESULT_WAIT_BIT : 0));
        if (r != VK_SUCCESS) return false; if (out) *out = v; return true;
    }

    // ---- map / readback -----------------------------------------------------
    void* map_buffer(BufferHandle h, uint32_t offset, uint32_t size) override {
        auto* b = buffers_.get(h.id); if (!b) return nullptr;
        vkMapMemory(dev, b->mem, offset, size ? size : (b->size - offset), 0, &b->mapped); return b->mapped;
    }
    void unmap_buffer(BufferHandle h) override { auto* b = buffers_.get(h.id); if (b && b->mapped) { vkUnmapMemory(dev, b->mem); b->mapped = nullptr; } }
    void read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset) override {
        auto* b = buffers_.get(h.id); if (!b || !dst) return;
        void* p = nullptr; vkMapMemory(dev, b->mem, offset, size, 0, &p); std::memcpy(dst, p, size); vkUnmapMemory(dev, b->mem);
    }
    void read_texture(TextureHandle h, const TextureRegion& r, void* dst) override {
        auto* t = textures_.get(h.id); if (!t || !dst) return;
        const VkDeviceSize bytes = VkDeviceSize(r.width) * r.height * format_bytes(t->format);
        VKBuffer stage = make_staging(bytes, nullptr);
        immediate([&](VkCommandBuffer cb) {
            barrier(cb, t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy bc{}; bc.imageSubresource = { t->aspect, (uint32_t)r.mip, (uint32_t)r.layer, 1 };
            bc.imageOffset = { r.x, r.y, 0 }; bc.imageExtent = { (uint32_t)r.width, (uint32_t)r.height, 1 };
            vkCmdCopyImageToBuffer(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stage.buf, 1, &bc);
        });
        void* p = nullptr; vkMapMemory(dev, stage.mem, 0, bytes, 0, &p); std::memcpy(dst, p, size_t(bytes)); vkUnmapMemory(dev, stage.mem);
        free_staging(stage);
    }

    // ---- pipeline cache -----------------------------------------------------
    PipelineCacheHandle create_pipeline_cache(const void* data, size_t size) override {
        VkPipelineCacheCreateInfo ci{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO }; ci.initialDataSize = size; ci.pInitialData = data;
        VKPipelineCache c; VK_OK(vkCreatePipelineCache(dev, &ci, nullptr, &c.cache)); return { pcaches_.alloc(c) };
    }
    size_t get_pipeline_cache_data(PipelineCacheHandle h, void* dst, size_t cap) override {
        auto* c = pcaches_.get(h.id); if (!c) return 0; size_t n = cap; if (vkGetPipelineCacheData(dev, c->cache, &n, dst) != VK_SUCCESS && !dst) { /* n holds size */ } return n;
    }
    void destroy_pipeline_cache(PipelineCacheHandle h) override { auto* c = pcaches_.get(h.id); if (c && c->cache) vkDestroyPipelineCache(dev, c->cache, nullptr); pcaches_.release(h.id); }

    // ---- sparse / ray tracing (optional ext; logged) ------------------------
    void update_texture_residency(TextureHandle, const TextureRegion&, bool) override { vk_unsupported("sparse residency (VK_KHR sparse)"); }
    AccelStructHandle create_acceleration_structure(const AccelStructDesc&) override { vk_unsupported("acceleration structures (VK_KHR ray tracing)"); return { accels_.alloc(VKAccelStruct{}) }; }
    void destroy_acceleration_structure(AccelStructHandle h) override { accels_.release(h.id); }

    // ---- debug labels -------------------------------------------------------
    void set_debug_name(ObjectType type, uint32_t id, const char* name) override {
        if (!has_debug_utils || !name) return;
        VkObjectType ot = VK_OBJECT_TYPE_UNKNOWN; uint64_t handle = 0;
        switch (type) {
            case ObjectType::Buffer:       if (auto* b = buffers_.get(int(id)))   { ot = VK_OBJECT_TYPE_BUFFER; handle = (uint64_t)b->buf; } break;
            case ObjectType::Texture:      if (auto* t = textures_.get(int(id)))  { ot = VK_OBJECT_TYPE_IMAGE; handle = (uint64_t)t->image; } break;
            case ObjectType::Sampler:      if (auto* s = samplers_.get(int(id)))  { ot = VK_OBJECT_TYPE_SAMPLER; handle = (uint64_t)s->sampler; } break;
            case ObjectType::Shader:       if (auto* s = shaders_.get(int(id)))   { ot = VK_OBJECT_TYPE_SHADER_MODULE; handle = (uint64_t)s->mod; } break;
            case ObjectType::Pipeline:     if (auto* p = pipelines_.get(int(id))) { ot = VK_OBJECT_TYPE_PIPELINE; handle = (uint64_t)p->pipeline; } break;
            default: break;
        }
        if (handle) set_name(ot, handle, name);
    }
    void set_name(VkObjectType ot, uint64_t handle, const char* name) {
        if (!p_set_name) return;
        VkDebugUtilsObjectNameInfoEXT ni{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        ni.objectType = ot; ni.objectHandle = handle; ni.pObjectName = name; p_set_name(dev, &ni);
    }

    // accessors for the commander
    VKBuffer* buffer(int id) { return buffers_.get(id); }
    VKTexture* texture(int id) { return textures_.get(id); }
    VKPipeline* pipeline(int id) { return pipelines_.get(id); }
    VKRenderTarget* rt(int id) { return rts_.get(id); }
    VKQuery* query(int id) { return queries_.get(id); }
    VKDescriptorSet* descriptor_set(int id) { return dsets_.get(id); }
    VKFence* fence(int id) { return fences_.get(id); }
    VKTimeline* timeline(int id) { return timelines_.get(id); }

    Pool<VKBuffer> buffers_; Pool<VKTexture> textures_; Pool<VKSampler> samplers_; Pool<VKShader> shaders_;
    Pool<VKPipeline> pipelines_; Pool<VKRenderTarget> rts_; Pool<VKFence> fences_; Pool<VKSem> sems_;
    Pool<VKTimeline> timelines_; Pool<VKQuery> queries_; Pool<VKDescSetLayout> dsls_; Pool<VKPipelineLayout> plls_;
    Pool<VKDescriptorSet> dsets_; Pool<VKPipelineCache> pcaches_; Pool<VKAccelStruct> accels_;
};

//=============================================================================
// VKCommander — records into a primary command buffer
//=============================================================================
class VKCommander : public GraphicCommander {
public:
    VKCommander(VKDevice* d) : dev_(d) {
        VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = d->pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        vkAllocateCommandBuffers(d->dev, &ai, &cb_);
    }
    ~VKCommander() override { vkFreeCommandBuffers(dev_->dev, dev_->pool, 1, &cb_); }
    VKDevice* device() const { return dev_; }
    VkCommandBuffer cmd() const { return cb_; }

    void begin() override { vkResetCommandBuffer(cb_, 0); VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb_, &bi); in_pass_ = false; }
    void end() override { end_pass(); vkEndCommandBuffer(cb_); }

    void set_render_target_backbuffer() override { end_pass(); color_tex_ = -1; }   // present path TODO
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle) override {
        end_pass(); color_tex_ = -1;
        if (count > 0 && colors) if (auto* rt = dev_->rt(colors[0].id)) color_tex_ = rt->color_tex;
    }
    void set_viewport(const Viewport& v) override { VkViewport vp{ v.x, v.y, v.width, v.height, v.min_depth, v.max_depth }; vkCmdSetViewport(cb_, 0, 1, &vp); }
    void set_scissor(const ScissorRect& r) override { VkRect2D s{ { r.x, r.y }, { (uint32_t)r.width, (uint32_t)r.height } }; vkCmdSetScissor(cb_, 0, 1, &s); }
    void clear_color(const ClearColor& c) override {
        auto* color = dev_->texture(color_tex_); if (!color) return;
        VKDevice::barrier(cb_, color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearColorValue cc{}; cc.float32[0] = c.r; cc.float32[1] = c.g; cc.float32[2] = c.b; cc.float32[3] = c.a;
        VkImageSubresourceRange rng{ color->aspect, 0, (uint32_t)color->levels, 0, (uint32_t)color->layers };
        vkCmdClearColorImage(cb_, color->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rng);
    }
    void clear_depth_stencil(const ClearDepthStencil&) override { vk_unsupported("clear_depth_stencil (no bound depth path yet)"); }
    void set_pipeline(PipelineHandle h) override { auto* p = dev_->pipeline(h.id); if (!p) return; cur_pipeline_ = p; vkCmdBindPipeline(cb_, p->bind, p->pipeline); }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; VkDeviceSize off = offset; vkCmdBindVertexBuffers(cb_, slot, 1, &b->buf, &off); }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; vkCmdBindIndexBuffer(cb_, b->buf, offset, fmt == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32); }
    void bind_texture(uint32_t, TextureHandle) override {}        // Vulkan binds via descriptor sets
    void bind_sampler(uint32_t, SamplerHandle) override {}
    void bind_uniform_buffer(uint32_t, BufferHandle, uint32_t, uint32_t) override {}
    void push_constants(uint32_t offset, const void* data, uint32_t size) override { if (cur_pipeline_ && cur_pipeline_->layout) vkCmdPushConstants(cb_, cur_pipeline_->layout, VK_SHADER_STAGE_ALL, offset, size, data); }
    void bind_storage_buffer(uint32_t, BufferHandle, uint32_t, uint32_t) override {}
    void bind_storage_texture(uint32_t, TextureHandle, int, StorageAccess) override {}
    void bind_descriptor_set(uint32_t set_index, DescriptorSetHandle h, const uint32_t* dyn, int dyn_count) override {
        auto* ds = dev_->descriptor_set(h.id); if (!ds || !cur_pipeline_) return;
        vkCmdBindDescriptorSets(cb_, cur_pipeline_->bind, cur_pipeline_->layout, set_index, 1, &ds->set, (uint32_t)dyn_count, dyn);
    }
    void draw(uint32_t vc, uint32_t first, uint32_t inst, uint32_t first_inst) override { ensure_pass(); vkCmdDraw(cb_, vc, inst ? inst : 1, first, first_inst); }
    void draw_indexed(uint32_t ic, uint32_t first, int32_t base_v, uint32_t inst, uint32_t first_inst) override { ensure_pass(); vkCmdDrawIndexed(cb_, ic, inst ? inst : 1, first, base_v, first_inst); }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { vkCmdDispatch(cb_, x, y, z); }
    void draw_indirect(BufferHandle a, uint32_t off, uint32_t count, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); if (b) vkCmdDrawIndirect(cb_, b->buf, off, count, stride ? stride : sizeof(VkDrawIndirectCommand)); }
    void draw_indexed_indirect(BufferHandle a, uint32_t off, uint32_t count, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); if (b) vkCmdDrawIndexedIndirect(cb_, b->buf, off, count, stride ? stride : sizeof(VkDrawIndexedIndirectCommand)); }
    void dispatch_indirect(BufferHandle a, uint32_t off) override { auto* b = dev_->buffer(a.id); if (b) vkCmdDispatchIndirect(cb_, b->buf, off); }
    void draw_mesh_tasks(uint32_t, uint32_t, uint32_t) override { vk_unsupported("draw_mesh_tasks"); }
    void draw_mesh_tasks_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { vk_unsupported("draw_mesh_tasks_indirect"); }
    void memory_barrier(uint32_t) override {
        VkMemoryBarrier b{ VK_STRUCTURE_TYPE_MEMORY_BARRIER }; b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cb_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
    }
    void copy_buffer(BufferHandle dst, uint32_t doff, BufferHandle src, uint32_t soff, uint32_t size) override {
        auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (!s || !d) return;
        VkBufferCopy c{ soff, doff, size }; vkCmdCopyBuffer(cb_, s->buf, d->buf, 1, &c);
    }
    void copy_texture(TextureHandle dst, const TextureRegion& dr, TextureHandle src, const TextureRegion& sr) override {
        auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (!s || !d) return;
        VKDevice::barrier(cb_, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL); VKDevice::barrier(cb_, d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy c{}; c.srcSubresource = { s->aspect, (uint32_t)sr.mip, (uint32_t)sr.layer, 1 }; c.srcOffset = { sr.x, sr.y, 0 };
        c.dstSubresource = { d->aspect, (uint32_t)dr.mip, (uint32_t)dr.layer, 1 }; c.dstOffset = { dr.x, dr.y, 0 };
        c.extent = { (uint32_t)(sr.width ? sr.width : s->w), (uint32_t)(sr.height ? sr.height : s->h), 1 };
        vkCmdCopyImage(cb_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }
    void blit_render_target(RenderTargetHandle dh, RenderTargetHandle sh, int sx0,int sy0,int sx1,int sy1,int dx0,int dy0,int dx1,int dy1, bool linear) override {
        auto* srt = dev_->rt(sh.id); auto* drt = dev_->rt(dh.id); if (!srt || !drt) return;
        auto* s = dev_->texture(srt->color_tex); auto* d = dev_->texture(drt->color_tex); if (!s || !d) return;
        VKDevice::barrier(cb_, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL); VKDevice::barrier(cb_, d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit bl{}; bl.srcSubresource = { s->aspect, 0, 0, 1 }; bl.dstSubresource = { d->aspect, 0, 0, 1 };
        bl.srcOffsets[0] = { sx0, sy0, 0 }; bl.srcOffsets[1] = { sx1, sy1, 1 };
        bl.dstOffsets[0] = { dx0, dy0, 0 }; bl.dstOffsets[1] = { dx1, dy1, 1 };
        vkCmdBlitImage(cb_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    }
    void resolve_render_target(RenderTargetHandle dh, RenderTargetHandle sh) override {
        auto* srt = dev_->rt(sh.id); auto* drt = dev_->rt(dh.id); if (!srt || !drt) return;
        auto* s = dev_->texture(srt->color_tex); auto* d = dev_->texture(drt->color_tex); if (!s || !d) return;
        VkImageResolve rr{}; rr.srcSubresource = { s->aspect, 0, 0, 1 }; rr.dstSubresource = { d->aspect, 0, 0, 1 }; rr.extent = { (uint32_t)d->w, (uint32_t)d->h, 1 };
        vkCmdResolveImage(cb_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rr);
    }
    void write_timestamp(QueryHandle h) override { if (auto* q = dev_->query(h.id)) { vkCmdResetQueryPool(cb_, q->pool, 0, 1); vkCmdWriteTimestamp(cb_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q->pool, 0); } }
    void begin_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) { vkCmdResetQueryPool(cb_, q->pool, 0, 1); vkCmdBeginQuery(cb_, q->pool, 0, 0); } }
    void end_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) vkCmdEndQuery(cb_, q->pool, 0); }
    void push_debug_group(const char* name) override { if (dev_->p_begin_label) { VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT }; l.pLabelName = name ? name : ""; dev_->p_begin_label(cb_, &l); } }
    void pop_debug_group() override { if (dev_->p_end_label) dev_->p_end_label(cb_); }
    void insert_debug_marker(const char* name) override { if (dev_->p_insert_label) { VkDebugUtilsLabelEXT l{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT }; l.pLabelName = name ? name : ""; dev_->p_insert_label(cb_, &l); } }
    void set_stencil_reference(uint32_t ref) override { vkCmdSetStencilReference(cb_, VK_STENCIL_FACE_FRONT_AND_BACK, ref); }
    void set_blend_constants(const float rgba[4]) override { if (rgba) vkCmdSetBlendConstants(cb_, rgba); }
    void set_depth_bias(float c, float clamp, float slope) override { vkCmdSetDepthBias(cb_, c, clamp, slope); }
    void set_line_width(float w) override { vkCmdSetLineWidth(cb_, w); }
    void set_viewports(const Viewport* v, int n) override { std::vector<VkViewport> vs(n); for (int i=0;i<n;++i) vs[i]={v[i].x,v[i].y,v[i].width,v[i].height,v[i].min_depth,v[i].max_depth}; vkCmdSetViewport(cb_,0,(uint32_t)n,vs.data()); }
    void set_scissors(const ScissorRect* r, int n) override { std::vector<VkRect2D> rs(n); for (int i=0;i<n;++i) rs[i]={{r[i].x,r[i].y},{(uint32_t)r[i].width,(uint32_t)r[i].height}}; vkCmdSetScissor(cb_,0,(uint32_t)n,rs.data()); }
    void draw_indirect_count(BufferHandle a, uint32_t ao, BufferHandle cnt, uint32_t co, uint32_t maxd, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); auto* c = dev_->buffer(cnt.id); if (b && c && vkCmdDrawIndirectCount) vkCmdDrawIndirectCount(cb_, b->buf, ao, c->buf, co, maxd, stride ? stride : sizeof(VkDrawIndirectCommand)); }
    void draw_indexed_indirect_count(BufferHandle a, uint32_t ao, BufferHandle cnt, uint32_t co, uint32_t maxd, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); auto* c = dev_->buffer(cnt.id); if (b && c && vkCmdDrawIndexedIndirectCount) vkCmdDrawIndexedIndirectCount(cb_, b->buf, ao, c->buf, co, maxd, stride ? stride : sizeof(VkDrawIndexedIndirectCommand)); }
    void build_acceleration_structure(AccelStructHandle, const AccelStructDesc&) override { vk_unsupported("build_acceleration_structure"); }
    void trace_rays(uint32_t, uint32_t, uint32_t) override { vk_unsupported("trace_rays"); }

private:
    void ensure_pass() {
        if (in_pass_ || !cur_pipeline_ || cur_pipeline_->bind != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
        auto* color = dev_->texture(color_tex_); if (!color) return;   // look up fresh: the Pool may have reallocated
        // Begin a transient framebuffer + render pass matching the bound target.
        VKDevice::barrier(cb_, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = cur_pipeline_->render_pass; fci.attachmentCount = 1; fci.pAttachments = &color->view;
        fci.width = color->w; fci.height = color->h; fci.layers = 1;
        vkCreateFramebuffer(dev_->dev, &fci, nullptr, &fb_);
        VkRenderPassBeginInfo rbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rbi.renderPass = cur_pipeline_->render_pass; rbi.framebuffer = fb_;
        rbi.renderArea = { { 0, 0 }, { (uint32_t)color->w, (uint32_t)color->h } };
        vkCmdBeginRenderPass(cb_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        in_pass_ = true;
    }
    void end_pass() { if (in_pass_) { vkCmdEndRenderPass(cb_); in_pass_ = false; } if (fb_) { vkDestroyFramebuffer(dev_->dev, fb_, nullptr); fb_ = VK_NULL_HANDLE; } }

    VKDevice* dev_ = nullptr;
    VkCommandBuffer cb_ = VK_NULL_HANDLE;
    VkFramebuffer fb_ = VK_NULL_HANDLE;
    int color_tex_ = -1;   // bound colour target's texture id (looked up per use; Pool may move)
    VKPipeline* cur_pipeline_ = nullptr;
    bool in_pass_ = false;
};

} // namespace

//-----------------------------------------------------------------------------
// Factory
//-----------------------------------------------------------------------------
GraphicDevice* create_device_vulkan(Graphics* context, Result* out_result) {
    VulkanGraphicsInfo gi;
    if (!context || !context->get_vulkan_info(&gi) || !gi.device) { if (out_result) *out_result = Result::ErrorNotSupported; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new VKDevice(gi);
}
GraphicCommander* create_commander_vulkan(Graphics* context, GraphicDevice* device, QueueType, Result* out_result) {
    if (!context || !device) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new VKCommander(static_cast<VKDevice*>(device));
}
void submit_commander_vulkan(Graphics*, GraphicCommander* commander, FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    if (!commander) return;
    auto* c = static_cast<VKCommander*>(commander); auto* d = c->device();
    VkCommandBuffer cb = c->cmd();
    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO }; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    VkTimelineSemaphoreSubmitInfo ts{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    VkSemaphore tl_sem = VK_NULL_HANDLE;
    if (timeline.valid()) { if (auto* t = d->timeline(timeline.id)) { tl_sem = t->sem; ts.signalSemaphoreValueCount = 1; ts.pSignalSemaphoreValues = &value; si.pNext = &ts; si.signalSemaphoreCount = 1; si.pSignalSemaphores = &tl_sem; } }
    VkFence f = VK_NULL_HANDLE; if (fence.valid()) if (auto* fh = d->fence(fence.id)) f = fh->fence;
    vkQueueSubmit(d->queue, 1, &si, f);
}

} // namespace window

#else  // !WINDOW_SUPPORT_VULKAN — not-supported stubs so the dispatcher links

namespace window {
GraphicDevice*    create_device_vulkan(Graphics*, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
GraphicCommander* create_commander_vulkan(Graphics*, GraphicDevice*, QueueType, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
void              submit_commander_vulkan(Graphics*, GraphicCommander*, FenceHandle, TimelineSemaphoreHandle, uint64_t) {}
} // namespace window

#endif
