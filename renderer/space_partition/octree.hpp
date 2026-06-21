#pragma once
// renderer/space_partition/octree.hpp
//
// Octree — an 8-way spatial-subdivision scene partition (octree.cpp).
//
// Space is recursively halved on all three axes at once: a node's region splits into eight
// octants about its centre, and an item is filed into the octant its CENTRE falls in. Unlike
// a BVH (which splits the object set into possibly-overlapping volumes) an octree partitions
// SPACE into disjoint cells, so it doubles as a point-location structure and is the classic
// choice for evenly-but-sparsely populated 3D scenes (LOD, occlusion, voxel worlds). Empty
// octants cost nothing — only populated children are created.
//
// Each node also caches the TIGHT AABB of the items beneath it (not just its region cube), so
// a query prunes on the real occupied bounds. Queries return item INDICES and are exact for
// centre semantics (a centre inside the region/frustum is never pruned away). Nodes are stored
// flattened depth-first; extract_leaves() hands the leaves to the GPU leaf-cull for a GPU query.

#include "space_partition.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

// One flattened octree node. Interior nodes index up to 8 children (-1 = empty octant); leaves
// own the slice ordered_indices()[first .. first+count).
struct OctNode {
    math::AABB bounds;              // tight AABB of contained items (pruning + GPU leaf bound)
    int32_t    children[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    uint32_t   first = 0;
    uint32_t   count = 0;
    bool       leaf  = false;
};

class Octree {
public:
    struct Desc {
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
    const std::vector<OctNode>&   nodes()           const { return nodes_; }
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
    std::vector<OctNode>   nodes_;
    math::AABB             region_{};
    int                    max_depth_reached_ = 0;
};

} // namespace gfx
} // namespace window
