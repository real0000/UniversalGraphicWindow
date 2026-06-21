// renderer/space_partition/gpu_lbvh.cpp
//
// GPU Linear BVH: Morton sort -> Karras radix-tree build -> hierarchical broad-phase traversal.

#include "gpu_lbvh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace window {
namespace gfx {

//=============================================================================
// Constant-buffer mirror — must match the std140/cbuffer layout in the lbvh_* shaders.
//=============================================================================
struct LbvhUBO {
    float   origin[4];     // scene min, xyz
    float   inv_size[4];   // 1/scene-extent xyz (centroid -> [0,1])
    int32_t misc[4];       // item_count, 0, 0, 0
};
static_assert(sizeof(LbvhUBO) == 48, "LbvhUBO must match the shader layout (48 bytes)");

//=============================================================================
// Compute shaders, authored once in HLSL (raw RWByteAddressBuffers; b0 + u1..u5 map to
// uniform slot 0 + storage slots 1..5). Compiled per backend + disk-cached by make_compute.
//=============================================================================
static const char* HLSL_SORT = R"(
cbuffer Params : register(b0) { float4 origin; float4 inv_size; int4 misc; };  // misc.x = item_count
RWByteAddressBuffer items : register(u1);   // 2 float4/item (min,max)
RWByteAddressBuffer kv    : register(u2);

#define P2 1024
groupshared uint skey[1024];
groupshared uint sval[1024];

uint expand_bits(uint v) {
    v = (v * 0x00010001u) & 0xFF0000FFu;
    v = (v * 0x00000101u) & 0x0F00F00Fu;
    v = (v * 0x00000011u) & 0xC30C30C3u;
    v = (v * 0x00000005u) & 0x49249249u;
    return v;
}
uint morton3D(float3 p) {
    float x = clamp(p.x * 1024.0, 0.0, 1023.0);
    float y = clamp(p.y * 1024.0, 0.0, 1023.0);
    float z = clamp(p.z * 1024.0, 0.0, 1023.0);
    return expand_bits((uint)x) * 4u + expand_bits((uint)y) * 2u + expand_bits((uint)z);
}
[numthreads(256, 1, 1)]
void main(uint3 gtid : SV_GroupThreadID) {
    uint tid = gtid.x;
    uint n = (uint)misc.x;
    for (uint i = tid; i < P2; i += 256u) {
        if (i < n) {
            float3 mn = asfloat(items.Load3(i * 32u + 0u));
            float3 mx = asfloat(items.Load3(i * 32u + 16u));
            float3 c = 0.5 * (mn + mx);
            skey[i] = morton3D((c - origin.xyz) * inv_size.xyz);
            sval[i] = i;
        } else { skey[i] = 0xffffffffu; sval[i] = 0xffffffffu; }
    }
    GroupMemoryBarrierWithGroupSync();
    for (uint k = 2u; k <= P2; k <<= 1u) {
        for (uint j = k >> 1u; j > 0u; j >>= 1u) {
            for (uint i = tid; i < P2; i += 256u) {
                uint ixj = i ^ j;
                if (ixj > i) {
                    bool asc = ((i & k) == 0u);
                    if ((skey[i] > skey[ixj]) == asc) {
                        uint tk = skey[i]; skey[i] = skey[ixj]; skey[ixj] = tk;
                        uint tv = sval[i]; sval[i] = sval[ixj]; sval[ixj] = tv;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
    }
    for (uint i = tid; i < P2; i += 256u)
        if (i < n) kv.Store2(i * 8u, uint2(skey[i], sval[i]));
}
)";

static const char* HLSL_BUILD = R"(
cbuffer Params : register(b0) { float4 origin; float4 inv_size; int4 misc; };  // misc.x = n
RWByteAddressBuffer items : register(u1);
RWByteAddressBuffer kv    : register(u2);
RWByteAddressBuffer links : register(u3);
RWByteAddressBuffer aabb  : register(u4);

static int g_n;
int delta(int i, int j) {
    if (j < 0 || j >= g_n) return -1;
    uint mi = kv.Load(i * 8u);
    uint mj = kv.Load(j * 8u);
    uint x = mi ^ mj;
    if (x == 0u) return 32 + (31 - (int)firstbithigh((uint)i ^ (uint)j));
    return 31 - (int)firstbithigh(x);
}
[numthreads(256, 1, 1)]
void main(uint3 gtid : SV_GroupThreadID) {
    uint tid = gtid.x;
    g_n = misc.x;
    int n = g_n;
    int numI = n - 1;
    for (int t = (int)tid; t < n; t += 256) {
        int leaf = (n - 1) + t;
        uint item = kv.Load(t * 8u + 4u);
        float3 mn = asfloat(items.Load3(item * 32u + 0u));
        float3 mx = asfloat(items.Load3(item * 32u + 16u));
        aabb.Store3(leaf * 32u + 0u,  asuint(mn));
        aabb.Store3(leaf * 32u + 16u, asuint(mx));
        links.Store(leaf * 16u + 12u, asuint(1));
    }
    DeviceMemoryBarrierWithGroupSync();
    for (int i = (int)tid; i < numI; i += 256) {
        int d = (delta(i, i + 1) - delta(i, i - 1)) >= 0 ? 1 : -1;
        int delta_min = delta(i, i - d);
        int l_max = 2;
        while (delta(i, i + l_max * d) > delta_min) l_max <<= 1;
        int l = 0;
        for (int t = l_max >> 1; t >= 1; t >>= 1)
            if (delta(i, i + (l + t) * d) > delta_min) l += t;
        int j = i + l * d;
        int first = min(i, j), last = max(i, j);
        int cprefix = delta(first, last);
        int split = first, step = last - first;
        do { step = (step + 1) >> 1; int ns = split + step; if (ns < last && delta(first, ns) > cprefix) split = ns; } while (step > 1);
        int childA = (split == first)    ? ((n - 1) + split)     : split;
        int childB = (split + 1 == last) ? ((n - 1) + split + 1) : (split + 1);
        links.Store(i * 16u + 0u,  asuint(childA));
        links.Store(i * 16u + 4u,  asuint(childB));
        links.Store(i * 16u + 12u, asuint(0));
        links.Store(childA * 16u + 8u, asuint(i));
        links.Store(childB * 16u + 8u, asuint(i));
        if (i == 0) links.Store(8u, asuint(-1));
    }
    DeviceMemoryBarrierWithGroupSync();
    for (int p = 0; p < 64; ++p) {
        for (int i = (int)tid; i < numI; i += 256) {
            if (asint(links.Load(i * 16u + 12u)) == 0) {
                int L = asint(links.Load(i * 16u + 0u));
                int R = asint(links.Load(i * 16u + 4u));
                if (asint(links.Load(L * 16u + 12u)) == 1 && asint(links.Load(R * 16u + 12u)) == 1) {
                    float3 lmn = asfloat(aabb.Load3(L * 32u + 0u)), lmx = asfloat(aabb.Load3(L * 32u + 16u));
                    float3 rmn = asfloat(aabb.Load3(R * 32u + 0u)), rmx = asfloat(aabb.Load3(R * 32u + 16u));
                    aabb.Store3(i * 32u + 0u,  asuint(min(lmn, rmn)));
                    aabb.Store3(i * 32u + 16u, asuint(max(lmx, rmx)));
                    links.Store(i * 16u + 12u, asuint(2));
                }
            }
        }
        DeviceMemoryBarrierWithGroupSync();
        for (int i2 = (int)tid; i2 < numI; i2 += 256)
            if (asint(links.Load(i2 * 16u + 12u)) == 2) links.Store(i2 * 16u + 12u, asuint(1));
        DeviceMemoryBarrierWithGroupSync();
    }
}
)";

static const char* HLSL_TRAVERSE = R"(
cbuffer Params : register(b0) { float4 origin; float4 inv_size; int4 misc; };  // misc.x = n
RWByteAddressBuffer items   : register(u1);
RWByteAddressBuffer kv      : register(u2);
RWByteAddressBuffer links   : register(u3);
RWByteAddressBuffer aabb    : register(u4);
RWByteAddressBuffer overlap : register(u5);

bool ov(float3 amn, float3 amx, float3 bmn, float3 bmx) {
    return amn.x <= bmx.x && amx.x >= bmn.x &&
           amn.y <= bmx.y && amx.y >= bmn.y &&
           amn.z <= bmx.z && amx.z >= bmn.z;
}
[numthreads(64, 1, 1)]
void main(uint3 gid : SV_DispatchThreadID) {
    uint i = gid.x;
    int n = misc.x;
    if ((int)i >= n) return;
    float3 mn = asfloat(items.Load3(i * 32u + 0u));
    float3 mx = asfloat(items.Load3(i * 32u + 16u));
    if (n == 1) { overlap.Store(i * 4u, 0u); return; }
    int stack[64]; int sp = 0; stack[sp++] = 0;
    uint count = 0u;
    while (sp > 0) {
        int node = stack[--sp];
        float3 nmn = asfloat(aabb.Load3(node * 32u + 0u));
        float3 nmx = asfloat(aabb.Load3(node * 32u + 16u));
        if (!ov(mn, mx, nmn, nmx)) continue;
        if (node >= n - 1) {
            uint j = kv.Load((uint)(node - (n - 1)) * 8u + 4u);
            if (j != i) count++;
        } else if (sp + 2 <= 64) {
            stack[sp++] = asint(links.Load((uint)node * 16u + 0u));
            stack[sp++] = asint(links.Load((uint)node * 16u + 4u));
        }
    }
    overlap.Store(i * 4u, count);
}
)";

extern ShaderHandle make_compute(GraphicDevice* dev, const char* hlsl);

//=============================================================================
// GpuLbvh
//=============================================================================
bool GpuLbvh::init(GraphicDevice* device) {
    shutdown();
    device_ = device;
    backend_ = device->get_backend();

    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps);
    device->get_capabilities(&caps);
    if (!caps.compute_shaders) return false;
    if (caps.max_compute_group_size_x > 0 && caps.max_compute_group_size_x < 256) return false;

    sort_cs_     = make_compute(device, HLSL_SORT);
    build_cs_    = make_compute(device, HLSL_BUILD);
    traverse_cs_ = make_compute(device, HLSL_TRAVERSE);
    if (!sort_cs_.valid() || !build_cs_.valid() || !traverse_cs_.valid()) { shutdown(); return false; }

    if (uses_descriptor_sets()) {
        DescriptorSetLayoutDesc dl; dl.binding_count = 6;
        dl.bindings[0] = { 0, BindingType::UniformBuffer, 1, STAGE_COMPUTE };
        for (int i = 1; i < 6; ++i) dl.bindings[i] = { (uint32_t)i, BindingType::StorageBuffer, 1, STAGE_COMPUTE };
        set_layout_ = device->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = set_layout_;
        pipe_layout_ = device->create_pipeline_layout(pl);
    }

    auto mk = [&](ShaderHandle cs, const char* name) {
        PipelineDesc pd; pd.compute_shader = cs; pd.layout = pipe_layout_; pd.debug_name = name;
        return device->create_pipeline(pd);
    };
    sort_pipe_     = mk(sort_cs_,     "lbvh.sort");
    build_pipe_    = mk(build_cs_,    "lbvh.build");
    traverse_pipe_ = mk(traverse_cs_, "lbvh.traverse");
    if (!valid()) { shutdown(); return false; }
    return true;
}

void GpuLbvh::shutdown() {
    if (device_) {
        items_buf_.destroy(); kv_buf_.destroy(); links_buf_.destroy();
        aabb_buf_.destroy(); overlap_buf_.destroy(); params_buf_.destroy();
        if (desc_set_.valid())     device_->destroy_descriptor_set(desc_set_);
        if (sort_pipe_.valid())    device_->destroy_pipeline(sort_pipe_);
        if (build_pipe_.valid())   device_->destroy_pipeline(build_pipe_);
        if (traverse_pipe_.valid())device_->destroy_pipeline(traverse_pipe_);
        if (sort_cs_.valid())      device_->destroy_shader(sort_cs_);
        if (build_cs_.valid())     device_->destroy_shader(build_cs_);
        if (traverse_cs_.valid())  device_->destroy_shader(traverse_cs_);
        if (pipe_layout_.valid())  device_->destroy_pipeline_layout(pipe_layout_);
        if (set_layout_.valid())   device_->destroy_descriptor_set_layout(set_layout_);
    }
    desc_set_ = {}; sort_pipe_ = {}; build_pipe_ = {}; traverse_pipe_ = {};
    sort_cs_ = {}; build_cs_ = {}; traverse_cs_ = {}; pipe_layout_ = {}; set_layout_ = {};
    item_count_ = node_count_ = 0;
    device_ = nullptr;
}

bool GpuLbvh::set_scene(const SceneItem* items, int count) {
    if (!device_ || count < 0 || count > MAX_ITEMS) return false;
    item_count_ = count;
    node_count_ = std::max(1, 2 * count - 1);

    // Scene bounds (for Morton quantisation) + item AABBs as 2 vec4/item.
    math::AABB scene = count > 0 ? items[0].bounds : math::AABB(math::Vec3(0,0,0), math::Vec3(1,1,1));
    std::vector<float> idata((size_t)std::max(1, count) * 8, 0.0f);
    for (int i = 0; i < count; ++i) {
        scene = scene.merged(items[i].bounds);
        const math::AABB& b = items[i].bounds;
        float* d = &idata[(size_t)i * 8];
        d[0] = b.min_pt.x; d[1] = b.min_pt.y; d[2] = b.min_pt.z;
        d[4] = b.max_pt.x; d[5] = b.max_pt.y; d[6] = b.max_pt.z;
    }
    math::Vec3 size = scene.size();

    bool ok = true;
    ok &= items_buf_.create(device_, (uint32_t)std::max(1, count) * 32u, 0, ResourceUsage::Default, idata.data(), "lbvh.items");
    ok &= kv_buf_.create(device_, (uint32_t)std::max(1, count) * 8u, 0, ResourceUsage::Default, nullptr, "lbvh.kv");
    ok &= links_buf_.create(device_, (uint32_t)node_count_ * 16u, 0, ResourceUsage::Default, nullptr, "lbvh.links");
    ok &= aabb_buf_.create(device_, (uint32_t)node_count_ * 32u, 0, ResourceUsage::Default, nullptr, "lbvh.aabb");
    ok &= overlap_buf_.create(device_, (uint32_t)std::max(1, count) * 4u, 0, ResourceUsage::Default, nullptr, "lbvh.overlap");
    ok &= params_buf_.create(device_, sizeof(LbvhUBO), ResourceUsage::Default, nullptr, "lbvh.params");
    if (!ok) return false;

    LbvhUBO ubo{};
    ubo.origin[0] = scene.min_pt.x; ubo.origin[1] = scene.min_pt.y; ubo.origin[2] = scene.min_pt.z;
    ubo.inv_size[0] = 1.0f / std::max(size.x, 1e-5f);
    ubo.inv_size[1] = 1.0f / std::max(size.y, 1e-5f);
    ubo.inv_size[2] = 1.0f / std::max(size.z, 1e-5f);
    ubo.misc[0] = count;
    std::memcpy(cpu_ubo_, &ubo, sizeof ubo);
    params_buf_.update(cpu_ubo_, sizeof(LbvhUBO));

    if (uses_descriptor_sets()) create_descriptor_set();
    return true;
}

void GpuLbvh::create_descriptor_set() {
    if (desc_set_.valid()) { device_->destroy_descriptor_set(desc_set_); desc_set_ = {}; }
    auto w = [](uint32_t b, BindingType t, BufferHandle h, uint32_t sz) {
        DescriptorWrite dw{}; dw.binding = b; dw.type = t; dw.buffer = h; dw.buffer_size = sz; return dw;
    };
    DescriptorSetDesc sd; sd.layout = set_layout_; sd.write_count = 6;
    sd.writes[0] = w(0, BindingType::UniformBuffer, params_buf_.handle(),  params_buf_.size());
    sd.writes[1] = w(1, BindingType::StorageBuffer, items_buf_.handle(),   items_buf_.size());
    sd.writes[2] = w(2, BindingType::StorageBuffer, kv_buf_.handle(),      kv_buf_.size());
    sd.writes[3] = w(3, BindingType::StorageBuffer, links_buf_.handle(),   links_buf_.size());
    sd.writes[4] = w(4, BindingType::StorageBuffer, aabb_buf_.handle(),    aabb_buf_.size());
    sd.writes[5] = w(5, BindingType::StorageBuffer, overlap_buf_.handle(), overlap_buf_.size());
    desc_set_ = device_->create_descriptor_set(sd);
}

void GpuLbvh::bind_slots(GraphicCommander* cmd) const {
    cmd->bind_uniform_buffer(0, params_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(1, items_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(2, kv_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(3, links_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(4, aabb_buf_.handle(), 0, 0);
    cmd->bind_storage_buffer(5, overlap_buf_.handle(), 0, 0);
}

void GpuLbvh::record(GraphicCommander* cmd) {
    if (!valid() || item_count_ <= 0) return;
    const bool desc = uses_descriptor_sets();
    auto bind = [&] { if (desc) cmd->bind_descriptor_set(0, desc_set_); else bind_slots(cmd); };

    cmd->set_pipeline(sort_pipe_);     bind(); cmd->dispatch(1, 1, 1);
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
    cmd->set_pipeline(build_pipe_);    bind(); cmd->dispatch(1, 1, 1);
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
    cmd->set_pipeline(traverse_pipe_); bind(); cmd->dispatch((uint32_t)((item_count_ + 63) / 64));
    cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
}

void GpuLbvh::read_overlap_counts(std::vector<uint32_t>& out) {
    out.assign((size_t)std::max(0, item_count_), 0u);
    if (item_count_ > 0 && overlap_buf_.valid()) overlap_buf_.read(out.data(), (uint32_t)item_count_ * 4u);
}

void GpuLbvh::read_sorted_codes(std::vector<uint32_t>& codes, std::vector<uint32_t>& items) {
    codes.assign((size_t)std::max(0, item_count_), 0u);
    items.assign((size_t)std::max(0, item_count_), 0u);
    if (item_count_ <= 0 || !kv_buf_.valid()) return;
    std::vector<uint32_t> kv((size_t)item_count_ * 2, 0u);
    kv_buf_.read(kv.data(), (uint32_t)item_count_ * 8u);
    for (int i = 0; i < item_count_; ++i) { codes[i] = kv[i * 2]; items[i] = kv[i * 2 + 1]; }
}

void GpuLbvh::read_root_bounds(math::Vec3* out_min, math::Vec3* out_max) {
    float v[8] = {};
    if (node_count_ > 0 && aabb_buf_.valid()) aabb_buf_.read(v, 32u, 0u);   // node 0 = root
    if (out_min) *out_min = math::Vec3(v[0], v[1], v[2]);
    if (out_max) *out_max = math::Vec3(v[4], v[5], v[6]);
}

} // namespace gfx
} // namespace window
