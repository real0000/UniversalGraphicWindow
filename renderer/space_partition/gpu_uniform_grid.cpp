// renderer/space_partition/gpu_uniform_grid.cpp
//
// GPU uniform-grid build (insert) + frustum cull, recorded as two compute dispatches.

#include "gpu_uniform_grid.hpp"

#include "../shader_compiler/shader_compiler.hpp"   // HLSL -> backend blob (cached) at runtime

#include <algorithm>
#include <cstring>

namespace window {
namespace gfx {

//=============================================================================
// Constant-buffer mirror — must match the std140/cbuffer layout in the shaders.
//=============================================================================
struct GpuGridUBO {
    float   origin[4];      // .xyz origin
    float   cell_size[4];   // .xyz cell extent
    int32_t dims[4];        // dim_x, dim_y, dim_z, item_count
    int32_t misc[4];        // cell_count, bucket_capacity, vis_capacity, 0
    float   planes[6][4];   // frustum planes
};
static_assert(sizeof(GpuGridUBO) == 160, "GpuGridUBO must match the shader layout (160 bytes)");

// One persistent mirror per grid so set_frustum() can rewrite only the planes.
static GpuGridUBO* ubo_of(void* p) { return static_cast<GpuGridUBO*>(p); }

//=============================================================================
// Compute shaders, authored once in HLSL (raw RWByteAddressBuffers so one source serves
// every backend) and compiled for the active backend by the built-in shader compiler. The
// register bindings (b0 / u1..u4) map to uniform slot 0 and storage slots 1..4 -- exactly the
// bind_*_buffer slots used below. Compiled blobs are cached on disk.
//=============================================================================
static const char* HLSL_INSERT = R"(
cbuffer Params : register(b0) {
    float4 origin; float4 cell_size; int4 dims; int4 misc; float4 planes[6];
};
RWByteAddressBuffer items   : register(u1);
RWByteAddressBuffer counts  : register(u2);
RWByteAddressBuffer buckets : register(u3);

int cell_of(float3 p) {
    int3 c = (int3)floor((p - origin.xyz) / cell_size.xyz);
    c = clamp(c, int3(0, 0, 0), dims.xyz - int3(1, 1, 1));
    return (c.z * dims.y + c.y) * dims.x + c.x;
}
[numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
    uint i = gid.x;
    if ((int)i >= dims.w) return;
    float3 mn = asfloat(items.Load3(i * 32u + 0u));
    float3 mx = asfloat(items.Load3(i * 32u + 16u));
    float3 center = 0.5 * (mn + mx);
    int  cell = cell_of(center);
    uint slot;
    counts.InterlockedAdd((uint)cell * 4u, 1u, slot);
    uint cap = (uint)misc.y;
    if (slot < cap) buckets.Store(((uint)cell * cap + slot) * 4u, i);
}
)";

static const char* HLSL_CULL = R"(
cbuffer Params : register(b0) {
    float4 origin; float4 cell_size; int4 dims; int4 misc; float4 planes[6];
};
RWByteAddressBuffer items   : register(u1);
RWByteAddressBuffer counts  : register(u2);
RWByteAddressBuffer buckets : register(u3);
RWByteAddressBuffer visible : register(u4);

bool inside_planes(float3 p) {
    [unroll] for (int k = 0; k < 6; ++k) { float4 pl = planes[k]; if (dot(pl.xyz, p) + pl.w < 0.0) return false; }
    return true;
}
bool cell_intersects(float3 mn, float3 mx) {
    [unroll] for (int k = 0; k < 6; ++k) {
        float4 pl = planes[k];
        float3 pv = float3(pl.x >= 0.0 ? mx.x : mn.x, pl.y >= 0.0 ? mx.y : mn.y, pl.z >= 0.0 ? mx.z : mn.z);
        if (dot(pl.xyz, pv) + pl.w < 0.0) return false;
    }
    return true;
}
[numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
    uint c = gid.x;
    if ((int)c >= misc.x) return;
    uint cnt = counts.Load(c * 4u);
    if (cnt == 0u) return;
    int cx = (int)c % dims.x;
    int cy = ((int)c / dims.x) % dims.y;
    int cz = (int)c / (dims.x * dims.y);
    float3 mn = origin.xyz + float3(cx, cy, cz) * cell_size.xyz;
    float3 mx = mn + cell_size.xyz;
    if (!cell_intersects(mn, mx)) return;
    uint cap = (uint)misc.y;
    uint lim = min(cnt, cap);
    for (uint s = 0u; s < lim; ++s) {
        uint item = buckets.Load((c * cap + s) * 4u);
        float3 imn = asfloat(items.Load3(item * 32u + 0u));
        float3 imx = asfloat(items.Load3(item * 32u + 16u));
        float3 center = 0.5 * (imn + imx);
        if (inside_planes(center)) {
            uint o;
            visible.InterlockedAdd(0u, 1u, o);
            if (o < (uint)misc.z) visible.Store(4u + o * 4u, item);
        }
    }
}
)";

