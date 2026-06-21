// renderer/space_partition/bsp.cpp
//
// Axis-aligned BSP / kd-tree build (median split on the longest axis) + pruned queries.

#include "bsp.hpp"

#include <algorithm>

namespace window {
namespace gfx {

using math::Vec3;
using math::AABB;

void BSP::clear() {
    items_.clear();
    ordered_.clear();
    nodes_.clear();
    root_bounds_ = AABB();
    max_depth_reached_ = 0;
}

AABB BSP::bounds_of(uint32_t start, uint32_t end) const {
    AABB b = items_[ordered_[start]].bounds;
    for (uint32_t i = start + 1; i < end; ++i) b = b.merged(items_[ordered_[i]].bounds);
    return b;
}

void BSP::build(const SceneItem* items, int count, const Desc& desc) {
    clear();
    desc_ = desc;
    items_.assign(items, items + std::max(0, count));
    const int n = (int)items_.size();
    if (n <= 0) return;

    ordered_.resize(n);
    for (int i = 0; i < n; ++i) ordered_[i] = (uint32_t)i;
    nodes_.reserve(2 * n);
    build_range(0, (uint32_t)n, 0);
    root_bounds_ = nodes_.empty() ? AABB() : nodes_[0].bounds;
}

int BSP::build_range(uint32_t start, uint32_t end, int depth) {
    const int idx = (int)nodes_.size();
    nodes_.push_back(BspNode{});
    max_depth_reached_ = std::max(max_depth_reached_, depth);

    BspNode node;
    node.bounds = bounds_of(start, end);
    const uint32_t span = end - start;

    // Split plane: the longest axis of the node's spatial extent, cut at the median centre.
    Vec3 ext = node.bounds.size();
    int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);

    if (span <= (uint32_t)desc_.max_leaf_size || depth >= desc_.max_depth || ext[axis] < math::EPSILON) {
        node.first = start; node.count = span;
        nodes_[idx] = node;
        return idx;
    }

    // Median partition by centre along `axis` (balanced kd split).
    uint32_t mid = start + span / 2;
    std::nth_element(ordered_.begin() + start, ordered_.begin() + mid, ordered_.begin() + end,
        [&](uint32_t a, uint32_t b) { return items_[a].center()[axis] < items_[b].center()[axis]; });

    node.axis  = (uint8_t)axis;
    node.split = items_[ordered_[mid]].center()[axis];   // plane through the median item's centre
    int l = build_range(start, mid, depth + 1);
    int r = build_range(mid, end, depth + 1);
    node.left = l; node.right = r; node.count = 0;
    nodes_[idx] = node;
    return idx;
}

int BSP::leaf_count() const {
    int n = 0;
    for (const BspNode& nd : nodes_) if (nd.is_leaf()) ++n;
    return n;
}

int BSP::query_region(const AABB& box, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int found = 0;
    uint32_t stack[128]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        const BspNode& nd = nodes_[stack[--sp]];
        if (!nd.bounds.intersects(box)) continue;
        if (nd.is_leaf()) {
            for (uint32_t k = 0; k < nd.count; ++k) {
                uint32_t idx = ordered_[nd.first + k];
                if (box.contains(items_[idx].center())) { out.push_back(idx); ++found; }
            }
        } else if (sp + 2 <= 128) {
            stack[sp++] = (uint32_t)nd.left;
            stack[sp++] = (uint32_t)nd.right;
        }
    }
    return found;
}

int BSP::frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int visible = 0;
    uint32_t stack[128]; int sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        const BspNode& nd = nodes_[stack[--sp]];
        if (!frustum.intersects(nd.bounds)) continue;
        if (nd.is_leaf()) {
            for (uint32_t k = 0; k < nd.count; ++k) {
                uint32_t idx = ordered_[nd.first + k];
                if (frustum.contains_point(items_[idx].center())) { out.push_back(idx); ++visible; }
            }
        } else if (sp + 2 <= 128) {
            stack[sp++] = (uint32_t)nd.left;
            stack[sp++] = (uint32_t)nd.right;
        }
    }
    return visible;
}

void BSP::extract_leaves(PartitionLeaves& out) const {
    out.clear();
    out.item_indices = ordered_;
    for (const BspNode& nd : nodes_)
        if (nd.is_leaf() && nd.count > 0)
            out.add_leaf(nd.bounds, nd.first, nd.count);
}

} // namespace gfx
} // namespace window
