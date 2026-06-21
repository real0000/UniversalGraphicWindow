// api_texture_format.cpp - TextureFormat memory-layout utilities (bytes/block/
// is_compressed + block_dims/row_pitch/row_count/image_size). Split out of the
// (uncompiled) api_interface.cpp so every backend can link them for block-aware
// (compressed) texture upload/readback. Declared in graphics_api.hpp.
#include "../graphics_api.hpp"

namespace window {

int texture_format_bytes_per_pixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::R8_UNORM:
    case TextureFormat::R8_SNORM:
    case TextureFormat::R8_UINT:
    case TextureFormat::R8_SINT:
    case TextureFormat::A8_UNORM:
    case TextureFormat::L8_UNORM:           return 1;

    case TextureFormat::R16_UNORM:
    case TextureFormat::R16_SNORM:
    case TextureFormat::R16_UINT:
    case TextureFormat::R16_SINT:
    case TextureFormat::R16_FLOAT:
    case TextureFormat::RG8_UNORM:
    case TextureFormat::RG8_SNORM:
    case TextureFormat::RG8_UINT:
    case TextureFormat::RG8_SINT:
    case TextureFormat::LA8_UNORM:          return 2;

    case TextureFormat::R32_UINT:
    case TextureFormat::R32_SINT:
    case TextureFormat::R32_FLOAT:
    case TextureFormat::RG16_UNORM:
    case TextureFormat::RG16_SNORM:
    case TextureFormat::RG16_UINT:
    case TextureFormat::RG16_SINT:
    case TextureFormat::RG16_FLOAT:
    case TextureFormat::RGBA8_UNORM:
    case TextureFormat::RGBA8_UNORM_SRGB:
    case TextureFormat::RGBA8_SNORM:
    case TextureFormat::RGBA8_UINT:
    case TextureFormat::RGBA8_SINT:
    case TextureFormat::BGRA8_UNORM:
    case TextureFormat::BGRA8_UNORM_SRGB:
    case TextureFormat::RGB10A2_UNORM:
    case TextureFormat::RGB10A2_UINT:
    case TextureFormat::RG11B10_FLOAT:
    case TextureFormat::RGB9E5_FLOAT:
    case TextureFormat::D24_UNORM_S8_UINT:
    case TextureFormat::D32_FLOAT:          return 4;

    case TextureFormat::RG32_UINT:
    case TextureFormat::RG32_SINT:
    case TextureFormat::RG32_FLOAT:
    case TextureFormat::RGBA16_UNORM:
    case TextureFormat::RGBA16_SNORM:
    case TextureFormat::RGBA16_UINT:
    case TextureFormat::RGBA16_SINT:
    case TextureFormat::RGBA16_FLOAT:
    case TextureFormat::D32_FLOAT_S8_UINT:  return 8;

    case TextureFormat::RGBA32_UINT:
    case TextureFormat::RGBA32_SINT:
    case TextureFormat::RGBA32_FLOAT:       return 16;

    case TextureFormat::D16_UNORM:          return 2;

    // Compressed formats: bytes per compressed block (4x4 pixels)
    case TextureFormat::BC1_UNORM:
    case TextureFormat::BC1_UNORM_SRGB:
    case TextureFormat::BC4_UNORM:
    case TextureFormat::BC4_SNORM:
    case TextureFormat::ETC1_RGB8:
    case TextureFormat::ETC2_RGB8:
    case TextureFormat::ETC2_RGB8_SRGB:
    case TextureFormat::ETC2_RGB8A1:
    case TextureFormat::ETC2_RGB8A1_SRGB:
    case TextureFormat::EAC_R11_UNORM:
    case TextureFormat::EAC_R11_SNORM:      return 8;

    case TextureFormat::BC2_UNORM:
    case TextureFormat::BC2_UNORM_SRGB:
    case TextureFormat::BC3_UNORM:
    case TextureFormat::BC3_UNORM_SRGB:
    case TextureFormat::BC5_UNORM:
    case TextureFormat::BC5_SNORM:
    case TextureFormat::BC6H_UF16:
    case TextureFormat::BC6H_SF16:
    case TextureFormat::BC7_UNORM:
    case TextureFormat::BC7_UNORM_SRGB:
    case TextureFormat::ETC2_RGBA8:
    case TextureFormat::ETC2_RGBA8_SRGB:
    case TextureFormat::EAC_RG11_UNORM:
    case TextureFormat::EAC_RG11_SNORM:
    case TextureFormat::ASTC_4x4_UNORM:
    case TextureFormat::ASTC_4x4_SRGB:
    case TextureFormat::ASTC_5x4_UNORM:
    case TextureFormat::ASTC_5x4_SRGB:
    case TextureFormat::ASTC_5x5_UNORM:
    case TextureFormat::ASTC_5x5_SRGB:
    case TextureFormat::ASTC_6x5_UNORM:
    case TextureFormat::ASTC_6x5_SRGB:
    case TextureFormat::ASTC_6x6_UNORM:
    case TextureFormat::ASTC_6x6_SRGB:
    case TextureFormat::ASTC_8x5_UNORM:
    case TextureFormat::ASTC_8x5_SRGB:
    case TextureFormat::ASTC_8x6_UNORM:
    case TextureFormat::ASTC_8x6_SRGB:
    case TextureFormat::ASTC_8x8_UNORM:
    case TextureFormat::ASTC_8x8_SRGB:
    case TextureFormat::ASTC_10x5_UNORM:
    case TextureFormat::ASTC_10x5_SRGB:
    case TextureFormat::ASTC_10x6_UNORM:
    case TextureFormat::ASTC_10x6_SRGB:
    case TextureFormat::ASTC_10x8_UNORM:
    case TextureFormat::ASTC_10x8_SRGB:
    case TextureFormat::ASTC_10x10_UNORM:
    case TextureFormat::ASTC_10x10_SRGB:
    case TextureFormat::ASTC_12x10_UNORM:
    case TextureFormat::ASTC_12x10_SRGB:
    case TextureFormat::ASTC_12x12_UNORM:
    case TextureFormat::ASTC_12x12_SRGB:    return 16;

