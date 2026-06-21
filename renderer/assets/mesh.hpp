#pragma once
// Mesh / model CPU data — the backend-neutral result of decoding a 3D model file.
// A whole ModelData carries one shared vertex/index pool split into SubMeshes (each
// with a single material), a material table, the textures those materials reference
// (external URIs or model-embedded blobs), and an axis-aligned bound. AssetManager
// turns this into GPU buffers + textures; the data here has no GPU dependency, so it
// is also useful for collision, CPU processing, or re-export.

#include "../../math_util.hpp"

#include <cfloat>
#include <cstdint>
#include <string>
#include <vector>

namespace window {
namespace gfx {

// Interleaved vertex (a common PBR-friendly layout). tangent.w is the bitangent sign
// (+1 / -1) so the shader can reconstruct it as cross(normal, tangent.xyz) * w.
struct MeshVertex {
    math::Vec3 position;
    math::Vec3 normal   = math::Vec3(0, 0, 1);
    math::Vec4 tangent  = math::Vec4(1, 0, 0, 1);
    math::Vec2 uv0;
    math::Vec2 uv1;
    math::Vec4 color    = math::Vec4(1, 1, 1, 1);
};

// Axis-aligned bounding box. Default-constructs "empty" (min > max) so the first
// expand() seeds it.
struct Aabb {
    math::Vec3 min = math::Vec3( FLT_MAX,  FLT_MAX,  FLT_MAX);
    math::Vec3 max = math::Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void expand(const math::Vec3& p) {
        min.x = p.x < min.x ? p.x : min.x;  max.x = p.x > max.x ? p.x : max.x;
        min.y = p.y < min.y ? p.y : min.y;  max.y = p.y > max.y ? p.y : max.y;
        min.z = p.z < min.z ? p.z : min.z;  max.z = p.z > max.z ? p.z : max.z;
    }
    void expand(const Aabb& b) { if (b.valid()) { expand(b.min); expand(b.max); } }
    math::Vec3 center() const { return (min + max) * 0.5f; }
    math::Vec3 extent() const { return (max - min) * 0.5f; }   // half-size
    math::Vec3 size()   const { return  max - min; }
};

// How a model alpha-blends, mapped from glTF alphaMode (others approximate to these).
enum class AlphaMode : uint8_t { Opaque, Mask, Blend };

// PBR metallic-roughness material parameters plus legacy fields the importer can
// supply. Texture members index ModelData::textures (-1 = no texture for that slot).
struct MeshMaterial {
    std::string name;
    math::Vec4  base_color  = math::Vec4(1, 1, 1, 1);   // RGBA factor
    math::Vec3  emissive    = math::Vec3(0, 0, 0);
    float       metallic    = 1.0f;
    float       roughness   = 1.0f;
    float       occlusion_strength = 1.0f;
    float       normal_scale       = 1.0f;
    float       alpha_cutoff       = 0.5f;
    AlphaMode   alpha_mode  = AlphaMode::Opaque;
    bool        double_sided = false;

    int base_color_tex         = -1;   // albedo / diffuse  (sRGB)
    int normal_tex             = -1;   // tangent-space normal map (linear)
    int metallic_roughness_tex = -1;   // G = roughness, B = metallic (linear)
    int emissive_tex           = -1;   // emissive (sRGB)
    int occlusion_tex          = -1;   // R = ambient occlusion (linear)
};

// A texture a material refers to: either an external file (relative to the model's
// directory) or an index into ModelData::embedded for model-packed textures (glb/fbx).
struct MeshTextureRef {
    std::string uri;                  // external path; empty when embedded
    int         embedded_index = -1;  // index into ModelData::embedded; -1 when external
};

// One drawable range that shares a single material. Indices are into ModelData::indices;
// base_vertex is added to each index (so submeshes can share one vertex pool but keep
// local index ranges when the importer produces them per-mesh).
struct SubMesh {
    std::string name;
    uint32_t    index_offset  = 0;
    uint32_t    index_count   = 0;
    uint32_t    base_vertex   = 0;
    int         material      = -1;   // index into ModelData::materials
    Aabb        bounds;
};

// A texture carried inside the model file. Compressed blobs hold the original file
// bytes (decode with Image::load_from_memory); uncompressed blobs hold raw pixels.
struct EmbeddedTexture {
    std::string          name;
    std::vector<uint8_t> data;
    bool                 is_compressed = true;     // data == png/jpg/... file bytes
    int                  width = 0, height = 0;     // only when !is_compressed
    int                  channels = 4;              // only when !is_compressed
};

// A whole decoded model.
struct ModelData {
    std::vector<MeshVertex>      vertices;
    std::vector<uint32_t>        indices;
    std::vector<SubMesh>         submeshes;
    std::vector<MeshMaterial>    materials;
    std::vector<MeshTextureRef>  textures;     // referenced by materials' *_tex indices
    std::vector<EmbeddedTexture> embedded;     // model-packed image blobs
    Aabb                         bounds;
    std::string                  source_dir;   // directory of the source file (resolves URIs)

    bool   empty()         const { return vertices.empty() || indices.empty(); }
    size_t vertex_bytes()  const { return vertices.size() * sizeof(MeshVertex); }
    size_t index_bytes()   const { return indices.size()  * sizeof(uint32_t); }
    uint32_t triangle_count() const { return uint32_t(indices.size() / 3); }

    // Recompute submesh + model bounds from the current vertices/indices.
    void compute_bounds();
};

} // namespace gfx
} // namespace window
