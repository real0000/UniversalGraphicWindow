// renderer/shader_compiler/shader_compiler.cpp
//
// HLSL -> { SPIR-V (Vulkan), GLSL (OpenGL), DXBC (D3D11/D3D12), MSL (Metal) } at runtime.
//   * glslang        compiles HLSL to SPIR-V (register(bN) -> binding N; no [[vk::binding]]).
//   * SPIRV-Cross    lowers that SPIR-V to GLSL / MSL.
//   * d3dcompiler    compiles HLSL straight to DXBC (Windows only; DXBC has no source form).
//
// Everything is data-in / data-out so the same path serves the runtime API and the CLI.

#include "shader_compiler.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

namespace window {
namespace gfx {

ShaderLanguage shader_blob_language(Backend backend) {
    switch (backend) {
        case Backend::Vulkan: return ShaderLanguage::SPIRV;
        case Backend::OpenGL: return ShaderLanguage::GLSL;
        case Backend::D3D11:
        case Backend::D3D12:  return ShaderLanguage::DXBC;
        case Backend::Metal:  return ShaderLanguage::MSL;
        default:              return ShaderLanguage::SPIRV;
    }
}

#ifdef WINDOW_SUPPORT_SHADER_COMPILER

} // namespace gfx
} // namespace window

// ---- third-party headers (outside our namespace) ---------------------------------------
// glslang gates its HLSL-only entry points (setHlslIoMapping, EShSourceHlsl handling) behind
// ENABLE_HLSL; the lib is built with it, so the consuming TU must define it too.
#ifndef ENABLE_HLSL
#define ENABLE_HLSL 1
#endif
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include <spirv_glsl.hpp>
#include <spirv_msl.hpp>

#ifdef _WIN32
#include <d3dcompiler.h>
#include <windows.h>
#pragma comment(lib, "d3dcompiler.lib")
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace window {
namespace gfx {
namespace {

//=============================================================================
// glslang process lifetime (ref-counted)
//=============================================================================
std::mutex g_init_mtx;
int        g_init_count = 0;

bool glslang_acquire() {
    std::lock_guard<std::mutex> lock(g_init_mtx);
    if (g_init_count++ == 0) return glslang::InitializeProcess();
    return true;
}
void glslang_release() {
    std::lock_guard<std::mutex> lock(g_init_mtx);
    if (g_init_count > 0 && --g_init_count == 0) glslang::FinalizeProcess();
}
// RAII bracket so every compile() balances its acquire/release.
struct GlslangScope {
    bool ok;
    GlslangScope() : ok(glslang_acquire()) {}
    ~GlslangScope() { glslang_release(); }
};

//=============================================================================
// Stage mapping
//=============================================================================
EShLanguage to_esh(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex:       return EShLangVertex;
        case ShaderStage::Fragment:     return EShLangFragment;
        case ShaderStage::Geometry:     return EShLangGeometry;
        case ShaderStage::TessControl:  return EShLangTessControl;
        case ShaderStage::TessEval:     return EShLangTessEvaluation;
        case ShaderStage::Compute:      return EShLangCompute;
        case ShaderStage::Task:         return EShLangTask;
        case ShaderStage::Mesh:         return EShLangMesh;
        case ShaderStage::RayGen:       return EShLangRayGen;
        case ShaderStage::Miss:         return EShLangMiss;
        case ShaderStage::ClosestHit:   return EShLangClosestHit;
        case ShaderStage::AnyHit:       return EShLangAnyHit;
        case ShaderStage::Intersection: return EShLangIntersect;
        case ShaderStage::Callable:     return EShLangCallable;
    }
    return EShLangVertex;
}

// HLSL profile for D3DCompile, e.g. (Vertex, 50) -> "vs_5_0". Returns "" if the stage has no
// SM5 profile (mesh/task/ray stages need SM6/DXC, which this compiler does not target).
std::string d3d_profile(ShaderStage s, int model) {
    const char* p = nullptr;
    switch (s) {
        case ShaderStage::Vertex:      p = "vs"; break;
        case ShaderStage::Fragment:    p = "ps"; break;
        case ShaderStage::Geometry:    p = "gs"; break;
        case ShaderStage::TessControl: p = "hs"; break;
        case ShaderStage::TessEval:    p = "ds"; break;
        case ShaderStage::Compute:     p = "cs"; break;
        default: return "";
    }
    int major = model / 10, minor = model % 10;
    return std::string(p) + "_" + std::to_string(major) + "_" + std::to_string(minor);
}

std::string build_preamble(const ShaderCompileOptions& o) {
    std::string pre;
    for (int i = 0; i < o.macro_count; ++i) {
        const ShaderMacro& m = o.macros[i];
        if (!m.name) continue;
        pre += "#define ";
        pre += m.name;
        pre += ' ';
        if (m.value) pre += m.value;
        pre += '\n';
    }
    return pre;
}

//=============================================================================
// #include resolution
//=============================================================================
std::vector<char> read_whole_file(const std::string& path) {
    std::vector<char> data;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) { data.resize((size_t)n); size_t got = std::fread(data.data(), 1, (size_t)n, f); data.resize(got); }
        std::fclose(f);
    }
    return data;
}

