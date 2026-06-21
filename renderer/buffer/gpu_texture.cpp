// renderer/buffer/gpu_texture.cpp — see gpu_texture.hpp.

#include "gpu_texture.hpp"

namespace window {
namespace gfx {

static inline int mip_extent(int base, int mip) {
    int v = base >> mip;
    return v > 0 ? v : 1;
}

//=============================================================================
// TextureView
//=============================================================================

void TextureView::move_from(TextureView& o) noexcept {
    device_ = o.device_;
    handle_ = o.handle_;
    desc_   = o.desc_;
    owns_   = o.owns_;
    o.device_ = nullptr;
    o.handle_ = TextureHandle{};
    o.desc_   = TextureViewDesc{};
    o.owns_   = false;
}

TextureView& TextureView::operator=(TextureView&& o) noexcept {
    if (this != &o) {
        destroy();
        move_from(o);
    }
    return *this;
}

void TextureView::destroy() {
    // A view is itself a TextureHandle; the RHI frees a real view via destroy_texture().
    // Only free what we own — a backend that aliases the source (D3D11/D3D12) must not be
    // double-freed here; the owning Texture releases the real resource.
    if (owns_ && device_ && handle_.valid()) device_->destroy_texture(handle_);
    device_ = nullptr;
    handle_ = TextureHandle{};
    desc_   = TextureViewDesc{};
    owns_   = false;
}

//=============================================================================
// Texture
//=============================================================================

void Texture::move_from(Texture& o) noexcept {
    device_ = o.device_;
    handle_ = o.handle_;
    desc_   = o.desc_;
    o.device_ = nullptr;
    o.handle_ = TextureHandle{};
    o.desc_   = TextureDesc{};
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        destroy();
        move_from(o);
    }
    return *this;
}

bool Texture::create(GraphicDevice* device, const TextureDesc& desc) {
    destroy();
    if (!device) return false;
    TextureHandle h = device->create_texture(desc);
    if (!h.valid()) return false;
    device_ = device;
    handle_ = h;
    desc_   = desc;
    desc_.initial_data = nullptr;   // don't retain the caller's pointer past creation
    return true;
}

bool Texture::create_2d(GraphicDevice* device, int width, int height, TextureFormat format,
                        const void* data, int mip_levels, uint32_t usage, const char* debug_name) {
    TextureDesc d;
    d.width        = width;
    d.height       = height;
    d.mip_levels   = mip_levels;
    d.format       = format;
    d.usage        = usage;
    d.initial_data = data;
    d.debug_name   = debug_name;
    return create(device, d);
}

void Texture::destroy() {
    if (device_ && handle_.valid()) device_->destroy_texture(handle_);
    device_ = nullptr;
    handle_ = TextureHandle{};
    desc_   = TextureDesc{};
}

void Texture::update(const TextureRegion& region, const void* data) {
    if (device_ && handle_.valid()) device_->update_texture(handle_, region, data);
}

void Texture::update_mip(const void* data, int mip, int layer) {
    if (!device_ || !handle_.valid()) return;
    TextureRegion r;
    r.x = r.y = r.z = 0;
    r.width  = mip_extent(desc_.width,  mip);
    r.height = mip_extent(desc_.height, mip);
    r.depth  = mip_extent(desc_.depth,  mip);
    r.mip    = mip;
    r.layer  = layer;
    device_->update_texture(handle_, r, data);
}

void Texture::generate_mipmaps() {
    if (device_ && handle_.valid()) device_->generate_mipmaps(handle_);
}

void Texture::read(const TextureRegion& region, void* dst) {
    if (device_ && handle_.valid()) device_->read_texture(handle_, region, dst);
}

//---- Views ------------------------------------------------------------------

TextureView Texture::create_view(const TextureViewDesc& vdesc) {
    TextureView v;
    if (!device_ || !handle_.valid()) return v;
    TextureViewDesc d = vdesc;
    d.texture = handle_;
    TextureHandle h = device_->create_texture_view(d);
    if (!h.valid()) return v;
    v.device_ = device_;
    v.handle_ = h;
    v.desc_   = d;
    // Backends without real view objects (D3D11/D3D12) return the source handle itself.
    // Such a view is a safe alias and must NOT be separately destroyed.
    v.owns_   = !(h == handle_);
    return v;
}

TextureView Texture::create_view(TextureFormat format, int base_mip, int mip_count,
                                 int base_layer, int layer_count, bool cube) {
    TextureViewDesc d;
    d.format      = format;
    d.base_mip    = base_mip;
    d.mip_count   = mip_count;
    d.base_layer  = base_layer;
    d.layer_count = layer_count;
    d.cube        = cube;
    return create_view(d);
}

TextureView Texture::mip_view(int mip) {
    return create_view(TextureFormat::Unknown, mip, 1, 0, 0, false);
}

TextureView Texture::layer_view(int base_layer, int layer_count) {
    return create_view(TextureFormat::Unknown, 0, 0, base_layer, layer_count, false);
}

TextureView Texture::format_view(TextureFormat format) {
    return create_view(format, 0, 0, 0, 0, false);
}

TextureView Texture::cube_view() {
    return create_view(TextureFormat::Unknown, 0, 0, 0, 6, true);
}

} // namespace gfx
} // namespace window
