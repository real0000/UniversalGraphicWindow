// api_render_metal.mm — Metal implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Apple platforms only.
//
// Built on UGW's GraphicsMetal: native_device() = id<MTLDevice>, native_context() =
// id<MTLCommandQueue>, native_swapchain() = CAMetalLayer*. Wired into the dispatcher
// in api_render.cpp.
//
// NOTE: written against the Metal API but COMPILED/VERIFIED ONLY ON APPLE. Expects
// ARC (-fobjc-arc, which CMake enables for .mm here). Core paths are implemented
// (resources, MSL pipelines, render-target clear, blit copy/readback, MTLSharedEvent
// as fence + native timeline, command-buffer commander); descriptor sets are
// emulated as direct encoder binds; sparse / ray tracing log "unsupported".

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

#if defined(WINDOW_SUPPORT_METAL) && defined(__APPLE__)

#import <Metal/Metal.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace window {
namespace {

void mtl_unsupported(const char* what) { static std::vector<const char*> seen; for (auto s : seen) if (s == what) return; seen.push_back(what); std::fprintf(stderr, "[UGW/Metal] %s not supported (no-op)\n", what); }

template <class T> struct Pool {
    std::vector<T> items; std::vector<int> free_list;
    int alloc(T v) { if (!free_list.empty()) { int i = free_list.back(); free_list.pop_back(); items[i] = std::move(v); return i; } items.push_back(std::move(v)); return int(items.size()) - 1; }
    T* get(int id) { return (id >= 0 && id < int(items.size())) ? &items[id] : nullptr; }
    void release(int id) { if (id >= 0 && id < int(items.size())) { items[id] = T{}; free_list.push_back(id); } }
};

MTLPixelFormat tex_format(TextureFormat f) {
    switch (f) {
        case TextureFormat::R8_UNORM: return MTLPixelFormatR8Unorm;
        case TextureFormat::RG8_UNORM: return MTLPixelFormatRG8Unorm;
        case TextureFormat::RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
        case TextureFormat::RGBA8_UNORM_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
        case TextureFormat::BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
        case TextureFormat::R16_FLOAT: return MTLPixelFormatR16Float;
        case TextureFormat::RGBA16_FLOAT: return MTLPixelFormatRGBA16Float;
        case TextureFormat::R32_FLOAT: return MTLPixelFormatR32Float;
        case TextureFormat::RGBA32_FLOAT: return MTLPixelFormatRGBA32Float;
        case TextureFormat::D32_FLOAT: return MTLPixelFormatDepth32Float;
        case TextureFormat::D24_UNORM_S8_UINT: return MTLPixelFormatDepth24Unorm_Stencil8;
        default: return MTLPixelFormatRGBA8Unorm;
    }
}

struct MTBuffer  { id<MTLBuffer> buf = nil; uint32_t size = 0; };
struct MTTexture { id<MTLTexture> tex = nil; MTLPixelFormat fmt = MTLPixelFormatRGBA8Unorm; int w = 0, h = 0; };
struct MTSampler { id<MTLSamplerState> s = nil; };
struct MTShader  { id<MTLFunction> fn = nil; ShaderStage stage = ShaderStage::Vertex; };
struct MTPipeline{ id<MTLRenderPipelineState> rps = nil; id<MTLComputePipelineState> cps = nil; id<MTLDepthStencilState> dss = nil; bool compute = false; };
struct MTRenderTarget { int color_tex = -1; int depth_tex = -1; };
struct MTFence   { id<MTLSharedEvent> ev = nil; uint64_t value = 0; };
struct MTQuery   { id<MTLBuffer> buf = nil; QueryType type = QueryType::Timestamp; };
struct MTDescSetLayout { DescriptorSetLayoutDesc desc; };
struct MTDescriptorSet { std::vector<DescriptorWrite> writes; };

class MTDevice : public GraphicDevice {
public:
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;