//=============================================================================
// Binary cache (content-hash keyed; <exe-dir>/cache/shader/<hash>.scb)
//=============================================================================
const uint32_t kCacheVersion = 1;   // bump when the compiler's output could change

// Directory of the running executable (where the cache lives -- a scratch folder beside the
// binary, never inside the project/working tree).
std::string executable_dir() {
    std::string path;
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof buf);
    if (n > 0) path.assign(buf, n);
#elif defined(__APPLE__)
    char buf[4096]; uint32_t sz = (uint32_t)sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) == 0) path = buf;
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf);
    if (n > 0) path.assign(buf, (size_t)n);
#endif
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Default cache directory: "<exe-dir>/cache/shader", resolved once.
const std::string& default_cache_dir() {
    static const std::string dir = executable_dir() + "/cache/shader";
    return dir;
}

uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ull) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

uint64_t cache_key(const void* src, size_t len, ShaderStage stage, const char* entry,
                   const ShaderCompileOptions& o) {
    uint64_t h = fnv1a(src, len);
    uint8_t st = (uint8_t)stage;        h = fnv1a(&st, 1, h);
    h = fnv1a(entry, std::strlen(entry), h);
    int tgt = (int)o.target;            h = fnv1a(&tgt, sizeof tgt, h);
    uint8_t flags = (o.optimize ? 1 : 0) | (o.debug_info ? 2 : 0) | (o.glsl_es ? 4 : 0);
    h = fnv1a(&flags, 1, h);
    h = fnv1a(&o.hlsl_model, sizeof o.hlsl_model, h);
    h = fnv1a(&o.glsl_version, sizeof o.glsl_version, h);
    for (int i = 0; i < o.macro_count; ++i) {
        if (o.macros[i].name)  h = fnv1a(o.macros[i].name,  std::strlen(o.macros[i].name),  h);
        if (o.macros[i].value) h = fnv1a(o.macros[i].value, std::strlen(o.macros[i].value), h);
    }
    uint32_t v = kCacheVersion;         h = fnv1a(&v, sizeof v, h);
    return h;
}

std::string cache_path(const std::string& dir, uint64_t key) {
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.scb", (unsigned long long)key);
    return dir + "/" + name;
}

// Cache file: "SCB1" | u32 version | u32 language | u32 size | u64 key | bytes  (24-byte header)
bool cache_load(const std::string& path, uint64_t key, ShaderCompileResult& r) {
    std::vector<char> f = read_whole_file(path);
    if (f.size() < 24 || std::memcmp(f.data(), "SCB1", 4) != 0) return false;
    uint32_t ver, lang, size; uint64_t k;
    std::memcpy(&ver,  f.data() + 4,  4);
    std::memcpy(&lang, f.data() + 8,  4);
    std::memcpy(&size, f.data() + 12, 4);
    std::memcpy(&k,    f.data() + 16, 8);
    if (ver != kCacheVersion || k != key || f.size() < 24 + (size_t)size) return false;
    r.bytecode.assign(f.begin() + 24, f.begin() + 24 + size);
    r.language = (ShaderLanguage)lang;
    r.ok = true;
    return true;
}

