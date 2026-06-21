// example_light - exercises renderer/light (gfx::Light + gfx::LightBuffer): builds every
// light type, packs them into the GPU layout, uploads through the managed constant buffers,
// reads them back, and checks the bytes round-trip on every desktop backend.
//
// The light subsystem only MANAGES BUFFERS - there is no shading here - so this test mirrors
// example_buffer: a hidden window -> GraphicDevice per backend, then create -> update -> read.
//
//   * pure-CPU pack() checks (no device): direction normalised, directional range = 0,
//     spot cone scale/offset math, area extents, type ids,
//   * per backend: add one of each light type, update(), read back the lights + globals
//     buffers and compare to LightBuffer::pack(), check live_count / ambient,
//   * disabled lights are compacted out, capacity is enforced (add -> -1 when full),
//     remove() swap-removes, clear() empties the live range.
//
// Run with no args for all backends, or pass a name: example_light d3d11

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/light/light.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

//============================================================================
// Tiny harness
//============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("    [%s] %-28s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-28s %s\n", name, detail.c_str());
    ++g_skip;
}
static bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
static bool gpu_light_eq(const GpuLight& a, const GpuLight& b) {
    return std::memcmp(&a, &b, sizeof(GpuLight)) == 0;
}

//============================================================================
// CPU-only: packing math (no device needed)
//============================================================================
static void test_pack_cpu() {
    std::printf("\n=== CPU pack() ===\n");

    // Directional: range forced to 0, direction normalised, type id 0.
    {
        GpuLight g = LightBuffer::pack(Light::directional({0, -2, 0}, {1, 0.9f, 0.8f}, 3.0f));
        check("dir.type", feq(g.type, 0.0f));
        check("dir.range_zero", feq(g.range, 0.0f));
        check("dir.normalised", feq(g.direction[0], 0) && feq(g.direction[1], -1) && feq(g.direction[2], 0),
              "dir=(" + std::to_string(g.direction[0]) + "," + std::to_string(g.direction[1]) + "," +
              std::to_string(g.direction[2]) + ")");
        check("dir.intensity", feq(g.intensity, 3.0f));
        check("dir.color", feq(g.color[0], 1) && feq(g.color[1], 0.9f) && feq(g.color[2], 0.8f));
    }

    // Point: position + range kept, type id 1, no spot term (scale 0, offset 1).
    {
        GpuLight g = LightBuffer::pack(Light::point({5, 6, 7}, {1, 1, 1}, 2.0f, 12.0f));
        check("point.type", feq(g.type, 1.0f));
        check("point.position", feq(g.position[0], 5) && feq(g.position[1], 6) && feq(g.position[2], 7));
        check("point.range", feq(g.range, 12.0f));
        check("point.no_spot", feq(g.spot_scale, 0.0f) && feq(g.spot_offset, 1.0f));
    }

    // Spot: cone angles bake into scale/offset such that
    //   saturate(cos*scale+offset) == 1 at the inner edge and 0 at the outer edge.
    {
        const float inner = 20.0f, outer = 30.0f;
        GpuLight g = LightBuffer::pack(Light::spot({0, 10, 0}, {0, -1, 0}, {1, 1, 1}, 5.0f, 25.0f, inner, outer));
        check("spot.type", feq(g.type, 2.0f));
        float ci = std::cos(inner * 3.14159265f / 180.0f);
        float co = std::cos(outer * 3.14159265f / 180.0f);
        float at_inner = ci * g.spot_scale + g.spot_offset;   // expect ~1
        float at_outer = co * g.spot_scale + g.spot_offset;   // expect ~0
        check("spot.cone_edges", feq(at_inner, 1.0f) && feq(at_outer, 0.0f),
              "inner=" + std::to_string(at_inner) + " outer=" + std::to_string(at_outer));
    }

    // Area: width/height kept, type id 3.
    {
        GpuLight g = LightBuffer::pack(Light::area({1, 2, 3}, {0, 0, -1}, 4.0f, 2.5f, {1, 1, 1}, 1.0f, 8.0f));
        check("area.type", feq(g.type, 3.0f));
        check("area.extents", feq(g.area_width, 4.0f) && feq(g.area_height, 2.5f));
        check("area.range", feq(g.range, 8.0f));
    }

    // Degenerate direction -> safe fallback (0,-1,0), never NaN.
    {
        GpuLight g = LightBuffer::pack(Light::directional({0, 0, 0}, {1, 1, 1}));
        check("degenerate.dir_fallback", feq(g.direction[1], -1.0f) && !std::isnan(g.direction[0]));
    }
}

