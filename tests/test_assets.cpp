/*
 * test_assets.cpp - Headless unit tests for the asset system (renderer/assets/).
 *
 * Covers the parts that need no GPU device: image decoding (from an in-memory BMP),
 * RGBA expansion, CPU model data + bounds, the GpuModel vertex layout, texture-cache
 * keys, and (when Assimp is compiled in) decoding a model from memory.
 */

#include "renderer/assets/asset_manager.hpp"

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace window;
using namespace window::gfx;

static int g_passed = 0, g_failed = 0;

#define CHECK(cond) do { \
    if (cond) { ++g_passed; } \
    else { ++g_failed; std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__); } \
} while (0)

// --- Build a 2x2, 24-bpp BMP in memory: TL=red TR=green BL=blue BR=white ----
static void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 24));
}
static void put_u16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8));
}
static std::vector<uint8_t> make_bmp_2x2() {
    std::vector<uint8_t> b;
    b.push_back('B'); b.push_back('M');
    put_u32(b, 70);          // file size
    put_u32(b, 0);           // reserved
    put_u32(b, 54);          // pixel data offset
    put_u32(b, 40);          // DIB header size
    put_u32(b, 2);           // width
    put_u32(b, 2);           // height (bottom-up)
    put_u16(b, 1);           // planes
    put_u16(b, 24);          // bpp
    put_u32(b, 0);           // compression (BI_RGB)
    put_u32(b, 16);          // image size
    put_u32(b, 2835);        // x ppm
    put_u32(b, 2835);        // y ppm
    put_u32(b, 0);           // colors used
    put_u32(b, 0);           // colors important
    // Pixels are bottom-up, BGR, rows padded to 4 bytes (2px*3 = 6 -> +2 pad).
    // Bottom row (image y=1): blue, white.
    b.push_back(255); b.push_back(0);   b.push_back(0);    // blue  (B,G,R)
    b.push_back(255); b.push_back(255); b.push_back(255);  // white
    b.push_back(0);   b.push_back(0);                      // padding
    // Top row (image y=0): red, green.
    b.push_back(0);   b.push_back(0);   b.push_back(255);  // red
    b.push_back(0);   b.push_back(255); b.push_back(0);    // green
    b.push_back(0);   b.push_back(0);                      // padding
    return b;
}

static void test_image_decode() {
    std::printf("test_image_decode\n");
    auto bmp = make_bmp_2x2();

    int w = 0, h = 0, c = 0;
    CHECK(Image::info_from_memory(bmp.data(), bmp.size(), &w, &h, &c));
    CHECK(w == 2 && h == 2 && c == 3);

    Image img;
    CHECK(Image::load_from_memory(bmp.data(), bmp.size(), &img));
    CHECK(img.valid());
    CHECK(img.width == 2 && img.height == 2 && img.channels == 3);
    CHECK(img.type == ImagePixelType::U8);
    // Top-left pixel is red (origin top-left after decode).
    CHECK(img.pixels[0] == 255 && img.pixels[1] == 0 && img.pixels[2] == 0);
    // Top-right pixel is green.
    CHECK(img.pixels[3] == 0 && img.pixels[4] == 255 && img.pixels[5] == 0);
    CHECK(img.byte_size() == size_t(2 * 2 * 3));
    CHECK(img.texture_format(false) == TextureFormat::RGBA8_UNORM);  // 3->4 channel format
    CHECK(img.texture_format(true)  == TextureFormat::RGBA8_UNORM_SRGB);

    // Forced 4 channels: alpha filled opaque.
    Image rgba;
    CHECK(Image::load_from_memory(bmp.data(), bmp.size(), &rgba, 4));
    CHECK(rgba.channels == 4);
    CHECK(rgba.pixels[3] == 255);

    // Flip vertically swaps top/bottom rows: top-left becomes blue.
    Image flipped;
    CHECK(Image::load_from_memory(bmp.data(), bmp.size(), &flipped, 0, true));
    CHECK(flipped.pixels[0] == 0 && flipped.pixels[1] == 0 && flipped.pixels[2] == 255);
}