void cache_store(const std::string& dir, const std::string& path, uint64_t key, const ShaderCompileResult& r) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);   // best-effort; write still guarded below
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) return;
    uint32_t ver = kCacheVersion, lang = (uint32_t)r.language, size = (uint32_t)r.bytecode.size();
    std::fwrite("SCB1", 1, 4, fp);
    std::fwrite(&ver, 4, 1, fp); std::fwrite(&lang, 4, 1, fp); std::fwrite(&size, 4, 1, fp); std::fwrite(&key, 8, 1, fp);
    if (size) std::fwrite(r.bytecode.data(), 1, size, fp);
    std::fclose(fp);
}

// glslang includer that resolves headers under a single root directory.
class GlslangIncluder : public glslang::TShader::Includer {
public:
    explicit GlslangIncluder(const char* root) : root_(root ? root : "") {}

    IncludeResult* includeLocal(const char* header, const char*, size_t) override { return load(header); }
    IncludeResult* includeSystem(const char* header, const char*, size_t) override { return load(header); }
    void releaseInclude(IncludeResult* r) override {
        if (r) { delete[] static_cast<char*>(const_cast<void*>((const void*)r->headerData)); delete r; }
    }
private:
    IncludeResult* load(const char* header) {
        if (root_.empty() || !header) return nullptr;
        std::string path = root_ + "/" + header;
        std::vector<char> bytes = read_whole_file(path);
        if (bytes.empty()) return nullptr;
        char* buf = new char[bytes.size()];
        std::memcpy(buf, bytes.data(), bytes.size());
        return new IncludeResult(path, buf, bytes.size(), nullptr);
    }
    std::string root_;
};

//=============================================================================
// HLSL -> SPIR-V (glslang)
//=============================================================================
bool hlsl_to_spirv(const char* src, int len, ShaderStage stage, const char* entry,
                   const ShaderCompileOptions& o, std::vector<uint32_t>& spirv, std::string& log) {
    EShLanguage lang = to_esh(stage);
    glslang::TShader shader(lang);

    const char* strings[1] = { src };
    const int   lengths[1] = { len };
    const char* names[1]   = { o.source_name ? o.source_name : "shader" };
    shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);

    // Keep the original HLSL entry name in the SPIR-V (e.g. "vs_main"), matching the
    // prebuilt-blob convention (dxc -E vs_main): the Vulkan backend uses it as the stage's
    // entry, while the GLSL/DXBC paths ignore the name (GLSL is always main(); DXBC bakes it).
    shader.setSourceEntryPoint(entry);
    shader.setEntryPoint(entry);
    shader.setEnvInput(glslang::EShSourceHlsl, lang, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    shader.setHlslIoMapping(true);          // honour HLSL register(bN/tN/sN) as binding N
    shader.setAutoMapBindings(true);        // assign anything left unbound
    shader.setAutoMapLocations(true);

    std::string preamble = build_preamble(o);
    if (!preamble.empty()) shader.setPreamble(preamble.c_str());

    const EShMessages msgs = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules |
                                           EShMsgReadHlsl | EShMsgHlslOffsets);
    GlslangIncluder includer(o.include_dir);
    const TBuiltInResource* res = GetDefaultResources();

    if (!shader.parse(res, 100, false, msgs, includer)) {
        log += shader.getInfoLog();
        log += shader.getInfoDebugLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msgs)) {
        log += program.getInfoLog();
        log += program.getInfoDebugLog();
        return false;
    }
    program.mapIO();   // resolve HLSL register() -> SPIR-V bindings

    glslang::TIntermediate* inter = program.getIntermediate(lang);
    if (!inter) { log += "glslang: no intermediate for stage\n"; return false; }

    glslang::SpvOptions spv_opts;
    spv_opts.disableOptimizer = !o.optimize;
    spv_opts.generateDebugInfo = o.debug_info;
    spv_opts.stripDebugInfo = !o.debug_info;
    spv::SpvBuildLogger spv_logger;
    glslang::GlslangToSpv(*inter, spirv, &spv_logger, &spv_opts);
    const std::string m = spv_logger.getAllMessages();
    if (!m.empty()) log += m;
    return !spirv.empty();
}

//=============================================================================
// SPIR-V -> GLSL / MSL (SPIRV-Cross)
//=============================================================================
// Pin each generated combined image/sampler to its source image's binding so a sampler
// lands on the same GL texture unit the RHI binds the texture to.
void pin_combined_samplers(spirv_cross::CompilerGLSL& comp) {
    comp.build_combined_image_samplers();
    for (const auto& r : comp.get_combined_image_samplers()) {
        uint32_t binding = comp.get_decoration(r.image_id, spv::DecorationBinding);
        comp.set_decoration(r.combined_id, spv::DecorationBinding, binding);
    }
}

