// test_space_partition — headless CPU unit tests for the scene-partition family.
//
// Each structure (uniform grid, BVH, octree, quadtree, BSP) is checked against a brute-force
// oracle on the same scene: frustum culling and region queries must return EXACTLY the items a
// linear scan would (centre-in-frustum / centre-in-box), and the grid's broad-phase pairs must
// match a same-cell brute force. No display or GPU needed.

#include "../renderer/space_partition/space_partition.hpp"
#include "../renderer/space_partition/uniform_grid.hpp"
#include "../renderer/space_partition/bvh.hpp"
#include "../renderer/space_partition/octree.hpp"
#include "../renderer/space_partition/quadtree.hpp"
#include "../renderer/space_partition/bsp.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;
using window::math::Vec3;
using window::math::Vec4;
using window::math::AABB;
using window::math::Mat4;

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("  [%s] %-34s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}

// ---- Deterministic scene ----------------------------------------------------
// A tiny xorshift so the scene is identical on every run / platform.
struct Rng { uint32_t s; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
             float f(float lo, float hi) { return lo + (hi - lo) * (next() / 4294967296.0f); } };

static std::vector<SceneItem> make_scene(int n, float extent, uint32_t seed) {
    Rng rng{ seed };
    std::vector<SceneItem> items(n);
    for (int i = 0; i < n; ++i) {
        Vec3 c(rng.f(-extent, extent), rng.f(-extent, extent), rng.f(-extent, extent));
        float h = rng.f(0.05f, 0.4f);   // small half-extent so items are nearly points vs cells
        items[i] = SceneItem((uint32_t)i, AABB(c - Vec3(h), c + Vec3(h)));
    }
    return items;
}

static std::vector<uint32_t> sorted(std::vector<uint32_t> v) { std::sort(v.begin(), v.end()); return v; }

// Brute-force oracles (centre semantics).
static std::vector<uint32_t> brute_frustum(const std::vector<SceneItem>& it, const Frustum& f) {
    std::vector<uint32_t> r;
    for (size_t i = 0; i < it.size(); ++i) if (f.contains_point(it[i].center())) r.push_back((uint32_t)i);
    return r;
}
static std::vector<uint32_t> brute_region(const std::vector<SceneItem>& it, const AABB& box) {
    std::vector<uint32_t> r;
    for (size_t i = 0; i < it.size(); ++i) if (box.contains(it[i].center())) r.push_back((uint32_t)i);
    return r;
}

// ---- Per-structure verification ---------------------------------------------
template <class Build, class Cull, class Region>
static void verify(const char* tag, const std::vector<SceneItem>& items,
                   const Frustum& frustum, const AABB& region,
                   Build build, Cull cull, Region region_q) {
    build();
    std::vector<uint32_t> got, want;
    cull(got);
    want = brute_frustum(items, frustum);
    check((std::string(tag) + ".frustum_cull").c_str(), sorted(got) == sorted(want),
          std::to_string(want.size()) + " visible of " + std::to_string(items.size()));
    got.clear();
    region_q(got);
    want = brute_region(items, region);
    check((std::string(tag) + ".query_region").c_str(), sorted(got) == sorted(want),
          std::to_string(want.size()) + " in region");
}

static void test_all(const std::vector<SceneItem>& items, const Frustum& frustum, const AABB& region) {
    GridParams gp = GridParams::fit(items.data(), (int)items.size(), 8, 1.0f);
    UniformGrid grid; BVH bvh; Octree oct; Quadtree quad; BSP bsp;

    verify("grid", items, frustum, region,
           [&]{ grid.build(items.data(), (int)items.size(), gp); },
           [&](std::vector<uint32_t>& o){ grid.frustum_cull(frustum, o); },
           [&](std::vector<uint32_t>& o){ grid.query_region(region, o); });
    verify("bvh", items, frustum, region,
           [&]{ bvh.build(items.data(), (int)items.size()); },
           [&](std::vector<uint32_t>& o){ bvh.frustum_cull(frustum, o); },
           [&](std::vector<uint32_t>& o){ bvh.query_region(region, o); });
    verify("octree", items, frustum, region,
           [&]{ oct.build(items.data(), (int)items.size()); },
           [&](std::vector<uint32_t>& o){ oct.frustum_cull(frustum, o); },
           [&](std::vector<uint32_t>& o){ oct.query_region(region, o); });
    verify("quadtree", items, frustum, region,
           [&]{ quad.build(items.data(), (int)items.size()); },
           [&](std::vector<uint32_t>& o){ quad.frustum_cull(frustum, o); },
           [&](std::vector<uint32_t>& o){ quad.query_region(region, o); });
    verify("bsp", items, frustum, region,
           [&]{ bsp.build(items.data(), (int)items.size()); },
           [&](std::vector<uint32_t>& o){ bsp.frustum_cull(frustum, o); },
           [&](std::vector<uint32_t>& o){ bsp.query_region(region, o); });

    // Every leaf-based structure must partition the items: each index appears once across leaves.
    auto check_partition = [&](const char* tag, const PartitionLeaves& lv) {
        std::set<uint32_t> seen; bool dup = false; uint32_t total = 0;
        for (int l = 0; l < lv.leaf_count(); ++l)
            for (uint32_t k = 0; k < lv.count[l]; ++k) {
                uint32_t id = lv.item_indices[lv.first[l] + k];
                if (!seen.insert(id).second) dup = true; ++total;
            }
        check((std::string(tag) + ".leaves_partition").c_str(),
              !dup && total == items.size() && seen.size() == items.size(),
              std::to_string(lv.leaf_count()) + " leaves, " + std::to_string(total) + " items");
    };
    PartitionLeaves lv;
    bvh.extract_leaves(lv);  check_partition("bvh", lv);
    oct.extract_leaves(lv);  check_partition("octree", lv);
    quad.extract_leaves(lv); check_partition("quadtree", lv);
    bsp.extract_leaves(lv);  check_partition("bsp", lv);

    // Grid broad-phase: pairs sharing a cell must match a same-cell brute force.
    std::vector<std::pair<uint32_t,uint32_t>> pairs;
    grid.potential_pairs(pairs);
    size_t brute = 0;
    for (size_t i = 0; i < items.size(); ++i)
        for (size_t j = i + 1; j < items.size(); ++j)
            if (gp.cell_of_point(items[i].center()) == gp.cell_of_point(items[j].center())) ++brute;
    check("grid.potential_pairs", pairs.size() == brute,
          std::to_string(pairs.size()) + " pairs (brute " + std::to_string(brute) + ")");
}

int main() {
    std::printf("scene-partition CPU unit tests\n");

    // A perspective camera looking down -Z; some of the scene falls outside its frustum.
    Mat4 view = Mat4::look_at(Vec3(0, 0, 14), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = Mat4::perspective(window::math::degrees_to_radians(60.0f), 1.0f, 0.5f, 40.0f);
    Mat4 vp = proj * view;
    Frustum frustum = Frustum::from_view_proj(vp);
    AABB region(Vec3(-3, -3, -3), Vec3(3, 3, 3));

    std::printf("\n[scene: 400 items, extent 10]\n");
    test_all(make_scene(400, 10.0f, 0x1234567u), frustum, region);

    std::printf("\n[scene: 1 item]\n");
    test_all(make_scene(1, 5.0f, 0xABCDu), frustum, region);

    std::printf("\n[scene: clustered 200 items, extent 2 (all in frustum)]\n");
    test_all(make_scene(200, 2.0f, 0x55AA55u), frustum, region);

    // Empty scene must not crash and must return nothing.
    {
        UniformGrid g; BVH b; Octree o; Quadtree q; BSP s;
        GridParams gp = GridParams::fit(AABB(Vec3(-1,-1,-1), Vec3(1,1,1)), 4);
        g.build(nullptr, 0, gp); b.build(nullptr, 0); o.build(nullptr, 0); q.build(nullptr, 0); s.build(nullptr, 0);
        std::vector<uint32_t> out;
        bool empty = g.frustum_cull(frustum, out) == 0 && b.frustum_cull(frustum, out) == 0 &&
                     o.frustum_cull(frustum, out) == 0 && q.frustum_cull(frustum, out) == 0 &&
                     s.frustum_cull(frustum, out) == 0 && out.empty();
        std::printf("\n[scene: empty]\n");
        check("empty.no_crash", empty, "all structures return 0");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
