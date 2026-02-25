/*
 * api_interface.cpp - Enum string converters for graphics_api.hpp
 *
 * Implements all to_string / parse_* functions declared in graphics_api.hpp
 * alongside their respective enums.
 */

#include "../graphics_api.hpp"
#include <cstring>

namespace window {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool iequal(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return false;
        ++a; ++b;
    }
    return *a == '\0' && *b == '\0';
}

// ---------------------------------------------------------------------------
// TextureFormat
// ---------------------------------------------------------------------------

const char* texture_format_to_string(TextureFormat format) {
    switch (format) {
    case TextureFormat::Unknown:            return "unknown";
    case TextureFormat::R8_UNORM:           return "r8_unorm";
    case TextureFormat::R8_SNORM:           return "r8_snorm";
    case TextureFormat::R8_UINT:            return "r8_uint";
    case TextureFormat::R8_SINT:            return "r8_sint";
    case TextureFormat::R16_UNORM:          return "r16_unorm";
    case TextureFormat::R16_SNORM:          return "r16_snorm";
    case TextureFormat::R16_UINT:           return "r16_uint";
    case TextureFormat::R16_SINT:           return "r16_sint";
    case TextureFormat::R16_FLOAT:          return "r16_float";
    case TextureFormat::RG8_UNORM:          return "rg8_unorm";
    case TextureFormat::RG8_SNORM:          return "rg8_snorm";
    case TextureFormat::RG8_UINT:           return "rg8_uint";
    case TextureFormat::RG8_SINT:           return "rg8_sint";
    case TextureFormat::R32_UINT:           return "r32_uint";
    case TextureFormat::R32_SINT:           return "r32_sint";
    case TextureFormat::R32_FLOAT:          return "r32_float";
    case TextureFormat::RG16_UNORM:         return "rg16_unorm";
    case TextureFormat::RG16_SNORM:         return "rg16_snorm";
    case TextureFormat::RG16_UINT:          return "rg16_uint";
    case TextureFormat::RG16_SINT:          return "rg16_sint";
    case TextureFormat::RG16_FLOAT:         return "rg16_float";
    case TextureFormat::RGBA8_UNORM:        return "rgba8_unorm";
    case TextureFormat::RGBA8_UNORM_SRGB:   return "rgba8_unorm_srgb";
    case TextureFormat::RGBA8_SNORM:        return "rgba8_snorm";
    case TextureFormat::RGBA8_UINT:         return "rgba8_uint";
    case TextureFormat::RGBA8_SINT:         return "rgba8_sint";
    case TextureFormat::BGRA8_UNORM:        return "bgra8_unorm";
    case TextureFormat::BGRA8_UNORM_SRGB:   return "bgra8_unorm_srgb";
    case TextureFormat::RGB10A2_UNORM:      return "rgb10a2_unorm";
    case TextureFormat::RGB10A2_UINT:       return "rgb10a2_uint";
    case TextureFormat::RG11B10_FLOAT:      return "rg11b10_float";
    case TextureFormat::RGB9E5_FLOAT:       return "rgb9e5_float";
    case TextureFormat::RG32_UINT:          return "rg32_uint";
    case TextureFormat::RG32_SINT:          return "rg32_sint";
    case TextureFormat::RG32_FLOAT:         return "rg32_float";
    case TextureFormat::RGBA16_UNORM:       return "rgba16_unorm";
    case TextureFormat::RGBA16_SNORM:       return "rgba16_snorm";
    case TextureFormat::RGBA16_UINT:        return "rgba16_uint";
    case TextureFormat::RGBA16_SINT:        return "rgba16_sint";
    case TextureFormat::RGBA16_FLOAT:       return "rgba16_float";
    case TextureFormat::RGBA32_UINT:        return "rgba32_uint";
    case TextureFormat::RGBA32_SINT:        return "rgba32_sint";
    case TextureFormat::RGBA32_FLOAT:       return "rgba32_float";
    case TextureFormat::D16_UNORM:          return "d16_unorm";
    case TextureFormat::D24_UNORM_S8_UINT:  return "d24_unorm_s8_uint";
    case TextureFormat::D32_FLOAT:          return "d32_float";
    case TextureFormat::D32_FLOAT_S8_UINT:  return "d32_float_s8_uint";
    case TextureFormat::BC1_UNORM:          return "bc1_unorm";
    case TextureFormat::BC1_UNORM_SRGB:     return "bc1_unorm_srgb";
    case TextureFormat::BC2_UNORM:          return "bc2_unorm";
    case TextureFormat::BC2_UNORM_SRGB:     return "bc2_unorm_srgb";
    case TextureFormat::BC3_UNORM:          return "bc3_unorm";
    case TextureFormat::BC3_UNORM_SRGB:     return "bc3_unorm_srgb";
    case TextureFormat::BC4_UNORM:          return "bc4_unorm";
    case TextureFormat::BC4_SNORM:          return "bc4_snorm";
    case TextureFormat::BC5_UNORM:          return "bc5_unorm";
    case TextureFormat::BC5_SNORM:          return "bc5_snorm";
    case TextureFormat::BC6H_UF16:          return "bc6h_uf16";
    case TextureFormat::BC6H_SF16:          return "bc6h_sf16";
    case TextureFormat::BC7_UNORM:          return "bc7_unorm";
    case TextureFormat::BC7_UNORM_SRGB:     return "bc7_unorm_srgb";
    case TextureFormat::ETC1_RGB8:          return "etc1_rgb8";
    case TextureFormat::ETC2_RGB8:          return "etc2_rgb8";
    case TextureFormat::ETC2_RGB8_SRGB:     return "etc2_rgb8_srgb";
    case TextureFormat::ETC2_RGBA8:         return "etc2_rgba8";
    case TextureFormat::ETC2_RGBA8_SRGB:    return "etc2_rgba8_srgb";
    case TextureFormat::ETC2_RGB8A1:        return "etc2_rgb8a1";
    case TextureFormat::ETC2_RGB8A1_SRGB:   return "etc2_rgb8a1_srgb";
    case TextureFormat::EAC_R11_UNORM:      return "eac_r11_unorm";
    case TextureFormat::EAC_R11_SNORM:      return "eac_r11_snorm";
    case TextureFormat::EAC_RG11_UNORM:     return "eac_rg11_unorm";
    case TextureFormat::EAC_RG11_SNORM:     return "eac_rg11_snorm";
    case TextureFormat::ASTC_4x4_UNORM:     return "astc_4x4_unorm";
    case TextureFormat::ASTC_4x4_SRGB:      return "astc_4x4_srgb";
    case TextureFormat::ASTC_5x4_UNORM:     return "astc_5x4_unorm";
    case TextureFormat::ASTC_5x4_SRGB:      return "astc_5x4_srgb";
    case TextureFormat::ASTC_5x5_UNORM:     return "astc_5x5_unorm";
    case TextureFormat::ASTC_5x5_SRGB:      return "astc_5x5_srgb";
    case TextureFormat::ASTC_6x5_UNORM:     return "astc_6x5_unorm";
    case TextureFormat::ASTC_6x5_SRGB:      return "astc_6x5_srgb";
    case TextureFormat::ASTC_6x6_UNORM:     return "astc_6x6_unorm";
    case TextureFormat::ASTC_6x6_SRGB:      return "astc_6x6_srgb";
    case TextureFormat::ASTC_8x5_UNORM:     return "astc_8x5_unorm";
    case TextureFormat::ASTC_8x5_SRGB:      return "astc_8x5_srgb";
    case TextureFormat::ASTC_8x6_UNORM:     return "astc_8x6_unorm";
    case TextureFormat::ASTC_8x6_SRGB:      return "astc_8x6_srgb";
    case TextureFormat::ASTC_8x8_UNORM:     return "astc_8x8_unorm";
    case TextureFormat::ASTC_8x8_SRGB:      return "astc_8x8_srgb";
    case TextureFormat::ASTC_10x5_UNORM:    return "astc_10x5_unorm";
    case TextureFormat::ASTC_10x5_SRGB:     return "astc_10x5_srgb";
    case TextureFormat::ASTC_10x6_UNORM:    return "astc_10x6_unorm";
    case TextureFormat::ASTC_10x6_SRGB:     return "astc_10x6_srgb";
    case TextureFormat::ASTC_10x8_UNORM:    return "astc_10x8_unorm";
    case TextureFormat::ASTC_10x8_SRGB:     return "astc_10x8_srgb";
    case TextureFormat::ASTC_10x10_UNORM:   return "astc_10x10_unorm";
    case TextureFormat::ASTC_10x10_SRGB:    return "astc_10x10_srgb";
    case TextureFormat::ASTC_12x10_UNORM:   return "astc_12x10_unorm";
    case TextureFormat::ASTC_12x10_SRGB:    return "astc_12x10_srgb";
    case TextureFormat::ASTC_12x12_UNORM:   return "astc_12x12_unorm";
    case TextureFormat::ASTC_12x12_SRGB:    return "astc_12x12_srgb";
    case TextureFormat::A8_UNORM:           return "a8_unorm";
    case TextureFormat::L8_UNORM:           return "l8_unorm";
    case TextureFormat::LA8_UNORM:          return "la8_unorm";
    default:                                return "unknown";
    }
}