//=============================================================================
// Shader creation: compile the HLSL for this backend (cached on disk).
//=============================================================================
ShaderHandle make_compute(GraphicDevice* dev, const char* hlsl) {
#ifdef WINDOW_SUPPORT_SHADER_COMPILER
    return ShaderCompiler::compile_and_create_cached(dev, hlsl, std::strlen(hlsl),
                                                     ShaderStage::Compute, "main");
#else
    (void)dev; (void)hlsl; return ShaderHandle{};   // requires the built-in shader compiler
#endif
}

//=============================================================================
// GpuUniformGrid
//=============================================================================
bool GpuUniformGrid::init(GraphicDevice* device, const Desc& desc) {
    shutdown();
    device_ = device;
    backend_ = device->get_backend();
    desc_ = desc;

    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps);
    device->get_capabilities(&caps);
    if (!caps.compute_shaders) return false;

    insert_cs_ = make_compute(device, HLSL_INSERT);
    cull_cs_   = make_compute(device, HLSL_CULL);
    if (!insert_cs_.valid() || !cull_cs_.valid()) { shutdown(); return false; }

    // Binding layout: 1 uniform (b0) + 4 storage (u1..u4), all compute-visible. Vulkan/D3D12
    // bind via a descriptor set; OpenGL/D3D11 bind by slot and need no layout.
    if (uses_descriptor_sets()) {
        DescriptorSetLayoutDesc dl; dl.binding_count = 5;
        dl.bindings[0] = { 0, BindingType::UniformBuffer, 1, STAGE_COMPUTE };
        for (int i = 1; i < 5; ++i) dl.bindings[i] = { (uint32_t)i, BindingType::StorageBuffer, 1, STAGE_COMPUTE };
        set_layout_ = device->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = set_layout_;
        pipe_layout_ = device->create_pipeline_layout(pl);
    }

    PipelineDesc pi; pi.compute_shader = insert_cs_; pi.layout = pipe_layout_; pi.debug_name = "space_partition.insert";
    insert_pipe_ = device->create_pipeline(pi);
    PipelineDesc pc; pc.compute_shader = cull_cs_; pc.layout = pipe_layout_; pc.debug_name = "space_partition.cull";
    cull_pipe_ = device->create_pipeline(pc);

    if (!insert_pipe_.valid() || !cull_pipe_.valid()) { shutdown(); return false; }
    return true;
}

