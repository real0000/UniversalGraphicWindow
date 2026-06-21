/*
 * assets.cpp - Asset loading & management demo (renderer/assets/).
 *
 * Shows the asset system end to end:
 *   1. Decode images (any stb_image format) and 3D models (any Assimp format) into
 *      backend-neutral CPU data (image.hpp / model.hpp / mesh.hpp).
 *   2. Upload + manage them on the GPU through AssetManager (caching, mip generation,
 *      material-texture resolution, one-call teardown).
 *
 * Run headless: it creates an invisible window/device. Optionally pass files:
 *     example_assets [image-file] [model-file]
 * With no arguments it synthesizes an image and a quad mesh so it always has something
 * to upload. If no GPU device is available (e.g. CI), it falls back to a CPU-only pass.
 */

#include "window.hpp"
#include "renderer/assets/asset_manager.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace window;
using namespace window::gfx;

// A unit quad as ModelData, so create_model() has geometry without a model file.
static ModelData make_quad() {
    ModelData m;
    auto v = [](float x, float y, float u, float vv) {
        MeshVertex mv;
        mv.position = math::Vec3(x, y, 0.0f);
        mv.normal   = math::Vec3(0, 0, 1);
        mv.uv0      = math::Vec2(u, vv);
        return mv;
    };
    m.vertices = { v(-0.5f, -0.5f, 0, 0), v(0.5f, -0.5f, 1, 0),
                   v(0.5f, 0.5f, 1, 1),   v(-0.5f, 0.5f, 0, 1) };
    m.indices  = { 0, 1, 2, 0, 2, 3 };
    MeshMaterial mat; mat.name = "quad"; mat.base_color = math::Vec4(0.8f, 0.85f, 0.9f, 1.0f);
    m.materials.push_back(mat);
    SubMesh sm; sm.name = "quad"; sm.index_offset = 0; sm.index_count = 6; sm.material = 0;
    m.submeshes.push_back(sm);
    m.compute_bounds();
    return m;
}

static void print_model(const char* tag, const GpuModel& gm) {
    std::printf("  %s: %u verts, %u indices, %zu submeshes, %zu materials\n",
                tag, gm.vertex_count, gm.index_count, gm.submeshes.size(), gm.materials.size());
    std::printf("    bounds: (%.2f %.2f %.2f) .. (%.2f %.2f %.2f)\n",
                gm.bounds.min.x, gm.bounds.min.y, gm.bounds.min.z,
                gm.bounds.max.x, gm.bounds.max.y, gm.bounds.max.z);
    for (size_t i = 0; i < gm.materials.size(); ++i) {
        const GpuMaterial& mm = gm.materials[i];
        std::printf("    material[%zu] '%s'  base_color_tex=%s normal_tex=%s mr_tex=%s\n",
                    i, mm.name.c_str(),
                    mm.base_color.valid() ? "yes" : "no",
                    mm.normal.valid() ? "yes" : "no",
                    mm.metallic_roughness.valid() ? "yes" : "no");
    }
}