static void test_expand_rgba() {
    std::printf("test_expand_rgba\n");
    auto bmp = make_bmp_2x2();
    Image rgb;
    CHECK(Image::load_from_memory(bmp.data(), bmp.size(), &rgb));
    CHECK(rgb.channels == 3);

    Image rgba;
    CHECK(expand_to_rgba(rgb, &rgba));
    CHECK(rgba.channels == 4);
    CHECK(rgba.width == 2 && rgba.height == 2);
    CHECK(rgba.pixels[0] == 255 && rgba.pixels[1] == 0 && rgba.pixels[2] == 0 && rgba.pixels[3] == 255);
}

static void test_bad_image() {
    std::printf("test_bad_image\n");
    const uint8_t junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    Image img;
    CHECK(!Image::load_from_memory(junk, sizeof(junk), &img));
    CHECK(!img.valid());
    CHECK(Image::last_error()[0] != '\0');
}

// --- Build a 2x2 uncompressed R8G8B8A8 DDS: TL=red TR=green BL=blue BR=white --
static void dds_put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = uint8_t(v); b[off + 1] = uint8_t(v >> 8);
    b[off + 2] = uint8_t(v >> 16); b[off + 3] = uint8_t(v >> 24);
}
static std::vector<uint8_t> make_dds_rgba_2x2() {
    std::vector<uint8_t> b(128, 0);          // "DDS " magic (4) + DDS_HEADER (124)
    b[0] = 'D'; b[1] = 'D'; b[2] = 'S'; b[3] = ' ';
    dds_put32(b, 4, 124);          // dwSize
    dds_put32(b, 8, 0x100F);       // dwFlags: caps|height|width|pitch|pixelformat
    dds_put32(b, 12, 2);           // dwHeight
    dds_put32(b, 16, 2);           // dwWidth
    dds_put32(b, 20, 8);           // dwPitchOrLinearSize = width*4
    dds_put32(b, 76, 32);          // pixelformat dwSize
    dds_put32(b, 80, 0x41);        // pixelformat dwFlags: RGB | ALPHAPIXELS
    dds_put32(b, 88, 32);          // dwRGBBitCount
    dds_put32(b, 92, 0x000000FF);  // R mask
    dds_put32(b, 96, 0x0000FF00);  // G mask
    dds_put32(b, 100, 0x00FF0000); // B mask
    dds_put32(b, 104, 0xFF000000); // A mask
    dds_put32(b, 108, 0x1000);     // dwCaps: texture
    const uint8_t px[16] = { 255,0,0,255,  0,255,0,255,    // top row:    red,  green
                             0,0,255,255,  255,255,255,255 }; // bottom row: blue, white
    b.insert(b.end(), px, px + 16);
    return b;
}

static void test_dds() {
    std::printf("test_dds (supported=%s)\n", Image::dds_supported() ? "yes" : "no");
    auto d = make_dds_rgba_2x2();
    if (!Image::dds_supported()) {
        Image img;
        CHECK(!Image::load_dds_from_memory(d.data(), d.size(), &img));  // not built -> graceful fail
        return;
    }
    Image img;
    CHECK(Image::load_dds_from_memory(d.data(), d.size(), &img));
    CHECK(img.valid());
    CHECK(img.width == 2 && img.height == 2 && img.channels == 4);
    CHECK(img.type == ImagePixelType::U8);
    CHECK(img.pixels[0] == 255 && img.pixels[1] == 0 && img.pixels[2] == 0 && img.pixels[3] == 255); // red TL

    // load_from_memory() auto-detects DDS by magic and routes to the DDS decoder.
    Image img2;
    CHECK(Image::load_from_memory(d.data(), d.size(), &img2));
    CHECK(img2.width == 2 && img2.channels == 4);

    // info_from_memory() reads the DDS header dimensions.
    int w = 0, h = 0, c = 0;
    CHECK(Image::info_from_memory(d.data(), d.size(), &w, &h, &c));
    CHECK(w == 2 && h == 2 && c == 4);
}

static void test_exr() {
    std::printf("test_exr (supported=%s)\n", Image::exr_supported() ? "yes" : "no");
    // No tiny hand-authored EXR, but loading non-EXR bytes as EXR must fail gracefully
    // (this also forces the tinyexr symbols to link when EXR support is built in).
    const uint8_t junk[32] = {0};
    Image img;
    CHECK(!Image::load_exr_from_memory(junk, sizeof(junk), &img));
    CHECK(!img.valid());
}

