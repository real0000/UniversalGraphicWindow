#pragma once
// renderer/buffer/gpu_texture.hpp
//
// RAII wrappers over the backend-neutral GraphicDevice texture resources, with the
// "one resource, many views" feature realised natively: the RHI's create_texture_view()
// produces a reinterpreting window (a different format and/or a mip / array-layer
// sub-range, optionally a cubemap) onto an existing texture, and TextureView owns and
// frees that view handle. A single Texture can hold many live views at once — e.g. an
// sRGB-reinterpret view for sampling colour, a single-mip view as a compute target, a
// per-face view of a cubemap, or a per-layer slice of an array.
//
// Lives in window::gfx alongside the buffer wrappers (gpu_buffer.hpp). Both a Texture
// and a TextureView convert to TextureHandle, so either can be bound wherever the RHI
// or higher-level code expects a texture.

#include "../../graphics_api.hpp"

#include <cstdint>
#include <utility>

namespace window {
namespace gfx {

//=============================================================================
// TextureView — a reinterpreting window onto a Texture (owns the RHI view handle).
//=============================================================================

class TextureView {
public:
    TextureView() = default;
    ~TextureView() { destroy(); }
    TextureView(TextureView&& o) noexcept { move_from(o); }
    TextureView& operator=(TextureView&& o) noexcept;
    TextureView(const TextureView&) = delete;
    TextureView& operator=(const TextureView&) = delete;

    bool          valid()  const { return handle_.valid(); }
    TextureHandle handle() const { return handle_; }
    operator TextureHandle() const { return handle_; }    // bind directly with a view
    const TextureViewDesc& desc() const { return desc_; }
    // True when this view is a distinct RHI object the wrapper owns and frees. False when
    // the backend returned the source texture itself as the "view" (D3D11/D3D12 don't make
    // separate view handles): then the view is a safe alias of the source — bindable, but
    // not separately destroyed (the owning Texture frees the real resource).
    bool          owns() const { return owns_; }
    void destroy();

private:
    friend class Texture;
    void move_from(TextureView& o) noexcept;

    GraphicDevice*  device_ = nullptr;
    TextureHandle   handle_;
    TextureViewDesc desc_{};
    bool            owns_ = false;
};

//=============================================================================
// Texture — RAII owner of one GraphicDevice texture (2D / 3D / array / cube / MSAA).
//=============================================================================

class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }
    Texture(Texture&& o) noexcept { move_from(o); }
    Texture& operator=(Texture&& o) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    bool create(GraphicDevice* device, const TextureDesc& desc);
    // Common sampled 2D texture. `data` (optional) uploads mip 0 / layer 0, tightly packed.
    bool create_2d(GraphicDevice* device, int width, int height,
                   TextureFormat format = TextureFormat::RGBA8_UNORM,
                   const void* data = nullptr, int mip_levels = 1,
                   uint32_t usage = TEXTURE_USAGE_SAMPLED, const char* debug_name = nullptr);
    void destroy();

    bool          valid()  const { return handle_.valid(); }
    TextureHandle handle() const { return handle_; }
    operator TextureHandle() const { return handle_; }
    const TextureDesc& desc() const { return desc_; }
    int           width()  const { return desc_.width; }
    int           height() const { return desc_.height; }
    int           depth()  const { return desc_.depth; }
    int           mip_levels()   const { return desc_.mip_levels; }
    int           array_layers() const { return desc_.array_layers; }
    TextureFormat format() const { return desc_.format; }
    GraphicDevice* device() const { return device_; }

    // ---- Data transfer ------------------------------------------------------
    void update(const TextureRegion& region, const void* data);
    // Upload a whole mip/layer (tightly packed). Region covers the mip's full extent.
    void update_mip(const void* data, int mip = 0, int layer = 0);
    void generate_mipmaps();
    void read(const TextureRegion& region, void* dst);

    // ---- Views (one texture → many reinterpreting windows) ------------------
    // Each call allocates a fresh RHI view that the returned TextureView frees.
    TextureView create_view(const TextureViewDesc& vdesc);
    TextureView create_view(TextureFormat format = TextureFormat::Unknown,
                            int base_mip = 0, int mip_count = 0,
                            int base_layer = 0, int layer_count = 0, bool cube = false);
    TextureView mip_view(int mip);                          // a single mip level (all layers)
    TextureView layer_view(int base_layer, int layer_count = 1);  // an array-layer / cube-face slice
    TextureView format_view(TextureFormat format);          // reinterpret the whole texture
    TextureView cube_view();                                // view the 6 layers as a cubemap

private:
    void move_from(Texture& o) noexcept;

    GraphicDevice* device_ = nullptr;
    TextureHandle  handle_;
    TextureDesc    desc_{};
};

} // namespace gfx
} // namespace window
