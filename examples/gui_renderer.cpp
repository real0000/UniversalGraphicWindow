// example_gui_renderer - offscreen pixel test for gui::GpuGuiRenderer.
//
// Builds a tiny WidgetRenderInfo (a solid rect on a cleared background), renders it to an
// offscreen target through GpuGuiRenderer, and reads back pixels to confirm the rect lands
// in the right place with the right colour. This is the verification harness for migrating
// the GUI shaders to the unified HLSL path (the renderer supports OpenGL + Vulkan; other
// backends auto-skip when init() declines).
//
// Run with no args for all backends, or pass a name: example_gui_renderer vulkan

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/gui_renderer.hpp"
#include "../gui/gui.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;

static int g_pass = 0, g_fail = 0, g_skip = 0;
static void check(const char* n, bool ok, const std::string& d = "") {
    std::printf("    [%s] %-22s %s\n", ok ? "PASS" : "FAIL", n, d.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* n, const std::string& d) { std::printf("    [SKIP] %-22s %s\n", n, d.c_str()); ++g_skip; }

static const int RT_W = 32, RT_H = 32;

// A 1x1 single-layer R8 array texture, just so the atlas descriptor is bindable (solid rects
// never sample it, but Vulkan still wants the set bound).
static TextureHandle make_atlas(GraphicDevice* dev) {
    uint8_t px = 255;
    TextureDesc td;
    td.width = 1; td.height = 1; td.array_layers = 1; td.array_texture = true;
    td.format = TextureFormat::R8_UNORM; td.usage = TEXTURE_USAGE_SAMPLED;
    td.initial_data = &px;
    return dev->create_texture(td);
}

static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_gui_renderer";
    config.windows[0].width = 64; config.windows[0].height = 64;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) { skip("backend", std::string("window create: ") + result_to_string(wr)); return false; }
    Graphics* gfx = windows[0]->graphics();

    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { skip("backend", std::string("no device: ") + result_to_string(dr)); windows[0]->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { skip("backend", "no commander"); destroy_device(dev); windows[0]->destroy(); return false; }

    gui::GpuGuiRenderer renderer;
    if (!renderer.init(dev)) {
        skip("init", "GpuGuiRenderer unsupported on this backend");
        destroy_commander(cmd); destroy_device(dev); windows[0]->destroy(); return false;
    }
    check("init", true);

    TextureHandle atlas = make_atlas(dev);

    gui::WidgetRenderInfo info;
    math::Box clip = math::make_box(0.0f, 0.0f, float(RT_W), float(RT_H));
    // Left: a solid green rect (no atlas sample). Right: a "glyph" quad (atlas_layer 0) tinted
    // red -- it samples the R8=255 atlas as an alpha mask, exercising the texture+sampler path.
    info.push_rect(4.0f, 8.0f, 8.0f, 16.0f, math::Vec4(0.0f, 1.0f, 0.0f, 1.0f), 0, clip);
    gui::WidgetRenderInfo::TextureCmd glyph{};
    glyph.atlas_layer = 0;
    glyph.dest = math::make_box(20.0f, 8.0f, 8.0f, 16.0f);
    glyph.uv   = math::make_box(0.0f, 0.0f, 1.0f, 1.0f);
    glyph.tint = math::Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    glyph.depth = 1;
    glyph.clip = clip;
    info.textures.push_back(glyph);

    // Orthographic, top-left origin (the GUI's pixel-space convention; column-major).
    float proj[16] = {
        2.0f / RT_W, 0.0f,          0.0f, 0.0f,
        0.0f,       -2.0f / RT_H,   0.0f, 0.0f,
        0.0f,        0.0f,         -1.0f, 0.0f,
       -1.0f,        1.0f,          0.0f, 1.0f
    };

    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = dev->create_render_target(rd);

    cmd->begin();
    cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; cmd->set_viewport(vp);
    cmd->clear_color(ClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    renderer.render(cmd, info, atlas, proj, RT_W, RT_H, 1.0f);
    cmd->end();
    submit_commander(gfx, cmd);

    std::vector<uint8_t> px(size_t(RT_W) * RT_H * 4, 0);
    TextureHandle tex = dev->render_target_texture(rt);
    TextureRegion r; r.x = 0; r.y = 0; r.width = RT_W; r.height = RT_H; r.mip = 0; r.layer = 0;
    dev->read_texture(tex, r, px.data());
    auto at = [&](int x, int y) { return &px[(size_t(y) * RT_W + x) * 4]; };

    const uint8_t* c = at(8, 16);    // inside the solid rect
    bool green = c[0] < 40 && c[1] > 200 && c[2] < 40;
    char d1[64]; std::snprintf(d1, sizeof d1, "solid=(%d,%d,%d,%d)", c[0], c[1], c[2], c[3]);
    check("rect.solid_green", green, d1);

    const uint8_t* gp = at(24, 16);  // inside the glyph quad (atlas-sampled)
    bool red = gp[0] > 200 && gp[1] < 40 && gp[2] < 40;
    char dg[64]; std::snprintf(dg, sizeof dg, "glyph=(%d,%d,%d,%d)", gp[0], gp[1], gp[2], gp[3]);
    check("glyph.atlas_red", red, dg);

    const uint8_t* o = at(2, 2);     // background
    bool black = o[0] < 30 && o[1] < 30 && o[2] < 30;
    char d2[64]; std::snprintf(d2, sizeof d2, "bg=(%d,%d,%d,%d)", o[0], o[1], o[2], o[3]);
    check("rect.outside_clear", black, d2);

    dev->destroy_render_target(rt);
    dev->destroy_texture(atlas);
    renderer.shutdown();
    destroy_commander(cmd); destroy_device(dev); windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("gui_renderer offscreen pixel test\n");
    struct { Backend b; const char* n; } backends[] = {
        { Backend::OpenGL, "OpenGL" },
        { Backend::Vulkan, "Vulkan" },
        { Backend::D3D11,  "Direct3D 11" },
        { Backend::D3D12,  "Direct3D 12" },
    };
    const char* only = (argc > 1) ? argv[1] : nullptr;
    for (auto& e : backends) {
        if (only) {
            std::string a = only, b = e.n;
            for (auto& ch : a) ch = (char)tolower(ch);
            for (auto& ch : b) ch = (char)tolower(ch);
            if (b.find(a) == std::string::npos) continue;
        }
        if (!is_backend_supported(e.b)) { std::printf("\n=== Backend: %s ===\n    [SKIP] not supported\n", e.n); ++g_skip; continue; }
        run_backend(e.b, e.n);
    }
    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