bool spirv_to_glsl(const std::vector<uint32_t>& spirv, const ShaderCompileOptions& o,
                   std::string& out, std::string& log) {
    try {
        spirv_cross::CompilerGLSL comp(spirv);
        spirv_cross::CompilerGLSL::Options opts;
        opts.version = (uint32_t)o.glsl_version;
        opts.es = o.glsl_es;
        opts.enable_420pack_extension = true;   // emit layout(binding=) qualifiers
        comp.set_common_options(opts);
        pin_combined_samplers(comp);
        out = comp.compile();
        return !out.empty();
    } catch (const std::exception& e) {
        log += "SPIRV-Cross (GLSL): "; log += e.what(); log += "\n";
        return false;
    }
}

bool spirv_to_msl(const std::vector<uint32_t>& spirv, std::string& out, std::string& log) {
    try {
        spirv_cross::CompilerMSL comp(spirv);
        out = comp.compile();
        return !out.empty();
    } catch (const std::exception& e) {
        log += "SPIRV-Cross (MSL): "; log += e.what(); log += "\n";
        return false;
    }
}

//=============================================================================
// HLSL -> DXBC (d3dcompiler, Windows)
//=============================================================================
#ifdef _WIN32
class D3DIncluder : public ID3DInclude {
public:
    explicit D3DIncluder(const char* root) : root_(root ? root : "") {}
    HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) override {
        if (root_.empty() || !name) return E_FAIL;
        std::vector<char> file = read_whole_file(root_ + "/" + name);
        if (file.empty()) return E_FAIL;
        char* buf = new char[file.size()];
        std::memcpy(buf, file.data(), file.size());
        *data = buf; *bytes = (UINT)file.size();
        return S_OK;
    }
    HRESULT __stdcall Close(LPCVOID data) override { delete[] static_cast<const char*>(data); return S_OK; }
private:
    std::string root_;
};

bool hlsl_to_dxbc(const char* src, size_t len, ShaderStage stage, const char* entry,
                  const ShaderCompileOptions& o, std::vector<uint8_t>& out, std::string& log) {
    std::string profile = d3d_profile(stage, o.hlsl_model);
    if (profile.empty()) { log += "d3dcompiler: stage has no SM5 profile (needs DXC/SM6)\n"; return false; }

    std::vector<D3D_SHADER_MACRO> defines;
    for (int i = 0; i < o.macro_count; ++i)
        defines.push_back({ o.macros[i].name, o.macros[i].value });
    defines.push_back({ nullptr, nullptr });

    UINT flags = 0;
    if (o.optimize) flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    else            flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
    if (o.debug_info) flags |= D3DCOMPILE_DEBUG;

    D3DIncluder includer(o.include_dir);
    ID3DInclude* inc = o.include_dir ? static_cast<ID3DInclude*>(&includer)
                                     : D3D_COMPILE_STANDARD_FILE_INCLUDE;

    ID3DBlob* code = nullptr;
    ID3DBlob* errs = nullptr;
    HRESULT hr = D3DCompile(src, len, o.source_name, defines.data(), inc,
                            entry, profile.c_str(), flags, 0, &code, &errs);
    if (errs) {
        log.append(static_cast<const char*>(errs->GetBufferPointer()), errs->GetBufferSize());
        errs->Release();
    }
    if (FAILED(hr) || !code) {
        if (code) code->Release();
        log += "d3dcompiler: D3DCompile failed\n";
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(code->GetBufferPointer());
    out.assign(p, p + code->GetBufferSize());
    code->Release();
    return true;
}
#endif // _WIN32

} // namespace

//=============================================================================
// Public API
//=============================================================================
bool ShaderCompiler::available() { return true; }
bool ShaderCompiler::initialize() { return glslang_acquire(); }
void ShaderCompiler::shutdown()   { glslang_release(); }

