#include "material.hpp"

#ifdef WINDOW_SUPPORT_SHADER_COMPILER
#include "shader_compiler/shader_compiler.hpp"
#endif

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace window {
namespace gfx {
namespace {

VertexFormat parse_vertex_format(const std::string& s) {
    if (s == "float1") return VertexFormat::Float1;
    if (s == "float2") return VertexFormat::Float2;
    if (s == "float3") return VertexFormat::Float3;
    if (s == "float4") return VertexFormat::Float4;
    if (s == "ubyte4n") return VertexFormat::UByte4N;
    if (s == "half2")  return VertexFormat::Half2;
    if (s == "half4")  return VertexFormat::Half4;
    return VertexFormat::Float4;
}
MaterialParamType parse_param_type(const std::string& s) {
    if (s == "uniform_buffer")        return MaterialParamType::UniformBuffer;
    if (s == "sampled_texture")       return MaterialParamType::SampledTexture;
    if (s == "sampler")               return MaterialParamType::Sampler;
    if (s == "combined_image_sampler")return MaterialParamType::CombinedImageSampler;
    if (s == "storage_buffer")        return MaterialParamType::StorageBuffer;
    if (s == "storage_texture")       return MaterialParamType::StorageTexture;
    return MaterialParamType::UniformBuffer;
}
uint32_t parse_stage(const std::string& s) {
    if (s == "vertex")   return STAGE_VERTEX;
    if (s == "fragment" || s == "pixel") return STAGE_FRAGMENT;
    if (s == "geometry") return STAGE_GEOMETRY;
    if (s == "compute")  return STAGE_COMPUTE;
    if (s == "mesh")     return STAGE_MESH;
    if (s == "task")     return STAGE_TASK;
    return STAGE_ALL;
}
// Map a [shader] config key to a ShaderStage (covers all stages incl. ray tracing).
// Returns -1 for non-stage keys (e.g. "source"). Accepts HLSL-ish aliases.
int stage_from_key(const std::string& k) {
    if (k == "vertex")                     return int(ShaderStage::Vertex);
    if (k == "fragment" || k == "pixel")   return int(ShaderStage::Fragment);
    if (k == "geometry")                   return int(ShaderStage::Geometry);
    if (k == "hull" || k == "tess_control")return int(ShaderStage::TessControl);
    if (k == "domain" || k == "tess_eval") return int(ShaderStage::TessEval);
    if (k == "compute")                    return int(ShaderStage::Compute);
    if (k == "task" || k == "amplification") return int(ShaderStage::Task);
    if (k == "mesh")                       return int(ShaderStage::Mesh);
    if (k == "raygen")                     return int(ShaderStage::RayGen);
    if (k == "miss")                       return int(ShaderStage::Miss);
    if (k == "closesthit")                 return int(ShaderStage::ClosestHit);
    if (k == "anyhit")                     return int(ShaderStage::AnyHit);
    if (k == "intersection")               return int(ShaderStage::Intersection);
    if (k == "callable")                   return int(ShaderStage::Callable);
    return -1;
}
BindingType to_binding_type(MaterialParamType t) {
    switch (t) {
        case MaterialParamType::UniformBuffer:        return BindingType::UniformBuffer;
        case MaterialParamType::SampledTexture:       return BindingType::SampledTexture;
        case MaterialParamType::Sampler:              return BindingType::Sampler;
        case MaterialParamType::CombinedImageSampler: return BindingType::CombinedImageSampler;
        case MaterialParamType::StorageBuffer:        return BindingType::StorageBuffer;
        case MaterialParamType::StorageTexture:       return BindingType::StorageTexture;
    }
    return BindingType::UniformBuffer;
}
// "a:b:c[:d]" → tokens
std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; size_t i = 0;
    while (i <= s.size()) { size_t j = s.find(d, i); if (j == std::string::npos) j = s.size(); out.push_back(s.substr(i, j - i)); i = j + 1; }
    return out;
}
std::string base_name(const std::string& file) { size_t dot = file.find_last_of('.'); return dot == std::string::npos ? file : file.substr(0, dot); }
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamsize n = f.tellg(); f.seekg(0);
    std::vector<uint8_t> buf(size_t(n < 0 ? 0 : n));
    if (!buf.empty()) f.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}

// Load the shader blob for `device`'s backend. Returns the bytes + the language to
// hand to create_shader. The build emits the per-backend artifacts next to the HLSL.
bool load_shader_blob(Backend backend, const std::string& dir, const MaterialDesc& d,
                      const std::string& entry, std::vector<uint8_t>& bytes, ShaderLanguage& lang) {
    const std::string base = dir + "/" + base_name(d.shader_source);
    switch (backend) {
        case Backend::Vulkan:
            bytes = read_file(base + "." + entry + ".spv"); lang = ShaderLanguage::SPIRV; break;
        case Backend::OpenGL: {
            // Prefer a GLSL blob (compiles on any GL); fall back to SPIR-V, which needs
            // GL 4.6 + GL_ARB_gl_spirv (absent on plain/GLES/ANGLE contexts).
            bytes = read_file(base + "." + entry + ".glsl"); lang = ShaderLanguage::GLSL;
            if (bytes.empty()) { bytes = read_file(base + "." + entry + ".spv"); lang = ShaderLanguage::SPIRV; }
            break;
        }
        case Backend::Metal:
            bytes = read_file(base + "." + entry + ".msl"); lang = ShaderLanguage::MSL; break;
        case Backend::D3D11: case Backend::D3D12:
            // DXBC (fxc, SM5) sits alongside the HLSL; works on both D3D11 and D3D12
            // (D3D12 also accepts DXIL). The backends consume compiled bytecode, not source.
            bytes = read_file(base + "." + entry + ".dxbc"); lang = ShaderLanguage::DXBC; break;
        default:
            return false;
    }
    return !bytes.empty();
}

#ifdef WINDOW_SUPPORT_SHADER_COMPILER
// The "one language, cross-API" path: compile the material's single HLSL source for the
// active backend (pbr.hlsl -> SPIR-V / GLSL / DXBC / MSL) with the built-in compiler. The
// .hlsl is the source of truth; prebuilt blobs are only a fallback when it is absent. The
// shader directory doubles as the #include root (so e.g. lights.hlsli resolves).
bool compile_shader_blob(Backend backend, const std::string& dir, const MaterialDesc& d,
                         ShaderStage stage, const std::string& entry,
                         std::vector<uint8_t>& bytes, ShaderLanguage& lang) {
    const std::string src_path = dir + "/" + d.shader_source;
    std::vector<uint8_t> src = read_file(src_path);
    if (src.empty()) return false;   // no HLSL source on disk -> fall back to a prebuilt blob

    ShaderCompileOptions o;
    o.target      = backend;
    o.source_name = d.shader_source.c_str();
    o.include_dir = dir.c_str();
    ShaderCompileResult r = ShaderCompiler::compile(src.data(), src.size(), stage, entry.c_str(), o);
    if (!r.ok) {
        std::fprintf(stderr, "[Material] compile %s (%s) for backend %d:\n%s\n",
                     d.shader_source.c_str(), entry.c_str(), int(backend), r.log.c_str());
        return false;
    }
    bytes = std::move(r.bytecode);
    lang  = r.language;
    return true;
}
#endif // WINDOW_SUPPORT_SHADER_COMPILER

} // namespace

