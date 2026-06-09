// api_render_opengl.cpp — OpenGL implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp), and the create_device /
// create_commander factory.
//
// Platform-agnostic GL (via GLAD) — works on any context backend (GLX/WGL/EGL/
// Cocoa) since the per-platform Graphics already created + loaded the context.
// This file always compiles; the GL-using bodies are guarded by
// WINDOW_SUPPORT_OPENGL so a GL-disabled build still links the factory (which
// then reports ErrorNotSupported). Other backends get their own api_render_*.cpp.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

#if defined(WINDOW_SUPPORT_OPENGL)
#include "glad.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// GLES / WebGL flavour: this same TU serves OpenGL ES (EGL) and WebGL (Emscripten).
// Those profiles lack a handful of desktop-GL entry points; UGW_GL_ES guards them so
// they log "unsupported" instead of referencing absent symbols. It is never defined
// for the desktop GL build (the path validated by the gui demo is byte-identical).
#if defined(__EMSCRIPTEN__) || defined(WINDOW_GLES) || defined(GL_ES_VERSION_3_0)
#define UGW_GL_ES 1
#else
#define UGW_GL_ES 0
#endif
#endif

namespace window {

#if defined(WINDOW_SUPPORT_OPENGL)
namespace {

// Log once per feature that core OpenGL cannot do (mesh shaders, etc.), then no-op.
void gl_unsupported(const char* feature) {
    static std::set<std::string> logged;
    if (logged.insert(feature).second)
        std::fprintf(stderr, "[UGW/OpenGL] %s not supported on this backend (no-op)\n", feature);
}

GLbitfield gl_barrier_bits(uint32_t bits) {
    if (bits == GPU_BARRIER_ALL) return GL_ALL_BARRIER_BITS;
    GLbitfield b = 0;
    if (bits & GPU_BARRIER_VERTEX_BUFFER)  b |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
    if (bits & GPU_BARRIER_INDEX_BUFFER)   b |= GL_ELEMENT_ARRAY_BARRIER_BIT;
    if (bits & GPU_BARRIER_UNIFORM_BUFFER) b |= GL_UNIFORM_BARRIER_BIT;
    if (bits & GPU_BARRIER_STORAGE_BUFFER) b |= GL_SHADER_STORAGE_BARRIER_BIT;
    if (bits & GPU_BARRIER_TEXTURE)        b |= GL_TEXTURE_FETCH_BARRIER_BIT;
    if (bits & GPU_BARRIER_STORAGE_IMAGE)  b |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
    if (bits & GPU_BARRIER_INDIRECT_ARGS)  b |= GL_COMMAND_BARRIER_BIT;
    if (bits & GPU_BARRIER_RENDER_TARGET)  b |= GL_FRAMEBUFFER_BARRIER_BIT;
    return b;
}
GLenum gl_image_access(StorageAccess a) {
    switch (a) { case StorageAccess::Read: return GL_READ_ONLY; case StorageAccess::Write: return GL_WRITE_ONLY; default: return GL_READ_WRITE; }
}

//-----------------------------------------------------------------------------
// Enum / format conversions
//-----------------------------------------------------------------------------
struct GLFormat { GLint internal_fmt; GLenum format; GLenum type; bool depth; bool stencil; };

GLFormat gl_format(TextureFormat f) {
    switch (f) {
        case TextureFormat::R8_UNORM:        return { GL_R8, GL_RED, GL_UNSIGNED_BYTE, false, false };
        case TextureFormat::RG8_UNORM:       return { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, false, false };
        case TextureFormat::RGBA8_UNORM:     return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false };
        case TextureFormat::RGBA8_UNORM_SRGB:return { GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false };
        case TextureFormat::BGRA8_UNORM:     return { GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, false, false };
        case TextureFormat::R16_FLOAT:       return { GL_R16F, GL_RED, GL_HALF_FLOAT, false, false };
        case TextureFormat::RGBA16_FLOAT:    return { GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, false, false };
        case TextureFormat::R32_FLOAT:       return { GL_R32F, GL_RED, GL_FLOAT, false, false };
        case TextureFormat::RGBA32_FLOAT:    return { GL_RGBA32F, GL_RGBA, GL_FLOAT, false, false };
        case TextureFormat::D32_FLOAT:       return { GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, true, false };
        case TextureFormat::D24_UNORM_S8_UINT: return { GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, true, true };
        default:                             return { GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false, false };
    }
}

GLenum gl_topology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::PointList:     return GL_POINTS;
        case PrimitiveTopology::LineList:      return GL_LINES;
        case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        default:                               return GL_TRIANGLES;
    }
}

GLenum gl_blend_factor(BlendFactor b) {
    switch (b) {
        case BlendFactor::Zero:          return GL_ZERO;
        case BlendFactor::One:           return GL_ONE;
        case BlendFactor::SrcColor:      return GL_SRC_COLOR;
        case BlendFactor::InvSrcColor:   return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::SrcAlpha:      return GL_SRC_ALPHA;
        case BlendFactor::InvSrcAlpha:   return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:      return GL_DST_COLOR;
        case BlendFactor::InvDstColor:   return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::DstAlpha:      return GL_DST_ALPHA;
        case BlendFactor::InvDstAlpha:   return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::SrcAlphaSat:   return GL_SRC_ALPHA_SATURATE;
        case BlendFactor::BlendFactor:   return GL_CONSTANT_COLOR;
        case BlendFactor::InvBlendFactor:return GL_ONE_MINUS_CONSTANT_COLOR;
        default:                         return GL_ONE;
    }
}
GLenum gl_blend_op(BlendOp o) {
    switch (o) {
        case BlendOp::Subtract:    return GL_FUNC_SUBTRACT;
        case BlendOp::RevSubtract: return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::Min:         return GL_MIN;
        case BlendOp::Max:         return GL_MAX;
        default:                   return GL_FUNC_ADD;
    }
}
GLenum gl_compare(CompareFunc c) {
    switch (c) {
        case CompareFunc::Never:        return GL_NEVER;
        case CompareFunc::Less:         return GL_LESS;
        case CompareFunc::Equal:        return GL_EQUAL;
        case CompareFunc::LessEqual:    return GL_LEQUAL;
        case CompareFunc::Greater:      return GL_GREATER;
        case CompareFunc::NotEqual:     return GL_NOTEQUAL;
        case CompareFunc::GreaterEqual: return GL_GEQUAL;
        default:                        return GL_ALWAYS;
    }
}
GLenum gl_stencil_op(StencilOp s) {
    switch (s) {
        case StencilOp::Zero:    return GL_ZERO;
        case StencilOp::Replace: return GL_REPLACE;
        case StencilOp::IncrSat: return GL_INCR;
        case StencilOp::DecrSat: return GL_DECR;
        case StencilOp::Invert:  return GL_INVERT;
        case StencilOp::IncrWrap:return GL_INCR_WRAP;
        case StencilOp::DecrWrap:return GL_DECR_WRAP;
        default:                 return GL_KEEP;
    }
}
GLenum gl_address(AddressMode a) {
    switch (a) {
        case AddressMode::Mirror:     return GL_MIRRORED_REPEAT;
        case AddressMode::Clamp:      return GL_CLAMP_TO_EDGE;
        case AddressMode::Border:     return GL_CLAMP_TO_BORDER;
        case AddressMode::MirrorOnce: return GL_MIRROR_CLAMP_TO_EDGE;
        default:                      return GL_REPEAT;
    }
}
void gl_vertex_attrib(VertexFormat f, GLint& comps, GLenum& type, GLboolean& norm, bool& integer) {
    integer = false; norm = GL_FALSE;
    switch (f) {
        case VertexFormat::Float1: comps=1; type=GL_FLOAT; return;
        case VertexFormat::Float2: comps=2; type=GL_FLOAT; return;
        case VertexFormat::Float3: comps=3; type=GL_FLOAT; return;
        case VertexFormat::Float4: comps=4; type=GL_FLOAT; return;
        // Integer formats (consumed as ints in-shader → glVertexAttribIPointer).
        case VertexFormat::Int1:  comps=1; type=GL_INT; integer=true; return;
        case VertexFormat::Int2:  comps=2; type=GL_INT; integer=true; return;
        case VertexFormat::Int3:  comps=3; type=GL_INT; integer=true; return;
        case VertexFormat::Int4:  comps=4; type=GL_INT; integer=true; return;
        case VertexFormat::UInt1: comps=1; type=GL_UNSIGNED_INT; integer=true; return;
        case VertexFormat::UInt2: comps=2; type=GL_UNSIGNED_INT; integer=true; return;
        case VertexFormat::UInt3: comps=3; type=GL_UNSIGNED_INT; integer=true; return;
        case VertexFormat::UInt4: comps=4; type=GL_UNSIGNED_INT; integer=true; return;
        case VertexFormat::Short2:  comps=2; type=GL_SHORT; integer=true; return;
        case VertexFormat::Short4:  comps=4; type=GL_SHORT; integer=true; return;
        case VertexFormat::UShort2: comps=2; type=GL_UNSIGNED_SHORT; integer=true; return;
        case VertexFormat::UShort4: comps=4; type=GL_UNSIGNED_SHORT; integer=true; return;
        case VertexFormat::Byte4:   comps=4; type=GL_BYTE; integer=true; return;
        case VertexFormat::UByte4:  comps=4; type=GL_UNSIGNED_BYTE; integer=true; return;
        // Normalized formats (scaled to [0,1]/[-1,1] floats in-shader).
        case VertexFormat::Short2N:  comps=2; type=GL_SHORT; norm=GL_TRUE; return;
        case VertexFormat::Short4N:  comps=4; type=GL_SHORT; norm=GL_TRUE; return;
        case VertexFormat::UShort2N: comps=2; type=GL_UNSIGNED_SHORT; norm=GL_TRUE; return;
        case VertexFormat::UShort4N: comps=4; type=GL_UNSIGNED_SHORT; norm=GL_TRUE; return;
        case VertexFormat::Byte4N:   comps=4; type=GL_BYTE; norm=GL_TRUE; return;
        case VertexFormat::UByte4N:  comps=4; type=GL_UNSIGNED_BYTE; norm=GL_TRUE; return;
        case VertexFormat::Half2: comps=2; type=GL_HALF_FLOAT; return;
        case VertexFormat::Half4: comps=4; type=GL_HALF_FLOAT; return;
        case VertexFormat::RGB10A2: comps=4; type=GL_UNSIGNED_INT_2_10_10_10_REV; norm=GL_TRUE; return;
        default: comps=4; type=GL_FLOAT; return;
    }
}