void GpuUniformGrid::shutdown() {
    if (device_) {
        items_buf_.destroy(); counts_buf_.destroy(); buckets_buf_.destroy();
        visible_buf_.destroy(); params_buf_.destroy();
        if (desc_set_.valid())    device_->destroy_descriptor_set(desc_set_);
        if (insert_pipe_.valid()) device_->destroy_pipeline(insert_pipe_);
        if (cull_pipe_.valid())   device_->destroy_pipeline(cull_pipe_);
        if (insert_cs_.valid())   device_->destroy_shader(insert_cs_);
        if (cull_cs_.valid())     device_->destroy_shader(cull_cs_);
        if (pipe_layout_.valid()) device_->destroy_pipeline_layout(pipe_layout_);
        if (set_layout_.valid())  device_->destroy_descriptor_set_layout(set_layout_);
    }
    desc_set_ = {}; insert_pipe_ = {}; cull_pipe_ = {}; insert_cs_ = {}; cull_cs_ = {};
    pipe_layout_ = {}; set_layout_ = {};
    item_count_ = cell_count_ = bucket_capacity_ = vis_capacity_ = 0;
    device_ = nullptr;
}

bool GpuUniformGrid::set_scene(const SceneItem* items, int count, const GridParams& params) {
    if (!device_ || count < 0) return false;
    cpu_params_ = params;
    item_count_ = count;
    cell_count_ = params.cell_count();
    bucket_capacity_ = std::max(1, desc_.bucket_capacity);
    vis_capacity_ = desc_.vis_capacity > 0 ? desc_.vis_capacity : std::max(1, count);

    // Item AABBs as 2 vec4 (min.xyz, max.xyz) each.
    std::vector<float> idata((size_t)std::max(1, count) * 8, 0.0f);
    for (int i = 0; i < count; ++i) {
        const math::AABB& b = items[i].bounds;
        float* d = &idata[(size_t)i * 8];
        d[0] = b.min_pt.x; d[1] = b.min_pt.y; d[2] = b.min_pt.z; d[3] = 0.0f;
        d[4] = b.max_pt.x; d[5] = b.max_pt.y; d[6] = b.max_pt.z; d[7] = 0.0f;
    }

    const uint32_t items_bytes   = (uint32_t)std::max(1, count) * 32u;
    const uint32_t counts_bytes  = (uint32_t)std::max(1, cell_count_) * 4u;
    const uint32_t buckets_bytes = (uint32_t)std::max(1, cell_count_ * bucket_capacity_) * 4u;
    const uint32_t visible_bytes = (uint32_t)(1 + std::max(1, vis_capacity_)) * 4u;

    bool ok = true;
    ok &= items_buf_.create(device_, items_bytes, 0, ResourceUsage::Default, idata.data(), "space_partition.items");
    ok &= counts_buf_.create(device_, counts_bytes, 0, ResourceUsage::Default, nullptr, "space_partition.counts");
    ok &= buckets_buf_.create(device_, buckets_bytes, 0, ResourceUsage::Default, nullptr, "space_partition.buckets");
    ok &= visible_buf_.create(device_, visible_bytes, 0, ResourceUsage::Default, nullptr, "space_partition.visible");
    ok &= params_buf_.create(device_, sizeof(GpuGridUBO), ResourceUsage::Default, nullptr, "space_partition.params");
    if (!ok) return false;

    GpuGridUBO ubo{};
    ubo.origin[0] = params.origin.x; ubo.origin[1] = params.origin.y; ubo.origin[2] = params.origin.z;
    ubo.cell_size[0] = params.cell_size.x; ubo.cell_size[1] = params.cell_size.y; ubo.cell_size[2] = params.cell_size.z;
    ubo.dims[0] = params.dim_x; ubo.dims[1] = params.dim_y; ubo.dims[2] = params.dim_z; ubo.dims[3] = count;
    ubo.misc[0] = cell_count_; ubo.misc[1] = bucket_capacity_; ubo.misc[2] = vis_capacity_; ubo.misc[3] = 0;
    std::memcpy(cpu_ubo_, &ubo, sizeof ubo);
    params_buf_.update(cpu_ubo_, sizeof(GpuGridUBO));

    if (uses_descriptor_sets()) create_descriptor_set();
    return true;
}

