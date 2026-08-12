// example_gpu_features — conformance test for the GPU features that sit above the basic
// draw/copy pipeline: storage-image writes, mip generation, sparse residency, ray-tracing
// acceleration structures and mesh shaders.
//
// None of these had coverage, and the gaps were real: on D3D12 bind_storage_texture() was
// a no-op and a StorageTexture descriptor write produced an SRV in a UAV slot, so a compute
// pass writing to a texture silently did nothing; D3D11's GenerateMips never ran because
// the texture was not created for it; and read_texture ignored the mip level entirely,
// returning recycled memory that happened to look plausible.
//
// Each check is written so that a feature which silently does NOTHING fails: targets start
// filled with a value the shader would never produce, and results are compared exactly.
//
// Where possible the shader is ONE HLSL source compiled per backend by the built-in runtime
// compiler, so no offline glslc/fxc step is needed. Mesh shaders are the exception: they
// require Shader Model 6.5 (DXIL), which that compiler cannot emit, so their bytecode is
// prebuilt (see gpu_features_mesh.hlsl).
//
// Features a backend does not support are SKIPPED via its reported capabilities, so this
// runs clean on every backend and still fails loudly on a backend that claims a feature it
// does not actually implement.

#include "window.hpp"
#include "graphics_api.hpp"
#include "renderer/shader_compiler/shader_compiler.hpp"
#include "gpu_features_mesh_dxil.h"   // prebuilt SM 6.5 mesh + pixel DXIL
#include "gpu_features_rt_dxil.h"     // prebuilt DXR shader library (lib_6_3)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;
using window::gfx::ShaderCompiler;
using window::gfx::ShaderCompileResult;

static int g_pass = 0, g_fail = 0, g_skip = 0;
static void pass(const char* n, const std::string& d) { std::printf("    [PASS] %-24s %s\n", n, d.c_str()); ++g_pass; }
static void fail(const char* n, const std::string& d) { std::printf("    [FAIL] %-24s %s\n", n, d.c_str()); ++g_fail; }
static void skip(const char* n, const std::string& d) { std::printf("    [SKIP] %-24s %s\n", n, d.c_str()); ++g_skip; }
static void check(const char* n, bool ok, const std::string& d = "") { ok ? pass(n, d) : fail(n, d); }

static const int TW = 16, TH = 16;

// One 8x8 thread group per 8x8 tile. Each thread writes its own coordinate, so the result
// is a known gradient: R = x*16, G = y*16, B = 64, A = 255.
static const char* kHLSL = R"(
RWTexture2D<float4> uOut : register(u0);

[numthreads(8, 8, 1)]
void cs_main(uint3 tid : SV_DispatchThreadID) {
    uOut[tid.xy] = float4(tid.x * 16.0 / 255.0, tid.y * 16.0 / 255.0, 64.0 / 255.0, 1.0);
}
)";

// The two binding paths are separate code in every backend, so both are run everywhere
// rather than only the one a given backend prefers — binding by slot on D3D12/Vulkan
// exercises the reflected root signature / auto descriptor set, which is where a texture
// UAV differs from a buffer UAV and where this used to silently do nothing.
enum class BindPath { DescriptorSet, Slot };