template <class T>
struct Pool {
    std::vector<T>   items;
    std::vector<int> free_list;
    int alloc(const T& v) {
        if (!free_list.empty()) { int id = free_list.back(); free_list.pop_back(); items[id] = v; return id; }
        items.push_back(v); return int(items.size()) - 1;
    }
    T* get(int id) { return (id >= 0 && id < int(items.size())) ? &items[id] : nullptr; }
    void release(int id) { if (id >= 0 && id < int(items.size())) { items[id] = T{}; free_list.push_back(id); } }
};

struct GLBuffer   { GLuint id = 0; GLenum target = GL_ARRAY_BUFFER; uint32_t size = 0; };
struct GLTexture  { GLuint id = 0; GLenum target = GL_TEXTURE_2D; GLFormat fmt{}; int w = 0, h = 0; int levels = 1, layers = 1; };
struct GLSampler  { GLuint id = 0; };
struct GLShader   { GLuint id = 0; ShaderStage stage = ShaderStage::Vertex; };
struct GLPipeline {
    GLuint program = 0;
    VertexLayout layout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    BlendState blend; DepthStencilState depth; RasterizerState raster;
    bool compute = false;
    bool is_patch = false;   // tessellation
    int  patch_points = 0;
    bool alpha_to_coverage = false;
};
struct GLRenderTarget { GLuint fbo = 0; int color_tex = -1; int depth_tex = -1; };
struct GLFence     { GLsync sync = nullptr; bool signaled = false; };
struct GLSemaphore { };  // OpenGL has one implicit queue; semaphores are no-ops
struct GLQuery     { GLuint id = 0; GLenum target = GL_TIMESTAMP; };
// Binding model (OpenGL has no descriptor sets — we store the layout/writes and
// replay them as individual slot binds in bind_descriptor_set).
struct GLDescSetLayout  { DescriptorSetLayoutDesc desc; };
struct GLPipelineLayout { PipelineLayoutDesc desc; };
struct GLDescriptorSet  { std::vector<DescriptorWrite> writes; DescriptorSetLayoutHandle layout; };
// PSO cache: key (hash of shaders+state) → [GLenum format][program binary bytes].
struct GLPipelineCache  { std::unordered_map<uint64_t, std::vector<uint8_t>> entries; };
// Timeline semaphore: current value + GPU fences pending against future values.
struct GLTimeline       { uint64_t value = 0; std::vector<std::pair<uint64_t, GLsync>> pending; };
struct GLAccelStruct    { AccelStructType type = AccelStructType::BottomLevel; };  // RT: stub on OpenGL

//-----------------------------------------------------------------------------
// GLDevice
//-----------------------------------------------------------------------------
class GLDevice : public GraphicDevice {
public:
    explicit GLDevice(Graphics* ctx) : ctx_(ctx) {}
    ~GLDevice() override = default;

