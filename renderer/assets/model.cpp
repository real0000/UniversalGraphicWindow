#include "model.hpp"

#ifdef WINDOW_SUPPORT_MODEL_LOADER

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
#include <assimp/config.h>

#include <cmath>
#include <cstdlib>   // std::atoi
#include <utility>

namespace window {
namespace gfx {
namespace {

// --- aiMatrix4x4 (row-major) transforms ------------------------------------
math::Vec3 xform_point(const aiMatrix4x4& m, const aiVector3D& v) {
    return math::Vec3(
        m.a1 * v.x + m.a2 * v.y + m.a3 * v.z + m.a4,
        m.b1 * v.x + m.b2 * v.y + m.b3 * v.z + m.b4,
        m.c1 * v.x + m.c2 * v.y + m.c3 * v.z + m.c4);
}
math::Vec3 xform_dir(const aiMatrix4x4& m, const aiVector3D& v) {  // ignores translation
    return math::Vec3(
        m.a1 * v.x + m.a2 * v.y + m.a3 * v.z,
        m.b1 * v.x + m.b2 * v.y + m.b3 * v.z,
        m.c1 * v.x + m.c2 * v.y + m.c3 * v.z);
}
math::Vec3 normalize_safe(const math::Vec3& v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 1e-8f ? math::Vec3(v.x / len, v.y / len, v.z / len) : math::Vec3(0, 0, 1);
}

inline math::Vec4 to_vec4(const aiColor4D& c) { return math::Vec4(c.r, c.g, c.b, c.a); }
inline math::Vec3 to_vec3(const aiColor3D& c) { return math::Vec3(c.r, c.g, c.b); }

// Register the texture referenced by `mat`'s first texture of any of the given types.
// Resolves Assimp's "*N" embedded references against scene->mTextures, dedups by the
// raw reference string, and returns an index into out->textures (or -1 if none).
struct TextureRegistry {
    ModelData* out;
    const aiScene* scene;
    std::vector<std::pair<std::string, int>> seen;   // ref-string -> texture index

    int resolve(const std::string& ref) {
        for (auto& s : seen) if (s.first == ref) return s.second;

        MeshTextureRef tref;
        if (!ref.empty() && ref[0] == '*') {
            // Embedded: "*N" indexes scene->mTextures[N].
            const int n = std::atoi(ref.c_str() + 1);
            if (scene && n >= 0 && n < int(scene->mNumTextures)) {
                const aiTexture* t = scene->mTextures[n];
                EmbeddedTexture emb;
                emb.name = t->mFilename.C_Str();
                if (t->mHeight == 0) {
                    // Compressed blob (png/jpg/...); mWidth is the byte size.
                    emb.is_compressed = true;
                    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(t->pcData);
                    emb.data.assign(bytes, bytes + t->mWidth);
                } else {
                    // Raw aiTexel pixels (BGRA) -> RGBA8.
                    emb.is_compressed = false;
                    emb.width = int(t->mWidth);
                    emb.height = int(t->mHeight);
                    emb.channels = 4;
                    const int px = int(t->mWidth) * int(t->mHeight);
                    emb.data.resize(size_t(px) * 4);
                    for (int i = 0; i < px; ++i) {
                        const aiTexel& tx = t->pcData[i];
                        emb.data[i * 4 + 0] = tx.r;
                        emb.data[i * 4 + 1] = tx.g;
                        emb.data[i * 4 + 2] = tx.b;
                        emb.data[i * 4 + 3] = tx.a;
                    }
                }
                tref.embedded_index = int(out->embedded.size());
                out->embedded.push_back(std::move(emb));
            }
        } else {
            tref.uri = ref;   // external file, relative to source_dir
        }

        const int idx = int(out->textures.size());
        out->textures.push_back(std::move(tref));
        seen.emplace_back(ref, idx);
        return idx;
    }

