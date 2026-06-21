/*
 * render_pipeline.cpp - Graphics pipeline conformance test.
 *
 * Exercises the whole RHI (graphics_api.hpp: GraphicDevice + GraphicCommander)
 * by rendering to OFFSCREEN render targets and reading the pixels back, so every
 * check is a hard numeric assertion rather than "a window appeared". Runs against
 * each backend available on the platform.
 *
 *   Tier A (every backend, no app shaders):
 *     device/commander creation, buffer create/update/map/readback, texture
 *     upload+readback, render-target clear+readback, depth target, fence, queries.
 *
 *   Tier B (programmable pipeline; OpenGL here, GLSL inline):
 *     shader compile, pipeline creation, triangle draw, interpolated vertex color,
 *     indexed draw, instanced draw, uniform buffer, texture sampling, depth test,
 *     alpha blend, scissor, blit, MSAA resolve, compute (SSBO), timestamp query.
 *
 * Exit code = number of failed checks (0 = all good).
 */

#include "window.hpp"
#include "graphics_api.hpp"
#include "render_pipeline_vk_spv.h"   // glslc-compiled SPIR-V for the Vulkan Tier B draws
#include "render_pipeline_d3d_dxbc.h" // fxc-compiled DXBC for the Direct3D 11 Tier B draws
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

using namespace window;

//=============================================================================
// Tiny test harness
//=============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;
static const char* g_backend_name = "?";

static void pass(const char* name, const std::string& detail) {
    std::printf("    [PASS] %-22s %s\n", name, detail.c_str()); ++g_pass;
}
static void fail(const char* name, const std::string& detail) {
    std::printf("    [FAIL] %-22s %s\n", name, detail.c_str()); ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-22s %s\n", name, detail.c_str()); ++g_skip;
}
static void check(const char* name, bool ok, const std::string& detail) {
    ok ? pass(name, detail) : fail(name, detail);
}

static std::string rgba_str(const uint8_t* p) {
    char b[64]; std::snprintf(b, sizeof b, "(%d,%d,%d,%d)", p[0], p[1], p[2], p[3]); return b;
}
static bool near_rgba(const uint8_t* p, int r, int g, int b, int a, int tol) {
    return std::abs(p[0]-r) <= tol && std::abs(p[1]-g) <= tol &&
           std::abs(p[2]-b) <= tol && std::abs(p[3]-a) <= tol;
}

//=============================================================================
// Offscreen helpers
//=============================================================================
static const int RT_W = 64, RT_H = 64;

struct Ctx {
    Graphics*         gfx = nullptr;
    GraphicDevice*    dev = nullptr;
    GraphicCommander* cmd = nullptr;
    Backend           backend = Backend::OpenGL;
};

static std::vector<uint8_t> read_rt(Ctx& c, RenderTargetHandle rt, int w = RT_W, int h = RT_H) {
    std::vector<uint8_t> px(size_t(w) * h * 4, 0);
    TextureHandle tex = c.dev->render_target_texture(rt);
    TextureRegion r; r.x = 0; r.y = 0; r.width = w; r.height = h; r.mip = 0; r.layer = 0;
    c.dev->read_texture(tex, r, px.data());
    return px;
}
static const uint8_t* at(const std::vector<uint8_t>& px, int w, int x, int y) {
    return &px[(size_t(y) * w + x) * 4];
}

//=============================================================================
// GLSL shader sources (OpenGL Tier B). #version 460 for explicit binding= layout.
//=============================================================================
static const char* VS_POS = R"(#version 460 core
layout(location=0) in vec2 aPos;
void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }
)";
static const char* VS_POS3 = R"(#version 460 core
layout(location=0) in vec3 aPos;
void main(){ gl_Position = vec4(aPos, 1.0); }
)";
static const char* VS_POS_COL = R"(#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aCol;
out vec4 vCol;
void main(){ vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
static const char* VS_POS_UV = R"(#version 460 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)";
static const char* VS_INSTANCED = R"(#version 460 core
layout(location=0) in vec2 aPos;
void main(){
    vec2 off = vec2((gl_InstanceID & 1)==0 ? -0.5 : 0.5,
                    (gl_InstanceID < 2)     ? -0.5 : 0.5);
    gl_Position = vec4(aPos * 0.30 + off, 0.0, 1.0);
}
)";
static const char* FS_GREEN = R"(#version 460 core
out vec4 o;
void main(){ o = vec4(0.0, 1.0, 0.0, 1.0); }
)";
static const char* FS_VCOL = R"(#version 460 core
in vec4 vCol; out vec4 o;
void main(){ o = vCol; }
)";
static const char* FS_REDA = R"(#version 460 core
out vec4 o;
void main(){ o = vec4(1.0, 0.0, 0.0, 0.5); }
)";
static const char* FS_RED = R"(#version 460 core
out vec4 o;
void main(){ o = vec4(1.0, 0.0, 0.0, 1.0); }
)";
static const char* FS_UBO = R"(#version 460 core
layout(std140, binding=0) uniform U { vec4 uColor; };
out vec4 o;
void main(){ o = uColor; }
)";
static const char* FS_TEX = R"(#version 460 core
layout(binding=0) uniform sampler2D uTex;
in vec2 vUV; out vec4 o;
void main(){ o = texture(uTex, vUV); }
)";
static const char* CS_FILL = R"(#version 460 core
layout(local_size_x=64) in;
layout(std430, binding=0) buffer B { uint data[]; };
void main(){ uint i = gl_GlobalInvocationID.x; data[i] = i * 2u; }
)";