    Backend get_backend() const override { return Backend::OpenGL; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (ctx_ && out) ctx_->get_capabilities(out);
        if (!out) return;
        const_cast<GLDevice*>(this)->cur();
        GLint v = 0;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &v);        out->min_uniform_buffer_offset_alignment = v;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &v); out->min_storage_buffer_offset_alignment = v;
        glGetIntegerv(GL_TEXTURE_BUFFER_OFFSET_ALIGNMENT, &v);        out->min_texel_buffer_offset_alignment = v;
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &v);                 out->max_uniform_buffer_range = v;
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &v);         out->max_storage_buffer_range = v;
        out->max_push_constant_size  = 256;                       // emulated via a UBO; 256 = safe cross-API budget
        out->max_bound_descriptor_sets = PipelineLayoutDesc::MAX_SETS;
    }

    BufferHandle create_buffer(const BufferDesc& d) override {
        cur();
        GLBuffer b; b.target = buffer_target(d.type); b.size = d.size;
        glGenBuffers(1, &b.id);
        glBindBuffer(b.target, b.id);
        glBufferData(b.target, d.size, d.initial_data, gl_usage(d.usage));
        const int id = buffers_.alloc(b);
        if (d.debug_name && glObjectLabel) glObjectLabel(GL_BUFFER, b.id, -1, d.debug_name);
        return { id };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override {
        cur(); if (auto* b = buffers_.get(h.id)) { glBindBuffer(b->target, b->id); glBufferSubData(b->target, offset, size, data); }
    }
    void destroy_buffer(BufferHandle h) override { cur(); if (auto* b = buffers_.get(h.id)) { glDeleteBuffers(1, &b->id); buffers_.release(h.id); } }

    TextureHandle create_texture(const TextureDesc& d) override {
        cur();
        GLTexture t; t.fmt = gl_format(d.format); t.w = d.width; t.h = d.height;
        t.target = (d.cube ? GL_TEXTURE_CUBE_MAP : ((d.array_layers > 1 || d.array_texture) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D));
        t.layers = (t.target == GL_TEXTURE_2D_ARRAY) ? (d.array_layers > 1 ? d.array_layers : 1) : 1;
        // mip_levels: >0 explicit, 0 = full chain. Immutable storage needs a count.
        t.levels = d.mip_levels > 0 ? d.mip_levels : 1;
        if (d.mip_levels == 0) { int s = d.width > d.height ? d.width : d.height; t.levels = 1; while (s > 1) { s >>= 1; ++t.levels; } }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // R8 / odd-width rows
        glGenTextures(1, &t.id);
        glBindTexture(t.target, t.id);
#if defined(GL_TEXTURE_SPARSE_ARB)
        if (d.sparse) glTexParameteri(t.target, GL_TEXTURE_SPARSE_ARB, GL_TRUE);  // commit tiles via update_texture_residency()
#endif
        // Immutable storage (glTexStorage) so the texture can back glTextureView().
        if (t.target == GL_TEXTURE_2D_ARRAY) {
            glTexStorage3D(t.target, t.levels, t.fmt.internal_fmt, d.width, d.height, t.layers);
            if (d.initial_data) glTexSubImage3D(t.target, 0, 0, 0, 0, d.width, d.height, t.layers, t.fmt.format, t.fmt.type, d.initial_data);
        } else { // GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP
            glTexStorage2D(t.target, t.levels, t.fmt.internal_fmt, d.width, d.height);
            if (d.initial_data && t.target == GL_TEXTURE_2D)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, d.width, d.height, t.fmt.format, t.fmt.type, d.initial_data);
        }
        glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, t.levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        const int id = textures_.alloc(t);
        if (d.debug_name && glObjectLabel) glObjectLabel(GL_TEXTURE, t.id, -1, d.debug_name);
        return { id };
    }
    void update_texture(TextureHandle h, const TextureRegion& r, const void* data) override {
        cur(); if (auto* t = textures_.get(h.id)) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);  // R8 / odd-width rows
            glBindTexture(t->target, t->id);
            if (t->target == GL_TEXTURE_2D)
                glTexSubImage2D(GL_TEXTURE_2D, r.mip, r.x, r.y, r.width, r.height, t->fmt.format, t->fmt.type, data);
            else if (t->target == GL_TEXTURE_2D_ARRAY)
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, r.mip, r.x, r.y, r.layer, r.width, r.height, 1, t->fmt.format, t->fmt.type, data);
        }
    }
    void generate_mipmaps(TextureHandle h) override { cur(); if (auto* t = textures_.get(h.id)) { glBindTexture(t->target, t->id); glGenerateMipmap(t->target); } }
    void destroy_texture(TextureHandle h) override { cur(); if (auto* t = textures_.get(h.id)) { glDeleteTextures(1, &t->id); textures_.release(h.id); } }

    SamplerHandle create_sampler(const SamplerState& s) override {
        cur(); GLSampler smp; glGenSamplers(1, &smp.id);
        const GLenum minf = s.min_filter == FilterMode::Point ? GL_NEAREST : GL_LINEAR;
        const GLenum magf = s.mag_filter == FilterMode::Point ? GL_NEAREST : GL_LINEAR;
        glSamplerParameteri(smp.id, GL_TEXTURE_MIN_FILTER, minf);
        glSamplerParameteri(smp.id, GL_TEXTURE_MAG_FILTER, magf);
        glSamplerParameteri(smp.id, GL_TEXTURE_WRAP_S, gl_address(s.address_u));
        glSamplerParameteri(smp.id, GL_TEXTURE_WRAP_T, gl_address(s.address_v));
        glSamplerParameteri(smp.id, GL_TEXTURE_WRAP_R, gl_address(s.address_w));
        glSamplerParameterfv(smp.id, GL_TEXTURE_BORDER_COLOR, s.border_color);
        return { samplers_.alloc(smp) };
    }
    void destroy_sampler(SamplerHandle h) override { cur(); if (auto* s = samplers_.get(h.id)) { glDeleteSamplers(1, &s->id); samplers_.release(h.id); } }

    ShaderHandle create_shader(const ShaderDesc& d) override {
        cur();
        GLenum type = GL_VERTEX_SHADER;
        switch (d.stage) {
            case ShaderStage::Fragment:    type = GL_FRAGMENT_SHADER; break;
            case ShaderStage::Geometry:    type = GL_GEOMETRY_SHADER; break;
            case ShaderStage::TessControl: type = GL_TESS_CONTROL_SHADER; break;
            case ShaderStage::TessEval:    type = GL_TESS_EVALUATION_SHADER; break;
            case ShaderStage::Compute:     type = GL_COMPUTE_SHADER; break;
            case ShaderStage::Task:
            case ShaderStage::Mesh:        gl_unsupported("mesh/task shaders"); return { -1 };
            case ShaderStage::RayGen: case ShaderStage::Miss: case ShaderStage::ClosestHit:
            case ShaderStage::AnyHit: case ShaderStage::Intersection: case ShaderStage::Callable:
                                           gl_unsupported("ray-tracing shaders"); return { -1 };
            default: break; // Vertex
        }
        GLShader sh; sh.stage = d.stage; sh.id = glCreateShader(type);
        if (d.language == ShaderLanguage::SPIRV) {
#if UGW_GL_ES
            gl_unsupported("SPIR-V shaders (GLES/WebGL has no ARB_gl_spirv; provide ESSL)"); glDeleteShader(sh.id); return { -1 };
#else
            // Cross-API path: ingest SPIR-V directly (GL 4.6 / ARB_gl_spirv), the same
            // bytecode a Vulkan backend consumes. One shader blob, many backends.
            if (!glSpecializeShader) { gl_unsupported("SPIR-V shaders (needs GL 4.6)"); glDeleteShader(sh.id); return { -1 }; }
            glShaderBinary(1, &sh.id, GL_SHADER_BINARY_FORMAT_SPIR_V, d.code, GLsizei(d.code_size));
            glSpecializeShader(sh.id, d.entry_point ? d.entry_point : "main", 0, nullptr, nullptr);
#endif
        } else {
            const char* src = static_cast<const char*>(d.code);
            const GLint len = d.code_size ? GLint(d.code_size) : -1;
            if (len < 0) glShaderSource(sh.id, 1, &src, nullptr);
            else         glShaderSource(sh.id, 1, &src, &len);
            glCompileShader(sh.id);
        }
        GLint ok = 0; glGetShaderiv(sh.id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0}; GLsizei n = 0; glGetShaderInfoLog(sh.id, sizeof(log) - 1, &n, log);
            std::fprintf(stderr, "[UGW/OpenGL] shader compile failed:\n%s\n", log);
            glDeleteShader(sh.id); sh.id = 0; return { -1 };
        }
        return { shaders_.alloc(sh) };
    }
    void destroy_shader(ShaderHandle h) override { cur(); if (auto* s = shaders_.get(h.id)) { if (s->id) glDeleteShader(s->id); shaders_.release(h.id); } }

    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        cur();
        if (d.mesh_shader.valid()) { gl_unsupported("mesh-shader pipelines"); return { -1 }; }
        GLPipeline p; p.layout = d.vertex_layout; p.topology = d.topology;
        p.blend = d.blend; p.depth = d.depth_stencil; p.raster = d.rasterizer;
        p.alpha_to_coverage = d.alpha_to_coverage;
        p.program = glCreateProgram();
        auto attach = [&](ShaderHandle h) { if (auto* s = shaders_.get(h.id)) glAttachShader(p.program, s->id); };
        if (d.compute_shader.valid()) { p.compute = true; attach(d.compute_shader); }
        else {
            attach(d.vertex_shader); attach(d.fragment_shader);
            if (d.geometry_shader.valid())     attach(d.geometry_shader);
            if (d.tess_control_shader.valid()) attach(d.tess_control_shader);
            if (d.tess_eval_shader.valid())    attach(d.tess_eval_shader);
            if (d.patch_control_points > 0) { p.is_patch = true; p.patch_points = int(d.patch_control_points); }
        }
        // PSO cache: try to load a previously compiled binary; else link + store one.
        GLPipelineCache* pc = d.cache.valid() ? pcaches_.get(d.cache.id) : nullptr;
        const uint64_t key = pipeline_cache_key(d);
        GLint ok = 0;
        bool loaded = false;
        if (pc) {
            auto it = pc->entries.find(key);
            if (it != pc->entries.end() && it->second.size() > sizeof(GLenum)) {
                GLenum fmt = 0; std::memcpy(&fmt, it->second.data(), sizeof(GLenum));
                glProgramBinary(p.program, fmt, it->second.data() + sizeof(GLenum), GLsizei(it->second.size() - sizeof(GLenum)));
                glGetProgramiv(p.program, GL_LINK_STATUS, &ok); loaded = (ok != 0);
            }
            if (!loaded) glProgramParameteri(p.program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        }
        if (!loaded) {
            glLinkProgram(p.program);
            glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
        }
        if (!ok) {
            char log[1024] = {0}; GLsizei n = 0; glGetProgramInfoLog(p.program, sizeof(log) - 1, &n, log);
            std::fprintf(stderr, "[UGW/OpenGL] program link failed:\n%s\n", log);
            glDeleteProgram(p.program); return { -1 };
        }
        if (pc && !loaded) {   // persist the freshly-linked binary into the cache
            GLint len = 0; glGetProgramiv(p.program, GL_PROGRAM_BINARY_LENGTH, &len);
            if (len > 0) {
                std::vector<uint8_t> blob(sizeof(GLenum) + size_t(len));
                GLenum fmt = 0; GLsizei written = 0;
                glGetProgramBinary(p.program, len, &written, &fmt, blob.data() + sizeof(GLenum));
                std::memcpy(blob.data(), &fmt, sizeof(GLenum));
                blob.resize(sizeof(GLenum) + size_t(written));
                pc->entries[key] = std::move(blob);
            }
        }
        const int id = pipelines_.alloc(p);
        if (d.debug_name && glObjectLabel) glObjectLabel(GL_PROGRAM, p.program, -1, d.debug_name);
        return { id };
    }
    void destroy_pipeline(PipelineHandle h) override { cur(); if (auto* p = pipelines_.get(h.id)) { if (p->program) glDeleteProgram(p->program); pipelines_.release(h.id); } }

    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override {
        cur();
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples;
        td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET; td.mip_levels = d.generate_mipmaps ? 0 : 1;
        const TextureHandle tex = create_texture(td);
        GLRenderTarget rt; rt.color_tex = tex.id;
        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        if (auto* t = textures_.get(tex.id)) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return { rts_.alloc(rt) };
    }
    RenderTargetHandle create_depth_target(const DepthStencilDesc& d) override {
        cur();
        TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.samples = d.samples;
        td.usage = TEXTURE_USAGE_DEPTH_STENCIL;
        const TextureHandle tex = create_texture(td);
        GLRenderTarget rt; rt.depth_tex = tex.id;
        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        if (auto* t = textures_.get(tex.id)) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->id, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return { rts_.alloc(rt) };
    }
    TextureHandle render_target_texture(RenderTargetHandle h) override { if (auto* rt = rts_.get(h.id)) return { rt->color_tex >= 0 ? rt->color_tex : rt->depth_tex }; return { -1 }; }
    void destroy_render_target(RenderTargetHandle h) override { cur(); if (auto* rt = rts_.get(h.id)) { if (rt->fbo) glDeleteFramebuffers(1, &rt->fbo); rts_.release(h.id); } }

    // ---- Synchronization ----------------------------------------------------
    FenceHandle create_fence(bool signaled) override { return { fences_.alloc(GLFence{ nullptr, signaled }) }; }
    void destroy_fence(FenceHandle h) override { cur(); if (auto* f = fences_.get(h.id)) { if (f->sync) glDeleteSync(f->sync); fences_.release(h.id); } }
    bool wait_fence(FenceHandle h, uint64_t timeout_ns) override {
        cur(); auto* f = fences_.get(h.id); if (!f) return false;
        if (f->signaled) return true;
        if (!f->sync) return false;
        const GLenum r = glClientWaitSync(f->sync, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(timeout_ns));
        if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) { f->signaled = true; return true; }
        return false;
    }
    bool get_fence_status(FenceHandle h) override { return wait_fence(h, 0); }
    void reset_fence(FenceHandle h) override { cur(); if (auto* f = fences_.get(h.id)) { if (f->sync) { glDeleteSync(f->sync); f->sync = nullptr; } f->signaled = false; } }
    SemaphoreHandle create_semaphore() override { return { semaphores_.alloc(GLSemaphore{}) }; }
    void destroy_semaphore(SemaphoreHandle h) override { semaphores_.release(h.id); }
    void wait_idle() override { cur(); glFinish(); }

    // Set on a fence by submit_commander() once its work is flushed to the GPU.
    void signal_fence_on_submit(FenceHandle h) {
        if (auto* f = fences_.get(h.id)) { if (f->sync) glDeleteSync(f->sync); f->sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); f->signaled = false; }
    }

    // ---- Queries ------------------------------------------------------------
    QueryHandle create_query(QueryType type) override {
        cur(); GLQuery q;
        switch (type) {
            case QueryType::Occlusion:          q.target = GL_SAMPLES_PASSED; break;
            case QueryType::PipelineStatistics: q.target = GL_PRIMITIVES_GENERATED; break;
            default:                            q.target = GL_TIMESTAMP; break;
        }
        glGenQueries(1, &q.id);
        return { queries_.alloc(q) };
    }
    void destroy_query(QueryHandle h) override { cur(); if (auto* q = queries_.get(h.id)) { if (q->id) glDeleteQueries(1, &q->id); queries_.release(h.id); } }
    bool get_query_result(QueryHandle h, uint64_t* out_value, bool wait) override {
        cur(); auto* q = queries_.get(h.id); if (!q || !q->id) return false;
        if (!wait) { GLint avail = 0; glGetQueryObjectiv(q->id, GL_QUERY_RESULT_AVAILABLE, &avail); if (!avail) return false; }
        GLuint64 v = 0; glGetQueryObjectui64v(q->id, GL_QUERY_RESULT, &v);
        if (out_value) *out_value = v;
        return true;
    }

    // ---- CPU<->GPU data access ----------------------------------------------
    void* map_buffer(BufferHandle h, uint32_t offset, uint32_t size) override {
        cur(); auto* b = buffer(h.id); if (!b) return nullptr;
        const uint32_t len = size ? size : (b->size - offset);
        glBindBuffer(b->target, b->id);
        return glMapBufferRange(b->target, offset, len, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
    }
    void unmap_buffer(BufferHandle h) override { cur(); if (auto* b = buffer(h.id)) { glBindBuffer(b->target, b->id); glUnmapBuffer(b->target); } }
    void read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset) override {
        cur(); auto* b = buffer(h.id); if (!b || !dst) return;
        glBindBuffer(b->target, b->id);
        glGetBufferSubData(b->target, offset, size, dst);
    }
    void read_texture(TextureHandle h, const TextureRegion& r, void* dst) override {
        cur(); auto* t = texture(h.id); if (!t || !dst) return;
        // Read a region back via a transient FBO (works for the colour/array case the
        // GUI needs; depth uses GL_DEPTH_COMPONENT).
        GLuint fbo = 0; glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        if (t->target == GL_TEXTURE_2D_ARRAY)
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, t->id, r.mip, r.layer);
        else
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, r.mip);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(r.x, r.y, r.width, r.height, t->fmt.format, t->fmt.type, dst);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
    }

    // ---- Texture views ------------------------------------------------------
    TextureHandle create_texture_view(const TextureViewDesc& d) override {
        cur(); auto* src = textures_.get(d.texture.id); if (!src) return { -1 };
        GLTexture v;
        v.fmt    = (d.format == TextureFormat::Unknown) ? src->fmt : gl_format(d.format);
        v.target = d.cube ? GL_TEXTURE_CUBE_MAP : src->target;
        v.w = src->w; v.h = src->h;
        v.levels = d.mip_count   ? d.mip_count   : (src->levels - d.base_mip);
        v.layers = d.layer_count ? d.layer_count : (src->layers - d.base_layer);
        glGenTextures(1, &v.id);
        glTextureView(v.id, v.target, src->id, v.fmt.internal_fmt,
                      d.base_mip, v.levels, d.base_layer, v.layers);
        glBindTexture(v.target, v.id);
        glTexParameteri(v.target, GL_TEXTURE_MIN_FILTER, v.levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(v.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return { textures_.alloc(v) };
    }

    // ---- Binding model (descriptor sets / pipeline layout = root signature) ---
    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override { return { dsls_.alloc(GLDescSetLayout{ d }) }; }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { dsls_.release(h.id); }
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc& d) override { return { plls_.alloc(GLPipelineLayout{ d }) }; }
    void destroy_pipeline_layout(PipelineLayoutHandle h) override { plls_.release(h.id); }
    DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& d) override {
        GLDescriptorSet s; s.layout = d.layout; s.writes.assign(d.writes, d.writes + d.write_count);
        return { dsets_.alloc(std::move(s)) };
    }
    void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& d) override {
        if (auto* s = dsets_.get(h.id)) { s->layout = d.layout; s->writes.assign(d.writes, d.writes + d.write_count); }
    }
    void destroy_descriptor_set(DescriptorSetHandle h) override { dsets_.release(h.id); }

    // ---- Debug labels -------------------------------------------------------
    void set_debug_name(ObjectType type, uint32_t id, const char* name) override {
        cur(); if (!glObjectLabel || !name) return;
        GLenum ns = GL_TEXTURE; GLuint obj = 0;
        switch (type) {
            case ObjectType::Buffer:       ns = GL_BUFFER;     if (auto* b = buffers_.get(int(id)))   obj = b->id; break;
            case ObjectType::Texture:      ns = GL_TEXTURE;    if (auto* t = textures_.get(int(id)))  obj = t->id; break;
            case ObjectType::Sampler:      ns = GL_SAMPLER;    if (auto* s = samplers_.get(int(id)))  obj = s->id; break;
            case ObjectType::Shader:       ns = GL_SHADER;     if (auto* s = shaders_.get(int(id)))   obj = s->id; break;
            case ObjectType::Pipeline:     ns = GL_PROGRAM;    if (auto* p = pipelines_.get(int(id))) obj = p->program; break;
            case ObjectType::RenderTarget: ns = GL_FRAMEBUFFER;if (auto* r = rts_.get(int(id)))       obj = r->fbo; break;
        }
        if (obj) glObjectLabel(ns, obj, -1, name);
    }

    // ---- Pipeline (PSO) cache -----------------------------------------------
    PipelineCacheHandle create_pipeline_cache(const void* data, size_t size) override {
        GLPipelineCache c;
        // Deserialize: repeated [u64 key][u32 len][len bytes].
        const uint8_t* p = static_cast<const uint8_t*>(data); size_t off = 0;
        while (data && off + 12 <= size) {
            uint64_t key; uint32_t len;
            std::memcpy(&key, p + off, 8); std::memcpy(&len, p + off + 8, 4); off += 12;
            if (off + len > size) break;
            c.entries[key].assign(p + off, p + off + len); off += len;
        }
        return { pcaches_.alloc(std::move(c)) };
    }
    size_t get_pipeline_cache_data(PipelineCacheHandle h, void* dst, size_t cap) override {
        auto* c = pcaches_.get(h.id); if (!c) return 0;
        size_t need = 0; for (auto& e : c->entries) need += 12 + e.second.size();
        if (!dst) return need;
        uint8_t* p = static_cast<uint8_t*>(dst); size_t off = 0;
        for (auto& e : c->entries) {
            const uint32_t len = uint32_t(e.second.size());
            if (off + 12 + len > cap) break;
            std::memcpy(p + off, &e.first, 8); std::memcpy(p + off + 8, &len, 4); off += 12;
            std::memcpy(p + off, e.second.data(), len); off += len;
        }
        return off;
    }
    void destroy_pipeline_cache(PipelineCacheHandle h) override { pcaches_.release(h.id); }

    // ---- Timeline semaphores (CPU value + GPU fences pending future values) --
    TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial) override { return { timelines_.alloc(GLTimeline{ initial, {} }) }; }
    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override { cur(); if (auto* t = timelines_.get(h.id)) { for (auto& pr : t->pending) if (pr.second) glDeleteSync(pr.second); timelines_.release(h.id); } }
    void signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value) override { if (auto* t = timelines_.get(h.id)) if (value > t->value) t->value = value; }  // host signal
    void timeline_collect(GLTimeline* t, bool block, uint64_t want) {
        cur();
        for (auto it = t->pending.begin(); it != t->pending.end();) {
            GLenum r = glClientWaitSync(it->second, GL_SYNC_FLUSH_COMMANDS_BIT, block ? 1000000000ull : 0);
            if (r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED) {
                if (it->first > t->value) t->value = it->first;
                glDeleteSync(it->second); it = t->pending.erase(it);
            } else ++it;
        }
        (void)want;
    }
    bool wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t value, uint64_t /*timeout*/) override {
        auto* t = timelines_.get(h.id); if (!t) return false;
        timeline_collect(t, /*block=*/true, value);
        return t->value >= value;
    }
    uint64_t get_timeline_value(TimelineSemaphoreHandle h) override {
        auto* t = timelines_.get(h.id); if (!t) return 0;
        timeline_collect(t, /*block=*/false, 0);
        return t->value;
    }
    // Called by submit_commander() to signal a timeline value on GPU completion.
    void signal_timeline_on_submit(TimelineSemaphoreHandle h, uint64_t value) {
        cur(); if (auto* t = timelines_.get(h.id)) t->pending.emplace_back(value, glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    }

    // ---- Sparse / tiled residency -------------------------------------------
    void update_texture_residency(TextureHandle h, const TextureRegion& r, bool resident) override {
        cur(); auto* t = textures_.get(h.id); if (!t) return;
#if defined(GL_TEXTURE_SPARSE_ARB)
        if (glTexPageCommitmentARB) {
            glBindTexture(t->target, t->id);
            glTexPageCommitmentARB(t->target, r.mip, r.x, r.y, r.layer,
                                   r.width, r.height, r.depth ? r.depth : 1, resident ? GL_TRUE : GL_FALSE);
            return;
        }
#endif
        gl_unsupported("sparse texture residency");
    }

    // ---- Ray tracing (no core OpenGL support) -------------------------------
    AccelStructHandle create_acceleration_structure(const AccelStructDesc& d) override { gl_unsupported("acceleration structures"); return { accels_.alloc(GLAccelStruct{ d.type }) }; }
    void destroy_acceleration_structure(AccelStructHandle h) override { accels_.release(h.id); }

    // Internal accessors for the commander.
    GLBuffer*   buffer(int id)   { return buffers_.get(id); }
    GLTexture*  texture(int id)  { return textures_.get(id); }
    GLSampler*  sampler(int id)  { return samplers_.get(id); }
    GLPipeline* pipeline(int id) { return pipelines_.get(id); }
    GLRenderTarget* rt(int id)   { return rts_.get(id); }
    GLQuery*    query(int id)    { return queries_.get(id); }
    GLDescriptorSet* descriptor_set(int id) { return dsets_.get(id); }
    void cur() { if (ctx_) ctx_->make_current(); }

private:
    // Deterministic key for the PSO cache (shaders + a few state bits).
    static uint64_t pipeline_cache_key(const PipelineDesc& d) {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h ^= v + 1; h *= 1099511628211ull; };
        mix(uint64_t(d.vertex_shader.id));   mix(uint64_t(d.fragment_shader.id));
        mix(uint64_t(d.geometry_shader.id)); mix(uint64_t(d.tess_control_shader.id));
        mix(uint64_t(d.tess_eval_shader.id)); mix(uint64_t(d.compute_shader.id));
        mix(uint64_t(int(d.topology)));      mix(uint64_t(d.patch_control_points));
        return h;
    }
    static GLenum buffer_target(BufferType t) {
        switch (t) {
            case BufferType::Index:    return GL_ELEMENT_ARRAY_BUFFER;
            case BufferType::Uniform:  return GL_UNIFORM_BUFFER;
            case BufferType::Storage:  return GL_SHADER_STORAGE_BUFFER;
            case BufferType::Indirect: return GL_DRAW_INDIRECT_BUFFER;
            default:                   return GL_ARRAY_BUFFER;
        }
    }
    static GLenum gl_usage(ResourceUsage u) {
        switch (u) {
            case ResourceUsage::Immutable: return GL_STATIC_DRAW;
            case ResourceUsage::Dynamic:   return GL_DYNAMIC_DRAW;
            case ResourceUsage::Staging:   return GL_STREAM_DRAW;
            default:                       return GL_DYNAMIC_DRAW;
        }
    }
    Graphics* ctx_;
    Pool<GLBuffer> buffers_; Pool<GLTexture> textures_; Pool<GLSampler> samplers_;
    Pool<GLShader> shaders_; Pool<GLPipeline> pipelines_; Pool<GLRenderTarget> rts_;
    Pool<GLFence> fences_; Pool<GLSemaphore> semaphores_; Pool<GLQuery> queries_;
    Pool<GLDescSetLayout> dsls_; Pool<GLPipelineLayout> plls_; Pool<GLDescriptorSet> dsets_;
    Pool<GLPipelineCache> pcaches_; Pool<GLTimeline> timelines_; Pool<GLAccelStruct> accels_;
};