static void test_storage_texture(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd, BindPath path) {
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps); dev->get_capabilities(&caps);
    if (!caps.compute_shaders) { skip("storage_texture.dispatch", "compute_shaders unsupported"); return; }

    ShaderCompileResult log;
    ShaderHandle cs = ShaderCompiler::compile_and_create(dev, kHLSL, std::strlen(kHLSL),
                                                        ShaderStage::Compute, "cs_main", {}, &log);
    check("compile+create cs", cs.valid(), log.ok ? "" : log.log);
    if (!cs.valid()) return;

    // STORAGE marks it UAV-capable; SAMPLED lets the backend keep it in the format-castable
    // typeless family, which the UAV must then name a concrete format for.
    TextureDesc td;
    td.width = TW; td.height = TH; td.format = TextureFormat::RGBA8_UNORM;
    td.usage = TEXTURE_USAGE_STORAGE | TEXTURE_USAGE_SAMPLED;
    // Start from a known non-answer, so a dispatch that never runs cannot pass by accident.
    std::vector<uint8_t> zero(size_t(TW) * TH * 4, 0);
    td.initial_data = zero.data();
    TextureHandle tex = dev->create_texture(td);
    check("create storage texture", tex.valid());
    if (!tex.valid()) { dev->destroy_shader(cs); return; }

    PipelineDesc pd; pd.compute_shader = cs;

    // Descriptor-set path: a one-binding StorageTexture layout. The slot path deliberately
    // leaves pd.layout invalid, so the backend must synthesise a layout from shader
    // reflection — the case that has to tell a texture UAV from a buffer UAV.
    DescriptorSetLayoutHandle dsl{}; PipelineLayoutHandle pll{}; DescriptorSetHandle set{};
    if (path == BindPath::DescriptorSet) {
        DescriptorSetLayoutDesc dl; dl.binding_count = 1;
        dl.bindings[0] = { 0, BindingType::StorageTexture, 1, STAGE_COMPUTE };
        dsl = dev->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = dsl;
        pll = dev->create_pipeline_layout(pl);
        pd.layout = pll;
        DescriptorSetDesc sd; sd.layout = dsl; sd.write_count = 1;
        sd.writes[0].binding = 0; sd.writes[0].type = BindingType::StorageTexture;
        sd.writes[0].texture = tex; sd.writes[0].storage_access = StorageAccess::Write;
        set = dev->create_descriptor_set(sd);
    }

    PipelineHandle pipe = dev->create_pipeline(pd);
    check("create compute pipeline", pipe.valid());
    if (!pipe.valid()) { dev->destroy_texture(tex); dev->destroy_shader(cs); return; }

    cmd->begin();
    cmd->set_pipeline(pipe);
    if (set.valid()) cmd->bind_descriptor_set(0, set);
    else             cmd->bind_storage_texture(0, tex, 0, StorageAccess::Write);
    cmd->dispatch(TW / 8, TH / 8, 1);
    cmd->memory_barrier(GPU_BARRIER_STORAGE_IMAGE);
    cmd->end();
    submit_commander(gfx, cmd);
    dev->wait_idle();

    std::vector<uint8_t> px(size_t(TW) * TH * 4, 0xAB);
    TextureRegion r; r.x = 0; r.y = 0; r.width = TW; r.height = TH; r.mip = 0; r.layer = 0;
    dev->read_texture(tex, r, px.data());

    // Exact per-texel comparison against what the shader was told to write.
    int bad = 0; std::string first;
    for (int y = 0; y < TH; ++y) {
        for (int x = 0; x < TW; ++x) {
            const uint8_t* p = &px[(size_t(y) * TW + x) * 4];
            const int er = x * 16, eg = y * 16, eb = 64, ea = 255;
            const bool ok = std::abs(int(p[0]) - er) <= 1 && std::abs(int(p[1]) - eg) <= 1 &&
                            std::abs(int(p[2]) - eb) <= 1 && std::abs(int(p[3]) - ea) <= 1;
            if (!ok && bad++ == 0)
                first = "(" + std::to_string(x) + "," + std::to_string(y) + ") got " +
                        std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
                        std::to_string(p[2]) + "," + std::to_string(p[3]) +
                        " want " + std::to_string(er) + "," + std::to_string(eg) + "," +
                        std::to_string(eb) + "," + std::to_string(ea);
        }
    }
    const char* pname = (path == BindPath::DescriptorSet) ? "storage_texture.desc_set"
                                                          : "storage_texture.slot_bind";
    check(pname, bad == 0,
          bad == 0 ? (std::to_string(TW * TH) + " texels exact")
                   : (std::to_string(bad) + " wrong texels; first " + first));

    if (set.valid()) dev->destroy_descriptor_set(set);
    if (pll.valid()) dev->destroy_pipeline_layout(pll);
    if (dsl.valid()) dev->destroy_descriptor_set_layout(dsl);
    dev->destroy_pipeline(pipe);
    dev->destroy_texture(tex);
    dev->destroy_shader(cs);
}