    explicit MTDevice(Graphics* g) { dev = (__bridge id<MTLDevice>)g->native_device(); queue = (__bridge id<MTLCommandQueue>)g->native_context(); }

    Backend get_backend() const override { return Backend::Metal; }
    void get_capabilities(GraphicsCapabilities* out) const override {
        if (!out) return;
        out->max_texture_size = 16384; out->max_color_attachments = 8; out->max_bound_descriptor_sets = 8;
        out->min_uniform_buffer_offset_alignment = 256; out->max_push_constant_size = 4096;
        out->compute_shaders = true; out->instancing = true; out->indirect_draw = true; out->mesh_shaders = true;
    }

    BufferHandle create_buffer(const BufferDesc& d) override {
        MTBuffer b; b.size = d.size ? d.size : 16;
        b.buf = d.initial_data ? [dev newBufferWithBytes:d.initial_data length:b.size options:MTLResourceStorageModeShared]
                               : [dev newBufferWithLength:b.size options:MTLResourceStorageModeShared];
        return { buffers_.alloc(b) };
    }
    void update_buffer(BufferHandle h, const void* data, uint32_t size, uint32_t offset) override { auto* b = buffers_.get(h.id); if (b && b->buf && data) std::memcpy((uint8_t*)b->buf.contents + offset, data, size); }
    void destroy_buffer(BufferHandle h) override { buffers_.release(h.id); }

