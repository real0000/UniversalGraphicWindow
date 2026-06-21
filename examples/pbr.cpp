/*
 * pbr.cpp - Physically-based rendering driven entirely by the Material system.
 *
 * There is no dedicated PBR C++ class: the surface is configured through
 * renderer/shaders/pbr.material (+ pbr.hlsl). This example loads that material per
 * backend, wires its bindings (Frame / Object / Material uniform buffers + the five
 * GpuMaterial textures + a sampler), draws a lit quad offscreen and reads the centre
 * pixel back to confirm the shader runs. Verifies Vulkan / D3D11 / D3D12 / OpenGL.
 */

#include "window.hpp"
#include "renderer/material.hpp"
#include "renderer/assets/mesh.hpp"   // MeshVertex layout the material expects

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

#ifndef WINDOW_PBR_SHADER_DIR
#define WINDOW_PBR_SHADER_DIR "renderer/shaders"
#endif

static int g_pass = 0, g_fail = 0;
static void check(const char* backend, const char* name, bool ok, const std::string& detail) {
    std::printf("    [%s] %-18s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

// --- per-backend uniform layouts (mirror the HLSL cbuffers in pbr.hlsl) ----------
struct FrameUBO {
    float view_proj[16];
    float cam_pos[3];     float _p0 = 0;
    float light_dir[3];   float _p1 = 0;
    float light_color[3]; float light_intensity = 1.0f;
    float ambient[3];     float _p2 = 0;
};
struct ObjectUBO  { float model[16]; float normal_mat[16]; };
struct MaterialUBO{ float base_color[4]; float emissive[4]; float metallic, roughness, occlusion, normal_scale; };

static void identity(float m[16]) { std::memset(m, 0, 16 * sizeof(float)); m[0]=m[5]=m[10]=m[15]=1.0f; }

static const int RT_W = 64, RT_H = 64;

static BufferHandle make_ubo(GraphicDevice* dev, const void* data, uint32_t size) {
    BufferDesc bd; bd.size = size; bd.type = BufferType::Uniform; bd.usage = ResourceUsage::Default; bd.initial_data = data;
    return dev->create_buffer(bd);
}
static TextureHandle make_tex(GraphicDevice* dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    const uint8_t px[4] = { r, g, b, a };
    TextureDesc td; td.width = 1; td.height = 1; td.format = TextureFormat::RGBA8_UNORM;
    td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_DST; td.initial_data = px;
    return dev->create_texture(td);
}

static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);
    Config config;
    config.backend = backend;
    config.windows[0].title = "pbr"; config.windows[0].width = 96; config.windows[0].height = 96;
    config.windows[0].visible = false;

    Result wr; auto windows = Window::create(config, &wr);
    if (wr != Result::Success || windows.empty()) { std::printf("    [SKIP] window/context: %s\n", result_to_string(wr)); return false; }
    Graphics* gfx = windows[0]->graphics();
    Result dr; GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { std::printf("    [SKIP] no device: %s\n", result_to_string(dr)); windows[0]->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { std::printf("    [SKIP] no commander\n"); destroy_device(dev); windows[0]->destroy(); return false; }

    // 1) Load the PBR surface purely from the .material config.
    const std::string dir = WINDOW_PBR_SHADER_DIR;
    Result mr; Material* mat = Material::create(dev, (dir + "/pbr.material").c_str(), dir.c_str(), &mr);
    check(name, "material.create", mat && mat->pipeline().valid(),
          mat ? "pbr.material -> valid pipeline" : std::string("Material::create failed: ") + result_to_string(mr));
    if (!mat) { destroy_device(dev); windows[0]->destroy(); return false; }

    // 2) A unit quad in the MeshVertex layout, facing +Z (toward the camera), CCW front.
    MeshVertex v[4] = {};
    const float P = 0.85f;
    auto set_v = [](MeshVertex& mv, float x, float y, float u, float w) {
        mv.position = math::Vec3(x, y, 0.0f); mv.normal = math::Vec3(0, 0, 1);
        mv.tangent = math::Vec4(1, 0, 0, 1); mv.uv0 = math::Vec2(u, w); mv.color = math::Vec4(1, 1, 1, 1);
    };
    set_v(v[0], -P, -P, 0, 0); set_v(v[1], P, -P, 1, 0); set_v(v[2], P, P, 1, 1); set_v(v[3], -P, P, 0, 1);
    // Double-sided (both windings) so the quad is visible regardless of a backend's
    // front-face / clip-space-Y convention (the surface normal still faces +Z).
    const uint32_t idx[12] = { 0, 1, 2, 0, 2, 3,   0, 2, 1, 0, 3, 2 };
    BufferDesc vbd; vbd.size = sizeof(v); vbd.type = BufferType::Vertex; vbd.stride = sizeof(MeshVertex); vbd.initial_data = v;
    BufferDesc ibd; ibd.size = sizeof(idx); ibd.type = BufferType::Index; ibd.initial_data = idx;
    BufferHandle vbo = dev->create_buffer(vbd), ibo = dev->create_buffer(ibd);

    // 3) Uniform buffers + fallback textures + sampler.
    FrameUBO fu{}; identity(fu.view_proj);
    fu.cam_pos[2] = 1.0f;                         // camera at +Z
    fu.light_dir[2] = -1.0f;                      // light travels -Z → hits the +Z face
    fu.light_color[0] = fu.light_color[1] = fu.light_color[2] = 1.0f; fu.light_intensity = 3.0f;
    fu.ambient[0] = fu.ambient[1] = fu.ambient[2] = 0.08f;
    ObjectUBO ou{}; identity(ou.model); identity(ou.normal_mat);
    MaterialUBO mu{}; mu.base_color[0] = 0.85f; mu.base_color[1] = 0.2f; mu.base_color[2] = 0.15f; mu.base_color[3] = 1.0f;
    mu.metallic = 0.0f; mu.roughness = 0.45f; mu.occlusion = 1.0f; mu.normal_scale = 1.0f;

    BufferHandle frame_ubo = make_ubo(dev, &fu, sizeof fu), object_ubo = make_ubo(dev, &ou, sizeof ou), mat_ubo = make_ubo(dev, &mu, sizeof mu);
    TextureHandle white = make_tex(dev, 255,255,255,255), normalT = make_tex(dev, 128,128,255,255),
                  mrT = make_tex(dev, 255,255,0,255), black = make_tex(dev, 0,0,0,255);
    SamplerState ss; SamplerHandle samp = dev->create_sampler(ss);

    mat->set_uniform_buffer("Frame",  frame_ubo,  0, sizeof(FrameUBO));
    mat->set_uniform_buffer("Object", object_ubo, 0, sizeof(ObjectUBO));
    mat->set_uniform_buffer("Material", mat_ubo,  0, sizeof(MaterialUBO));
    mat->set_texture("uBaseColorTex", white);   mat->set_texture("uNormalTex", normalT);
    mat->set_texture("uMetalRoughTex", mrT);     mat->set_texture("uEmissiveTex", black);
    mat->set_texture("uOcclusionTex", white);    mat->set_sampler("uSamp", samp);

    // 4) Render the quad to an RGBA8 target (+ depth) and read the centre pixel back.
    RenderTargetDesc rd; rd.width = RT_W; rd.height = RT_H; rd.format = TextureFormat::RGBA8_UNORM;
    DepthStencilDesc dd; dd.width = RT_W; dd.height = RT_H; dd.format = TextureFormat::D24_UNORM_S8_UINT;
    RenderTargetHandle rt = dev->create_render_target(rd);
    RenderTargetHandle depth = dev->create_depth_target(dd);

    cmd->begin();
    cmd->set_render_targets(&rt, 1, depth);
    Viewport vp; vp.x = 0; vp.y = 0; vp.width = RT_W; vp.height = RT_H; cmd->set_viewport(vp);
    ScissorRect full{ 0, 0, RT_W, RT_H }; cmd->set_scissor(full);
    cmd->clear_color(ClearColor(0.0f, 0.0f, 0.0f, 1.0f));
    cmd->clear_depth_stencil(ClearDepthStencil{ 1.0f, 0 });
    mat->bind(cmd);                              // set_pipeline + bind the material's resources
    cmd->bind_vertex_buffer(0, vbo, 0);
    cmd->bind_index_buffer(ibo, IndexFormat::UInt32, 0);
    cmd->draw_indexed(12, 0, 0);
    cmd->end();
    submit_commander(gfx, cmd);
    dev->wait_idle();

    std::vector<uint8_t> px(size_t(RT_W) * RT_H * 4, 0);
    TextureRegion rr; rr.x = 0; rr.y = 0; rr.width = RT_W; rr.height = RT_H;
    dev->read_texture(dev->render_target_texture(rt), rr, px.data());
    const uint8_t* c = &px[(size_t(RT_H/2) * RT_W + RT_W/2) * 4];
    char buf[64]; std::snprintf(buf, sizeof buf, "centre=(%d,%d,%d,%d)", c[0], c[1], c[2], c[3]);
    // Lit red quad: noticeably red, brighter than the ambient-only floor, not the black clear.
    const bool lit = c[0] > 40 && c[0] > c[1] && c[0] > c[2];
    check(name, "material.render", lit, std::string(lit ? "lit quad drawn " : "quad not lit/visible ") + buf);

    dev->destroy_render_target(rt); dev->destroy_render_target(depth);
    dev->destroy_sampler(samp);
    for (TextureHandle t : { white, normalT, mrT, black }) dev->destroy_texture(t);
    for (BufferHandle b : { frame_ubo, object_ubo, mat_ubo, vbo, ibo }) dev->destroy_buffer(b);
    mat->destroy();
    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main() {
    std::printf("=== PBR via Material settings (no dedicated renderer class) ===\n");
    run_backend(Backend::OpenGL, "OpenGL");
    run_backend(Backend::Vulkan, "Vulkan");
    run_backend(Backend::D3D11,  "Direct3D 11");
    run_backend(Backend::D3D12,  "Direct3D 12");
    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
