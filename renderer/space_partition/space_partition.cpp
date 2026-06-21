// renderer/space_partition/space_partition.cpp
//
// GridParams cell math + Frustum plane extraction shared by the CPU and GPU grids.

#include "space_partition.hpp"

#include <algorithm>
#include <cmath>

namespace window {
namespace gfx {

using math::Vec3;
using math::Vec4;
using math::AABB;
using math::Mat4;

//=============================================================================
// GridParams
//=============================================================================

void GridParams::cell_coord(const Vec3& p, int* cx, int* cy, int* cz) const {
    int x = (int)std::floor((p.x - origin.x) / cell_size.x);
    int y = (int)std::floor((p.y - origin.y) / cell_size.y);
    int z = (int)std::floor((p.z - origin.z) / cell_size.z);
    *cx = std::clamp(x, 0, dim_x - 1);
    *cy = std::clamp(y, 0, dim_y - 1);
    *cz = std::clamp(z, 0, dim_z - 1);
}

int GridParams::cell_of_point(const Vec3& p) const {
    int cx, cy, cz;
    cell_coord(p, &cx, &cy, &cz);
    return linear(cx, cy, cz);
}

AABB GridParams::cell_bounds(int cx, int cy, int cz) const {
    Vec3 mn(origin.x + cx * cell_size.x,
            origin.y + cy * cell_size.y,
            origin.z + cz * cell_size.z);
    return AABB(mn, mn + cell_size);
}

GridParams GridParams::fit(const AABB& world, int cells_per_axis) {
    int n = std::max(1, cells_per_axis);
    GridParams g;
    g.origin = world.min_pt;
    Vec3 size = world.size();
    // Guard against degenerate (flat) axes so cell_size stays strictly positive.
    g.cell_size = Vec3(std::max(size.x, 1e-4f) / n,
                       std::max(size.y, 1e-4f) / n,
                       std::max(size.z, 1e-4f) / n);
    g.dim_x = g.dim_y = g.dim_z = n;
    return g;
}

GridParams GridParams::fit(const SceneItem* items, int count, int cells_per_axis, float pad) {
    if (count <= 0) return fit(AABB(Vec3(0, 0, 0), Vec3(1, 1, 1)), cells_per_axis);
    AABB world = items[0].bounds;
    for (int i = 1; i < count; ++i) world = world.merged(items[i].bounds);
    if (pad != 0.0f) world = world.expanded(pad);
    return fit(world, cells_per_axis);
}

//=============================================================================
// Frustum
//=============================================================================

static Vec4 normalize_plane(const Vec4& p) {
    float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len < math::EPSILON) return p;
    return Vec4(p.x / len, p.y / len, p.z / len, p.w / len);
}

Frustum Frustum::from_view_proj(const float vp[16]) {
    // Column-major storage: element (row r, col c) is vp[c*4 + r]. Gribb–Hartmann combines
    // matrix rows; rows are r0..r3 below. Near/far use the [-1,1] clip-Z convention that
    // math::Mat4::perspective() / ortho() produce (the matrix is built on the CPU and the
    // same planes feed both CPU and GPU, so the answer is identical either way).
    auto m = [&](int r, int c) { return vp[c * 4 + r]; };
    Vec4 r0(m(0, 0), m(0, 1), m(0, 2), m(0, 3));
    Vec4 r1(m(1, 0), m(1, 1), m(1, 2), m(1, 3));
    Vec4 r2(m(2, 0), m(2, 1), m(2, 2), m(2, 3));
    Vec4 r3(m(3, 0), m(3, 1), m(3, 2), m(3, 3));

    Frustum f;
    f.planes[0] = normalize_plane(r3 + r0);  // Left
    f.planes[1] = normalize_plane(r3 - r0);  // Right
    f.planes[2] = normalize_plane(r3 + r1);  // Bottom
    f.planes[3] = normalize_plane(r3 - r1);  // Top
    f.planes[4] = normalize_plane(r3 + r2);  // Near
    f.planes[5] = normalize_plane(r3 - r2);  // Far
    return f;
}

bool Frustum::contains_point(const Vec3& p) const {
    for (const Vec4& pl : planes)
        if (pl.x * p.x + pl.y * p.y + pl.z * p.z + pl.w < 0.0f) return false;
    return true;
}

bool Frustum::intersects(const AABB& box) const {
    for (const Vec4& pl : planes) {
        // The corner farthest along the plane normal — if even it is outside, the whole box is.
        Vec3 pv((pl.x >= 0.0f) ? box.max_pt.x : box.min_pt.x,
                (pl.y >= 0.0f) ? box.max_pt.y : box.min_pt.y,
                (pl.z >= 0.0f) ? box.max_pt.z : box.min_pt.z);
        if (pl.x * pv.x + pl.y * pv.y + pl.z * pv.z + pl.w < 0.0f) return false;
    }
    return true;
}

//=============================================================================
// PartitionLeaves
//=============================================================================

int PartitionLeaves::max_leaf_size() const {
    uint32_t mx = 0;
    for (uint32_t c : count) mx = std::max(mx, c);
    return (int)mx;
}

} // namespace gfx
} // namespace window