bool parse_texture_format(const char* s, TextureFormat* out) {
    if (!s || !out) return false;
    for (int i = 0; i < static_cast<int>(TextureFormat::Count); ++i) {
        auto fmt = static_cast<TextureFormat>(i);
        if (iequal(s, texture_format_to_string(fmt))) {
            *out = fmt;
            return true;
        }
    }
    return false;
}

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

bool texture_format_is_depth_stencil(TextureFormat format) {
    switch (format) {
    case TextureFormat::D16_UNORM:
    case TextureFormat::D24_UNORM_S8_UINT:
    case TextureFormat::D32_FLOAT:
    case TextureFormat::D32_FLOAT_S8_UINT:  return true;
    default:                                return false;
    }
}

bool texture_format_is_srgb(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8_UNORM_SRGB:
    case TextureFormat::BGRA8_UNORM_SRGB:
    case TextureFormat::BC1_UNORM_SRGB:
    case TextureFormat::BC2_UNORM_SRGB:
    case TextureFormat::BC3_UNORM_SRGB:
    case TextureFormat::BC7_UNORM_SRGB:
    case TextureFormat::ETC2_RGB8_SRGB:
    case TextureFormat::ETC2_RGBA8_SRGB:
    case TextureFormat::ETC2_RGB8A1_SRGB:
    case TextureFormat::ASTC_4x4_SRGB:
    case TextureFormat::ASTC_5x4_SRGB:
    case TextureFormat::ASTC_5x5_SRGB:
    case TextureFormat::ASTC_6x5_SRGB:
    case TextureFormat::ASTC_6x6_SRGB:
    case TextureFormat::ASTC_8x5_SRGB:
    case TextureFormat::ASTC_8x6_SRGB:
    case TextureFormat::ASTC_8x8_SRGB:
    case TextureFormat::ASTC_10x5_SRGB:
    case TextureFormat::ASTC_10x6_SRGB:
    case TextureFormat::ASTC_10x8_SRGB:
    case TextureFormat::ASTC_10x10_SRGB:
    case TextureFormat::ASTC_12x10_SRGB:
    case TextureFormat::ASTC_12x12_SRGB:   return true;
    default:                                return false;
    }
}

