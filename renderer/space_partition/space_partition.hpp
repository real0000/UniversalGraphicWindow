#pragma once
// renderer/space_partition/space_partition.hpp
//
// Scene-partitioning primitives shared by the CPU and GPU implementations.
//
// The partitioning subsystem answers the broad-phase questions a renderer asks every
// frame — "which objects are in this region / visible to this camera / close enough to
// collide" — by bucketing a scene's objects into a spatial structure instead of testing
// every object against every query (O(N) per query, O(N^2) for pairs).
//
// The structure here is a UNIFORM GRID (a.k.a. spatial hash): world space is carved into
// equal axis-aligned cells and each object is filed into the cell its CENTRE falls in (a
// "point grid"). This is the partition that has a canonical, embarrassingly-parallel GPU
// build (count / scatter into per-cell buckets with atomics), so the same algorithm runs:
//   * on the CPU — UniformGrid (uniform_grid.hpp): a counting-sort CSR layout, the
//     textbook reference implementation, used standalone or as the GPU oracle, and
//   * on the GPU — GpuUniformGrid (gpu_uniform_grid.hpp): a bucketed grid built by a
//     compute shader, cross-API (OpenGL / Vulkan / D3D11 / D3D12).
//
// Both expose the same queries (region / frustum-cull / broad-phase pairs) over identical
// cell math, so a GPU result can be checked bit-for-bit against the CPU one. Objects are
// classified by centre point, which makes the grid an exact accelerator for point queries:
// a point inside a query region (or frustum) always lands in a cell the query visits, so
// the grid-accelerated answer equals the brute-force answer with no false negatives. (For
// large objects whose extent should register in several cells, insert by AABB overlap — a
// documented extension; the centre grid keeps CPU and GPU trivially comparable.)
//
// Header-declared / cpp-defined, namespace window::gfx, matching the rest of renderer/.
// Depends only on math_util.hpp (no graphics backend) so it is usable headless.

#include "../../math_util.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

//=============================================================================
// SceneItem — one object to be partitioned: a stable id + its world AABB.
//=============================================================================
// The grid keys on bounds.center(); the full AABB is kept so refinement passes
// (e.g. exact overlap after a broad-phase pair) have it to hand.
struct SceneItem {
    uint32_t   id     = 0;
    math::AABB bounds;

    SceneItem() = default;
    SceneItem(uint32_t id_, const math::AABB& b) : id(id_), bounds(b) {}
    math::Vec3 center() const { return bounds.center(); }
};

//=============================================================================
// GridParams — the uniform grid's placement and resolution.
//=============================================================================
// Cell (cx,cy,cz) covers [origin + (cx,cy,cz)*cell_size, origin + (cx+1,..)*cell_size].
// A point is classified by flooring its offset from the origin into cell coordinates and
// clamping to the grid, so points outside the grid fall into the nearest edge cell (both
// the CPU and the GPU use this exact rule, keeping their cell assignments identical).
struct GridParams {
    math::Vec3 origin    = math::Vec3(0, 0, 0);  // min corner of cell (0,0,0)
    math::Vec3 cell_size = math::Vec3(1, 1, 1);  // per-axis cell extent (each > 0)
    int        dim_x     = 1;
    int        dim_y     = 1;
    int        dim_z     = 1;

    int  cell_count() const { return dim_x * dim_y * dim_z; }
    bool valid() const {
        return dim_x > 0 && dim_y > 0 && dim_z > 0 &&
               cell_size.x > 0.0f && cell_size.y > 0.0f && cell_size.z > 0.0f;
    }

    // Linear cell index from integer cell coordinates (z-major, then y, then x).
    int linear(int cx, int cy, int cz) const { return (cz * dim_y + cy) * dim_x + cx; }

    // Integer cell coordinates of a world point (clamped into [0, dim-1]).
    void cell_coord(const math::Vec3& p, int* cx, int* cy, int* cz) const;
    // Linear cell index of a world point (clamped). Always in [0, cell_count()).
    int  cell_of_point(const math::Vec3& p) const;
    // World-space AABB covered by cell (cx,cy,cz).
    math::AABB cell_bounds(int cx, int cy, int cz) const;

    // Build a grid of `cells_per_axis`^3 cells covering `world` (its min corner becomes the
    // origin). `world` must be non-empty on every axis.
    static GridParams fit(const math::AABB& world, int cells_per_axis);
    // Build a grid that encloses every item's AABB, padded outward by `pad` on each side,
    // at `cells_per_axis` cells per axis. Degenerate axes get a unit thickness.
    static GridParams fit(const SceneItem* items, int count, int cells_per_axis, float pad = 0.0f);
};

//=============================================================================
// Frustum — six inward-pointing planes, for visibility culling.
//=============================================================================
// plane = (a,b,c,d); a point p is on the inside half-space iff a*p.x+b*p.y+c*p.z+d >= 0.
// Planes are normalised (|(a,b,c)| == 1) so the value is a signed distance. Extracted from
// a column-major view-projection matrix (Gribb–Hartmann), so the CPU and GPU cull against
// identical planes uploaded in a constant buffer.
struct Frustum {
    math::Vec4 planes[6];  // order: Left, Right, Bottom, Top, Near, Far

    // Build from a 16-float column-major view-projection (== math::Mat4::data()).
    static Frustum from_view_proj(const float view_proj[16]);
    static Frustum from_view_proj(const math::Mat4& view_proj) { return from_view_proj(view_proj.data()); }

    // Is the point inside all six planes?
    bool contains_point(const math::Vec3& p) const;
    // Conservative AABB test: false only when the box is wholly outside some plane.
    bool intersects(const math::AABB& box) const;
};

//=============================================================================
// PartitionLeaves — a flattened set of leaf nodes any partition can produce.
//=============================================================================
// The hierarchy-independent form the GPU leaf-cull (gpu_leaf_cull.hpp) consumes: one tight
// AABB per leaf plus a [first, first+count) slice into a shared array of item indices. The
// BVH and the Octree both emit this (UniformGrid's cells are an implicit version handled by
// its own GPU pass), so one compute kernel culls every tree structure. Leaves partition the
// items: each item index appears in exactly one leaf's slice.
struct PartitionLeaves {
    std::vector<math::AABB> bounds;        // one tight AABB per leaf
    std::vector<uint32_t>   first;         // start of the leaf's items in `item_indices`
    std::vector<uint32_t>   count;         // number of items in the leaf
    std::vector<uint32_t>   item_indices;  // item indices, grouped leaf by leaf

    int  leaf_count()    const { return (int)bounds.size(); }
    int  total_items()   const { return (int)item_indices.size(); }
    int  max_leaf_size() const;            // largest `count` (0 if no leaves)
    void clear() { bounds.clear(); first.clear(); count.clear(); item_indices.clear(); }
    // Append one leaf covering item_indices[start_in_items, start_in_items+n).
    void add_leaf(const math::AABB& b, uint32_t start_in_items, uint32_t n) {
        bounds.push_back(b); first.push_back(start_in_items); count.push_back(n);
    }
};

} // namespace gfx
} // namespace window
