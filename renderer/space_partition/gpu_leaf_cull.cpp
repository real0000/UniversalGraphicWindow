// renderer/space_partition/gpu_leaf_cull.cpp
//
// GPU frustum cull over a flattened leaf set, recorded as one compute dispatch.

#include "gpu_leaf_cull.hpp"

#include <algorithm>
#include <cstring>

namespace window {
namespace gfx {

//=============================================================================
// Constant-buffer mirror — must match the std140/cbuffer layout in leaf_cull.*.
//=============================================================================
struct LeafCullUBO {
    float   planes[6][4];   // frustum planes
    int32_t misc[4];        // leaf_count, vis_capacity, 0, 0
};
static_assert(sizeof(LeafCullUBO) == 112, "LeafCullUBO must match the shader layout (112 bytes)");

//=============================================================================
// Compute shader, authored once in HLSL (raw RWByteAddressBuffers; b0 + u1..u5 map to
// uniform slot 0 and storage slots 1..5). Compiled per backend + disk-cached by make_compute.
//=============================================================================
static const char* HLSL_LEAF_CULL = R"(
cbuffer Params : register(b0) { float4 planes[6]; int4 misc; };  // misc.x=leaf_count, y=vis_capacity
RWByteAddressBuffer items   : register(u1);   // 2 float4/item (min,max)
RWByteAddressBuffer leafBnd : register(u2);   // 2 float4/leaf (min,max)
RWByteAddressBuffer leafRng : register(u3);   // 2 uint/leaf (first,count)
RWByteAddressBuffer itemIdx : register(u4);   // uint
RWByteAddressBuffer visible : register(u5);

bool inside_planes(float3 p) {
    [unroll] for (int k = 0; k < 6; ++k) { float4 pl = planes[k]; if (dot(pl.xyz, p) + pl.w < 0.0) return false; }
    return true;
}
bool box_intersects(float3 mn, float3 mx) {
    [unroll] for (int k = 0; k < 6; ++k) {
        float4 pl = planes[k];
        float3 pv = float3(pl.x >= 0.0 ? mx.x : mn.x, pl.y >= 0.0 ? mx.y : mn.y, pl.z >= 0.0 ? mx.z : mn.z);
        if (dot(pl.xyz, pv) + pl.w < 0.0) return false;
    }
    return true;
}
[numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
    uint l = gid.x;
    if ((int)l >= misc.x) return;
    float3 mn = asfloat(leafBnd.Load3(l * 32u + 0u));
    float3 mx = asfloat(leafBnd.Load3(l * 32u + 16u));
    if (!box_intersects(mn, mx)) return;
    uint first = leafRng.Load(l * 8u + 0u);
    uint count = leafRng.Load(l * 8u + 4u);
    for (uint k = 0u; k < count; ++k) {
        uint item = itemIdx.Load((first + k) * 4u);
        float3 imn = asfloat(items.Load3(item * 32u + 0u));
        float3 imx = asfloat(items.Load3(item * 32u + 16u));
        float3 ctr = 0.5 * (imn + imx);
        if (inside_planes(ctr)) {
            uint o;
            visible.InterlockedAdd(0u, 1u, o);
            if (o < (uint)misc.y) visible.Store(4u + o * 4u, item);
        }
    }
}
)";

extern ShaderHandle make_compute(GraphicDevice* dev, const char* hlsl);

//=============================================================================
// GpuLeafCuller
//=============================================================================
bool GpuLeafCuller::init(GraphicDevice* device, const Desc& desc) {
    shutdown();
    device_ = device;
    backend_ = device->get_backend();
    desc_ = desc;

    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps);
    device->get_capabilities(&caps);
    if (!caps.compute_shaders) return false;

    cull_cs_ = make_compute(device, HLSL_LEAF_CULL);
    if (!cull_cs_.valid()) { shutdown(); return false; }

    if (uses_descriptor_sets()) {
        DescriptorSetLayoutDesc dl; dl.binding_count = 6;
        dl.bindings[0] = { 0, BindingType::UniformBuffer, 1, STAGE_COMPUTE };
        for (int i = 1; i < 6; ++i) dl.bindings[i] = { (uint32_t)i, BindingType::StorageBuffer, 1, STAGE_COMPUTE };
        set_layout_ = device->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = set_layout_;
        pipe_layout_ = device->create_pipeline_layout(pl);
    }