// Logical shader identity, resolved to the backend's bytecode/source by make_shader().
enum class SK { VS_POS, VS_POS3, VS_POSCOL, VS_POSUV, VS_INST, FS_GREEN, FS_VCOL, FS_REDA, FS_RED, FS_UBO, FS_TEX, CS_FILL };

struct SpvBlob { const uint32_t* code; size_t bytes; };
static SpvBlob vk_spv(SK k) {
    switch (k) {
        case SK::VS_POS:    return { SPV_POS_VERT,    sizeof SPV_POS_VERT };
        case SK::VS_POS3:   return { SPV_POS3_VERT,   sizeof SPV_POS3_VERT };
        case SK::VS_POSCOL: return { SPV_POSCOL_VERT, sizeof SPV_POSCOL_VERT };
        case SK::VS_INST:   return { SPV_INST_VERT,   sizeof SPV_INST_VERT };
        case SK::VS_POSUV:  return { SPV_POSUV_VERT,  sizeof SPV_POSUV_VERT };
        case SK::FS_GREEN:  return { SPV_GREEN_FRAG,  sizeof SPV_GREEN_FRAG };
        case SK::FS_VCOL:   return { SPV_VCOL_FRAG,   sizeof SPV_VCOL_FRAG };
        case SK::FS_REDA:   return { SPV_REDA_FRAG,   sizeof SPV_REDA_FRAG };
        case SK::FS_RED:    return { SPV_RED_FRAG,    sizeof SPV_RED_FRAG };
        case SK::FS_UBO:    return { SPV_UBO_FRAG,    sizeof SPV_UBO_FRAG };
        case SK::FS_TEX:    return { SPV_TEX_FRAG,    sizeof SPV_TEX_FRAG };
        case SK::CS_FILL:   return { SPV_FILL_COMP,   sizeof SPV_FILL_COMP };
        default:            return { nullptr, 0 };
    }
}
static const char* gl_glsl(SK k) {
    switch (k) {
        case SK::VS_POS:    return VS_POS;     case SK::VS_POS3:  return VS_POS3;
        case SK::VS_POSCOL: return VS_POS_COL; case SK::VS_POSUV: return VS_POS_UV;
        case SK::VS_INST:   return VS_INSTANCED;
        case SK::FS_GREEN:  return FS_GREEN;   case SK::FS_VCOL:  return FS_VCOL;
        case SK::FS_REDA:   return FS_REDA;    case SK::FS_RED:   return FS_RED;
        case SK::FS_UBO:    return FS_UBO;     case SK::FS_TEX:   return FS_TEX;
        case SK::CS_FILL:   return CS_FILL;
    }
    return nullptr;
}
struct DxbcBlob { const void* code; size_t bytes; };
static DxbcBlob d3d_dxbc(SK k) {
    switch (k) {
        case SK::VS_POS:    return { DXBC_POS_VS,    sizeof DXBC_POS_VS };
        case SK::VS_POS3:   return { DXBC_POS3_VS,   sizeof DXBC_POS3_VS };
        case SK::VS_POSCOL: return { DXBC_POSCOL_VS, sizeof DXBC_POSCOL_VS };
        case SK::VS_POSUV:  return { DXBC_POSUV_VS,  sizeof DXBC_POSUV_VS };
        case SK::VS_INST:   return { DXBC_INST_VS,   sizeof DXBC_INST_VS };
        case SK::FS_GREEN:  return { DXBC_GREEN_PS,  sizeof DXBC_GREEN_PS };
        case SK::FS_VCOL:   return { DXBC_VCOL_PS,   sizeof DXBC_VCOL_PS };
        case SK::FS_REDA:   return { DXBC_REDA_PS,   sizeof DXBC_REDA_PS };
        case SK::FS_RED:    return { DXBC_RED_PS,    sizeof DXBC_RED_PS };
        case SK::FS_UBO:    return { DXBC_UBO_PS,    sizeof DXBC_UBO_PS };
        case SK::FS_TEX:    return { DXBC_TEX_PS,    sizeof DXBC_TEX_PS };
        case SK::CS_FILL:   return { DXBC_FILL_CS,   sizeof DXBC_FILL_CS };
        default:            return { nullptr, 0 };
    }
}
// Backend-aware shader source: GLSL on OpenGL, SPIR-V on Vulkan, DXBC on D3D11.
// Returns an invalid handle when a kind has no bytecode for the backend (caller skips).
static ShaderHandle make_shader(Ctx& c, ShaderStage stage, SK kind) {
    ShaderDesc d; d.stage = stage;
    if (c.backend == Backend::Vulkan) {
        SpvBlob b = vk_spv(kind);
        if (!b.code) return { -1 };
        d.language = ShaderLanguage::SPIRV; d.code = b.code; d.code_size = b.bytes;
    } else if (c.backend == Backend::D3D11 || c.backend == Backend::D3D12) {
        DxbcBlob b = d3d_dxbc(kind);   // D3D12 also accepts DXBC (SM5) bytecode
        if (!b.code) return { -1 };
        d.language = ShaderLanguage::DXBC; d.code = b.code; d.code_size = b.bytes;
    } else {
        d.language = ShaderLanguage::GLSL; d.code = gl_glsl(kind); d.code_size = 0;
    }
    return c.dev->create_shader(d);
}

