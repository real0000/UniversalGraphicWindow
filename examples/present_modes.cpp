// example_present_modes — verifies that every SwapMode and a range of custom
// back_buffer counts can actually be created AND presented on each available backend,
// then reports the real swapchain length + effective present mode the driver settled on.
//
// For each backend it walks the matrix {Fifo, FifoRelaxed, Mailbox, Immediate, Auto}
// x {2,3,4 back buffers}: creates a hidden window with that Config, builds an RHI
// device+commander, presents a few frames to the swapchain backbuffer, then queries
// Graphics::get_backbuffer_count() / get_swap_mode() to confirm what took effect.
//
//   exact   - the driver used exactly the requested back_buffer count (DXGI / Vulkan)
//   driver+ - the driver allocated more (e.g. Vulkan Mailbox needs >= 3)
//   clamped - the driver clamped below the request (surface max)
//   GL fixed - OpenGL is double-buffered; back_buffers is not configurable (always 2)
//
// Run with no args for all backends, or pass a name: example_present_modes vulkan

#include "../window.hpp"
#include "../graphics_api.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace window;

static int g_pass = 0, g_fail = 0, g_skip = 0;

static const int   kFrames = 4;
static const SwapMode kModes[] = {
    SwapMode::Fifo, SwapMode::FifoRelaxed, SwapMode::Mailbox, SwapMode::Immediate, SwapMode::Auto
};
static const int kCounts[] = { 2, 3, 4 };

static bool run_combo(Backend backend, SwapMode mode, int back_buffers) {
    Config cfg;
    cfg.backend      = backend;
    cfg.swap_mode    = mode;
    cfg.back_buffers = back_buffers;
    cfg.windows[0].title   = "present_modes";
    cfg.windows[0].width   = 256;
    cfg.windows[0].height  = 144;
    cfg.windows[0].visible = false;   // hidden: the swapchain is still real, just not shown

    const char* mn = swap_mode_to_string(mode);

    Result wr;
    auto windows = Window::create(cfg, &wr);
    if (wr != Result::Success || windows.empty()) {
        std::printf("  %-12s bb=%d -> [FAIL] window create: %s\n", mn, back_buffers, result_to_string(wr));
        return false;
    }
    Window*   win = windows[0];
    Graphics* gfx = win->graphics();

    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) {
        std::printf("  %-12s bb=%d -> [FAIL] no device: %s\n", mn, back_buffers, result_to_string(dr));
        win->destroy();
        return false;
    }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) {
        std::printf("  %-12s bb=%d -> [FAIL] no commander\n", mn, back_buffers);
        destroy_device(dev);
        win->destroy();
        return false;
    }

    int w, h; win->get_size(&w, &h);
    int presented = 0;
    for (int i = 0; i < kFrames && !win->should_close(); ++i) {
        win->poll_events();
        cmd->begin();
        cmd->set_render_target_backbuffer();
        Viewport vp; vp.x = 0; vp.y = 0; vp.width = (float)w; vp.height = (float)h; vp.min_depth = 0; vp.max_depth = 1;
        cmd->set_viewport(vp);
        cmd->clear_color(ClearColor(0.10f, 0.20f, 0.40f, 1.0f));
        cmd->end();
        submit_commander(gfx, cmd);
        gfx->present();
        ++presented;
    }

    const int      bb  = gfx->get_backbuffer_count();
    const SwapMode eff = gfx->get_swap_mode();

    const char* tag;
    if      (backend == Backend::OpenGL) tag = "GL fixed";
    else if (bb == back_buffers)         tag = "exact";
    else if (bb >  back_buffers)         tag = "driver+";
    else                                 tag = "clamped";

    const bool ok = (presented == kFrames);
    std::printf("  %-12s bb=%d -> [%s] presented %d, actual_bb=%d effective=%-12s (%s)\n",
                mn, back_buffers, ok ? "PASS" : "FAIL", presented, bb, swap_mode_to_string(eff), tag);

    destroy_commander(cmd);
    destroy_device(dev);
    win->destroy();
    return ok;
}

int main(int argc, char** argv) {
    std::printf("present-mode + backbuffer-count matrix\n");

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
            std::printf("\n=== Backend: %s ===\n  [SKIP] not supported\n", e.n);
            ++g_skip;
            continue;
        }
        std::printf("\n=== Backend: %s ===\n", e.n);
        for (SwapMode m : kModes)
            for (int c : kCounts)
                run_combo(e.b, m, c) ? ++g_pass : ++g_fail;
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