bool MaterialDesc::load(const char* path, MaterialDesc* out) {
    if (!path || !out) return false;
    boost::property_tree::ptree pt;
    try { boost::property_tree::ini_parser::read_ini(path, pt); }
    catch (const std::exception& e) { std::fprintf(stderr, "[Material] parse %s: %s\n", path, e.what()); return false; }

    // [shader]: source + one entry per stage. Keys are stage names (vertex, pixel,
    // geometry, hull, domain, compute, task, mesh, raygen, miss, closesthit, anyhit,
    // intersection, callable); "<stage>_entry" aliases are accepted too.
    if (auto sh = pt.get_child_optional("shader")) {
        for (auto& kv : *sh) {
            std::string key = kv.first;
            if (key == "source") { out->shader_source = kv.second.get_value<std::string>(); continue; }
            const std::string alias = key.size() > 6 && key.compare(key.size() - 6, 6, "_entry") == 0 ? key.substr(0, key.size() - 6) : key;
            int st = stage_from_key(alias);
            if (st >= 0) out->entry[st] = kv.second.get_value<std::string>();
        }
    }

    const std::string blend = pt.get<std::string>("state.blend", "none");
    if (blend == "alpha") out->blend = BlendState::alpha_blend();
    else                  out->blend.enabled = (blend == "additive");
    out->depth_stencil = pt.get<bool>("state.depth_test", false) ? DepthStencilState::depth_test() : DepthStencilState::disabled();
    const std::string cull = pt.get<std::string>("state.cull", "none");
    out->rasterizer = (cull == "none") ? RasterizerState::no_cull() : RasterizerState::default_state();
    out->rasterizer.scissor_enable = true;
    out->topology = PrimitiveTopology::TriangleList;  // (only triangles needed so far)
    out->patch_control_points = (uint32_t)pt.get<int>("state.patch_control_points", 0);  // >0 → tessellation

    // Vertex layout: "<loc> = <format>:<offset>:<slot>", plus "stride = N".
    VertexLayout& vl = out->vertex_layout; vl.attribute_count = 0;
    if (auto vlsec = pt.get_child_optional("vertex_layout")) {
        for (auto& kv : *vlsec) {
            if (kv.first == "stride") { vl.strides[0] = (uint32_t)std::stoul(kv.second.get_value<std::string>()); continue; }
            auto t = split(kv.second.get_value<std::string>(), ':');
            if (t.size() < 3) continue;
            VertexAttribute a; a.location = (uint32_t)std::stoul(kv.first);
            a.format = parse_vertex_format(t[0]); a.offset = (uint32_t)std::stoul(t[1]); a.buffer_slot = (uint32_t)std::stoul(t[2]);
            if (vl.attribute_count < VertexLayout::MAX_ATTRIBUTES) vl.attributes[vl.attribute_count++] = a;
        }
        vl.buffer_count = 1;
    }
    // Bindings: "<name> = <set>:<binding>:<type>[:<stages>]".
    if (auto bsec = pt.get_child_optional("bindings")) {
        for (auto& kv : *bsec) {
            auto t = split(kv.second.get_value<std::string>(), ':');
            if (t.size() < 3) continue;
            MaterialParam p; p.name = kv.first;
            p.set = (uint32_t)std::stoul(t[0]); p.binding = (uint32_t)std::stoul(t[1]); p.type = parse_param_type(t[2]);
            p.stages = t.size() > 3 ? parse_stage(t[3]) : STAGE_ALL;
            out->params.push_back(p);
        }
    }
    return !out->shader_source.empty();
}

