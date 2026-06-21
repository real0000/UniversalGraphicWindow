// example_video - GStreamer video playback demo.
//
// Opens a video (local path or URI) with window::video::VideoPlayer, then each
// frame pulls the decoded GPU texture (zero-copy when the backend supports it,
// CPU-upload fallback otherwise) and blits it to the swapchain backbuffer with a
// textured fullscreen triangle. Audio plays through UGW's own audio backend.
//
//   example_video [path-or-uri] [backend]
//     path-or-uri : a video file or URI. If omitted, a GStreamer videotestsrc
//                   pattern is used so the demo runs with no media file.
//     backend     : opengl | vulkan | d3d11 | d3d12   (default: Auto)
//
// Built only when CMake enabled the video renderer (WINDOW_SUPPORT_VIDEO); the
// renderer (WINDOW_SUPPORT_RENDERER) supplies the shader compiler used here.

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/video/video_renderer.hpp"
#include "../renderer/shader_compiler/shader_compiler.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace window;

// Textured fullscreen-triangle shader (one HLSL source, two entry points). The
// vertex shader generates a full-screen triangle from the vertex id, so no vertex
// buffer is needed; the pixel shader samples the video texture at t0/s0.
static const char* kHLSL = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut vs_quad(uint vid : SV_VertexID) {
    VSOut o;
    float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0),(2,0),(0,2)
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
Texture2D    uTex : register(t0);
SamplerState uSmp : register(s0);
float4 ps_tex(VSOut i) : SV_Target { return uTex.Sample(uSmp, i.uv); }
)";

static Backend pick_backend(const char* s) {
    if (!s) return Backend::Auto;
    std::string a = s; for (auto& c : a) c = (char)tolower(c);
    if (a.find("11") != std::string::npos) return Backend::D3D11;
    if (a.find("12") != std::string::npos) return Backend::D3D12;
    if (a.find("vk") != std::string::npos || a.find("vulkan") != std::string::npos) return Backend::Vulkan;
    if (a.find("gl") != std::string::npos) return Backend::OpenGL;
    return Backend::Auto;
}

// Default swapchain colour format per backend (D3D presents BGRA8 by default).
static TextureFormat backbuffer_format(Backend b) {
    return (b == Backend::D3D11 || b == Backend::D3D12) ? TextureFormat::BGRA8_UNORM
                                                        : TextureFormat::RGBA8_UNORM;
}

