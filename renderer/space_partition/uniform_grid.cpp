// renderer/space_partition/uniform_grid.cpp
//
// CPU uniform-grid build (counting sort → CSR) and queries.

#include "uniform_grid.hpp"

#include <algorithm>

namespace window {
namespace gfx {

using math::Vec3;
using math::AABB;

void UniformGrid::clear() {
    items_.clear();
    counts_.clear();
    cell_start_.clear();
    sorted_.clear();
}

void UniformGrid::build(const SceneItem* items, int count, const GridParams& params) {
    params_ = params;
    items_.assign(items, items + std::max(0, count));

    const int cells = params_.cell_count();
    const int n     = (int)items_.size();

    counts_.assign(cells, 0u);
    cell_start_.assign(cells, 0u);
    sorted_.assign(n, 0u);
    if (cells <= 0 || n <= 0) return;

    // Cache each item's cell so we touch the (more expensive) cell math once.
    std::vector<int> item_cell(n);
    for (int i = 0; i < n; ++i) {
        int c = params_.cell_of_point(items_[i].center());
        item_cell[i] = c;
        ++counts_[c];
    }

    // Exclusive prefix sum: cell_start_[c] = first slot of cell c in the sorted array.
    uint32_t acc = 0;
    for (int c = 0; c < cells; ++c) {
        cell_start_[c] = acc;
        acc += counts_[c];
    }

    // Scatter item indices into their cell's slice, using a moving cursor per cell.
    std::vector<uint32_t> cursor = cell_start_;
    for (int i = 0; i < n; ++i)
        sorted_[cursor[item_cell[i]]++] = (uint32_t)i;
}

int UniformGrid::query_cell(int cx, int cy, int cz, std::vector<uint32_t>& out) const {
    if (!built() || cx < 0 || cy < 0 || cz < 0 ||
        cx >= params_.dim_x || cy >= params_.dim_y || cz >= params_.dim_z)
        return 0;
    const int c = params_.linear(cx, cy, cz);
    const uint32_t start = cell_start_[c], cnt = counts_[c];
    for (uint32_t k = 0; k < cnt; ++k) out.push_back(sorted_[start + k]);
    return (int)cnt;
}

int UniformGrid::query_point(const Vec3& p, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int cx, cy, cz;
    params_.cell_coord(p, &cx, &cy, &cz);
    return query_cell(cx, cy, cz, out);
}

template <class Fn>
void UniformGrid::for_cells_in_box(const AABB& box, Fn&& fn) const {
    int x0, y0, z0, x1, y1, z1;
    params_.cell_coord(box.min_pt, &x0, &y0, &z0);
    params_.cell_coord(box.max_pt, &x1, &y1, &z1);
    for (int cz = z0; cz <= z1; ++cz)
        for (int cy = y0; cy <= y1; ++cy)
            for (int cx = x0; cx <= x1; ++cx)
                fn(params_.linear(cx, cy, cz));
}

int UniformGrid::query_region(const AABB& box, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int found = 0;
    for_cells_in_box(box, [&](int c) {
        const uint32_t start = cell_start_[c], cnt = counts_[c];
        for (uint32_t k = 0; k < cnt; ++k) {
            uint32_t idx = sorted_[start + k];
            if (box.contains(items_[idx].center())) { out.push_back(idx); ++found; }
        }
    });
    return found;
}

int UniformGrid::frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const {
    if (!built()) return 0;
    int visible = 0;
    const int cells = params_.cell_count();
    for (int c = 0; c < cells; ++c) {
        if (counts_[c] == 0) continue;
        // Decode cell coords to test the cell box; skip whole cells outside the frustum.
        int cx = c % params_.dim_x;
        int cy = (c / params_.dim_x) % params_.dim_y;
        int cz = c / (params_.dim_x * params_.dim_y);
        if (!frustum.intersects(params_.cell_bounds(cx, cy, cz))) continue;
        const uint32_t start = cell_start_[c], cnt = counts_[c];
        for (uint32_t k = 0; k < cnt; ++k) {
            uint32_t idx = sorted_[start + k];
            if (frustum.contains_point(items_[idx].center())) { out.push_back(idx); ++visible; }
        }
    }
    return visible;
}

int UniformGrid::potential_pairs(std::vector<std::pair<uint32_t, uint32_t>>& out) const {
    if (!built()) return 0;
    int pairs = 0;
    const int cells = params_.cell_count();
    for (int c = 0; c < cells; ++c) {
        const uint32_t start = cell_start_[c], cnt = counts_[c];
        for (uint32_t a = 0; a < cnt; ++a)
            for (uint32_t b = a + 1; b < cnt; ++b) {
                out.emplace_back(sorted_[start + a], sorted_[start + b]);
                ++pairs;
            }
    }
    return pairs;
}

} // namespace gfx
} // namespace window