Material* Material::create(GraphicDevice* device, const char* material_path, const char* shader_dir, Result* out_result) {
    if (!device || !material_path || !shader_dir) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    MaterialDesc d;
    if (!MaterialDesc::load(material_path, &d)) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }

    const Backend backend = device->get_backend();
    auto* m = new Material();
    m->device_ = device; m->desc_ = std::move(d);

    // Create a shader module for every stage that declares an entry point. Keep the
    // blob bytes alive until create_shader has consumed them.
    std::vector<std::vector<uint8_t>> blobs(MaterialDesc::STAGE_COUNT);
    int created = 0;
    for (int s = 0; s < MaterialDesc::STAGE_COUNT; ++s) {
        const ShaderStage stage = ShaderStage(s);
        if (!m->desc_.has(stage)) continue;
        ShaderLanguage lang;
        bool got = false;
        // Source of truth is the HLSL: compile it for this backend when the built-in compiler
        // is available, and only fall back to a prebuilt per-backend blob otherwise.
#ifdef WINDOW_SUPPORT_SHADER_COMPILER
        if (ShaderCompiler::available())
            got = compile_shader_blob(backend, shader_dir, m->desc_, stage, m->desc_.entry[s], blobs[s], lang);
#endif
        if (!got)
            got = load_shader_blob(backend, shader_dir, m->desc_, m->desc_.entry[s], blobs[s], lang);
        if (!got) {
            std::fprintf(stderr, "[Material] no shader for stage %d backend %d (%s)\n", s, int(backend), m->desc_.shader_source.c_str());
            m->destroy(); if (out_result) *out_result = Result::ErrorNotSupported; return nullptr;
        }
        ShaderDesc sd; sd.stage = stage; sd.language = lang; sd.code = blobs[s].data(); sd.code_size = blobs[s].size();
        sd.entry_point = m->desc_.entry[s].c_str();
        m->shaders_[s] = device->create_shader(sd);
        if (!m->shaders_[s].valid()) { m->destroy(); if (out_result) *out_result = Result::ErrorUnknown; return nullptr; }
        ++created;
    }
    if (created == 0) { m->destroy(); if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }

    // Descriptor set layout + pipeline layout from the declared bindings.
    DescriptorSetLayoutDesc dl; dl.binding_count = 0;
    for (const auto& p : m->desc_.params)
        if (dl.binding_count < DescriptorSetLayoutDesc::MAX_BINDINGS)
            dl.bindings[dl.binding_count++] = { p.binding, to_binding_type(p.type), 1, p.stages };
    m->set_layout_ = device->create_descriptor_set_layout(dl);
    PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = m->set_layout_;
    m->layout_ = device->create_pipeline_layout(pl);

    // Assemble the pipeline from whichever stages are present. compute_shader →
    // compute; mesh_shader → mesh pipeline (+ optional task); else graphics
    // (vertex + fragment + optional geometry/tessellation). Ray-tracing stage
    // modules are created above and kept for an RT pipeline (backend-specific; the
    // graphics PipelineDesc doesn't carry them).
    auto sh = [&](ShaderStage s) { return m->shaders_[int(s)]; };
    PipelineDesc gp;
    gp.layout = m->layout_;
    gp.compute_shader     = sh(ShaderStage::Compute);
    gp.mesh_shader        = sh(ShaderStage::Mesh);
    gp.task_shader        = sh(ShaderStage::Task);
    gp.vertex_shader      = sh(ShaderStage::Vertex);
    gp.fragment_shader    = sh(ShaderStage::Fragment);
    gp.geometry_shader    = sh(ShaderStage::Geometry);
    gp.tess_control_shader= sh(ShaderStage::TessControl);
    gp.tess_eval_shader   = sh(ShaderStage::TessEval);
    gp.patch_control_points = m->desc_.patch_control_points;
    gp.vertex_layout = m->desc_.vertex_layout; gp.topology = m->desc_.topology;
    gp.blend = m->desc_.blend; gp.depth_stencil = m->desc_.depth_stencil; gp.rasterizer = m->desc_.rasterizer;
    const bool ray_only = !gp.compute_shader.valid() && !gp.mesh_shader.valid() && !gp.vertex_shader.valid();
    if (ray_only) {
        // RT-only material: modules created; RT pipeline assembly is backend-specific (TODO).
        std::fprintf(stderr, "[Material] ray-tracing-only material: shader modules created, RT pipeline assembly pending\n");
    } else {
        m->pipeline_ = device->create_pipeline(gp);
        if (!m->pipeline_.valid()) { m->destroy(); if (out_result) *out_result = Result::ErrorUnknown; return nullptr; }
    }

    // One descriptor write slot per param (filled in by set_*).
    m->writes_.resize(m->desc_.params.size());
    for (size_t i = 0; i < m->desc_.params.size(); ++i) {
        m->writes_[i].binding = m->desc_.params[i].binding;
        m->writes_[i].type    = to_binding_type(m->desc_.params[i].type);
    }
    if (out_result) *out_result = Result::Success;
    return m;
}