ShaderCompileResult ShaderCompiler::compile(const void* source, size_t size,
                                            ShaderStage stage, const char* entry,
                                            const ShaderCompileOptions& opts) {
    ShaderCompileResult result;
    if (!source) { result.log = "shader compile: null source\n"; return result; }
    if (!entry || !*entry) entry = "main";
    if (opts.source_lang != ShaderSourceLang::HLSL) {
        result.log = "shader compile: only HLSL source is supported\n";
        return result;
    }

    const char* src = static_cast<const char*>(source);
    const size_t len = size ? size : std::strlen(src);

    Backend target = opts.target;
    result.language = shader_blob_language(target);

    switch (target) {
        case Backend::Vulkan: {
            GlslangScope gs;
            if (!gs.ok) { result.log += "glslang: InitializeProcess failed\n"; return result; }
            std::vector<uint32_t> spirv;
            if (!hlsl_to_spirv(src, (int)len, stage, entry, opts, spirv, result.log)) return result;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(spirv.data());
            result.bytecode.assign(p, p + spirv.size() * sizeof(uint32_t));
            result.ok = true;
            return result;
        }
        case Backend::OpenGL: {
            GlslangScope gs;
            if (!gs.ok) { result.log += "glslang: InitializeProcess failed\n"; return result; }
            std::vector<uint32_t> spirv;
            if (!hlsl_to_spirv(src, (int)len, stage, entry, opts, spirv, result.log)) return result;
            std::string glsl;
            if (!spirv_to_glsl(spirv, opts, glsl, result.log)) return result;
            result.bytecode.assign(glsl.begin(), glsl.end());
            result.ok = true;
            return result;
        }
        case Backend::Metal: {
            GlslangScope gs;
            if (!gs.ok) { result.log += "glslang: InitializeProcess failed\n"; return result; }
            std::vector<uint32_t> spirv;
            if (!hlsl_to_spirv(src, (int)len, stage, entry, opts, spirv, result.log)) return result;
            std::string msl;
            if (!spirv_to_msl(spirv, msl, result.log)) return result;
            result.bytecode.assign(msl.begin(), msl.end());
            result.ok = true;
            return result;
        }
        case Backend::D3D11:
        case Backend::D3D12: {
#ifdef _WIN32
            if (!hlsl_to_dxbc(src, len, stage, entry, opts, result.bytecode, result.log)) return result;
            result.ok = true;
            return result;
#else
            result.log += "DXBC requires d3dcompiler (Windows only)\n";
            return result;
#endif
        }
        default:
            result.log += "shader compile: unsupported target backend\n";
            return result;
    }
}

bool ShaderCompiler::reflect(const void* source, size_t size, ShaderStage stage, const char* entry,
                             ShaderReflection* out, const ShaderCompileOptions& opts) {
    if (!out || !source) return false;
    if (!entry || !*entry) entry = "main";
    if (opts.source_lang != ShaderSourceLang::HLSL) return false;

    const char* src = static_cast<const char*>(source);
    const size_t len = size ? size : std::strlen(src);

    GlslangScope gs;
    if (!gs.ok) return false;
    std::vector<uint32_t> spirv; std::string log;
    if (!hlsl_to_spirv(src, (int)len, stage, entry, opts, spirv, log) || spirv.empty()) return false;

    try {
        spirv_cross::CompilerGLSL comp(std::move(spirv));
        const spirv_cross::ShaderResources res = comp.get_shader_resources();
        for (const auto& ub : res.uniform_buffers) {
            const spirv_cross::SPIRType& type = comp.get_type(ub.base_type_id);
            ShaderUniformBlock blk;
            blk.name    = comp.get_name(ub.id);
            if (blk.name.empty()) blk.name = comp.get_name(ub.base_type_id);
            blk.set     = comp.get_decoration(ub.id, spv::DecorationDescriptorSet);
            blk.binding = comp.get_decoration(ub.id, spv::DecorationBinding);
            blk.size    = (uint32_t)comp.get_declared_struct_size(type);
            for (uint32_t i = 0; i < (uint32_t)type.member_types.size(); ++i) {
                ShaderUniformMember m;
                m.name   = comp.get_member_name(ub.base_type_id, i);
                m.offset = comp.type_struct_member_offset(type, i);
                m.size   = (uint32_t)comp.get_declared_struct_member_size(type, i);
                blk.members.push_back(std::move(m));
            }
            out->uniform_blocks.push_back(std::move(blk));
        }
        return true;
    } catch (const std::exception& e) {
        (void)e;
        return false;
    }
}

