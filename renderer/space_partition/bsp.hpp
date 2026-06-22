#pragma once
// renderer/space_partition/bsp.hpp
//
// BSP — an axis-aligned Binary Space Partition (a.k.a. kd-tree) scene partition (bsp.cpp).
//
// At each interior node space is cut by ONE axis-aligned plane into two disjoint half-spaces;
// an item goes to the side its CENTRE lies on. Recursing builds a binary tree of nested convex
// (here, axis-aligned) regions. Unlike a BVH — which stores bounding *volumes* of object groups
// that may overlap — a BSP stores the splitting *planes* and its cells tile space without
// overlap, which is what makes a BSP a point-location / ordered-traversal structure (the lineage
// of Doom-era visibility and back-to-front sorting). This implementation keeps the planes
// axis-aligned and splits at the median of the items' centres along the node's longest axis,
// i.e. the classic kd-tree form.
//
// Each node also caches the TIGHT AABB of the items beneath it, so pruning and the GPU leaf-cull
// act on the real occupied bounds. Queries return item INDICES, exact for centre semantics.
// extract_leaves() yields the GPU leaf-cull form for a GPU query.

#include "space_partition.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

// One flattened BSP/kd node. Interior nodes record the split plane (axis + position) and two
// children; leaves own the slice ordered_indices()[first .. first+count). left < 0 marks a leaf.
struct BspNode {
    math::AABB bounds;            // tight AABB of contained items (pruning + GPU leaf bound)
    int32_t    left  = -1;        // interior: front/low child; leaf: -1
    int32_t    right = -1;        // interior: back/high child
    uint8_t    axis  = 0;         // interior: split axis (0=x,1=y,2=z)
    float      split = 0.0f;      // interior: split-plane position along `axis`
    uint32_t   first = 0;         // leaf
    uint32_t   count = 0;         // leaf
    bool is_leaf() const { return left < 0; }
};

class BSP {
public:
    struct Desc {
        Desc() {}   // empty user-provided default ctor: GCC will not brace-init this nested aggregate (math::AABB member) in a `const Desc&={}` default arg
        int max_leaf_size = 4;
        int max_depth     = 32;
    };

    void build(const SceneItem* items, int count, const Desc& desc = {});
    void clear();

    bool built() const { return !nodes_.empty(); }
    int  node_count() const { return (int)nodes_.size(); }
    int  item_count() const { return (int)items_.size(); }
    int  leaf_count() const;
    int  max_depth_reached() const { return max_depth_reached_; }
    const std::vector<BspNode>&   nodes()           const { return nodes_; }
    const std::vector<uint32_t>&  ordered_indices() const { return ordered_; }
    const std::vector<SceneItem>& items()           const { return items_; }
    const math::AABB&             bounds()          const { return root_bounds_; }

    int query_region(const math::AABB& box, std::vector<uint32_t>& out) const;
    int frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const;
    void extract_leaves(PartitionLeaves& out) const;

private:
    int  build_range(uint32_t start, uint32_t end, int depth);
    math::AABB bounds_of(uint32_t start, uint32_t end) const;

    Desc                   desc_{};
    std::vector<SceneItem> items_;
    std::vector<uint32_t>  ordered_;
    std::vector<BspNode>   nodes_;
    math::AABB             root_bounds_{};
    int                    max_depth_reached_ = 0;
};

} // namespace gfx
} // namespace window