int main(int argc, char** argv) {
    const char* image_file = argc > 1 ? argv[1] : nullptr;
    const char* model_file = argc > 2 ? argv[2] : nullptr;

    std::printf("=== Asset system demo ===\n");
    std::printf("image formats: stb (png/jpg/bmp/tga/gif/hdr/psd/...)%s%s%s%s\n",
                Image::exr_supported()  ? " + exr" : "",
                Image::dds_supported()  ? " + dds(BCn)" : "",
                Image::etc2_supported() ? " + ktx/etc2" : "",
                Image::astc_supported() ? " + ktx/astc" : "");
    std::printf("model loader: %s\n", model_loader_available() ? "Assimp (available)" : "disabled");
    if (model_loader_available())
        std::printf("supported model formats: %s\n", supported_model_extensions().c_str());

    // --- CPU decode (works without a GPU) ------------------------------------
    if (image_file) {
        int w = 0, h = 0, c = 0;
        if (Image::info(image_file, &w, &h, &c))
            std::printf("image '%s': %dx%d, %d channels, %s\n",
                        image_file, w, h, c, Image::is_hdr_file(image_file) ? "HDR" : "LDR");
        else
            std::printf("image '%s': probe failed: %s\n", image_file, Image::last_error());
    }
    if (model_file) {
        ModelData md; std::string err;
        if (load_model(model_file, &md, {}, &err))
            std::printf("model '%s': %zu verts, %u tris, %zu materials, %zu textures (%zu embedded)\n",
                        model_file, md.vertices.size(), md.triangle_count(),
                        md.materials.size(), md.textures.size(), md.embedded.size());
        else
            std::printf("model '%s': load failed: %s\n", model_file, err.c_str());
    }

    // --- GPU upload + management ----------------------------------------------
    Config config;
    config.windows[0].title   = "assets";
    config.windows[0].width   = 64;
    config.windows[0].height  = 64;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) {
        std::printf("\n[no window/context: %s] CPU-only demo done.\n", result_to_string(wr));
        return 0;
    }

    Result dr;
    GraphicDevice* dev = create_device(windows[0]->graphics(), &dr);
    if (!dev) {
        std::printf("\n[no GPU device: %s] CPU-only demo done.\n", result_to_string(dr));
        windows[0]->destroy();
        return 0;
    }

    AssetManager assets;
    if (!assets.init(dev)) {
        std::printf("AssetManager init failed: %s\n", assets.last_error().c_str());
        destroy_device(dev); windows[0]->destroy();
        return 1;
    }
    std::printf("\nGPU device ready (%s). Uploading assets...\n", backend_to_string(dev->get_backend()));

    // Synthetic checkerboard -> GPU texture (with mip chain).
    {
        const int N = 256, C = 32;
        std::vector<uint8_t> px(size_t(N) * N * 4);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const bool on = ((x / C) ^ (y / C)) & 1;
                uint8_t* p = &px[(size_t(y) * N + x) * 4];
                p[0] = on ? 230 : 40; p[1] = on ? 200 : 40; p[2] = on ? 60 : 40; p[3] = 255;
            }
        Image img; img.width = N; img.height = N; img.channels = 4;
        img.type = ImagePixelType::U8; img.pixels = px.data();   // borrowed view
        TextureLoadOptions opt; opt.srgb = true;
        GpuTexture t = assets.create_texture(img, opt);
        img.pixels = nullptr;   // do not let ~Image free the borrowed stack buffer
        std::printf("  checkerboard texture: %dx%d, %d mips, valid=%s\n",
                    t.width, t.height, t.mip_levels, t.valid() ? "yes" : "no");
    }

    // Fallback textures + sampler (cached, owned by the manager).
    std::printf("  fallbacks: white=%s normal=%s sampler=%s\n",
                assets.white_texture().valid() ? "ok" : "no",
                assets.default_normal().valid() ? "ok" : "no",
                assets.default_sampler().valid() ? "ok" : "no");

    // Synthetic quad -> GPU model.
    GpuModel quad = assets.create_model(make_quad());
    print_model("quad model", quad);

    // Optional: load real files from the command line, cached.
    if (image_file) {
        const GpuTexture* t = assets.load_texture(image_file, {});
        std::printf("  load_texture('%s'): %s\n", image_file,
                    t ? "ok" : assets.last_error().c_str());
        // Cached: a second load returns the same handle without re-decoding.
        const GpuTexture* t2 = assets.load_texture(image_file, {});
        std::printf("    cached (same handle): %s\n",
                    (t && t2 && t->texture == t2->texture) ? "yes" : "no");
    }
    if (model_file) {
        const GpuModel* gm = assets.load_model(model_file, {}, {});
        if (gm) print_model("file model", *gm);
        else    std::printf("  load_model('%s'): %s\n", model_file, assets.last_error().c_str());
    }

    std::printf("\ncached: %zu textures, %zu models. Shutting down.\n",
                assets.cached_texture_count(), assets.cached_model_count());

    assets.shutdown();      // destroys every GPU resource the manager created
    destroy_device(dev);
    windows[0]->destroy();
    std::printf("done.\n");
    return 0;
}
