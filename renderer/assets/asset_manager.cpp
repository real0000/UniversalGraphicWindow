#include "asset_manager.hpp"

#include <cstddef>   // offsetof
#include <cstdint>
#include <vector>

namespace window {
namespace gfx {
namespace {

int mip_count(int w, int h) {
    int m = w > h ? w : h, levels = 1;
    while (m > 1) { m >>= 1; ++levels; }
    return levels;
}

TextureFormat format_for(int channels, ImagePixelType type, bool srgb) {
    if (type == ImagePixelType::F32) {
        switch (channels) {
            case 1:  return TextureFormat::R32_FLOAT;
            case 2:  return TextureFormat::RG32_FLOAT;
            default: return TextureFormat::RGBA32_FLOAT;
        }
    }
    switch (channels) {
        case 1:  return TextureFormat::R8_UNORM;
        case 2:  return TextureFormat::RG8_UNORM;
        default: return srgb ? TextureFormat::RGBA8_UNORM_SRGB : TextureFormat::RGBA8_UNORM;
    }
}

std::string join_path(const std::string& dir, const std::string& file) {
    if (dir.empty()) return file;
    char back = dir.back();
    if (back == '/' || back == '\\') return dir + file;
    return dir + "/" + file;
}

} // namespace

std::string TextureLoadOptions::cache_suffix() const {
    std::string s = "|";
    s += srgb ? 's' : '_';
    s += generate_mipmaps ? 'm' : '_';
    s += flip_vertically ? 'f' : '_';
    s += force_rgba ? 'r' : '_';
    s += hdr ? 'h' : '_';
    return s;
}

VertexLayout GpuModel::vertex_layout() {
    VertexLayout l;
    l.attributes[0] = { 0, VertexFormat::Float3, uint32_t(offsetof(MeshVertex, position)), 0 };
    l.attributes[1] = { 1, VertexFormat::Float3, uint32_t(offsetof(MeshVertex, normal)),   0 };
    l.attributes[2] = { 2, VertexFormat::Float4, uint32_t(offsetof(MeshVertex, tangent)),  0 };
    l.attributes[3] = { 3, VertexFormat::Float2, uint32_t(offsetof(MeshVertex, uv0)),      0 };
    l.attributes[4] = { 4, VertexFormat::Float2, uint32_t(offsetof(MeshVertex, uv1)),      0 };
    l.attributes[5] = { 5, VertexFormat::Float4, uint32_t(offsetof(MeshVertex, color)),    0 };
    l.attribute_count = 6;
    l.strides[0]      = uint32_t(sizeof(MeshVertex));
    l.input_rates[0]  = VertexInputRate::PerVertex;
    l.buffer_count    = 1;
    return l;
}

bool AssetManager::init(GraphicDevice* device) {
    if (!device) { last_error_ = "null device"; return false; }
    device_ = device;
    last_error_.clear();
    return true;
}

void AssetManager::shutdown() {
    destroy_all();
    device_ = nullptr;
}

void AssetManager::clear_cache() { destroy_all(); }

void AssetManager::destroy_all() {
    if (device_) {
        for (auto h : owned_textures_) if (h.valid()) device_->destroy_texture(h);
        for (auto h : owned_buffers_)  if (h.valid()) device_->destroy_buffer(h);
        if (sampler_.valid()) device_->destroy_sampler(sampler_);
    }
    owned_textures_.clear();
    owned_buffers_.clear();
    texture_cache_.clear();
    model_cache_.clear();
    white_ = black_ = normal_ = GpuTexture{};
    sampler_ = SamplerHandle{};
}

GpuTexture AssetManager::upload_pixels(const void* pixels, int w, int h, int channels,
                                       ImagePixelType type, const TextureLoadOptions& opts) {
    if (!device_) { last_error_ = "manager not initialized"; return {}; }
    if (!pixels || w <= 0 || h <= 0 || channels < 1) { last_error_ = "invalid image data"; return {}; }

    const int bpc = (type == ImagePixelType::F32) ? 4 : 1;
    const void* src = pixels;

    // RGB has no common sampled GPU format, so 3-channel data is always expanded;
    // 1/2-channel data is expanded only when force_rgba is set.
    std::vector<uint8_t> expanded;
    int out_ch = channels;
    if (channels == 3 || (opts.force_rgba && channels < 4)) {
        out_ch = 4;
        expanded.resize(size_t(w) * size_t(h) * 4 * bpc);
        const int px = w * h;
        if (type == ImagePixelType::F32) {
            const float* s = static_cast<const float*>(pixels);
            float*       d = reinterpret_cast<float*>(expanded.data());
            for (int i = 0; i < px; ++i, s += channels, d += 4) {
                const float r = s[0];
                d[0] = r;
                d[1] = channels >= 2 ? s[1] : r;
                d[2] = channels >= 3 ? s[2] : (channels == 1 ? r : 0.0f);
                d[3] = 1.0f;
            }
        } else {
            const uint8_t* s = static_cast<const uint8_t*>(pixels);
            uint8_t*       d = expanded.data();
            for (int i = 0; i < px; ++i, s += channels, d += 4) {
                const uint8_t r = s[0];
                d[0] = r;
                d[1] = channels >= 2 ? s[1] : r;
                d[2] = channels >= 3 ? s[2] : (channels == 1 ? r : uint8_t(0));
                d[3] = 255;
            }
        }
        src = expanded.data();
    }

    const int mips = opts.generate_mipmaps ? mip_count(w, h) : 1;

    TextureDesc td;
    td.width      = w;
    td.height     = h;
    td.mip_levels = mips;
    td.format     = format_for(out_ch, type, opts.srgb);
    td.usage      = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_DST;
    if (mips > 1) td.usage |= TEXTURE_USAGE_COPY_SRC;   // generate_mipmaps reads back levels
    td.initial_data = src;
    td.debug_name = "asset_texture";

    TextureHandle tex = device_->create_texture(td);
    if (!tex.valid()) { last_error_ = "create_texture failed"; return {}; }
    if (mips > 1) device_->generate_mipmaps(tex);

    owned_textures_.push_back(tex);

    GpuTexture gt;
    gt.texture    = tex;
    gt.width      = w;
    gt.height     = h;
    gt.mip_levels = mips;
    gt.format     = td.format;
    return gt;
}

GpuTexture AssetManager::create_texture(const Image& img, const TextureLoadOptions& opts) {
    if (!img.valid()) { last_error_ = "invalid image"; return {}; }
    return upload_pixels(img.pixels, img.width, img.height, img.channels, img.type, opts);
}

GpuTexture AssetManager::create_texture_from_memory(const void* data, size_t size,
                                                    const TextureLoadOptions& opts) {
    Image img;
    const int desired = opts.force_rgba ? 4 : 0;
    const bool ok = opts.hdr
        ? Image::load_hdr_from_memory(data, size, &img, desired, opts.flip_vertically)
        : Image::load_from_memory(data, size, &img, desired, opts.flip_vertically);
    if (!ok) { last_error_ = Image::last_error(); return {}; }
    return upload_pixels(img.pixels, img.width, img.height, img.channels, img.type, opts);
}

const GpuTexture* AssetManager::load_texture(const char* path, const TextureLoadOptions& opts) {
    if (!path || !*path) { last_error_ = "empty path"; return nullptr; }
    const std::string key = std::string(path) + opts.cache_suffix();
    auto it = texture_cache_.find(key);
    if (it != texture_cache_.end()) return &it->second;

    Image img;
    const int desired = opts.force_rgba ? 4 : 0;
    const bool ok = opts.hdr
        ? Image::load_hdr(path, &img, desired, opts.flip_vertically)
        : Image::load(path, &img, desired, opts.flip_vertically);
    if (!ok) { last_error_ = std::string("load '") + path + "': " + Image::last_error(); return nullptr; }

    GpuTexture gt = upload_pixels(img.pixels, img.width, img.height, img.channels, img.type, opts);
    if (!gt.valid()) return nullptr;
    auto res = texture_cache_.emplace(key, gt);
    return &res.first->second;
}

GpuTexture AssetManager::solid_texture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t px[4] = { r, g, b, a };
    TextureLoadOptions o;
    o.generate_mipmaps = false;
    o.force_rgba = false;   // already 4-channel
    return upload_pixels(px, 1, 1, 4, ImagePixelType::U8, o);
}

GpuTexture AssetManager::white_texture()  { if (!white_.valid())  white_  = solid_texture(255, 255, 255, 255); return white_;  }
GpuTexture AssetManager::black_texture()  { if (!black_.valid())  black_  = solid_texture(0,   0,   0,   255); return black_;  }
GpuTexture AssetManager::default_normal() { if (!normal_.valid()) normal_ = solid_texture(128, 128, 255, 255); return normal_; }

SamplerHandle AssetManager::default_sampler() {
    if (sampler_.valid() || !device_) return sampler_;
    SamplerState ss;   // trilinear, repeat by default
    ss.max_anisotropy = 16;
    sampler_ = device_->create_sampler(ss);
    return sampler_;
}

GpuTexture AssetManager::resolve_model_texture(const ModelData& data, int tex_index,
                                               const std::string& dir, const TextureLoadOptions& opts,
                                               std::unordered_map<int, GpuTexture>& per_model) {
    if (tex_index < 0 || tex_index >= int(data.textures.size())) return {};
    auto cached = per_model.find(tex_index);
    if (cached != per_model.end()) return cached->second;

    const MeshTextureRef& ref = data.textures[tex_index];
    GpuTexture gt;
    if (ref.embedded_index >= 0 && ref.embedded_index < int(data.embedded.size())) {
        const EmbeddedTexture& emb = data.embedded[ref.embedded_index];
        if (emb.is_compressed) gt = create_texture_from_memory(emb.data.data(), emb.data.size(), opts);
        else                   gt = upload_pixels(emb.data.data(), emb.width, emb.height,
                                                   emb.channels, ImagePixelType::U8, opts);
    } else if (!ref.uri.empty()) {
        const GpuTexture* loaded = load_texture(join_path(dir, ref.uri).c_str(), opts);
        if (loaded) gt = *loaded;
    }
    per_model[tex_index] = gt;
    return gt;
}

GpuModel AssetManager::create_model(const ModelData& data, const TextureLoadOptions& topts) {
    GpuModel model;
    if (!device_) { last_error_ = "manager not initialized"; return model; }
    if (data.empty()) { last_error_ = "empty model data"; return model; }

    // Geometry: one shared vertex + index buffer.
    BufferDesc vbd;
    vbd.size = uint32_t(data.vertex_bytes());
    vbd.type = BufferType::Vertex;
    vbd.stride = uint32_t(sizeof(MeshVertex));
    vbd.initial_data = data.vertices.data();
    vbd.debug_name = "asset_model_vb";
    BufferHandle vb = device_->create_buffer(vbd);

    BufferDesc ibd;
    ibd.size = uint32_t(data.index_bytes());
    ibd.type = BufferType::Index;
    ibd.initial_data = data.indices.data();
    ibd.debug_name = "asset_model_ib";
    BufferHandle ib = device_->create_buffer(ibd);

    if (!vb.valid() || !ib.valid()) {
        last_error_ = "create_buffer failed";
        if (vb.valid()) device_->destroy_buffer(vb);
        if (ib.valid()) device_->destroy_buffer(ib);
        return model;
    }
    owned_buffers_.push_back(vb);
    owned_buffers_.push_back(ib);

    model.vertex_buffer = vb;
    model.index_buffer  = ib;
    model.index_format  = IndexFormat::UInt32;
    model.vertex_count  = uint32_t(data.vertices.size());
    model.index_count   = uint32_t(data.indices.size());
    model.bounds        = data.bounds;

    model.submeshes.reserve(data.submeshes.size());
    for (const SubMesh& s : data.submeshes) {
        GpuSubMesh g;
        g.name = s.name;
        g.index_offset = s.index_offset;
        g.index_count  = s.index_count;
        g.base_vertex  = int32_t(s.base_vertex);
        g.material     = s.material;
        g.bounds       = s.bounds;
        model.submeshes.push_back(std::move(g));
    }

    // Materials + their textures (sRGB for colour data, linear for the rest).
    std::unordered_map<int, GpuTexture> per_model;
    TextureLoadOptions color = topts; color.srgb = true;
    TextureLoadOptions linear = topts; linear.srgb = false;

    model.materials.reserve(data.materials.size());
    for (const MeshMaterial& m : data.materials) {
        GpuMaterial g;
        g.name               = m.name;
        g.base_color_factor  = m.base_color;
        g.emissive_factor    = m.emissive;
        g.metallic           = m.metallic;
        g.roughness          = m.roughness;
        g.alpha_cutoff       = m.alpha_cutoff;
        g.alpha_mode         = m.alpha_mode;
        g.double_sided       = m.double_sided;
        g.base_color         = resolve_model_texture(data, m.base_color_tex,         data.source_dir, color,  per_model);
        g.emissive           = resolve_model_texture(data, m.emissive_tex,           data.source_dir, color,  per_model);
        g.normal             = resolve_model_texture(data, m.normal_tex,             data.source_dir, linear, per_model);
        g.metallic_roughness = resolve_model_texture(data, m.metallic_roughness_tex, data.source_dir, linear, per_model);
        g.occlusion          = resolve_model_texture(data, m.occlusion_tex,          data.source_dir, linear, per_model);
        model.materials.push_back(std::move(g));
    }

    last_error_.clear();
    return model;
}

const GpuModel* AssetManager::load_model(const char* path, const ModelLoadOptions& mopts,
                                         const TextureLoadOptions& topts) {
    if (!path || !*path) { last_error_ = "empty path"; return nullptr; }
    auto it = model_cache_.find(path);
    if (it != model_cache_.end()) return &it->second;

    ModelData data;
    std::string err;
    if (!gfx::load_model(path, &data, mopts, &err)) { last_error_ = err; return nullptr; }

    GpuModel model = create_model(data, topts);
    if (!model.valid()) return nullptr;
    auto res = model_cache_.emplace(path, std::move(model));
    return &res.first->second;
}

} // namespace gfx
} // namespace window