// Fullscreen-covering triangle (over-large so it fills the whole RT).
static BufferHandle make_fulltri(Ctx& c) {
    const float v[] = { -1.f,-1.f,  3.f,-1.f,  -1.f,3.f };
    BufferDesc bd; bd.size = sizeof v; bd.type = BufferType::Vertex; bd.initial_data = v; bd.stride = 8;
    return c.dev->create_buffer(bd);
}
static VertexLayout layout_pos2() {
    VertexLayout l; l.attributes[0] = { 0, VertexFormat::Float2, 0, 0 }; l.attribute_count = 1;
    l.strides[0] = 8; l.buffer_count = 1; return l;
}

//=============================================================================
// Tier A — backend-agnostic resource / sync tests (no app shaders)
//=============================================================================
static void test_buffers(Ctx& c) {
    // create + readback
    uint32_t src[16]; for (int i = 0; i < 16; ++i) src[i] = 0xA0000000u | i;
    BufferDesc bd; bd.size = sizeof src; bd.type = BufferType::Storage;
    bd.usage = ResourceUsage::Default; bd.initial_data = src;
    BufferHandle b = c.dev->create_buffer(bd);
    if (!b.valid()) { fail("buffer.create", "create_buffer returned invalid"); return; }

    uint32_t back[16] = {};
    c.dev->read_buffer(b, back, sizeof back, 0);
    check("buffer.roundtrip", std::memcmp(src, back, sizeof src) == 0,
          "create initial_data -> read_buffer");

    // update_buffer
    uint32_t upd[16]; for (int i = 0; i < 16; ++i) upd[i] = 0xB0000000u | (i * 7);
    c.dev->update_buffer(b, upd, sizeof upd, 0);
    std::memset(back, 0, sizeof back);
    c.dev->read_buffer(b, back, sizeof back, 0);
    check("buffer.update", std::memcmp(upd, back, sizeof upd) == 0, "update_buffer -> read_buffer");

    // map / unmap
    void* m = c.dev->map_buffer(b, 0, sizeof back);
    if (m) {
        for (int i = 0; i < 16; ++i) static_cast<uint32_t*>(m)[i] = 0xC0000000u | i;
        c.dev->unmap_buffer(b);
        std::memset(back, 0, sizeof back);
        c.dev->read_buffer(b, back, sizeof back, 0);
        bool ok = true; for (int i = 0; i < 16; ++i) ok &= (back[i] == (0xC0000000u | uint32_t(i)));
        check("buffer.map", ok, "map_buffer write -> read_buffer");
    } else {
        skip("buffer.map", "map_buffer returned null on this backend");
    }
    c.dev->destroy_buffer(b);
}

static void test_texture_upload(Ctx& c) {
    // 4x4 RGBA8 known pattern; upload at create, read back via read_texture.
    uint8_t tex[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) { tex[i*4+0]=uint8_t(i*16); tex[i*4+1]=uint8_t(255-i*16); tex[i*4+2]=uint8_t(i*8); tex[i*4+3]=255; }
    TextureDesc td; td.width = 4; td.height = 4; td.format = TextureFormat::RGBA8_UNORM;
    td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST;
    td.initial_data = tex;
    TextureHandle t = c.dev->create_texture(td);
    if (!t.valid()) { fail("texture.upload", "create_texture invalid"); return; }
    uint8_t back[4 * 4 * 4] = {};
    TextureRegion r; r.x = 0; r.y = 0; r.width = 4; r.height = 4;
    c.dev->read_texture(t, r, back);
    int diff = 0; for (int i = 0; i < (int)sizeof tex; ++i) diff += std::abs(int(tex[i]) - int(back[i]));
    check("texture.upload", diff <= 8, "create initial_data -> read_texture (sum|d|=" + std::to_string(diff) + ")");
    c.dev->destroy_texture(t);
}

// Block-compressed upload/readback: the bytes are opaque BC1 blocks, so a correct
// block-aware update_texture/read_texture round-trips them byte-exact. Exercises both
// the create-with-initial_data and the explicit update_texture paths.
static void test_compressed_texture(Ctx& c) {
    const int W = 8, H = 8;   // 2x2 BC1 blocks (8 bytes each) = 32 bytes
    const size_t bytes = texture_format_image_size(TextureFormat::BC1_UNORM, W, H);
    std::vector<uint8_t> blocks(bytes);
    for (size_t i = 0; i < bytes; ++i) blocks[i] = uint8_t((i * 37 + 11) & 0xFF);

    auto run = [&](const char* tag, bool via_update) {
        TextureDesc td; td.width = W; td.height = H; td.format = TextureFormat::BC1_UNORM;
        td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST;
        if (!via_update) td.initial_data = blocks.data();
        TextureHandle t = c.dev->create_texture(td);
        if (!t.valid()) { skip(tag, "BC1 not supported on this backend"); return; }
        if (via_update) { TextureRegion u; u.x = u.y = 0; u.width = W; u.height = H; c.dev->update_texture(t, u, blocks.data()); }
        std::vector<uint8_t> back(bytes, 0);
        TextureRegion r; r.x = r.y = 0; r.width = W; r.height = H;
        c.dev->read_texture(t, r, back.data());
        bool ok = true; size_t bad = 0;
        for (size_t i = 0; i < bytes; ++i) if (blocks[i] != back[i]) { ok = false; ++bad; }
        check(tag, ok, ok ? "BC1 32B round-trip exact" : (std::to_string(bad) + "/" + std::to_string(bytes) + " bytes differ"));
        c.dev->destroy_texture(t);
    };
    run("texture.bc1_initial", false);   // create_texture(initial_data) -> read_texture
    run("texture.bc1_update",  true);    // create_texture + update_texture -> read_texture
}