//-----------------------------------------------------------------------------
// GLCommander
//-----------------------------------------------------------------------------
class GLCommander : public GraphicCommander {
public:
    GLCommander(Graphics* ctx, GLDevice* dev) : ctx_(ctx), dev_(dev) {}
    GLDevice* device() const { return dev_; }
    ~GLCommander() override {
        if (ctx_) ctx_->make_current();
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (push_ubo_) glDeleteBuffers(1, &push_ubo_);
    }

    void begin() override {
        if (ctx_) ctx_->make_current();
        if (!vao_) glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        for (int i = 0; i < VertexLayout::MAX_BUFFER_SLOTS; ++i) vbo_[i] = 0;
        ibo_ = 0; pipeline_ = -1;
    }
    void end() override {}

    void set_render_target_backbuffer() override { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) override {
        // Use the first color target's FBO (or the depth target's) as the base, then
        // (re)attach the whole set onto it: colors → COLOR_ATTACHMENT0..N-1 (MRT),
        // depth → DEPTH[_STENCIL]_ATTACHMENT. glDrawBuffers limits writes to the
        // listed attachments, so any stale attachment beyond `count` is ignored.
        GLRenderTarget* base = (count > 0 && colors) ? dev_->rt(colors[0].id) : dev_->rt(depth.id);
        if (!base) { glBindFramebuffer(GL_FRAMEBUFFER, 0); return; }
        glBindFramebuffer(GL_FRAMEBUFFER, base->fbo);

        GLenum draw_bufs[8]; int nbuf = 0;
        for (int i = 0; i < count && i < 8 && colors; ++i) {
            GLuint tex = 0;
            if (auto* rt = dev_->rt(colors[i].id))
                if (auto* t = dev_->texture(rt->color_tex)) tex = t->id;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tex, 0);
            draw_bufs[nbuf++] = GLenum(GL_COLOR_ATTACHMENT0 + i);
        }
        if (nbuf) glDrawBuffers(nbuf, draw_bufs);

