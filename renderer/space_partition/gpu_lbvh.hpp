#pragma once
// renderer/space_partition/gpu_lbvh.hpp
//
// GpuLbvh — a Linear BVH built and queried entirely on the GPU, the canonical "hierarchical id +
// comparison" acceleration structure (Karras 2012; the scheme used by GPU ray tracers and Bullet's
// GPU broad-phase). It goes beyond the flat per-leaf frustum cull (gpu_leaf_cull.hpp) by putting a
// real hierarchy on the device and pruning whole subtrees during traversal. Three compute passes:
//
//   1. sort     — give each item a 30-bit MORTON CODE (a Z-order "hierarchical id" of its quantised
//                 centroid) and sort the (code, item) pairs by code (single-workgroup bitonic sort).
//   2. build    — build a binary radix tree over the sorted codes: each internal node finds its leaf
//                 range and split by COMPARING codes via their longest common prefix (Karras' delta),
//                 then node AABBs propagate leaf->root. Fully parallel; one workgroup.
//   3. traverse — hierarchical broad-phase: one thread per item descends the tree, pruning subtrees
//                 whose AABB misses the query box, and counts overlapping neighbours.
//
// Everything — id assignment, sort, hierarchy construction, traversal — runs on the GPU. Cross-API
// exactly like the rest of renderer/ (inline GLSL / SPIR-V / DXBC, descriptor set vs slot binds).
//
// NOTE: the single-workgroup sort/build caps the item count at LBVH_MAX_ITEMS (1024) — plenty for a
// demo/verification; a production build would swap in a multi-pass radix sort + atomic-flag bounds to
// scale. Verified against a brute-force O(n^2) overlap count (see examples/space_partition.cpp).
//
// Lifetime: init() once -> set_scene() per scene -> record() between a commander's begin()/end();
// submit + wait, then read_overlap_counts() / read_sorted_codes() / read_root_bounds().

#include "space_partition.hpp"
#include "../buffer/gpu_buffer.hpp"
#include "../../graphics_api.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

class GpuLbvh {
public:
    static const int MAX_ITEMS = 1024;   // single-workgroup sort/build limit (== shader P2)

    GpuLbvh() = default;
    ~GpuLbvh() { shutdown(); }
    GpuLbvh(const GpuLbvh&) = delete;
    GpuLbvh& operator=(const GpuLbvh&) = delete;

    bool init(GraphicDevice* device);
    void shutdown();
    bool valid() const { return sort_pipe_.valid() && build_pipe_.valid() && traverse_pipe_.valid(); }

    // Upload the scene; (re)allocates buffers + descriptor set. Returns false if count > MAX_ITEMS.
    bool set_scene(const SceneItem* items, int count);

    // Record the three passes (sort -> build -> traverse) into `cmd` (between begin()/end()).
    void record(GraphicCommander* cmd);

    int item_count() const { return item_count_; }

    // ---- Readback (after submit + wait_idle) --------------------------------
    // Per-item neighbour count: how many other items' AABBs overlap item i (broad-phase result).
    void read_overlap_counts(std::vector<uint32_t>& out);
    // The sorted hierarchical ids and their item indices (codes non-decreasing; vals a permutation).
    void read_sorted_codes(std::vector<uint32_t>& codes, std::vector<uint32_t>& items);
    // The root node's AABB (should equal the union of all item AABBs once built).
    void read_root_bounds(math::Vec3* out_min, math::Vec3* out_max);

private:
    bool uses_descriptor_sets() const {
        return backend_ == Backend::Vulkan || backend_ == Backend::D3D12;
    }
    void bind_slots(GraphicCommander* cmd) const;
    void create_descriptor_set();

    GraphicDevice* device_ = nullptr;
    Backend        backend_ = Backend::OpenGL;

    ShaderHandle   sort_cs_, build_cs_, traverse_cs_;
    PipelineHandle sort_pipe_, build_pipe_, traverse_pipe_;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
    DescriptorSetHandle       desc_set_;

    ConstBuffer   params_buf_;   // 0 (b0): origin / inv_size / misc
    StorageBuffer items_buf_;    // 1 (u1): 2 vec4/item
    StorageBuffer kv_buf_;       // 2 (u2): 2 uint/item (morton code, item index)
    StorageBuffer links_buf_;    // 3 (u3): int4/node (left,right,parent,valid)
    StorageBuffer aabb_buf_;     // 4 (u4): 2 vec4/node (min,max)
    StorageBuffer overlap_buf_;  // 5 (u5): uint/item (neighbour count)

    int item_count_ = 0;
    int node_count_ = 0;
    // Persistent mirror of the constant buffer (LbvhUBO, 48 bytes; defined in the .cpp).
    alignas(16) unsigned char cpu_ubo_[48] = {};
};

} // namespace gfx
} // namespace window