void GpuUniformGrid::create_descriptor_set() {
    if (desc_set_.valid()) { device_->destroy_descriptor_set(desc_set_); desc_set_ = {}; }
    auto w = [](uint32_t b, BindingType t, BufferHandle h, uint32_t sz) {
        DescriptorWrite dw{}; dw.binding = b; dw.type = t; dw.buffer = h; dw.buffer_offset = 0; dw.buffer_size = sz;
        return dw;
    };
    DescriptorSetDesc sd; sd.layout = set_layout_; sd.write_count = 5;
    sd.writes[0] = w(0, BindingType::UniformBuffer, params_buf_.handle(),  params_buf_.size());
    sd.writes[1] = w(1, BindingType::StorageBuffer, items_buf_.handle(),   items_buf_.size());
    sd.writes[2] = w(2, BindingType::StorageBuffer, counts_buf_.handle(),  counts_buf_.size());
    sd.writes[3] = w(3, BindingType::StorageBuffer, buckets_buf_.handle(), buckets_buf_.size());
    sd.writes[4] = w(4, BindingType::StorageBuffer, visible_buf_.handle(), visible_buf_.size());
    desc_set_ = device_->create_descriptor_set(sd);
}

void GpuUniformGrid::set_frustum(const Frustum& frustum) {
    if (!params_buf_.valid()) return;
    GpuGridUBO* u = ubo_of(&cpu_ubo_);
    for (int k = 0; k < 6; ++k) {
        u->planes[k][0] = frustum.planes[k].x;
        u->planes[k][1] = frustum.planes[k].y;
        u->planes[k][2] = frustum.planes[k].z;
        u->planes[k][3] = frustum.planes[k].w;
    }
    params_buf_.update(&cpu_ubo_, sizeof(GpuGridUBO));
}

void GpuUniformGrid::reset_counters() {
    if (cell_count_ > 0 && counts_buf_.valid()) {
        std::vector<uint32_t> zeros((size_t)cell_count_, 0u);
        counts_buf_.update(zeros.data(), (uint32_t)cell_count_ * 4u);
    }
    if (visible_buf_.valid()) {
        uint32_t zero = 0u;
        visible_buf_.update(&zero, 4u, 0u);
    }
}

void GpuUniformGrid::bind_slots(GraphicCommander* cmd) const {
    cmd->bind_uniform_buffer(0, params_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(1, items_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(2, counts_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(3, buckets_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(4, visible_buf_.handle(), 0, 0);
}

void GpuUniformGrid::record(GraphicCommander* cmd) {
    if (!valid() || item_count_ <= 0) return;
    const bool desc = uses_descriptor_sets();
    auto groups = [](int n) -> uint32_t { return (uint32_t)((std::max(1, n) + 63) / 64); };

    // Pass 1: build (one thread per item).
    cmd->set_pipeline(insert_pipe_);
    if (desc) cmd->bind_descriptor_set(0, desc_set_); else bind_slots(cmd);
    cmd->dispatch(groups(item_count_));
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);

    // Pass 2: frustum cull (one thread per cell).
    cmd->set_pipeline(cull_pipe_);
    if (desc) cmd->bind_descriptor_set(0, desc_set_); else bind_slots(cmd);
    cmd->dispatch(groups(cell_count_));
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
}

int GpuUniformGrid::read_cell_counts(std::vector<uint32_t>& out) {
    out.assign((size_t)std::max(0, cell_count_), 0u);
    if (cell_count_ <= 0 || !counts_buf_.valid()) return 0;
    counts_buf_.read(out.data(), (uint32_t)cell_count_ * 4u);
    uint32_t mx = 0; for (uint32_t v : out) mx = std::max(mx, v);
    return (int)mx;
}

void GpuUniformGrid::read_buckets(std::vector<uint32_t>& out) {
    int n = cell_count_ * bucket_capacity_;
    out.assign((size_t)std::max(0, n), 0u);
    if (n <= 0 || !buckets_buf_.valid()) return;
    buckets_buf_.read(out.data(), (uint32_t)n * 4u);
}

int GpuUniformGrid::read_visible(std::vector<uint32_t>& out) {
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