        if (depth.valid()) {
            if (auto* dt = dev_->rt(depth.id))
                if (auto* t = dev_->texture(dt->depth_tex)) {
                    const GLenum slot = t->fmt.stencil ? GL_DEPTH_STENCIL_ATTACHMENT
                                                       : GL_DEPTH_ATTACHMENT;
                    glFramebufferTexture2D(GL_FRAMEBUFFER, slot, GL_TEXTURE_2D, t->id, 0);
                }
        }
    }
    void set_viewport(const Viewport& v) override {
        glViewport(GLint(v.x), GLint(v.y), GLsizei(v.width), GLsizei(v.height));
        glDepthRangef(v.min_depth, v.max_depth);
    }
    void set_scissor(const ScissorRect& s) override { glScissor(s.x, s.y, s.width, s.height); }
    void clear_color(const ClearColor& c) override { glClearColor(c.r, c.g, c.b, c.a); glClear(GL_COLOR_BUFFER_BIT); }
    void clear_depth_stencil(const ClearDepthStencil& ds) override {
        glClearDepthf(ds.depth); glClearStencil(ds.stencil);
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void set_pipeline(PipelineHandle h) override {
        pipeline_ = h.id;
        auto* p = dev_->pipeline(h.id); if (!p) return;
        glUseProgram(p->program);
        if (p->is_patch) glPatchParameteri(GL_PATCH_VERTICES, p->patch_points);
        // Blend
        if (p->blend.enabled) {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(gl_blend_factor(p->blend.src_color), gl_blend_factor(p->blend.dst_color),
                                gl_blend_factor(p->blend.src_alpha), gl_blend_factor(p->blend.dst_alpha));
            glBlendEquationSeparate(gl_blend_op(p->blend.color_op), gl_blend_op(p->blend.alpha_op));
        } else glDisable(GL_BLEND);
        const uint8_t m = p->blend.write_mask;
        glColorMask(m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1);
        // Depth / stencil
        if (p->depth.depth_enable) { glEnable(GL_DEPTH_TEST); glDepthFunc(gl_compare(p->depth.depth_func)); }
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(p->depth.depth_write ? GL_TRUE : GL_FALSE);
        if (p->depth.stencil_enable) {
            glEnable(GL_STENCIL_TEST);
            const auto& ff = p->depth.front_face; const auto& bf = p->depth.back_face;
            glStencilOpSeparate(GL_FRONT, gl_stencil_op(ff.stencil_fail), gl_stencil_op(ff.depth_fail), gl_stencil_op(ff.pass));
            glStencilOpSeparate(GL_BACK,  gl_stencil_op(bf.stencil_fail), gl_stencil_op(bf.depth_fail), gl_stencil_op(bf.pass));
            glStencilFuncSeparate(GL_FRONT, gl_compare(ff.func), GLint(stencil_ref_), p->depth.stencil_read_mask);
            glStencilFuncSeparate(GL_BACK,  gl_compare(bf.func), GLint(stencil_ref_), p->depth.stencil_read_mask);
            glStencilMask(p->depth.stencil_write_mask);
        } else glDisable(GL_STENCIL_TEST);
        // Rasterizer
#if !UGW_GL_ES
        glPolygonMode(GL_FRONT_AND_BACK, p->raster.fill_mode == FillMode::Wireframe ? GL_LINE : GL_FILL);  // GLES is always FILL
#endif
        if (p->raster.cull_mode == CullMode::None) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(p->raster.cull_mode == CullMode::Front ? GL_FRONT : GL_BACK); }
        glFrontFace(p->raster.front_face == FrontFace::Clockwise ? GL_CW : GL_CCW);
        if (p->raster.scissor_enable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (p->raster.multisample_enable) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
        if (p->alpha_to_coverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE); else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        // Static depth bias from the pipeline (dynamic set_depth_bias overrides later).
        if (p->raster.depth_bias != 0 || p->raster.slope_scaled_depth_bias != 0.0f) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(p->raster.slope_scaled_depth_bias, float(p->raster.depth_bias));
        } else glDisable(GL_POLYGON_OFFSET_FILL);
    }

    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override {
        if (slot < VertexLayout::MAX_BUFFER_SLOTS) { auto* b = dev_->buffer(h.id); vbo_[slot] = b ? b->id : 0; vbo_off_[slot] = offset; }
    }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override {
        auto* b = dev_->buffer(h.id); ibo_ = b ? b->id : 0; index_type_ = (fmt == IndexFormat::UInt16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        index_off_ = offset; index_size_ = (fmt == IndexFormat::UInt16) ? 2 : 4;
    }
    void bind_texture(uint32_t slot, TextureHandle h) override { auto* t = dev_->texture(h.id); glActiveTexture(GL_TEXTURE0 + slot); if (t) glBindTexture(t->target, t->id); }
    void bind_sampler(uint32_t slot, SamplerHandle h) override { auto* s = dev_->sampler(h.id); if (s) glBindSampler(slot, s->id); }
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t size) override {
        auto* b = dev_->buffer(h.id); if (!b) return;
        if (size) glBindBufferRange(GL_UNIFORM_BUFFER, slot, b->id, offset, size);
        else      glBindBufferBase(GL_UNIFORM_BUFFER, slot, b->id);
    }
    // Emulated push constants → a managed UBO bound at reserved binding 0.
    void push_constants(uint32_t offset, const void* data, uint32_t size) override {
        if (!push_ubo_) { glGenBuffers(1, &push_ubo_); glBindBuffer(GL_UNIFORM_BUFFER, push_ubo_); glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW); }
        glBindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
        glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, push_ubo_);
    }

    void draw(uint32_t vcount, uint32_t first, uint32_t inst, uint32_t base_inst) override {
        configure_attribs();
        const GLenum topo = topology();
        if (inst <= 1 && base_inst == 0) glDrawArrays(topo, first, vcount);
        else glDrawArraysInstancedBaseInstance(topo, first, vcount, inst, base_inst);
    }
    void draw_indexed(uint32_t icount, uint32_t first, int32_t base_vtx, uint32_t inst, uint32_t base_inst) override {
        configure_attribs();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        const GLenum topo = topology();
        const void* off = reinterpret_cast<const void*>(size_t(index_off_) + size_t(first) * index_size_);
        if (inst <= 1 && base_inst == 0 && base_vtx == 0) glDrawElements(topo, icount, index_type_, off);
        else glDrawElementsInstancedBaseVertexBaseInstance(topo, icount, index_type_, off, inst, base_vtx, base_inst);
    }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { glDispatchCompute(x, y, z); }

    // Storage (UAV) bindings.
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t size) override {
        auto* b = dev_->buffer(h.id); if (!b) return;
        if (size) glBindBufferRange(GL_SHADER_STORAGE_BUFFER, slot, b->id, offset, size);
        else      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, b->id);
    }
    void bind_storage_texture(uint32_t slot, TextureHandle h, int mip, StorageAccess access) override {
        auto* t = dev_->texture(h.id); if (!t) return;
        glBindImageTexture(slot, t->id, mip, GL_FALSE, 0, gl_image_access(access), GLenum(t->fmt.internal_fmt));
    }

    // Indirect (GPU-driven) draws / dispatch.
    void draw_indirect(BufferHandle args, uint32_t offset, uint32_t draw_count, uint32_t stride) override {
        auto* b = dev_->buffer(args.id); if (!b) return;
        configure_attribs();
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b->id);
        const void* off = reinterpret_cast<const void*>(size_t(offset));