    TextureHandle create_texture(const TextureDesc& d) override {
        MTTexture t; t.fmt = tex_format(d.format); t.w = d.width; t.h = d.height;
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:t.fmt width:d.width height:d.height mipmapped:(d.mip_levels != 1)];
        if (d.array_layers > 1) { td.textureType = MTLTextureType2DArray; td.arrayLength = d.array_layers; }
        MTLTextureUsage u = 0;
        if (d.usage & TEXTURE_USAGE_SAMPLED) u |= MTLTextureUsageShaderRead;
        if (d.usage & (TEXTURE_USAGE_RENDER_TARGET | TEXTURE_USAGE_DEPTH_STENCIL)) u |= MTLTextureUsageRenderTarget;
        if (d.usage & TEXTURE_USAGE_STORAGE) u |= MTLTextureUsageShaderWrite;
        td.usage = u;
        t.tex = [dev newTextureWithDescriptor:td];
        int id = textures_.alloc(t);
        if (d.initial_data) { TextureRegion r; r.width = d.width; r.height = d.height; update_texture({ id }, r, d.initial_data); }
        return { id };
    }
    void update_texture(TextureHandle h, const TextureRegion& r, const void* data) override {
        auto* t = textures_.get(h.id); if (!t || !t->tex || !data) return;
        [t->tex replaceRegion:MTLRegionMake2D(r.x, r.y, r.width, r.height) mipmapLevel:r.mip withBytes:data bytesPerRow:r.width * 4];
    }
    void generate_mipmaps(TextureHandle h) override { auto* t = textures_.get(h.id); if (!t) return; id<MTLCommandBuffer> cb = [queue commandBuffer]; id<MTLBlitCommandEncoder> e = [cb blitCommandEncoder]; [e generateMipmapsForTexture:t->tex]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted]; }
    void destroy_texture(TextureHandle h) override { textures_.release(h.id); }

    SamplerHandle create_sampler(const SamplerState& s) override { MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = (s.min_filter == FilterMode::Point ? MTLSamplerMinMagFilterNearest : MTLSamplerMinMagFilterLinear); MTSampler smp; smp.s = [dev newSamplerStateWithDescriptor:sd]; return { samplers_.alloc(smp) }; }
    void destroy_sampler(SamplerHandle h) override { samplers_.release(h.id); }

    ShaderHandle create_shader(const ShaderDesc& d) override {
        if (d.language != ShaderLanguage::MSL) { mtl_unsupported("non-MSL shader (provide Metal source/metallib)"); return { -1 }; }
        NSError* err = nil;
        NSString* src = [[NSString alloc] initWithBytes:d.code length:d.code_size encoding:NSUTF8StringEncoding];
        id<MTLLibrary> lib = [dev newLibraryWithSource:src options:nil error:&err];
        if (!lib) { mtl_unsupported("MSL compile"); return { -1 }; }
        MTShader sh; sh.stage = d.stage; sh.fn = [lib newFunctionWithName:[NSString stringWithUTF8String:(d.entry_point ? d.entry_point : "main0")]];
        return sh.fn ? ShaderHandle{ shaders_.alloc(sh) } : ShaderHandle{ -1 };
    }
    void destroy_shader(ShaderHandle h) override { shaders_.release(h.id); }

    PipelineHandle create_pipeline(const PipelineDesc& d) override {
        MTPipeline p; NSError* err = nil;
        if (auto* cs = shaders_.get(d.compute_shader.id)) { p.compute = true; p.cps = [dev newComputePipelineStateWithFunction:cs->fn error:&err]; return { pipelines_.alloc(p) }; }
        MTLRenderPipelineDescriptor* rd = [MTLRenderPipelineDescriptor new];
        if (auto* vs = shaders_.get(d.vertex_shader.id)) rd.vertexFunction = vs->fn;
        if (auto* ps = shaders_.get(d.fragment_shader.id)) rd.fragmentFunction = ps->fn;
        for (int i = 0; i < d.color_format_count; ++i) rd.colorAttachments[i].pixelFormat = tex_format(d.color_formats[i]);
        rd.colorAttachments[0].blendingEnabled = d.blend.enabled;
        p.rps = [dev newRenderPipelineStateWithDescriptor:rd error:&err];
        if (err) mtl_unsupported("render pipeline");
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new]; dd.depthWriteEnabled = d.depth_stencil.depth_write; dd.depthCompareFunction = d.depth_stencil.depth_enable ? MTLCompareFunctionLess : MTLCompareFunctionAlways;
        p.dss = [dev newDepthStencilStateWithDescriptor:dd];
        return { pipelines_.alloc(p) };
    }
    void destroy_pipeline(PipelineHandle h) override { pipelines_.release(h.id); }

    RenderTargetHandle create_render_target(const RenderTargetDesc& d) override { TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.usage = TEXTURE_USAGE_SAMPLED | TEXTURE_USAGE_RENDER_TARGET; MTRenderTarget rt; rt.color_tex = create_texture(td).id; return { rts_.alloc(rt) }; }
    RenderTargetHandle create_depth_target(const DepthStencilDesc& d) override { TextureDesc td; td.width = d.width; td.height = d.height; td.format = d.format; td.usage = TEXTURE_USAGE_DEPTH_STENCIL; MTRenderTarget rt; rt.depth_tex = create_texture(td).id; return { rts_.alloc(rt) }; }
    TextureHandle render_target_texture(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return { -1 }; return { rt->color_tex >= 0 ? rt->color_tex : rt->depth_tex }; }
    void destroy_render_target(RenderTargetHandle h) override { auto* rt = rts_.get(h.id); if (!rt) return; if (rt->color_tex >= 0) destroy_texture({ rt->color_tex }); if (rt->depth_tex >= 0) destroy_texture({ rt->depth_tex }); rts_.release(h.id); }
    TextureHandle create_texture_view(const TextureViewDesc& d) override { auto* s = textures_.get(d.texture.id); if (!s) return { -1 }; MTTexture v; v.fmt = (d.format == TextureFormat::Unknown) ? s->fmt : tex_format(d.format); v.w = s->w; v.h = s->h; v.tex = [s->tex newTextureViewWithPixelFormat:v.fmt]; return { textures_.alloc(v) }; }

    // MTLSharedEvent doubles as fence and native timeline.
    FenceHandle create_fence(bool) override { MTFence f; f.ev = [dev newSharedEvent]; f.value = 0; return { fences_.alloc(f) }; }
    void destroy_fence(FenceHandle h) override { fences_.release(h.id); }
    bool wait_fence(FenceHandle h, uint64_t) override { auto* f = fences_.get(h.id); if (!f) return false; while (f->ev.signaledValue < f->value) { } return true; }
    bool get_fence_status(FenceHandle h) override { auto* f = fences_.get(h.id); return f && f->ev.signaledValue >= f->value; }
    void reset_fence(FenceHandle) override {}
    SemaphoreHandle create_semaphore() override { return { sems_.alloc(0) }; }
    void destroy_semaphore(SemaphoreHandle h) override { sems_.release(h.id); }
    void wait_idle() override { id<MTLCommandBuffer> cb = [queue commandBuffer]; [cb commit]; [cb waitUntilCompleted]; }
    TimelineSemaphoreHandle create_timeline_semaphore(uint64_t initial) override { MTFence t; t.ev = [dev newSharedEvent]; t.ev.signaledValue = initial; return { timelines_.alloc(t) }; }
    void destroy_timeline_semaphore(TimelineSemaphoreHandle h) override { timelines_.release(h.id); }
    void signal_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v) override { if (auto* t = timelines_.get(h.id)) t->ev.signaledValue = v; }
    bool wait_timeline_semaphore(TimelineSemaphoreHandle h, uint64_t v, uint64_t) override { auto* t = timelines_.get(h.id); if (!t) return false; while (t->ev.signaledValue < v) { } return true; }
    uint64_t get_timeline_value(TimelineSemaphoreHandle h) override { auto* t = timelines_.get(h.id); return t ? t->ev.signaledValue : 0; }

    QueryHandle create_query(QueryType type) override { MTQuery q; q.type = type; q.buf = [dev newBufferWithLength:8 options:MTLResourceStorageModeShared]; return { queries_.alloc(q) }; }
    void destroy_query(QueryHandle h) override { queries_.release(h.id); }
    bool get_query_result(QueryHandle h, uint64_t* out, bool) override { auto* q = queries_.get(h.id); if (!q) return false; if (out) *out = *(uint64_t*)q->buf.contents; return true; }

    void* map_buffer(BufferHandle h, uint32_t offset, uint32_t) override { auto* b = buffers_.get(h.id); return b && b->buf ? (uint8_t*)b->buf.contents + offset : nullptr; }   // shared storage
    void unmap_buffer(BufferHandle) override {}
    void read_buffer(BufferHandle h, void* dst, uint32_t size, uint32_t offset) override { auto* b = buffers_.get(h.id); if (b && b->buf && dst) std::memcpy(dst, (uint8_t*)b->buf.contents + offset, size); }
    void read_texture(TextureHandle h, const TextureRegion& r, void* dst) override { auto* t = textures_.get(h.id); if (t && t->tex && dst) [t->tex getBytes:dst bytesPerRow:r.width * 4 fromRegion:MTLRegionMake2D(r.x, r.y, r.width, r.height) mipmapLevel:r.mip]; }

    DescriptorSetLayoutHandle create_descriptor_set_layout(const DescriptorSetLayoutDesc& d) override { return { dsls_.alloc(MTDescSetLayout{ d }) }; }
    void destroy_descriptor_set_layout(DescriptorSetLayoutHandle h) override { dsls_.release(h.id); }
    PipelineLayoutHandle create_pipeline_layout(const PipelineLayoutDesc&) override { return { plls_.alloc(0) }; }   // Metal binds by index, no root signature object
    void destroy_pipeline_layout(PipelineLayoutHandle h) override { plls_.release(h.id); }
    DescriptorSetHandle create_descriptor_set(const DescriptorSetDesc& d) override { MTDescriptorSet s; s.writes.assign(d.writes, d.writes + d.write_count); return { dsets_.alloc(std::move(s)) }; }
    void update_descriptor_set(DescriptorSetHandle h, const DescriptorSetDesc& d) override { if (auto* s = dsets_.get(h.id)) s->writes.assign(d.writes, d.writes + d.write_count); }
    void destroy_descriptor_set(DescriptorSetHandle h) override { dsets_.release(h.id); }
    void set_debug_name(ObjectType type, uint32_t id, const char* name) override { if (!name) return; if (type == ObjectType::Texture) if (auto* t = textures_.get(int(id))) t->tex.label = [NSString stringWithUTF8String:name]; }

    PipelineCacheHandle create_pipeline_cache(const void*, size_t) override { return { pcaches_.alloc(0) }; }   // MTLBinaryArchive — TODO
    size_t get_pipeline_cache_data(PipelineCacheHandle, void*, size_t) override { return 0; }
    void destroy_pipeline_cache(PipelineCacheHandle h) override { pcaches_.release(h.id); }
    void update_texture_residency(TextureHandle, const TextureRegion&, bool) override { mtl_unsupported("sparse residency (MTLHeap sparse)"); }
    AccelStructHandle create_acceleration_structure(const AccelStructDesc&) override { mtl_unsupported("acceleration structures (MTLAccelerationStructure)"); return { accels_.alloc(0) }; }
    void destroy_acceleration_structure(AccelStructHandle h) override { accels_.release(h.id); }

    MTBuffer* buffer(int id) { return buffers_.get(id); }
    MTTexture* texture(int id) { return textures_.get(id); }
    MTSampler* sampler(int id) { return samplers_.get(id); }
    MTPipeline* pipeline(int id) { return pipelines_.get(id); }
    MTRenderTarget* rt(int id) { return rts_.get(id); }
    MTFence* fence(int id) { return fences_.get(id); }
    MTFence* timeline(int id) { return timelines_.get(id); }
    MTDescriptorSet* descriptor_set(int id) { return dsets_.get(id); }

    Pool<MTBuffer> buffers_; Pool<MTTexture> textures_; Pool<MTSampler> samplers_; Pool<MTShader> shaders_;
    Pool<MTPipeline> pipelines_; Pool<MTRenderTarget> rts_; Pool<MTFence> fences_; Pool<int> sems_;
    Pool<MTFence> timelines_; Pool<MTQuery> queries_; Pool<MTDescSetLayout> dsls_; Pool<int> plls_;
    Pool<MTDescriptorSet> dsets_; Pool<int> pcaches_; Pool<int> accels_;
};

