#pragma once
// Model loading — decode a 3D model file into backend-neutral ModelData via Assimp.
// Supported formats include OBJ, FBX, glTF 2.0 / GLB, Collada (DAE), STL, PLY, 3DS,
// Blender (.blend), DirectX (.x), and many more (see supported_model_extensions()).
//
// Assimp is optional: when the build is configured with WINDOW_ENABLE_MODEL_LOADER=OFF
// these functions return false with an explanatory error and model_loader_available()
// is false. Image loading (image.hpp) and the rest of AssetManager keep working.

#include "mesh.hpp"

#include <cstddef>
#include <string>

namespace window {
namespace gfx {

struct ModelLoadOptions {
    bool  triangulate            = true;   // split polygons into triangles
    bool  generate_normals       = true;   // smooth normals where the file has none
    bool  calc_tangents          = true;   // tangent basis for normal mapping
    bool  flip_uvs               = false;  // flip V (use for D3D / top-left UV origin)
    bool  join_identical_vertices = true;  // weld duplicate vertices (smaller buffers)
    bool  optimize_meshes        = true;   // merge small meshes / reduce draw calls
    bool  pretransform           = false;  // bake the node hierarchy into vertices (flatten)
    bool  flip_winding           = false;  // reverse triangle winding (CW <-> CCW)
    bool  make_left_handed       = false;  // convert to a left-handed coordinate system
    bool  import_colors          = true;   // keep per-vertex colors when present
    float global_scale           = 1.0f;   // uniform scale applied on import
};

// Load `path` into `out`. On failure returns false and (if non-null) fills *out_error.
bool load_model(const char* path, ModelData* out,
                const ModelLoadOptions& options = {}, std::string* out_error = nullptr);

// Load from an in-memory copy of a model file. `format_hint` is the extension without
// the dot ("obj", "gltf", "fbx", ...) to steer Assimp; nullptr/"" lets it auto-detect.
bool load_model_from_memory(const void* data, size_t size, const char* format_hint,
                            ModelData* out, const ModelLoadOptions& options = {},
                            std::string* out_error = nullptr);

// True when model loading was compiled in (Assimp available).
bool model_loader_available();

// Space-separated importable extensions ("obj fbx gltf glb dae stl ply ..."), or ""
// when the loader is unavailable.
std::string supported_model_extensions();

} // namespace gfx
} // namespace window