static void test_rt_clear(Ctx& c) {
    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = c.dev->create_render_target(rd);
    if (!rt.valid()) { fail("rt.clear", "create_render_target invalid"); return; }

    c.cmd->begin();
    c.cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; c.cmd->set_viewport(vp);
    c.cmd->clear_color(ClearColor(0.2f, 0.4f, 0.6f, 1.0f));
    c.cmd->end();
    submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, RT_W/2, RT_H/2);
    check("rt.clear", near_rgba(p, 51, 102, 153, 255, 3), "clear (0.2,0.4,0.6) -> " + rgba_str(p));
    c.dev->destroy_render_target(rt);
}

static void test_depth_target(Ctx& c) {
    DepthStencilDesc dd; dd.width = RT_W; dd.height = RT_H; dd.format = TextureFormat::D24_UNORM_S8_UINT;
    RenderTargetHandle dt = c.dev->create_depth_target(dd);
    check("depth.target", dt.valid(), "create_depth_target");
    if (dt.valid()) c.dev->destroy_render_target(dt);
}

static void test_fence(Ctx& c) {
    FenceHandle f = c.dev->create_fence(false);
    if (!f.valid()) { skip("sync.fence", "create_fence unsupported"); return; }
    // Submit a trivial cleared frame signalling the fence.
    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = c.dev->create_render_target(rd);
    c.cmd->begin();
    c.cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
    c.cmd->clear_color(ClearColor::black());
    c.cmd->end();
    submit_commander(c.gfx, c.cmd, f);
    bool signaled = c.dev->wait_fence(f, 1000ull * 1000 * 1000);  // 1s
    check("sync.fence", signaled, "submit with fence -> wait_fence");
    c.dev->wait_idle();
    c.dev->destroy_render_target(rt);
    c.dev->destroy_fence(f);
}

static void test_caps(Ctx& c) {
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps);
    c.dev->get_capabilities(&caps);
    char b[160];
    std::snprintf(b, sizeof b, "GL%d.%d max_tex=%d compute=%d instancing=%d timestamp=%d msaa=%d",
                  caps.api_version_major, caps.api_version_minor, caps.max_texture_size,
                  caps.compute_shaders, caps.instancing, caps.timestamp_query, caps.max_samples);
    check("device.caps", caps.max_texture_size > 0, b);
}

//=============================================================================
// Tier B — programmable pipeline (OpenGL / GLSL)
//=============================================================================
static RenderTargetHandle new_rt(Ctx& c, int samples = 1) {
    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM; rd.samples = samples;
    return c.dev->create_render_target(rd);
}
static void begin_rt(Ctx& c, RenderTargetHandle rt, RenderTargetHandle depth = RenderTargetHandle{}) {
    c.cmd->begin();
    c.cmd->set_render_targets(&rt, 1, depth);
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; c.cmd->set_viewport(vp);
    // Backends with dynamic scissor (Vulkan) clip to an empty rect until one is set;
    // a full-RT default makes draws appear. Harmless on GL (scissor stays disabled in
    // the pipeline unless a test opts in via rasterizer.scissor_enable).
    ScissorRect full; full.x = 0; full.y = 0; full.width = RT_W; full.height = RT_H; c.cmd->set_scissor(full);
}

// Modern-binding backends (Vulkan) bind resources only through descriptor sets, so
// the UBO / texture / storage-buffer tests must build a 1-binding set + pipeline
// layout there. On OpenGL these stay invalid and the test uses slot binding instead.
static bool uses_desc_sets(Ctx& c) { return c.backend == Backend::Vulkan || c.backend == Backend::D3D12; }

struct DescSet {
    DescriptorSetLayoutHandle dsl;
    PipelineLayoutHandle      pll;
    DescriptorSetHandle       set;
    bool valid() const { return set.valid(); }
    void destroy(Ctx& c) {
        if (set.valid()) c.dev->destroy_descriptor_set(set);
        if (pll.valid()) c.dev->destroy_pipeline_layout(pll);
        if (dsl.valid()) c.dev->destroy_descriptor_set_layout(dsl);
    }
};
// Build a single-binding descriptor set (+ its layout and a matching pipeline layout)
// holding `write`. Caller assigns d.pll to PipelineDesc::layout and binds set 0.
static DescSet make_desc_set(Ctx& c, BindingType type, uint32_t stages, const DescriptorWrite& write) {
    DescSet d{};
    DescriptorSetLayoutDesc dl; dl.binding_count = 1; dl.bindings[0] = { 0, type, 1, stages };
    d.dsl = c.dev->create_descriptor_set_layout(dl);
    PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = d.dsl;
    d.pll = c.dev->create_pipeline_layout(pl);
    DescriptorSetDesc sd; sd.layout = d.dsl; sd.write_count = 1; sd.writes[0] = write;
    sd.writes[0].binding = 0; sd.writes[0].type = type;
    d.set = c.dev->create_descriptor_set(sd);
    return d;
}

