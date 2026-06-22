#pragma once
// renderer/space_partition/gpu_leaf_cull.hpp
//
// GpuLeafCuller — the GPU query shared by every TREE partition (BVH, Octree, Quadtree, BSP).
//
// Those structures build on the CPU (pointer-chasing builds don't map well to a single compute
// dispatch), then flatten their leaves into a PartitionLeaves (tight per-leaf AABB + a slice of
// item indices). This class uploads that leaf set and runs one compute pass — the standard
// "GPU-driven culling over a precomputed acceleration structure" — to frustum-cull it: one
// thread per leaf skips leaves wholly outside the frustum, then tests each item's centre and
// atomically appends the visible ones to a compacted list. So each tree gets a GPU query path
// regardless of how it was built; the UniformGrid keeps its own GPU pass (it also BUILDS on the
// GPU). The visible set matches the structure's CPU frustum_cull exactly, so one can verify the
// other (see examples/space_partition.cpp).
//
// Cross-API the same way as the rest of renderer/: inline GLSL on OpenGL, embedded SPIR-V on
// Vulkan, embedded DXBC on D3D11/D3D12 (raw storage buffers); descriptor set on Vulkan/D3D12,
// slot binds on OpenGL/D3D11.
//
// Lifetime: init() once → set_data() per (structure, scene) → per query: set_frustum(),
// reset_counters(), record() between a commander's begin()/end(); submit + wait, then read_visible().

#include "space_partition.hpp"
#include "../buffer/gpu_buffer.hpp"
#include "../../graphics_api.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

class GpuLeafCuller {
public:
    struct Desc {
        Desc() {}   // empty user-provided default ctor: GCC will not brace-init this nested aggregate (math::AABB member) in a `const Desc&={}` default arg
        int vis_capacity = 0;   // max items the cull records (0 = size to the item count)
    };

    GpuLeafCuller() = default;
    ~GpuLeafCuller() { shutdown(); }
    GpuLeafCuller(const GpuLeafCuller&) = delete;
    GpuLeafCuller& operator=(const GpuLeafCuller&) = delete;

    bool init(GraphicDevice* device, const Desc& desc = {});
    void shutdown();
    bool valid() const { return cull_pipe_.valid(); }

    // Upload the scene items + a flattened leaf set (from BVH/Octree/Quadtree/BSP::extract_leaves).
    // (Re)allocates the GPU buffers and descriptor set. Call again to switch structure/scene.
    bool set_data(const SceneItem* items, int item_count, const PartitionLeaves& leaves);

    void set_frustum(const Frustum& frustum);   // upload the cull planes
    void reset_counters();                       // zero the visible-list counter

    void record(GraphicCommander* cmd);          // record the cull dispatch (between begin()/end())

    int  leaf_count() const { return leaf_count_; }
    int  item_count() const { return item_count_; }
    // Compacted visible item indices. Returns the GPU's count (may exceed vis_capacity if it
    // overflowed); `out` holds up to vis_capacity entries. (Non-const: reads back from GPU.)
    int  read_visible(std::vector<uint32_t>& out);

private:
    bool uses_descriptor_sets() const {
        return backend_ == Backend::Vulkan || backend_ == Backend::D3D12;
    }
    void bind_slots(GraphicCommander* cmd) const;
    void create_descriptor_set();

    GraphicDevice* device_ = nullptr;
    Backend        backend_ = Backend::OpenGL;
    Desc           desc_{};

    ShaderHandle              cull_cs_;
    PipelineHandle            cull_pipe_;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
    DescriptorSetHandle       desc_set_;

    ConstBuffer   params_buf_;     // binding 0 (b0)
    StorageBuffer items_buf_;      // binding 1 (u1): 2 vec4/item
    StorageBuffer leaf_bnd_buf_;   // binding 2 (u2): 2 vec4/leaf
    StorageBuffer leaf_rng_buf_;   // binding 3 (u3): 2 uint/leaf (first,count)
    StorageBuffer item_idx_buf_;   // binding 4 (u4): uint item indices
    StorageBuffer visible_buf_;    // binding 5 (u5): [0]=count, [1..]=ids

    int item_count_ = 0;
    int leaf_count_ = 0;
    int vis_capacity_ = 0;
    // Persistent mirror of the constant buffer (LeafCullUBO, 112 bytes; defined in the .cpp).
    alignas(16) unsigned char cpu_ubo_[112] = {};
};

} // namespace gfx
} // namespace window
