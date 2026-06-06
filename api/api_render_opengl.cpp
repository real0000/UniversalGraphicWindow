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

#if defined(WINDOW_SUPPORT_OPENGL)
#include "glad.h"
#include <cstring>
#include <vector>
#endif

namespace window {

#if defined(WINDOW_SUPPORT_OPENGL)
namespace {

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
        case VertexFormat::Int1: comps=1; type=GL_INT; integer=true; return;
        case VertexFormat::Int2: comps=2; type=GL_INT; integer=true; return;
        case VertexFormat::Int4: comps=4; type=GL_INT; integer=true; return;
        case VertexFormat::UInt1: comps=1; type=GL_UNSIGNED_INT; integer=true; return;
        case VertexFormat::UByte4N: comps=4; type=GL_UNSIGNED_BYTE; norm=GL_TRUE; return;
        case VertexFormat::Byte4N: comps=4; type=GL_BYTE; norm=GL_TRUE; return;
        case VertexFormat::Half2: comps=2; type=GL_HALF_FLOAT; return;
        case VertexFormat::Half4: comps=4; type=GL_HALF_FLOAT; return;
        case VertexFormat::UShort2N: comps=2; type=GL_UNSIGNED_SHORT; norm=GL_TRUE; return;
        case VertexFormat::UShort4N: comps=4; type=GL_UNSIGNED_SHORT; norm=GL_TRUE; return;
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
struct GLTexture  { GLuint id = 0; GLenum target = GL_TEXTURE_2D; GLFormat fmt{}; int w = 0, h = 0; };
struct GLSampler  { GLuint id = 0; };
struct GLShader   { GLuint id = 0; ShaderStage stage = ShaderStage::Vertex; };
struct GLPipeline {
    GLuint program = 0;
    VertexLayout layout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    BlendState blend; DepthStencilState depth; RasterizerState raster;
    bool compute = false;
};
struct GLRenderTarget { GLuint fbo = 0; int color_tex = -1; int depth_tex = -1; };

//-----------------------------------------------------------------------------
// GLDevice
//-----------------------------------------------------------------------------
class GLDevice : public GraphicDevice {
public:
    explicit GLDevice(Graphics* ctx) : ctx_(ctx) {}
    ~GLDevice() override = default;

    Backend get_backend() const override { return Backend::OpenGL; }
    void get_capabilities(GraphicsCapabilities* out) const override { if (ctx_ && out) ctx_->get_capabilities(out); }

    BufferHandle create_buffer(const BufferDesc& d) override {
        cur();
        GLBuffer b; b.target = buffer_target(d.type); b.size = d.size;
        glGenBuffers(1, &b.id);
        glBindBuffer(b.target, b.id);
        glBufferData(b.target, d.size, d.initial_data, gl_usage(d.usage));
        return { buffers_.alloc(b) };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override {
        cur(); if (auto* b = buffers_.get(h.id)) { glBindBuffer(b->target, b->id); glBufferSubData(b->target, offset, size, data); }
    }
    void destroy_buffer(BufferHandle h) override { cur(); if (auto* b = buffers_.get(h.id)) { glDeleteBuffers(1, &b->id); buffers_.release(h.id); } }

    TextureHandle create_texture(const TextureDesc& d) override {
        cur();
        GLTexture t; t.fmt = gl_format(d.format); t.w = d.width; t.h = d.height;
        t.target = (d.cube ? GL_TEXTURE_CUBE_MAP : (d.array_layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D));
        glGenTextures(1, &t.id);
        glBindTexture(t.target, t.id);
        if (t.target == GL_TEXTURE_2D) {
            glTexImage2D(GL_TEXTURE_2D, 0, t.fmt.internal_fmt, d.width, d.height, 0, t.fmt.format, t.fmt.type, d.initial_data);
        } else if (t.target == GL_TEXTURE_2D_ARRAY) {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, t.fmt.internal_fmt, d.width, d.height, d.array_layers, 0, t.fmt.format, t.fmt.type, d.initial_data);
        }
        glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, d.mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return { textures_.alloc(t) };
    }
    void update_texture(TextureHandle h, const TextureRegion& r, const void* data) override {
        cur(); if (auto* t = textures_.get(h.id)) {
            glBindTexture(t->target, t->id);
            if (t->target == GL_TEXTURE_2D)
                glTexSubImage2D(GL_TEXTURE_2D, r.mip, r.x, r.y, r.width, r.height, t->fmt.format, t->fmt.type, data);
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
            case ShaderStage::Fragment: type = GL_FRAGMENT_SHADER; break;
            case ShaderStage::Geometry: type = GL_GEOMETRY_SHADER; break;
            case ShaderStage::Compute:  type = GL_COMPUTE_SHADER; break;
            default: break;
        }
        GLShader sh; sh.stage = d.stage; sh.id = glCreateShader(type);
        const char* src = static_cast<const char*>(d.code);
        const GLint len = d.code_size ? GLint(d.code_size) : -1;
        if (len < 0) glShaderSource(sh.id, 1, &src, nullptr);
        else         glShaderSource(sh.id, 1, &src, &len);
        glCompileShader(sh.id);
        GLint ok = 0; glGetShaderiv(sh.id, GL_COMPILE_STATUS, &ok);
        if (!ok) { glDeleteShader(sh.id); sh.id = 0; return { -1 }; }
        return { shaders_.alloc(sh) };
    }
    void destroy_shader(ShaderHandle h) override { cur(); if (auto* s = shaders_.get(h.id)) { if (s->id) glDeleteShader(s->id); shaders_.release(h.id); } }

    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        cur();
        GLPipeline p; p.layout = d.vertex_layout; p.topology = d.topology;
        p.blend = d.blend; p.depth = d.depth_stencil; p.raster = d.rasterizer;
        p.program = glCreateProgram();
        auto attach = [&](ShaderHandle h) { if (auto* s = shaders_.get(h.id)) glAttachShader(p.program, s->id); };
        if (d.compute_shader.valid()) { p.compute = true; attach(d.compute_shader); }
        else { attach(d.vertex_shader); attach(d.fragment_shader); }
        glLinkProgram(p.program);
        GLint ok = 0; glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(p.program); return { -1 }; }
        return { pipelines_.alloc(p) };
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

