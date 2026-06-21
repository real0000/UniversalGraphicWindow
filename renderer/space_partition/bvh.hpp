#pragma once
// renderer/space_partition/bvh.hpp
//
// BVH — a binary Bounding Volume Hierarchy scene partition (bvh.cpp).
//
// Items are grouped into a tree of nested AABBs: each interior node bounds its two children,
// each leaf bounds a small handful of items. A query descends only into nodes whose bounds it
// touches, pruning whole subtrees — O(log N) average for point/region/frustum queries versus
// O(N) for a flat scan, and unlike a uniform grid it adapts to clustered scenes (no empty
// cells, tight leaves). This is the workhorse partition for visibility culling and ray casts.
//
// Built top-down by splitting the longest axis of the centroid bounds at the spatial median
// (a fast, robust median-of-extent split). Nodes are stored flattened depth-first in one
// array (cache-friendly, trivially uploadable), with the left child always at node_index+1.
// Item order is permuted into ordered_indices() so each leaf owns a contiguous slice.
//
// Queries return item INDICES (into the array passed to build()); they are exact for centre
// semantics — an item whose centre lies in the query region/frustum is always returned, since
// its leaf's bounds contain that centre and therefore survive the prune. extract_leaves()
// flattens the leaves into the backend-neutral PartitionLeaves the GPU leaf-cull consumes, so
// the BVH gets a GPU query for free (build on CPU, cull on GPU). Same semantics, so a GPU cull
// can be checked against this CPU one.

#include "space_partition.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

// One flattened BVH node. Interior nodes use left/right child indices; leaves own the item
// slice ordered_indices()[first .. first+count). left < 0 marks a leaf.
struct BvhNode {
    math::AABB bounds;
    int32_t    left  = -1;   // interior: left child index (== self+1); leaf: -1
    int32_t    right = -1;   // interior: right child index
    uint32_t   first = 0;    // leaf: first item in ordered_indices()
    uint32_t   count = 0;    // leaf: item count (interior: 0)
    bool is_leaf() const { return left < 0; }
};

class BVH {
public:
    struct Desc {
        int max_leaf_size = 4;   // stop splitting at this many items
        int max_depth     = 32;  // hard recursion cap
    };

    void build(const SceneItem* items, int count, const Desc& desc = {});
    void clear();

    bool built() const { return !nodes_.empty(); }
    int  node_count() const { return (int)nodes_.size(); }
    int  item_count() const { return (int)items_.size(); }
    int  leaf_count() const;
    int  max_depth_reached() const { return max_depth_reached_; }
    const std::vector<BvhNode>&   nodes()           const { return nodes_; }
    const std::vector<uint32_t>&  ordered_indices() const { return ordered_; }
    const std::vector<SceneItem>& items()           const { return items_; }
    const math::AABB&             bounds()          const { return root_bounds_; }

    // Item indices whose CENTRE lies in `box` / `frustum` (subtree-pruned; exact for centres).
    int query_region(const math::AABB& box, std::vector<uint32_t>& out) const;
    int frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const;

    // Flatten the leaves into the GPU-friendly PartitionLeaves form (for GpuLeafCuller).
    void extract_leaves(PartitionLeaves& out) const;

private:
    int  build_range(uint32_t start, uint32_t end, int depth);
    math::AABB bounds_of(uint32_t start, uint32_t end) const;        // union of item bounds
    math::AABB centroid_bounds(uint32_t start, uint32_t end) const;  // union of item centres

    Desc                   desc_{};
    std::vector<SceneItem> items_;
    std::vector<uint32_t>  ordered_;   // item indices, permuted so leaves are contiguous
    std::vector<BvhNode>   nodes_;     // flattened, depth-first; root at 0
    math::AABB             root_bounds_{};
    int                    max_depth_reached_ = 0;
};

} // namespace gfx
} // namespace window