    int from_material(const aiMaterial* mat, std::initializer_list<aiTextureType> types) {
        for (aiTextureType type : types) {
            if (mat->GetTextureCount(type) == 0) continue;
            aiString path;
            if (mat->GetTexture(type, 0, &path) == AI_SUCCESS && path.length > 0)
                return resolve(std::string(path.C_Str()));
        }
        return -1;
    }
};

void convert_material(const aiMaterial* m, MeshMaterial* out, TextureRegistry& tex) {
    aiString name;
    if (m->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) out->name = name.C_Str();

    aiColor4D base;
    if (m->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS) out->base_color = to_vec4(base);
    else if (m->Get(AI_MATKEY_COLOR_DIFFUSE, base) == AI_SUCCESS) out->base_color = to_vec4(base);

    aiColor3D emissive;
    if (m->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) out->emissive = to_vec3(emissive);

    float f = 0.0f;
    if (m->Get(AI_MATKEY_METALLIC_FACTOR, f) == AI_SUCCESS)  out->metallic = f;
    if (m->Get(AI_MATKEY_ROUGHNESS_FACTOR, f) == AI_SUCCESS) out->roughness = f;
    if (m->Get(AI_MATKEY_OPACITY, f) == AI_SUCCESS)          out->base_color.w = f;

    int two_sided = 0;
    if (m->Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) out->double_sided = two_sided != 0;

    // glTF alpha mode/cutoff (raw keys; absent on non-glTF imports).
    aiString alpha_mode;
    if (m->Get("$mat.gltf.alphaMode", 0, 0, alpha_mode) == AI_SUCCESS) {
        const std::string am = alpha_mode.C_Str();
        if (am == "MASK")  out->alpha_mode = AlphaMode::Mask;
        else if (am == "BLEND") out->alpha_mode = AlphaMode::Blend;
        else out->alpha_mode = AlphaMode::Opaque;
    }
    float cutoff = 0.0f;
    if (m->Get("$mat.gltf.alphaCutoff", 0, 0, cutoff) == AI_SUCCESS) out->alpha_cutoff = cutoff;

    out->base_color_tex         = tex.from_material(m, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
    out->normal_tex             = tex.from_material(m, { aiTextureType_NORMALS, aiTextureType_HEIGHT });
    out->metallic_roughness_tex = tex.from_material(m, { aiTextureType_METALNESS,
                                                         aiTextureType_DIFFUSE_ROUGHNESS,
                                                         aiTextureType_UNKNOWN });
    out->emissive_tex           = tex.from_material(m, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR });
    out->occlusion_tex          = tex.from_material(m, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });
}

// Append one aiMesh, baked into world space by `world`, as a SubMesh of `out`.
void append_mesh(const aiMesh* mesh, const aiMatrix4x4& world, bool import_colors, ModelData* out) {
    if (mesh->mNumVertices == 0 || mesh->mNumFaces == 0) return;

    // Normal matrix: inverse-transpose of the upper 3x3 keeps normals correct under
    // non-uniform scale.
    aiMatrix4x4 normal_mat = world;
    normal_mat.Inverse();
    normal_mat.Transpose();

    const uint32_t vbase = uint32_t(out->vertices.size());
    SubMesh sm;
    sm.name = mesh->mName.C_Str();
    sm.material = int(mesh->mMaterialIndex);
    sm.index_offset = uint32_t(out->indices.size());
    sm.base_vertex = 0;   // indices are stored already offset by vbase (global pool)

    const bool has_normals  = mesh->HasNormals();
    const bool has_tangents = mesh->HasTangentsAndBitangents();
    const bool has_uv0      = mesh->HasTextureCoords(0);
    const bool has_uv1      = mesh->HasTextureCoords(1);
    const bool has_colors   = import_colors && mesh->HasVertexColors(0);

    out->vertices.reserve(out->vertices.size() + mesh->mNumVertices);
    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        MeshVertex v;
        v.position = xform_point(world, mesh->mVertices[i]);
        if (has_normals) v.normal = normalize_safe(xform_dir(normal_mat, mesh->mNormals[i]));
        if (has_tangents) {
            const math::Vec3 t = normalize_safe(xform_dir(world, mesh->mTangents[i]));
            const math::Vec3 b = xform_dir(world, mesh->mBitangents[i]);
            // Reconstruct handedness: cross(N,T) should align with B for w = +1.
            const math::Vec3& n = v.normal;
            const math::Vec3 nt(n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x);
            const float sign = (nt.x * b.x + nt.y * b.y + nt.z * b.z) < 0.0f ? -1.0f : 1.0f;
            v.tangent = math::Vec4(t.x, t.y, t.z, sign);
        }
        if (has_uv0) v.uv0 = math::Vec2(mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y);
        if (has_uv1) v.uv1 = math::Vec2(mesh->mTextureCoords[1][i].x, mesh->mTextureCoords[1][i].y);
        if (has_colors) {
            const aiColor4D& c = mesh->mColors[0][i];
            v.color = math::Vec4(c.r, c.g, c.b, c.a);
        }
        out->vertices.push_back(v);
    }

    out->indices.reserve(out->indices.size() + mesh->mNumFaces * 3);
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) continue;   // triangulated; skip stray points/lines
        out->indices.push_back(vbase + face.mIndices[0]);
        out->indices.push_back(vbase + face.mIndices[1]);
        out->indices.push_back(vbase + face.mIndices[2]);
    }

    sm.index_count = uint32_t(out->indices.size()) - sm.index_offset;
    if (sm.index_count > 0) out->submeshes.push_back(std::move(sm));
    else out->vertices.resize(vbase);   // rolled-back: mesh had no triangles
}