    PipelineDesc pc; pc.compute_shader = cull_cs_; pc.layout = pipe_layout_; pc.debug_name = "space_partition.leaf_cull";
    cull_pipe_ = device->create_pipeline(pc);
    if (!cull_pipe_.valid()) { shutdown(); return false; }
    return true;
}

void GpuLeafCuller::shutdown() {
    if (device_) {
        items_buf_.destroy(); leaf_bnd_buf_.destroy(); leaf_rng_buf_.destroy();
        item_idx_buf_.destroy(); visible_buf_.destroy(); params_buf_.destroy();
        if (desc_set_.valid())    device_->destroy_descriptor_set(desc_set_);
        if (cull_pipe_.valid())   device_->destroy_pipeline(cull_pipe_);
        if (cull_cs_.valid())     device_->destroy_shader(cull_cs_);
        if (pipe_layout_.valid()) device_->destroy_pipeline_layout(pipe_layout_);
        if (set_layout_.valid())  device_->destroy_descriptor_set_layout(set_layout_);
    }
    desc_set_ = {}; cull_pipe_ = {}; cull_cs_ = {}; pipe_layout_ = {}; set_layout_ = {};
    item_count_ = leaf_count_ = vis_capacity_ = 0;
    device_ = nullptr;
}

bool GpuLeafCuller::set_data(const SceneItem* items, int item_count, const PartitionLeaves& leaves) {
    if (!device_ || item_count < 0) return false;
    item_count_ = item_count;
    leaf_count_ = leaves.leaf_count();
    vis_capacity_ = desc_.vis_capacity > 0 ? desc_.vis_capacity : std::max(1, item_count);

    // Item AABBs: 2 vec4 (min,max) per item.
    std::vector<float> idata((size_t)std::max(1, item_count) * 8, 0.0f);
    for (int i = 0; i < item_count; ++i) {
        const math::AABB& b = items[i].bounds;
        float* d = &idata[(size_t)i * 8];
        d[0] = b.min_pt.x; d[1] = b.min_pt.y; d[2] = b.min_pt.z;
        d[4] = b.max_pt.x; d[5] = b.max_pt.y; d[6] = b.max_pt.z;
    }
    // Leaf bounds: 2 vec4 (min,max) per leaf.
    const int L = std::max(1, leaf_count_);
    std::vector<float>    lbnd((size_t)L * 8, 0.0f);
    std::vector<uint32_t> lrng((size_t)L * 2, 0u);
    for (int l = 0; l < leaf_count_; ++l) {
        const math::AABB& b = leaves.bounds[l];
        float* d = &lbnd[(size_t)l * 8];
        d[0] = b.min_pt.x; d[1] = b.min_pt.y; d[2] = b.min_pt.z;
        d[4] = b.max_pt.x; d[5] = b.max_pt.y; d[6] = b.max_pt.z;
        lrng[(size_t)l * 2 + 0] = leaves.first[l];
        lrng[(size_t)l * 2 + 1] = leaves.count[l];
    }
    std::vector<uint32_t> iidx = leaves.item_indices;
    if (iidx.empty()) iidx.push_back(0u);   // never allocate a zero-byte buffer

    bool ok = true;
    ok &= items_buf_.create(device_, (uint32_t)idata.size() * 4u, 0, ResourceUsage::Default, idata.data(), "leafcull.items");
    ok &= leaf_bnd_buf_.create(device_, (uint32_t)lbnd.size() * 4u, 0, ResourceUsage::Default, lbnd.data(), "leafcull.leafbnd");
    ok &= leaf_rng_buf_.create(device_, (uint32_t)lrng.size() * 4u, 0, ResourceUsage::Default, lrng.data(), "leafcull.leafrng");
    ok &= item_idx_buf_.create(device_, (uint32_t)iidx.size() * 4u, 0, ResourceUsage::Default, iidx.data(), "leafcull.itemidx");
    ok &= visible_buf_.create(device_, (uint32_t)(1 + std::max(1, vis_capacity_)) * 4u, 0, ResourceUsage::Default, nullptr, "leafcull.visible");
    ok &= params_buf_.create(device_, sizeof(LeafCullUBO), ResourceUsage::Default, nullptr, "leafcull.params");
    if (!ok) return false;

    LeafCullUBO ubo{};
    ubo.misc[0] = leaf_count_; ubo.misc[1] = vis_capacity_;
    std::memcpy(cpu_ubo_, &ubo, sizeof ubo);
    params_buf_.update(cpu_ubo_, sizeof(LeafCullUBO));

    if (uses_descriptor_sets()) create_descriptor_set();
    return true;
}

