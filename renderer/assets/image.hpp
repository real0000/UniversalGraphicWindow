#pragma once
// Image — CPU-side decode of common image files into a tightly-packed pixel buffer,
// via stb_image. Handles PNG, JPEG, BMP, TGA, GIF (first frame), HDR (Radiance),
// PSD, PIC and PNM (PPM/PGM). 8-bit (LDR) sources decode to 8 bits/channel; .hdr /
// Radiance sources decode to 32-bit float via load_hdr().
//
// The result owns its pixels (freed in the destructor; the type is move-only so a
// decoded image is cheap to hand around). Pair with AssetManager to upload to the GPU.

#include "../../graphics_api.hpp"   // TextureFormat

#include <cstddef>
#include <cstdint>

namespace window {
namespace gfx {

enum class ImagePixelType : uint8_t {
    U8,   // 8 bits per channel, unsigned normalized  (stbi_load)
    F32   // 32-bit float per channel (HDR)           (stbi_loadf)
};

// A decoded, CPU-resident image. `pixels` is tightly packed (no row padding),
// row-major, with the origin at the top-left unless decoded with flip_vertically.
struct Image {
    int            width    = 0;
    int            height   = 0;
    int            channels = 0;                  // actual channels in `pixels` (1..4)
    ImagePixelType type     = ImagePixelType::U8;
    uint8_t*       pixels   = nullptr;            // raw bytes (cast to float* when type == F32)

    Image() = default;
    ~Image();
    Image(Image&&) noexcept;
    Image& operator=(Image&&) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    bool   valid()       const { return pixels && width > 0 && height > 0 && channels > 0; }
    int    bytes_per_channel() const { return type == ImagePixelType::F32 ? 4 : 1; }
    size_t pixel_size()  const { return size_t(channels) * bytes_per_channel(); }
    size_t row_pitch()   const { return size_t(width) * pixel_size(); }
    size_t byte_size()   const { return row_pitch() * size_t(height); }
    float* pixels_f()    const { return reinterpret_cast<float*>(pixels); }

    // Best-fit gfx format for this image's channel count / pixel type. 3-channel
    // sources map to a 4-channel format (GPUs rarely sample RGB8); expand the data
    // to 4 channels before upload (AssetManager / expand_to_rgba do this for you).
    TextureFormat texture_format(bool srgb = false) const;

    void reset();   // free pixels and clear to an empty image

    // --- Loaders --------------------------------------------------------------
    // desired_channels: 0 keeps the source channels; 1..4 forces that many (stb fills
    // a missing alpha with opaque, missing colour by replicating, etc.). On failure
    // *out is left empty and false is returned (see last_error()).
    //
    // load/load_from_memory/load_hdr auto-detect EXR and DDS by magic and route to the
    // dedicated decoders below; EXR and float/BC6H DDS therefore come back as F32 even
    // through load() (the Image carries its own pixel type). desired_channels is ignored
    // for EXR/DDS (always RGBA).
    static bool load(const char* path, Image* out,
                     int desired_channels = 0, bool flip_vertically = false);
    static bool load_from_memory(const void* data, size_t size, Image* out,
                                 int desired_channels = 0, bool flip_vertically = false);

    // 32-bit float decode (HDR). Non-HDR sources are promoted to float (0..1).
    static bool load_hdr(const char* path, Image* out,
                         int desired_channels = 0, bool flip_vertically = false);
    static bool load_hdr_from_memory(const void* data, size_t size, Image* out,
                                     int desired_channels = 0, bool flip_vertically = false);

    // OpenEXR (.exr) — always decodes to 32-bit float RGBA. Available when the build
    // enables WINDOW_ENABLE_EXR (otherwise returns false and exr_supported() is false).
    static bool load_exr(const char* path, Image* out, bool flip_vertically = false);
    static bool load_exr_from_memory(const void* data, size_t size, Image* out,
                                     bool flip_vertically = false);
    static bool exr_supported();

    // DDS (DirectDraw Surface) — decodes the top mip to RGBA. BC1/BC2/BC3/BC4/BC5/BC7
    // and the common uncompressed layouts decode to RGBA8; BC6H and float layouts decode
    // to RGBA32F. CPU-decoded so the result uploads on every backend. Available when the
    // build enables WINDOW_ENABLE_DDS (otherwise returns false, dds_supported() false).
    static bool load_dds(const char* path, Image* out, bool flip_vertically = false);
    static bool load_dds_from_memory(const void* data, size_t size, Image* out,
                                     bool flip_vertically = false);
    static bool dds_supported();

    // KTX / KTX2 (.ktx/.ktx2) — decodes the top mip to RGBA. Carries BC1-7/BC6H, ETC2/
    // EAC, ASTC and uncompressed payloads; each is CPU-decoded via the matching codec
    // (the corresponding WINDOW_ENABLE_DDS/ETC2/ASTC must be built in). KTX2 supports
    // only un-supercompressed payloads (basis/zstd are rejected with an error).
    static bool load_ktx(const char* path, Image* out, bool flip_vertically = false);
    static bool load_ktx_from_memory(const void* data, size_t size, Image* out,
                                     bool flip_vertically = false);

    // Raw ARM .astc files (ASTC LDR -> RGBA8). Available when WINDOW_ENABLE_ASTC is built.
    static bool load_astc(const char* path, Image* out, bool flip_vertically = false);
    static bool load_astc_from_memory(const void* data, size_t size, Image* out,
                                      bool flip_vertically = false);

    static bool etc2_supported();   // ETC2/EAC decoding compiled in (detex)
    static bool astc_supported();   // ASTC decoding compiled in (astc_dec)

    // Probe size / channel count without decoding the pixels (EXR/DDS reported as 4ch).
    static bool info(const char* path, int* w, int* h, int* channels);
    static bool info_from_memory(const void* data, size_t size, int* w, int* h, int* channels);

    // Magic-byte format checks.
    static bool is_exr_file(const char* path);
    static bool is_dds_file(const char* path);
    static bool is_ktx_file(const char* path);
    static bool is_astc_file(const char* path);

    // True if the file looks like a Radiance/HDR or OpenEXR image (decode with load_hdr/
    // load — both route HDR sources to a float Image).
    static bool is_hdr_file(const char* path);

    // Last decode error for this thread ("" when none). Set on a failed load*.
    static const char* last_error();
};

// Return a 4-channel copy of `src` (RGBA8 or RGBA32F). 1/2/3-channel sources are
// expanded (G/B replicate R for 1-channel grey, alpha defaults to opaque); a
// 4-channel source is copied unchanged. Returns false if `src` is invalid.
bool expand_to_rgba(const Image& src, Image* out);

} // namespace gfx
} // namespace window
