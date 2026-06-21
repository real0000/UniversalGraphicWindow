// example_buffer — exercises the renderer/buffer wrappers (gfx::Buffer + typed
// VertexBuffer/IndexBuffer/ConstBuffer/StorageBuffer/IndirectBuffer, gfx::Texture, and
// the BufferView / TextureView "one resource, many views" model) against every desktop
// backend with no visible window.
//
// For each backend it creates a hidden window → GraphicDevice, then:
//   * round-trips data through each typed buffer (create → update → read_buffer),
//   * verifies the metadata the wrappers derive (vertex/index/element counts, stride,
//     index format),
//   * slices one uniform buffer into several BufferView windows and one storage buffer
//     into a read + a read-write view, checking each descriptor,
//   * uploads a 4x4 texture, reads it back, and builds several TextureViews
//     (format-reinterpret / single-mip / whole), checking validity + ownership, and
//   * checks move semantics (no double-free, source invalidated).
//
// Run with no args for all backends, or pass a name: example_buffer d3d11

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/buffer/gpu_buffer.hpp"
#include "../renderer/buffer/gpu_texture.hpp"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

//============================================================================
// Tiny harness
//============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;
static const char* g_backend = "";

static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("    [%s] %-26s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-26s %s\n", name, detail.c_str());
    ++g_skip;
}

//============================================================================
// Tests (per backend)
//============================================================================

static void test_vertex_index(GraphicDevice* dev) {
    struct V { float x, y, z; float r, g, b, a; };
    const V verts[3] = {
        { -0.5f, -0.5f, 0, 1, 0, 0, 1 },
        {  0.5f, -0.5f, 0, 0, 1, 0, 1 },
        {  0.0f,  0.5f, 0, 0, 0, 1, 1 },
    };
    const uint16_t idx[3] = { 0, 1, 2 };

    VertexBuffer vb;
    bool okv = vb.create_from(dev, verts, 3, ResourceUsage::Default, "tri-vb");
    check("vertex.create", okv && vb.valid());
    check("vertex.metadata", vb.stride() == sizeof(V) && vb.vertex_count() == 3 &&
          vb.type() == BufferType::Vertex,
          "stride=" + std::to_string(vb.stride()) + " count=" + std::to_string(vb.vertex_count()));

    V back[3] = {};
    vb.read(back, sizeof back, 0);
    check("vertex.readback", std::memcmp(verts, back, sizeof verts) == 0);

    IndexBuffer ib;
    bool oki = ib.create_from(dev, idx, 3, ResourceUsage::Default, "tri-ib");
    check("index.create", oki && ib.valid() && ib.format() == IndexFormat::UInt16);
    check("index.metadata", ib.index_count() == 3 && ib.type() == BufferType::Index,
          "count=" + std::to_string(ib.index_count()));
    uint16_t iback[3] = {};
    ib.read(iback, sizeof iback, 0);
    check("index.readback", std::memcmp(idx, iback, sizeof idx) == 0);
}

static void test_const_buffer(GraphicDevice* dev) {
    struct Cb { float mvp[16]; float color[4]; };
    Cb c{};
    for (int i = 0; i < 16; ++i) c.mvp[i] = (i % 5 == 0) ? 1.0f : 0.0f;  // identity-ish
    c.color[0] = 0.25f; c.color[1] = 0.5f; c.color[2] = 0.75f; c.color[3] = 1.0f;

    ConstBuffer cb;
    bool ok = cb.create_for<Cb>(dev, &c, ResourceUsage::Default, "cb");
    check("const.create", ok && cb.valid() && cb.size() == sizeof(Cb) &&
          cb.type() == BufferType::Uniform);

    // Mutate via set<T>() then read back.
    c.color[0] = 0.9f;
    cb.set(c);
    Cb back{};
    cb.read(&back, sizeof back, 0);
    check("const.set/readback", std::memcmp(&c, &back, sizeof c) == 0);
}