#if UGW_GL_ES
        if (draw_count > 1) gl_unsupported("multi-draw indirect (GLES: single draw only)");
        glDrawArraysIndirect(topology(), off);   // ES 3.1 supports a single indirect draw
        (void)stride;
#else
        if (draw_count > 1) glMultiDrawArraysIndirect(topology(), off, draw_count, stride);
        else                glDrawArraysIndirect(topology(), off);
#endif
    }
    void draw_indexed_indirect(BufferHandle args, uint32_t offset, uint32_t draw_count, uint32_t stride) override {
        auto* b = dev_->buffer(args.id); if (!b) return;
        configure_attribs();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b->id);
        const void* off = reinterpret_cast<const void*>(size_t(offset));
#if UGW_GL_ES
        if (draw_count > 1) gl_unsupported("multi-draw indexed indirect (GLES: single draw only)");
        glDrawElementsIndirect(topology(), index_type_, off);
        (void)stride;
#else
        if (draw_count > 1) glMultiDrawElementsIndirect(topology(), index_type_, off, draw_count, stride);
        else                glDrawElementsIndirect(topology(), index_type_, off);
#endif
    }
    void dispatch_indirect(BufferHandle args, uint32_t offset) override {
        auto* b = dev_->buffer(args.id); if (!b) return;
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, b->id);
        glDispatchComputeIndirect(GLintptr(offset));
    }

    // Mesh-shader pipeline — not core OpenGL.
    void draw_mesh_tasks(uint32_t, uint32_t, uint32_t) override { gl_unsupported("draw_mesh_tasks"); }
    void draw_mesh_tasks_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { gl_unsupported("draw_mesh_tasks_indirect"); }

    void memory_barrier(uint32_t bits) override { glMemoryBarrier(gl_barrier_bits(bits)); }

    // ---- Copies / blit / resolve --------------------------------------------
    void copy_buffer(BufferHandle dst, uint32_t dst_off, BufferHandle src, uint32_t src_off, uint32_t size) override {
        auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (!s || !d) return;
        glBindBuffer(GL_COPY_READ_BUFFER, s->id);
        glBindBuffer(GL_COPY_WRITE_BUFFER, d->id);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, src_off, dst_off, size);
    }
    void copy_texture(TextureHandle dst, const TextureRegion& dr, TextureHandle src, const TextureRegion& sr) override {
        auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (!s || !d) return;
        const int w = sr.width ? sr.width : s->w, h = sr.height ? sr.height : s->h, depth = sr.depth ? sr.depth : 1;
#if UGW_GL_ES && !defined(GL_ES_VERSION_3_2)
        gl_unsupported("copy_texture (glCopyImageSubData needs GLES 3.2; blit instead)"); (void)w; (void)h; (void)depth; (void)dr;
#else
        glCopyImageSubData(s->id, s->target, sr.mip, sr.x, sr.y, sr.layer,
                           d->id, d->target, dr.mip, dr.x, dr.y, dr.layer, w, h, depth);
#endif
    }
    void blit_render_target(RenderTargetHandle dst, RenderTargetHandle src,
                            int sx0, int sy0, int sx1, int sy1,
                            int dx0, int dy0, int dx1, int dy1, bool linear) override {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_of(src));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_of(dst));
        glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1,
                          GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void resolve_render_target(RenderTargetHandle dst, RenderTargetHandle src) override {
        int w = 0, h = 0;   // resolve covers the dst colour target's extent
        if (auto* rt = dev_->rt(dst.id)) if (auto* t = dev_->texture(rt->color_tex)) { w = t->w; h = t->h; }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_of(src));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_of(dst));
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ---- Queries / debug markers --------------------------------------------
    void write_timestamp(QueryHandle h) override { if (auto* q = dev_->query(h.id)) glQueryCounter(q->id, GL_TIMESTAMP); }
    void begin_query(QueryHandle h) override { if (auto* q = dev_->query(h.id)) glBeginQuery(q->target, q->id); }
    void end_query(QueryHandle h) override   { if (auto* q = dev_->query(h.id)) glEndQuery(q->target); }
    void push_debug_group(const char* name) override { if (glPushDebugGroup) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name ? name : ""); }
    void pop_debug_group() override { if (glPopDebugGroup) glPopDebugGroup(); }
    void insert_debug_marker(const char* name) override {
        if (glDebugMessageInsert)
            glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                                 GL_DEBUG_SEVERITY_NOTIFICATION, -1, name ? name : "");
    }

    // ---- Descriptor sets (emulated: replay the set's writes as slot binds) ----
    void bind_descriptor_set(uint32_t /*set_index*/, DescriptorSetHandle set,
                             const uint32_t* dyn_offsets, int dyn_count) override {
        auto* s = dev_->descriptor_set(set.id); if (!s) return;
        // OpenGL has a flat binding space; the write's `binding` is used directly.
        // Dynamic offsets are consumed in order by Uniform/Storage buffer bindings.
        int dyn = 0;
        for (const auto& w : s->writes) {
            switch (w.type) {
                case BindingType::UniformBuffer:
                case BindingType::StorageBuffer: {
                    auto* b = dev_->buffer(w.buffer.id); if (!b) break;
                    uint32_t off = w.buffer_offset + ((dyn_offsets && dyn < dyn_count) ? dyn_offsets[dyn++] : 0);
                    const GLenum tgt = (w.type == BindingType::UniformBuffer) ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
                    if (w.buffer_size) glBindBufferRange(tgt, w.binding, b->id, off, w.buffer_size);
                    else               glBindBufferBase(tgt, w.binding, b->id);
                    break;
                }
                case BindingType::SampledTexture:
                case BindingType::CombinedImageSampler: {
                    if (auto* t = dev_->texture(w.texture.id)) { glActiveTexture(GL_TEXTURE0 + w.binding); glBindTexture(t->target, t->id); }
                    if (w.type == BindingType::CombinedImageSampler) if (auto* sm = dev_->sampler(w.sampler.id)) glBindSampler(w.binding, sm->id);
                    break;
                }
                case BindingType::Sampler:
                    if (auto* sm = dev_->sampler(w.sampler.id)) glBindSampler(w.binding, sm->id);
                    break;
                case BindingType::StorageTexture:
                    if (auto* t = dev_->texture(w.texture.id))
                        glBindImageTexture(w.binding, t->id, w.texture_mip, GL_FALSE, 0, gl_image_access(w.storage_access), GLenum(t->fmt.internal_fmt));
                    break;
            }
        }
    }

    // ---- Dynamic pipeline state ---------------------------------------------
    void set_stencil_reference(uint32_t ref) override {
        stencil_ref_ = ref;
        auto* p = dev_->pipeline(pipeline_);
        if (p && p->depth.stencil_enable) {
            glStencilFuncSeparate(GL_FRONT, gl_compare(p->depth.front_face.func), GLint(ref), p->depth.stencil_read_mask);
            glStencilFuncSeparate(GL_BACK,  gl_compare(p->depth.back_face.func),  GLint(ref), p->depth.stencil_read_mask);
        }
    }
    void set_blend_constants(const float rgba[4]) override { if (rgba) glBlendColor(rgba[0], rgba[1], rgba[2], rgba[3]); }
    void set_depth_bias(float constant, float /*clamp*/, float slope) override {
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(slope, constant);   // GL lacks a portable bias clamp
    }
    void set_line_width(float w) override { glLineWidth(w); }

    // ---- Multiple viewports / scissors --------------------------------------
    void set_viewports(const Viewport* vps, int count) override {
        if (!vps || count <= 0) return;
#if UGW_GL_ES
        if (count > 1) gl_unsupported("multi-viewport (GLES: viewport 0 only)");
        const Viewport& v0 = vps[0];
        glViewport(GLint(v0.x), GLint(v0.y), GLsizei(v0.width), GLsizei(v0.height));
        glDepthRangef(v0.min_depth, v0.max_depth);
#else
        std::vector<GLfloat> v(size_t(count) * 4); std::vector<GLdouble> d(size_t(count) * 2);
        for (int i = 0; i < count; ++i) {
            v[i*4+0] = vps[i].x; v[i*4+1] = vps[i].y; v[i*4+2] = vps[i].width; v[i*4+3] = vps[i].height;
            d[i*2+0] = vps[i].min_depth; d[i*2+1] = vps[i].max_depth;
        }
        glViewportArrayv(0, count, v.data());
        glDepthRangeArrayv(0, count, d.data());
#endif
    }
    void set_scissors(const ScissorRect* rs, int count) override {
        if (!rs || count <= 0) return;
#if UGW_GL_ES
        if (count > 1) gl_unsupported("multi-scissor (GLES: scissor 0 only)");
        glScissor(rs[0].x, rs[0].y, rs[0].width, rs[0].height);
#else
        std::vector<GLint> s(size_t(count) * 4);
        for (int i = 0; i < count; ++i) { s[i*4+0] = rs[i].x; s[i*4+1] = rs[i].y; s[i*4+2] = rs[i].width; s[i*4+3] = rs[i].height; }
        glScissorArrayv(0, count, s.data());
#endif
    }

    // ---- Indirect draw with a GPU-supplied count (≈ ExecuteIndirect) ---------
    void draw_indirect_count(BufferHandle args, uint32_t args_off, BufferHandle count_buf, uint32_t count_off,
                             uint32_t max_draws, uint32_t stride) override {
#if UGW_GL_ES
        gl_unsupported("draw_indirect_count (no GLES count buffer)"); (void)args; (void)args_off; (void)count_buf; (void)count_off; (void)max_draws; (void)stride;
#else
        auto* a = dev_->buffer(args.id); auto* c = dev_->buffer(count_buf.id); if (!a || !c) return;
        configure_attribs();
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->id);
        glBindBuffer(GL_PARAMETER_BUFFER, c->id);
        glMultiDrawArraysIndirectCount(topology(), reinterpret_cast<const void*>(size_t(args_off)),
                                       GLintptr(count_off), max_draws, stride);