class MTCommander : public GraphicCommander {
public:
    MTCommander(MTDevice* d) : dev_(d) {}
    MTDevice* device() const { return dev_; }
    id<MTLCommandBuffer> command_buffer() { return cb_; }

    void begin() override { cb_ = [dev_->queue commandBuffer]; }
    void end() override { if (enc_) { [enc_ endEncoding]; enc_ = nil; } }
    void set_render_target_backbuffer() override { mtl_unsupported("backbuffer target (CAMetalLayer drawable path TODO)"); }
    void set_render_targets(const RenderTargetHandle* colors, int count, RenderTargetHandle depth) override {
        if (enc_) { [enc_ endEncoding]; enc_ = nil; }
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        for (int i = 0; i < count && colors; ++i) if (auto* rt = dev_->rt(colors[i].id)) if (auto* t = dev_->texture(rt->color_tex)) { rp.colorAttachments[i].texture = t->tex; rp.colorAttachments[i].loadAction = MTLLoadActionLoad; rp.colorAttachments[i].storeAction = MTLStoreActionStore; }
        if (depth.valid()) if (auto* rt = dev_->rt(depth.id)) if (auto* t = dev_->texture(rt->depth_tex)) rp.depthAttachment.texture = t->tex;
        pending_rp_ = rp;
    }
    void clear_color(const ClearColor& c) override { if (pending_rp_) { pending_rp_.colorAttachments[0].loadAction = MTLLoadActionClear; pending_rp_.colorAttachments[0].clearColor = MTLClearColorMake(c.r, c.g, c.b, c.a); } }
    void clear_depth_stencil(const ClearDepthStencil& ds) override { if (pending_rp_) { pending_rp_.depthAttachment.loadAction = MTLLoadActionClear; pending_rp_.depthAttachment.clearDepth = ds.depth; } }
    void ensure_encoder() { if (!enc_ && pending_rp_) { enc_ = [cb_ renderCommandEncoderWithDescriptor:pending_rp_]; if (cur_ && cur_->rps) { [enc_ setRenderPipelineState:cur_->rps]; if (cur_->dss) [enc_ setDepthStencilState:cur_->dss]; } } }
    void set_viewport(const Viewport& v) override { ensure_encoder(); if (enc_) [enc_ setViewport:(MTLViewport){ v.x, v.y, v.width, v.height, v.min_depth, v.max_depth }]; }
    void set_scissor(const ScissorRect& r) override { ensure_encoder(); if (enc_) [enc_ setScissorRect:(MTLScissorRect){ (NSUInteger)r.x, (NSUInteger)r.y, (NSUInteger)r.width, (NSUInteger)r.height }]; }
    void set_pipeline(PipelineHandle h) override { cur_ = dev_->pipeline(h.id); if (enc_ && cur_ && cur_->rps) { [enc_ setRenderPipelineState:cur_->rps]; if (cur_->dss) [enc_ setDepthStencilState:cur_->dss]; } }
    void bind_vertex_buffer(uint32_t slot, BufferHandle h, uint32_t offset) override { ensure_encoder(); auto* b = dev_->buffer(h.id); if (enc_ && b) [enc_ setVertexBuffer:b->buf offset:offset atIndex:slot]; }
    void bind_index_buffer(BufferHandle h, IndexFormat fmt, uint32_t offset) override { auto* b = dev_->buffer(h.id); if (b) { index_buf_ = b->buf; index_type_ = fmt == IndexFormat::UInt16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; index_off_ = offset; } }
    void bind_texture(uint32_t slot, TextureHandle h) override { ensure_encoder(); auto* t = dev_->texture(h.id); if (enc_ && t) [enc_ setFragmentTexture:t->tex atIndex:slot]; }
    void bind_sampler(uint32_t slot, SamplerHandle h) override { ensure_encoder(); auto* s = dev_->sampler(h.id); if (enc_ && s) [enc_ setFragmentSamplerState:s->s atIndex:slot]; }
    void bind_uniform_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override { ensure_encoder(); auto* b = dev_->buffer(h.id); if (enc_ && b) { [enc_ setVertexBuffer:b->buf offset:offset atIndex:slot]; [enc_ setFragmentBuffer:b->buf offset:offset atIndex:slot]; } }
    void push_constants(uint32_t, const void* data, uint32_t size) override { ensure_encoder(); if (enc_) { [enc_ setVertexBytes:data length:size atIndex:30]; [enc_ setFragmentBytes:data length:size atIndex:30]; } }
    void bind_storage_buffer(uint32_t slot, BufferHandle h, uint32_t offset, uint32_t) override { ensure_encoder(); auto* b = dev_->buffer(h.id); if (enc_ && b) [enc_ setFragmentBuffer:b->buf offset:offset atIndex:slot]; }
    void bind_storage_texture(uint32_t slot, TextureHandle h, int, StorageAccess) override { ensure_encoder(); auto* t = dev_->texture(h.id); if (enc_ && t) [enc_ setFragmentTexture:t->tex atIndex:slot]; }
    void bind_descriptor_set(uint32_t, DescriptorSetHandle h, const uint32_t*, int) override {
        auto* s = dev_->descriptor_set(h.id); if (!s) return;
        for (const auto& w : s->writes) {
            if (w.type == BindingType::UniformBuffer || w.type == BindingType::StorageBuffer) bind_uniform_buffer(w.binding, w.buffer, w.buffer_offset, 0);
            else if (w.type == BindingType::SampledTexture || w.type == BindingType::CombinedImageSampler) { bind_texture(w.binding, w.texture); if (w.type == BindingType::CombinedImageSampler) bind_sampler(w.binding, w.sampler); }
            else if (w.type == BindingType::Sampler) bind_sampler(w.binding, w.sampler);
            else if (w.type == BindingType::StorageTexture) bind_storage_texture(w.binding, w.texture, w.texture_mip, w.storage_access);
        }
    }
    void draw(uint32_t vc, uint32_t first, uint32_t inst, uint32_t) override { ensure_encoder(); if (enc_) [enc_ drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:first vertexCount:vc instanceCount:(inst ? inst : 1)]; }
    void draw_indexed(uint32_t ic, uint32_t, int32_t, uint32_t inst, uint32_t) override { ensure_encoder(); if (enc_ && index_buf_) [enc_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:ic indexType:index_type_ indexBuffer:index_buf_ indexBufferOffset:index_off_ instanceCount:(inst ? inst : 1)]; }
    void dispatch(uint32_t x, uint32_t y, uint32_t z) override { if (enc_) { [enc_ endEncoding]; enc_ = nil; } id<MTLComputeCommandEncoder> ce = [cb_ computeCommandEncoder]; if (cur_ && cur_->cps) [ce setComputePipelineState:cur_->cps]; [ce dispatchThreadgroups:MTLSizeMake(x, y, z) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)]; [ce endEncoding]; }
    void draw_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_indirect"); }
    void draw_indexed_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_indexed_indirect"); }
    void dispatch_indirect(BufferHandle, uint32_t) override { mtl_unsupported("dispatch_indirect"); }
    void draw_mesh_tasks(uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_mesh_tasks (drawMeshThreadgroups)"); }
    void draw_mesh_tasks_indirect(BufferHandle, uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_mesh_tasks_indirect"); }
    void memory_barrier(uint32_t) override {}
    void copy_buffer(BufferHandle dst, uint32_t doff, BufferHandle src, uint32_t soff, uint32_t size) override { auto* s = dev_->buffer(src.id); auto* d = dev_->buffer(dst.id); if (!s || !d) return; if (enc_) { [enc_ endEncoding]; enc_ = nil; } id<MTLBlitCommandEncoder> be = [cb_ blitCommandEncoder]; [be copyFromBuffer:s->buf sourceOffset:soff toBuffer:d->buf destinationOffset:doff size:size]; [be endEncoding]; }
    void copy_texture(TextureHandle dst, const TextureRegion&, TextureHandle src, const TextureRegion&) override { auto* s = dev_->texture(src.id); auto* d = dev_->texture(dst.id); if (!s || !d) return; if (enc_) { [enc_ endEncoding]; enc_ = nil; } id<MTLBlitCommandEncoder> be = [cb_ blitCommandEncoder]; [be copyFromTexture:s->tex toTexture:d->tex]; [be endEncoding]; }
    void blit_render_target(RenderTargetHandle, RenderTargetHandle, int,int,int,int,int,int,int,int, bool) override { mtl_unsupported("blit_render_target"); }
    void resolve_render_target(RenderTargetHandle, RenderTargetHandle) override { mtl_unsupported("resolve_render_target (use a resolve attachment)"); }
    void write_timestamp(QueryHandle) override { mtl_unsupported("write_timestamp (MTLCounterSampleBuffer)"); }
    void begin_query(QueryHandle) override {}
    void end_query(QueryHandle) override {}
    void push_debug_group(const char* name) override { if (cb_ && name) [cb_ pushDebugGroup:[NSString stringWithUTF8String:name]]; }
    void pop_debug_group() override { if (cb_) [cb_ popDebugGroup]; }
    void insert_debug_marker(const char*) override {}
    void set_stencil_reference(uint32_t ref) override { ensure_encoder(); if (enc_) [enc_ setStencilReferenceValue:ref]; }
    void set_blend_constants(const float rgba[4]) override { ensure_encoder(); if (enc_) [enc_ setBlendColorRed:rgba[0] green:rgba[1] blue:rgba[2] alpha:rgba[3]]; }
    void set_depth_bias(float c, float clamp, float slope) override { ensure_encoder(); if (enc_) [enc_ setDepthBias:c slopeScale:slope clamp:clamp]; }
    void set_line_width(float) override {}
    void set_viewports(const Viewport* v, int n) override { ensure_encoder(); if (!enc_ || n <= 0) return; std::vector<MTLViewport> vs(n); for (int i = 0; i < n; ++i) vs[i] = { v[i].x, v[i].y, v[i].width, v[i].height, v[i].min_depth, v[i].max_depth }; [enc_ setViewports:vs.data() count:n]; }
    void set_scissors(const ScissorRect* r, int n) override { ensure_encoder(); if (!enc_ || n <= 0) return; std::vector<MTLScissorRect> rs(n); for (int i = 0; i < n; ++i) rs[i] = { (NSUInteger)r[i].x, (NSUInteger)r[i].y, (NSUInteger)r[i].width, (NSUInteger)r[i].height }; [enc_ setScissorRects:rs.data() count:n]; }
    void draw_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_indirect_count"); }
    void draw_indexed_indirect_count(BufferHandle, uint32_t, BufferHandle, uint32_t, uint32_t, uint32_t) override { mtl_unsupported("draw_indexed_indirect_count"); }
    void build_acceleration_structure(AccelStructHandle, const AccelStructDesc&) override { mtl_unsupported("build_acceleration_structure"); }
    void trace_rays(uint32_t, uint32_t, uint32_t) override { mtl_unsupported("trace_rays"); }

private:
    MTDevice* dev_ = nil; id<MTLCommandBuffer> cb_ = nil; id<MTLRenderCommandEncoder> enc_ = nil;
    MTLRenderPassDescriptor* pending_rp_ = nil; MTPipeline* cur_ = nullptr;
    id<MTLBuffer> index_buf_ = nil; MTLIndexType index_type_ = MTLIndexTypeUInt32; uint32_t index_off_ = 0;
};

} // namespace