static void test_storage_and_views(GraphicDevice* dev) {
    const uint32_t N = 64;
    std::vector<uint32_t> src(N);
    for (uint32_t i = 0; i < N; ++i) src[i] = i * 3 + 1;

    StorageBuffer sb;
    bool ok = sb.create_array<uint32_t>(dev, N, src.data(), ResourceUsage::Default, "ssbo");
    check("storage.create", ok && sb.valid() && sb.element_count() == N &&
          sb.stride() == sizeof(uint32_t) && sb.type() == BufferType::Storage,
          "elems=" + std::to_string(sb.element_count()));

    std::vector<uint32_t> back(N, 0);
    sb.read(back.data(), N * sizeof(uint32_t), 0);
    check("storage.readback", std::memcmp(src.data(), back.data(), N * sizeof(uint32_t)) == 0);

    // One allocation → a read view and a read-write view (native SRV + UAV).
    BufferView rv = sb.read_view();
    BufferView wv = sb.read_write_view();
    check("storage.read_view", rv.valid() && rv.type == BufferViewType::Storage &&
          rv.access == StorageAccess::Read && rv.buffer == sb.handle());
    check("storage.rw_view", wv.valid() && wv.access == StorageAccess::ReadWrite &&
          wv.buffer == sb.handle());

    // Slice ONE uniform buffer into several per-object constant windows.
    ConstBuffer ring;
    const uint32_t slice = 64, slices = 4;
    bool rok = ring.create(dev, slice * slices, ResourceUsage::Default, nullptr, "ubo-ring");
    check("views.ubo_ring.create", rok && ring.valid());
    bool slices_ok = rok;
    for (uint32_t i = 0; i < slices; ++i) {
        BufferView v = ring.uniform_view(i * slice, slice);
        slices_ok = slices_ok && v.valid() && v.type == BufferViewType::Uniform &&
                    v.offset == i * slice && v.size == slice && v.buffer == ring.handle();
    }
    check("views.ubo_ring.slices", slices_ok, std::to_string(slices) + " uniform windows on 1 buffer");

    // A combined-geometry buffer viewed as both vertex and index ranges (descriptor
    // correctness; whether the backend can bind a single allocation as both depends on
    // the underlying buffer type, hence this checks the view math, not a live bind).
    BufferView vtx = sb.vertex_view(0, 12);
    BufferView ind = sb.index_view(IndexFormat::UInt32, 48);
    check("views.vertex_view", vtx.type == BufferViewType::Vertex && vtx.stride == 12 &&
          vtx.offset == 0);
    check("views.index_view", ind.type == BufferViewType::Index &&
          ind.index_format == IndexFormat::UInt32 && ind.offset == 48);
}

static void test_texture_and_views(GraphicDevice* dev) {
    // 4x4 RGBA8 gradient.
    uint8_t px[4 * 4 * 4];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            uint8_t* p = &px[(y * 4 + x) * 4];
            p[0] = (uint8_t)(x * 64); p[1] = (uint8_t)(y * 64); p[2] = 128; p[3] = 255;
        }

    Texture tex;
    bool ok = tex.create_2d(dev, 4, 4, TextureFormat::RGBA8_UNORM, px, 1,
                            TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_COPY_SRC | TEXTURE_USAGE_COPY_DST,
                            "tex4x4");
    check("texture.create", ok && tex.valid() && tex.width() == 4 && tex.height() == 4 &&
          tex.format() == TextureFormat::RGBA8_UNORM);

    uint8_t back[4 * 4 * 4] = {};
    TextureRegion r; r.x = 0; r.y = 0; r.width = 4; r.height = 4; r.mip = 0; r.layer = 0;
    tex.read(r, back);
    int diff = 0;
    for (size_t i = 0; i < sizeof px; ++i) diff += std::abs(int(px[i]) - int(back[i]));
    check("texture.readback", diff <= 8, "sum|d|=" + std::to_string(diff));

    // Views onto the same texture. On D3D11/D3D12 a view aliases the source (owns()==false);
    // on OpenGL/Vulkan it is a distinct object (owns()==true). Either way it is valid + bindable.
    TextureView whole = tex.create_view();
    check("texture.view.whole", whole.valid(),
          std::string("owns=") + (whole.owns() ? "true" : "false"));

    TextureView fview = tex.format_view(TextureFormat::RGBA8_UNORM_SRGB);
    check("texture.view.format", fview.valid() &&
          fview.desc().format == TextureFormat::RGBA8_UNORM_SRGB);

    TextureView m0 = tex.mip_view(0);
    check("texture.view.mip", m0.valid() && m0.desc().base_mip == 0 && m0.desc().mip_count == 1);

    // Many live views at once on one texture (the headline feature).
    check("texture.multi_view", whole.valid() && fview.valid() && m0.valid(),
          "3 live views on 1 texture");
}

static void test_move_semantics(GraphicDevice* dev) {
    float data[4] = { 1, 2, 3, 4 };
    VertexBuffer a;
    a.create(dev, 1, sizeof data, data, ResourceUsage::Default, "move-src");
    BufferHandle h = a.handle();
    check("move.src_valid", a.valid());

    VertexBuffer b = std::move(a);
    check("move.transferred", b.valid() && b.handle() == h && !a.valid(),
          "source invalidated, handle moved");
    // a's destructor must be a safe no-op now (no double free). b frees the resource.
}

//============================================================================
// Per-backend driver
//============================================================================
static bool run_backend(Backend backend, const char* name) {
    std::printf("\n=== Backend: %s ===\n", name);
    g_backend = name;

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_buffer";
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

    test_vertex_index(dev);
    test_const_buffer(dev);
    test_storage_and_views(dev);
    test_texture_and_views(dev);
    test_move_semantics(dev);

    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("renderer/buffer wrapper smoke test\n");

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

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