#endif
    }
    void draw_indexed_indirect_count(BufferHandle args, uint32_t args_off, BufferHandle count_buf, uint32_t count_off,
                                     uint32_t max_draws, uint32_t stride) override {
#if UGW_GL_ES
        gl_unsupported("draw_indexed_indirect_count (no GLES count buffer)"); (void)args; (void)args_off; (void)count_buf; (void)count_off; (void)max_draws; (void)stride;
#else
        auto* a = dev_->buffer(args.id); auto* c = dev_->buffer(count_buf.id); if (!a || !c) return;
        configure_attribs();
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, a->id);
        glBindBuffer(GL_PARAMETER_BUFFER, c->id);
        glMultiDrawElementsIndirectCount(topology(), index_type_, reinterpret_cast<const void*>(size_t(args_off)),
                                         GLintptr(count_off), max_draws, stride);
#endif
    }

    // ---- Ray tracing — no core OpenGL support -------------------------------
    void build_acceleration_structure(AccelStructHandle, const AccelStructDesc&) override { gl_unsupported("build_acceleration_structure"); }
    void trace_rays(uint32_t, uint32_t, uint32_t) override { gl_unsupported("trace_rays"); }

private:
    GLuint fbo_of(RenderTargetHandle h) { if (auto* rt = dev_->rt(h.id)) return rt->fbo; return 0; }  // invalid → backbuffer
    GLenum topology() const { auto* p = dev_->pipeline(pipeline_); if (p && p->is_patch) return GL_PATCHES; return p ? gl_topology(p->topology) : GL_TRIANGLES; }
    void configure_attribs() {
        auto* p = dev_->pipeline(pipeline_); if (!p) return;
        const VertexLayout& l = p->layout;
        for (int i = 0; i < l.attribute_count; ++i) {
            const VertexAttribute& a = l.attributes[i];
            const GLuint vbo = (a.buffer_slot < VertexLayout::MAX_BUFFER_SLOTS) ? vbo_[a.buffer_slot] : 0;
            if (!vbo) continue;
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(a.location);
            GLint comps; GLenum type; GLboolean norm; bool integer;
            gl_vertex_attrib(a.format, comps, type, norm, integer);
            const GLsizei stride = GLsizei(l.strides[a.buffer_slot]);
            const void* off = reinterpret_cast<const void*>(size_t(vbo_off_[a.buffer_slot]) + a.offset);
            if (integer) glVertexAttribIPointer(a.location, comps, type, stride, off);
            else         glVertexAttribPointer(a.location, comps, type, norm, stride, off);
            glVertexAttribDivisor(a.location, l.input_rates[a.buffer_slot] == VertexInputRate::PerInstance ? 1u : 0u);
        }
    }

    Graphics* ctx_; GLDevice* dev_;
    GLuint vao_ = 0, push_ubo_ = 0;
    GLuint vbo_[VertexLayout::MAX_BUFFER_SLOTS] = {}; uint32_t vbo_off_[VertexLayout::MAX_BUFFER_SLOTS] = {};
    GLuint ibo_ = 0; GLenum index_type_ = GL_UNSIGNED_INT; uint32_t index_off_ = 0; int index_size_ = 4;
    int pipeline_ = -1;
    uint32_t stencil_ref_ = 0;
};

} // namespace
#endif // WINDOW_SUPPORT_OPENGL

