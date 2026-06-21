// example_space_partition — the scene-partition family (uniform grid, BVH, octree, quadtree,
// BSP), CPU + GPU, verified cross-API.
//
// One scene of axis-aligned boxes is partitioned five ways. The CPU structures are checked
// against a brute-force frustum cull; then, for each desktop backend, the GPU versions reproduce
// the same answers on the device and are read back and compared:
//   * Uniform grid — built AND culled on the GPU (compute: atomic bucket insert + per-cell cull).
//     We verify the GPU's per-item cell assignment matches the CPU grid, and the GPU visible set
//     matches the CPU/brute-force visible set.
//   * BVH / Octree / Quadtree / BSP — built on the CPU, flattened to leaves, and frustum-culled on
//     the GPU by the shared leaf-cull compute kernel. The GPU visible set must equal the CPU one.
//
// Run with no args for all backends, or pass a name: example_space_partition d3d11

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/space_partition/space_partition.hpp"
#include "../renderer/space_partition/uniform_grid.hpp"
#include "../renderer/space_partition/bvh.hpp"
#include "../renderer/space_partition/octree.hpp"
#include "../renderer/space_partition/quadtree.hpp"
#include "../renderer/space_partition/bsp.hpp"
#include "../renderer/space_partition/gpu_uniform_grid.hpp"
#include "../renderer/space_partition/gpu_leaf_cull.hpp"
#include "../renderer/space_partition/gpu_lbvh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;
using window::math::Vec3;
using window::math::AABB;
using window::math::Mat4;

static int g_pass = 0, g_fail = 0, g_skip = 0;
static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("    [%s] %-30s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-30s %s\n", name, detail.c_str());
    ++g_skip;
}

static std::vector<uint32_t> sorted(std::vector<uint32_t> v) { std::sort(v.begin(), v.end()); return v; }
static bool same_set(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    return sorted(a) == sorted(b);
}

// ---- Deterministic scene ----------------------------------------------------
struct Rng { uint32_t s; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
             float f(float lo, float hi) { return lo + (hi - lo) * (next() / 4294967296.0f); } };

static std::vector<SceneItem> make_scene(int n, float extent, uint32_t seed) {
    Rng rng{ seed };
    std::vector<SceneItem> items(n);
    for (int i = 0; i < n; ++i) {
        Vec3 c(rng.f(-extent, extent), rng.f(-extent, extent), rng.f(-extent, extent));
        float h = rng.f(0.05f, 0.4f);
        items[i] = SceneItem((uint32_t)i, AABB(c - Vec3(h), c + Vec3(h)));
    }
    return items;
}

// CPU reference results computed once (backend-independent).
struct Reference {
    std::vector<SceneItem> items;
    GridParams             gp;
    Frustum                frustum;
    AABB                   region{ Vec3(-3,-3,-3), Vec3(3,3,3) };
    std::vector<uint32_t>  brute_visible;
    std::vector<uint32_t>  grid_vis, bvh_vis, oct_vis, quad_vis, bsp_vis;
    PartitionLeaves        bvh_leaves, oct_leaves, quad_leaves, bsp_leaves;
    AABB                   scene_bounds;
    std::vector<uint32_t>  overlap_counts;   // brute-force broad-phase: neighbours per item
};

