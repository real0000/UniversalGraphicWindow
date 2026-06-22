#pragma once
// renderer/space_partition/quadtree.hpp
//
// Quadtree — a 4-way spatial-subdivision scene partition over the ground (XZ) plane (quadtree.cpp).
//
// The octree's flat cousin: space is halved on two axes (X and Z) at a time, so each node has
// four children, and the third axis (Y / height) is left whole. This is the natural partition
// for scenes that are broad but shallow — terrain, top-down/RTS maps, 2.5-D worlds — where a
// full octree would waste a level subdividing a thin vertical extent. Items are filed into the
// quadrant their CENTRE's XZ projection falls in.
//
// As with the octree, each node caches the TIGHT 3-D AABB of its items, so pruning and the GPU
// leaf-cull see the real occupied volume (height included) and frustum culling stays correct in
// 3-D. Queries return item INDICES, exact for centre semantics. extract_leaves() yields the
// GPU leaf-cull form for a GPU query.

#include "space_partition.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

// One flattened quadtree node. Up to 4 children (-1 = empty quadrant); leaves own the slice
// ordered_indices()[first .. first+count).
struct QuadNode {
    math::AABB bounds;              // tight 3-D AABB of contained items
    int32_t    children[4] = { -1, -1, -1, -1 };
    uint32_t   first = 0;
    uint32_t   count = 0;
    bool       leaf  = false;
};

class Quadtree {
public:
    struct Desc {
        Desc() {}   // empty user-provided default ctor: GCC will not brace-init this nested aggregate (math::AABB member) in a `const Desc&={}` default arg
        int        max_leaf_size = 4;
        int        max_depth     = 16;
        math::AABB region;            // root region; empty (default) = fit to the items
        bool       has_region = false;
    };

    void build(const SceneItem* items, int count, const Desc& desc = {});
    void clear();

    bool built() const { return !nodes_.empty(); }
    int  node_count() const { return (int)nodes_.size(); }
    int  item_count() const { return (int)items_.size(); }
    int  leaf_count() const;
    int  max_depth_reached() const { return max_depth_reached_; }
    const std::vector<QuadNode>&  nodes()           const { return nodes_; }
    const std::vector<uint32_t>&  ordered_indices() const { return ordered_; }
    const std::vector<SceneItem>& items()           const { return items_; }
    const math::AABB&             region()          const { return region_; }

    int query_region(const math::AABB& box, std::vector<uint32_t>& out) const;
    int frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const;
    void extract_leaves(PartitionLeaves& out) const;

private:
    int  build_node(std::vector<uint32_t>& idxs, const math::AABB& region, int depth);
    void query_region_rec(int node, const math::AABB& box, std::vector<uint32_t>& out, int& found) const;
    void frustum_rec(int node, const Frustum& f, std::vector<uint32_t>& out, int& visible) const;

    Desc                   desc_{};
    std::vector<SceneItem> items_;
    std::vector<uint32_t>  ordered_;
    std::vector<QuadNode>  nodes_;
    math::AABB             region_{};
    int                    max_depth_reached_ = 0;
};

} // namespace gfx
} // namespace window
