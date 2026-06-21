// example_present - windowed swapchain present smoke test.
//
// Verifies the RHI's windowed render-to-screen path: bind the swapchain backbuffer
// (set_render_target_backbuffer), clear it to a distinct colour, and present -- per backend.
// A correct run shows a solid green window; capture it (capture_window.ps1) to verify.
//
//   example_present [backend] [frames]
//     backend : opengl | vulkan | d3d11 | d3d12   (default: all supported)
//     frames  : number of frames to present        (default: 600)

#include "../window.hpp"
#include "../graphics_api.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

using namespace window;

static bool g_verify = false;   // read the backbuffer back and print its centre pixel

#ifdef _WIN32
// Definitive D3D11 check: copy the freshly-cleared backbuffer to a staging texture and read
// its centre pixel (no screenshot needed). Proves set_render_target_backbuffer bound the real
// backbuffer and the clear landed on it. Must run BEFORE present (FLIP_DISCARD recycles it).
static void verify_d3d11_backbuffer(Graphics* gfx, int w, int h) {
    auto* dev = (ID3D11Device*)gfx->native_device();
    auto* ctx = (ID3D11DeviceContext*)gfx->native_context();
    auto* sc  = (IDXGISwapChain*)gfx->native_swapchain();
    if (!dev || !ctx || !sc) { std::printf("[verify] missing D3D11 handles\n"); return; }
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)) || !bb) { std::printf("[verify] GetBuffer failed\n"); return; }
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0;
    ID3D11Texture2D* stg = nullptr;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &stg)) || !stg) { bb->Release(); std::printf("[verify] staging create failed\n"); return; }
    ctx->CopyResource(stg, bb);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) {
        int cx = w / 2, cy = h / 2;
        const uint8_t* px = (const uint8_t*)m.pData + (size_t)cy * m.RowPitch + (size_t)cx * 4;
        // Swapchain is typically B8G8R8A8 or R8G8B8A8; print raw bytes (clear = R26 G178 B76).
        std::printf("[verify] D3D11 backbuffer centre bytes = (%d,%d,%d,%d)  [clear R26 G178 B76]\n",
                    px[0], px[1], px[2], px[3]);
        ctx->Unmap(stg, 0);
    } else std::printf("[verify] Map failed\n");
    stg->Release(); bb->Release();
}

// Definitive D3D12 check: copy the current backbuffer (in PRESENT state after the frame's
// end() barrier) to a READBACK buffer on its own command list, fence-wait, read the centre.
static void verify_d3d12_backbuffer(Graphics* gfx, int w, int h) {
    auto* dev   = (ID3D12Device*)gfx->native_device();
    auto* queue = (ID3D12CommandQueue*)gfx->native_context();
    auto* scraw = (IDXGISwapChain*)gfx->native_swapchain();
    if (!dev || !queue || !scraw) { std::printf("[verify] missing D3D12 handles\n"); return; }
    IDXGISwapChain3* sc = nullptr;
    if (FAILED(scraw->QueryInterface(IID_PPV_ARGS(&sc))) || !sc) { std::printf("[verify] no IDXGISwapChain3\n"); return; }
    UINT idx = sc->GetCurrentBackBufferIndex();
    ID3D12Resource* bb = nullptr;
    if (FAILED(sc->GetBuffer(idx, IID_PPV_ARGS(&bb))) || !bb) { sc->Release(); std::printf("[verify] GetBuffer failed\n"); return; }

    D3D12_RESOURCE_DESC rd = bb->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows = 0; UINT64 rowbytes = 0, total = 0;
    dev->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowbytes, &total);

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* rb = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb));

    ID3D12CommandAllocator* alloc = nullptr; ID3D12GraphicsCommandList* cl = nullptr;
    dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&cl));

    auto barrier = [&](D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = { bb, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to }; cl->ResourceBarrier(1, &b);
    };
    barrier(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = bb; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    cl->Close();
    ID3D12CommandList* lists[] = { cl }; queue->ExecuteCommandLists(1, lists);

    ID3D12Fence* fence = nullptr; dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence, 1); fence->SetEventOnCompletion(1, ev); WaitForSingleObject(ev, INFINITE);

    void* p = nullptr; D3D12_RANGE rr{ 0, (SIZE_T)total };
    if (SUCCEEDED(rb->Map(0, &rr, &p)) && p) {
        int cx = w / 2, cy = h / 2;
        const uint8_t* px = (const uint8_t*)p + fp.Offset + (size_t)cy * fp.Footprint.RowPitch + (size_t)cx * 4;
        std::printf("[verify] D3D12 backbuffer centre bytes = (%d,%d,%d,%d)  [clear R26 G178 B76]\n",
                    px[0], px[1], px[2], px[3]);
        rb->Unmap(0, nullptr);
    } else std::printf("[verify] D3D12 Map failed\n");

    CloseHandle(ev); fence->Release(); cl->Release(); alloc->Release(); rb->Release(); bb->Release(); sc->Release();
}
#endif

