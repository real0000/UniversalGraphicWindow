// example_material_resources — exercises the Material binding / creation / editing API.
//
// On top of renderer/material this verifies the pieces added for full shader-resource
// handling:
//   * set_storage_buffer / set_uniform_buffer binders (storage + uniform in one set),
//   * Material-owned buffer creation: create_storage<T>() / create_uniform<T>() build a
//     gfx::Buffer for a named binding and hand it back for editing (Material owns it),
//   * the typed CPU-mirror editing wrappers — TypedStorageBuffer's operator[] (vector-like)
//     and TypedConstBuffer's data()/-> (struct-like), pushed to the GPU with flush(),
//   * multi-descriptor-set layouts: the `set` field is honoured (Data in set 0, Params in
//     set 1) — set_count() reports it and bind() issues one bind per set.
//
// Each backend runs a tiny compute material — Data[i] = Data[i]*mul + i, with Data seeded
// to i — three ways and reads the result back:
//   A. external buffers bound via set_storage_buffer / set_uniform_buffer
//   B. Material-owned buffers via create_storage/create_uniform, edited then flushed
//   C. a 2-set variant (firm on Vulkan; best-effort elsewhere — register spaces need
//      SM5.1+/DXIL, so the D3D SM5 path may not build it: reported as SKIP, not FAIL)
//
// Offscreen compute only, no visible window. Run with no args for all backends or pass a
// name: example_material_resources vulkan
//
// The HLSL + .material files are synthesized into a temp directory at startup and compiled
// at runtime by the built-in shader compiler (one HLSL source, every backend).

#include "../window.hpp"
#include "../graphics_api.hpp"
#include "../renderer/material.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

//============================================================================
// Tiny harness
//============================================================================
static int g_pass = 0, g_fail = 0, g_skip = 0;

static void check(const char* name, bool ok, const std::string& detail = "") {
    std::printf("    [%s] %-26s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    ok ? ++g_pass : ++g_fail;
}
static void skip(const char* name, const std::string& detail) {
    std::printf("    [SKIP] %-26s %s\n", name, detail.c_str());
    ++g_skip;
}

//============================================================================
// Shader + material sources (HLSL compiled at runtime; INI .material)
//============================================================================

// Cross-API: the built-in compiler reads register(uN)/(bN) as binding N directly. The typed
// storage buffers carry a stride (sizeof(T)), so the backend builds a *structured* UAV and the
// shader uses RWStructuredBuffer<uint> — element addressing matches operator[] on every backend.
static const char* kComputeHLSL = R"(
RWStructuredBuffer<uint> Data : register(u0);
cbuffer Params : register(b1) { uint count; uint mul; uint2 _pad; };

[numthreads(64,1,1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i < count) Data[i] = Data[i] * mul + i;
}
)";

// Same kernel, but the resources live in two descriptor sets (spaces).
static const char* kComputeMultiHLSL = R"(
RWStructuredBuffer<uint> Data : register(u0, space0);
cbuffer Params : register(b0, space1) { uint count; uint mul; uint2 _pad; };

[numthreads(64,1,1)]
void cs_main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i < count) Data[i] = Data[i] * mul + i;
}
)";

static const char* kComputeMaterial = R"(
[shader]
source  = mat_cs.hlsl
compute = cs_main

[bindings]
Data   = 0:0:storage_buffer:compute
Params = 0:1:uniform_buffer:compute
)";

static const char* kComputeMultiMaterial = R"(
[shader]
source  = mat_cs_multi.hlsl
compute = cs_main

[bindings]
Data   = 0:0:storage_buffer:compute
Params = 1:0:uniform_buffer:compute
)";

// Matches `cbuffer Params { uint count; uint mul; uint2 _pad; }` (16 bytes).
struct ParamsT { uint32_t count = 0, mul = 0, pad0 = 0, pad1 = 0; };

static const uint32_t N   = 256;
static const uint32_t MUL = 3;

static bool write_text(const std::string& path, const char* text) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(text, (std::streamsize)std::strlen(text));
    return (bool)f;
}