int main(int argc, char** argv) {
    if (!video::video_is_supported()) {
        std::printf("video support not compiled in (build with -DWINDOW_ENABLE_VIDEO=ON)\n");
        return 1;
    }

    const char* source  = (argc > 1) ? argv[1] : "videotestsrc://";   // playbin test pattern
    Backend     backend = pick_backend(argc > 2 ? argv[2] : nullptr);

    Config c;
    c.backend = backend;
    c.windows[0].title  = "UGW Video Player";
    c.windows[0].width  = 1280; c.windows[0].height = 720;
    c.windows[0].visible = true;

    Result wr;
    auto ws = Window::create(c, &wr);
    if (wr != Result::Success || ws.empty()) { std::printf("window create failed: %s\n", result_to_string(wr)); return 1; }
    Window*   win = ws[0];
    Graphics* gfx = win->graphics();

    Result dr;
    GraphicDevice*    dev = create_device(gfx, &dr);
    if (!dev) { std::printf("no device: %s\n", result_to_string(dr)); win->destroy(); return 1; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { std::printf("no commander\n"); destroy_device(dev); win->destroy(); return 1; }
    const Backend actual = dev->get_backend();

    // ---- open the video ----------------------------------------------------
    video::VideoPlayerDesc vd;
    vd.device = dev;
    vd.enable_audio = true;
    vd.loop = true;
    vd.prefer_zero_copy = true;
    video::VideoResult vr;
    video::VideoPlayer* player = video::VideoPlayer::open(source, vd, &vr);
    if (!player) { std::printf("video open failed: %s\n", video::video_result_to_string(vr));
                   destroy_commander(cmd); destroy_device(dev); win->destroy(); return 1; }
    player->play();
    std::printf("playing '%s' on %s (duration %.1fs)\n", source, backend_to_string(actual), player->duration());

    // ---- blit pipeline -----------------------------------------------------
    ShaderHandle vs = gfx::ShaderCompiler::compile_and_create_cached(dev, kHLSL, std::strlen(kHLSL), ShaderStage::Vertex,   "vs_quad");
    ShaderHandle ps = gfx::ShaderCompiler::compile_and_create_cached(dev, kHLSL, std::strlen(kHLSL), ShaderStage::Fragment, "ps_tex");
    if (!vs.valid() || !ps.valid()) { std::printf("shader compile failed\n"); }

    SamplerHandle samp = dev->create_sampler(SamplerState::linear_clamp());

    const bool use_desc = (actual == Backend::Vulkan || actual == Backend::D3D12);
    DescriptorSetLayoutHandle dsl{};
    PipelineLayoutHandle      pll{};
    if (use_desc) {
        DescriptorSetLayoutDesc dl{};
        dl.bindings[0] = { 0, BindingType::CombinedImageSampler, 1, STAGE_FRAGMENT };
        dl.binding_count = 1;
        dsl = dev->create_descriptor_set_layout(dl);
        PipelineLayoutDesc pl{}; pl.set_layouts[0] = dsl; pl.set_layout_count = 1;
        pll = dev->create_pipeline_layout(pl);
    }

    PipelineDesc pd;
    pd.vertex_shader = vs; pd.fragment_shader = ps;
    pd.depth_stencil = DepthStencilState::disabled();
    pd.rasterizer    = RasterizerState::no_cull();
    pd.color_formats[0]   = backbuffer_format(actual);
    pd.color_format_count = 1;
    if (use_desc) pd.layout = pll;
    PipelineHandle pipe = dev->create_pipeline(pd);

    int w, h; win->get_size(&w, &h);
    uint64_t frames = 0;
    while (!win->should_close()) {
        win->poll_events();
        player->update();                 // pull newest decoded frame -> texture()
        TextureHandle vtex = player->texture();

        cmd->begin();
        cmd->set_render_target_backbuffer();
        Viewport vp; vp.x = 0; vp.y = 0; vp.width = (float)w; vp.height = (float)h; vp.min_depth = 0; vp.max_depth = 1;
        cmd->set_viewport(vp);
        cmd->clear_color(ClearColor::black());
        if (vtex.valid()) {
            cmd->set_pipeline(pipe);
            if (use_desc) {
                DescriptorSetDesc sd{}; sd.layout = dsl;
                sd.writes[0] = {}; sd.writes[0].binding = 0; sd.writes[0].type = BindingType::CombinedImageSampler;
                sd.writes[0].texture = vtex; sd.writes[0].sampler = samp; sd.write_count = 1;
                DescriptorSetHandle set = dev->create_descriptor_set(sd);
                cmd->bind_descriptor_set(0, set);
                cmd->draw(3);
                cmd->end(); submit_commander(gfx, cmd);
                dev->destroy_descriptor_set(set);   // per-frame transient set
            } else {
                cmd->bind_texture(0, vtex);
                cmd->bind_sampler(0, samp);
                cmd->draw(3);
                cmd->end(); submit_commander(gfx, cmd);
            }
        } else {
            cmd->end(); submit_commander(gfx, cmd);
        }
        gfx->present();

        if ((frames++ % 120) == 0) {
            video::VideoFrameInfo fi;
            if (player->get_frame_info(&fi))
                std::printf("frame %llu  %dx%d  pts=%.2fs  zero_copy=%d  pos=%.1f/%.1fs\n",
                            (unsigned long long)fi.frame_index, fi.width, fi.height,
                            fi.pts_seconds, (int)fi.zero_copy, player->position(), player->duration());
        }
    }

    player->destroy();
    if (use_desc) { dev->destroy_pipeline(pipe); dev->destroy_pipeline_layout(pll); dev->destroy_descriptor_set_layout(dsl); }
    else dev->destroy_pipeline(pipe);
    dev->destroy_sampler(samp);
    dev->destroy_shader(vs); dev->destroy_shader(ps);
    destroy_commander(cmd); destroy_device(dev); win->destroy();
    return 0;
}
