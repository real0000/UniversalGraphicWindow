// example_slot_binding - verifies slot-based resource binding works on every backend.
//
// Draws a fullscreen triangle whose colour comes from a uniform buffer bound by SLOT
// (bind_uniform_buffer), using a pipeline created WITHOUT an explicit pipeline layout. On
// Vulkan/D3D12 the backend must auto-build the descriptor set layout / root signature from
// shader reflection for this to work (the point of the test). Reads back the centre pixel.
//
// Run with no args for all backends, or pass a name: example_slot_binding vulkan

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/shader_compiler/shader_compiler.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

static int g_pass = 0, g_fail = 0, g_skip = 0;
static void check(const char* n, bool ok, const std::string& d = "") {
    std::printf("    [%s] %-22s %s\n", ok ? "PASS" : "FAIL", n, d.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* n, const std::string& d) { std::printf("    [SKIP] %-22s %s\n", n, d.c_str()); ++g_skip; }

// Colour comes from cbuffer b0 (slot 0); vertex pos from TEXCOORD0.
static const char* kHLSL = R"(
cbuffer Col : register(b0) { float4 uColor; };
struct VSIn  { float2 pos : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; };
VSOut vs_main(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); return o; }
float4 ps_main(VSOut i) : SV_Target { return uColor; }
)";

static const int RT_W = 32, RT_H = 32;

static void test_backend(GraphicDevice* dev, GraphicCommander* cmd, Graphics* gfx) {
    const size_t len = std::strlen(kHLSL);
    ShaderCompileResult vlog, plog;
    ShaderHandle vs = ShaderCompiler::compile_and_create_cached(dev, kHLSL, len, ShaderStage::Vertex,   "vs_main", {}, &vlog);
    ShaderHandle ps = ShaderCompiler::compile_and_create_cached(dev, kHLSL, len, ShaderStage::Fragment, "ps_main", {}, &plog);
    check("compile vs/ps", vs.valid() && ps.valid(), vs.valid() ? "" : vlog.log + plog.log);
    if (!vs.valid() || !ps.valid()) return;

    PipelineDesc pd;                                  // no explicit layout -> backend reflects it
    pd.vertex_shader = vs; pd.fragment_shader = ps;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.rasterizer = RasterizerState::no_cull();
    VertexLayout vl; vl.attributes[0] = { 0, VertexFormat::Float2, 0, 0 }; vl.attribute_count = 1;
    vl.strides[0] = 8; vl.buffer_count = 1; pd.vertex_layout = vl;
    PipelineHandle pipe = dev->create_pipeline(pd);
    check("create_pipeline", pipe.valid());

    const float verts[] = { -1.f, -1.f,  3.f, -1.f,  -1.f, 3.f };
    BufferDesc vbd; vbd.size = sizeof verts; vbd.type = BufferType::Vertex; vbd.initial_data = verts; vbd.stride = 8;
    BufferHandle vb = dev->create_buffer(vbd);

    const float color[4] = { 0.20f, 0.40f, 0.80f, 1.0f };   // distinct blue-ish
    BufferDesc ubd; ubd.size = sizeof color; ubd.type = BufferType::Uniform; ubd.usage = ResourceUsage::Dynamic; ubd.initial_data = color;
    BufferHandle ubo = dev->create_buffer(ubd);

    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = dev->create_render_target(rd);

    if (pipe.valid() && vb.valid() && ubo.valid() && rt.valid()) {
        cmd->begin();
        cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
        Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; vp.min_depth = 0; vp.max_depth = 1; cmd->set_viewport(vp);
        ScissorRect sc; sc.x = 0; sc.y = 0; sc.width = RT_W; sc.height = RT_H; cmd->set_scissor(sc);
        cmd->clear_color(ClearColor(0.f, 0.f, 0.f, 1.f));
        cmd->set_pipeline(pipe);
        cmd->bind_uniform_buffer(0, ubo, 0, sizeof color);   // SLOT bind (the thing under test)
        cmd->bind_vertex_buffer(0, vb, 0);
        cmd->draw(3);
        cmd->end();
        submit_commander(gfx, cmd);

        std::vector<uint8_t> px(size_t(RT_W) * RT_H * 4, 0);
        TextureHandle tex = dev->render_target_texture(rt);
        TextureRegion r; r.x = 0; r.y = 0; r.width = RT_W; r.height = RT_H; r.mip = 0; r.layer = 0;
        dev->read_texture(tex, r, px.data());
        const uint8_t* c = &px[(size_t(RT_H/2) * RT_W + RT_W/2) * 4];
        bool ok = c[0] > 35 && c[0] < 70 && c[1] > 85 && c[1] < 120 && c[2] > 180 && c[2] < 225;
        char d[64]; std::snprintf(d, sizeof d, "centre=(%d,%d,%d) expect ~(51,102,204)", c[0], c[1], c[2]);
        check("ubo slot color", ok, d);
    }

    if (rt.valid()) dev->destroy_render_target(rt);
    if (vb.valid()) dev->destroy_buffer(vb);
    if (ubo.valid()) dev->destroy_buffer(ubo);
    if (pipe.valid()) dev->destroy_pipeline(pipe);
    dev->destroy_shader(vs); dev->destroy_shader(ps);
}

static void run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);
    Config config; config.backend = backend;
    config.windows[0].title = "example_slot_binding"; config.windows[0].width = 64; config.windows[0].height = 64; config.windows[0].visible = false;
    Result wr; auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) { skip("backend", std::string("window: ") + result_to_string(wr)); return; }
    Graphics* gfx = windows[0]->graphics();
    Result dr; GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { skip("backend", "no device"); windows[0]->destroy(); return; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { skip("backend", "no commander"); destroy_device(dev); windows[0]->destroy(); return; }
    test_backend(dev, cmd, gfx);
    destroy_commander(cmd); destroy_device(dev); windows[0]->destroy();
}

int main(int argc, char** argv) {
    std::printf("slot-based resource binding test (uniform buffer by slot, no explicit layout)\n");
    if (!ShaderCompiler::available()) { std::printf("shader compiler not built\n"); return 0; }
    ShaderCompiler::initialize();
    struct { Backend b; const char* n; } bk[] = {
        { Backend::OpenGL, "OpenGL" }, { Backend::Vulkan, "Vulkan" },
        { Backend::D3D11, "Direct3D 11" }, { Backend::D3D12, "Direct3D 12" },
    };
    auto matches = [](const std::string& a, Backend b) {
        switch (b) {
            case Backend::OpenGL: return a.find("gl") != std::string::npos;
            case Backend::Vulkan: return a.find("vk") != std::string::npos || a.find("vulkan") != std::string::npos;
            case Backend::D3D11:  return a.find("11") != std::string::npos;
            case Backend::D3D12:  return a.find("12") != std::string::npos;
            default: return false;
        }
    };
    std::string a; if (argc > 1) { a = argv[1]; for (auto& c : a) c = (char)tolower(c); }
    for (auto& e : bk) {
        if (!a.empty() && !matches(a, e.b)) continue;
        if (!is_backend_supported(e.b)) { std::printf("\n=== Backend: %s ===\n    [SKIP] not supported\n", e.n); ++g_skip; continue; }
        run_backend(e.b, e.n);
    }
    ShaderCompiler::shutdown();
    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
