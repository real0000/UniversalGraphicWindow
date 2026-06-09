#include "material.hpp"

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
    if (s == "fragment") return STAGE_FRAGMENT;
    if (s == "compute")  return STAGE_COMPUTE;
    return STAGE_ALL;
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
            // Prefer SPIR-V (GL 4.6 ARB_gl_spirv); fall back to a GLSL blob if present.
            bytes = read_file(base + "." + entry + ".spv"); lang = ShaderLanguage::SPIRV;
            if (bytes.empty()) { bytes = read_file(base + "." + entry + ".glsl"); lang = ShaderLanguage::GLSL; }
            break;
        }
        case Backend::Metal:
            bytes = read_file(base + "." + entry + ".msl"); lang = ShaderLanguage::MSL; break;
        case Backend::D3D11: case Backend::D3D12:
            // Hand the HLSL source to the backend (it compiles via D3DCompile).
            bytes = read_file(dir + "/" + d.shader_source); lang = ShaderLanguage::HLSL; break;
        default:
            return false;
    }
    return !bytes.empty();
}

} // namespace

bool MaterialDesc::load(const char* path, MaterialDesc* out) {
    if (!path || !out) return false;
    boost::property_tree::ptree pt;
    try { boost::property_tree::ini_parser::read_ini(path, pt); }
    catch (const std::exception& e) { std::fprintf(stderr, "[Material] parse %s: %s\n", path, e.what()); return false; }

    out->shader_source = pt.get<std::string>("shader.source", "");
    out->vertex_entry  = pt.get<std::string>("shader.vertex_entry", "vs_main");
    out->pixel_entry   = pt.get<std::string>("shader.pixel_entry", "ps_main");

    const std::string blend = pt.get<std::string>("state.blend", "none");
    if (blend == "alpha") out->blend = BlendState::alpha_blend();
    else                  out->blend.enabled = (blend == "additive");
    out->depth_stencil = pt.get<bool>("state.depth_test", false) ? DepthStencilState::depth_test() : DepthStencilState::disabled();
    const std::string cull = pt.get<std::string>("state.cull", "none");
    out->rasterizer = (cull == "none") ? RasterizerState::no_cull() : RasterizerState::default_state();
    out->rasterizer.scissor_enable = true;
    out->topology = PrimitiveTopology::TriangleList;  // (only triangles needed so far)

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
    std::vector<uint8_t> vsb, psb; ShaderLanguage vlang, plang;
    if (!load_shader_blob(backend, shader_dir, d, d.vertex_entry, vsb, vlang) ||
        !load_shader_blob(backend, shader_dir, d, d.pixel_entry,  psb, plang)) {
        std::fprintf(stderr, "[Material] no shader blob for backend %d (%s)\n", int(backend), d.shader_source.c_str());
        if (out_result) *out_result = Result::ErrorNotSupported; return nullptr;
    }

    auto* m = new Material();
    m->device_ = device; m->desc_ = std::move(d);

    ShaderDesc vd; vd.stage = ShaderStage::Vertex;   vd.language = vlang; vd.code = vsb.data(); vd.code_size = vsb.size(); vd.entry_point = m->desc_.vertex_entry.c_str();
    ShaderDesc pd; pd.stage = ShaderStage::Fragment; pd.language = plang; pd.code = psb.data(); pd.code_size = psb.size(); pd.entry_point = m->desc_.pixel_entry.c_str();
    m->vs_ = device->create_shader(vd);
    m->ps_ = device->create_shader(pd);
    if (!m->vs_.valid() || !m->ps_.valid()) { m->destroy(); if (out_result) *out_result = Result::ErrorUnknown; return nullptr; }

    // Descriptor set layout + pipeline layout from the declared bindings.
    DescriptorSetLayoutDesc dl; dl.binding_count = 0;
    for (const auto& p : m->desc_.params)
        if (dl.binding_count < DescriptorSetLayoutDesc::MAX_BINDINGS)
            dl.bindings[dl.binding_count++] = { p.binding, to_binding_type(p.type), 1, p.stages };
    m->set_layout_ = device->create_descriptor_set_layout(dl);
    PipelineLayoutDesc pl; pl.set_layout_count = 1; pl.set_layouts[0] = m->set_layout_;
    m->layout_ = device->create_pipeline_layout(pl);

    PipelineDesc gp;
    gp.vertex_shader = m->vs_; gp.fragment_shader = m->ps_; gp.layout = m->layout_;
    gp.vertex_layout = m->desc_.vertex_layout; gp.topology = m->desc_.topology;
    gp.blend = m->desc_.blend; gp.depth_stencil = m->desc_.depth_stencil; gp.rasterizer = m->desc_.rasterizer;
    m->pipeline_ = device->create_pipeline(gp);
    if (!m->pipeline_.valid()) { m->destroy(); if (out_result) *out_result = Result::ErrorUnknown; return nullptr; }

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
        if (vs_.valid())         device_->destroy_shader(vs_);
        if (ps_.valid())         device_->destroy_shader(ps_);
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