void process_node(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent,
                  bool import_colors, ModelData* out) {
    const aiMatrix4x4 world = parent * node->mTransformation;
    for (unsigned i = 0; i < node->mNumMeshes; ++i)
        append_mesh(scene->mMeshes[node->mMeshes[i]], world, import_colors, out);
    for (unsigned i = 0; i < node->mNumChildren; ++i)
        process_node(scene, node->mChildren[i], world, import_colors, out);
}

unsigned build_flags(const ModelLoadOptions& o) {
    unsigned flags = aiProcess_SortByPType | aiProcess_GenUVCoords | aiProcess_ImproveCacheLocality
                   | aiProcess_RemoveRedundantMaterials | aiProcess_FindInvalidData;
    if (o.triangulate)             flags |= aiProcess_Triangulate;
    if (o.generate_normals)        flags |= aiProcess_GenSmoothNormals;
    if (o.calc_tangents)           flags |= aiProcess_CalcTangentSpace;
    if (o.flip_uvs)                flags |= aiProcess_FlipUVs;
    if (o.join_identical_vertices) flags |= aiProcess_JoinIdenticalVertices;
    if (o.optimize_meshes)         flags |= aiProcess_OptimizeMeshes;
    if (o.pretransform)            flags |= aiProcess_PreTransformVertices;
    if (o.flip_winding)            flags |= aiProcess_FlipWindingOrder;
    if (o.make_left_handed)        flags |= aiProcess_MakeLeftHanded;
    if (o.global_scale != 1.0f)    flags |= aiProcess_GlobalScale;
    return flags;
}

bool finish(const aiScene* scene, Assimp::Importer& imp, ModelData* out, std::string* err) {
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        if (err) *err = imp.GetErrorString();
        return false;
    }
    TextureRegistry reg{ out, scene, {} };
    out->materials.resize(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        convert_material(scene->mMaterials[i], &out->materials[i], reg);

    process_node(scene, scene->mRootNode, aiMatrix4x4(), true, out);
    out->compute_bounds();

    if (out->empty()) { if (err) *err = "model contains no triangle geometry"; return false; }
    return true;
}

} // namespace

bool load_model(const char* path, ModelData* out, const ModelLoadOptions& options, std::string* err) {
    if (!path || !out) { if (err) *err = "null argument"; return false; }
    *out = ModelData{};

    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, options.global_scale);
    const aiScene* scene = importer.ReadFile(path, build_flags(options));

    // source_dir = directory portion of `path` (for resolving relative texture URIs).
    std::string p(path);
    const size_t slash = p.find_last_of("/\\");
    out->source_dir = (slash == std::string::npos) ? std::string() : p.substr(0, slash);

    return finish(scene, importer, out, err);
}

bool load_model_from_memory(const void* data, size_t size, const char* format_hint,
                            ModelData* out, const ModelLoadOptions& options, std::string* err) {
    if (!data || !size || !out) { if (err) *err = "null/empty buffer"; return false; }
    *out = ModelData{};

    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_GLOBAL_SCALE_FACTOR_KEY, options.global_scale);
    const aiScene* scene = importer.ReadFileFromMemory(data, size, build_flags(options),
                                                       format_hint ? format_hint : "");
    return finish(scene, importer, out, err);
}

bool model_loader_available() { return true; }

std::string supported_model_extensions() {
    Assimp::Importer importer;
    aiString list;
    importer.GetExtensionList(list);
    // Assimp returns "*.obj;*.fbx;..."; normalize to "obj fbx ...".
    std::string s(list.C_Str()), out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '*' || c == '.') continue;
        out += (c == ';') ? ' ' : c;
    }
    return out;
}

} // namespace gfx
} // namespace window

#else  // !WINDOW_SUPPORT_MODEL_LOADER ---------------------------------------

namespace window {
namespace gfx {

static const char* kUnavailable =
    "model loader unavailable (build with -DWINDOW_ENABLE_MODEL_LOADER=ON to fetch Assimp)";

bool load_model(const char*, ModelData*, const ModelLoadOptions&, std::string* err) {
    if (err) *err = kUnavailable;
    return false;
}
bool load_model_from_memory(const void*, size_t, const char*, ModelData*,
                            const ModelLoadOptions&, std::string* err) {
    if (err) *err = kUnavailable;
    return false;
}
bool model_loader_available() { return false; }
std::string supported_model_extensions() { return std::string(); }

} // namespace gfx
} // namespace window

#endif // WINDOW_SUPPORT_MODEL_LOADER