static Reference build_reference() {
    Reference r;
    r.items = make_scene(400, 10.0f, 0x1234567u);
    const int n = (int)r.items.size();
    r.gp = GridParams::fit(r.items.data(), n, 8, 1.0f);

    Mat4 view = Mat4::look_at(Vec3(0, 0, 14), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspective(window::math::degrees_to_radians(60.0f), 1.0f, 0.5f, 40.0f);
    r.frustum = Frustum::from_view_proj(proj * view);

    for (int i = 0; i < n; ++i)
        if (r.frustum.contains_point(r.items[i].center())) r.brute_visible.push_back((uint32_t)i);

    // Brute-force broad-phase oracle: scene bounds + per-item AABB-overlap neighbour counts.
    r.scene_bounds = r.items[0].bounds;
    for (int i = 1; i < n; ++i) r.scene_bounds = r.scene_bounds.merged(r.items[i].bounds);
    r.overlap_counts.assign(n, 0u);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (j != i && r.items[i].bounds.intersects(r.items[j].bounds)) ++r.overlap_counts[i];

    UniformGrid grid; grid.build(r.items.data(), n, r.gp); grid.frustum_cull(r.frustum, r.grid_vis);
    BVH bvh;      bvh.build(r.items.data(), n);   bvh.frustum_cull(r.frustum, r.bvh_vis);   bvh.extract_leaves(r.bvh_leaves);
    Octree oct;   oct.build(r.items.data(), n);   oct.frustum_cull(r.frustum, r.oct_vis);   oct.extract_leaves(r.oct_leaves);
    Quadtree quad;quad.build(r.items.data(), n);  quad.frustum_cull(r.frustum, r.quad_vis); quad.extract_leaves(r.quad_leaves);
    BSP bsp;      bsp.build(r.items.data(), n);   bsp.frustum_cull(r.frustum, r.bsp_vis);   bsp.extract_leaves(r.bsp_leaves);
    return r;
}

static void report_cpu(const Reference& r) {
    std::printf("\n=== CPU partition build (400 items, %zu visible to the camera) ===\n",
                r.brute_visible.size());
    BVH bvh; bvh.build(r.items.data(), (int)r.items.size());
    Octree oct; oct.build(r.items.data(), (int)r.items.size());
    Quadtree quad; quad.build(r.items.data(), (int)r.items.size());
    BSP bsp; bsp.build(r.items.data(), (int)r.items.size());
    std::printf("    grid  : %d cells (%dx%dx%d)\n", r.gp.cell_count(), r.gp.dim_x, r.gp.dim_y, r.gp.dim_z);
    std::printf("    bvh   : %d nodes, %d leaves, depth %d\n", bvh.node_count(), bvh.leaf_count(), bvh.max_depth_reached());
    std::printf("    octree: %d nodes, %d leaves, depth %d\n", oct.node_count(), oct.leaf_count(), oct.max_depth_reached());
    std::printf("    quad  : %d nodes, %d leaves, depth %d\n", quad.node_count(), quad.leaf_count(), quad.max_depth_reached());
    std::printf("    bsp   : %d nodes, %d leaves, depth %d\n", bsp.node_count(), bsp.leaf_count(), bsp.max_depth_reached());

    // Each CPU structure must agree with the brute-force visible set.
    check("cpu.grid",     same_set(r.grid_vis, r.brute_visible));
    check("cpu.bvh",      same_set(r.bvh_vis,  r.brute_visible));
    check("cpu.octree",   same_set(r.oct_vis,  r.brute_visible));
    check("cpu.quadtree", same_set(r.quad_vis, r.brute_visible));
    check("cpu.bsp",      same_set(r.bsp_vis,  r.brute_visible));
}

// ---- GPU verification per backend -------------------------------------------
static void run_gpu_leaf(GraphicDevice* dev, Graphics* gfx, GraphicCommander* cmd,
                         const Reference& r, const char* tag,
                         const PartitionLeaves& leaves, const std::vector<uint32_t>& cpu_vis) {
    GpuLeafCuller lc;
    if (!lc.init(dev)) { skip(tag, "leaf-cull init failed"); return; }
    if (!lc.set_data(r.items.data(), (int)r.items.size(), leaves)) { skip(tag, "set_data failed"); return; }
    lc.set_frustum(r.frustum);
    lc.reset_counters();
    cmd->begin();
    lc.record(cmd);
    cmd->end();
    submit_commander(gfx, cmd);
    dev->wait_idle();

    std::vector<uint32_t> gpu_vis;
    int counted = lc.read_visible(gpu_vis);
    bool ok = counted == (int)cpu_vis.size() && same_set(gpu_vis, cpu_vis);
    check(tag, ok, "GPU " + std::to_string(counted) + " vs CPU " + std::to_string(cpu_vis.size()) + " visible");
}

static bool run_backend(Backend backend, const char* name, const Reference& r) {
    std::printf("\n=== Backend: %s ===\n", name);
    const int n = (int)r.items.size();

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_space_partition";
    config.windows[0].width = 64; config.windows[0].height = 64;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) {
        skip("backend", std::string("window/context create failed: ") + result_to_string(wr));
        return false;
    }
    Graphics* gfx = windows[0]->graphics();
    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { skip("backend", std::string("no RHI GraphicDevice: ") + result_to_string(dr)); windows[0]->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, nullptr);
    if (!cmd) { skip("backend", "no commander"); destroy_device(dev); windows[0]->destroy(); return false; }

    // ---- GPU uniform grid: build + cull on the device ----
    {
        GpuUniformGrid::Desc gd; gd.bucket_capacity = 64;
        GpuUniformGrid g;
        if (!g.init(dev, gd)) {
            skip("grid.gpu", "compute unsupported on this backend");
        } else {
            g.set_scene(r.items.data(), n, r.gp);
            g.set_frustum(r.frustum);
            g.reset_counters();
            cmd->begin();
            g.record(cmd);
            cmd->end();
            submit_commander(gfx, cmd);
            dev->wait_idle();

            // (a) The GPU's per-item cell assignment must match the CPU grid exactly.
            std::vector<uint32_t> counts, buckets;
            int max_cell = g.read_cell_counts(counts);
            g.read_buckets(buckets);
            std::vector<int> item_cell((size_t)n, -1);
            int cap = g.bucket_capacity();
            for (int c = 0; c < g.cell_count(); ++c) {
                uint32_t cnt = counts[c]; if (cnt > (uint32_t)cap) cnt = (uint32_t)cap;
                for (uint32_t s = 0; s < cnt; ++s) {
                    uint32_t item = buckets[(size_t)c * cap + s];
                    if (item < (uint32_t)n) item_cell[item] = c;
                }
            }
            bool cells_ok = max_cell <= cap;   // no bucket overflow
            int assigned = 0;
            for (int i = 0; i < n; ++i) {
                if (item_cell[i] < 0) { cells_ok = false; continue; }
                ++assigned;
                if (item_cell[i] != r.gp.cell_of_point(r.items[i].center())) cells_ok = false;
            }
            check("grid.gpu.build", cells_ok && assigned == n,
                  "max cell=" + std::to_string(max_cell) + "/cap " + std::to_string(cap) +
                  ", " + std::to_string(assigned) + "/" + std::to_string(n) + " items bucketed");

            // (b) The GPU frustum-cull visible set must match the CPU/brute-force set.
            std::vector<uint32_t> gpu_vis;
            int counted = g.read_visible(gpu_vis);
            check("grid.gpu.cull", counted == (int)r.grid_vis.size() && same_set(gpu_vis, r.grid_vis),
                  "GPU " + std::to_string(counted) + " vs CPU " + std::to_string(r.grid_vis.size()) + " visible");
        }
    }

    // ---- GPU leaf-cull for the four tree structures (CPU-built, GPU-culled) ----
    run_gpu_leaf(dev, gfx, cmd, r, "bvh.gpu.cull",      r.bvh_leaves,  r.bvh_vis);
    run_gpu_leaf(dev, gfx, cmd, r, "octree.gpu.cull",   r.oct_leaves,  r.oct_vis);
    run_gpu_leaf(dev, gfx, cmd, r, "quadtree.gpu.cull", r.quad_leaves, r.quad_vis);
    run_gpu_leaf(dev, gfx, cmd, r, "bsp.gpu.cull",      r.bsp_leaves,  r.bsp_vis);

    // ---- GPU LBVH: Morton hierarchical ids + radix-tree build + hierarchical broad-phase, all on GPU ----
    {
        GpuLbvh lbvh;
        if (!lbvh.init(dev)) {
            skip("lbvh.gpu", "compute unsupported on this backend");
        } else if (!lbvh.set_scene(r.items.data(), n)) {
            skip("lbvh.gpu", "scene exceeds GpuLbvh::MAX_ITEMS");
        } else {
            cmd->begin();
            lbvh.record(cmd);
            cmd->end();
            submit_commander(gfx, cmd);
            dev->wait_idle();

            // (a) Hierarchical ids: sorted Morton codes non-decreasing, payload a permutation.
            std::vector<uint32_t> codes, ids;
            lbvh.read_sorted_codes(codes, ids);
            bool sort_ok = true;
            for (int i = 1; i < n; ++i) if (codes[i] < codes[i - 1]) sort_ok = false;
            std::vector<char> seen((size_t)n, 0); int distinct = 0;
            for (uint32_t id : ids) { if (id >= (uint32_t)n || seen[id]) { sort_ok = false; } else { seen[id] = 1; ++distinct; } }
            check("lbvh.gpu.sort", sort_ok && distinct == n, "Morton ids sorted + payload is a permutation");

            // (b) Radix-tree build: root AABB must equal the scene bounds.
            Vec3 rmn, rmx; lbvh.read_root_bounds(&rmn, &rmx);
            auto close = [](const Vec3& a, const Vec3& b) {
                return std::fabs(a.x-b.x) < 1e-2f && std::fabs(a.y-b.y) < 1e-2f && std::fabs(a.z-b.z) < 1e-2f;
            };
            check("lbvh.gpu.build", close(rmn, r.scene_bounds.min_pt) && close(rmx, r.scene_bounds.max_pt),
                  "root AABB == scene bounds (tree built via Morton-prefix comparison)");

            // (c) Hierarchical broad-phase: per-item neighbour counts must match brute force.
            std::vector<uint32_t> counts; lbvh.read_overlap_counts(counts);
            bool bp_ok = counts == r.overlap_counts;
            long total = 0; for (uint32_t c : r.overlap_counts) total += c;
            check("lbvh.gpu.broadphase", bp_ok,
                  "neighbour counts vs brute force (" + std::to_string(total / 2) + " overlapping pairs)");
        }
    }

    destroy_commander(cmd);
    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("scene-partition demo (CPU + GPU, cross-API)\n");
    Reference r = build_reference();
    report_cpu(r);

    struct { Backend b; const char* n; } backends[] = {
        { Backend::OpenGL, "OpenGL" },
        { Backend::Vulkan, "Vulkan" },
        { Backend::D3D11,  "Direct3D 11" },
        { Backend::D3D12,  "Direct3D 12" },
    };
    const char* only = (argc > 1) ? argv[1] : nullptr;
    for (auto& e : backends) {
        if (only) {
            std::string a = only, b = e.n;
            for (auto& ch : a) ch = (char)tolower(ch);
            for (auto& ch : b) ch = (char)tolower(ch);
            if (b.find(a) == std::string::npos) continue;
        }
        if (!is_backend_supported(e.b)) { std::printf("\n=== Backend: %s ===\n    [SKIP] not supported\n", e.n); ++g_skip; continue; }
        run_backend(e.b, e.n, r);
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
