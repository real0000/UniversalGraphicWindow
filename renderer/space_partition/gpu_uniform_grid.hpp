#pragma once
// renderer/space_partition/gpu_uniform_grid.hpp
//
// GpuUniformGrid — the GPU uniform-grid scene partition, built and queried by compute
// shaders over the backend-neutral RHI (GraphicDevice / GraphicCommander). It mirrors the
// CPU UniformGrid (uniform_grid.hpp): items are filed by centre into equal cells, then a
// frustum-cull pass walks the grid and emits a compacted list of visible items. Two passes:
//
//   build : one thread per item — counts[cell]++ (atomic) and append the item index into a
//           fixed-capacity per-cell bucket (buckets[cell*capacity + slot]).
//   cull  : one thread per cell — skip cells wholly outside the frustum, then test each
//           bucketed item's centre and atomically append the visible ones to visible[].
//
// Cross-API: inline GLSL on OpenGL, embedded SPIR-V on Vulkan, embedded DXBC on D3D11/D3D12
// (raw RWByteAddressBuffer storage so one blob serves both). Bindings go through a descriptor
// set on Vulkan/D3D12 and slot binds on OpenGL/D3D11 — the same split the rest of renderer/
// uses. The grid parameters + frustum planes travel in one constant buffer.
//
// The bucket layout differs from the CPU's CSR layout, but the PARTITION is identical, so a
// readback can be checked against the CPU grid as per-cell sets (read_cell_counts / per-cell
// buckets) and the visible list as a set (read_visible). See examples/space_partition.cpp.
//
// Lifetime: init() once → set_scene() to upload items+grid → per query: set_frustum(),
// reset_counters(), then record() between a commander's begin()/end(); submit + wait, then
// read_*(). shutdown() (or the destructor) frees everything.

#include "space_partition.hpp"
#include "../buffer/gpu_buffer.hpp"
#include "../../graphics_api.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

class GpuUniformGrid {
public:
    struct Desc {
        Desc() {}   // empty user-provided default ctor: GCC will not brace-init this nested aggregate (math::AABB member) in a `const Desc&={}` default arg
        int bucket_capacity = 64;  // max items stored per cell (overflow is counted but dropped)
        int vis_capacity    = 0;   // max items the cull pass records (0 = size to the item count)
    };

    GpuUniformGrid() = default;
    ~GpuUniformGrid() { shutdown(); }
    GpuUniformGrid(const GpuUniformGrid&) = delete;
    GpuUniformGrid& operator=(const GpuUniformGrid&) = delete;

    // Create shaders, pipelines and (on Vulkan/D3D12) the binding layout. Returns false if
    // the backend has no compute support or a shader/pipeline fails to build.
    bool init(GraphicDevice* device, const Desc& desc = {});
    void shutdown();
    bool valid() const { return insert_pipe_.valid() && cull_pipe_.valid(); }

    // Upload the scene: (re)allocates the GPU buffers and the descriptor set, uploads the
    // item AABBs and grid parameters. Call again to replace the scene.
    bool set_scene(const SceneItem* items, int count, const GridParams& params);

    // Upload the cull frustum (writes its planes into the constant buffer). Call before record().
    void set_frustum(const Frustum& frustum);
    // Zero the per-cell counts and the visible-list counter. Call before record(), outside the
    // commander's begin()/end() (it uploads via the device, which is synchronous).
    void reset_counters();

    // Record the build + cull dispatches into `cmd` (between its begin()/end()). The caller
    // submits the commander and waits before reading results back.
    void record(GraphicCommander* cmd);

    // ---- Readback (after submit + wait_idle) --------------------------------
    int  item_count()   const { return item_count_; }
    int  cell_count()   const { return cell_count_; }
    int  bucket_capacity() const { return bucket_capacity_; }
    const GridParams& params() const { return cpu_params_; }

    // Per-cell item counts (size == cell_count()). Returns the largest cell count seen, so a
    // caller can detect bucket overflow (> bucket_capacity()). (Non-const: reads back from GPU.)
    int  read_cell_counts(std::vector<uint32_t>& out);
    // Raw bucket contents (size == cell_count()*bucket_capacity()); entry [c*cap + s] is the
    // s-th item index in cell c (valid for s < min(counts[c], cap)).
    void read_buckets(std::vector<uint32_t>& out);
    // Compacted visible item indices from the cull pass. Returns the number the GPU counted
    // (may exceed out.size() if it overflowed vis_capacity()); out holds up to vis_capacity().
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

    ShaderHandle   insert_cs_, cull_cs_;
    PipelineHandle insert_pipe_, cull_pipe_;
    // Vulkan/D3D12 binding objects (invalid on OpenGL/D3D11).
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      pipe_layout_;
    DescriptorSetHandle       desc_set_;

    // GPU buffers (RAII; reassigning frees the old allocation).
    ConstBuffer   params_buf_;   // binding 0 (b0)
    StorageBuffer items_buf_;    // binding 1 (u1): 2 vec4/item (min,max)
    StorageBuffer counts_buf_;   // binding 2 (u2): uint/cell
    StorageBuffer buckets_buf_;  // binding 3 (u3): uint, cell_count*bucket_capacity
    StorageBuffer visible_buf_;  // binding 4 (u4): [0]=count, [1..]=ids

    int        item_count_ = 0;
    int        cell_count_ = 0;
    int        bucket_capacity_ = 0;
    int        vis_capacity_ = 0;
    GridParams cpu_params_{};
    // Persistent mirror of the constant buffer (GpuGridUBO, 160 bytes, defined in the .cpp)
    // so set_frustum() can rewrite only the planes and re-upload. Raw bytes here because the
    // layout struct is private to the .cpp; the .cpp static_asserts the size.
    alignas(16) unsigned char cpu_ubo_[160] = {};
};

} // namespace gfx
} // namespace window