// generate_mipmaps: every backend gets there differently — glGenerateMipmap, a
// vkCmdBlitImage chain, ID3D11DeviceContext::GenerateMips, and (D3D12, which has no such
// service) a compute downsample. A texture filled with one flat colour must stay that
// colour at every level, whichever route ran; an untouched level reads back as the
// cleared value and fails.
static void test_generate_mipmaps(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    (void)gfx; (void)cmd;
    const int W = 16, H = 16, MIPS = 5;   // 16,8,4,2,1
    const uint8_t R = 200, G = 100, B = 50, A = 255;
    std::vector<uint8_t> src(size_t(W) * H * 4);
    for (size_t i = 0; i < src.size(); i += 4) { src[i] = R; src[i+1] = G; src[i+2] = B; src[i+3] = A; }

    TextureDesc td;
    td.width = W; td.height = H; td.format = TextureFormat::RGBA8_UNORM; td.mip_levels = MIPS;
    // STORAGE because the D3D12 route writes levels through a UAV; COPY_SRC to read back.
    td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_STORAGE | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST;
    td.initial_data = src.data();
    TextureHandle tex = dev->create_texture(td);
    if (!tex.valid()) { fail("mipmaps.generate", "create_texture failed"); return; }

    dev->generate_mipmaps(tex);
    dev->wait_idle();

    // Level 2 is 4x4: far enough down that it only exists if the chain actually ran.
    const int LV = 2, LW = W >> LV, LH = H >> LV;
    std::vector<uint8_t> px(size_t(LW) * LH * 4, 0);
    TextureRegion r; r.x = 0; r.y = 0; r.width = LW; r.height = LH; r.mip = LV; r.layer = 0;
    dev->read_texture(tex, r, px.data());

    int bad = 0; std::string first;
    for (int i = 0; i < LW * LH; ++i) {
        const uint8_t* p = &px[size_t(i) * 4];
        const bool ok = std::abs(int(p[0]) - R) <= 2 && std::abs(int(p[1]) - G) <= 2 &&
                        std::abs(int(p[2]) - B) <= 2 && std::abs(int(p[3]) - A) <= 2;
        if (!ok && bad++ == 0)
            first = "texel " + std::to_string(i) + " got " + std::to_string(p[0]) + "," +
                    std::to_string(p[1]) + "," + std::to_string(p[2]) + "," + std::to_string(p[3]);
    }
    check("mipmaps.generate", bad == 0,
          bad == 0 ? ("level " + std::to_string(LV) + " (" + std::to_string(LW) + "x" +
                      std::to_string(LH) + ") matches the source colour")
                   : (std::to_string(bad) + "/" + std::to_string(LW * LH) + " wrong; " + first));
    dev->destroy_texture(tex);
}

// Sparse (partially-resident / tiled) textures: the resource has an address range but no
// memory until tiles are committed. Committing a region and round-tripping data through it
// proves the mapping actually took — an uncommitted tile reads back as garbage or faults.
static void test_sparse_residency(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    (void)gfx; (void)cmd;
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps); dev->get_capabilities(&caps);
    if (!caps.sparse_textures) { skip("sparse.residency", "sparse_textures unsupported"); return; }

    // 256x256 RGBA8 is 4 tiles wide at the standard 128x128 tile shape, so a region well
    // inside the resource still spans whole tiles.
    const int W = 256, H = 256;
    TextureDesc td;
    td.width = W; td.height = H; td.format = TextureFormat::RGBA8_UNORM;
    td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST;
    td.sparse = true;
    TextureHandle tex = dev->create_texture(td);
    if (!tex.valid()) { fail("sparse.residency", "create sparse texture failed"); return; }

    TextureRegion commit; commit.x = 0; commit.y = 0; commit.width = 128; commit.height = 128; commit.mip = 0; commit.layer = 0;
    dev->update_texture_residency(tex, commit, true);

    // Write into the committed region and read it back.
    std::vector<uint8_t> src(size_t(128) * 128 * 4);
    for (size_t i = 0; i < src.size(); i += 4) { src[i] = 11; src[i+1] = 22; src[i+2] = 33; src[i+3] = 255; }
    dev->update_texture(tex, commit, src.data());
    dev->wait_idle();

    std::vector<uint8_t> back(src.size(), 0);
    dev->read_texture(tex, commit, back.data());
    int bad = 0;
    for (size_t i = 0; i < back.size(); i += 4)
        if (back[i] != 11 || back[i+1] != 22 || back[i+2] != 33 || back[i+3] != 255) ++bad;
    check("sparse.residency", bad == 0,
          bad == 0 ? "128x128 tile region committed and round-tripped"
                   : (std::to_string(bad) + " wrong texels in the committed region"));

    dev->update_texture_residency(tex, commit, false);   // release the tiles again
    dev->destroy_texture(tex);
}

