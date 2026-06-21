#include "image.hpp"

// Standard headers first, so the detex __attribute__ shim below can't perturb the STL.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

// stb_image is the single-header decoder; this TU carries the implementation.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef WINDOW_SUPPORT_EXR
// tinyexr (OpenEXR). Uses its bundled miniz (compiled separately) for zlib.
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
#endif

#ifdef WINDOW_SUPPORT_DDS
// tinyddsloader parses the DDS container; bcdec decodes BCn blocks to RGB(A).
#define TINYDDSLOADER_IMPLEMENTATION
#include "tinyddsloader.h"
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"
#include <istream>
#include <streambuf>
#endif

#ifdef WINDOW_SUPPORT_ASTC
#include "astc_decomp.h"    // basisu::astc::decompress (single ASTC block -> RGBA8)
#endif

#ifdef WINDOW_SUPPORT_ETC2
#include "detex_compat.h"   // neutralize __attribute__ on MSVC (must precede detex.h)
#include "detex.h"          // ETC2/EAC block decoders (C linkage via extern "C")
#endif

namespace window {
namespace gfx {
namespace {

// Per-thread error string so concurrent loads on different threads don't clobber it.
thread_local char g_error[256] = {0};

void set_error(const char* msg) {
    if (!msg) msg = "";
    std::strncpy(g_error, msg, sizeof(g_error) - 1);
    g_error[sizeof(g_error) - 1] = '\0';
}

void take_ownership(Image* out, void* pixels, int w, int h, int channels, ImagePixelType type) {
    out->reset();
    out->pixels   = static_cast<uint8_t*>(pixels);
    out->width    = w;
    out->height   = h;
    out->channels = channels;
    out->type     = type;
}

// Container kind, sniffed from the first bytes (each has unambiguous magic).
enum class Container { Stb, Exr, Dds, Ktx, Astc };

Container detect_container(const uint8_t* h, size_t n) {
    if (n >= 4 && h[0] == 0x76 && h[1] == 0x2f && h[2] == 0x31 && h[3] == 0x01) return Container::Exr;
    if (n >= 4 && h[0] == 'D' && h[1] == 'D' && h[2] == 'S' && h[3] == ' ')     return Container::Dds;
    // KTX1 «AB 4B 54 58 20 31 31 BB…» and KTX2 «AB 4B 54 58 20 32 30 BB…» share a prefix.
    if (n >= 4 && h[0] == 0xAB && h[1] == 'K' && h[2] == 'T' && h[3] == 'X')     return Container::Ktx;
    // ARM raw ASTC: magic 0x5CA1AB13 little-endian.
    if (n >= 4 && h[0] == 0x13 && h[1] == 0xAB && h[2] == 0xA1 && h[3] == 0x5C)  return Container::Astc;
    return Container::Stb;
}

// Read up to `n` leading bytes of a file (for magic sniffing). Returns bytes read.
size_t peek_file_head(const char* path, uint8_t* buf, size_t n) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return 0;
    size_t got = std::fread(buf, 1, n, f);
    std::fclose(f);
    return got;
}

// In-place vertical flip (rows), valid for any pixel type/channel count.
void flip_vertical(Image* img) {
    if (!img->valid()) return;
    const size_t pitch = img->row_pitch();
    std::vector<uint8_t> tmp(pitch);
    for (int y = 0; y < img->height / 2; ++y) {
        uint8_t* a = img->pixels + size_t(y) * pitch;
        uint8_t* b = img->pixels + size_t(img->height - 1 - y) * pitch;
        std::memcpy(tmp.data(), a, pitch);
        std::memcpy(a, b, pitch);
        std::memcpy(b, tmp.data(), pitch);
    }
}

// Allocate via stb's allocator so Image::reset() (stbi_image_free) frees it uniformly.
void* dup_into_stbi(const void* src, size_t bytes) {
    void* buf = stbi__malloc(bytes);
    if (buf && src) std::memcpy(buf, src, bytes);
    return buf;
}

// Convert an IEEE 754 half (binary16) to float. Subnormals decode as mant * 2^-24.
float half_to_float(uint16_t hbits) {
    const uint32_t sign = (hbits >> 15) & 1u;
    const uint32_t exp  = (hbits >> 10) & 0x1Fu;
    const uint32_t mant = hbits & 0x3FFu;
    float val;
    if (exp == 0) {
        val = float(mant) * (1.0f / 16777216.0f);          // 2^-24
        return sign ? -val : val;
    }
    uint32_t f;
    if (exp == 0x1Fu) f = (sign << 31) | 0x7F800000u | (mant << 13);     // inf / nan
    else              f = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
    std::memcpy(&val, &f, sizeof(val));
    return val;
}

} // namespace

Image::~Image() { reset(); }

Image::Image(Image&& o) noexcept
    : width(o.width), height(o.height), channels(o.channels), type(o.type), pixels(o.pixels) {
    o.pixels = nullptr;
    o.width = o.height = o.channels = 0;
}

Image& Image::operator=(Image&& o) noexcept {
    if (this != &o) {
        reset();
        width = o.width; height = o.height; channels = o.channels; type = o.type; pixels = o.pixels;
        o.pixels = nullptr;
        o.width = o.height = o.channels = 0;
    }
    return *this;
}

void Image::reset() {
    if (pixels) stbi_image_free(pixels);   // stb owns LDR + HDR allocations alike
    pixels = nullptr;
    width = height = channels = 0;
    type = ImagePixelType::U8;
}

TextureFormat Image::texture_format(bool srgb) const {
    if (type == ImagePixelType::F32) {
        switch (channels) {
            case 1:  return TextureFormat::R32_FLOAT;
            case 2:  return TextureFormat::RG32_FLOAT;
            default: return TextureFormat::RGBA32_FLOAT;   // 3 or 4
        }
    }
    switch (channels) {
        case 1:  return TextureFormat::R8_UNORM;
        case 2:  return TextureFormat::RG8_UNORM;
        default: return srgb ? TextureFormat::RGBA8_UNORM_SRGB : TextureFormat::RGBA8_UNORM;  // 3 or 4
    }
}

bool Image::load(const char* path, Image* out, int desired_channels, bool flip_vertically) {
    if (!path || !out) { set_error("null argument"); return false; }
    uint8_t head[8] = {0};
    switch (detect_container(head, peek_file_head(path, head, sizeof(head)))) {
        case Container::Exr: return load_exr(path, out, flip_vertically);
        case Container::Dds: return load_dds(path, out, flip_vertically);
        case Container::Ktx: return load_ktx(path, out, flip_vertically);
        case Container::Astc: return load_astc(path, out, flip_vertically);
        default: break;
    }
    stbi_set_flip_vertically_on_load_thread(flip_vertically ? 1 : 0);
    int w = 0, h = 0, src_ch = 0;
    stbi_uc* px = stbi_load(path, &w, &h, &src_ch, desired_channels);
    if (!px) { set_error(stbi_failure_reason()); return false; }
    take_ownership(out, px, w, h, desired_channels ? desired_channels : src_ch, ImagePixelType::U8);
    set_error("");
    return true;
}

bool Image::load_from_memory(const void* data, size_t size, Image* out,
                             int desired_channels, bool flip_vertically) {
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    switch (detect_container(static_cast<const uint8_t*>(data), size)) {
        case Container::Exr: return load_exr_from_memory(data, size, out, flip_vertically);
        case Container::Dds: return load_dds_from_memory(data, size, out, flip_vertically);
        case Container::Ktx: return load_ktx_from_memory(data, size, out, flip_vertically);
        case Container::Astc: return load_astc_from_memory(data, size, out, flip_vertically);
        default: break;
    }
    stbi_set_flip_vertically_on_load_thread(flip_vertically ? 1 : 0);
    int w = 0, h = 0, src_ch = 0;
    stbi_uc* px = stbi_load_from_memory(static_cast<const stbi_uc*>(data), int(size),
                                        &w, &h, &src_ch, desired_channels);
    if (!px) { set_error(stbi_failure_reason()); return false; }
    take_ownership(out, px, w, h, desired_channels ? desired_channels : src_ch, ImagePixelType::U8);
    set_error("");
    return true;
}

bool Image::load_hdr(const char* path, Image* out, int desired_channels, bool flip_vertically) {
    if (!path || !out) { set_error("null argument"); return false; }
    uint8_t head[8] = {0};
    switch (detect_container(head, peek_file_head(path, head, sizeof(head)))) {
        case Container::Exr: return load_exr(path, out, flip_vertically);
        case Container::Dds: return load_dds(path, out, flip_vertically);
        case Container::Ktx: return load_ktx(path, out, flip_vertically);
        case Container::Astc: return load_astc(path, out, flip_vertically);
        default: break;
    }
    stbi_set_flip_vertically_on_load_thread(flip_vertically ? 1 : 0);
    int w = 0, h = 0, src_ch = 0;
    float* px = stbi_loadf(path, &w, &h, &src_ch, desired_channels);
    if (!px) { set_error(stbi_failure_reason()); return false; }
    take_ownership(out, px, w, h, desired_channels ? desired_channels : src_ch, ImagePixelType::F32);
    set_error("");
    return true;
}

bool Image::load_hdr_from_memory(const void* data, size_t size, Image* out,
                                 int desired_channels, bool flip_vertically) {
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    switch (detect_container(static_cast<const uint8_t*>(data), size)) {
        case Container::Exr: return load_exr_from_memory(data, size, out, flip_vertically);
        case Container::Dds: return load_dds_from_memory(data, size, out, flip_vertically);
        case Container::Ktx: return load_ktx_from_memory(data, size, out, flip_vertically);
        case Container::Astc: return load_astc_from_memory(data, size, out, flip_vertically);
        default: break;
    }
    stbi_set_flip_vertically_on_load_thread(flip_vertically ? 1 : 0);
    int w = 0, h = 0, src_ch = 0;
    float* px = stbi_loadf_from_memory(static_cast<const stbi_uc*>(data), int(size),
                                       &w, &h, &src_ch, desired_channels);
    if (!px) { set_error(stbi_failure_reason()); return false; }
    take_ownership(out, px, w, h, desired_channels ? desired_channels : src_ch, ImagePixelType::F32);
    set_error("");
    return true;
}

// ---- EXR (tinyexr) ---------------------------------------------------------
bool Image::exr_supported() {
#ifdef WINDOW_SUPPORT_EXR
    return true;
#else
    return false;
#endif
}

bool Image::load_exr(const char* path, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_EXR
    if (!path || !out) { set_error("null argument"); return false; }
    float* rgba = nullptr; int w = 0, h = 0; const char* err = nullptr;
    if (LoadEXR(&rgba, &w, &h, path, &err) != TINYEXR_SUCCESS) {
        set_error(err ? err : "EXR load failed");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    take_ownership(out, rgba, w, h, 4, ImagePixelType::F32);   // rgba is malloc'd
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
#else
    (void)path; (void)out; (void)flip_vertically;
    set_error("EXR support not built (configure with -DWINDOW_ENABLE_EXR=ON)");
    return false;
#endif
}

bool Image::load_exr_from_memory(const void* data, size_t size, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_EXR
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    float* rgba = nullptr; int w = 0, h = 0; const char* err = nullptr;
    if (LoadEXRFromMemory(&rgba, &w, &h, static_cast<const unsigned char*>(data), size, &err)
            != TINYEXR_SUCCESS) {
        set_error(err ? err : "EXR load failed");
        if (err) FreeEXRErrorMessage(err);
        return false;
    }
    take_ownership(out, rgba, w, h, 4, ImagePixelType::F32);
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
#else
    (void)data; (void)size; (void)out; (void)flip_vertically;
    set_error("EXR support not built (configure with -DWINDOW_ENABLE_EXR=ON)");
    return false;
#endif
}

// ---- DDS (tinyddsloader container + bcdec block decode) --------------------
#ifdef WINDOW_SUPPORT_DDS
namespace {

// Seekable read-only streambuf over a memory range (DDSFile::Load seeks the stream).
struct MemReadBuf : std::streambuf {
    MemReadBuf(const char* base, size_t size) {
        char* p = const_cast<char*>(base);
        setg(p, p, p + size);
    }
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        char* beg = eback(); char* cur = gptr(); char* end = egptr();
        char* target = (dir == std::ios_base::beg) ? beg + off
                     : (dir == std::ios_base::cur) ? cur + off
                                                   : end + off;
        if (target < beg || target > end) return pos_type(off_type(-1));
        setg(beg, target, end);
        return pos_type(target - beg);
    }
    pos_type seekpos(pos_type pos, std::ios_base::openmode which) override {
        return seekoff(off_type(pos), std::ios_base::beg, which);
    }
};

// Decode a BCn-compressed image (tightly-packed top-mip blocks) to RGBA8 (kind
// 1/2/3/4/5/7) or RGBA32F (kind 6 = BC6H; `sign` selects the signed-float variant).
// Fills exactly one of `rgba8`/`rgbaf`. Reused by both the DDS and KTX paths.
void decode_bc_image(int kind, bool sign, const uint8_t* src, int w, int h,
                     std::vector<uint8_t>& rgba8, std::vector<float>& rgbaf) {
    const int bx = (w + 3) / 4, by = (h + 3) / 4;
    if (kind == 6) {                                  // BC6H -> RGBA32F
        rgbaf.assign(size_t(w) * h * 4, 0.0f);
        float blk[4 * 4 * 3];
        for (int yb = 0; yb < by; ++yb) for (int xb = 0; xb < bx; ++xb) {
            const uint8_t* sb = src + (size_t(yb) * bx + xb) * 16;
            bcdec_bc6h_float(sb, blk, 4 * 3, sign ? 1 : 0);
            for (int ty = 0; ty < 4; ++ty) { int y = yb*4+ty; if (y>=h) continue;
                for (int tx = 0; tx < 4; ++tx) { int x = xb*4+tx; if (x>=w) continue;
                    float* d = &rgbaf[(size_t(y)*w+x)*4]; const float* p = &blk[(ty*4+tx)*3];
                    d[0]=p[0]; d[1]=p[1]; d[2]=p[2]; d[3]=1.0f; } }
        }
        return;
    }
    rgba8.assign(size_t(w) * h * 4, 0);
    const bool small_block = (kind == 1 || kind == 4);   // BC1/BC4 = 8 bytes, others = 16
    const int block_bytes = small_block ? 8 : 16;
    uint8_t blk4[4*4*4], blk1[4*4], blk2[4*4*2];
    for (int yb = 0; yb < by; ++yb) for (int xb = 0; xb < bx; ++xb) {
        const uint8_t* sb = src + (size_t(yb) * bx + xb) * block_bytes;
        switch (kind) {
            case 1: bcdec_bc1(sb, blk4, 4*4); break;
            case 2: bcdec_bc2(sb, blk4, 4*4); break;
            case 3: bcdec_bc3(sb, blk4, 4*4); break;
            case 7: bcdec_bc7(sb, blk4, 4*4); break;
            case 4: bcdec_bc4(sb, blk1, 4);   break;
            case 5: bcdec_bc5(sb, blk2, 4*2); break;
            default: break;
        }
        for (int ty = 0; ty < 4; ++ty) { int y = yb*4+ty; if (y>=h) continue;
            for (int tx = 0; tx < 4; ++tx) { int x = xb*4+tx; if (x>=w) continue;
                uint8_t* d = &rgba8[(size_t(y)*w+x)*4]; const int t = ty*4+tx;
                if (kind == 4)      { uint8_t v = blk1[t];   d[0]=v; d[1]=v; d[2]=v; d[3]=255; }
                else if (kind == 5) { d[0]=blk2[t*2]; d[1]=blk2[t*2+1]; d[2]=0; d[3]=255; }
                else                { const uint8_t* p=&blk4[t*4]; d[0]=p[0]; d[1]=p[1]; d[2]=p[2]; d[3]=p[3]; } } }
    }
}

// Decode the top mip / first array slice of a DDS into `out` (RGBA8 or RGBA32F).
bool decode_dds(tinyddsloader::DDSFile& dds, Image* out) {
    using DDS = tinyddsloader::DDSFile;
    using Fmt = DDS::DXGIFormat;
    const DDS::ImageData* img = dds.GetImageData(0, 0);
    if (!img || !img->m_mem) { set_error("DDS: no image data"); return false; }

    const int w = int(img->m_width), h = int(img->m_height);
    const uint8_t* src = static_cast<const uint8_t*>(img->m_mem);
    const Fmt fmt = dds.GetFormat();
    const size_t npx = size_t(w) * size_t(h);

    std::vector<uint8_t> rgba;    // RGBA8 path
    std::vector<float>   rgbaf;   // RGBA32F path

    switch (fmt) {
        case Fmt::R8G8B8A8_UNorm:
        case Fmt::R8G8B8A8_UNorm_SRGB: {
            rgba.resize(npx * 4);
            const int pitch = img->m_memPitch ? int(img->m_memPitch) : w * 4;
            for (int y = 0; y < h; ++y)
                std::memcpy(&rgba[size_t(y) * w * 4], src + size_t(y) * pitch, size_t(w) * 4);
            break;
        }
        case Fmt::B8G8R8A8_UNorm:
        case Fmt::B8G8R8A8_UNorm_SRGB: {
            rgba.resize(npx * 4);
            const int pitch = img->m_memPitch ? int(img->m_memPitch) : w * 4;
            for (int y = 0; y < h; ++y) {
                const uint8_t* s = src + size_t(y) * pitch;
                uint8_t* d = &rgba[size_t(y) * w * 4];
                for (int x = 0; x < w; ++x, s += 4, d += 4) { d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=s[3]; }
            }
            break;
        }
        case Fmt::R32G32B32A32_Float: {
            rgbaf.resize(npx * 4);
            const int pitch = img->m_memPitch ? int(img->m_memPitch) : w * 16;
            for (int y = 0; y < h; ++y)
                std::memcpy(&rgbaf[size_t(y) * w * 4], src + size_t(y) * pitch, size_t(w) * 16);
            break;
        }
        case Fmt::R16G16B16A16_Float: {
            rgbaf.resize(npx * 4);
            const int pitch = img->m_memPitch ? int(img->m_memPitch) : w * 8;
            for (int y = 0; y < h; ++y) {
                const uint16_t* s = reinterpret_cast<const uint16_t*>(src + size_t(y) * pitch);
                float* d = &rgbaf[size_t(y) * w * 4];
                for (int x = 0; x < w * 4; ++x) d[x] = half_to_float(s[x]);
            }
            break;
        }
        case Fmt::BC1_UNorm: case Fmt::BC1_UNorm_SRGB: decode_bc_image(1, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC2_UNorm: case Fmt::BC2_UNorm_SRGB: decode_bc_image(2, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC3_UNorm: case Fmt::BC3_UNorm_SRGB: decode_bc_image(3, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC4_UNorm:                            decode_bc_image(4, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC5_UNorm:                            decode_bc_image(5, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC7_UNorm: case Fmt::BC7_UNorm_SRGB: decode_bc_image(7, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC6H_UF16: decode_bc_image(6, false, src, w, h, rgba, rgbaf); break;
        case Fmt::BC6H_SF16: decode_bc_image(6, true,  src, w, h, rgba, rgbaf); break;
        default:
            set_error("DDS: unsupported pixel format");
            return false;
    }

    if (!rgbaf.empty()) {
        void* buf = dup_into_stbi(rgbaf.data(), rgbaf.size() * sizeof(float));
        if (!buf) { set_error("out of memory"); return false; }
        take_ownership(out, buf, w, h, 4, ImagePixelType::F32);
    } else {
        void* buf = dup_into_stbi(rgba.data(), rgba.size());
        if (!buf) { set_error("out of memory"); return false; }
        take_ownership(out, buf, w, h, 4, ImagePixelType::U8);
    }
    return true;
}

} // namespace
#endif // WINDOW_SUPPORT_DDS

bool Image::dds_supported() {
#ifdef WINDOW_SUPPORT_DDS
    return true;
#else
    return false;
#endif
}

bool Image::load_dds(const char* path, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_DDS
    if (!path || !out) { set_error("null argument"); return false; }
    tinyddsloader::DDSFile dds;
    if (dds.Load(path) != tinyddsloader::Result::Success) { set_error("DDS: load/parse failed"); return false; }
    if (!decode_dds(dds, out)) return false;
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
#else
    (void)path; (void)out; (void)flip_vertically;
    set_error("DDS support not built (configure with -DWINDOW_ENABLE_DDS=ON)");
    return false;
#endif
}

bool Image::load_dds_from_memory(const void* data, size_t size, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_DDS
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    MemReadBuf buf(static_cast<const char*>(data), size);
    std::istream is(&buf);
    tinyddsloader::DDSFile dds;
    if (dds.Load(is) != tinyddsloader::Result::Success) { set_error("DDS: load/parse failed"); return false; }
    if (!decode_dds(dds, out)) return false;
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
#else
    (void)data; (void)size; (void)out; (void)flip_vertically;
    set_error("DDS support not built (configure with -DWINDOW_ENABLE_DDS=ON)");
    return false;
#endif
}

// ---- KTX / KTX2 containers + ETC2/ASTC block decode ------------------------
namespace {

// ASTC footprint table (block width x height), shared by the GL and VK format maps.
struct AstcDim { int w, h; };
const AstcDim kAstcDims[14] = {
    {4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},{8,8},
    {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}
};

// A container's payload format, normalized to one CPU decoder.
struct TexFormat {
    enum Codec { Unsupported, RawRGBA8, RawBGRA8, RawRGBA32F, RawRGBA16F, BC, ETC2, ASTC };
    Codec codec  = Unsupported;
    int   param  = 0;        // BC: 1-7 ; ETC2: 0=RGB 1=RGBA(EAC) 2=RGB+1-bit-alpha
    bool  is_signed = false; // BC6H signed-float
    bool  srgb   = false;
    int   astc_w = 4, astc_h = 4;
};

#ifdef WINDOW_SUPPORT_ETC2
// kind: 0=ETC2_RGB(8B), 1=ETC2_RGBA via EAC alpha(16B), 2=ETC2 RGB+punchthrough A1(8B).
void decode_etc2_image(int kind, const uint8_t* src, int w, int h, std::vector<uint8_t>& rgba8) {
    rgba8.assign(size_t(w) * h * 4, 0);
    const int bx = (w + 3) / 4, by = (h + 3) / 4;
    const int block_bytes = (kind == 1) ? 16 : 8;
    uint8_t blk[4 * 4 * 4];   // detex outputs RGBA8 (R in lowest byte)
    for (int yb = 0; yb < by; ++yb) for (int xb = 0; xb < bx; ++xb) {
        const uint8_t* sb = src + (size_t(yb) * bx + xb) * block_bytes;
        switch (kind) {
            case 0: detexDecompressBlockETC2(sb, DETEX_MODE_MASK_ALL, 0, blk); break;
            case 1: detexDecompressBlockETC2_EAC(sb, DETEX_MODE_MASK_ALL, 0, blk); break;
            case 2: detexDecompressBlockETC2_PUNCHTHROUGH(sb, DETEX_MODE_MASK_ALL, 0, blk); break;
            default: break;
        }
        for (int ty = 0; ty < 4; ++ty) { int y = yb*4+ty; if (y>=h) continue;
            for (int tx = 0; tx < 4; ++tx) { int x = xb*4+tx; if (x>=w) continue;
                uint8_t* d = &rgba8[(size_t(y)*w+x)*4]; const uint8_t* p = &blk[(ty*4+tx)*4];
                d[0]=p[0]; d[1]=p[1]; d[2]=p[2]; d[3]=p[3]; } }
    }
}
#endif

#ifdef WINDOW_SUPPORT_ASTC
void decode_astc_image(int bw, int bh, bool srgb, const uint8_t* src, int w, int h,
                       std::vector<uint8_t>& rgba8) {
    rgba8.assign(size_t(w) * h * 4, 0);
    const int bx = (w + bw - 1) / bw, by = (h + bh - 1) / bh;
    std::vector<uint8_t> tile(size_t(bw) * bh * 4);
    for (int yb = 0; yb < by; ++yb) for (int xb = 0; xb < bx; ++xb) {
        const uint8_t* sb = src + (size_t(yb) * bx + xb) * 16;   // ASTC block is always 16 bytes
        basisu::astc::decompress(tile.data(), sb, srgb, bw, bh);  // -> bw*bh RGBA8 tile
        for (int ty = 0; ty < bh; ++ty) { int y = yb*bh+ty; if (y>=h) continue;
            for (int tx = 0; tx < bw; ++tx) { int x = xb*bw+tx; if (x>=w) continue;
                uint8_t* d = &rgba8[(size_t(y)*w+x)*4]; const uint8_t* p = &tile[(ty*bw+tx)*4];
                d[0]=p[0]; d[1]=p[1]; d[2]=p[2]; d[3]=p[3]; } }
    }
}
#endif

uint32_t rd_u32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
}
uint64_t rd_u64(const uint8_t* p) { return uint64_t(rd_u32(p)) | (uint64_t(rd_u32(p+4))<<32); }

// KTX1 glInternalFormat (OpenGL enum) -> normalized format.
TexFormat gl_to_format(uint32_t gl) {
    TexFormat f;
    switch (gl) {
        case 0x8058: f.codec=TexFormat::RawRGBA8; break;                       // GL_RGBA8
        case 0x8C43: f.codec=TexFormat::RawRGBA8; f.srgb=true; break;          // GL_SRGB8_ALPHA8
        case 0x83F0: case 0x83F1: f.codec=TexFormat::BC; f.param=1; break;     // DXT1 (RGB/RGBA)
        case 0x8C4C: case 0x8C4D: f.codec=TexFormat::BC; f.param=1; f.srgb=true; break;
        case 0x83F2: f.codec=TexFormat::BC; f.param=2; break;                  // DXT3
        case 0x8C4E: f.codec=TexFormat::BC; f.param=2; f.srgb=true; break;
        case 0x83F3: f.codec=TexFormat::BC; f.param=3; break;                  // DXT5
        case 0x8C4F: f.codec=TexFormat::BC; f.param=3; f.srgb=true; break;
        case 0x8DBB: f.codec=TexFormat::BC; f.param=4; break;                  // RGTC1 -> BC4
        case 0x8DBD: f.codec=TexFormat::BC; f.param=5; break;                  // RGTC2 -> BC5
        case 0x8E8C: f.codec=TexFormat::BC; f.param=7; break;                  // BPTC unorm -> BC7
        case 0x8E8D: f.codec=TexFormat::BC; f.param=7; f.srgb=true; break;
        case 0x8E8E: f.codec=TexFormat::BC; f.param=6; f.is_signed=true; break;// BPTC signed float -> BC6H
        case 0x8E8F: f.codec=TexFormat::BC; f.param=6; break;                  // BPTC unsigned float
        case 0x9274: f.codec=TexFormat::ETC2; f.param=0; break;                // RGB8_ETC2
        case 0x9275: f.codec=TexFormat::ETC2; f.param=0; f.srgb=true; break;
        case 0x9276: f.codec=TexFormat::ETC2; f.param=2; break;                // RGB8_PUNCHTHROUGH_A1
        case 0x9277: f.codec=TexFormat::ETC2; f.param=2; f.srgb=true; break;
        case 0x9278: f.codec=TexFormat::ETC2; f.param=1; break;                // RGBA8_ETC2_EAC
        case 0x9279: f.codec=TexFormat::ETC2; f.param=1; f.srgb=true; break;
        default:
            if (gl >= 0x93B0 && gl <= 0x93BD) { f.codec=TexFormat::ASTC; const AstcDim d=kAstcDims[gl-0x93B0]; f.astc_w=d.w; f.astc_h=d.h; }
            else if (gl >= 0x93D0 && gl <= 0x93DD) { f.codec=TexFormat::ASTC; f.srgb=true; const AstcDim d=kAstcDims[gl-0x93D0]; f.astc_w=d.w; f.astc_h=d.h; }
            break;
    }
    return f;
}

// KTX2 vkFormat (Vulkan VkFormat enum) -> normalized format.
TexFormat vk_to_format(uint32_t vk) {
    TexFormat f;
    switch (vk) {
        case 37:  f.codec=TexFormat::RawRGBA8; break;                       // R8G8B8A8_UNORM
        case 43:  f.codec=TexFormat::RawRGBA8; f.srgb=true; break;          // R8G8B8A8_SRGB
        case 44:  f.codec=TexFormat::RawBGRA8; break;                       // B8G8R8A8_UNORM
        case 50:  f.codec=TexFormat::RawBGRA8; f.srgb=true; break;          // B8G8R8A8_SRGB
        case 97:  f.codec=TexFormat::RawRGBA16F; break;                     // R16G16B16A16_SFLOAT
        case 109: f.codec=TexFormat::RawRGBA32F; break;                     // R32G32B32A32_SFLOAT
        case 131: case 133: f.codec=TexFormat::BC; f.param=1; break;        // BC1 RGB/RGBA UNORM
        case 132: case 134: f.codec=TexFormat::BC; f.param=1; f.srgb=true; break;
        case 135: f.codec=TexFormat::BC; f.param=2; break;
        case 136: f.codec=TexFormat::BC; f.param=2; f.srgb=true; break;
        case 137: f.codec=TexFormat::BC; f.param=3; break;
        case 138: f.codec=TexFormat::BC; f.param=3; f.srgb=true; break;
        case 139: case 140: f.codec=TexFormat::BC; f.param=4; break;        // BC4
        case 141: case 142: f.codec=TexFormat::BC; f.param=5; break;        // BC5
        case 143: f.codec=TexFormat::BC; f.param=6; break;                  // BC6H ufloat
        case 144: f.codec=TexFormat::BC; f.param=6; f.is_signed=true; break;// BC6H sfloat
        case 145: f.codec=TexFormat::BC; f.param=7; break;
        case 146: f.codec=TexFormat::BC; f.param=7; f.srgb=true; break;
        case 147: f.codec=TexFormat::ETC2; f.param=0; break;                // ETC2_R8G8B8
        case 148: f.codec=TexFormat::ETC2; f.param=0; f.srgb=true; break;
        case 149: f.codec=TexFormat::ETC2; f.param=2; break;                // ETC2_R8G8B8A1
        case 150: f.codec=TexFormat::ETC2; f.param=2; f.srgb=true; break;
        case 151: f.codec=TexFormat::ETC2; f.param=1; break;                // ETC2_R8G8B8A8
        case 152: f.codec=TexFormat::ETC2; f.param=1; f.srgb=true; break;
        default:
            if (vk >= 157 && vk <= 184) {   // ASTC_*_UNORM_BLOCK / _SRGB_BLOCK
                const int idx = int(vk) - 157;
                f.codec=TexFormat::ASTC; f.srgb=(idx & 1) != 0;
                const AstcDim d=kAstcDims[idx/2]; f.astc_w=d.w; f.astc_h=d.h;
            }
            break;
    }
    return f;
}

// Decode one tightly-packed mip of normalized format `f` into `out` (RGBA8/RGBA32F).
bool decode_texture(const TexFormat& f, const uint8_t* src, size_t src_size, int w, int h, Image* out) {
    if (w <= 0 || h <= 0) { set_error("texture: bad dimensions"); return false; }
    std::vector<uint8_t> rgba8; std::vector<float> rgbaf;
    const size_t npx = size_t(w) * size_t(h);
    switch (f.codec) {
        case TexFormat::RawRGBA8:
            if (src_size < npx*4) { set_error("texture: truncated data"); return false; }
            rgba8.assign(src, src + npx*4); break;
        case TexFormat::RawBGRA8:
            if (src_size < npx*4) { set_error("texture: truncated data"); return false; }
            rgba8.resize(npx*4);
            for (size_t i = 0; i < npx; ++i) { const uint8_t* s=src+i*4; uint8_t* d=&rgba8[i*4]; d[0]=s[2]; d[1]=s[1]; d[2]=s[0]; d[3]=s[3]; }
            break;
        case TexFormat::RawRGBA32F:
            if (src_size < npx*16) { set_error("texture: truncated data"); return false; }
            rgbaf.assign(reinterpret_cast<const float*>(src), reinterpret_cast<const float*>(src) + npx*4); break;
        case TexFormat::RawRGBA16F: {
            if (src_size < npx*8) { set_error("texture: truncated data"); return false; }
            rgbaf.resize(npx*4); const uint16_t* s = reinterpret_cast<const uint16_t*>(src);
            for (size_t i = 0; i < npx*4; ++i) rgbaf[i] = half_to_float(s[i]); break;
        }
        case TexFormat::BC:
#ifdef WINDOW_SUPPORT_DDS
            decode_bc_image(f.param, f.is_signed, src, w, h, rgba8, rgbaf); break;
#else
            set_error("BC decode not built (enable WINDOW_ENABLE_DDS)"); return false;
#endif
        case TexFormat::ETC2:
#ifdef WINDOW_SUPPORT_ETC2
            decode_etc2_image(f.param, src, w, h, rgba8); break;
#else
            set_error("ETC2 decode not built (enable WINDOW_ENABLE_ETC2)"); return false;
#endif
        case TexFormat::ASTC:
#ifdef WINDOW_SUPPORT_ASTC
            decode_astc_image(f.astc_w, f.astc_h, f.srgb, src, w, h, rgba8); break;
#else
            set_error("ASTC decode not built (enable WINDOW_ENABLE_ASTC)"); return false;
#endif
        default:
            set_error("unsupported texture format"); return false;
    }
    if (!rgbaf.empty()) {
        void* b = dup_into_stbi(rgbaf.data(), rgbaf.size() * sizeof(float));
        if (!b) { set_error("out of memory"); return false; }
        take_ownership(out, b, w, h, 4, ImagePixelType::F32);
    } else {
        void* b = dup_into_stbi(rgba8.data(), rgba8.size());
        if (!b) { set_error("out of memory"); return false; }
        take_ownership(out, b, w, h, 4, ImagePixelType::U8);
    }
    return true;
}

// Parse a KTX1/KTX2 blob and decode its top mip. KTX2 must be un-supercompressed.
bool decode_ktx(const uint8_t* data, size_t size, Image* out) {
    if (size < 16) { set_error("KTX: too small"); return false; }
    if (data[5] == '2') {   // KTX2: 12-byte id, then header @12, level index @80.
        if (size < 80 + 24) { set_error("KTX2: truncated header"); return false; }
        const uint32_t vk_format = rd_u32(data + 12);
        const int w = int(rd_u32(data + 20));
        int h = int(rd_u32(data + 24)); if (h == 0) h = 1;
        const uint32_t super = rd_u32(data + 44);
        if (super != 0) { set_error("KTX2: supercompressed (basis/zstd) not supported"); return false; }
        const uint64_t off = rd_u64(data + 80);          // level 0: {u64 offset, u64 length, u64 ulen}
        const uint64_t len = rd_u64(data + 88);
        if (len == 0 || off + len > size) { set_error("KTX2: bad level index"); return false; }
        TexFormat f = vk_to_format(vk_format);
        if (f.codec == TexFormat::Unsupported) { set_error("KTX2: unsupported vkFormat"); return false; }
        return decode_texture(f, data + off, size_t(len), w, h, out);
    }
    // KTX1: 64-byte header (12-byte id + 13 u32 fields).
    if (size < 64) { set_error("KTX: truncated header"); return false; }
    const uint32_t gl_internal = rd_u32(data + 28);
    const int w = int(rd_u32(data + 36));
    int h = int(rd_u32(data + 40)); if (h == 0) h = 1;
    const uint32_t kvd = rd_u32(data + 60);
    const size_t lvl0 = 64 + size_t(kvd);
    if (lvl0 + 4 > size) { set_error("KTX: truncated"); return false; }
    const uint32_t image_size = rd_u32(data + lvl0);
    const size_t data_off = lvl0 + 4;
    if (image_size == 0 || data_off + image_size > size) { set_error("KTX: bad image size"); return false; }
    TexFormat f = gl_to_format(gl_internal);
    if (f.codec == TexFormat::Unsupported) { set_error("KTX: unsupported glInternalFormat"); return false; }
    return decode_texture(f, data + data_off, size_t(image_size), w, h, out);
}

// Read width/height from a KTX header (KTX1: w@36 h@40; KTX2: w@20 h@24).
bool ktx_header_dims(const uint8_t* hdr, size_t n, int* w, int* h) {
    if (n < 44) return false;
    const bool v2 = hdr[5] == '2';
    const uint32_t ww = rd_u32(hdr + (v2 ? 20 : 36));
    uint32_t hh = rd_u32(hdr + (v2 ? 24 : 40)); if (hh == 0) hh = 1;
    if (w) *w = int(ww); if (h) *h = int(hh);
    return true;
}

} // namespace

bool Image::etc2_supported() {
#ifdef WINDOW_SUPPORT_ETC2
    return true;
#else
    return false;
#endif
}

bool Image::astc_supported() {
#ifdef WINDOW_SUPPORT_ASTC
    return true;
#else
    return false;
#endif
}

bool Image::load_ktx_from_memory(const void* data, size_t size, Image* out, bool flip_vertically) {
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    if (!decode_ktx(static_cast<const uint8_t*>(data), size, out)) return false;
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
}

bool Image::load_ktx(const char* path, Image* out, bool flip_vertically) {
    if (!path || !out) { set_error("null argument"); return false; }
    FILE* fp = std::fopen(path, "rb");
    if (!fp) { set_error("KTX: open failed"); return false; }
    std::fseek(fp, 0, SEEK_END); long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(fp); set_error("KTX: empty file"); return false; }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    const size_t got = std::fread(buf.data(), 1, static_cast<size_t>(sz), fp);
    std::fclose(fp);
    if (got != size_t(sz)) { set_error("KTX: read failed"); return false; }
    return load_ktx_from_memory(buf.data(), buf.size(), out, flip_vertically);
}

bool Image::load_astc_from_memory(const void* data, size_t size, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_ASTC
    if (!data || !size || !out) { set_error("null/empty buffer"); return false; }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    if (size < 16 || !(p[0]==0x13 && p[1]==0xAB && p[2]==0xA1 && p[3]==0x5C)) {
        set_error("ASTC: bad magic"); return false;
    }
    const int bw = p[4], bh = p[5];                          // p[6] = block depth (1 for 2D)
    const int w = int(p[7]) | (int(p[8])<<8) | (int(p[9])<<16);
    const int h = int(p[10]) | (int(p[11])<<8) | (int(p[12])<<16);
    if (bw < 1 || bh < 1 || w < 1 || h < 1) { set_error("ASTC: bad header"); return false; }
    const size_t blocks = size_t((w + bw - 1) / bw) * size_t((h + bh - 1) / bh);
    if (16 + blocks * 16 > size) { set_error("ASTC: truncated data"); return false; }
    std::vector<uint8_t> rgba8;
    decode_astc_image(bw, bh, false, p + 16, w, h, rgba8);   // raw .astc carries no colorspace flag
    void* b = dup_into_stbi(rgba8.data(), rgba8.size());
    if (!b) { set_error("out of memory"); return false; }
    take_ownership(out, b, w, h, 4, ImagePixelType::U8);
    if (flip_vertically) flip_vertical(out);
    set_error("");
    return true;
#else
    (void)data; (void)size; (void)out; (void)flip_vertically;
    set_error("ASTC support not built (configure with -DWINDOW_ENABLE_ASTC=ON)");
    return false;
#endif
}

bool Image::load_astc(const char* path, Image* out, bool flip_vertically) {
#ifdef WINDOW_SUPPORT_ASTC
    if (!path || !out) { set_error("null argument"); return false; }
    FILE* fp = std::fopen(path, "rb");
    if (!fp) { set_error("ASTC: open failed"); return false; }
    std::fseek(fp, 0, SEEK_END); long sz = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(fp); set_error("ASTC: empty file"); return false; }
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    const size_t got = std::fread(buf.data(), 1, static_cast<size_t>(sz), fp);
    std::fclose(fp);
    if (got != size_t(sz)) { set_error("ASTC: read failed"); return false; }
    return load_astc_from_memory(buf.data(), buf.size(), out, flip_vertically);
#else
    (void)path; (void)out; (void)flip_vertically;
    set_error("ASTC support not built (configure with -DWINDOW_ENABLE_ASTC=ON)");
    return false;
#endif
}

// Read width/height from a DDS header (dwHeight @ off 12, dwWidth @ off 16, LE).
static bool dds_header_dims(const uint8_t* hdr, size_t n, int* w, int* h) {
    if (n < 20) return false;
    const uint32_t hh = uint32_t(hdr[12]) | (uint32_t(hdr[13]) << 8) | (uint32_t(hdr[14]) << 16) | (uint32_t(hdr[15]) << 24);
    const uint32_t ww = uint32_t(hdr[16]) | (uint32_t(hdr[17]) << 8) | (uint32_t(hdr[18]) << 16) | (uint32_t(hdr[19]) << 24);
    if (w) *w = int(ww); if (h) *h = int(hh);
    return true;
}

bool Image::info(const char* path, int* w, int* h, int* channels) {
    uint8_t head[64] = {0};
    const size_t n = peek_file_head(path, head, sizeof(head));
    switch (detect_container(head, n)) {
        case Container::Dds:
            if (!dds_header_dims(head, n, w, h)) { set_error("DDS: short header"); return false; }
            if (channels) *channels = 4;
            return true;
        case Container::Ktx:
            if (!ktx_header_dims(head, n, w, h)) { set_error("KTX: short header"); return false; }
            if (channels) *channels = 4;
            return true;
        case Container::Astc:
            if (n < 13) { set_error("ASTC: short header"); return false; }
            if (w) *w = int(head[7]) | (int(head[8])<<8) | (int(head[9])<<16);
            if (h) *h = int(head[10]) | (int(head[11])<<8) | (int(head[12])<<16);
            if (channels) *channels = 4;
            return true;
        case Container::Exr: {
            Image tmp;
            if (!load_exr(path, &tmp)) return false;   // simple: decode then report dims
            if (w) *w = tmp.width; if (h) *h = tmp.height; if (channels) *channels = 4;
            return true;
        }
        default: break;
    }
    int lw = 0, lh = 0, lc = 0;
    if (!stbi_info(path, &lw, &lh, &lc)) { set_error(stbi_failure_reason()); return false; }
    if (w) *w = lw; if (h) *h = lh; if (channels) *channels = lc;
    return true;
}

bool Image::info_from_memory(const void* data, size_t size, int* w, int* h, int* channels) {
    if (!data || !size) { set_error("null/empty buffer"); return false; }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    switch (detect_container(bytes, size)) {
        case Container::Dds:
            if (!dds_header_dims(bytes, size, w, h)) { set_error("DDS: short header"); return false; }
            if (channels) *channels = 4;
            return true;
        case Container::Ktx:
            if (!ktx_header_dims(bytes, size, w, h)) { set_error("KTX: short header"); return false; }
            if (channels) *channels = 4;
            return true;
        case Container::Astc:
            if (size < 13) { set_error("ASTC: short header"); return false; }
            if (w) *w = int(bytes[7]) | (int(bytes[8])<<8) | (int(bytes[9])<<16);
            if (h) *h = int(bytes[10]) | (int(bytes[11])<<8) | (int(bytes[12])<<16);
            if (channels) *channels = 4;
            return true;
        case Container::Exr: {
            Image tmp;
            if (!load_exr_from_memory(data, size, &tmp)) return false;
            if (w) *w = tmp.width; if (h) *h = tmp.height; if (channels) *channels = 4;
            return true;
        }
        default: break;
    }
    int lw = 0, lh = 0, lc = 0;
    if (!stbi_info_from_memory(bytes, int(size), &lw, &lh, &lc)) {
        set_error(stbi_failure_reason());
        return false;
    }
    if (w) *w = lw; if (h) *h = lh; if (channels) *channels = lc;
    return true;
}

bool Image::is_exr_file(const char* path) {
    uint8_t head[4] = {0};
    return detect_container(head, peek_file_head(path, head, sizeof(head))) == Container::Exr;
}

bool Image::is_dds_file(const char* path) {
    uint8_t head[4] = {0};
    return detect_container(head, peek_file_head(path, head, sizeof(head))) == Container::Dds;
}

bool Image::is_ktx_file(const char* path) {
    uint8_t head[4] = {0};
    return detect_container(head, peek_file_head(path, head, sizeof(head))) == Container::Ktx;
}

bool Image::is_astc_file(const char* path) {
    uint8_t head[4] = {0};
    return detect_container(head, peek_file_head(path, head, sizeof(head))) == Container::Astc;
}

bool Image::is_hdr_file(const char* path) {
    return stbi_is_hdr(path) != 0 || is_exr_file(path);
}

const char* Image::last_error() { return g_error; }

bool expand_to_rgba(const Image& src, Image* out) {
    if (!src.valid() || !out) { set_error("invalid source"); return false; }
    if (src.channels == 4) {
        // Already RGBA: hand back an owned copy so callers can treat it uniformly.
        const size_t n = src.byte_size();
        void* buf = stbi__malloc(n);   // freed via stbi_image_free in Image::reset
        if (!buf) { set_error("out of memory"); return false; }
        std::memcpy(buf, src.pixels, n);
        take_ownership(out, buf, src.width, src.height, 4, src.type);
        return true;
    }

    const int   px      = src.width * src.height;
    const int   bpc     = src.bytes_per_channel();
    const size_t out_sz = size_t(px) * 4 * bpc;
    void* buf = stbi__malloc(out_sz);
    if (!buf) { set_error("out of memory"); return false; }

    if (src.type == ImagePixelType::F32) {
        const float* s = src.pixels_f();
        float*       d = static_cast<float*>(buf);
        for (int i = 0; i < px; ++i, s += src.channels, d += 4) {
            const float r = s[0];
            const float g = src.channels >= 2 ? s[1] : r;
            const float b = src.channels >= 3 ? s[2] : (src.channels == 1 ? r : 0.0f);
            d[0] = r; d[1] = g; d[2] = b; d[3] = 1.0f;
        }
    } else {
        const uint8_t* s = src.pixels;
        uint8_t*       d = static_cast<uint8_t*>(buf);
        for (int i = 0; i < px; ++i, s += src.channels, d += 4) {
            const uint8_t r = s[0];
            const uint8_t g = src.channels >= 2 ? s[1] : r;
            const uint8_t b = src.channels >= 3 ? s[2] : (src.channels == 1 ? r : 0);
            d[0] = r; d[1] = g; d[2] = b; d[3] = 255;
        }
    }
    take_ownership(out, buf, src.width, src.height, 4, src.type);
    return true;
}

} // namespace gfx
} // namespace window
