// renderer/space_partition/quadtree.cpp
//
// 4-way XZ-plane spatial quadtree build + pruned queries + leaf extraction.

#include "quadtree.hpp"

#include <algorithm>

namespace window {
namespace gfx {

using math::Vec3;
using math::AABB;

void Quadtree::clear() {
    items_.clear();
    ordered_.clear();
    nodes_.clear();
    region_ = AABB();
    max_depth_reached_ = 0;
}

// Sub-region for quadrant `q`: bit 0 = +x, bit 1 = +z (split at the centre on X and Z; Y whole).
static AABB quadrant_region(const AABB& r, const Vec3& c, int q) {
    Vec3 mn((q & 1) ? c.x : r.min_pt.x, r.min_pt.y, (q & 2) ? c.z : r.min_pt.z);
    Vec3 mx((q & 1) ? r.max_pt.x : c.x, r.max_pt.y, (q & 2) ? r.max_pt.z : c.z);
    return AABB(mn, mx);
}

void Quadtree::build(const SceneItem* items, int count, const Desc& desc) {
    clear();
    desc_ = desc;
    items_.assign(items, items + std::max(0, count));
    const int n = (int)items_.size();
    if (n <= 0) return;

    region_ = desc.has_region ? desc.region : items_[0].bounds;
    if (!desc.has_region)
        for (int i = 1; i < n; ++i) region_ = region_.merged(items_[i].bounds);

    std::vector<uint32_t> idxs(n);
    for (int i = 0; i < n; ++i) idxs[i] = (uint32_t)i;
    ordered_.reserve(n);
    nodes_.reserve(n);
    build_node(idxs, region_, 0);
}

int Quadtree::build_node(std::vector<uint32_t>& idxs, const AABB& region, int depth) {
    const int ni = (int)nodes_.size();
    nodes_.push_back(QuadNode{});
    max_depth_reached_ = std::max(max_depth_reached_, depth);

    QuadNode node;
    node.bounds = items_[idxs[0]].bounds;
    for (size_t i = 1; i < idxs.size(); ++i) node.bounds = node.bounds.merged(items_[idxs[i]].bounds);

    if ((int)idxs.size() <= desc_.max_leaf_size || depth >= desc_.max_depth) {
        node.leaf = true;
        node.first = (uint32_t)ordered_.size();
        node.count = (uint32_t)idxs.size();
        for (uint32_t id : idxs) ordered_.push_back(id);
        nodes_[ni] = node;
        return ni;
    }

    const Vec3 c = region.center();
    std::vector<uint32_t> bins[4];
    for (uint32_t id : idxs) {
        Vec3 p = items_[id].center();
        int q = (p.x >= c.x ? 1 : 0) | (p.z >= c.z ? 2 : 0);
        bins[q].push_back(id);
    }
    node.leaf = false;
    for (int q = 0; q < 4; ++q)
        node.children[q] = bins[q].empty() ? -1 : build_node(bins[q], quadrant_region(region, c, q), depth + 1);
    nodes_[ni] = node;
    return ni;
}

int Quadtree::leaf_count() const {
    int n = 0;
    for (const QuadNode& nd : nodes_) if (nd.leaf) ++n;
    return n;
}

void Quadtree::query_region_rec(int ni, const AABB& box, std::vector<uint32_t>& out, int& found) const {
    const QuadNode& nd = nodes_[ni];
    if (!nd.bounds.intersects(box)) return;
    if (nd.leaf) {
        for (uint32_t k = 0; k < nd.count; ++k) {
            uint32_t idx = ordered_[nd.first + k];
            if (box.contains(items_[idx].center())) { out.push_back(idx); ++found; }
        }
        return;
    }
    for (int q = 0; q < 4; ++q)
        if (nd.children[q] >= 0) query_region_rec(nd.children[q], box, out, found);
}

int Quadtree::query_region(const AABB& box, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int found = 0;
    query_region_rec(0, box, out, found);
    return found;
}

void Quadtree::frustum_rec(int ni, const Frustum& f, std::vector<uint32_t>& out, int& visible) const {
    const QuadNode& nd = nodes_[ni];
    if (!f.intersects(nd.bounds)) return;
    if (nd.leaf) {
        for (uint32_t k = 0; k < nd.count; ++k) {
            uint32_t idx = ordered_[nd.first + k];
            if (f.contains_point(items_[idx].center())) { out.push_back(idx); ++visible; }
        }
        return;
    }
    for (int q = 0; q < 4; ++q)
        if (nd.children[q] >= 0) frustum_rec(nd.children[q], f, out, visible);
}

int Quadtree::frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int visible = 0;
    frustum_rec(0, frustum, out, visible);
    return visible;
}

void Quadtree::extract_leaves(PartitionLeaves& out) const {
    out.clear();
    out.item_indices = ordered_;
    for (const QuadNode& nd : nodes_)
        if (nd.leaf && nd.count > 0)
            out.add_leaf(nd.bounds, nd.first, nd.count);
}

} // namespace gfx
} // namespace window