static void test_triangle(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    if (!vs.valid() || !fs.valid()) { fail("draw.triangle", "shader compile failed"); return; }
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.topology = PrimitiveTopology::TriangleList; pd.vertex_layout = layout_pos2();
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt);
    c.cmd->clear_color(ClearColor(0.f, 0.f, 0.f, 1.f));
    c.cmd->set_pipeline(pipe);
    c.cmd->bind_vertex_buffer(0, vb, 0);
    c.cmd->draw(3);
    c.cmd->end();
    submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("draw.triangle", near_rgba(p, 0, 255, 0, 255, 2), "fullscreen green -> " + rgba_str(p));

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_vertex_color(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POSCOL);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_VCOL);
    if (!vs.valid() || !fs.valid()) { fail("draw.vertex_color", "shader compile failed"); return; }
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    VertexLayout l; l.attributes[0] = { 0, VertexFormat::Float2, 0, 0 };
    l.attributes[1] = { 1, VertexFormat::Float4, 8, 0 }; l.attribute_count = 2;
    l.strides[0] = 24; l.buffer_count = 1; pd.vertex_layout = l;
    PipelineHandle pipe = c.dev->create_pipeline(pd);

    // big triangle, three pure colors at the corners -> center ~ average
    const float v[] = {
        -1.f,-1.f,  1,0,0,1,
         3.f,-1.f,  0,1,0,1,
        -1.f, 3.f,  0,0,1,1,
    };
    BufferDesc bd; bd.size = sizeof v; bd.type = BufferType::Vertex; bd.initial_data = v; bd.stride = 24;
    BufferHandle vb = c.dev->create_buffer(bd);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    bool interpolated = p[0] > 20 && p[1] > 20 && p[2] > 20 && p[0] < 235 && p[1] < 235 && p[2] < 235;
    check("draw.vertex_color", interpolated, "interpolated corners -> " + rgba_str(p));

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_indexed(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);

    const float v[] = { -0.9f,-0.9f,  0.9f,-0.9f,  0.9f,0.9f,  -0.9f,0.9f };
    const uint16_t idx[] = { 0,1,2, 0,2,3 };
    BufferDesc vbd; vbd.size = sizeof v; vbd.type = BufferType::Vertex; vbd.initial_data = v; vbd.stride = 8;
    BufferDesc ibd; ibd.size = sizeof idx; ibd.type = BufferType::Index; ibd.initial_data = idx;
    BufferHandle vb = c.dev->create_buffer(vbd), ib = c.dev->create_buffer(ibd);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0);
    c.cmd->bind_index_buffer(ib, IndexFormat::UInt16, 0); c.cmd->draw_indexed(6);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    bool ok = near_rgba(at(px, RT_W, 32, 32), 0,255,0,255, 2) &&
              near_rgba(at(px, RT_W, 12, 12), 0,255,0,255, 2) &&
              near_rgba(at(px, RT_W, 52, 52), 0,255,0,255, 2);
    check("draw.indexed", ok, "quad of 2 tris -> center " + rgba_str(at(px, RT_W, 32, 32)));

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb); c.dev->destroy_buffer(ib);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_instanced(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_INST);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);  // small triangle scaled in the shader

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0);
    c.cmd->draw(3, 0, 4, 0);  // 4 instances, one per quadrant
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    int lit = 0;
    const int qx[4] = {16,48,16,48}, qy[4] = {16,16,48,48};
    for (int i = 0; i < 4; ++i) if (at(px, RT_W, qx[i], qy[i])[1] > 200) ++lit;
    check("draw.instanced", lit == 4, std::to_string(lit) + "/4 quadrants drawn (instance_count=4)");

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_uniform_buffer(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_UBO);
    if (!vs.valid() || !fs.valid()) { fail("draw.uniform_buffer", "shader compile failed"); return; }

    const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    BufferDesc ubd; ubd.size = sizeof color; ubd.type = BufferType::Uniform;
    ubd.usage = ResourceUsage::Dynamic; ubd.initial_data = color;
    BufferHandle ub = c.dev->create_buffer(ubd);

    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.vertex_layout = layout_pos2();
    DescSet ds{};
    if (uses_desc_sets(c)) {
        DescriptorWrite w{}; w.buffer = ub; w.buffer_size = sizeof color;
        ds = make_desc_set(c, BindingType::UniformBuffer, STAGE_FRAGMENT, w);
        pd.layout = ds.pll;
    }
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe);
    if (ds.valid()) c.cmd->bind_descriptor_set(0, ds.set);
    else            c.cmd->bind_uniform_buffer(0, ub, 0, sizeof color);
    c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("draw.uniform_buffer", near_rgba(p, 64, 128, 191, 255, 3), "UBO color -> " + rgba_str(p));

    ds.destroy(c);
    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb); c.dev->destroy_buffer(ub);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_texture_sample(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POSUV);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_TEX);
    if (!vs.valid() || !fs.valid()) { fail("draw.texture_sample", "shader compile failed"); return; }
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    VertexLayout l; l.attributes[0] = { 0, VertexFormat::Float2, 0, 0 };
    l.attributes[1] = { 1, VertexFormat::Float2, 8, 0 }; l.attribute_count = 2;
    l.strides[0] = 16; l.buffer_count = 1; pd.vertex_layout = l;

    // solid magenta 2x2 texture; sampling anywhere -> magenta
    uint8_t tx[2*2*4]; for (int i=0;i<4;++i){ tx[i*4+0]=255; tx[i*4+1]=0; tx[i*4+2]=255; tx[i*4+3]=255; }
    TextureDesc td; td.width=2; td.height=2; td.format=TextureFormat::RGBA8_UNORM; td.usage=TEXTURE_USAGE_SAMPLED; td.initial_data=tx;
    TextureHandle tex = c.dev->create_texture(td);
    SamplerState ss = SamplerState::point_clamp(); SamplerHandle samp = c.dev->create_sampler(ss);

    DescSet ds{};
    if (uses_desc_sets(c)) {
        DescriptorWrite w{}; w.texture = tex; w.sampler = samp;
        ds = make_desc_set(c, BindingType::CombinedImageSampler, STAGE_FRAGMENT, w);
        pd.layout = ds.pll;
    }
    PipelineHandle pipe2 = c.dev->create_pipeline(pd);

    const float v[] = { -1,-1, 0,0,  3,-1, 2,0,  -1,3, 0,2 };
    BufferDesc bd; bd.size=sizeof v; bd.type=BufferType::Vertex; bd.initial_data=v; bd.stride=16;
    BufferHandle vb = c.dev->create_buffer(bd);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe2);
    if (ds.valid()) c.cmd->bind_descriptor_set(0, ds.set);
    else { c.cmd->bind_texture(0, tex); c.cmd->bind_sampler(0, samp); }
    c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("draw.texture_sample", near_rgba(p, 255, 0, 255, 255, 4), "sampled magenta -> " + rgba_str(p));

    ds.destroy(c);
    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_texture(tex); c.dev->destroy_sampler(samp);
    c.dev->destroy_pipeline(pipe2); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_depth(Ctx& c) {
    ShaderHandle vs  = make_shader(c, ShaderStage::Vertex, SK::VS_POS3);
    ShaderHandle fsG = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    ShaderHandle fsR = make_shader(c, ShaderStage::Fragment, SK::FS_RED);
    if (!vs.valid() || !fsG.valid() || !fsR.valid()) { fail("draw.depth_test", "shader compile failed"); return; }
    // D32_FLOAT (depth-only) on Vulkan avoids the D24S8 stencil-aspect handling; both
    // map clip-Z [0,1] to depth [0,1] (Vulkan) / [0,1] via [-1,1] remap (GL), so the
    // far/near ordering below holds on both. z is kept in [0,1] to avoid GL/VK NDC-Z skew.
    TextureFormat dfmt = (c.backend == Backend::Vulkan) ? TextureFormat::D32_FLOAT
                                                        : TextureFormat::D24_UNORM_S8_UINT;
    PipelineDesc pd; pd.vertex_shader = vs;
    pd.depth_stencil = DepthStencilState::depth_test();  // enable + write + Less
    pd.rasterizer = RasterizerState::no_cull();
    VertexLayout l; l.attributes[0] = { 0, VertexFormat::Float3, 0, 0 }; l.attribute_count = 1;
    l.strides[0] = 12; l.buffer_count = 1; pd.vertex_layout = l;
    pd.depth_format = dfmt;
    pd.fragment_shader = fsG; PipelineHandle pipeFar  = c.dev->create_pipeline(pd);
    pd.fragment_shader = fsR; PipelineHandle pipeNear = c.dev->create_pipeline(pd);

    auto tri = [&](float z) {
        const float v[] = { -1,-1,z,  3,-1,z,  -1,3,z };
        BufferDesc bd; bd.size=sizeof v; bd.type=BufferType::Vertex; bd.initial_data=v; bd.stride=12;
        return c.dev->create_buffer(bd);
    };
    BufferHandle vbFar  = tri(0.75f);  // farther
    BufferHandle vbNear = tri(0.25f);  // nearer (smaller depth -> Less passes)

    RenderTargetHandle rt = new_rt(c);
    DepthStencilDesc dd; dd.width=RT_W; dd.height=RT_H; dd.format=dfmt;
    RenderTargetHandle dt = c.dev->create_depth_target(dd);

    begin_rt(c, rt, dt);
    c.cmd->clear_color(ClearColor::black());
    ClearDepthStencil cds; cds.depth = 1.0f; cds.stencil = 0; c.cmd->clear_depth_stencil(cds);
    c.cmd->set_pipeline(pipeFar);  c.cmd->bind_vertex_buffer(0, vbFar, 0);  c.cmd->draw(3);   // far green
    c.cmd->set_pipeline(pipeNear); c.cmd->bind_vertex_buffer(0, vbNear, 0); c.cmd->draw(3);   // near red occludes
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("draw.depth_test", near_rgba(p, 255, 0, 0, 255, 3), "near red occludes far green -> " + rgba_str(p));

    c.dev->destroy_render_target(rt); c.dev->destroy_render_target(dt);
    c.dev->destroy_buffer(vbFar); c.dev->destroy_buffer(vbNear);
    c.dev->destroy_pipeline(pipeFar); c.dev->destroy_pipeline(pipeNear);
    c.dev->destroy_shader(vs); c.dev->destroy_shader(fsG); c.dev->destroy_shader(fsR);
}