void GpuLeafCuller::create_descriptor_set() {
    if (desc_set_.valid()) { device_->destroy_descriptor_set(desc_set_); desc_set_ = {}; }
    auto w = [](uint32_t b, BindingType t, BufferHandle h, uint32_t sz) {
        DescriptorWrite dw{}; dw.binding = b; dw.type = t; dw.buffer = h; dw.buffer_offset = 0; dw.buffer_size = sz;
        return dw;
    };
    DescriptorSetDesc sd; sd.layout = set_layout_; sd.write_count = 6;
    sd.writes[0] = w(0, BindingType::UniformBuffer, params_buf_.handle(),   params_buf_.size());
    sd.writes[1] = w(1, BindingType::StorageBuffer, items_buf_.handle(),    items_buf_.size());
    sd.writes[2] = w(2, BindingType::StorageBuffer, leaf_bnd_buf_.handle(), leaf_bnd_buf_.size());
    sd.writes[3] = w(3, BindingType::StorageBuffer, leaf_rng_buf_.handle(), leaf_rng_buf_.size());
    sd.writes[4] = w(4, BindingType::StorageBuffer, item_idx_buf_.handle(), item_idx_buf_.size());
    sd.writes[5] = w(5, BindingType::StorageBuffer, visible_buf_.handle(),  visible_buf_.size());
    desc_set_ = device_->create_descriptor_set(sd);
}

void GpuLeafCuller::set_frustum(const Frustum& frustum) {
    if (!params_buf_.valid()) return;
    LeafCullUBO* u = reinterpret_cast<LeafCullUBO*>(cpu_ubo_);
    for (int k = 0; k < 6; ++k) {
        u->planes[k][0] = frustum.planes[k].x;
        u->planes[k][1] = frustum.planes[k].y;
        u->planes[k][2] = frustum.planes[k].z;
        u->planes[k][3] = frustum.planes[k].w;
    }
    params_buf_.update(cpu_ubo_, sizeof(LeafCullUBO));
}

void GpuLeafCuller::reset_counters() {
    if (visible_buf_.valid()) {
        uint32_t zero = 0u;
        visible_buf_.update(&zero, 4u, 0u);
    }
}

void GpuLeafCuller::bind_slots(GraphicCommander* cmd) const {
    cmd->bind_uniform_buffer(0, params_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(1, items_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(2, leaf_bnd_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(3, leaf_rng_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(4, item_idx_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(5, visible_buf_.handle(), 0, 0);
}

void GpuLeafCuller::record(GraphicCommander* cmd) {
    if (!valid() || leaf_count_ <= 0) return;
    const bool desc = uses_descriptor_sets();
    uint32_t groups = (uint32_t)((leaf_count_ + 63) / 64);
    cmd->set_pipeline(cull_pipe_);
    if (desc) cmd->bind_descriptor_set(0, desc_set_); else bind_slots(cmd);
    cmd->dispatch(groups);
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
}

int GpuLeafCuller::read_visible(std::vector<uint32_t>& out) {
    out.clear();
    if (!visible_buf_.valid()) return 0;
    std::vector<uint32_t> tmp((size_t)(1 + std::max(1, vis_capacity_)), 0u);
    visible_buf_.read(tmp.data(), (uint32_t)tmp.size() * 4u);
    uint32_t count = tmp[0];
    uint32_t keep = std::min<uint32_t>(count, (uint32_t)vis_capacity_);
    out.assign(tmp.begin() + 1, tmp.begin() + 1 + keep);
    return (int)count;
}

} // namespace gfx
} // namespace window
