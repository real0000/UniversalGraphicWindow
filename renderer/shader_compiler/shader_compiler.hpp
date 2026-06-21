#pragma once
// renderer/shader_compiler/shader_compiler.hpp
//
// The renderer's built-in shader compiler: turn HLSL source into whatever blob the active
// backend's create_shader() wants, at runtime (or offline through the CLI), so consumers no
// longer have to hand-bake per-backend shader artifacts.
//
//   target backend        output blob (ShaderLanguage)     tool
//   --------------------   ------------------------------   --------------------------------
//   Vulkan                 SPIR-V                           glslang (HLSL -> SPIR-V)
//   OpenGL                 GLSL (text) [or SPIR-V on 4.6]   glslang -> SPIRV-Cross
//   Direct3D 11 / 12       DXBC                             d3dcompiler (D3DCompile, Windows)
//   Metal                  MSL (text)                       glslang -> SPIRV-Cross
//
// Input is HLSL (the language the rest of renderer/shaders is authored in). The SPIR-V path
// reads register(bN)/(tN)/(sN) as binding N (no [[vk::binding]] needed -- do NOT define
// VK_SPIRV for this path), matching how the RHI maps a binding straight to the per-type slot.
// The OpenGL path then runs SPIRV-Cross with combined image/samplers whose binding is pinned
// to the source texture's slot, so a generated sampler lands on the same GL texture unit.
//
// DXBC is produced by the Windows system d3dcompiler (FXC) -- the only producer of DXBC, it
// has no source form. DXC/DXIL is deliberately not used: both D3D backends consume DXBC here.
//
// Build-gated by WINDOW_SUPPORT_SHADER_COMPILER (CMake WINDOW_ENABLE_SHADER_COMPILER). When
// the library is built without it, available() returns false and compile() fails gracefully
// so callers can fall back to prebuilt blobs.
//
// Header-declared / cpp-defined, namespace window::gfx, matching the rest of renderer/.

#include "../../graphics_api.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace window {
namespace gfx {

// Input source language. HLSL is the primary (and currently only) front end.
enum class ShaderSourceLang : uint8_t { HLSL };

// A preprocessor define handed to the compiler (value may be null for a bare -D NAME).
struct ShaderMacro {
    const char* name  = nullptr;
    const char* value = nullptr;
};

// Knobs for one compile. Defaults compile HLSL for the device's own backend, optimized.
struct ShaderCompileOptions {
    ShaderSourceLang   source_lang = ShaderSourceLang::HLSL;
    Backend            target      = Backend::Auto;   // Auto: set by compile_and_create() from the device
    bool               optimize    = true;            // SPIR-V: glslang optimizer hints; DXBC: D3DCOMPILE_OPTIMIZATION
    bool               debug_info  = false;           // keep debug info / names
    int                hlsl_model  = 50;              // DXBC profile: 50 = SM5.0 (D3D11+D3D12), 51 = SM5.1
    int                glsl_version = 460;            // OpenGL GLSL #version for the SPIRV-Cross path
    bool               glsl_es     = false;           // emit ES GLSL (mobile/WebGL)
    const ShaderMacro* macros      = nullptr;
    int                macro_count = 0;
    const char*        source_name = "shader";        // shown in diagnostics
    const char*        include_dir = nullptr;         // root for #include resolution (optional)
};

// Result of one compile: the blob plus the language tag to feed ShaderDesc, and any log.
struct ShaderCompileResult {
    bool                 ok       = false;
    ShaderLanguage       language = ShaderLanguage::SPIRV;  // language of `bytecode`
    std::vector<uint8_t> bytecode;                          // SPIR-V words / DXBC / GLSL or MSL text
    std::string          log;                               // warnings + errors (empty on a clean success)

    // Build a ShaderDesc that points at this blob (kept alive by this result).
    ShaderDesc to_desc(ShaderStage stage, const char* entry_point = "main") const {
        ShaderDesc d;
        d.stage       = stage;
        d.language    = language;
        d.code        = bytecode.data();
        d.code_size   = bytecode.size();
        d.entry_point = entry_point;
        return d;
    }
};

// The compiler is a stateless facade over glslang / SPIRV-Cross / d3dcompiler. glslang needs
// a process-global init; it is reference-counted and taken lazily, but you may bracket heavy
// use with initialize()/shutdown() to control when it happens.
class ShaderCompiler {
public:
    // True if the library was built with the compiler (WINDOW_SUPPORT_SHADER_COMPILER).
    static bool available();

    // Optional explicit lifetime control for glslang's process init. compile() also inits
    // lazily, so these are only needed to pin the timing. Ref-counted: balance the calls.
    static bool initialize();
    static void shutdown();

    // Compile `source` (`size` bytes; size 0 means `source` is a NUL-terminated string) for
    // `stage`/`entry` into the target backend's blob. Pure data in/out -- no device required,
    // so it also drives the offline CLI. On failure returns ok=false with `log` populated.
    static ShaderCompileResult compile(const void* source, size_t size,
                                       ShaderStage stage, const char* entry,
                                       const ShaderCompileOptions& opts = {});

    // Convenience: compile for `device`'s backend (opts.target is overridden to it) and create
    // the shader. Returns an invalid handle on failure; pass `out` to inspect the log/blob.
    static ShaderHandle compile_and_create(GraphicDevice* device,
                                           const void* source, size_t size,
                                           ShaderStage stage, const char* entry,
                                           ShaderCompileOptions opts = {},
                                           ShaderCompileResult* out = nullptr);

    // ---- Cached variants ----------------------------------------------------
    // Compile through an on-disk binary cache. The blob is keyed by a hash of the source +
    // stage + entry + options (NOT a timestamp), so it is reused until any of those change --
    // covering both "cache is stale" (source/options changed -> new key -> miss) and "cache is
    // missing" with one mechanism. Cache files live in `cache_dir` (default "cache/shader" beside
    // the executable, not in the working tree) as <hash>.scb. opts.target must be concrete (not Auto).
    static ShaderCompileResult compile_cached(const void* source, size_t size,
                                              ShaderStage stage, const char* entry,
                                              const ShaderCompileOptions& opts = {},
                                              const char* cache_dir = nullptr);

    // compile_and_create() backed by the binary cache: the first run for a given source
    // compiles + caches; later runs load the blob and skip compilation entirely. This is the
    // path the engine's own shaders use (author HLSL once, compile lazily, cache on disk).
    static ShaderHandle compile_and_create_cached(GraphicDevice* device,
                                                  const void* source, size_t size,
                                                  ShaderStage stage, const char* entry,
                                                  ShaderCompileOptions opts = {},
                                                  ShaderCompileResult* out = nullptr,
                                                  const char* cache_dir = nullptr);
};

// The blob language the compiler emits for a backend (e.g. Vulkan -> SPIRV, D3D11 -> DXBC).
ShaderLanguage shader_blob_language(Backend backend);

} // namespace gfx
} // namespace window