static void test_blend(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_REDA);  // red, alpha 0.5
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.blend = BlendState::alpha_blend(); pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt);
    c.cmd->clear_color(ClearColor(0.f, 0.f, 1.f, 1.f));  // blue background
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    const uint8_t* p = at(px, RT_W, 32, 32);
    // 0.5*red over blue = (128, 0, 128)
    check("draw.alpha_blend", near_rgba(p, 128, 0, 128, 255, 6), "0.5 red over blue -> " + rgba_str(p));

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_scissor(Ctx& c) {
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.rasterizer = RasterizerState::no_cull(); pd.rasterizer.scissor_enable = true;
    pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle rt = new_rt(c);
    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe);
    ScissorRect sc; sc.x = 32; sc.y = 0; sc.width = 32; sc.height = 64; c.cmd->set_scissor(sc);  // right half only
    c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, rt);
    bool left_clear = near_rgba(at(px, RT_W, 12, 32), 0,0,0,255, 2);
    bool right_green = near_rgba(at(px, RT_W, 52, 32), 0,255,0,255, 2);
    check("draw.scissor", left_clear && right_green,
          "left " + rgba_str(at(px,RT_W,12,32)) + " right " + rgba_str(at(px,RT_W,52,32)));

    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_blit(Ctx& c) {
    // Draw green into src, blit to dst, read dst.
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle src = new_rt(c), dst = new_rt(c);
    begin_rt(c, src); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->blit_render_target(dst, src, 0,0,RT_W,RT_H, 0,0,RT_W,RT_H, false);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, dst);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("cmd.blit", near_rgba(p, 0,255,0,255, 2), "blit src->dst -> " + rgba_str(p));

    c.dev->destroy_render_target(src); c.dev->destroy_render_target(dst);
    c.dev->destroy_buffer(vb); c.dev->destroy_pipeline(pipe);
    c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_msaa_resolve(Ctx& c) {
    GraphicsCapabilities caps; std::memset(&caps,0,sizeof caps); c.dev->get_capabilities(&caps);
    if (caps.max_samples < 4) { skip("cmd.msaa_resolve", "max_samples < 4"); return; }
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.rasterizer.multisample_enable = true; pd.samples = 4; pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);

    RenderTargetHandle msaa = new_rt(c, 4), resolved = new_rt(c, 1);
    begin_rt(c, msaa); c.cmd->clear_color(ClearColor::black());
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->resolve_render_target(resolved, msaa);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);

    auto px = read_rt(c, resolved);
    const uint8_t* p = at(px, RT_W, 32, 32);
    check("cmd.msaa_resolve", near_rgba(p, 0,255,0,255, 2), "4x MSAA resolved -> " + rgba_str(p));

    c.dev->destroy_render_target(msaa); c.dev->destroy_render_target(resolved);
    c.dev->destroy_buffer(vb); c.dev->destroy_pipeline(pipe);
    c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