// --- Build a minimal single-mip KTX1 container (12-byte id + 13 u32 header) -----
static std::vector<uint8_t> make_ktx1(uint32_t gl_internal, int w, int h,
                                      const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b(64, 0);
    const uint8_t id[12] = { 0xAB,'K','T','X',' ','1','1', 0xBB, 0x0D,0x0A,0x1A,0x0A };
    for (int i = 0; i < 12; ++i) b[i] = id[i];
    dds_put32(b, 12, 0x04030201);    // endianness
    dds_put32(b, 28, gl_internal);   // glInternalFormat
    dds_put32(b, 32, 0x1908);        // glBaseInternalFormat = GL_RGBA
    dds_put32(b, 36, uint32_t(w));   // pixelWidth
    dds_put32(b, 40, uint32_t(h));   // pixelHeight
    dds_put32(b, 52, 1);             // numberOfFaces
    dds_put32(b, 56, 1);             // numberOfMipmapLevels
    // bytesOfKeyValueData @60 = 0, then mip 0: u32 imageSize + data.
    std::vector<uint8_t> sz(4); dds_put32(sz, 0, uint32_t(payload.size()));
    b.insert(b.end(), sz.begin(), sz.end());
    b.insert(b.end(), payload.begin(), payload.end());
    return b;
}

static void test_ktx() {
    std::printf("test_ktx (etc2=%s astc=%s)\n",
                Image::etc2_supported() ? "yes" : "no",
                Image::astc_supported() ? "yes" : "no");

    // Uncompressed RGBA8 (GL_RGBA8 = 0x8058), 2x2: exact round-trip through the container.
    {
        std::vector<uint8_t> px = { 255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255 };
        auto ktx = make_ktx1(0x8058, 2, 2, px);
        Image img;
        CHECK(Image::load_ktx_from_memory(ktx.data(), ktx.size(), &img));
        CHECK(img.valid() && img.width == 2 && img.height == 2 && img.channels == 4);
        CHECK(img.pixels[0] == 255 && img.pixels[1] == 0 && img.pixels[2] == 0 && img.pixels[3] == 255);
        // load_from_memory auto-detects the KTX magic.
        Image img2;
        CHECK(Image::load_from_memory(ktx.data(), ktx.size(), &img2));
        CHECK(img2.width == 2 && img2.channels == 4);
        int w = 0, h = 0, c = 0;
        CHECK(Image::info_from_memory(ktx.data(), ktx.size(), &w, &h, &c));
        CHECK(w == 2 && h == 2 && c == 4);
    }

    // ETC2: run the real detex path on a 4x4 RGBA8_ETC2_EAC block (16 bytes). We only
    // assert it decodes to a valid 4x4 RGBA image (exact colors depend on the encoder).
    if (Image::etc2_supported()) {
        std::vector<uint8_t> block(16, 0);            // one ETC2_EAC block
        auto ktx = make_ktx1(0x9278, 4, 4, block);    // GL_COMPRESSED_RGBA8_ETC2_EAC
        Image img;
        CHECK(Image::load_ktx_from_memory(ktx.data(), ktx.size(), &img));
        CHECK(img.valid() && img.width == 4 && img.height == 4 && img.channels == 4);

        std::vector<uint8_t> rgb_block(8, 0);         // one ETC2 RGB block
        auto ktx_rgb = make_ktx1(0x9274, 4, 4, rgb_block);  // GL_COMPRESSED_RGB8_ETC2
        Image img_rgb;
        CHECK(Image::load_ktx_from_memory(ktx_rgb.data(), ktx_rgb.size(), &img_rgb));
        CHECK(img_rgb.valid() && img_rgb.width == 4 && img_rgb.height == 4);
    } else {
        std::vector<uint8_t> block(16, 0);
        auto ktx = make_ktx1(0x9278, 4, 4, block);
        Image img;
        CHECK(!Image::load_ktx_from_memory(ktx.data(), ktx.size(), &img));  // graceful: not built
    }

    // ASTC: run the real astc_dec path on a 4x4 ASTC block (16 bytes).
    if (Image::astc_supported()) {
        std::vector<uint8_t> block(16, 0);
        auto ktx = make_ktx1(0x93B0, 4, 4, block);    // GL_COMPRESSED_RGBA_ASTC_4x4
        Image img;
        CHECK(Image::load_ktx_from_memory(ktx.data(), ktx.size(), &img));
        CHECK(img.valid() && img.width == 4 && img.height == 4 && img.channels == 4);

        // Raw ARM .astc container: 16-byte header (magic, 4x4 block, dims) + one block.
        std::vector<uint8_t> astc = { 0x13,0xAB,0xA1,0x5C, 4,4,1, 4,0,0, 4,0,0, 1,0,0 };
        astc.insert(astc.end(), 16, 0);
        Image aimg;
        CHECK(Image::load_astc_from_memory(astc.data(), astc.size(), &aimg));
        CHECK(aimg.valid() && aimg.width == 4 && aimg.height == 4 && aimg.channels == 4);
    }
}