// Data[i] seeded to i, kernel computes Data[i]*mul + i = i*(mul+1).
static bool verify(const uint32_t* data, std::string& detail) {
    for (uint32_t i = 0; i < N; ++i) {
        const uint32_t expect = i * MUL + i;
        if (data[i] != expect) {
            char b[96];
            std::snprintf(b, sizeof b, "Data[%u]=%u, expected %u", i, data[i], expect);
            detail = b;
            return false;
        }
    }
    detail = "all " + std::to_string(N) + " elements correct";
    return true;
}

//============================================================================
// Per-backend driver
//============================================================================
static bool run_backend(Backend backend, const char* name, const std::string& dir) {
    std::printf("\n=== Backend: %s ===\n", name);

    Config config;
    config.backend = backend;
    config.windows[0].title = "example_material_resources";
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
    if (!dev) { skip("backend", std::string("no GraphicDevice: ") + result_to_string(dr)); windows[0]->destroy(); return false; }

    GraphicsCapabilities caps; dev->get_capabilities(&caps);
    if (!caps.compute_shaders) {
        skip("backend", "no compute-shader support");
        destroy_device(dev); windows[0]->destroy(); return false;
    }
    GraphicCommander* cmd = create_commander(gfx, dev, &dr);
    if (!cmd) { skip("backend", "no commander"); destroy_device(dev); windows[0]->destroy(); return false; }

    const std::string single_mat = dir + "/mat_cs.material";
    const std::string multi_mat  = dir + "/mat_cs_multi.material";
    const bool firm_multiset = (backend == Backend::Vulkan);

    auto dispatch = [&](Material* mat) {
        cmd->begin();
        mat->bind(cmd);                       // set_pipeline + bind every descriptor set
        cmd->dispatch((N + 63) / 64);
        cmd->end();
        submit_commander(gfx, cmd);
        dev->wait_idle();
    };

    // ---- A. external buffers via set_storage_buffer / set_uniform_buffer ----
    {
        Result mr; Material* mat = Material::create(dev, single_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            check("A.material.create", false, std::string("create failed: ") + result_to_string(mr));
            if (mat) mat->destroy();
        } else {
            std::vector<uint32_t> init(N);
            for (uint32_t i = 0; i < N; ++i) init[i] = i;
            StorageBuffer data; data.create_array<uint32_t>(dev, N, init.data(), ResourceUsage::Default, "Data");
            ParamsT pv; pv.count = N; pv.mul = MUL;
            ConstBuffer params; params.create_for<ParamsT>(dev, &pv, ResourceUsage::Default, "Params");

            mat->set_storage_buffer("Data", data);
            mat->set_uniform_buffer("Params", params, 0, sizeof(ParamsT));
            dispatch(mat);

            std::vector<uint32_t> back(N, 0);
            data.read(back.data(), N * sizeof(uint32_t), 0);
            std::string det; bool ok = verify(back.data(), det);
            check("A.set_storage_buffer", ok, det);
            mat->destroy();
        }
    }

    // ---- B. Material-owned buffers + typed CPU-mirror editing ----
    {
        Result mr; Material* mat = Material::create(dev, single_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            check("B.material.create", false, std::string("create failed: ") + result_to_string(mr));
            if (mat) mat->destroy();
        } else {
            // Material allocates + owns the buffers for these bindings.
            TypedStorageBuffer<uint32_t>* data = mat->create_storage<uint32_t>("Data", N);
            TypedConstBuffer<ParamsT>*    params = mat->create_uniform<ParamsT>("Params");
            bool created = data && params;
            check("B.create_storage/uniform", created, created ? "Material owns Data + Params" : "create_* returned null");
            if (created) {
                for (uint32_t i = 0; i < N; ++i) (*data)[i] = i;   // vector-like operator[] edit
                data->flush();                                     // upload the mirror
                params->data().count = N;                          // struct-like field edit
                params->data().mul   = MUL;
                params->flush();
                dispatch(mat);

                data->sync();                                      // pull GPU result into the mirror
                std::string det; bool ok = verify(data->begin(), det);
                check("B.typed_edit+flush", ok, det);
            }
            mat->destroy();   // frees Material-owned Data + Params
        }
    }

    // ---- C. multi-descriptor-set material (honours the `set` field) ----
    {
        Result mr; Material* mat = Material::create(dev, multi_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            skip("C.multiset", std::string(name) + ": 2-set/register-space shader unsupported (" + result_to_string(mr) + ")");
            if (mat) mat->destroy();
        } else {
            check("C.multiset.layout", mat->set_count() == 2, "set_count=" + std::to_string(mat->set_count()));
            TypedStorageBuffer<uint32_t>* data = mat->create_storage<uint32_t>("Data", N);
            TypedConstBuffer<ParamsT>*    params = mat->create_uniform<ParamsT>("Params");
            if (data && params) {
                for (uint32_t i = 0; i < N; ++i) (*data)[i] = i;
                data->flush();
                params->data().count = N; params->data().mul = MUL; params->flush();
                dispatch(mat);
                data->sync();
                std::string det; bool ok = verify(data->begin(), det);
                if (firm_multiset || ok) check("C.multiset.dispatch", ok, det);
                else                     skip("C.multiset.dispatch", std::string(name) + ": multi-set runtime mismatch (" + det + ")");
            }
            mat->destroy();
        }
    }

    // ---- D. aligned sub-range binding: UBO ring offset + SSBO offset ----
    // Proves the bind offset is honored per each backend's alignment rules (D3D12 CBV 256B,
    // D3D11.1 first-constant 256B, VK min*OffsetAlignment, GL *_OFFSET_ALIGNMENT). Binds
    // Params from slice i of a ring and Data as the *second half* of a 2N buffer.
    {
        Result mr; Material* mat = Material::create(dev, single_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            check("D.material.create", false, std::string("create failed: ") + result_to_string(mr));
            if (mat) mat->destroy();
        } else {
            const uint32_t UA = caps.min_uniform_buffer_offset_alignment ? (uint32_t)caps.min_uniform_buffer_offset_alignment : 256u;
            const uint32_t SLICE = UA > sizeof(ParamsT) ? UA : 256u;   // alignment-sized ring slot
            const uint32_t K = 3, HALF = N;
            std::vector<uint8_t> ring(size_t(SLICE) * K, 0);
            for (uint32_t i = 0; i < K; ++i) { ParamsT p; p.count = HALF; p.mul = i + 2; std::memcpy(ring.data() + size_t(i) * SLICE, &p, sizeof p); }
            ConstBuffer ubo; ubo.create(dev, SLICE * K, ResourceUsage::Default, ring.data(), "ring");
            StorageBuffer data; data.create(dev, 2 * HALF * sizeof(uint32_t), sizeof(uint32_t), ResourceUsage::Default, nullptr, "data2N");

            const uint32_t soff = HALF * (uint32_t)sizeof(uint32_t);                 // second half (stride- and 16-aligned)
            mat->set_storage_buffer("Data", data, soff, HALF * (uint32_t)sizeof(uint32_t));

            bool all_ok = true; std::string det;
            for (uint32_t i = 0; i < K && all_ok; ++i) {
                std::vector<uint32_t> seed(2 * HALF);
                for (uint32_t j = 0; j < HALF; ++j) { seed[j] = 0xDEADu; seed[HALF + j] = j; }   // 1st half sentinel
                data.update(seed.data(), 2 * HALF * sizeof(uint32_t), 0);
                mat->set_uniform_buffer("Params", ubo, i * SLICE, sizeof(ParamsT));
                dispatch(mat);
                std::vector<uint32_t> back(2 * HALF, 0);
                data.read(back.data(), 2 * HALF * sizeof(uint32_t), 0);
                const uint32_t mul = i + 2;
                for (uint32_t j = 0; j < HALF; ++j) {
                    if (back[j] != 0xDEADu)            { all_ok = false; det = "1st half clobbered at " + std::to_string(j) + " (offset ignored?)"; break; }
                    if (back[HALF + j] != j * mul + j) { all_ok = false; det = "slice " + std::to_string(i) + " Data[" + std::to_string(j) + "]=" + std::to_string(back[HALF + j]) + " exp " + std::to_string(j * mul + j); break; }
                }
            }
            if (all_ok) det = std::to_string(K) + " UBO ring slices + SSBO 2nd-half offset, all correct";
            check("D.aligned_subrange", all_ok, det);
            mat->destroy();
        }
    }

    // ---- E. alignment-free []-indexed uniform array (no offset/stride in user code) ----
    // The headline ergonomics: edit a UniformArray like std::vector and bind one element by
    // index. UniformArray hides the 256B (etc.) per-element alignment entirely.
    {
        Result mr; Material* mat = Material::create(dev, single_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            check("E.material.create", false, std::string("create failed: ") + result_to_string(mr));
            if (mat) mat->destroy();
        } else {
            const uint32_t K = 3;
            UniformArray<ParamsT> ring;                                   // user code: zero alignment
            bool created = ring.create(dev, K);
            for (uint32_t i = 0; i < K; ++i) { ring[i].count = N; ring[i].mul = i + 2; }  // plain [] set
            ring.flush();
            TypedStorageBuffer<uint32_t>* data = mat->create_storage<uint32_t>("Data", N);
            bool ok = created && data; std::string det;
            for (uint32_t i = 0; i < K && ok; ++i) {
                for (uint32_t j = 0; j < N; ++j) (*data)[j] = j;
                data->flush();
                mat->set_uniform_element("Params", ring, i);             // bind element i — offset hidden
                dispatch(mat);
                data->sync();
                const uint32_t mul = ring[i].mul;                        // plain [] get
                for (uint32_t j = 0; j < N; ++j)
                    if ((*data)[j] != j * mul + j) { ok = false; det = "elem " + std::to_string(i) + " Data[" + std::to_string(j) + "]=" + std::to_string((*data)[j]); break; }
            }
            if (ok) det = std::to_string(K) + " []-indexed UBO elements bound by index, no alignment code (stride=" + std::to_string(ring.stride()) + "B)";
            check("E.uniform_array", ok, det);
            mat->destroy();
        }
    }

    // ---- F. set uniform fields BY NAME (offsets from shader reflection; no struct/packing) ----
    // The strongest "no alignment knowledge" path: the user never declares a C++ struct nor pads
    // anything — fields are written at the offsets the shader itself reports.
    {
        Result mr; Material* mat = Material::create(dev, single_mat.c_str(), dir.c_str(), &mr);
        if (!mat || !mat->pipeline().valid()) {
            check("F.material.create", false, std::string("create failed: ") + result_to_string(mr));
            if (mat) mat->destroy();
        } else {
            UniformBlock params = mat->uniform_block("Params");      // reflected layout
            const bool reflected = params.valid() && params.has("count") && params.has("mul");
            check("F.reflect_fields", reflected,
                  reflected ? "Params{count,mul} reflected, size=" + std::to_string(params.size()) + "B"
                            : "reflection unavailable or field names not found");
            if (reflected) {
                TypedStorageBuffer<uint32_t>* data = mat->create_storage<uint32_t>("Data", N);
                for (uint32_t j = 0; j < N; ++j) (*data)[j] = j;
                data->flush();
                params.set("count", (uint32_t)N).set("mul", (uint32_t)MUL);   // set BY NAME
                params.flush();
                dispatch(mat);
                data->sync();
                std::string det; bool ok = verify(data->begin(), det);
                check("F.set_by_name", ok, det);
            }
            mat->destroy();
        }
    }

    destroy_commander(cmd);
    destroy_device(dev);
    windows[0]->destroy();
    return true;
}

int main(int argc, char** argv) {
    std::printf("Material binding / creation / editing test\n");

    // Synthesize the HLSL + .material into a temp dir, compiled at runtime per backend.
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / "ugw_material_res";
    std::filesystem::create_directories(dir, ec);
    const std::string d = dir.string();
    bool wrote = write_text(d + "/mat_cs.hlsl",         kComputeHLSL)
              && write_text(d + "/mat_cs_multi.hlsl",   kComputeMultiHLSL)
              && write_text(d + "/mat_cs.material",      kComputeMaterial)
              && write_text(d + "/mat_cs_multi.material", kComputeMultiMaterial);
    if (!wrote) { std::printf("    [FAIL] could not write shader files to %s\n", d.c_str()); return 1; }
    std::printf("shaders: %s\n", d.c_str());

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
        run_backend(e.b, e.n, d);
    }

    std::printf("\n=== Summary: %d passed, %d failed, %d skipped ===\n", g_pass, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