void Material::destroy() {
    if (device_) {
        if (set_.valid())        device_->destroy_descriptor_set(set_);
        if (pipeline_.valid())   device_->destroy_pipeline(pipeline_);
        if (layout_.valid())     device_->destroy_pipeline_layout(layout_);
        if (set_layout_.valid()) device_->destroy_descriptor_set_layout(set_layout_);
        for (auto& s : shaders_) if (s.valid()) device_->destroy_shader(s);
    }
    delete this;
}

int Material::find_param(const char* name) const {
    for (size_t i = 0; i < desc_.params.size(); ++i) if (desc_.params[i].name == name) return int(i);
    return -1;
}
void Material::set_uniform_buffer(const char* name, BufferHandle h, uint32_t offset, uint32_t size) {
    int i = find_param(name); if (i < 0) return;
    writes_[i].buffer = h; writes_[i].buffer_offset = offset; writes_[i].buffer_size = size; dirty_ = true;
}
void Material::set_texture(const char* name, TextureHandle h) { int i = find_param(name); if (i < 0) return; writes_[i].texture = h; dirty_ = true; }
void Material::set_sampler(const char* name, SamplerHandle h) { int i = find_param(name); if (i < 0) return; writes_[i].sampler = h; dirty_ = true; }

void Material::bind(GraphicCommander* cmd) {
    if (!cmd || !device_) return;
    if (dirty_) {
        if (set_.valid()) device_->destroy_descriptor_set(set_);
        DescriptorSetDesc sd; sd.layout = set_layout_;
        sd.write_count = int(writes_.size() < size_t(DescriptorSetDesc::MAX_WRITES) ? writes_.size() : DescriptorSetDesc::MAX_WRITES);
        for (int i = 0; i < sd.write_count; ++i) sd.writes[i] = writes_[i];
        set_ = device_->create_descriptor_set(sd);
        dirty_ = false;
    }
    cmd->set_pipeline(pipeline_);
    if (set_.valid()) cmd->bind_descriptor_set(0, set_);
}

} // namespace gfx
} // namespace window