    default:                                return 0;
    }
}

int texture_format_block_size(TextureFormat format) {
    switch (format) {
    case TextureFormat::BC1_UNORM:
    case TextureFormat::BC1_UNORM_SRGB:
    case TextureFormat::BC2_UNORM:
    case TextureFormat::BC2_UNORM_SRGB:
    case TextureFormat::BC3_UNORM:
    case TextureFormat::BC3_UNORM_SRGB:
    case TextureFormat::BC4_UNORM:
    case TextureFormat::BC4_SNORM:
    case TextureFormat::BC5_UNORM:
    case TextureFormat::BC5_SNORM:
    case TextureFormat::BC6H_UF16:
    case TextureFormat::BC6H_SF16:
    case TextureFormat::BC7_UNORM:
    case TextureFormat::BC7_UNORM_SRGB:
    case TextureFormat::ETC1_RGB8:
    case TextureFormat::ETC2_RGB8:
    case TextureFormat::ETC2_RGB8_SRGB:
    case TextureFormat::ETC2_RGBA8:
    case TextureFormat::ETC2_RGBA8_SRGB:
    case TextureFormat::ETC2_RGB8A1:
    case TextureFormat::ETC2_RGB8A1_SRGB:
    case TextureFormat::EAC_R11_UNORM:
    case TextureFormat::EAC_R11_SNORM:
    case TextureFormat::EAC_RG11_UNORM:
    case TextureFormat::EAC_RG11_SNORM:
    case TextureFormat::ASTC_4x4_UNORM:
    case TextureFormat::ASTC_4x4_SRGB:     return 4;

    case TextureFormat::ASTC_5x4_UNORM:
    case TextureFormat::ASTC_5x4_SRGB:
    case TextureFormat::ASTC_5x5_UNORM:
    case TextureFormat::ASTC_5x5_SRGB:     return 5;

    case TextureFormat::ASTC_6x5_UNORM:
    case TextureFormat::ASTC_6x5_SRGB:
    case TextureFormat::ASTC_6x6_UNORM:
    case TextureFormat::ASTC_6x6_SRGB:     return 6;

    case TextureFormat::ASTC_8x5_UNORM:
    case TextureFormat::ASTC_8x5_SRGB:
    case TextureFormat::ASTC_8x6_UNORM:
    case TextureFormat::ASTC_8x6_SRGB:
    case TextureFormat::ASTC_8x8_UNORM:
    case TextureFormat::ASTC_8x8_SRGB:     return 8;

    case TextureFormat::ASTC_10x5_UNORM:
    case TextureFormat::ASTC_10x5_SRGB:
    case TextureFormat::ASTC_10x6_UNORM:
    case TextureFormat::ASTC_10x6_SRGB:
    case TextureFormat::ASTC_10x8_UNORM:
    case TextureFormat::ASTC_10x8_SRGB:
    case TextureFormat::ASTC_10x10_UNORM:
    case TextureFormat::ASTC_10x10_SRGB:   return 10;

    case TextureFormat::ASTC_12x10_UNORM:
    case TextureFormat::ASTC_12x10_SRGB:
    case TextureFormat::ASTC_12x12_UNORM:
    case TextureFormat::ASTC_12x12_SRGB:   return 12;

    default:                                return 1;
    }
}

