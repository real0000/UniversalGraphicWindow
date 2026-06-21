// example_shader_compiler - exercises the built-in renderer shader compiler
// (gfx::ShaderCompiler): compile ONE HLSL source into every backend's blob, then prove the
// generated blobs actually run by drawing with them on each available backend.
//
//   * cross-compile (no device): HLSL -> SPIR-V / GLSL / DXBC / MSL; validate each blob's
//     signature (SPIR-V magic, "DXBC" FourCC, "#version", metal_stdlib),
//   * per backend (hidden window -> GraphicDevice): compile_and_create() a VS+PS, then build
//     a pipeline, draw a fullscreen triangle into an offscreen RT and read back the centre
//     pixel -- end-to-end proof the runtime-compiled shader executes on the GPU.
//
// Run with no args for all backends, or pass a name: example_shader_compiler vulkan

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

//============================================================================
// Tiny harness
//============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("    [%s] %-30s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-30s %s\n", name, detail.c_str());
    ++g_skip;
}

//============================================================================
// One HLSL source compiled for everything: a fullscreen triangle, solid green.
//============================================================================
static const char* kHLSL = R"(
struct VSIn  { float2 pos : TEXCOORD0; };
struct VSOut { float4 pos : SV_Position; };

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos, 0.0, 1.0);
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    return float4(0.0, 1.0, 0.0, 1.0);   // green
}
)";

//============================================================================
// CPU-only: compile to each backend format and validate the blob signature
//============================================================================
static bool blob_has(const std::vector<uint8_t>& b, const char* s) {
    size_t n = std::strlen(s);
    if (b.size() < n) return false;
    for (size_t i = 0; i + n <= b.size(); ++i)
        if (std::memcmp(b.data() + i, s, n) == 0) return true;
    return false;
}

static void test_cross_compile() {
    std::printf("\n=== cross-compile (no device) ===\n");
    const size_t len = std::strlen(kHLSL);

    // Vulkan -> SPIR-V (magic word 0x07230203).
    {
        ShaderCompileOptions o; o.target = Backend::Vulkan;
        ShaderCompileResult r = ShaderCompiler::compile(kHLSL, len, ShaderStage::Fragment, "ps_main", o);
        bool magic = r.ok && r.bytecode.size() >= 4;
        if (magic) { uint32_t m; std::memcpy(&m, r.bytecode.data(), 4); magic = (m == 0x07230203u); }
        check("spirv (Vulkan)", r.ok && r.language == ShaderLanguage::SPIRV && magic,
              r.ok ? (std::to_string(r.bytecode.size()) + " bytes") : r.log);
    }
    // OpenGL -> GLSL text (#version ...).
    {
        ShaderCompileOptions o; o.target = Backend::OpenGL;
        ShaderCompileResult r = ShaderCompiler::compile(kHLSL, len, ShaderStage::Fragment, "ps_main", o);
        check("glsl (OpenGL)", r.ok && r.language == ShaderLanguage::GLSL && blob_has(r.bytecode, "#version"),
              r.ok ? (std::to_string(r.bytecode.size()) + " bytes") : r.log);
    }
    // Metal -> MSL text (metal_stdlib).
    {
        ShaderCompileOptions o; o.target = Backend::Metal;
        ShaderCompileResult r = ShaderCompiler::compile(kHLSL, len, ShaderStage::Fragment, "ps_main", o);
        check("msl (Metal)", r.ok && r.language == ShaderLanguage::MSL && blob_has(r.bytecode, "metal_stdlib"),
              r.ok ? (std::to_string(r.bytecode.size()) + " bytes") : r.log);
    }
    // D3D -> DXBC ("DXBC" FourCC). Windows-only (d3dcompiler).
    {
        ShaderCompileOptions o; o.target = Backend::D3D11;
        ShaderCompileResult r = ShaderCompiler::compile(kHLSL, len, ShaderStage::Fragment, "ps_main", o);
#ifdef _WIN32
        bool fourcc = r.ok && r.bytecode.size() >= 4 && std::memcmp(r.bytecode.data(), "DXBC", 4) == 0;
        check("dxbc (Direct3D)", r.ok && r.language == ShaderLanguage::DXBC && fourcc,
              r.ok ? (std::to_string(r.bytecode.size()) + " bytes") : r.log);
#else
        skip("dxbc (Direct3D)", "d3dcompiler is Windows-only");
        (void)r;
#endif
    }
}

