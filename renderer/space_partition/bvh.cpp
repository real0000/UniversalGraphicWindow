// renderer/space_partition/bvh.cpp
//
// Top-down median-split binary BVH build + pruned queries + leaf extraction.

#include "bvh.hpp"

#include <algorithm>

namespace window {
namespace gfx {

using math::Vec3;
using math::AABB;

void BVH::clear() {
    items_.clear();
    ordered_.clear();
    nodes_.clear();
    root_bounds_ = AABB();
    max_depth_reached_ = 0;
}

AABB BVH::bounds_of(uint32_t start, uint32_t end) const {
    AABB b = items_[ordered_[start]].bounds;
    for (uint32_t i = start + 1; i < end; ++i) b = b.merged(items_[ordered_[i]].bounds);
    return b;
}

AABB BVH::centroid_bounds(uint32_t start, uint32_t end) const {
    Vec3 c0 = items_[ordered_[start]].center();
    AABB b(c0, c0);
    for (uint32_t i = start + 1; i < end; ++i) {
        Vec3 c = items_[ordered_[i]].center();
        b.min_pt = math::min(b.min_pt, c);
        b.max_pt = math::max(b.max_pt, c);
    }
    return b;
}

void BVH::build(const SceneItem* items, int count, const Desc& desc) {
    clear();
    desc_ = desc;
    items_.assign(items, items + std::max(0, count));
    const int n = (int)items_.size();
    if (n <= 0) return;

    ordered_.resize(n);
    for (int i = 0; i < n; ++i) ordered_[i] = (uint32_t)i;
    nodes_.reserve(2 * n);          // a binary tree over n leaves has < 2n nodes
    build_range(0, (uint32_t)n, 0);
    root_bounds_ = nodes_.empty() ? AABB() : nodes_[0].bounds;
}

int BVH::build_range(uint32_t start, uint32_t end, int depth) {
    const int idx = (int)nodes_.size();
    nodes_.push_back(BvhNode{});            // reserve this slot (children append after it)
    max_depth_reached_ = std::max(max_depth_reached_, depth);

    BvhNode node;
    node.bounds = bounds_of(start, end);

    const uint32_t span = end - start;
    AABB cb = centroid_bounds(start, end);
    Vec3 ext = cb.size();
    int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);

    // Make a leaf when small enough, too deep, or the centroids coincide (no split possible).
    if (span <= (uint32_t)desc_.max_leaf_size || depth >= desc_.max_depth || ext[axis] < math::EPSILON) {
        node.first = start; node.count = span;
        nodes_[idx] = node;
        return idx;
    }

    // Partition ordered_[start,end) about the spatial median of the centroid extent.
    float mid_coord = 0.5f * (cb.min_pt[axis] + cb.max_pt[axis]);
    auto mid_it = std::partition(ordered_.begin() + start, ordered_.begin() + end,
        [&](uint32_t it) { return items_[it].center()[axis] < mid_coord; });
    uint32_t mid = (uint32_t)(mid_it - ordered_.begin());
    if (mid == start || mid == end) mid = start + span / 2;   // fall back to a balanced split

    int l = build_range(start, mid, depth + 1);
    int r = build_range(mid, end, depth + 1);
    node.left = l; node.right = r; node.count = 0;
    nodes_[idx] = node;
    return idx;
}

int BVH::leaf_count() const {
    int n = 0;
    for (const BvhNode& nd : nodes_) if (nd.is_leaf()) ++n;
    return n;
}

int BVH::query_region(const AABB& box, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int found = 0;
    // Iterative descent over the flattened array (root at 0).
    uint32_t stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        const BvhNode& nd = nodes_[stack[--sp]];
        if (!nd.bounds.intersects(box)) continue;
        if (nd.is_leaf()) {
            for (uint32_t k = 0; k < nd.count; ++k) {
                uint32_t idx = ordered_[nd.first + k];
                if (box.contains(items_[idx].center())) { out.push_back(idx); ++found; }
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = (uint32_t)nd.left;
            stack[sp++] = (uint32_t)nd.right;
        }
    }
    return found;
}

int BVH::frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int visible = 0;
    uint32_t stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        const BvhNode& nd = nodes_[stack[--sp]];
        if (!frustum.intersects(nd.bounds)) continue;
        if (nd.is_leaf()) {
            for (uint32_t k = 0; k < nd.count; ++k) {
                uint32_t idx = ordered_[nd.first + k];
                if (frustum.contains_point(items_[idx].center())) { out.push_back(idx); ++visible; }
            }
        } else if (sp + 2 <= 64) {
            stack[sp++] = (uint32_t)nd.left;
            stack[sp++] = (uint32_t)nd.right;
        }
    }
    return visible;
}

void BVH::extract_leaves(PartitionLeaves& out) const {
    out.clear();
    out.item_indices = ordered_;   // leaves already index contiguous slices of ordered_
    for (const BvhNode& nd : nodes_)
        if (nd.is_leaf() && nd.count > 0)
            out.add_leaf(nd.bounds, nd.first, nd.count);
}

} // namespace gfx
} // namespace window