bool texture_format_is_compressed(TextureFormat format) {
    switch (format) {
    case TextureFormat::BC1_UNORM:
    case TextureFormat::BC1_UNORM_SRGB:
    case TextureFormat::BC2_UNORM:
    case TextureFormat::BC2_UNORM_SRGB:
    case TextureFormat::BC3_UNORM:
    case TextureFormat::BC3_UNORM_SRGB:
    case TextureFormat::BC4_UNORM:
    case TextureFormat::BC4_SNORM:
    case TextureFormat::BC5_UNORM:
    case TextureFormat::BC5_SNORM:
    case TextureFormat::BC6H_UF16:
    case TextureFormat::BC6H_SF16:
    case TextureFormat::BC7_UNORM:
    case TextureFormat::BC7_UNORM_SRGB:
    case TextureFormat::ETC1_RGB8:
    case TextureFormat::ETC2_RGB8:
    case TextureFormat::ETC2_RGB8_SRGB:
    case TextureFormat::ETC2_RGBA8:
    case TextureFormat::ETC2_RGBA8_SRGB:
    case TextureFormat::ETC2_RGB8A1:
    case TextureFormat::ETC2_RGB8A1_SRGB:
    case TextureFormat::EAC_R11_UNORM:
    case TextureFormat::EAC_R11_SNORM:
    case TextureFormat::EAC_RG11_UNORM:
    case TextureFormat::EAC_RG11_SNORM:
    case TextureFormat::ASTC_4x4_UNORM:
    case TextureFormat::ASTC_4x4_SRGB:
    case TextureFormat::ASTC_5x4_UNORM:
    case TextureFormat::ASTC_5x4_SRGB:
    case TextureFormat::ASTC_5x5_UNORM:
    case TextureFormat::ASTC_5x5_SRGB:
    case TextureFormat::ASTC_6x5_UNORM:
    case TextureFormat::ASTC_6x5_SRGB:
    case TextureFormat::ASTC_6x6_UNORM:
    case TextureFormat::ASTC_6x6_SRGB:
    case TextureFormat::ASTC_8x5_UNORM:
    case TextureFormat::ASTC_8x5_SRGB:
    case TextureFormat::ASTC_8x6_UNORM:
    case TextureFormat::ASTC_8x6_SRGB:
    case TextureFormat::ASTC_8x8_UNORM:
    case TextureFormat::ASTC_8x8_SRGB:
    case TextureFormat::ASTC_10x5_UNORM:
    case TextureFormat::ASTC_10x5_SRGB:
    case TextureFormat::ASTC_10x6_UNORM:
    case TextureFormat::ASTC_10x6_SRGB:
    case TextureFormat::ASTC_10x8_UNORM:
    case TextureFormat::ASTC_10x8_SRGB:
    case TextureFormat::ASTC_10x10_UNORM:
    case TextureFormat::ASTC_10x10_SRGB:
    case TextureFormat::ASTC_12x10_UNORM:
    case TextureFormat::ASTC_12x10_SRGB:
    case TextureFormat::ASTC_12x12_UNORM:
    case TextureFormat::ASTC_12x12_SRGB:   return true;
    default:                                return false;
    }
}

void texture_format_block_dims(TextureFormat format, int* out_w, int* out_h) {
    int w = 1, h = 1;
    if (texture_format_is_compressed(format)) {
        w = texture_format_block_size(format);   // block width (4/5/6/8/10/12)
        switch (format) {                        // override height for non-square ASTC
        case TextureFormat::ASTC_5x4_UNORM:  case TextureFormat::ASTC_5x4_SRGB:   h = 4; break;
        case TextureFormat::ASTC_6x5_UNORM:  case TextureFormat::ASTC_6x5_SRGB:
        case TextureFormat::ASTC_8x5_UNORM:  case TextureFormat::ASTC_8x5_SRGB:
        case TextureFormat::ASTC_10x5_UNORM: case TextureFormat::ASTC_10x5_SRGB:  h = 5; break;
        case TextureFormat::ASTC_8x6_UNORM:  case TextureFormat::ASTC_8x6_SRGB:
        case TextureFormat::ASTC_10x6_UNORM: case TextureFormat::ASTC_10x6_SRGB:  h = 6; break;
        case TextureFormat::ASTC_10x8_UNORM: case TextureFormat::ASTC_10x8_SRGB:  h = 8; break;
        case TextureFormat::ASTC_12x10_UNORM:case TextureFormat::ASTC_12x10_SRGB: h = 10; break;
        default: h = w; break;                   // BC/ETC/EAC (4x4) and square ASTC (NxN)
        }
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

size_t texture_format_row_pitch(TextureFormat format, int width) {
    if (width <= 0) return 0;
    int bw = 1, bh = 1; texture_format_block_dims(format, &bw, &bh);
    const int blocks_x = (width + bw - 1) / bw;
    return size_t(blocks_x) * size_t(texture_format_bytes_per_pixel(format));
}

int texture_format_row_count(TextureFormat format, int height) {
    if (height <= 0) return 0;
    int bw = 1, bh = 1; texture_format_block_dims(format, &bw, &bh);
    return (height + bh - 1) / bh;
}

size_t texture_format_image_size(TextureFormat format, int width, int height) {
    return texture_format_row_pitch(format, width) * size_t(texture_format_row_count(format, height));
}

} // namespace window