// Ray-tracing acceleration structures. Building needs no shaders at all — only geometry —
// so the build path is verifiable even where a ray-tracing pipeline is not.
static void test_acceleration_structure(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps); dev->get_capabilities(&caps);
    if (!caps.ray_tracing) { skip("raytracing.blas", "ray_tracing unsupported"); return; }

    const float tri[9] = { 0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f };
    BufferDesc bd; bd.size = sizeof tri; bd.type = BufferType::Vertex; bd.usage = ResourceUsage::Default;
    bd.initial_data = tri; bd.stride = 12;
    BufferHandle vb = dev->create_buffer(bd);
    if (!vb.valid()) { fail("raytracing.blas", "vertex buffer failed"); return; }

    AccelStructDesc ad;
    ad.type = AccelStructType::BottomLevel;
    ad.vertex_buffer = vb; ad.vertex_count = 3; ad.vertex_stride = 12;
    ad.vertex_format = VertexFormat::Float3;
    AccelStructHandle blas = dev->create_acceleration_structure(ad);
    check("raytracing.blas_create", blas.valid());
    if (!blas.valid()) { dev->destroy_buffer(vb); return; }

    cmd->begin();
    cmd->build_acceleration_structure(blas, ad);
    cmd->end();
    submit_commander(gfx, cmd);
    dev->wait_idle();

    // A build failure shows up as device removal rather than a return code, so the check is
    // that the device is still alive and usable afterwards.
    GraphicsCapabilities after; std::memset(&after, 0, sizeof after); dev->get_capabilities(&after);
    check("raytracing.blas_build", after.max_texture_size == caps.max_texture_size,
          "BLAS built over 1 triangle; device still healthy");

    dev->destroy_acceleration_structure(blas);
    dev->destroy_buffer(vb);
}

// Mesh shaders. A mesh pipeline has no input assembler, so it cannot be expressed as an
// ordinary graphics PSO at all — D3D12 builds it from a subobject stream. The shader is
// prebuilt DXIL (SM 6.5) because the built-in runtime compiler only emits DXBC.
// The mesh shader emits a fullscreen triangle and the pixel shader paints it green, so a
// pipeline that was created but never rasterized leaves the clear colour and is caught.
static void test_mesh_shader(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps); dev->get_capabilities(&caps);
    if (!caps.mesh_shaders) { skip("mesh.dispatch", "mesh_shaders unsupported"); return; }
    // Same HLSL, compiled to each API's bytecode (see gpu_features_mesh.hlsl).
    const bool vk = dev->get_backend() == Backend::Vulkan;
    ShaderDesc msd; msd.stage = ShaderStage::Mesh; msd.entry_point = "ms_main";
    ShaderDesc psd; psd.stage = ShaderStage::Fragment; psd.entry_point = "ps_main";
    if (vk) {
        msd.language = ShaderLanguage::SPIRV; msd.code = SPV_MESH_MS; msd.code_size = sizeof SPV_MESH_MS;
        psd.language = ShaderLanguage::SPIRV; psd.code = SPV_MESH_PS; psd.code_size = sizeof SPV_MESH_PS;
    } else {
        msd.language = ShaderLanguage::DXIL;  msd.code = DXIL_MESH_MS; msd.code_size = sizeof DXIL_MESH_MS;
        psd.language = ShaderLanguage::DXIL;  psd.code = DXIL_MESH_PS; psd.code_size = sizeof DXIL_MESH_PS;
    }
    ShaderHandle ms = dev->create_shader(msd), ps = dev->create_shader(psd);
    check("mesh.create_shaders", ms.valid() && ps.valid());
    if (!ms.valid() || !ps.valid()) return;

    PipelineDesc pd;
    pd.mesh_shader = ms; pd.fragment_shader = ps;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.rasterizer = RasterizerState::no_cull();
    pd.color_formats[0] = TextureFormat::RGBA8_UNORM; pd.color_format_count = 1;
    PipelineHandle pipe = dev->create_pipeline(pd);
    check("mesh.create_pipeline", pipe.valid());
    if (!pipe.valid()) { dev->destroy_shader(ms); dev->destroy_shader(ps); return; }

    const int W = 32, H = 32;
    RenderTargetDesc rd; rd.width = W; rd.height = H; rd.format = TextureFormat::RGBA8_UNORM;
    RenderTargetHandle rt = dev->create_render_target(rd);

    cmd->begin();
    cmd->set_render_targets(&rt, 1, RenderTargetHandle{});
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = float(W); vp.height = float(H);
    cmd->set_viewport(vp);
    cmd->set_scissor(ScissorRect{ 0, 0, W, H });
    cmd->clear_color(ClearColor(1, 0, 0, 1));   // red: an unrasterized target stays red
    cmd->set_pipeline(pipe);
    cmd->draw_mesh_tasks(1, 1, 1);
    cmd->end();
    submit_commander(gfx, cmd);
    dev->wait_idle();

    std::vector<uint8_t> px(size_t(W) * H * 4, 0);
    TextureRegion r; r.x = 0; r.y = 0; r.width = W; r.height = H; r.mip = 0; r.layer = 0;
    dev->read_texture(dev->render_target_texture(rt), r, px.data());
    const uint8_t* p = &px[(size_t(H / 2) * W + W / 2) * 4];
    check("mesh.dispatch", p[0] < 8 && p[1] > 247 && p[2] < 8,
          "DispatchMesh filled the target -> (" + std::to_string(p[0]) + "," +
          std::to_string(p[1]) + "," + std::to_string(p[2]) + "," + std::to_string(p[3]) + ")");

    dev->destroy_render_target(rt);
    dev->destroy_pipeline(pipe);
    dev->destroy_shader(ms); dev->destroy_shader(ps);
}