static bool run(Backend backend, const char* name, int frames) {
    Config c;
    c.backend = backend;
    c.windows[0].title = "UGW Present Test";
    c.windows[0].width = 400; c.windows[0].height = 300;
    c.windows[0].visible = true;

    Result wr;
    auto ws = Window::create(c, &wr);
    if (wr != Result::Success || ws.empty()) {
        std::printf("[%-11s] SKIP window create: %s\n", name, result_to_string(wr));
        return false;
    }
    Window* win = ws[0];
    Graphics* gfx = win->graphics();

    Result dr;
    GraphicDevice* dev = create_device(gfx, &dr);
    if (!dev) { std::printf("[%-11s] SKIP no device: %s\n", name, result_to_string(dr)); win->destroy(); return false; }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { std::printf("[%-11s] SKIP no commander\n", name); destroy_device(dev); win->destroy(); return false; }

    int w, h; win->get_size(&w, &h);
    int presented = 0;
    // frames <= 0 means run until the window is closed (handy for screen-capture verification).
    for (int i = 0; (frames <= 0 || i < frames) && !win->should_close(); ++i) {
        win->poll_events();
        cmd->begin();
        cmd->set_render_target_backbuffer();
        Viewport vp; vp.x = 0; vp.y = 0; vp.width = (float)w; vp.height = (float)h; vp.min_depth = 0; vp.max_depth = 1;
        cmd->set_viewport(vp);
        cmd->clear_color(ClearColor(0.10f, 0.70f, 0.30f, 1.0f));   // distinct green
        cmd->end();
        submit_commander(gfx, cmd);
#ifdef _WIN32
        if (g_verify && backend == Backend::D3D11 && i == 0) verify_d3d11_backbuffer(gfx, w, h);
        if (g_verify && backend == Backend::D3D12 && i == 0) verify_d3d12_backbuffer(gfx, w, h);
#endif
        gfx->present();
        ++presented;
    }
    std::printf("[%-11s] presented %d frames (window %dx%d)\n", name, presented, w, h);

    destroy_commander(cmd); destroy_device(dev); win->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("windowed swapchain present test\n");
    const char* only   = (argc > 1) ? argv[1] : nullptr;
    const int   frames = (argc > 2) ? std::atoi(argv[2]) : 600;

    struct { Backend b; const char* n; } bk[] = {
        { Backend::OpenGL, "OpenGL" }, { Backend::Vulkan, "Vulkan" },
        { Backend::D3D11, "Direct3D 11" }, { Backend::D3D12, "Direct3D 12" },
    };
    auto matches = [](const std::string& a, Backend b) -> bool {
        switch (b) {
            case Backend::OpenGL: return a.find("gl") != std::string::npos;
            case Backend::Vulkan: return a.find("vk") != std::string::npos || a.find("vulkan") != std::string::npos;
            case Backend::D3D11:  return a.find("11") != std::string::npos;
            case Backend::D3D12:  return a.find("12") != std::string::npos;
            default: return false;
        }
    };
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], "verify") == 0) g_verify = true;
    std::string a;
    if (only && std::strcmp(only, "verify") != 0) { a = only; for (auto& ch : a) ch = (char)tolower(ch); }
    else only = nullptr;
    for (auto& e : bk) {
        if (only && !matches(a, e.b)) continue;
        if (!is_backend_supported(e.b)) { std::printf("[%-11s] not supported\n", e.n); continue; }
        run(e.b, e.n, frames);
    }
    return 0;
}
