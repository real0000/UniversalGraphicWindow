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
#include <map>
#include <unordered_map>
#include <vector>

// SPIRV-Cross (linked with the shader compiler) reflects a module's set-0 bindings so that
// pipelines built WITHOUT an explicit pipeline layout get an auto descriptor-set layout. That
// makes slot-based bind_uniform_buffer/bind_texture/bind_sampler/bind_storage_buffer work on
// Vulkan exactly like GL/D3D11 (the commander auto-allocates + writes a descriptor set per draw).
#if defined(WINDOW_SUPPORT_SHADER_COMPILER)
#include <spirv_cross.hpp>
#define VK_AUTO_BIND 1
#endif

namespace window {
namespace {

void vk_unsupported(const char* what) {
    static std::unordered_map<const char*, bool> seen;
    if (!seen[what]) { seen[what] = true; std::fprintf(stderr, "[UGW/Vulkan] %s not supported (no-op)\n", what); }
}

#ifdef VK_AUTO_BIND
// Reflect set-0 resource bindings of one SPIR-V stage into descriptor-set-layout bindings.
void reflect_set0_bindings(const uint32_t* code, size_t bytes, VkShaderStageFlags stage,
                           std::vector<VkDescriptorSetLayoutBinding>& out) {
    try {
        spirv_cross::Compiler comp(code, bytes / 4);
        spirv_cross::ShaderResources res = comp.get_shader_resources();
        auto add = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& list, VkDescriptorType type) {
            for (const auto& r : list) {
                if (comp.get_decoration(r.id, spv::DecorationDescriptorSet) != 0) continue;  // set 0 only
                VkDescriptorSetLayoutBinding b{};
                b.binding = comp.get_decoration(r.id, spv::DecorationBinding);
                b.descriptorType = type; b.descriptorCount = 1; b.stageFlags = stage;
                out.push_back(b);
            }
        };
        add(res.uniform_buffers,   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        add(res.storage_buffers,   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        add(res.sampled_images,    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        add(res.separate_images,   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        add(res.separate_samplers, VK_DESCRIPTOR_TYPE_SAMPLER);
        add(res.storage_images,    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    } catch (...) {}
}
#endif

// ---- State mapping (honour the requested PipelineDesc, not a hardcode) -------
VkBlendFactor vk_blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero:          return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One:           return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor:      return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::InvSrcColor:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:      return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:   return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:      return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::InvDstColor:   return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:      return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::InvDstAlpha:   return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcAlphaSat:   return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case BlendFactor::BlendFactor:   return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::InvBlendFactor:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    }
    return VK_BLEND_FACTOR_ONE;
}
VkBlendOp vk_blend_op(BlendOp o) {
    switch (o) {
        case BlendOp::Add:        return VK_BLEND_OP_ADD;
        case BlendOp::Subtract:   return VK_BLEND_OP_SUBTRACT;
        case BlendOp::RevSubtract:return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min:        return VK_BLEND_OP_MIN;
        case BlendOp::Max:        return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}
VkSampleCountFlagBits vk_samples(int n) {
    switch (n) {
        case 64: return VK_SAMPLE_COUNT_64_BIT; case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT; case 8:  return VK_SAMPLE_COUNT_8_BIT;
        case 4:  return VK_SAMPLE_COUNT_4_BIT;  case 2:  return VK_SAMPLE_COUNT_2_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}
VkCompareOp vk_compare(CompareFunc f) {
    switch (f) {
        case CompareFunc::Never:        return VK_COMPARE_OP_NEVER;
        case CompareFunc::Less:         return VK_COMPARE_OP_LESS;
        case CompareFunc::Equal:        return VK_COMPARE_OP_EQUAL;
        case CompareFunc::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareFunc::Greater:      return VK_COMPARE_OP_GREATER;
        case CompareFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareFunc::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareFunc::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}
VkStencilOp vk_stencil_op(StencilOp o) {
    switch (o) {
        case StencilOp::Keep:     return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero:     return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace:  return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrSat:  return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrSat:  return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert:   return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
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
        // Block-compressed: BCn (desktop), ETC2/EAC + ASTC (mobile; needs device support).
        case TextureFormat::BC1_UNORM:         return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC1_UNORM_SRGB:    return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case TextureFormat::BC2_UNORM:         return VK_FORMAT_BC2_UNORM_BLOCK;
        case TextureFormat::BC2_UNORM_SRGB:    return VK_FORMAT_BC2_SRGB_BLOCK;
        case TextureFormat::BC3_UNORM:         return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC3_UNORM_SRGB:    return VK_FORMAT_BC3_SRGB_BLOCK;
        case TextureFormat::BC4_UNORM:         return VK_FORMAT_BC4_UNORM_BLOCK;
        case TextureFormat::BC4_SNORM:         return VK_FORMAT_BC4_SNORM_BLOCK;
        case TextureFormat::BC5_UNORM:         return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC5_SNORM:         return VK_FORMAT_BC5_SNORM_BLOCK;
        case TextureFormat::BC6H_UF16:         return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case TextureFormat::BC6H_SF16:         return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case TextureFormat::BC7_UNORM:         return VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::BC7_UNORM_SRGB:    return VK_FORMAT_BC7_SRGB_BLOCK;
        case TextureFormat::ETC2_RGB8:         return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case TextureFormat::ETC2_RGB8_SRGB:    return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case TextureFormat::ETC2_RGB8A1:       return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
        case TextureFormat::ETC2_RGB8A1_SRGB:  return VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK;
        case TextureFormat::ETC2_RGBA8:        return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case TextureFormat::ETC2_RGBA8_SRGB:   return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case TextureFormat::EAC_R11_UNORM:     return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case TextureFormat::EAC_R11_SNORM:     return VK_FORMAT_EAC_R11_SNORM_BLOCK;
        case TextureFormat::EAC_RG11_UNORM:    return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
        case TextureFormat::EAC_RG11_SNORM:    return VK_FORMAT_EAC_R11G11_SNORM_BLOCK;
        case TextureFormat::ASTC_4x4_UNORM:    return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case TextureFormat::ASTC_4x4_SRGB:     return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case TextureFormat::ASTC_5x4_UNORM:    return VK_FORMAT_ASTC_5x4_UNORM_BLOCK;
        case TextureFormat::ASTC_5x4_SRGB:     return VK_FORMAT_ASTC_5x4_SRGB_BLOCK;
        case TextureFormat::ASTC_5x5_UNORM:    return VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
        case TextureFormat::ASTC_5x5_SRGB:     return VK_FORMAT_ASTC_5x5_SRGB_BLOCK;
        case TextureFormat::ASTC_6x5_UNORM:    return VK_FORMAT_ASTC_6x5_UNORM_BLOCK;
        case TextureFormat::ASTC_6x5_SRGB:     return VK_FORMAT_ASTC_6x5_SRGB_BLOCK;
        case TextureFormat::ASTC_6x6_UNORM:    return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case TextureFormat::ASTC_6x6_SRGB:     return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case TextureFormat::ASTC_8x5_UNORM:    return VK_FORMAT_ASTC_8x5_UNORM_BLOCK;
        case TextureFormat::ASTC_8x5_SRGB:     return VK_FORMAT_ASTC_8x5_SRGB_BLOCK;
        case TextureFormat::ASTC_8x6_UNORM:    return VK_FORMAT_ASTC_8x6_UNORM_BLOCK;
        case TextureFormat::ASTC_8x6_SRGB:     return VK_FORMAT_ASTC_8x6_SRGB_BLOCK;
        case TextureFormat::ASTC_8x8_UNORM:    return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        case TextureFormat::ASTC_8x8_SRGB:     return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
        case TextureFormat::ASTC_10x5_UNORM:   return VK_FORMAT_ASTC_10x5_UNORM_BLOCK;
        case TextureFormat::ASTC_10x5_SRGB:    return VK_FORMAT_ASTC_10x5_SRGB_BLOCK;
        case TextureFormat::ASTC_10x6_UNORM:   return VK_FORMAT_ASTC_10x6_UNORM_BLOCK;
        case TextureFormat::ASTC_10x6_SRGB:    return VK_FORMAT_ASTC_10x6_SRGB_BLOCK;
        case TextureFormat::ASTC_10x8_UNORM:   return VK_FORMAT_ASTC_10x8_UNORM_BLOCK;
        case TextureFormat::ASTC_10x8_SRGB:    return VK_FORMAT_ASTC_10x8_SRGB_BLOCK;
        case TextureFormat::ASTC_10x10_UNORM:  return VK_FORMAT_ASTC_10x10_UNORM_BLOCK;
        case TextureFormat::ASTC_10x10_SRGB:   return VK_FORMAT_ASTC_10x10_SRGB_BLOCK;
        case TextureFormat::ASTC_12x10_UNORM:  return VK_FORMAT_ASTC_12x10_UNORM_BLOCK;
        case TextureFormat::ASTC_12x10_SRGB:   return VK_FORMAT_ASTC_12x10_SRGB_BLOCK;
        case TextureFormat::ASTC_12x12_UNORM:  return VK_FORMAT_ASTC_12x12_UNORM_BLOCK;
        case TextureFormat::ASTC_12x12_SRGB:   return VK_FORMAT_ASTC_12x12_SRGB_BLOCK;
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
        case ShaderStage::Task:        return VK_SHADER_STAGE_TASK_BIT_EXT;
        case ShaderStage::Mesh:        return VK_SHADER_STAGE_MESH_BIT_EXT;
        case ShaderStage::RayGen:      return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case ShaderStage::Miss:        return VK_SHADER_STAGE_MISS_BIT_KHR;
        case ShaderStage::ClosestHit:  return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        case ShaderStage::AnyHit:      return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        case ShaderStage::Intersection:return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
        case ShaderStage::Callable:    return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
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
        case BindingType::AccelerationStructure: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
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
    if (bits & STAGE_TASK)         f |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (bits & STAGE_MESH)         f |= VK_SHADER_STAGE_MESH_BIT_EXT;
    if (bits & STAGE_RAYGEN)       f |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    if (bits & STAGE_MISS)         f |= VK_SHADER_STAGE_MISS_BIT_KHR;
    if (bits & STAGE_CLOSEST_HIT)  f |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    if (bits & STAGE_ANY_HIT)      f |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    if (bits & STAGE_INTERSECTION) f |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    if (bits & STAGE_CALLABLE)     f |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    return f ? f : VK_SHADER_STAGE_ALL;
}

//-----------------------------------------------------------------------------
// Resource records
//-----------------------------------------------------------------------------
struct VKBuffer  { VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkDeviceSize size = 0; void* mapped = nullptr; };
struct VKTexture { VkImage image = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
                   VkFormat format = VK_FORMAT_UNDEFINED; TextureFormat tf = TextureFormat::RGBA8_UNORM; VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                   int w = 0, h = 0, layers = 1, levels = 1; VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; bool owns = true; };
struct VKSampler { VkSampler sampler = VK_NULL_HANDLE; };
struct VKShader  { VkShaderModule mod = VK_NULL_HANDLE; ShaderStage stage = ShaderStage::Vertex; std::string entry = "main";
                   std::vector<VkDescriptorSetLayoutBinding> bindings; };   // reflected set-0 bindings
struct VKPipeline{ VkPipeline pipeline = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; bool owns_layout = false;
                   VkRenderPass render_pass = VK_NULL_HANDLE; VkPipelineBindPoint bind = VK_PIPELINE_BIND_POINT_GRAPHICS;
                   bool has_depth = false; bool mesh = false;
                   // Auto descriptor-set layout built from shader reflection (when no explicit
                   // PipelineDesc::layout). auto_bindings drives per-draw descriptor writes.
                   VkDescriptorSetLayout auto_dsl = VK_NULL_HANDLE;
                   std::vector<VkDescriptorSetLayoutBinding> auto_bindings; };
struct VKRenderTarget { int color_tex = -1; int depth_tex = -1; };
struct VKFence   { VkFence fence = VK_NULL_HANDLE; };
struct VKSem     { VkSemaphore sem = VK_NULL_HANDLE; };
struct VKTimeline{ VkSemaphore sem = VK_NULL_HANDLE; };
struct VKQuery   { VkQueryPool pool = VK_NULL_HANDLE; VkQueryType type = VK_QUERY_TYPE_TIMESTAMP; };
struct VKDescSetLayout  { VkDescriptorSetLayout layout = VK_NULL_HANDLE; DescriptorSetLayoutDesc desc; };
struct VKPipelineLayout { VkPipelineLayout layout = VK_NULL_HANDLE; };
struct VKDescriptorSet  { VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE; DescriptorSetLayoutHandle layout;
                          // Storage images must be in GENERAL when read/written, but a descriptor set is
                          // written without a command buffer — so the images are recorded here and
                          // transitioned when the set is bound.
                          std::vector<int> storage_textures; };
struct VKPipelineCache  { VkPipelineCache cache = VK_NULL_HANDLE; };
// A ray-tracing acceleration structure: the handle plus the buffers backing its result and
// its build scratch. Vulkan keeps these as ordinary buffers the caller must allocate.
// A ray-tracing pipeline plus the shader binding table built from its group handles.
struct VKRayPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE; bool owns_layout = false;
    VkBuffer sbt = VK_NULL_HANDLE; VkDeviceMemory sbt_mem = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR raygen{}, miss{}, hit{};
};
struct VKAccelStruct {
    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    VkBuffer result = VK_NULL_HANDLE;  VkDeviceMemory result_mem = VK_NULL_HANDLE;
    VkBuffer scratch = VK_NULL_HANDLE; VkDeviceMemory scratch_mem = VK_NULL_HANDLE;
    VkDeviceSize result_size = 0, scratch_size = 0;
    VkDeviceAddress address = 0;
};

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

    // Swapchain present path (set_render_target_backbuffer): each swapchain image is wrapped as
    // a VKTexture (owns=false; we own only its view). One acquire/render-finished semaphore pair
    // and a full queue-idle after each present keep the (interactive) windowed loop simple+correct.
    VkSwapchainKHR    swapchain_ = VK_NULL_HANDLE;
    VkSurfaceKHR      surface_   = VK_NULL_HANDLE;
    VkFormat          sc_format_ = VK_FORMAT_UNDEFINED;
    std::vector<int>  bb_ids_;                       // texture-pool ids of the wrapped images
    VkSemaphore       acquire_sem_ = VK_NULL_HANDLE; // signalled by vkAcquireNextImageKHR
    VkSemaphore       render_sem_  = VK_NULL_HANDLE; // signalled by submit, waited by present
    uint32_t          acquired_index_ = 0;
    bool              have_acquired_  = false;

    // Optional features and their entry points (see the constructor).
    bool mesh_shaders_enabled_ = false, ray_tracing_enabled_ = false;
    PFN_vkCmdDrawMeshTasksEXT                        p_draw_mesh = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR      p_get_as_build_sizes = nullptr;
    PFN_vkCreateAccelerationStructureKHR             p_create_as = nullptr;
    PFN_vkDestroyAccelerationStructureKHR            p_destroy_as = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR          p_cmd_build_as = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR   p_get_as_addr = nullptr;
    PFN_vkGetBufferDeviceAddress                     p_get_buf_addr = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR               p_create_rt_pipes = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR         p_get_group_handles = nullptr;
    PFN_vkCmdTraceRaysKHR                            p_cmd_trace = nullptr;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR  rt_props_{};

    explicit VKDevice(const VulkanGraphicsInfo& gi) {
        instance = (VkInstance)gi.instance; phys = (VkPhysicalDevice)gi.physical_device;
        dev = (VkDevice)gi.device; queue = (VkQueue)gi.graphics_queue; queue_family = gi.graphics_queue_family;
        swapchain_ = (VkSwapchainKHR)gi.swapchain; surface_ = (VkSurfaceKHR)gi.surface; sc_format_ = (VkFormat)gi.swapchain_format;
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = queue_family;
        VK_OK(vkCreateCommandPool(dev, &pci, nullptr, &pool));
        p_set_name     = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
        p_begin_label  = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
        p_end_label    = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");
        p_insert_label = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdInsertDebugUtilsLabelEXT");
        has_debug_utils = (p_set_name != nullptr);

        // Optional feature entry points. These live in extensions, so they must be looked up
        // and are only valid when the CONTEXT enabled the extension at device-creation time —
        // hence the flags come from the context rather than from a capability probe.
        mesh_shaders_enabled_ = gi.mesh_shader;
        ray_tracing_enabled_  = gi.ray_tracing;
        if (mesh_shaders_enabled_)
            p_draw_mesh = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(dev, "vkCmdDrawMeshTasksEXT");
        if (ray_tracing_enabled_) {
            p_get_as_build_sizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR");
            p_create_as          = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR");
            p_destroy_as         = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR");
            p_cmd_build_as       = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR");
            p_get_as_addr        = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR");
            p_get_buf_addr       = (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(dev, "vkGetBufferDeviceAddress");
            p_create_rt_pipes    = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(dev, "vkCreateRayTracingPipelinesKHR");
            p_get_group_handles  = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(dev, "vkGetRayTracingShaderGroupHandlesKHR");
            p_cmd_trace          = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(dev, "vkCmdTraceRaysKHR");
            rt_props_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            VkPhysicalDeviceProperties2 p2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
            p2.pNext = &rt_props_;
            vkGetPhysicalDeviceProperties2(phys, &p2);
        }
    }
    ~VKDevice() override {
        for (int id : bb_ids_) if (auto* t = textures_.get(id)) if (t->view) vkDestroyImageView(dev, t->view, nullptr);
        if (acquire_sem_) vkDestroySemaphore(dev, acquire_sem_, nullptr);
        if (render_sem_)  vkDestroySemaphore(dev, render_sem_, nullptr);
        if (pool) vkDestroyCommandPool(dev, pool, nullptr);
    }

    Backend get_backend() const override { return Backend::Vulkan; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (!out) return;
        VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(phys, &p);
        const auto& l = p.limits;
        out->max_texture_size = int(l.maxImageDimension2D);
        out->max_texture_array_layers = int(l.maxImageArrayLayers);
        out->max_color_attachments = int(l.maxColorAttachments);
        // Highest MSAA sample count usable for both colour and depth framebuffers.
        {
            VkSampleCountFlags s = l.framebufferColorSampleCounts & l.framebufferDepthSampleCounts;
            int ms = 1;
            for (int n : { 64, 32, 16, 8, 4, 2 }) if (s & VkSampleCountFlags(n)) { ms = n; break; }
            out->max_samples = ms;
        }
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
        out->read_write_textures = true;   // storage images (see bind_storage_texture)
        // Report what the CONTEXT actually enabled, not what the physical device could do:
        // a Vulkan extension cannot be switched on after device creation, so a driver that
        // supports ray tracing on a device created without it still cannot use it here.
        out->mesh_shaders = mesh_shaders_enabled_;
        out->ray_tracing  = ray_tracing_enabled_;
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

    // ---- swapchain present path ---------------------------------------------
    // Lazily wrap each swapchain image as a (non-owning) VKTexture so the existing
    // clear/render-pass paths work against it, and create the present semaphores.
    void ensure_backbuffers() {
        if (!bb_ids_.empty() || !swapchain_) return;
        uint32_t count = 0; vkGetSwapchainImagesKHR(dev, swapchain_, &count, nullptr);
        if (!count) return;
        std::vector<VkImage> images(count); vkGetSwapchainImagesKHR(dev, swapchain_, &count, images.data());
        VkSurfaceCapabilitiesKHR caps{}; if (surface_) vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface_, &caps);
        int w = (int)caps.currentExtent.width, h = (int)caps.currentExtent.height;
        if (w <= 0 || (uint32_t)w == 0xFFFFFFFFu) w = 1;
        if (h <= 0 || (uint32_t)h == 0xFFFFFFFFu) h = 1;
        for (uint32_t i = 0; i < count; ++i) {
            VKTexture t; t.image = images[i]; t.format = sc_format_; t.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            t.w = w; t.h = h; t.layers = 1; t.levels = 1; t.layout = VK_IMAGE_LAYOUT_UNDEFINED; t.owns = false;
            VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            vi.image = images[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = sc_format_;
            vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCreateImageView(dev, &vi, nullptr, &t.view);
            bb_ids_.push_back(textures_.alloc(t));
        }
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(dev, &sci, nullptr, &acquire_sem_);
        vkCreateSemaphore(dev, &sci, nullptr, &render_sem_);
    }
    // Acquire the next image; returns its wrapped texture id (or -1 if no swapchain).
    int acquire_backbuffer() {
        ensure_backbuffers();
        if (bb_ids_.empty()) return -1;
        vkAcquireNextImageKHR(dev, swapchain_, ~0ull, acquire_sem_, VK_NULL_HANDLE, &acquired_index_);
        have_acquired_ = true;
        // The previous contents are gone after present; treat as UNDEFINED for the next barrier.
        if (auto* t = textures_.get(bb_ids_[acquired_index_])) t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return bb_ids_[acquired_index_];
    }
    // Present the acquired image (waits the render-finished semaphore the submit signalled), then
    // idle the queue so the single semaphore pair is safe to reuse next frame.
    void present_backbuffer() {
        if (!have_acquired_) return;
        VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &render_sem_;
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &acquired_index_;
        vkQueuePresentKHR(queue, &pi);
        vkQueueWaitIdle(queue);
        have_acquired_ = false;
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
        // With ray tracing enabled, any buffer may end up feeding an acceleration-structure
        // build or being addressed by a shader, and Vulkan requires those usages to be
        // declared at creation. Added unconditionally there so callers need no RT-specific
        // buffer type; it costs nothing on a device without the extension because the flags
        // are only added when it is enabled.
        if (ray_tracing_enabled_) {
            usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                   | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        }
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = b.size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_OK(vkCreateBuffer(dev, &bi, nullptr, &b.buf));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, b.buf, &req);
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mi.allocationSize = req.size;
        mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Taking a device address from a buffer requires its memory to have been allocated
        // with the matching flag; without it vkGetBufferDeviceAddress is invalid.
        VkMemoryAllocateFlagsInfo maf{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        if (ray_tracing_enabled_) { maf.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT; mi.pNext = &maf; }
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
        VKTexture t; t.format = tex_format(d.format); t.tf = d.format; t.w = d.width; t.h = d.height;
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
        ii.mipLevels = t.levels; ii.arrayLayers = t.layers; ii.samples = vk_samples(d.samples);
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
        // Block-aware tight size (per layer) so compressed payloads aren't truncated.
        const VkDeviceSize bytes = VkDeviceSize(texture_format_image_size(t.tf, t.w, t.h)) * t.layers;
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
        const VkDeviceSize bytes = texture_format_image_size(t->tf, reg.width, reg.height);
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
    // Successive halving blits, mip N-1 -> mip N, for every array layer. Each level is
    // transitioned to TRANSFER_SRC once it has been written, so the next blit reads a
    // level the GPU has finished with; the whole image ends in SHADER_READ_ONLY.
    //
    // Blitting requires the format to advertise linear filtering as a blit source, which
    // is not guaranteed (notably for compressed formats) — checked rather than assumed,
    // since a failed blit here would silently leave garbage in the tail mips.
    void generate_mipmaps(TextureHandle h) override {
        auto* t = textures_.get(h.id);
        if (!t || !t->image) return;
        if (t->levels <= 1) return;   // nothing to generate
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(phys, t->format, &fp);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((fp.optimalTilingFeatures & need) != need) {
            vk_unsupported("generate_mipmaps (format cannot be linearly blitted)");
            return;
        }
        const uint32_t levels = (uint32_t)t->levels, layers = (uint32_t)t->layers;
        immediate([&](VkCommandBuffer cb) {
            // Per-level transitions, so this does not disturb the tracked whole-image layout
            // until the end. Start from whatever the image currently is.
            auto transition = [&](uint32_t level, VkImageLayout from, VkImageLayout to) {
                VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                b.oldLayout = from; b.newLayout = to; b.image = t->image;
                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.subresourceRange = { t->aspect, level, 1, 0, layers };
                b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
                b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                     0, 0, nullptr, 0, nullptr, 1, &b);
            };
            transition(0, t->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            int32_t mw = t->w, mh = t->h;
            for (uint32_t level = 1; level < levels; ++level) {
                const int32_t nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
                transition(level, t->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkImageBlit bl{};
                bl.srcSubresource = { t->aspect, level - 1, 0, layers };
                bl.dstSubresource = { t->aspect, level,     0, layers };
                bl.srcOffsets[0] = { 0, 0, 0 }; bl.srcOffsets[1] = { mw, mh, 1 };
                bl.dstOffsets[0] = { 0, 0, 0 }; bl.dstOffsets[1] = { nw, nh, 1 };
                vkCmdBlitImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &bl, VK_FILTER_LINEAR);
                // This level becomes the next blit's source.
                transition(level, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                mw = nw; mh = nh;
            }
            // Every level is TRANSFER_SRC now; hand the whole image to the shader.
            t->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier(cb, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });
    }
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
        VKShader sh; sh.stage = d.stage; sh.entry = d.entry_point ? d.entry_point : "main"; VK_OK(vkCreateShaderModule(dev, &mi, nullptr, &sh.mod));
        if (!sh.mod) return { -1 };
#ifdef VK_AUTO_BIND
        reflect_set0_bindings(static_cast<const uint32_t*>(d.code), d.code_size, shader_stage(d.stage), sh.bindings);
#endif
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
        std::vector<VkWriteDescriptorSetAccelerationStructureKHR> as_writes(d.write_count);
        std::vector<VkAccelerationStructureKHR> as_handles(d.write_count, VK_NULL_HANDLE);
        ds->storage_textures.clear();
        for (int i = 0; i < d.write_count; ++i) {
            const auto& w = d.writes[i];
            VkWriteDescriptorSet ws{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            ws.dstSet = ds->set; ws.dstBinding = w.binding; ws.descriptorCount = 1; ws.descriptorType = desc_type(w.type);
            if (w.type == BindingType::UniformBuffer || w.type == BindingType::StorageBuffer) {
                auto* b = buffers_.get(w.buffer.id); if (!b) continue;
                bufs[i] = { b->buf, w.buffer_offset, w.buffer_size ? w.buffer_size : VK_WHOLE_SIZE }; ws.pBufferInfo = &bufs[i];
            } else if (w.type == BindingType::AccelerationStructure) {
                // An acceleration structure is not an image or a buffer: its handle is
                // delivered through a chained write struct.
                auto* a = accels_.get(w.accel.id); if (!a || !a->as) continue;
                as_handles[i] = a->as;
                as_writes[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
                as_writes[i].accelerationStructureCount = 1;
                as_writes[i].pAccelerationStructures = &as_handles[i];
                ws.pNext = &as_writes[i];
            } else {
                auto* t = textures_.get(w.texture.id); auto* s = samplers_.get(w.sampler.id);
                imgs[i] = { s ? s->sampler : VK_NULL_HANDLE, t ? t->view : VK_NULL_HANDLE,
                            (w.type == BindingType::StorageTexture) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                ws.pImageInfo = &imgs[i];
                // Remember it so bind_descriptor_set can put the image into GENERAL; the
                // descriptor above promises that layout but cannot perform the transition.
                if (w.type == BindingType::StorageTexture && t) ds->storage_textures.push_back(w.texture.id);
            }
            writes.push_back(ws);
        }
        if (!writes.empty()) vkUpdateDescriptorSets(dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
    void destroy_descriptor_set(DescriptorSetHandle h) override { auto* ds = dsets_.get(h.id); if (ds && ds->pool) vkDestroyDescriptorPool(dev, ds->pool, nullptr); dsets_.release(h.id); }

    // ---- pipelines ----------------------------------------------------------
    // True when the pipeline reads or writes the depth-stencil attachment. Stencil-only
    // pipelines (depth off) count: the render pass and framebuffer must still carry it.
    static bool pipeline_uses_depth_stencil(const PipelineDesc& d) {
        return d.depth_stencil.depth_enable || d.depth_stencil.stencil_enable;
    }
    VkRenderPass make_render_pass(const PipelineDesc& d) {
        VkAttachmentDescription atts[2]{};
        atts[0].format = tex_format(d.color_formats[0]); atts[0].samples = vk_samples(d.samples);
        atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; atts[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference cref{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &cref;
        // Optional depth attachment (loadOp=LOAD: we clear via vkCmdClearDepthStencilImage
        // before the pass, like colour). initial/final layout is the attachment-optimal one.
        VkAttachmentReference dref{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        uint32_t count = 1;
        // A stencil-only pipeline (the GUI's clip masks: depth off, stencil on) needs the
        // attachment too — keying this on depth_enable alone would silently drop the stencil.
        if (pipeline_uses_depth_stencil(d)) {
            atts[1].format = tex_format(d.depth_format); atts[1].samples = vk_samples(d.samples);
            atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; atts[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD; atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            atts[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            sub.pDepthStencilAttachment = &dref; count = 2;
        }
        VkRenderPassCreateInfo ci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ci.attachmentCount = count; ci.pAttachments = atts; ci.subpassCount = 1; ci.pSubpasses = &sub;
        VkRenderPass rp = VK_NULL_HANDLE; VK_OK(vkCreateRenderPass(dev, &ci, nullptr, &rp)); return rp;
    }
    // Build an auto descriptor-set layout (set 0) + pipeline layout from the stages' reflected
    // bindings, so a pipeline created without an explicit layout still supports slot binds.
    void build_auto_layout(std::initializer_list<ShaderHandle> shs, VKPipeline& p) {
        std::map<uint32_t, VkDescriptorSetLayoutBinding> merged;
        for (ShaderHandle h : shs) { auto* s = shaders_.get(h.id); if (!s) continue;
            for (const auto& b : s->bindings) {
                auto it = merged.find(b.binding);
                if (it == merged.end()) merged[b.binding] = b; else it->second.stageFlags |= b.stageFlags;
            }
        }
        if (merged.empty()) {   // no resources: a plain empty pipeline layout (as before)
            VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            vkCreatePipelineLayout(dev, &lci, nullptr, &p.layout); p.owns_layout = true; return;
        }
        for (auto& kv : merged) p.auto_bindings.push_back(kv.second);
        VkDescriptorSetLayoutCreateInfo dci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        dci.bindingCount = (uint32_t)p.auto_bindings.size(); dci.pBindings = p.auto_bindings.data();
        vkCreateDescriptorSetLayout(dev, &dci, nullptr, &p.auto_dsl);
        VkDescriptorSetLayout sets[1] = { p.auto_dsl };
        VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        lci.setLayoutCount = 1; lci.pSetLayouts = sets;
        vkCreatePipelineLayout(dev, &lci, nullptr, &p.layout); p.owns_layout = true;
    }

    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        VKPipeline p;
        auto* pl = plls_.get(d.layout.id);
        if (pl) p.layout = pl->layout;   // else: auto layout from reflection (built below)
        VkPipelineCache cache = VK_NULL_HANDLE; if (auto* c = pcaches_.get(d.cache.id)) cache = c->cache;

        if (d.compute_shader.valid()) {
            p.bind = VK_PIPELINE_BIND_POINT_COMPUTE;
            auto* cs = shaders_.get(d.compute_shader.id); if (!cs) return { -1 };
            if (!p.layout) build_auto_layout({ d.compute_shader }, p);
            VkComputePipelineCreateInfo ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            ci.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }; ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            ci.stage.module = cs->mod; ci.stage.pName = cs->entry.c_str(); ci.layout = p.layout;
            VK_OK(vkCreateComputePipelines(dev, cache, 1, &ci, nullptr, &p.pipeline));
            return p.pipeline ? PipelineHandle{ pipelines_.alloc(p) } : PipelineHandle{ -1 };
        }
        // A mesh pipeline is a graphics pipeline whose geometry comes from a mesh shader
        // instead of the input assembler; only the stage set and the absence of vertex input
        // differ, so it shares everything below.
        const bool mesh = d.mesh_shader.valid();
        if (mesh && !mesh_shaders_enabled_) { vk_unsupported("mesh-shader pipelines (VK_EXT_mesh_shader not enabled)"); return { -1 }; }

        std::vector<VkPipelineShaderStageCreateInfo> stages;
        auto add = [&](ShaderHandle h) { if (auto* s = shaders_.get(h.id)) { VkPipelineShaderStageCreateInfo si{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO }; si.stage = shader_stage(s->stage); si.module = s->mod; si.pName = s->entry.c_str(); stages.push_back(si); } };
        if (mesh) {
            if (d.task_shader.valid()) add(d.task_shader);
            add(d.mesh_shader); add(d.fragment_shader);
            if (!p.layout) build_auto_layout({ d.task_shader, d.mesh_shader, d.fragment_shader }, p);
        } else {
            add(d.vertex_shader); add(d.fragment_shader);
            if (d.geometry_shader.valid()) add(d.geometry_shader);
            if (d.tess_control_shader.valid()) add(d.tess_control_shader);
            if (d.tess_eval_shader.valid()) add(d.tess_eval_shader);
            if (!p.layout) build_auto_layout({ d.vertex_shader, d.fragment_shader, d.geometry_shader, d.tess_control_shader, d.tess_eval_shader }, p);
        }
        p.mesh = mesh;

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
        VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO }; ms.rasterizationSamples = vk_samples(d.samples);
        // Honour the requested BlendState (factors / ops / write mask), not a hardcode.
        VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = d.blend.write_mask & 0xF; cba.blendEnable = d.blend.enabled ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = vk_blend_factor(d.blend.src_color); cba.dstColorBlendFactor = vk_blend_factor(d.blend.dst_color); cba.colorBlendOp = vk_blend_op(d.blend.color_op);
        cba.srcAlphaBlendFactor = vk_blend_factor(d.blend.src_alpha); cba.dstAlphaBlendFactor = vk_blend_factor(d.blend.dst_alpha); cba.alphaBlendOp = vk_blend_op(d.blend.alpha_op);
        VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO }; cb.attachmentCount = 1; cb.pAttachments = &cba;
        VkPipelineDepthStencilStateCreateInfo dss{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        dss.depthTestEnable = d.depth_stencil.depth_enable; dss.depthWriteEnable = d.depth_stencil.depth_write; dss.depthCompareOp = vk_compare(d.depth_stencil.depth_func);
        // Stencil (used by the GUI's clip masks). compareMask/writeMask are static here; the
        // reference is dynamic (vkCmdSetStencilReference via set_stencil_reference()).
        dss.stencilTestEnable = d.depth_stencil.stencil_enable ? VK_TRUE : VK_FALSE;
        auto face = [&](const StencilOpDesc& s) {
            VkStencilOpState o{};
            o.failOp      = vk_stencil_op(s.stencil_fail);
            o.depthFailOp = vk_stencil_op(s.depth_fail);
            o.passOp      = vk_stencil_op(s.pass);
            o.compareOp   = vk_compare(s.func);
            o.compareMask = d.depth_stencil.stencil_read_mask;
            o.writeMask   = d.depth_stencil.stencil_write_mask;
            o.reference   = 0;   // dynamic
            return o;
        };
        dss.front = face(d.depth_stencil.front_face);
        dss.back  = face(d.depth_stencil.back_face);
        VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
        VkPipelineDynamicStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO }; ds.dynamicStateCount = 3; ds.pDynamicStates = dyn;
        p.render_pass = make_render_pass(d);
        p.has_depth = pipeline_uses_depth_stencil(d);
        VkGraphicsPipelineCreateInfo ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        ci.stageCount = (uint32_t)stages.size(); ci.pStages = stages.data();
        // Mesh pipelines must NOT supply vertex-input or input-assembly state: there is no
        // input assembler to configure, and providing them is invalid.
        ci.pVertexInputState = mesh ? nullptr : &vi; ci.pInputAssemblyState = mesh ? nullptr : &ia;
        ci.pViewportState = &vp; ci.pRasterizationState = &rs;
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
        if (p->auto_dsl) vkDestroyDescriptorSetLayout(dev, p->auto_dsl, nullptr);
        pipelines_.release(h.id);
    }

    // ---- render targets -----------------------------------------------------
    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples;
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

    // Zero-copy interop: wrap an existing VkImage (e.g. from a GStreamer vulkan decoder
    // sharing this VkDevice) as a sampled RHI texture. We create our own VkImageView but
    // never destroy the image/memory unless take_ownership (owns=false). The caller tells
    // us the image's current layout so binding samples it correctly.
    TextureHandle import_texture(const NativeTextureDesc& d) override {
        VkImage img = (VkImage)d.vk_image;
        if (img == VK_NULL_HANDLE) { vk_unsupported("import_texture (null vk_image)"); return { -1 }; }
        VKTexture t;
        t.image = img; t.mem = VK_NULL_HANDLE; t.owns = d.take_ownership;
        t.tf = d.format;
        t.format = d.vk_format ? (VkFormat)d.vk_format : tex_format(d.format);
        t.w = d.width; t.h = d.height; t.layers = 1; t.levels = 1;
        t.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        t.layout = d.vk_layout ? (VkImageLayout)d.vk_layout : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        t.view = make_view(t, t.format, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0, 1);
        if (d.debug_name) set_name(VK_OBJECT_TYPE_IMAGE, (uint64_t)t.image, d.debug_name);
        return { textures_.alloc(t) };
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
        const VkDeviceSize bytes = texture_format_image_size(t->tf, r.width, r.height);
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
    VkDeviceAddress buffer_address(BufferHandle h) {
        auto* b = buffers_.get(h.id);
        if (!b || !p_get_buf_addr) return 0;
        VkBufferDeviceAddressInfo ai{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO }; ai.buffer = b->buf;
        return p_get_buf_addr(dev, &ai);
    }
    // Translate an AccelStructDesc into Vulkan build geometry. Shared by create (which sizes
    // the buffers) and build (which records it), so the two cannot disagree.
    bool accel_geometry(const AccelStructDesc& d, VkAccelerationStructureGeometryKHR* geom,
                        VkAccelerationStructureBuildGeometryInfoKHR* info, uint32_t* prim_count) {
        *geom = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
        *info = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
        VkBuildAccelerationStructureFlagsKHR f = 0;
        if (d.flags & ACCEL_ALLOW_UPDATE)      f |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        if (d.flags & ACCEL_ALLOW_COMPACTION)  f |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        if (d.flags & ACCEL_PREFER_FAST_TRACE) f |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        if (d.flags & ACCEL_PREFER_FAST_BUILD) f |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        if (d.flags & ACCEL_LOW_MEMORY)        f |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
        info->flags = f;
        info->mode = d.update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                              : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        if (d.type == AccelStructType::TopLevel) {
            geom->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            geom->geometry.instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
            geom->geometry.instances.arrayOfPointers = VK_FALSE;
            geom->geometry.instances.data.deviceAddress = buffer_address(d.instance_buffer);
            info->type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
            *prim_count = d.instance_count;
        } else {
            const VkDeviceAddress va = buffer_address(d.vertex_buffer);
            if (!va) return false;
            geom->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            geom->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
            auto& tri = geom->geometry.triangles;
            tri = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
            tri.vertexFormat = (d.vertex_format == VertexFormat::Float2) ? VK_FORMAT_R32G32_SFLOAT
                                                                        : VK_FORMAT_R32G32B32_SFLOAT;
            tri.vertexData.deviceAddress = va;
            tri.vertexStride = d.vertex_stride ? d.vertex_stride : 12;
            tri.maxVertex = d.vertex_count ? d.vertex_count - 1 : 0;
            tri.indexType = VK_INDEX_TYPE_NONE_KHR;
            if (d.index_buffer.valid()) {
                tri.indexType = (d.index_format == IndexFormat::UInt16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                tri.indexData.deviceAddress = buffer_address(d.index_buffer);
            }
            if (d.transform_buffer.valid()) tri.transformData.deviceAddress = buffer_address(d.transform_buffer);
            info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            *prim_count = d.index_buffer.valid() ? d.index_count / 3 : d.vertex_count / 3;
        }
        info->geometryCount = 1; info->pGeometries = geom;
        return true;
    }
    // Allocate a device-local buffer usable as acceleration-structure storage or scratch.
    bool alloc_as_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* out_buf, VkDeviceMemory* out_mem) {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = size ? size : 4; bi.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &bi, nullptr, out_buf) != VK_SUCCESS) return false;
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, *out_buf, &req);
        VkMemoryAllocateFlagsInfo maf{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        maf.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.pNext = &maf;
        mi.allocationSize = req.size;
        mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &mi, nullptr, out_mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(dev, *out_buf, *out_mem, 0);
        return true;
    }
    AccelStructHandle create_acceleration_structure(const AccelStructDesc& d) override {
        if (!ray_tracing_enabled_ || !p_get_as_build_sizes || !p_create_as) {
            vk_unsupported("acceleration structures (VK_KHR ray tracing not enabled)");
            return { -1 };   // an invalid handle: a valid one here would look like success
        }
        VKAccelStruct a;
        VkAccelerationStructureGeometryKHR geom{};
        VkAccelerationStructureBuildGeometryInfoKHR info{};
        uint32_t prims = 0;
        if (!accel_geometry(d, &geom, &info, &prims)) { vk_unsupported("acceleration structures (invalid geometry)"); return { -1 }; }
        VkAccelerationStructureBuildSizesInfoKHR sizes{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        p_get_as_build_sizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &prims, &sizes);
        if (sizes.accelerationStructureSize == 0) { vk_unsupported("acceleration structures (build sizes returned 0)"); return { -1 }; }
        a.result_size = sizes.accelerationStructureSize;
        a.scratch_size = (std::max)(sizes.buildScratchSize, sizes.updateScratchSize);
        if (!alloc_as_buffer(a.result_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, &a.result, &a.result_mem) ||
            !alloc_as_buffer(a.scratch_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &a.scratch, &a.scratch_mem)) {
            vk_unsupported("acceleration structures (buffer allocation failed)"); return { -1 };
        }
        VkAccelerationStructureCreateInfoKHR ci{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        ci.buffer = a.result; ci.size = a.result_size; ci.type = info.type;
        if (p_create_as(dev, &ci, nullptr, &a.as) != VK_SUCCESS || !a.as) {
            vk_unsupported("acceleration structures (vkCreateAccelerationStructureKHR failed)"); return { -1 };
        }
        if (p_get_as_addr) {
            VkAccelerationStructureDeviceAddressInfoKHR ai{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
            ai.accelerationStructure = a.as;
            a.address = p_get_as_addr(dev, &ai);
        }
        return { accels_.alloc(a) };
    }
    uint64_t acceleration_structure_address(AccelStructHandle h) override {
        auto* a = accels_.get(h.id); return a ? a->address : 0;
    }

    // ---- ray-tracing pipeline + shader binding table ------------------------
    RayTracingPipelineHandle create_ray_tracing_pipeline(const RayTracingPipelineDesc& d) override {
        if (!ray_tracing_enabled_ || !p_create_rt_pipes || !p_get_group_handles) {
            vk_unsupported("ray-tracing pipeline (VK_KHR_ray_tracing_pipeline not enabled)"); return { -1 };
        }
        auto* lib = shaders_.get(d.library.id);
        if (!lib || !lib->mod || !d.ray_gen.entry_point) { vk_unsupported("ray-tracing pipeline (no library / ray-gen)"); return { -1 }; }

        VKRayPipeline rp;
        // Vulkan takes the library's exports as ordinary pipeline stages selected by entry
        // point name, then GROUPS them: general groups for ray-gen and miss, triangle-hit
        // groups tying a closest-hit (and optional any-hit) together.
        std::vector<VkPipelineShaderStageCreateInfo> stages;
        std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
        auto add_stage = [&](VkShaderStageFlagBits st, const char* entry) -> uint32_t {
            VkPipelineShaderStageCreateInfo si{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            si.stage = st; si.module = lib->mod; si.pName = entry;
            stages.push_back(si);
            return uint32_t(stages.size() - 1);
        };
        auto general_group = [&](uint32_t stage_index) {
            VkRayTracingShaderGroupCreateInfoKHR g{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
            g.generalShader = stage_index;
            g.closestHitShader = g.anyHitShader = g.intersectionShader = VK_SHADER_UNUSED_KHR;
            groups.push_back(g);
        };
        general_group(add_stage(VK_SHADER_STAGE_RAYGEN_BIT_KHR, d.ray_gen.entry_point));
        for (int i = 0; i < d.miss_count; ++i)
            general_group(add_stage(VK_SHADER_STAGE_MISS_BIT_KHR, d.miss[i].entry_point));
        for (int i = 0; i < d.hit_group_count; ++i) {
            const RayHitGroup& hg = d.hit_groups[i];
            VkRayTracingShaderGroupCreateInfoKHR g{ VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR };
            g.type = hg.intersection ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
                                     : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
            g.generalShader = VK_SHADER_UNUSED_KHR;
            g.closestHitShader   = hg.closest_hit  ? add_stage(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, hg.closest_hit)   : VK_SHADER_UNUSED_KHR;
            g.anyHitShader       = hg.any_hit      ? add_stage(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, hg.any_hit)           : VK_SHADER_UNUSED_KHR;
            g.intersectionShader = hg.intersection ? add_stage(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, hg.intersection) : VK_SHADER_UNUSED_KHR;
            groups.push_back(g);
        }

        auto* pl = plls_.get(d.layout.id);
        rp.layout = pl ? pl->layout : VK_NULL_HANDLE;
        if (!rp.layout) {   // no explicit layout: an empty one still needs to exist
            VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            vkCreatePipelineLayout(dev, &lci, nullptr, &rp.layout);
            rp.owns_layout = true;
        }
        // (VKPipelineLayout holds only the layout handle; set count is not tracked here.)

        VkRayTracingPipelineCreateInfoKHR ci{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
        ci.stageCount = (uint32_t)stages.size(); ci.pStages = stages.data();
        ci.groupCount = (uint32_t)groups.size(); ci.pGroups = groups.data();
        ci.maxPipelineRayRecursionDepth = d.max_recursion ? d.max_recursion : 1;
        ci.layout = rp.layout;
        if (p_create_rt_pipes(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ci, nullptr, &rp.pipeline) != VK_SUCCESS || !rp.pipeline) {
            vk_unsupported("ray-tracing pipeline (vkCreateRayTracingPipelinesKHR failed)"); return { -1 };
        }

        // Shader binding table. Each record is a group handle plus that record's own
        // arguments; both the per-record stride and each region's base have their own
        // alignment requirement, taken from the device rather than assumed.
        const uint32_t hsize  = rt_props_.shaderGroupHandleSize;
        const uint32_t halign = rt_props_.shaderGroupHandleAlignment ? rt_props_.shaderGroupHandleAlignment : 1;
        const uint32_t balign = rt_props_.shaderGroupBaseAlignment ? rt_props_.shaderGroupBaseAlignment : 1;
        auto align_up = [](uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); };
        auto record_size = [&](const RayLocalArg* args, int n) {
            return (uint32_t)align_up(hsize + local_arg_bytes(args, n), halign);
        };
        const uint32_t rg_rec = record_size(d.ray_gen.args, d.ray_gen.arg_count);
        uint32_t miss_rec = hsize, hit_rec = hsize;
        for (int i = 0; i < d.miss_count; ++i)      miss_rec = (std::max)(miss_rec, record_size(d.miss[i].args, d.miss[i].arg_count));
        for (int i = 0; i < d.hit_group_count; ++i) hit_rec  = (std::max)(hit_rec,  record_size(d.hit_groups[i].args, d.hit_groups[i].arg_count));
        miss_rec = (uint32_t)align_up(miss_rec, halign); hit_rec = (uint32_t)align_up(hit_rec, halign);

        const uint64_t rg_off   = 0;
        const uint64_t miss_off = align_up(rg_off + align_up(rg_rec, balign), balign);
        const uint64_t hit_off  = align_up(miss_off + uint64_t(miss_rec) * d.miss_count, balign);
        const uint64_t total    = align_up(hit_off + uint64_t(hit_rec) * d.hit_group_count, balign);

        std::vector<uint8_t> handles(size_t(hsize) * groups.size());
        if (p_get_group_handles(dev, rp.pipeline, 0, (uint32_t)groups.size(), handles.size(), handles.data()) != VK_SUCCESS) {
            vk_unsupported("ray-tracing pipeline (shader group handles)"); return { -1 };
        }
        if (!alloc_sbt_buffer(total ? total : balign, &rp.sbt, &rp.sbt_mem)) {
            vk_unsupported("ray-tracing pipeline (shader table allocation)"); return { -1 };
        }
        uint8_t* map = nullptr;
        vkMapMemory(dev, rp.sbt_mem, 0, VK_WHOLE_SIZE, 0, (void**)&map);
        std::memset(map, 0, size_t(total ? total : balign));
        auto write_record = [&](uint8_t* dst, uint32_t group, const RayLocalArg* args, int n) {
            std::memcpy(dst, handles.data() + size_t(hsize) * group, hsize);
            encode_local_args(dst + hsize, args, n);
        };
        uint32_t g = 0;
        write_record(map + rg_off, g++, d.ray_gen.args, d.ray_gen.arg_count);
        for (int i = 0; i < d.miss_count; ++i)
            write_record(map + miss_off + uint64_t(miss_rec) * i, g++, d.miss[i].args, d.miss[i].arg_count);
        for (int i = 0; i < d.hit_group_count; ++i)
            write_record(map + hit_off + uint64_t(hit_rec) * i, g++, d.hit_groups[i].args, d.hit_groups[i].arg_count);
        vkUnmapMemory(dev, rp.sbt_mem);

        const VkDeviceAddress base = buffer_device_address(rp.sbt);
        rp.raygen = { base + rg_off, rg_rec, rg_rec };   // ray-gen: size must equal stride
        rp.miss   = { base + miss_off, miss_rec, uint64_t(miss_rec) * d.miss_count };
        rp.hit    = { base + hit_off,  hit_rec,  uint64_t(hit_rec) * d.hit_group_count };
        return { rt_pipes_.alloc(rp) };
    }
    void destroy_ray_tracing_pipeline(RayTracingPipelineHandle h) override {
        if (auto* p = rt_pipes_.get(h.id)) {
            if (p->pipeline) vkDestroyPipeline(dev, p->pipeline, nullptr);
            if (p->owns_layout && p->layout) vkDestroyPipelineLayout(dev, p->layout, nullptr);
            if (p->sbt) vkDestroyBuffer(dev, p->sbt, nullptr);
            if (p->sbt_mem) vkFreeMemory(dev, p->sbt_mem, nullptr);
        }
        rt_pipes_.release(h.id);
    }
    VKRayPipeline* rt_pipeline(int id) { return rt_pipes_.get(id); }

    // Bytes a record's local arguments occupy. Vulkan reads them as a shader-record buffer,
    // so constants are laid out as-is and a buffer argument becomes its device address.
    uint32_t local_arg_bytes(const RayLocalArg* args, int count) { return encode_local_args(nullptr, args, count); }
    uint32_t encode_local_args(uint8_t* dst, const RayLocalArg* args, int count) {
        uint32_t off = 0;
        for (int i = 0; i < count; ++i) {
            const RayLocalArg& a = args[i];
            switch (a.kind) {
                case RayArgKind::Constants: {
                    const uint32_t n = (a.constants_size + 3u) & ~3u;
                    if (dst && a.constants) std::memcpy(dst + off, a.constants, a.constants_size);
                    off += n; break;
                }
                case RayArgKind::BufferAddress: {
                    VkDeviceAddress va = 0;
                    if (auto* b = buffers_.get(a.buffer.id)) va = buffer_device_address(b->buf) + a.buffer_offset;
                    if (dst) std::memcpy(dst + off, &va, sizeof va);
                    off += sizeof va; break;
                }
                case RayArgKind::DescriptorTable:
                    // Vulkan has no descriptor-table-in-a-record concept; a shader record
                    // carries plain data. Bind such resources through the global layout.
                    if (dst) vk_unsupported("ray-tracing local arg (DescriptorTable; use Constants/BufferAddress)");
                    break;
            }
        }
        return off;
    }
    bool alloc_sbt_buffer(VkDeviceSize size, VkBuffer* out_buf, VkDeviceMemory* out_mem) {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = size; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bi.usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (vkCreateBuffer(dev, &bi, nullptr, out_buf) != VK_SUCCESS) return false;
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, *out_buf, &req);
        VkMemoryAllocateFlagsInfo maf{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
        maf.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo mi{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO }; mi.pNext = &maf;
        mi.allocationSize = req.size;
        mi.memoryTypeIndex = find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(dev, &mi, nullptr, out_mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(dev, *out_buf, *out_mem, 0);
        return true;
    }
    void destroy_acceleration_structure(AccelStructHandle h) override {
        if (auto* a = accels_.get(h.id)) {
            if (a->as && p_destroy_as) p_destroy_as(dev, a->as, nullptr);
            if (a->result)      vkDestroyBuffer(dev, a->result, nullptr);
            if (a->result_mem)  vkFreeMemory(dev, a->result_mem, nullptr);
            if (a->scratch)     vkDestroyBuffer(dev, a->scratch, nullptr);
            if (a->scratch_mem) vkFreeMemory(dev, a->scratch_mem, nullptr);
        }
        accels_.release(h.id);
    }

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
    VKAccelStruct* accel(int id) { return accels_.get(id); }
    // Device address of a raw VkBuffer (acceleration-structure scratch is not a BufferHandle).
    VkDeviceAddress buffer_device_address(VkBuffer b) {
        if (!b || !p_get_buf_addr) return 0;
        VkBufferDeviceAddressInfo ai{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO }; ai.buffer = b;
        return p_get_buf_addr(dev, &ai);
    }
    VKTexture* texture(int id) { return textures_.get(id); }
    VKSampler* sampler(int id) { return samplers_.get(id); }
    VKPipeline* pipeline(int id) { return pipelines_.get(id); }
    VKRenderTarget* rt(int id) { return rts_.get(id); }
    VKQuery* query(int id) { return queries_.get(id); }
    VKDescriptorSet* descriptor_set(int id) { return dsets_.get(id); }
    VKFence* fence(int id) { return fences_.get(id); }
    VKTimeline* timeline(int id) { return timelines_.get(id); }

    Pool<VKBuffer> buffers_; Pool<VKTexture> textures_; Pool<VKSampler> samplers_; Pool<VKShader> shaders_;
    Pool<VKPipeline> pipelines_; Pool<VKRenderTarget> rts_; Pool<VKFence> fences_; Pool<VKSem> sems_;
    Pool<VKTimeline> timelines_; Pool<VKQuery> queries_; Pool<VKDescSetLayout> dsls_; Pool<VKPipelineLayout> plls_;
    Pool<VKDescriptorSet> dsets_; Pool<VKPipelineCache> pcaches_; Pool<VKAccelStruct> accels_; Pool<VKRayPipeline> rt_pipes_;
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
        // Per-frame pool for the auto descriptor sets that back slot binds (reset each begin()).
        VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 512 },  { VK_DESCRIPTOR_TYPE_SAMPLER, 512 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
        };
        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.maxSets = 1024; pci.poolSizeCount = 6; pci.pPoolSizes = sizes;
        vkCreateDescriptorPool(d->dev, &pci, nullptr, &desc_pool_);
    }
    ~VKCommander() override { if (desc_pool_) vkDestroyDescriptorPool(dev_->dev, desc_pool_, nullptr); vkFreeCommandBuffers(dev_->dev, dev_->pool, 1, &cb_); }
    VKDevice* device() const { return dev_; }
    VkCommandBuffer cmd() const { return cb_; }

    void begin() override {
        vkResetCommandBuffer(cb_, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO }; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb_, &bi); in_pass_ = false;
        if (desc_pool_) vkResetDescriptorPool(dev_->dev, desc_pool_, 0);
        for (int i = 0; i < MAX_BIND; ++i) { ubo_[i] = {}; ssbo_[i] = {}; tex_[i] = VK_NULL_HANDLE; samp_[i] = VK_NULL_HANDLE; simg_[i] = VK_NULL_HANDLE; }
        desc_dirty_ = false;
    }
    void end() override {
        end_pass();
        // Hand the acquired swapchain image to the presentation engine.
        if (dev_->have_acquired_) if (auto* t = dev_->texture(color_tex_)) VKDevice::barrier(cb_, t, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        vkEndCommandBuffer(cb_);
    }

    // Acquire the next swapchain image and bind it as the colour target (windowed present).
    // `depth_stencil` (optional) is attached alongside — a swapchain image is colour-only, so
    // depth testing and stencil clipping against the backbuffer need one supplied here.
    void set_render_target_backbuffer(RenderTargetHandle depth_stencil) override {
        end_pass();
        color_tex_ = dev_->acquire_backbuffer();
        depth_tex_ = -1;
        if (depth_stencil.valid()) if (auto* rt = dev_->rt(depth_stencil.id)) depth_tex_ = rt->depth_tex;
    }
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) override {
        end_pass(); color_tex_ = -1; depth_tex_ = -1;
        if (count > 0 && colors) if (auto* rt = dev_->rt(colors[0].id)) color_tex_ = rt->color_tex;
        if (depth.valid()) if (auto* rt = dev_->rt(depth.id)) depth_tex_ = rt->depth_tex;
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
    void clear_depth_stencil(const ClearDepthStencil& ds) override {
        end_pass();
        auto* depth = dev_->texture(depth_tex_); if (!depth) { vk_unsupported("clear_depth_stencil (no depth target bound)"); return; }
        VKDevice::barrier(cb_, depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkClearDepthStencilValue dsv{ ds.depth, ds.stencil };
        VkImageSubresourceRange rng{ depth->aspect, 0, (uint32_t)depth->levels, 0, (uint32_t)depth->layers };
        vkCmdClearDepthStencilImage(cb_, depth->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &dsv, 1, &rng);
    }
    void set_pipeline(PipelineHandle h) override { auto* p = dev_->pipeline(h.id); if (!p) return; cur_pipeline_ = p; rt_ = nullptr; vkCmdBindPipeline(cb_, p->bind, p->pipeline); }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; VkDeviceSize off = offset; vkCmdBindVertexBuffers(cb_, slot, 1, &b->buf, &off); }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (!b) return; vkCmdBindIndexBuffer(cb_, b->buf, offset, fmt == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32); }
    // Slot binds record into the pending tables; flush_descriptors() writes + binds an auto
    // descriptor set per draw (so these work like GL/D3D11 on pipelines with a reflected layout).
    void bind_texture(uint32_t slot, TextureHandle h) override { if (slot >= MAX_BIND) return; auto* t = dev_->texture(h.id); if (!t) return; tex_[slot] = t->view; desc_dirty_ = true; }
    void bind_sampler(uint32_t slot, SamplerHandle h) override { if (slot >= MAX_BIND) return; auto* s = dev_->sampler(h.id); if (!s) return; samp_[slot] = s->sampler; desc_dirty_ = true; }
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t size) override { if (slot >= MAX_BIND) return; auto* b = dev_->buffer(h.id); if (!b) return; ubo_[slot] = { b->buf, offset, size ? size : VK_WHOLE_SIZE }; desc_dirty_ = true; }
    void push_constants(uint32_t offset, const void* data, uint32_t size) override { if (cur_pipeline_ && cur_pipeline_->layout) vkCmdPushConstants(cb_, cur_pipeline_->layout, VK_SHADER_STAGE_ALL, offset, size, data); }
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t size) override { if (slot >= MAX_BIND) return; auto* b = dev_->buffer(h.id); if (!b) return; ssbo_[slot] = { b->buf, offset, size ? size : VK_WHOLE_SIZE }; desc_dirty_ = true; }
    // Storage image (RWTexture2D / image2D). Kept in its own slot array rather than tex_[]:
    // a sampled image and a storage image can share a binding number in different sets, and
    // they need different descriptor types and layouts. The image is transitioned to GENERAL
    // here — a storage image cannot be read/written in SHADER_READ_ONLY layout.
    void bind_storage_texture(uint32_t slot, TextureHandle h, int mip, StorageAccess access) override {
        (void)mip; (void)access;   // the shader's declaration carries the access; mip views are per-texture
        if (slot >= MAX_BIND) return;
        auto* t = dev_->texture(h.id); if (!t) return;
        VKDevice::barrier(cb_, t, VK_IMAGE_LAYOUT_GENERAL);
        simg_[slot] = t->view;
        desc_dirty_ = true;
    }
    void bind_descriptor_set(uint32_t set_index, DescriptorSetHandle h, const uint32_t* dyn, int dyn_count) override {
        auto* ds = dev_->descriptor_set(h.id); if (!ds) return;
        // The set's descriptors promise GENERAL for any storage image; perform that
        // transition now, since writing the set had no command buffer to do it with.
        for (int tid : ds->storage_textures)
            if (auto* t = dev_->texture(tid)) VKDevice::barrier(cb_, t, VK_IMAGE_LAYOUT_GENERAL);
        // A ray-tracing pipeline has its own bind point and layout; descriptor sets bound to
        // the graphics/compute point are not visible to a trace.
        if (rt_) {
            vkCmdBindDescriptorSets(cb_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_->layout,
                                    set_index, 1, &ds->set, (uint32_t)dyn_count, dyn);
            return;
        }
        if (!cur_pipeline_) return;
        vkCmdBindDescriptorSets(cb_, cur_pipeline_->bind, cur_pipeline_->layout, set_index, 1, &ds->set, (uint32_t)dyn_count, dyn);
    }
    void draw(uint32_t vc, uint32_t first, uint32_t inst, uint32_t first_inst) override { ensure_pass(); flush_descriptors(); vkCmdDraw(cb_, vc, inst ? inst : 1, first, first_inst); }
    void draw_indexed(uint32_t ic, uint32_t first, int32_t base_v, uint32_t inst, uint32_t first_inst) override { ensure_pass(); flush_descriptors(); vkCmdDrawIndexed(cb_, ic, inst ? inst : 1, first, base_v, first_inst); }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { flush_descriptors(); vkCmdDispatch(cb_, x, y, z); }
    void draw_indirect(BufferHandle a, uint32_t off, uint32_t count, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); if (b) vkCmdDrawIndirect(cb_, b->buf, off, count, stride ? stride : sizeof(VkDrawIndirectCommand)); }
    void draw_indexed_indirect(BufferHandle a, uint32_t off, uint32_t count, uint32_t stride) override { ensure_pass(); auto* b = dev_->buffer(a.id); if (b) vkCmdDrawIndexedIndirect(cb_, b->buf, off, count, stride ? stride : sizeof(VkDrawIndexedIndirectCommand)); }
    void dispatch_indirect(BufferHandle a, uint32_t off) override { auto* b = dev_->buffer(a.id); if (b) vkCmdDispatchIndirect(cb_, b->buf, off); }
    void draw_mesh_tasks(uint32_t x, uint32_t y, uint32_t z) override {
        if (!dev_->p_draw_mesh) { vk_unsupported("draw_mesh_tasks (VK_EXT_mesh_shader not enabled)"); return; }
        ensure_pass(); flush_descriptors();
        dev_->p_draw_mesh(cb_, x ? x : 1, y ? y : 1, z ? z : 1);
    }
    void draw_mesh_tasks_indirect(BufferHandle a, uint32_t off, uint32_t count, uint32_t stride) override {
        auto p = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(dev_->dev, "vkCmdDrawMeshTasksIndirectEXT");
        if (!p) { vk_unsupported("draw_mesh_tasks_indirect (VK_EXT_mesh_shader not enabled)"); return; }
        ensure_pass(); flush_descriptors();
        auto* b = dev_->buffer(a.id); if (!b) return;
        p(cb_, b->buf, off, count, stride ? stride : sizeof(VkDrawMeshTasksIndirectCommandEXT));
    }
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
        end_pass();   // a transfer (blit) cannot be recorded inside a render pass
        auto* srt = dev_->rt(sh.id); auto* drt = dev_->rt(dh.id); if (!srt || !drt) return;
        auto* s = dev_->texture(srt->color_tex); auto* d = dev_->texture(drt->color_tex); if (!s || !d) return;
        VKDevice::barrier(cb_, s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL); VKDevice::barrier(cb_, d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit bl{}; bl.srcSubresource = { s->aspect, 0, 0, 1 }; bl.dstSubresource = { d->aspect, 0, 0, 1 };
        bl.srcOffsets[0] = { sx0, sy0, 0 }; bl.srcOffsets[1] = { sx1, sy1, 1 };
        bl.dstOffsets[0] = { dx0, dy0, 0 }; bl.dstOffsets[1] = { dx1, dy1, 1 };
        vkCmdBlitImage(cb_, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
    }
    void resolve_render_target(RenderTargetHandle dh, RenderTargetHandle sh) override {
        end_pass();   // a resolve (transfer) cannot be recorded inside a render pass
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
    void build_acceleration_structure(AccelStructHandle h, const AccelStructDesc& d) override {
        auto* a = dev_->accel(h.id);
        if (!a || !a->as || !dev_->p_cmd_build_as) { vk_unsupported("build_acceleration_structure (ray tracing not enabled)"); return; }
        // A build is not a render-pass operation; an open pass must be closed first.
        end_pass();
        VkAccelerationStructureGeometryKHR geom{};
        VkAccelerationStructureBuildGeometryInfoKHR info{};
        uint32_t prims = 0;
        if (!dev_->accel_geometry(d, &geom, &info, &prims)) return;
        info.dstAccelerationStructure = a->as;
        if (d.update) info.srcAccelerationStructure = a->as;   // refit in place
        info.scratchData.deviceAddress = dev_->buffer_device_address(a->scratch);
        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = prims;
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
        dev_->p_cmd_build_as(cb_, 1, &info, &ranges);
        // Anything reading the structure — a trace, or a TLAS build over this BLAS — must see
        // the build's writes.
        VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb_, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
    }
    // Ray tracing has its own bind point, so binding an RT pipeline does not disturb the
    // graphics/compute one — but descriptor sets must then be bound to that bind point,
    // which is why the bound pipeline is remembered here.
    void set_ray_tracing_pipeline(RayTracingPipelineHandle h) override {
        auto* p = dev_->rt_pipeline(h.id);
        if (!p || !p->pipeline) { vk_unsupported("set_ray_tracing_pipeline (invalid pipeline)"); return; }
        end_pass();   // tracing is not a render-pass operation
        vkCmdBindPipeline(cb_, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p->pipeline);
        rt_ = p;
        cur_pipeline_ = nullptr;
    }
    void trace_rays(uint32_t width, uint32_t height, uint32_t depth) override {
        if (!rt_ || !dev_->p_cmd_trace) { vk_unsupported("trace_rays (no ray-tracing pipeline bound)"); return; }
        VkStridedDeviceAddressRegionKHR callable{};
        dev_->p_cmd_trace(cb_, &rt_->raygen, &rt_->miss, &rt_->hit, &callable,
                          width, height ? height : 1, depth ? depth : 1);
    }

private:
    void ensure_pass() {
        if (!cur_pipeline_ || cur_pipeline_->bind != VK_PIPELINE_BIND_POINT_GRAPHICS) return;
        // A pipeline may only be used inside a render pass COMPATIBLE with the one it was
        // built against, and attachment count is part of compatibility. So an open pass
        // whose attachments don't match the bound pipeline's must be restarted, not reused
        // — otherwise switching between a depth/stencil pipeline and a colour-only one
        // mid-frame (which the GUI does whenever clipping starts) binds into the wrong pass.
        // Pipeline, descriptor and dynamic state all survive a render-pass boundary.
        const bool want_depth = cur_pipeline_->has_depth && dev_->texture(depth_tex_) != nullptr;
        if (in_pass_) {
            if (want_depth == pass_has_depth_) return;
            end_pass();
        }
        auto* color = dev_->texture(color_tex_); if (!color) return;   // look up fresh: the Pool may have reallocated
        // Begin a transient framebuffer + render pass matching the bound target. When the
        // pipeline has a depth attachment, include the bound depth target as attachment 1
        // (its render pass was built with the matching depth attachment).
        VKDevice::barrier(cb_, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkImageView views[2] = { color->view, VK_NULL_HANDLE };
        uint32_t att = 1;
        if (want_depth) {
            if (auto* depth = dev_->texture(depth_tex_)) {
                VKDevice::barrier(cb_, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                views[1] = depth->view; att = 2;
            }
        }
        pass_has_depth_ = want_depth;
        VkFramebufferCreateInfo fci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fci.renderPass = cur_pipeline_->render_pass; fci.attachmentCount = att; fci.pAttachments = views;
        fci.width = color->w; fci.height = color->h; fci.layers = 1;
        vkCreateFramebuffer(dev_->dev, &fci, nullptr, &fb_);
        VkRenderPassBeginInfo rbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        rbi.renderPass = cur_pipeline_->render_pass; rbi.framebuffer = fb_;
        rbi.renderArea = { { 0, 0 }, { (uint32_t)color->w, (uint32_t)color->h } };
        vkCmdBeginRenderPass(cb_, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        in_pass_ = true;
    }
    void end_pass() { if (in_pass_) { vkCmdEndRenderPass(cb_); in_pass_ = false; } if (fb_) { vkDestroyFramebuffer(dev_->dev, fb_, nullptr); fb_ = VK_NULL_HANDLE; } }

    // Write the pending slot binds into a fresh descriptor set and bind it (only for pipelines
    // whose layout was auto-built from reflection; explicit-layout pipelines use bind_descriptor_set).
    void flush_descriptors() {
        if (!desc_dirty_ || !cur_pipeline_ || cur_pipeline_->auto_dsl == VK_NULL_HANDLE) return;
        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = desc_pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &cur_pipeline_->auto_dsl;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(dev_->dev, &ai, &set) != VK_SUCCESS) return;
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufs; bufs.reserve(MAX_BIND * 2);
        std::vector<VkDescriptorImageInfo>  imgs; imgs.reserve(MAX_BIND * 2);
        for (const auto& b : cur_pipeline_->auto_bindings) {
            if (b.binding >= MAX_BIND) continue;
            VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            w.dstSet = set; w.dstBinding = b.binding; w.descriptorCount = 1; w.descriptorType = b.descriptorType;
            switch (b.descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    if (!ubo_[b.binding].buf) continue;
                    bufs.push_back({ ubo_[b.binding].buf, ubo_[b.binding].off, ubo_[b.binding].range }); w.pBufferInfo = &bufs.back(); break;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    if (!ssbo_[b.binding].buf) continue;
                    bufs.push_back({ ssbo_[b.binding].buf, ssbo_[b.binding].off, ssbo_[b.binding].range }); w.pBufferInfo = &bufs.back(); break;
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    if (!tex_[b.binding]) continue;
                    imgs.push_back({ VK_NULL_HANDLE, tex_[b.binding], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }); w.pImageInfo = &imgs.back(); break;
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    if (!samp_[b.binding]) continue;
                    imgs.push_back({ samp_[b.binding], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED }); w.pImageInfo = &imgs.back(); break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    if (!tex_[b.binding] || !samp_[b.binding]) continue;
                    imgs.push_back({ samp_[b.binding], tex_[b.binding], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }); w.pImageInfo = &imgs.back(); break;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    // GENERAL, not SHADER_READ_ONLY: a storage image is written, not sampled.
                    if (!simg_[b.binding]) continue;
                    imgs.push_back({ VK_NULL_HANDLE, simg_[b.binding], VK_IMAGE_LAYOUT_GENERAL }); w.pImageInfo = &imgs.back(); break;
                default: continue;
            }
            writes.push_back(w);
        }
        if (!writes.empty()) vkUpdateDescriptorSets(dev_->dev, (uint32_t)writes.size(), writes.data(), 0, nullptr);
        vkCmdBindDescriptorSets(cb_, cur_pipeline_->bind, cur_pipeline_->layout, 0, 1, &set, 0, nullptr);
        desc_dirty_ = false;
    }

    static const int MAX_BIND = 16;
    VKDevice* dev_ = nullptr;
    VkCommandBuffer cb_ = VK_NULL_HANDLE;
    VkFramebuffer fb_ = VK_NULL_HANDLE;
    int color_tex_ = -1;   // bound colour target's texture id (looked up per use; Pool may move)
    int depth_tex_ = -1;   // bound depth target's texture id (-1 = none)
    VKRayPipeline* rt_ = nullptr;   // bound ray-tracing pipeline (its tables feed trace_rays)
    VKPipeline* cur_pipeline_ = nullptr;
    bool in_pass_ = false;
    bool pass_has_depth_ = false;   // attachment shape of the open pass (render-pass compatibility)
    // Pending slot binds (auto descriptor path) + the per-frame pool they allocate from.
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    struct SlotBuf { VkBuffer buf = VK_NULL_HANDLE; VkDeviceSize off = 0; VkDeviceSize range = VK_WHOLE_SIZE; };
    SlotBuf  ubo_[MAX_BIND]{}, ssbo_[MAX_BIND]{};
    VkImageView tex_[MAX_BIND]{};
    VkImageView simg_[MAX_BIND]{};   // storage images (bind_storage_texture), bound as STORAGE_IMAGE in GENERAL layout
    VkSampler   samp_[MAX_BIND]{};
    bool desc_dirty_ = false;
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

    // Windowed present: wait the acquire semaphore (at colour-output), signal render-finished,
    // submit, then present the image. Otherwise the plain offscreen submit.
    if (d->have_acquired_) {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &d->acquire_sem_; si.pWaitDstStageMask = &wait_stage;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &d->render_sem_; si.pNext = nullptr;
        vkQueueSubmit(d->queue, 1, &si, f);
        d->present_backbuffer();
        return;
    }
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