static void test_compute(Ctx& c) {
    GraphicsCapabilities caps; std::memset(&caps,0,sizeof caps); c.dev->get_capabilities(&caps);
    if (!caps.compute_shaders) { skip("compute.ssbo", "compute_shaders unsupported"); return; }
    ShaderHandle cs = make_shader(c, ShaderStage::Compute, SK::CS_FILL);
    if (!cs.valid()) { fail("compute.ssbo", "compute shader compile failed"); return; }

    const int N = 64;
    BufferDesc bd; bd.size = N * sizeof(uint32_t); bd.type = BufferType::Storage; bd.usage = ResourceUsage::Default;
    std::vector<uint32_t> zero(N, 0u); bd.initial_data = zero.data();
    BufferHandle ssbo = c.dev->create_buffer(bd);

    PipelineDesc pd; pd.compute_shader = cs;
    DescSet ds{};
    if (uses_desc_sets(c)) {
        DescriptorWrite w{}; w.buffer = ssbo; w.buffer_size = N * sizeof(uint32_t);
        ds = make_desc_set(c, BindingType::StorageBuffer, STAGE_COMPUTE, w);
        pd.layout = ds.pll;
    }
    PipelineHandle pipe = c.dev->create_pipeline(pd);

    c.cmd->begin();
    c.cmd->set_pipeline(pipe);
    if (ds.valid()) c.cmd->bind_descriptor_set(0, ds.set);
    else            c.cmd->bind_storage_buffer(0, ssbo, 0, 0);
    c.cmd->dispatch(1, 1, 1);  // 64 threads
    c.cmd->memory_barrier(GPU_BARRIER_STORAGE_BUFFER);
    c.cmd->end(); submit_commander(c.gfx, c.cmd);
    c.dev->wait_idle();

    std::vector<uint32_t> back(N, 0xFFFFFFFFu);
    c.dev->read_buffer(ssbo, back.data(), N * sizeof(uint32_t), 0);
    int bad = 0; for (int i = 0; i < N; ++i) if (back[i] != uint32_t(i*2)) ++bad;
    check("compute.ssbo", bad == 0, "data[i]=i*2 via dispatch (" + std::to_string(N-bad) + "/" + std::to_string(N) + " correct)");

    ds.destroy(c);
    c.dev->destroy_buffer(ssbo); c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(cs);
}

static void test_timestamp(Ctx& c) {
    GraphicsCapabilities caps; std::memset(&caps,0,sizeof caps); c.dev->get_capabilities(&caps);
    if (!caps.timestamp_query) { skip("query.timestamp", "timestamp_query unsupported"); return; }
    QueryHandle q0 = c.dev->create_query(QueryType::Timestamp);
    QueryHandle q1 = c.dev->create_query(QueryType::Timestamp);
    ShaderHandle vs = make_shader(c, ShaderStage::Vertex, SK::VS_POS);
    ShaderHandle fs = make_shader(c, ShaderStage::Fragment, SK::FS_GREEN);
    PipelineDesc pd; pd.vertex_shader = vs; pd.fragment_shader = fs;
    pd.depth_stencil = DepthStencilState::disabled(); pd.rasterizer = RasterizerState::no_cull();
    pd.vertex_layout = layout_pos2();
    PipelineHandle pipe = c.dev->create_pipeline(pd);
    BufferHandle vb = make_fulltri(c);
    RenderTargetHandle rt = new_rt(c);

    begin_rt(c, rt); c.cmd->clear_color(ClearColor::black());
    c.cmd->write_timestamp(q0);
    c.cmd->set_pipeline(pipe); c.cmd->bind_vertex_buffer(0, vb, 0); c.cmd->draw(3);
    c.cmd->write_timestamp(q1);
    c.cmd->end(); submit_commander(c.gfx, c.cmd); c.dev->wait_idle();

    uint64_t t0 = 0, t1 = 0;
    bool ok0 = c.dev->get_query_result(q0, &t0, true);
    bool ok1 = c.dev->get_query_result(q1, &t1, true);
    check("query.timestamp", ok0 && ok1 && t1 >= t0, "t0=" + std::to_string(t0) + " t1=" + std::to_string(t1));

    c.dev->destroy_query(q0); c.dev->destroy_query(q1);
    c.dev->destroy_render_target(rt); c.dev->destroy_buffer(vb);
    c.dev->destroy_pipeline(pipe); c.dev->destroy_shader(vs); c.dev->destroy_shader(fs);
}