static void test_model_data_bounds() {
    std::printf("test_model_data_bounds\n");
    ModelData m;
    MeshVertex a, b, c;
    a.position = math::Vec3(-1, -2, -3);
    b.position = math::Vec3( 4,  5,  6);
    c.position = math::Vec3( 0,  1,  2);
    m.vertices = { a, b, c };
    m.indices  = { 0, 1, 2 };
    SubMesh sm; sm.index_offset = 0; sm.index_count = 3; sm.material = -1;
    m.submeshes.push_back(sm);
    m.compute_bounds();

    CHECK(m.triangle_count() == 1);
    CHECK(!m.empty());
    CHECK(m.bounds.valid());
    CHECK(m.bounds.min.x == -1 && m.bounds.min.y == -2 && m.bounds.min.z == -3);
    CHECK(m.bounds.max.x ==  4 && m.bounds.max.y ==  5 && m.bounds.max.z ==  6);
    CHECK(m.submeshes[0].bounds.valid());
    CHECK(m.vertex_bytes() == 3 * sizeof(MeshVertex));
    CHECK(m.index_bytes()  == 3 * sizeof(uint32_t));
}

static void test_vertex_layout() {
    std::printf("test_vertex_layout\n");
    VertexLayout l = GpuModel::vertex_layout();
    CHECK(l.attribute_count == 6);
    CHECK(l.buffer_count == 1);
    CHECK(l.strides[0] == sizeof(MeshVertex));
    CHECK(l.attributes[0].format == VertexFormat::Float3);   // position
    CHECK(l.attributes[0].offset == 0);
    CHECK(l.attributes[2].format == VertexFormat::Float4);   // tangent
    // Offsets strictly increase and fit within the stride.
    for (int i = 1; i < l.attribute_count; ++i)
        CHECK(l.attributes[i].offset > l.attributes[i - 1].offset);
    CHECK(l.attributes[5].offset < l.strides[0]);
}

static void test_cache_suffix() {
    std::printf("test_cache_suffix\n");
    TextureLoadOptions a;            // defaults
    TextureLoadOptions b; b.srgb = true;
    TextureLoadOptions c; c.generate_mipmaps = false;
    CHECK(a.cache_suffix() != b.cache_suffix());
    CHECK(a.cache_suffix() != c.cache_suffix());
    CHECK(a.cache_suffix() == TextureLoadOptions{}.cache_suffix());
}

static void test_manager_guard() {
    std::printf("test_manager_guard\n");
    AssetManager mgr;
    CHECK(!mgr.init(nullptr));               // null device rejected
    CHECK(mgr.last_error().size() > 0);
    CHECK(mgr.load_texture("nope.png") == nullptr);   // no device -> fails gracefully
    mgr.shutdown();                          // safe even when never initialized
}

static void test_model_loader() {
    std::printf("test_model_loader (available=%s)\n", model_loader_available() ? "yes" : "no");
    if (!model_loader_available()) {
        ModelData md; std::string err;
        CHECK(!load_model("x.obj", &md, {}, &err));   // stub fails with a message
        CHECK(!err.empty());
        return;
    }
    CHECK(!supported_model_extensions().empty());

    static const char kObj[] =
        "o tri\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n";
    ModelData md; std::string err;
    bool ok = load_model_from_memory(kObj, std::strlen(kObj), "obj", &md, {}, &err);
    CHECK(ok);
    if (ok) {
        CHECK(!md.empty());
        CHECK(md.triangle_count() == 1);
        CHECK(md.bounds.valid());
    } else {
        std::printf("  (obj load error: %s)\n", err.c_str());
    }
}

int main() {
    std::printf("=== test_assets ===\n");
    test_image_decode();
    test_expand_rgba();
    test_bad_image();
    test_dds();
    test_exr();
    test_ktx();
    test_model_data_bounds();
    test_vertex_layout();
    test_cache_suffix();
    test_manager_guard();
    test_model_loader();

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
