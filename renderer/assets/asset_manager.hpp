#pragma once
// AssetManager — loads, caches and GPU-uploads the asset types decoded by image.hpp
// and model.hpp, and owns the lifetime of every GPU resource it creates.
//
//   * Textures: decode an image (any format stb handles) and upload it as a sampled
//     texture, optionally generating a full mip chain. Cached by path+options so the
//     same file uploads once.
//   * Models: decode a model file (any format Assimp handles), upload one shared
//     vertex/index buffer, and resolve every material's textures (external files and
//     model-embedded blobs) into GPU textures. Cached by path.
//
// All handles the manager hands out are owned by it; destroy them by calling
// shutdown() (or clear_cache() to drop everything but keep the manager alive). Do not
// pass them to GraphicDevice::destroy_*.

#include "../../graphics_api.hpp"
#include "image.hpp"
#include "mesh.hpp"
#include "model.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace window {
namespace gfx {

struct TextureLoadOptions {
    bool srgb             = false;   // tag colour data as sRGB (albedo/emissive)
    bool generate_mipmaps = true;
    bool flip_vertically  = false;
    bool force_rgba       = true;    // expand 1/2/3-channel data to RGBA (GPUs rarely sample RGB8)
    bool hdr              = false;   // decode as 32-bit float (Radiance/.hdr environment maps)

    // A small key used to dedup cache entries that share a path but differ in options.
    std::string cache_suffix() const;
};

struct GpuTexture {
    TextureHandle texture;
    int           width = 0, height = 0;
    int           mip_levels = 1;
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    bool valid() const { return texture.valid(); }
};

struct GpuSubMesh {
    std::string name;
    uint32_t    index_offset = 0;
    uint32_t    index_count  = 0;
    int32_t     base_vertex  = 0;
    int         material     = -1;   // index into GpuModel::materials
    Aabb        bounds;
};

// A material with its textures resolved to GPU handles. Absent textures are invalid
// handles; the caller can substitute white_texture()/default_normal() when binding.
struct GpuMaterial {
    std::string name;
    GpuTexture  base_color;
    GpuTexture  normal;
    GpuTexture  metallic_roughness;
    GpuTexture  emissive;
    GpuTexture  occlusion;
    math::Vec4  base_color_factor = math::Vec4(1, 1, 1, 1);
    math::Vec3  emissive_factor   = math::Vec3(0, 0, 0);
    float       metallic    = 1.0f;
    float       roughness   = 1.0f;
    float       alpha_cutoff = 0.5f;
    AlphaMode   alpha_mode  = AlphaMode::Opaque;
    bool        double_sided = false;
};

// One GPU-resident model: a shared vertex buffer (MeshVertex layout) + index buffer,
// the submesh ranges, and the resolved materials. Bind the buffers once, then for each
// submesh bind its material and draw_indexed(index_count, index_offset, base_vertex).
struct GpuModel {
    BufferHandle vertex_buffer;
    BufferHandle index_buffer;
    IndexFormat  index_format = IndexFormat::UInt32;
    uint32_t     vertex_count = 0;
    uint32_t     index_count  = 0;
    std::vector<GpuSubMesh>  submeshes;
    std::vector<GpuMaterial> materials;
    Aabb         bounds;
    bool valid() const { return vertex_buffer.valid() && index_buffer.valid(); }

    // The interleaved vertex layout these buffers use (pos/normal/tangent/uv0/uv1/color),
    // ready to drop into a PipelineDesc. Static; depends only on MeshVertex.
    static VertexLayout vertex_layout();
};

class AssetManager {
public:
    bool init(GraphicDevice* device);
    void shutdown();                       // destroy every GPU resource + clear caches
    GraphicDevice* device() const { return device_; }

    // --- Textures -------------------------------------------------------------
    // Decode + upload `path`, caching by path+options. Returns nullptr on failure
    // (see last_error()); the returned pointer stays valid until clear_cache()/shutdown.
    const GpuTexture* load_texture(const char* path, const TextureLoadOptions& opts = {});

    // Upload an already-decoded image (not cached). Owned by the manager.
    GpuTexture create_texture(const Image& img, const TextureLoadOptions& opts = {});
    // Decode encoded bytes (png/jpg/...) from memory and upload (not cached).
    GpuTexture create_texture_from_memory(const void* data, size_t size,
                                          const TextureLoadOptions& opts = {});

    // --- Models ---------------------------------------------------------------
    // Decode + upload `path`, caching by path. Uploads geometry and resolves all
    // material textures (cached like load_texture, relative to the model's directory).
    const GpuModel* load_model(const char* path, const ModelLoadOptions& mopts = {},
                               const TextureLoadOptions& topts = {});
    // Upload already-decoded ModelData (not cached). Resolves textures via data.source_dir.
    GpuModel create_model(const ModelData& data, const TextureLoadOptions& topts = {});

    // --- Fallback textures (1x1, cached) --------------------------------------
    GpuTexture white_texture();    // opaque white  (1,1,1,1)
    GpuTexture black_texture();    // opaque black  (0,0,0,1)
    GpuTexture default_normal();   // flat normal   (0.5,0.5,1,1)

    // A reusable trilinear-repeat sampler owned by the manager.
    SamplerHandle default_sampler();

    // --- Cache management -----------------------------------------------------
    void clear_cache();            // destroy all cached + created resources (keeps manager usable)
    size_t cached_texture_count() const { return texture_cache_.size(); }
    size_t cached_model_count()   const { return model_cache_.size(); }

    const std::string& last_error() const { return last_error_; }

private:
    // Upload raw, tightly-packed pixels. Expands 3-channel (and, when force_rgba,
    // 1/2-channel) data to RGBA; generates mips per opts. Tracks the texture for teardown.
    GpuTexture upload_pixels(const void* pixels, int w, int h, int channels,
                             ImagePixelType type, const TextureLoadOptions& opts);
    GpuTexture solid_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    // Resolve a model texture slot (external uri / embedded blob) to a GPU texture,
    // deduping within one model via `per_model` (keyed by texture index).
    GpuTexture resolve_model_texture(const ModelData& data, int tex_index,
                                     const std::string& dir, const TextureLoadOptions& opts,
                                     std::unordered_map<int, GpuTexture>& per_model);
    void destroy_all();

    GraphicDevice* device_ = nullptr;
    std::string    last_error_;

    std::unordered_map<std::string, GpuTexture> texture_cache_;   // key = path + options
    std::unordered_map<std::string, GpuModel>   model_cache_;     // key = path

    // Every handle the manager created, for teardown (cached + uncached + fallbacks).
    std::vector<TextureHandle> owned_textures_;
    std::vector<BufferHandle>  owned_buffers_;

    GpuTexture    white_, black_, normal_;
    SamplerHandle sampler_;
};

} // namespace gfx
} // namespace window