//============================================================================
// GPU round-trip (per backend)
//============================================================================
static void test_round_trip(GraphicDevice* dev) {
    LightBuffer lb;
    bool oki = lb.init(dev, 64);
    check("init", oki && lb.valid() && lb.capacity() == 64);
    if (!oki) return;

    lb.set_ambient({0.1f, 0.2f, 0.3f});

    Light lights[4] = {
        Light::directional({0, -1, -0.3f}, {1.0f, 0.95f, 0.9f}, 3.0f),
        Light::point({2, 3, 4}, {1, 0, 0}, 5.0f, 15.0f),
        Light::spot({0, 8, 0}, {0, -1, 0}, {0, 1, 0}, 4.0f, 20.0f, 18.0f, 28.0f),
        Light::area({-3, 2, 1}, {1, 0, 0}, 2.0f, 1.0f, {0, 0, 1}, 2.0f, 10.0f),
    };
    for (auto& l : lights) lb.add(l);
    check("add_all", lb.count() == 4);

    lb.update();
    check("live_count", lb.live_count() == 4, "live=" + std::to_string(lb.live_count()));

    // Read back the live light range and compare to a fresh pack().
    GpuLight back[4] = {};
    dev->read_buffer(lb.lights_buffer().handle(), back, sizeof(back), 0);
    bool all_match = true;
    for (int i = 0; i < 4; ++i) all_match = all_match && gpu_light_eq(back[i], LightBuffer::pack(lights[i]));
    check("lights.readback", all_match);

    // Globals: ambient + live count.
    GpuLightGlobals g{};
    dev->read_buffer(lb.globals_buffer().handle(), &g, sizeof(g), 0);
    check("globals.count", g.light_count == 4);
    check("globals.ambient", feq(g.ambient[0], 0.1f) && feq(g.ambient[1], 0.2f) && feq(g.ambient[2], 0.3f));

    // Disable one light -> it is compacted out of the GPU array.
    lb.get(1)->enabled = false;
    lb.update();
    check("disabled.compacted", lb.live_count() == 3, "live=" + std::to_string(lb.live_count()));
    {
        GpuLightGlobals g2{};
        dev->read_buffer(lb.globals_buffer().handle(), &g2, sizeof(g2), 0);
        check("disabled.globals_count", g2.light_count == 3);
        // The compacted array now holds: [dir, spot, area] (point removed from the middle).
        GpuLight back3[3] = {};
        dev->read_buffer(lb.lights_buffer().handle(), back3, sizeof(back3), 0);
        bool ok = gpu_light_eq(back3[0], LightBuffer::pack(lights[0])) &&
                  gpu_light_eq(back3[1], LightBuffer::pack(lights[2])) &&
                  gpu_light_eq(back3[2], LightBuffer::pack(lights[3]));
        check("disabled.array_compacted", ok);
    }

    // remove() swap-removes (last moves into the hole); clear() empties.
    lb.get(1)->enabled = true;          // re-enable so count() == 4 again
    lb.remove(0);                        // light[3] (area) swaps into slot 0
    check("remove.count", lb.count() == 3);
    lb.update();
    check("remove.live", lb.live_count() == 3);

    lb.clear();
    lb.update();
    check("clear.empty", lb.count() == 0 && lb.live_count() == 0);

    // Capacity is enforced.
    LightBuffer small;
    small.init(dev, 2);
    int i0 = small.add(Light::point({0, 0, 0}, {1, 1, 1}));
    int i1 = small.add(Light::point({1, 0, 0}, {1, 1, 1}));
    int i2 = small.add(Light::point({2, 0, 0}, {1, 1, 1}));  // over capacity
    check("capacity.enforced", i0 == 0 && i1 == 1 && i2 == -1);
}

//============================================================================
// Per-backend driver
//============================================================================
static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_light";
    config.windows[0].width = 64; config.windows[0].height = 64;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) {
        skip("backend", std::string("window/context create failed: ") + result_to_string(wr));
        return false;
    }
    Graphics* gfx = windows[0]->graphics();

    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) {
        skip("backend", std::string("no RHI GraphicDevice: ") + result_to_string(dr));
        windows[0]->destroy();
        return false;
    }

    test_round_trip(dev);

    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("renderer/light buffer smoke test\n");

    test_pack_cpu();   // device-independent

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
        if (!is_backend_supported(e.b)) {
            std::printf("\n=== Backend: %s ===\n    [SKIP] not supported\n", e.n);
            ++g_skip; continue;
        }
        run_backend(e.b, e.n);
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
