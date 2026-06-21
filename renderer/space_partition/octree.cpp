// renderer/space_partition/octree.cpp
//
// 8-way spatial octree build + pruned queries + leaf extraction.

#include "octree.hpp"

#include <algorithm>

namespace window {
namespace gfx {

using math::Vec3;
using math::AABB;

void Octree::clear() {
    items_.clear();
    ordered_.clear();
    nodes_.clear();
    region_ = AABB();
    max_depth_reached_ = 0;
}

// Sub-region for octant `o`: bit 0 = +x, 1 = +y, 2 = +z (split at the region centre).
static AABB octant_region(const AABB& r, const Vec3& c, int o) {
    Vec3 mn((o & 1) ? c.x : r.min_pt.x, (o & 2) ? c.y : r.min_pt.y, (o & 4) ? c.z : r.min_pt.z);
    Vec3 mx((o & 1) ? r.max_pt.x : c.x, (o & 2) ? r.max_pt.y : c.y, (o & 4) ? r.max_pt.z : c.z);
    return AABB(mn, mx);
}

void Octree::build(const SceneItem* items, int count, const Desc& desc) {
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

int Octree::build_node(std::vector<uint32_t>& idxs, const AABB& region, int depth) {
    const int ni = (int)nodes_.size();
    nodes_.push_back(OctNode{});
    max_depth_reached_ = std::max(max_depth_reached_, depth);

    OctNode node;
    node.bounds = items_[idxs[0]].bounds;            // tight bound of the items beneath this node
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
    std::vector<uint32_t> bins[8];
    for (uint32_t id : idxs) {
        Vec3 p = items_[id].center();
        int o = (p.x >= c.x ? 1 : 0) | (p.y >= c.y ? 2 : 0) | (p.z >= c.z ? 4 : 0);
        bins[o].push_back(id);
    }
    node.leaf = false;
    for (int o = 0; o < 8; ++o)
        node.children[o] = bins[o].empty() ? -1 : build_node(bins[o], octant_region(region, c, o), depth + 1);
    nodes_[ni] = node;
    return ni;
}

int Octree::leaf_count() const {
    int n = 0;
    for (const OctNode& nd : nodes_) if (nd.leaf) ++n;
    return n;
}

void Octree::query_region_rec(int ni, const AABB& box, std::vector<uint32_t>& out, int& found) const {
    const OctNode& nd = nodes_[ni];
    if (!nd.bounds.intersects(box)) return;
    if (nd.leaf) {
        for (uint32_t k = 0; k < nd.count; ++k) {
            uint32_t idx = ordered_[nd.first + k];
            if (box.contains(items_[idx].center())) { out.push_back(idx); ++found; }
        }
        return;
    }
    for (int o = 0; o < 8; ++o)
        if (nd.children[o] >= 0) query_region_rec(nd.children[o], box, out, found);
}

int Octree::query_region(const AABB& box, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int found = 0;
    query_region_rec(0, box, out, found);
    return found;
}

void Octree::frustum_rec(int ni, const Frustum& f, std::vector<uint32_t>& out, int& visible) const {
    const OctNode& nd = nodes_[ni];
    if (!f.intersects(nd.bounds)) return;
    if (nd.leaf) {
        for (uint32_t k = 0; k < nd.count; ++k) {
            uint32_t idx = ordered_[nd.first + k];
            if (f.contains_point(items_[idx].center())) { out.push_back(idx); ++visible; }
        }
        return;
    }
    for (int o = 0; o < 8; ++o)
        if (nd.children[o] >= 0) frustum_rec(nd.children[o], f, out, visible);
}

int Octree::frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int visible = 0;
    frustum_rec(0, frustum, out, visible);
    return visible;
}

void Octree::extract_leaves(PartitionLeaves& out) const {
    out.clear();
    out.item_indices = ordered_;
    for (const OctNode& nd : nodes_)
        if (nd.leaf && nd.count > 0)
            out.add_leaf(nd.bounds, nd.first, nd.count);
}

} // namespace gfx
} // namespace window