ShaderHandle ShaderCompiler::compile_and_create(GraphicDevice* device,
                                                const void* source, size_t size,
                                                ShaderStage stage, const char* entry,
                                                ShaderCompileOptions opts,
                                                ShaderCompileResult* out) {
    ShaderHandle invalid;
    if (!device) return invalid;
    if (opts.target == Backend::Auto) opts.target = device->get_backend();

    ShaderCompileResult r = compile(source, size, stage, entry, opts);
    if (out) *out = r;
    if (!r.ok) return invalid;

    // The SPIR-V keeps `entry` as its entry point name (Vulkan uses it); GLSL/DXBC ignore it.
    ShaderDesc desc = r.to_desc(stage, entry);
    return device->create_shader(desc);
}

ShaderCompileResult ShaderCompiler::compile_cached(const void* source, size_t size,
                                                   ShaderStage stage, const char* entry,
                                                   const ShaderCompileOptions& opts,
                                                   const char* cache_dir) {
    if (!entry || !*entry) entry = "main";
    if (!source) { ShaderCompileResult r; r.log = "shader compile: null source\n"; return r; }
    const std::string dir = cache_dir ? std::string(cache_dir) : default_cache_dir();
    const size_t len = size ? size : std::strlen(static_cast<const char*>(source));

    uint64_t key = cache_key(source, len, stage, entry, opts);
    std::string path = cache_path(dir, key);

    ShaderCompileResult r;
    if (cache_load(path, key, r)) return r;        // hit: skip compilation

    r = compile(source, size, stage, entry, opts); // miss: compile + persist
    if (r.ok) cache_store(dir, path, key, r);
    return r;
}

ShaderHandle ShaderCompiler::compile_and_create_cached(GraphicDevice* device,
                                                       const void* source, size_t size,
                                                       ShaderStage stage, const char* entry,
                                                       ShaderCompileOptions opts,
                                                       ShaderCompileResult* out,
                                                       const char* cache_dir) {
    ShaderHandle invalid;
    if (!device) return invalid;
    if (opts.target == Backend::Auto) opts.target = device->get_backend();
    if (!entry || !*entry) entry = "main";

    ShaderCompileResult r = compile_cached(source, size, stage, entry, opts, cache_dir);
    if (out) *out = r;
    if (!r.ok) return invalid;

    ShaderDesc desc = r.to_desc(stage, entry);
    return device->create_shader(desc);
}

} // namespace gfx
} // namespace window

#else  // !WINDOW_SUPPORT_SHADER_COMPILER -- graceful stubs

namespace window {
namespace gfx {

bool ShaderCompiler::available() { return false; }
bool ShaderCompiler::initialize() { return false; }
void ShaderCompiler::shutdown() {}

ShaderCompileResult ShaderCompiler::compile(const void*, size_t, ShaderStage, const char*,
                                            const ShaderCompileOptions&) {
    ShaderCompileResult r;
    r.log = "shader compiler not built (enable WINDOW_ENABLE_SHADER_COMPILER)\n";
    return r;
}
bool ShaderCompiler::reflect(const void*, size_t, ShaderStage, const char*,
                             ShaderReflection*, const ShaderCompileOptions&) {
    return false;
}
ShaderHandle ShaderCompiler::compile_and_create(GraphicDevice*, const void*, size_t,
                                                ShaderStage, const char*,
                                                ShaderCompileOptions, ShaderCompileResult* out) {
    if (out) *out = compile(nullptr, 0, ShaderStage::Vertex, nullptr, {});
    return ShaderHandle{};
}
ShaderCompileResult ShaderCompiler::compile_cached(const void*, size_t, ShaderStage, const char*,
                                                   const ShaderCompileOptions&, const char*) {
    return compile(nullptr, 0, ShaderStage::Vertex, nullptr, {});
}
ShaderHandle ShaderCompiler::compile_and_create_cached(GraphicDevice*, const void*, size_t,
                                                       ShaderStage, const char*,
                                                       ShaderCompileOptions, ShaderCompileResult* out,
                                                       const char*) {
    if (out) *out = compile(nullptr, 0, ShaderStage::Vertex, nullptr, {});
    return ShaderHandle{};
}

} // namespace gfx
} // namespace window

#endif // WINDOW_SUPPORT_SHADER_COMPILER