//=============================================================================
// Per-backend driver
//=============================================================================
static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);
    g_backend_name = name;

    Config config;
    config.backend = backend;
    config.windows[0].title = "render_pipeline";
    config.windows[0].width = 128; config.windows[0].height = 128;
    config.windows[0].visible = false;

    Result wr;
    auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) {
        std::printf("    [SKIP] (window/context create failed: %s)\n", result_to_string(wr));
        ++g_skip; return false;
    }
    Ctx c; c.backend = backend; c.gfx = windows[0]->graphics();

    Result dr;
    c.dev = create_device(c.gfx, &dr);
    if (!c.dev) {
        std::printf("    [SKIP] (no RHI GraphicDevice for this backend: %s)\n", result_to_string(dr));
        ++g_skip; windows[0]->destroy(); return false;
    }
    c.cmd = create_commander(c.gfx, c.dev, &dr);
    if (!c.cmd) {
        std::printf("    [SKIP] (no GraphicCommander: %s)\n", result_to_string(dr));
        ++g_skip; destroy_device(c.dev); windows[0]->destroy(); return false;
    }

    // Tier A — every backend
    test_caps(c);
    test_buffers(c);
    test_texture_upload(c);
    test_compressed_texture(c);
    test_rt_clear(c);
    test_depth_target(c);
    test_fence(c);

    // Tier B — programmable pipeline.
    if (backend == Backend::OpenGL) {
        // OpenGL: full pipeline via inline GLSL (slot-based binding, depth clear, compute).
        test_triangle(c);
        test_vertex_color(c);
        test_indexed(c);
        test_instanced(c);
        test_uniform_buffer(c);
        test_texture_sample(c);
        test_depth(c);
        test_blend(c);
        test_scissor(c);
        test_blit(c);
        test_msaa_resolve(c);
        test_compute(c);
        test_timestamp(c);
    } else if (backend == Backend::Vulkan) {
        // Vulkan: full pipeline via glslc-compiled SPIR-V. Resource binding goes
        // through descriptor sets (UBO / texture / storage buffer); raster + compute.
        test_triangle(c);
        test_vertex_color(c);
        test_indexed(c);
        test_instanced(c);
        test_uniform_buffer(c);
        test_texture_sample(c);
        test_depth(c);
        test_blend(c);
        test_scissor(c);
        test_blit(c);
        test_msaa_resolve(c);
        test_compute(c);
        test_timestamp(c);
    } else if (backend == Backend::D3D11) {
        // Direct3D 11: full raster pipeline via fxc-compiled DXBC + slot binding.
        test_triangle(c);
        test_vertex_color(c);
        test_indexed(c);
        test_instanced(c);
        test_uniform_buffer(c);
        test_texture_sample(c);
        test_depth(c);
        test_blend(c);
        test_scissor(c);
        test_blit(c);
        test_msaa_resolve(c);  // auto-skips: D3D11 backend doesn't wire MSAA sample counts (caps=0)
        test_compute(c);       // UAV (RWByteAddressBuffer) via CSSetUnorderedAccessViews
        test_timestamp(c);     // auto-skips: timestamp_query caps = 0 on this backend
    } else if (backend == Backend::D3D12) {
        // Direct3D 12: descriptor-free raster pipeline via DXBC (PSO + empty root signature).
        test_triangle(c);
        test_vertex_color(c);
        test_indexed(c);
        test_instanced(c);
        test_uniform_buffer(c);   // CBV via the shader-visible descriptor heap
        test_texture_sample(c);   // SRV + SAMPLER descriptor tables
        test_depth(c);
        test_blend(c);
        test_scissor(c);
        test_blit(c);
        test_msaa_resolve(c);  // auto-skips: D3D12 backend doesn't wire MSAA sample counts (caps=0)
        test_compute(c);          // UAV (RWByteAddressBuffer) via descriptor table
        test_timestamp(c);     // auto-skips: timestamp_query caps = 0 on this backend
    } else {
        skip("pipeline.tierB", "unhandled backend");
    }

    destroy_commander(c.cmd);
    destroy_device(c.dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("Graphics pipeline conformance test\n");

    struct { Backend b; const char* n; } backends[] = {
        { Backend::OpenGL, "OpenGL" },
        { Backend::Vulkan, "Vulkan" },
        { Backend::D3D11,  "Direct3D 11" },
        { Backend::D3D12,  "Direct3D 12" },
    };

    // Optional: a single backend by name on the command line.
    const char* only = (argc > 1) ? argv[1] : nullptr;
    for (auto& e : backends) {
        if (only && std::strstr(e.n, only) == nullptr && std::strcmp(only, e.n) != 0) {
            // crude case-insensitive-ish match on common names
            std::string a = only, b = e.n;
            for (auto& ch : a) ch = (char)tolower(ch);
            for (auto& ch : b) ch = (char)tolower(ch);
            if (b.find(a) == std::string::npos) continue;
        }
        run_backend(e.b, e.n);
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail;
}