//-----------------------------------------------------------------------------
// OpenGL backend creators (called by the dispatcher in api_render.cpp). Always
// defined; return ErrorNotSupported / no-op when OpenGL is compiled out.
//-----------------------------------------------------------------------------
GraphicDevice* create_device_gl(Graphics* context, Result* out_result) {
#if defined(WINDOW_SUPPORT_OPENGL)
    if (context && context->get_backend() == Backend::OpenGL) {
        if (out_result) *out_result = Result::Success;
        return new GLDevice(context);
    }
#endif
    (void)context;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
GraphicCommander* create_commander_gl(Graphics* context, GraphicDevice* device, QueueType /*queue*/, Result* out_result) {
#if defined(WINDOW_SUPPORT_OPENGL)
    if (context && device && context->get_backend() == Backend::OpenGL) {   // single implicit queue
        if (out_result) *out_result = Result::Success;
        return new GLCommander(context, static_cast<GLDevice*>(device));
    }
#endif
    (void)device;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void submit_commander_gl(Graphics* context, GraphicCommander* commander,
                         FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    (void)context; (void)commander; (void)fence; (void)timeline; (void)value;
#if defined(WINDOW_SUPPORT_OPENGL)
    glFlush(); // GL records immediately; nothing to replay, just flush the stream
    if (commander) {
        GLDevice* dev = static_cast<GLCommander*>(commander)->device();
        if (fence.valid())    dev->signal_fence_on_submit(fence);          // CPU can wait on completion
        if (timeline.valid()) dev->signal_timeline_on_submit(timeline, value);
    }
#endif
}

} // namespace window