GraphicDevice* create_device_metal(Graphics* context, Result* out_result) {
    if (!context || context->get_backend() != Backend::Metal || !context->native_device()) { if (out_result) *out_result = Result::ErrorNotSupported; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new MTDevice(context);
}
GraphicCommander* create_commander_metal(Graphics* context, GraphicDevice* device, QueueType, Result* out_result) {
    if (!context || !device) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    if (out_result) *out_result = Result::Success;
    return new MTCommander(static_cast<MTDevice*>(device));
}
void submit_commander_metal(Graphics*, GraphicCommander* commander, FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    if (!commander) return;
    auto* c = static_cast<MTCommander*>(commander); auto* d = c->device();
    id<MTLCommandBuffer> cb = c->command_buffer(); if (!cb) return;
    if (fence.valid()) if (auto* f = d->fence(fence.id)) { f->value++; [cb encodeSignalEvent:f->ev value:f->value]; }
    if (timeline.valid()) if (auto* t = d->timeline(timeline.id)) [cb encodeSignalEvent:t->ev value:value];
    [cb commit];
}

} // namespace window

#else  // not Apple / Metal disabled — not-supported stubs

namespace window {
GraphicDevice*    create_device_metal(Graphics*, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
GraphicCommander* create_commander_metal(Graphics*, GraphicDevice*, QueueType, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
void              submit_commander_metal(Graphics*, GraphicCommander*, FenceHandle, TimelineSemaphoreHandle, uint64_t) {}
} // namespace window

#endif