bool texture_format_has_alpha(TextureFormat format) {
    switch (format) {
    case TextureFormat::RG8_UNORM:
    case TextureFormat::RG8_SNORM:
    case TextureFormat::RG8_UINT:
    case TextureFormat::RG8_SINT:
    case TextureFormat::RGBA8_UNORM:
    case TextureFormat::RGBA8_UNORM_SRGB:
    case TextureFormat::RGBA8_SNORM:
    case TextureFormat::RGBA8_UINT:
    case TextureFormat::RGBA8_SINT:
    case TextureFormat::BGRA8_UNORM:
    case TextureFormat::BGRA8_UNORM_SRGB:
    case TextureFormat::RGB10A2_UNORM:
    case TextureFormat::RGB10A2_UINT:
    case TextureFormat::RG16_UNORM:
    case TextureFormat::RG16_SNORM:
    case TextureFormat::RG16_UINT:
    case TextureFormat::RG16_SINT:
    case TextureFormat::RG16_FLOAT:
    case TextureFormat::RGBA16_UNORM:
    case TextureFormat::RGBA16_SNORM:
    case TextureFormat::RGBA16_UINT:
    case TextureFormat::RGBA16_SINT:
    case TextureFormat::RGBA16_FLOAT:
    case TextureFormat::RG32_UINT:
    case TextureFormat::RG32_SINT:
    case TextureFormat::RG32_FLOAT:
    case TextureFormat::RGBA32_UINT:
    case TextureFormat::RGBA32_SINT:
    case TextureFormat::RGBA32_FLOAT:
    case TextureFormat::A8_UNORM:
    case TextureFormat::LA8_UNORM:
    case TextureFormat::BC2_UNORM:
    case TextureFormat::BC2_UNORM_SRGB:
    case TextureFormat::BC3_UNORM:
    case TextureFormat::BC3_UNORM_SRGB:
    case TextureFormat::BC7_UNORM:
    case TextureFormat::BC7_UNORM_SRGB:
    case TextureFormat::ETC2_RGBA8:
    case TextureFormat::ETC2_RGBA8_SRGB:
    case TextureFormat::ETC2_RGB8A1:
    case TextureFormat::ETC2_RGB8A1_SRGB:
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

// ---------------------------------------------------------------------------
// BlendFactor
// ---------------------------------------------------------------------------

const char* blend_factor_to_string(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:             return "zero";
    case BlendFactor::One:              return "one";
    case BlendFactor::SrcColor:         return "src_color";
    case BlendFactor::InvSrcColor:      return "inv_src_color";
    case BlendFactor::SrcAlpha:         return "src_alpha";
    case BlendFactor::InvSrcAlpha:      return "inv_src_alpha";
    case BlendFactor::DstColor:         return "dst_color";
    case BlendFactor::InvDstColor:      return "inv_dst_color";
    case BlendFactor::DstAlpha:         return "dst_alpha";
    case BlendFactor::InvDstAlpha:      return "inv_dst_alpha";
    case BlendFactor::SrcAlphaSat:      return "src_alpha_sat";
    case BlendFactor::BlendFactor:      return "blend_factor";
    case BlendFactor::InvBlendFactor:   return "inv_blend_factor";
    default:                            return "zero";
    }
}

bool parse_blend_factor(const char* s, BlendFactor* out) {
    if (!s || !out) return false;
    static const struct { const char* name; BlendFactor value; } table[] = {
        { "zero",             BlendFactor::Zero           },
        { "one",              BlendFactor::One            },
        { "src_color",        BlendFactor::SrcColor       },
        { "inv_src_color",    BlendFactor::InvSrcColor    },
        { "src_alpha",        BlendFactor::SrcAlpha       },
        { "inv_src_alpha",    BlendFactor::InvSrcAlpha    },
        { "dst_color",        BlendFactor::DstColor       },
        { "inv_dst_color",    BlendFactor::InvDstColor    },
        { "dst_alpha",        BlendFactor::DstAlpha       },
        { "inv_dst_alpha",    BlendFactor::InvDstAlpha    },
        { "src_alpha_sat",    BlendFactor::SrcAlphaSat    },
        { "blend_factor",     BlendFactor::BlendFactor    },
        { "inv_blend_factor", BlendFactor::InvBlendFactor },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// BlendOp
// ---------------------------------------------------------------------------

const char* blend_op_to_string(BlendOp op) {
    switch (op) {
    case BlendOp::Add:          return "add";
    case BlendOp::Subtract:     return "subtract";
    case BlendOp::RevSubtract:  return "rev_subtract";
    case BlendOp::Min:          return "min";
    case BlendOp::Max:          return "max";
    default:                    return "add";
    }
}

bool parse_blend_op(const char* s, BlendOp* out) {
    if (!s || !out) return false;
    static const struct { const char* name; BlendOp value; } table[] = {
        { "add",          BlendOp::Add         },
        { "subtract",     BlendOp::Subtract    },
        { "rev_subtract", BlendOp::RevSubtract },
        { "min",          BlendOp::Min         },
        { "max",          BlendOp::Max         },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// CompareFunc
// ---------------------------------------------------------------------------

const char* compare_func_to_string(CompareFunc func) {
    switch (func) {
    case CompareFunc::Never:        return "never";
    case CompareFunc::Less:         return "less";
    case CompareFunc::Equal:        return "equal";
    case CompareFunc::LessEqual:    return "less_equal";
    case CompareFunc::Greater:      return "greater";
    case CompareFunc::NotEqual:     return "not_equal";
    case CompareFunc::GreaterEqual: return "greater_equal";
    case CompareFunc::Always:       return "always";
    default:                        return "always";
    }
}

bool parse_compare_func(const char* s, CompareFunc* out) {
    if (!s || !out) return false;
    static const struct { const char* name; CompareFunc value; } table[] = {
        { "never",         CompareFunc::Never        },
        { "less",          CompareFunc::Less         },
        { "equal",         CompareFunc::Equal        },
        { "less_equal",    CompareFunc::LessEqual    },
        { "greater",       CompareFunc::Greater      },
        { "not_equal",     CompareFunc::NotEqual     },
        { "greater_equal", CompareFunc::GreaterEqual },
        { "always",        CompareFunc::Always       },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// StencilOp
// ---------------------------------------------------------------------------

const char* stencil_op_to_string(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:       return "keep";
    case StencilOp::Zero:       return "zero";
    case StencilOp::Replace:    return "replace";
    case StencilOp::IncrSat:    return "incr_sat";
    case StencilOp::DecrSat:    return "decr_sat";
    case StencilOp::Invert:     return "invert";
    case StencilOp::IncrWrap:   return "incr_wrap";
    case StencilOp::DecrWrap:   return "decr_wrap";
    default:                    return "keep";
    }
}

bool parse_stencil_op(const char* s, StencilOp* out) {
    if (!s || !out) return false;
    static const struct { const char* name; StencilOp value; } table[] = {
        { "keep",      StencilOp::Keep     },
        { "zero",      StencilOp::Zero     },
        { "replace",   StencilOp::Replace  },
        { "incr_sat",  StencilOp::IncrSat  },
        { "decr_sat",  StencilOp::DecrSat  },
        { "invert",    StencilOp::Invert   },
        { "incr_wrap", StencilOp::IncrWrap },
        { "decr_wrap", StencilOp::DecrWrap },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// FillMode
// ---------------------------------------------------------------------------

const char* fill_mode_to_string(FillMode mode) {
    switch (mode) {
    case FillMode::Solid:       return "solid";
    case FillMode::Wireframe:   return "wireframe";
    default:                    return "solid";
    }
}

bool parse_fill_mode(const char* s, FillMode* out) {
    if (!s || !out) return false;
    if (iequal(s, "solid"))     { *out = FillMode::Solid;     return true; }
    if (iequal(s, "wireframe")) { *out = FillMode::Wireframe; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// CullMode
// ---------------------------------------------------------------------------

const char* cull_mode_to_string(CullMode mode) {
    switch (mode) {
    case CullMode::None:    return "none";
    case CullMode::Front:   return "front";
    case CullMode::Back:    return "back";
    default:                return "none";
    }
}

bool parse_cull_mode(const char* s, CullMode* out) {
    if (!s || !out) return false;
    if (iequal(s, "none"))  { *out = CullMode::None;  return true; }
    if (iequal(s, "front")) { *out = CullMode::Front; return true; }
    if (iequal(s, "back"))  { *out = CullMode::Back;  return true; }
    return false;
}

// ---------------------------------------------------------------------------
// FrontFace
// ---------------------------------------------------------------------------

const char* front_face_to_string(FrontFace face) {
    switch (face) {
    case FrontFace::CounterClockwise:   return "ccw";
    case FrontFace::Clockwise:          return "cw";
    default:                            return "ccw";
    }
}

bool parse_front_face(const char* s, FrontFace* out) {
    if (!s || !out) return false;
    if (iequal(s, "ccw") || iequal(s, "counter_clockwise") || iequal(s, "counterclockwise")) {
        *out = FrontFace::CounterClockwise; return true;
    }
    if (iequal(s, "cw") || iequal(s, "clockwise")) {
        *out = FrontFace::Clockwise; return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// FilterMode
// ---------------------------------------------------------------------------

const char* filter_mode_to_string(FilterMode mode) {
    switch (mode) {
    case FilterMode::Point:         return "point";
    case FilterMode::Linear:        return "linear";
    case FilterMode::Anisotropic:   return "anisotropic";
    default:                        return "linear";
    }
}

bool parse_filter_mode(const char* s, FilterMode* out) {
    if (!s || !out) return false;
    if (iequal(s, "point"))       { *out = FilterMode::Point;       return true; }
    if (iequal(s, "linear"))      { *out = FilterMode::Linear;      return true; }
    if (iequal(s, "anisotropic")) { *out = FilterMode::Anisotropic; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// AddressMode
// ---------------------------------------------------------------------------

const char* address_mode_to_string(AddressMode mode) {
    switch (mode) {
    case AddressMode::Wrap:         return "wrap";
    case AddressMode::Mirror:       return "mirror";
    case AddressMode::Clamp:        return "clamp";
    case AddressMode::Border:       return "border";
    case AddressMode::MirrorOnce:   return "mirror_once";
    default:                        return "wrap";
    }
}

bool parse_address_mode(const char* s, AddressMode* out) {
    if (!s || !out) return false;
    if (iequal(s, "wrap"))        { *out = AddressMode::Wrap;       return true; }
    if (iequal(s, "mirror"))      { *out = AddressMode::Mirror;     return true; }
    if (iequal(s, "clamp"))       { *out = AddressMode::Clamp;      return true; }
    if (iequal(s, "border"))      { *out = AddressMode::Border;     return true; }
    if (iequal(s, "mirror_once")) { *out = AddressMode::MirrorOnce; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// PrimitiveTopology
// ---------------------------------------------------------------------------

const char* primitive_topology_to_string(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList:          return "point_list";
    case PrimitiveTopology::LineList:           return "line_list";
    case PrimitiveTopology::LineStrip:          return "line_strip";
    case PrimitiveTopology::TriangleList:       return "triangle_list";
    case PrimitiveTopology::TriangleStrip:      return "triangle_strip";
    case PrimitiveTopology::LineListAdj:        return "line_list_adj";
    case PrimitiveTopology::LineStripAdj:       return "line_strip_adj";
    case PrimitiveTopology::TriangleListAdj:    return "triangle_list_adj";
    case PrimitiveTopology::TriangleStripAdj:   return "triangle_strip_adj";
    default:                                    return "triangle_list";
    }
}

bool parse_primitive_topology(const char* s, PrimitiveTopology* out) {
    if (!s || !out) return false;
    static const struct { const char* name; PrimitiveTopology value; } table[] = {
        { "point_list",          PrimitiveTopology::PointList         },
        { "line_list",           PrimitiveTopology::LineList          },
        { "line_strip",          PrimitiveTopology::LineStrip         },
        { "triangle_list",       PrimitiveTopology::TriangleList      },
        { "triangle_strip",      PrimitiveTopology::TriangleStrip     },
        { "line_list_adj",       PrimitiveTopology::LineListAdj       },
        { "line_strip_adj",      PrimitiveTopology::LineStripAdj      },
        { "triangle_list_adj",   PrimitiveTopology::TriangleListAdj   },
        { "triangle_strip_adj",  PrimitiveTopology::TriangleStripAdj  },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// VertexFormat
// ---------------------------------------------------------------------------

int vertex_format_size(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float1:  return 4;
    case VertexFormat::Float2:  return 8;
    case VertexFormat::Float3:  return 12;
    case VertexFormat::Float4:  return 16;
    case VertexFormat::Int1:    return 4;
    case VertexFormat::Int2:    return 8;
    case VertexFormat::Int3:    return 12;
    case VertexFormat::Int4:    return 16;
    case VertexFormat::UInt1:   return 4;
    case VertexFormat::UInt2:   return 8;
    case VertexFormat::UInt3:   return 12;
    case VertexFormat::UInt4:   return 16;
    case VertexFormat::Short2:  return 4;
    case VertexFormat::Short4:  return 8;
    case VertexFormat::Short2N: return 4;
    case VertexFormat::Short4N: return 8;
    case VertexFormat::UShort2: return 4;
    case VertexFormat::UShort4: return 8;
    case VertexFormat::UShort2N:return 4;
    case VertexFormat::UShort4N:return 8;
    case VertexFormat::Byte4:   return 4;
    case VertexFormat::Byte4N:  return 4;
    case VertexFormat::UByte4:  return 4;
    case VertexFormat::UByte4N: return 4;
    case VertexFormat::Half2:   return 4;
    case VertexFormat::Half4:   return 8;
    case VertexFormat::RGB10A2: return 4;
    default:                    return 0;
    }
}

const char* vertex_format_to_string(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float1:   return "float1";
    case VertexFormat::Float2:   return "float2";
    case VertexFormat::Float3:   return "float3";
    case VertexFormat::Float4:   return "float4";
    case VertexFormat::Int1:     return "int1";
    case VertexFormat::Int2:     return "int2";
    case VertexFormat::Int3:     return "int3";
    case VertexFormat::Int4:     return "int4";
    case VertexFormat::UInt1:    return "uint1";
    case VertexFormat::UInt2:    return "uint2";
    case VertexFormat::UInt3:    return "uint3";
    case VertexFormat::UInt4:    return "uint4";
    case VertexFormat::Short2:   return "short2";
    case VertexFormat::Short4:   return "short4";
    case VertexFormat::Short2N:  return "short2n";
    case VertexFormat::Short4N:  return "short4n";
    case VertexFormat::UShort2:  return "ushort2";
    case VertexFormat::UShort4:  return "ushort4";
    case VertexFormat::UShort2N: return "ushort2n";
    case VertexFormat::UShort4N: return "ushort4n";
    case VertexFormat::Byte4:    return "byte4";
    case VertexFormat::Byte4N:   return "byte4n";
    case VertexFormat::UByte4:   return "ubyte4";
    case VertexFormat::UByte4N:  return "ubyte4n";
    case VertexFormat::Half2:    return "half2";
    case VertexFormat::Half4:    return "half4";
    case VertexFormat::RGB10A2:  return "rgb10a2";
    default:                     return "float1";
    }
}

bool parse_vertex_format(const char* s, VertexFormat* out) {
    if (!s || !out) return false;
    static const struct { const char* name; VertexFormat value; } table[] = {
        { "float1",   VertexFormat::Float1   },
        { "float2",   VertexFormat::Float2   },
        { "float3",   VertexFormat::Float3   },
        { "float4",   VertexFormat::Float4   },
        { "int1",     VertexFormat::Int1     },
        { "int2",     VertexFormat::Int2     },
        { "int3",     VertexFormat::Int3     },
        { "int4",     VertexFormat::Int4     },
        { "uint1",    VertexFormat::UInt1    },
        { "uint2",    VertexFormat::UInt2    },
        { "uint3",    VertexFormat::UInt3    },
        { "uint4",    VertexFormat::UInt4    },
        { "short2",   VertexFormat::Short2   },
        { "short4",   VertexFormat::Short4   },
        { "short2n",  VertexFormat::Short2N  },
        { "short4n",  VertexFormat::Short4N  },
        { "ushort2",  VertexFormat::UShort2  },
        { "ushort4",  VertexFormat::UShort4  },
        { "ushort2n", VertexFormat::UShort2N },
        { "ushort4n", VertexFormat::UShort4N },
        { "byte4",    VertexFormat::Byte4    },
        { "byte4n",   VertexFormat::Byte4N   },
        { "ubyte4",   VertexFormat::UByte4   },
        { "ubyte4n",  VertexFormat::UByte4N  },
        { "half2",    VertexFormat::Half2    },
        { "half4",    VertexFormat::Half4    },
        { "rgb10a2",  VertexFormat::RGB10A2  },
    };
    for (auto& e : table) {
        if (iequal(s, e.name)) { *out = e.value; return true; }
    }
    return false;
}

} // namespace window
