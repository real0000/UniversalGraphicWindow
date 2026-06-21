// shaderc - offline driver for the renderer's built-in shader compiler (gfx::ShaderCompiler).
//
// Same code path as the runtime API, used at build time to pre-bake blobs (so the engine can
// ship compiled shaders without anyone running dxc/glslc/spirv-cross by hand). Authoring is
// HLSL; one source produces every backend's blob.
//
//   shaderc <input.hlsl> --stage <vs|ps|cs|gs|hs|ds> [options]
//     --entry <name>       entry point (default: main)
//     --target <t>         spirv | glsl | dxbc | msl | all   (default: all)
//     --out <path>         output file; for --target all, the base path (suffix added)
//     --include <dir>      #include search root
//     -D NAME[=VALUE]      preprocessor define (repeatable)
//     --glsl-version <n>   GLSL #version for the glsl target (default 460)
//     --sm <n>             HLSL shader model for dxbc, e.g. 50 or 51 (default 50)
//     --debug              keep debug info, skip optimization
//
// Exit code 0 on success, non-zero on any failure. Writes one file per emitted target.

#include "../renderer/shader_compiler/shader_compiler.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace window;
using namespace window::gfx;

static ShaderStage parse_stage(const std::string& s, bool* ok) {
    *ok = true;
    if (s == "vs" || s == "vertex")               return ShaderStage::Vertex;
    if (s == "ps" || s == "fs" || s == "fragment" || s == "pixel") return ShaderStage::Fragment;
    if (s == "cs" || s == "compute")              return ShaderStage::Compute;
    if (s == "gs" || s == "geometry")             return ShaderStage::Geometry;
    if (s == "hs" || s == "tesscontrol" || s == "hull")   return ShaderStage::TessControl;
    if (s == "ds" || s == "tesseval" || s == "domain")    return ShaderStage::TessEval;
    *ok = false;
    return ShaderStage::Vertex;
}

// (Backend used to select the output format, file-suffix, ShaderLanguage label)
struct TargetSpec { Backend backend; const char* suffix; const char* name; };
static const TargetSpec kAllTargets[] = {
    { Backend::Vulkan, ".spv",  "spirv" },
    { Backend::OpenGL, ".glsl", "glsl"  },
    { Backend::D3D11,  ".dxbc", "dxbc"  },
    { Backend::Metal,  ".msl",  "msl"   },
};

static const TargetSpec* find_target(const std::string& t) {
    for (const TargetSpec& s : kAllTargets) if (t == s.name) return &s;
    return nullptr;
}

static std::vector<char> read_file(const char* path) {
    std::vector<char> data;
    FILE* f = std::fopen(path, "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n > 0) { data.resize((size_t)n); size_t got = std::fread(data.data(), 1, (size_t)n, f); data.resize(got); }
    std::fclose(f);
    return data;
}
static bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}
static std::string strip_ext(const std::string& p) {
    size_t dot = p.find_last_of('.'); size_t slash = p.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) return p.substr(0, dot);
    return p;
}

int main(int argc, char** argv) {
    if (!ShaderCompiler::available()) {
        std::fprintf(stderr, "shaderc: built without WINDOW_ENABLE_SHADER_COMPILER\n");
        return 2;
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: shaderc <input.hlsl> --stage <vs|ps|cs|...> [--target spirv|glsl|dxbc|msl|all]\n"
                             "               [--entry name] [--out path] [--include dir] [-D NAME[=VAL]]\n"
                             "               [--glsl-version n] [--sm n] [--debug]\n");
        return 2;
    }

    const char* input = argv[1];
    std::string stage_str, entry = "main", target = "all", out, include_dir;
    int glsl_version = 460, sm = 50; bool debug = false;
    std::vector<std::string> define_storage;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "shaderc: %s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if      (a == "--stage")        stage_str = next("--stage");
        else if (a == "--entry")        entry = next("--entry");
        else if (a == "--target")       target = next("--target");
        else if (a == "--out" || a == "-o") out = next("--out");
        else if (a == "--include" || a == "-I") include_dir = next("--include");
        else if (a == "--glsl-version") glsl_version = std::atoi(next("--glsl-version"));
        else if (a == "--sm")           sm = std::atoi(next("--sm"));
        else if (a == "--debug")        debug = true;
        else if (a == "-D")             define_storage.push_back(next("-D"));
        else { std::fprintf(stderr, "shaderc: unknown arg '%s'\n", a.c_str()); return 2; }
    }
    if (stage_str.empty()) { std::fprintf(stderr, "shaderc: --stage is required\n"); return 2; }

    bool stage_ok = false;
    ShaderStage stage = parse_stage(stage_str, &stage_ok);
    if (!stage_ok) { std::fprintf(stderr, "shaderc: bad --stage '%s'\n", stage_str.c_str()); return 2; }

    std::vector<char> src = read_file(input);
    if (src.empty()) { std::fprintf(stderr, "shaderc: cannot read '%s'\n", input); return 1; }

    // Split "NAME=VALUE" defines into name/value pairs (storage kept alive in vectors).
    std::vector<std::string> names, values;
    for (const std::string& d : define_storage) {
        size_t eq = d.find('=');
        names.push_back(eq == std::string::npos ? d : d.substr(0, eq));
        values.push_back(eq == std::string::npos ? std::string() : d.substr(eq + 1));
    }
    std::vector<ShaderMacro> macros;
    for (size_t i = 0; i < names.size(); ++i)
        macros.push_back({ names[i].c_str(), values[i].empty() ? nullptr : values[i].c_str() });

    // Targets to emit.
    std::vector<const TargetSpec*> targets;
    if (target == "all") { for (const TargetSpec& s : kAllTargets) targets.push_back(&s); }
    else {
        const TargetSpec* t = find_target(target);
        if (!t) { std::fprintf(stderr, "shaderc: bad --target '%s'\n", target.c_str()); return 2; }
        targets.push_back(t);
    }

    const std::string base = out.empty() ? (strip_ext(input) + "." + entry) : strip_ext(out);

    ShaderCompiler::initialize();
    int failures = 0, written = 0;
    for (const TargetSpec* t : targets) {
        ShaderCompileOptions o;
        o.target       = t->backend;
        o.optimize     = !debug;
        o.debug_info   = debug;
        o.hlsl_model   = sm;
        o.glsl_version = glsl_version;
        o.macros       = macros.empty() ? nullptr : macros.data();
        o.macro_count  = (int)macros.size();
        o.source_name  = input;
        o.include_dir  = include_dir.empty() ? nullptr : include_dir.c_str();

        ShaderCompileResult r = ShaderCompiler::compile(src.data(), src.size(), stage, entry.c_str(), o);
        if (!r.ok) {
            std::fprintf(stderr, "shaderc: [%s] FAILED\n%s\n", t->name, r.log.c_str());
            ++failures;
            continue;
        }
        // For an explicit single --out the user's path wins; otherwise base + suffix.
        std::string path = (!out.empty() && targets.size() == 1) ? out : (base + t->suffix);
        if (!write_file(path, r.bytecode)) {
            std::fprintf(stderr, "shaderc: cannot write '%s'\n", path.c_str());
            ++failures;
            continue;
        }
        std::printf("shaderc: %-5s -> %s (%zu bytes)\n", t->name, path.c_str(), r.bytecode.size());
        if (!r.log.empty()) std::fprintf(stderr, "  [%s log] %s\n", t->name, r.log.c_str());
        ++written;
    }
    ShaderCompiler::shutdown();

    if (failures) { std::fprintf(stderr, "shaderc: %d target(s) failed\n", failures); return 1; }
    return written ? 0 : 1;
}