    // Internal accessors for the commander.
    GLBuffer*   buffer(int id)   { return buffers_.get(id); }
    GLTexture*  texture(int id)  { return textures_.get(id); }
    GLSampler*  sampler(int id)  { return samplers_.get(id); }
    GLPipeline* pipeline(int id) { return pipelines_.get(id); }
    GLRenderTarget* rt(int id)   { return rts_.get(id); }
    void cur() { if (ctx_) ctx_->make_current(); }

private:
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
};

//-----------------------------------------------------------------------------
// GLCommander
//-----------------------------------------------------------------------------
class GLCommander : public GraphicCommander {
public:
    GLCommander(Graphics* ctx, GLDevice* dev) : ctx_(ctx), dev_(dev) {}
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
        // Single-FBO simplification: bind the first color target's FBO (or depth's).
        int id = (count > 0 && colors) ? colors[0].id : depth.id;
        if (auto* rt = dev_->rt(id)) glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        else glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
            glStencilFuncSeparate(GL_FRONT, gl_compare(ff.func), 0, p->depth.stencil_read_mask);
            glStencilFuncSeparate(GL_BACK,  gl_compare(bf.func), 0, p->depth.stencil_read_mask);
            glStencilMask(p->depth.stencil_write_mask);
        } else glDisable(GL_STENCIL_TEST);
        // Rasterizer
        glPolygonMode(GL_FRONT_AND_BACK, p->raster.fill_mode == FillMode::Wireframe ? GL_LINE : GL_FILL);
        if (p->raster.cull_mode == CullMode::None) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(p->raster.cull_mode == CullMode::Front ? GL_FRONT : GL_BACK); }
        glFrontFace(p->raster.front_face == FrontFace::Clockwise ? GL_CW : GL_CCW);
        if (p->raster.scissor_enable) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (p->raster.multisample_enable) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
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

private:
    GLenum topology() const { auto* p = dev_->pipeline(pipeline_); return p ? gl_topology(p->topology) : GL_TRIANGLES; }
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
        }
    }

    Graphics* ctx_; GLDevice* dev_;
    GLuint vao_ = 0, push_ubo_ = 0;
    GLuint vbo_[VertexLayout::MAX_BUFFER_SLOTS] = {}; uint32_t vbo_off_[VertexLayout::MAX_BUFFER_SLOTS] = {};
    GLuint ibo_ = 0; GLenum index_type_ = GL_UNSIGNED_INT; uint32_t index_off_ = 0; int index_size_ = 4;
    int pipeline_ = -1;
};

} // namespace
#endif // WINDOW_SUPPORT_OPENGL

//-----------------------------------------------------------------------------
// Public factory (always defined; dispatches by backend).
//-----------------------------------------------------------------------------
GraphicDevice* create_device(Graphics* context, Result* out_result) {
#if defined(WINDOW_SUPPORT_OPENGL)
    if (context && context->get_backend() == Backend::OpenGL) {
        if (out_result) *out_result = Result::Success;
        return new GLDevice(context);
    }
#endif
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void destroy_device(GraphicDevice* device) { delete device; }

GraphicCommander* create_commander(Graphics* context, GraphicDevice* device, Result* out_result) {
#if defined(WINDOW_SUPPORT_OPENGL)
    if (context && device && context->get_backend() == Backend::OpenGL) {
        if (out_result) *out_result = Result::Success;
        return new GLCommander(context, static_cast<GLDevice*>(device));
    }
#endif
    (void)device;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void destroy_commander(GraphicCommander* commander) { delete commander; }

void submit_commander(Graphics* context, GraphicCommander* commander) {
    (void)context; (void)commander;
#if defined(WINDOW_SUPPORT_OPENGL)
    glFlush(); // GL records immediately; nothing to replay, just flush the stream
#endif
}

} // namespace window
