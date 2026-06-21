#pragma once
// renderer/space_partition/uniform_grid.hpp
//
// UniformGrid — the CPU uniform-grid scene partition (uniform_grid.cpp).
//
// Items are filed by centre into a grid of equal cells (GridParams) using a counting sort,
// which lays the result out in compact CSR form: cell c owns the slice
//   sorted_indices()[ cell_start()[c] .. cell_start()[c] + counts()[c] )
// of item indices. Building is O(N + cells); a region/frustum query visits only the cells
// the query overlaps instead of all N items, and broad-phase pairing emits only items that
// share a cell. This is the reference implementation the GpuUniformGrid is checked against
// (same cell math, same query semantics), and it is fully usable on its own — it needs no
// graphics backend.
//
// Queries append item INDICES (into the array passed to build()); recover the object via
// items()[index]. Results are exact for centre-point semantics: an object whose centre lies
// in the query region/frustum is always returned (no false negatives), because its centre's
// cell is always among the cells the query visits.

#include "space_partition.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace window {
namespace gfx {

class UniformGrid {
public:
    // (Re)build the grid for `items[0..count)` under `params`. Previous contents are dropped.
    void build(const SceneItem* items, int count, const GridParams& params);
    void clear();

    bool              built()      const { return !cell_start_.empty(); }
    const GridParams& params()     const { return params_; }
    int               item_count() const { return (int)items_.size(); }
    int               cell_count() const { return params_.cell_count(); }
    const std::vector<SceneItem>& items() const { return items_; }

    // CSR layout (see header). Sizes: counts/cell_start == cell_count(); sorted == item_count().
    const std::vector<uint32_t>& counts()        const { return counts_; }
    const std::vector<uint32_t>& cell_start()     const { return cell_start_; }
    const std::vector<uint32_t>& sorted_indices() const { return sorted_; }

    // Item indices filed in one cell. Appends to `out`, returns the number appended.
    int query_cell(int cx, int cy, int cz, std::vector<uint32_t>& out) const;
    // Item indices in the cell containing `p` (one cell of broad-phase candidates).
    int query_point(const math::Vec3& p, std::vector<uint32_t>& out) const;

    // Item indices whose CENTRE lies inside `box` (grid-accelerated; exact for centres).
    int query_region(const math::AABB& box, std::vector<uint32_t>& out) const;
    // Item indices whose CENTRE lies inside `frustum` (grid-accelerated visibility cull).
    int frustum_cull(const Frustum& frustum, std::vector<uint32_t>& out) const;

    // Broad-phase: every unordered index pair (i<j) whose items share a cell — the classic
    // uniform-grid collision candidate set. Appends to `out`, returns the number appended.
    int potential_pairs(std::vector<std::pair<uint32_t, uint32_t>>& out) const;

private:
    // Visit the cells overlapping `box`'s clamped coordinate range, calling fn(cell_linear).
    template <class Fn>
    void for_cells_in_box(const math::AABB& box, Fn&& fn) const;

    GridParams            params_{};
    std::vector<SceneItem> items_;
    std::vector<uint32_t>  counts_;      // per cell
    std::vector<uint32_t>  cell_start_;  // per cell (exclusive prefix sum of counts_)
    std::vector<uint32_t>  sorted_;      // item indices, grouped by cell
};

} // namespace gfx
} // namespace window
