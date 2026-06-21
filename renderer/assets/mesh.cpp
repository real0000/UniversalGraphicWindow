#include "mesh.hpp"

namespace window {
namespace gfx {

void ModelData::compute_bounds() {
    bounds = Aabb{};
    for (auto& sm : submeshes) {
        sm.bounds = Aabb{};
        const uint32_t end = sm.index_offset + sm.index_count;
        for (uint32_t i = sm.index_offset; i < end && i < indices.size(); ++i) {
            const uint32_t vi = sm.base_vertex + indices[i];
            if (vi < vertices.size()) sm.bounds.expand(vertices[vi].position);
        }
        bounds.expand(sm.bounds);
    }
    // No submeshes: fall back to bounding every vertex.
    if (submeshes.empty()) {
        for (auto& v : vertices) bounds.expand(v.position);
    }
}

} // namespace gfx
} // namespace window