//============================================================================
// Per-backend: compile + create + draw + read back
//============================================================================
static const int RT_W = 32, RT_H = 32;

static void test_backend_draw(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    const size_t len = std::strlen(kHLSL);

    // Compile + create both stages straight onto this device's backend.
    ShaderCompileResult vlog, plog;
    ShaderHandle vs = ShaderCompiler::compile_and_create(dev, kHLSL, len, ShaderStage::Vertex,   "vs_main", {}, &vlog);
    ShaderHandle ps = ShaderCompiler::compile_and_create(dev, kHLSL, len, ShaderStage::Fragment, "ps_main", {}, &plog);
    check("compile+create vs", vs.valid(), vlog.ok ? "" : vlog.log);
    check("compile+create ps", ps.valid(), plog.ok ? "" : plog.log);
    if (!vs.valid() || !ps.valid()) return;

    // Pipeline: fullscreen triangle, no depth, no cull.
    PipelineDesc pd;
    pd.vertex_shader = vs; pd.fragment_shader = ps;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.rasterizer = RasterizerState::no_cull();
    VertexLayout vl; vl.attributes[0] = { 0, VertexFormat::Float2, 0, 0 }; vl.attribute_count = 1;
    vl.strides[0] = 8; vl.buffer_count = 1; pd.vertex_layout = vl;
    PipelineHandle pipe = dev->create_pipeline(pd);
    check("create_pipeline", pipe.valid());

    // Over-large triangle covering the whole RT.
    const float verts[] = { -1.f, -1.f,  3.f, -1.f,  -1.f, 3.f };
    BufferDesc bd; bd.size = sizeof verts; bd.type = BufferType::Vertex; bd.initial_data = verts; bd.stride = 8;
    BufferHandle vb = dev->create_buffer(bd);

    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = dev->create_render_target(rd);

    if (pipe.valid() && vb.valid() && rt.valid()) {
        cmd->begin();
        cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
        Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; cmd->set_viewport(vp);
        ScissorRect sc; sc.x = 0; sc.y = 0; sc.width = RT_W; sc.height = RT_H; cmd->set_scissor(sc);
        cmd->clear_color(ClearColor(0.f, 0.f, 0.f, 1.f));
        cmd->set_pipeline(pipe);
        cmd->bind_vertex_buffer(0, vb, 0);
        cmd->draw(3);
        cmd->end();
        submit_commander(gfx, cmd);

        std::vector<uint8_t> px(size_t(RT_W) * RT_H * 4, 0);
        TextureHandle tex = dev->render_target_texture(rt);
        TextureRegion r; r.x = 0; r.y = 0; r.width = RT_W; r.height = RT_H; r.mip = 0; r.layer = 0;
        dev->read_texture(tex, r, px.data());
        const uint8_t* c = &px[(size_t(RT_H / 2) * RT_W + RT_W / 2) * 4];
        bool green = c[0] < 40 && c[1] > 200 && c[2] < 40 && c[3] > 200;
        char d[64]; std::snprintf(d, sizeof d, "centre=(%d,%d,%d,%d)", c[0], c[1], c[2], c[3]);
        check("draw runs (green)", green, d);
    } else {
        skip("draw runs (green)", "pipeline/buffer/RT create failed");
    }

    if (rt.valid())   dev->destroy_render_target(rt);
    if (vb.valid())   dev->destroy_buffer(vb);
    if (pipe.valid()) dev->destroy_pipeline(pipe);
    dev->destroy_shader(vs);
    dev->destroy_shader(ps);
}

static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_shader_compiler";
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
    if (!dev) { skip("backend", std::string("no RHI GraphicDevice: ") + result_to_string(dr)); windows[0]->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { skip("backend", "no GraphicCommander"); destroy_device(dev); windows[0]->destroy(); return false; }

    test_backend_draw(gfx, dev, cmd);

    destroy_commander(cmd);
    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("renderer built-in shader compiler test\n");
    std::printf("ShaderCompiler::available() = %s\n", ShaderCompiler::available() ? "true" : "false");
    if (!ShaderCompiler::available()) {
        std::printf("compiler not built (WINDOW_ENABLE_SHADER_COMPILER=OFF) - nothing to test\n");
        return 0;
    }

    ShaderCompiler::initialize();
    test_cross_compile();

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

    ShaderCompiler::shutdown();
    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