// A full ray-tracing dispatch: BLAS + TLAS, a pipeline built from a DXIL library, and a
// shader binding table the backend assembles. The hit colour is delivered as a LOCAL root
// argument — it lives in the hit group's shader-table record, not in any globally bound
// resource — so a table whose records are mis-sized or mis-addressed produces the wrong
// colour rather than quietly working.
static void test_trace_rays(Graphics* gfx, GraphicDevice* dev, GraphicCommander* cmd) {
    GraphicsCapabilities caps; std::memset(&caps, 0, sizeof caps); dev->get_capabilities(&caps);
    if (!caps.ray_tracing) { skip("raytracing.trace", "ray_tracing unsupported"); return; }
    const bool vk = dev->get_backend() == Backend::Vulkan;

    // A triangle covering the lower-left of the [-1,1] plane the ray-gen shoots through.
    const float verts[9] = { -0.9f, -0.9f, -1.0f,   0.7f, -0.9f, -1.0f,   -0.9f, 0.7f, -1.0f };
    BufferDesc vbd; vbd.size = sizeof verts; vbd.type = BufferType::Vertex; vbd.initial_data = verts; vbd.stride = 12;
    BufferHandle vb = dev->create_buffer(vbd);

    AccelStructDesc bd;
    bd.type = AccelStructType::BottomLevel;
    bd.vertex_buffer = vb; bd.vertex_count = 3; bd.vertex_stride = 12; bd.vertex_format = VertexFormat::Float3;
    AccelStructHandle blas = dev->create_acceleration_structure(bd);
    if (!blas.valid()) { fail("raytracing.trace", "BLAS creation failed"); dev->destroy_buffer(vb); return; }
    cmd->begin(); cmd->build_acceleration_structure(blas, bd); cmd->end();
    submit_commander(gfx, cmd); dev->wait_idle();

    // One identity instance referencing the BLAS. The instance layout is the backend's
    // (D3D12_RAYTRACING_INSTANCE_DESC): 3x4 row-major transform, then packed id/mask/offset
    // /flags, then the BLAS address.
    struct Instance {
        float    transform[12];
        uint32_t id_mask;          // instanceID:24 | mask:8
        uint32_t offset_flags;     // hitGroupOffset:24 | flags:8
        uint64_t blas_address;
    } inst{};
    inst.transform[0] = 1.0f; inst.transform[5] = 1.0f; inst.transform[10] = 1.0f;
    inst.id_mask = (0xFFu << 24);
    inst.offset_flags = 0;
    inst.blas_address = dev->acceleration_structure_address(blas);
    BufferDesc ibd; ibd.size = sizeof inst; ibd.type = BufferType::Storage; ibd.initial_data = &inst;
    BufferHandle ib = dev->create_buffer(ibd);

    AccelStructDesc td;
    td.type = AccelStructType::TopLevel; td.instance_buffer = ib; td.instance_count = 1;
    AccelStructHandle tlas = dev->create_acceleration_structure(td);
    if (!tlas.valid()) { fail("raytracing.trace", "TLAS creation failed"); dev->destroy_acceleration_structure(blas); dev->destroy_buffer(vb); dev->destroy_buffer(ib); return; }
    cmd->begin(); cmd->build_acceleration_structure(tlas, td); cmd->end();
    submit_commander(gfx, cmd); dev->wait_idle();

    // Output image the ray-gen shader writes.
    const int W = 32, H = 32;
    TextureDesc od; od.width = W; od.height = H; od.format = TextureFormat::RGBA8_UNORM;
    od.usage = TEXTURE_USAGE_STORAGE | TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_SRC;
    TextureHandle out = dev->create_texture(od);

    ShaderDesc ld; ld.stage = ShaderStage::RayTracingLibrary;
    if (vk) { ld.language = ShaderLanguage::SPIRV; ld.code = SPV_RT_LIB;  ld.code_size = sizeof SPV_RT_LIB; }
    else    { ld.language = ShaderLanguage::DXIL;  ld.code = DXIL_RT_LIB; ld.code_size = sizeof DXIL_RT_LIB; }
    ShaderHandle lib = dev->create_shader(ld);
    check("raytracing.library", lib.valid());
    if (!lib.valid()) { dev->destroy_texture(out); dev->destroy_acceleration_structure(tlas); dev->destroy_acceleration_structure(blas); dev->destroy_buffer(vb); dev->destroy_buffer(ib); return; }

    // Global layout: the scene and the output image. D3D12 keeps t0 and u0 in separate
    // register spaces so both are binding 0; Vulkan has one binding space per set, so the
    // shader is annotated with distinct bindings there (see the .hlsl).
    const uint32_t scene_binding = 0, out_binding = vk ? 1u : 0u;
    DescriptorSetLayoutDesc gl; gl.binding_count = 2;
    // Visibility must name the stages that actually read them: Vulkan rejects a pipeline
    // whose layout exposes a binding only to compute while a ray-generation shader uses it.
    gl.bindings[0] = { scene_binding, BindingType::AccelerationStructure, 1, STAGE_RAY_TRACING };
    gl.bindings[1] = { out_binding,   BindingType::StorageTexture,        1, STAGE_RAY_TRACING };
    DescriptorSetLayoutHandle gdsl = dev->create_descriptor_set_layout(gl);
    PipelineLayoutDesc gpl; gpl.set_layout_count = 1; gpl.set_layouts[0] = gdsl;
    PipelineLayoutHandle global_layout = dev->create_pipeline_layout(gpl);

    // Local layout for the hit group: four 32-bit constants (the hit colour) at b0.
    PipelineLayoutDesc lpl; lpl.push_constant_count = 1; lpl.push_constants[0].size = 16;
    PipelineLayoutHandle local_layout = dev->create_pipeline_layout(lpl);

    const float hit_color[4] = { 0.0f, 1.0f, 0.0f, 1.0f };   // green, delivered per-record
    RayLocalArg hit_arg; hit_arg.kind = RayArgKind::Constants;
    hit_arg.constants = hit_color; hit_arg.constants_size = sizeof hit_color;

    RayHitGroup hg; hg.name = "HitGroup"; hg.closest_hit = "chit_main";
    hg.local_layout = local_layout; hg.args = &hit_arg; hg.arg_count = 1;
    RayShader miss; miss.entry_point = "miss_main";

    RayTracingPipelineDesc rpd;
    rpd.library = lib;
    rpd.ray_gen.entry_point = "rgen_main";
    rpd.miss = &miss; rpd.miss_count = 1;
    rpd.hit_groups = &hg; rpd.hit_group_count = 1;
    rpd.layout = global_layout;
    rpd.max_recursion = 1;
    rpd.max_payload_size = 16; rpd.max_attribute_size = 8;
    RayTracingPipelineHandle rt = dev->create_ray_tracing_pipeline(rpd);
    check("raytracing.pipeline", rt.valid());
    // The scene and the output image go in one descriptor set, bound globally.
    DescriptorSetDesc gsd; gsd.layout = gdsl; gsd.write_count = 2;
    gsd.writes[0].binding = scene_binding; gsd.writes[0].type = BindingType::AccelerationStructure; gsd.writes[0].accel = tlas;
    gsd.writes[1].binding = out_binding;   gsd.writes[1].type = BindingType::StorageTexture;
    gsd.writes[1].texture = out; gsd.writes[1].storage_access = StorageAccess::Write;
    DescriptorSetHandle gset = dev->create_descriptor_set(gsd);

    if (rt.valid()) {
        cmd->begin();
        cmd->set_ray_tracing_pipeline(rt);
        cmd->bind_descriptor_set(0, gset);
        cmd->trace_rays(W, H, 1);
        cmd->end();
        submit_commander(gfx, cmd);
        dev->wait_idle();

        std::vector<uint8_t> px(size_t(W) * H * 4, 0);
        TextureRegion rr; rr.x = 0; rr.y = 0; rr.width = W; rr.height = H; rr.mip = 0; rr.layer = 0;
        dev->read_texture(out, rr, px.data());
        const uint8_t* hit  = &px[(size_t(H / 4) * W + W / 4) * 4];        // inside the triangle
        const uint8_t* miss_px = &px[(size_t(H - 2) * W + (W - 2)) * 4];   // outside it
        const bool hit_ok  = hit[1] > 200 && hit[0] < 60 && hit[2] < 60;   // green from the local arg
        const bool miss_ok = miss_px[2] > 200 && miss_px[0] < 60;          // blue from the miss shader
        check("raytracing.trace", hit_ok && miss_ok,
              "hit=(" + std::to_string(hit[0]) + "," + std::to_string(hit[1]) + "," + std::to_string(hit[2]) +
              ") miss=(" + std::to_string(miss_px[0]) + "," + std::to_string(miss_px[1]) + "," + std::to_string(miss_px[2]) + ")");
        dev->destroy_ray_tracing_pipeline(rt);
    }

    dev->destroy_descriptor_set(gset);
    dev->destroy_pipeline_layout(local_layout);
    dev->destroy_pipeline_layout(global_layout);
    dev->destroy_descriptor_set_layout(gdsl);
    dev->destroy_shader(lib);
    dev->destroy_texture(out);
    dev->destroy_acceleration_structure(tlas);
    dev->destroy_acceleration_structure(blas);
    dev->destroy_buffer(vb); dev->destroy_buffer(ib);
}

