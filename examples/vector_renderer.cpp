// example_vector_renderer — exercises gfx::VectorRenderer across every desktop
// backend (OpenGL, Vulkan, D3D11, D3D12) with no visible window. For each backend it
// renders two frames to an offscreen target through the *same* renderer object:
//
//   1. a 3D perspective scene  — grid, RGB axes, a wire box + sphere, a cubic Bézier,
//                                and thick (pixel-width) red strokes;
//   2. a 2D overlay scene      — an orthographic, pixel-space GUI-style frame with
//                                filled/outlined rects, circles and thick lines.
//
// The geometry is read back and verified with orientation-invariant aggregate checks
// (so the same asserts hold under OpenGL's bottom-up framebuffer and the top-down
// D3D/Vulkan one), and a small ASCII thumbnail is printed so the output is visible in
// a headless terminal. This is both the renderer's demo and its cross-API smoke test.
//
// Run with no args for all backends, or pass a name: example_vector_renderer vulkan

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../math_util.hpp"
#include "../renderer/vector_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace window;
using window::math::Vec3;
using window::math::Vec4;
using window::math::Mat4;

//============================================================================
// Tiny harness
//============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* backend, const char* name, bool ok, const std::string& detail) {
    std::printf("    [%s] %-18s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}

static const int   W = 128, H = 128;
static const Vec4  kBg = { 0.05f, 0.07f, 0.14f, 1.0f };   // dark background

static bool is_bg(const uint8_t* p) {
    return std::abs(p[0] - 13) <= 22 && std::abs(p[1] - 18) <= 22 && std::abs(p[2] - 36) <= 22;
}
static bool red_dominant(const uint8_t* p)   { return p[0] > 150 && p[1] < 110 && p[2] < 110; }
static bool green_dominant(const uint8_t* p) { return p[1] > 130 && p[0] < 130 && p[2] < 130; }
static bool blue_dominant(const uint8_t* p)  { return p[2] > 130 && p[0] < 140 && p[1] < 150; }

struct Stats { int non_bg = 0, red = 0, green = 0, blue = 0; };
static Stats analyze(const std::vector<uint8_t>& px) {
    Stats s;
    for (int i = 0; i < W * H; ++i) {
        const uint8_t* p = &px[size_t(i) * 4];
        if (!is_bg(p)) ++s.non_bg;
        if (red_dominant(p))   ++s.red;
        if (green_dominant(p)) ++s.green;
        if (blue_dominant(p))  ++s.blue;
    }
    return s;
}

// Downsampled brightness thumbnail so the render is visible in the terminal.
static void ascii_preview(const std::vector<uint8_t>& px) {
    const char* ramp = " .:-=+*#%@";
    const int cols = 48, rows = 24;
    for (int ry = 0; ry < rows; ++ry) {
        std::string line = "      ";
        for (int rx = 0; rx < cols; ++rx) {
            const int x = rx * W / cols, y = ry * H / rows;
            const uint8_t* p = &px[(size_t(y) * W + x) * 4];
            const int lum = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
            line += ramp[lum * 9 / 255];
        }
        std::printf("%s\n", line.c_str());
    }
}

//============================================================================
// Scenes (drawn through one VectorRenderer)
//============================================================================
static void scene_3d(gfx::VectorRenderer& vr, GraphicCommander* cmd) {
    const float aspect = float(W) / float(H);
    Mat4 view = Mat4::look_at({ 4.5f, 3.2f, 5.0f }, { 0.0f, 0.6f, 0.0f }, Vec3::up());
    Mat4 proj = Mat4::perspective(50.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
    Mat4 vp = proj * view;

    vr.begin(vp.data(), W, H);

    // Ground grid + world axes (thin).
    vr.set_line_width(1.0f);
    vr.grid({ 0, 0, 0 }, Vec3::unit_x(), Vec3::unit_z(), 8.0f, 16, { 0.35f, 0.38f, 0.45f, 1 });
    vr.axes({ 0, 0, 0 }, 2.0f);                                  // X=red, Y=green, Z=blue

    // A wire box, a sphere and a cubic Bézier (thin).
    vr.aabb({ -1.2f, 0.1f, -1.2f }, { 1.2f, 2.5f, 1.2f }, { 1, 1, 0, 1 });   // yellow
    vr.sphere({ 2.6f, 1.0f, -1.0f }, 1.0f, { 0, 1, 1, 1 });                  // cyan
    vr.bezier({ -3, 0.1f, 2.5f }, { -1, 3.5f, 2.5f },
              { 1, 3.5f, -2.5f }, { 3, 0.1f, -2.5f }, { 1, 1, 1, 1 });       // white

    // Thick (pixel-width) red strokes — the screen-space-expanded path.
    vr.set_line_width(5.0f);
    vr.ring({ 0, 0.04f, 0 }, Vec3::unit_y(), 3.4f, { 1.0f, 0.25f, 0.25f, 1 }, 64);
    vr.line({ -3.2f, 0.1f, -3.2f }, { 3.2f, 4.0f, 3.2f }, { 1.0f, 0.2f, 0.2f, 1 });
    vr.set_line_width(1.0f);

    vr.end(cmd);
}

static void scene_2d(gfx::VectorRenderer& vr, GraphicCommander* cmd) {
    // Pixel-space orthographic matrix (top-left origin), as a GUI/overlay would use.
    // near=-3,far=1 maps the z=0 draw plane to NDC-Z 0.5 — safely inside the depth
    // range on every backend. (z=0 with a symmetric -1..1 ortho lands on NDC-Z 0, the
    // near-plane boundary, where the [0,1]-depth backends (Vulkan/D3D) clip thin lines.)
    Mat4 proj = Mat4::ortho(0.0f, float(W), float(H), 0.0f, -3.0f, 1.0f);
    vr.begin(proj.data(), W, H);

    vr.fill_rect(12, 12, 60, 36, { 0.12f, 0.30f, 0.80f, 1 });   // blue panel
    vr.set_line_width(3.0f);
    vr.rect(12, 12, 60, 36, { 1, 1, 1, 1 });                    // thick white border
    vr.set_line_width(6.0f);
    vr.line2d(18, 110, 116, 70, { 1.0f, 0.2f, 0.2f, 1 });       // thick red stroke
    vr.set_line_width(1.0f);
    vr.circle2d(92, 44, 26, { 0, 1, 0, 1 }, 48);                // green ring
    vr.fill_circle(60, 100, 16, { 1, 1, 0, 1 }, 32);            // yellow disc

    vr.end(cmd);
}

//============================================================================
// Per-backend driver
//============================================================================
struct Ctx {
    Graphics*         gfx = nullptr;
    GraphicDevice*    dev = nullptr;
    GraphicCommander* cmd = nullptr;
};

static std::vector<uint8_t> draw_and_read(Ctx& c, gfx::VectorRenderer& vr,
                                          void (*scene)(gfx::VectorRenderer&, GraphicCommander*),
                                          RenderTargetHandle rt) {
    c.cmd->begin();
    c.cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = float(W); vp.height = float(H);
    c.cmd->set_viewport(vp);
    ScissorRect full{ 0, 0, W, H }; c.cmd->set_scissor(full);
    c.cmd->clear_color(ClearColor(kBg.x, kBg.y, kBg.z, kBg.w));
    scene(vr, c.cmd);
    c.cmd->end();
    submit_commander(c.gfx, c.cmd);
    c.dev->wait_idle();

    std::vector<uint8_t> px(size_t(W) * H * 4, 0);
    TextureHandle tex = c.dev->render_target_texture(rt);
    TextureRegion r; r.x = 0; r.y = 0; r.width = W; r.height = H;
    c.dev->read_texture(tex, r, px.data());
    return px;
}

static void run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);

    Config config;
    config.backend = backend;
    config.windows[0].title = "vector_renderer";
    config.windows[0].width = W; config.windows[0].height = H;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) {
        std::printf("    [SKIP] window/context create failed (%s)\n", result_to_string(wr));
        ++g_skip; return;
    }
    Ctx c; c.gfx = windows[0]->graphics();
    Result dr;
    c.dev = create_device(c.gfx, &dr);
    if (!c.dev) { std::printf("    [SKIP] no GraphicDevice (%s)\n", result_to_string(dr)); ++g_skip; windows[0]->destroy(); return; }
    c.cmd = create_commander(c.gfx, c.dev, &dr);
    if (!c.cmd) { std::printf("    [SKIP] no GraphicCommander (%s)\n", result_to_string(dr)); ++g_skip; destroy_device(c.dev); windows[0]->destroy(); return; }

    gfx::VectorRendererDesc vd;
    vd.depth_test = false; vd.depth_write = false;   // overlay-style; draw order = submit order
    vd.color_format = TextureFormat::RGBA8_UNORM;
    gfx::VectorRenderer vr;
    if (!vr.init(c.dev, vd)) {
        std::printf("    [SKIP] VectorRenderer.init failed (no shaders for this backend)\n");
        ++g_skip; destroy_commander(c.cmd); destroy_device(c.dev); windows[0]->destroy(); return;
    }

    RenderTargetDesc rd; rd.width = W; rd.height = H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = c.dev->create_render_target(rd);

    // --- 3D scene ---
    {
        auto px = draw_and_read(c, vr, scene_3d, rt);
        Stats s = analyze(px);
        char d[160];
        std::snprintf(d, sizeof d, "non-bg=%d red(thick)=%d green=%d blue=%d", s.non_bg, s.red, s.green, s.blue);
        check(name, "3d.rendered",   s.non_bg > 600, d);
        check(name, "3d.thick_red",  s.red   > 250,  "thick-line expansion produced a wide red ring/stroke");
        check(name, "3d.axes_rgb",   s.green > 0 && s.blue > 0, "green + blue axes present");
        ascii_preview(px);
    }
    // --- 2D overlay scene ---
    {
        auto px = draw_and_read(c, vr, scene_2d, rt);
        Stats s = analyze(px);
        char d[160];
        std::snprintf(d, sizeof d, "non-bg=%d red(thick)=%d green=%d blue=%d", s.non_bg, s.red, s.green, s.blue);
        check(name, "2d.rendered",  s.non_bg > 600, d);
        check(name, "2d.thick_red", s.red   > 200,  "thick 2D stroke present");
        check(name, "2d.shapes",    s.green > 0 && s.blue > 0, "green ring + blue panel present");
        ascii_preview(px);
    }

    vr.shutdown();
    c.dev->destroy_render_target(rt);
    destroy_commander(c.cmd);
    destroy_device(c.dev);
    windows[0]->destroy();
}

int main(int argc, char** argv) {
    std::printf("VectorRenderer cross-API example (%dx%d offscreen)\n", W, H);
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
            for (auto& ch : a) ch = char(tolower(ch));
            for (auto& ch : b) ch = char(tolower(ch));
            if (b.find(a) == std::string::npos) continue;
        }
        run_backend(e.b, e.n);
    }
    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail;
}