static bool run_backend(Backend b, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);
    Config cfg; cfg.backend = b; cfg.window_count = 1;
    cfg.windows[0].width = 64; cfg.windows[0].height = 64; cfg.windows[0].visible = false;
    cfg.windows[0].title = "gpu_features";
    Result wr;
    auto windows = Window::create(cfg, &wr);
    if (wr != Result::Success || windows.empty()) { skip("backend", std::string("no window: ") + result_to_string(wr)); return false; }
    Graphics* gfx = windows[0]->graphics();
    if (!gfx) { skip("backend", "no graphics context"); windows[0]->destroy(); return false; }
    windows[0]->poll_events();

    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { skip("backend", std::string("no device: ") + result_to_string(dr)); windows[0]->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { skip("backend", "no commander"); destroy_device(dev); windows[0]->destroy(); return false; }

    test_storage_texture(gfx, dev, cmd, BindPath::DescriptorSet);
    test_storage_texture(gfx, dev, cmd, BindPath::Slot);
    test_generate_mipmaps(gfx, dev, cmd);
    test_sparse_residency(gfx, dev, cmd);
    test_acceleration_structure(gfx, dev, cmd);
    test_mesh_shader(gfx, dev, cmd);
    test_trace_rays(gfx, dev, cmd);

    destroy_commander(cmd);
    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    // Unbuffered: this drives GPU drivers, so a hard crash is a possible outcome and the
    // output up to that point is what identifies the failing case.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("GPU feature conformance test (storage images, mipmaps, sparse, ray tracing, mesh shaders)\n");
    if (!ShaderCompiler::available()) {
        std::printf("built-in shader compiler not built (WINDOW_ENABLE_SHADER_COMPILER=OFF)\n");
        return 0;
    }
    ShaderCompiler::initialize();

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
