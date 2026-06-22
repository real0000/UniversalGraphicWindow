#pragma once
// Material — a reusable shader+state+bindings object over the render abstraction
// (graphics_api.hpp). Shaders are authored in HLSL (the baseline) in a .hlsl file;
// a .material config (INI) declares the entry points, vertex layout, render state
// and resource bindings. The build compiles the HLSL to per-backend blobs that sit
// next to it (quad.vs_main.spv, quad.ps_main.spv for SPIR-V; .glsl/.msl/.dxbc for
// the others via SPIRV-Cross/fxc). At load time a Material picks the blob for the
// device's backend, builds a pipeline + pipeline layout + descriptor set layout,
// and binds resources by name. Future phases build their draw calls from Materials.

#include "../graphics_api.hpp"
#include "buffer/gpu_buffer.hpp"   // gfx::Buffer family — Material can create + own bindings

#include <memory>
#include <string>
#include <vector>

namespace window {
namespace gfx {

enum class MaterialParamType : uint8_t {
    UniformBuffer, SampledTexture, Sampler, CombinedImageSampler, StorageBuffer, StorageTexture
};

struct MaterialParam {
    std::string       name;
    MaterialParamType type   = MaterialParamType::UniformBuffer;
    uint32_t          set     = 0;
    uint32_t          binding = 0;
    uint32_t          stages  = STAGE_ALL;   // ShaderStageBits
};

// Parsed .material config. Authored alongside the .hlsl shader.
struct MaterialDesc {
    // One entry point per ShaderStage (count == number of ShaderStage values).
    // Empty string = that stage is absent. Covers the whole pipeline: vertex,
    // fragment/pixel, geometry, hull(tess control), domain(tess eval), compute,
    // task(amplification), mesh, and the ray-tracing stages (raygen/miss/closesthit/
    // anyhit/intersection/callable).
    static const int STAGE_COUNT = 14;

    std::string  shader_source;             // HLSL authoring file, e.g. "quad.hlsl"
    std::string  entry[STAGE_COUNT];        // entry[(int)ShaderStage::X]
    VertexLayout vertex_layout;
    BlendState        blend;
    DepthStencilState depth_stencil;
    RasterizerState   rasterizer;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    uint32_t          patch_control_points = 0;   // >0 → tessellation patches
    std::vector<MaterialParam> params;

    bool        has(ShaderStage s) const { return !entry[int(s)].empty(); }
    const char* entry_of(ShaderStage s) const { return entry[int(s)].c_str(); }
    static bool load(const char* path, MaterialDesc* out);   // parse a .material INI
};

class Material;

// A field-addressable view over a uniform/constant block whose member layout came from shader
// reflection. Set fields BY NAME — the byte offset is the shader's own (HLSL cbuffer packing),
// so callers never declare a matching C++ struct, never pad, never align:
//   auto frame = mat->uniform_block("Frame");
//   frame.set("metallic", 0.5f).set("baseColor", color4);
//   frame.flush();                       // upload
// set() copies sizeof(value) bytes (clamped to the field's size) at the field's reflected offset.
class UniformBlock {
public:
    UniformBlock() = default;
    bool     valid() const { return mat_ != nullptr && index_ >= 0; }
    bool     has(const char* field) const;
    uint32_t size() const;                          // block size in bytes
    template <class T>
    UniformBlock& set(const char* field, const T& value) {
        write(field, &value, (uint32_t)sizeof(T)); return *this;
    }
    void set_bytes(const char* field, const void* data, uint32_t size) { write(field, data, size); }
    void flush();                                   // upload the CPU mirror to the GPU

private:
    friend class Material;
    UniformBlock(Material* m, int i) : mat_(m), index_(i) {}
    void write(const char* field, const void* data, uint32_t size);
    Material* mat_   = nullptr;
    int       index_ = -1;
};

class Material {
public:
    // Load a .material config + the shader blob for `device`'s backend from shader_dir.
    static Material* create(GraphicDevice* device, const char* material_path,
                            const char* shader_dir, Result* out_result = nullptr);
    void destroy();

    // ---- Bind existing resources by the names declared in [bindings] --------
    // Each setter writes the slot whose declared type it matches; the binding's type
    // was fixed at load, so e.g. set_storage_buffer targets a `storage_buffer` slot.
    void set_uniform_buffer(const char* name, BufferHandle h, uint32_t offset = 0, uint32_t size = 0);
    void set_storage_buffer(const char* name, BufferHandle h, uint32_t offset = 0, uint32_t size = 0);
    void set_texture(const char* name, TextureHandle h, int mip = 0);   // sampled texture (SRV)
    void set_storage_texture(const char* name, TextureHandle h, int mip = 0,
                             StorageAccess access = StorageAccess::ReadWrite);
    void set_sampler(const char* name, SamplerHandle h);
    // Combined image+sampler slot (one GLSL sampler2D / one binding): set both at once.
    void set_combined_image_sampler(const char* name, TextureHandle tex, SamplerHandle samp);

    // ---- Create + own a buffer for a binding, sized for it, and bind it -----
    // The Material owns the allocation (freed in destroy()), so the caller need not
    // manage lifetime. The backing object is a gfx::Buffer (renderer/buffer); the typed
    // overloads return TypedConstBuffer/TypedStorageBuffer so the data edits like a
    // struct (->/*) or a vector (operator[]) — call flush() to upload. Returns nullptr
    // if `name` is not a declared binding (or allocation fails).
    ConstBuffer*   create_uniform_buffer(const char* name, uint32_t size, const void* initial = nullptr,
                                         ResourceUsage usage = ResourceUsage::Dynamic);
    StorageBuffer* create_storage_buffer(const char* name, uint32_t size, uint32_t stride = 0,
                                         const void* initial = nullptr,
                                         ResourceUsage usage = ResourceUsage::Default);
    template <class T>
    TypedConstBuffer<T>* create_uniform(const char* name, const T& initial = T{},
                                        ResourceUsage usage = ResourceUsage::Dynamic) {
        if (!device_ || find_param(name) < 0) return nullptr;
        auto* b = new TypedConstBuffer<T>();
        if (!b->create(device_, initial, usage, name)) { delete b; return nullptr; }
        set_uniform_buffer(name, b->handle(), 0, (uint32_t)sizeof(T));
        own_(b);
        return b;
    }
    template <class T>
    TypedStorageBuffer<T>* create_storage(const char* name, uint32_t count, const T* data = nullptr,
                                          ResourceUsage usage = ResourceUsage::Default) {
        if (!device_ || find_param(name) < 0) return nullptr;
        auto* b = new TypedStorageBuffer<T>();
        if (!b->create(device_, count, data, usage, name)) { delete b; return nullptr; }
        set_storage_buffer(name, b->handle(), 0, 0);
        own_(b);
        return b;
    }

    // A []-indexed array of per-element uniform structs (e.g. per-object constants). The
    // Material owns it; edit it like std::vector and bind one element by index with
    // set_uniform_element — all GPU alignment (element stride / dynamic offset) is hidden.
    template <class T>
    UniformArray<T>* create_uniform_array(const char* name, uint32_t count,
                                          ResourceUsage usage = ResourceUsage::Default) {
        if (!device_ || find_param(name) < 0) return nullptr;
        auto* a = new UniformArray<T>();
        if (!a->create(device_, count, usage, name)) { delete a; return nullptr; }
        set_uniform_buffer(name, a->handle(), 0, (uint32_t)sizeof(T));   // element 0 by default
        own_(a);
        return a;
    }
    // Bind element `i` of `arr` to the named uniform binding. The caller passes only the index;
    // the aligned byte offset is computed by UniformArray (offset/alignment never surfaces).
    template <class T>
    void set_uniform_element(const char* name, const UniformArray<T>& arr, uint32_t i) {
        set_uniform_buffer(name, arr.handle(), arr.byte_offset(i), arr.element_size());
    }

    // Field-addressable view of a named uniform binding: set members BY NAME with offsets taken
    // from shader reflection — no C++ struct, no packing, no alignment knowledge required (see
    // UniformBlock). The Material auto-creates + owns + binds the block's buffer. Returns an
    // invalid UniformBlock if `name` is not a uniform binding or the shader compiler is absent.
    UniformBlock uniform_block(const char* name);

    // set_pipeline + bind every descriptor set built from the current resources.
    void bind(GraphicCommander* cmd);

    PipelineHandle        pipeline() const { return pipeline_; }
    PipelineLayoutHandle  layout()   const { return layout_; }
    const MaterialDesc&   desc()     const { return desc_; }
    int                   set_count() const { return set_count_; }   // distinct descriptor sets

private:
    Material() = default;
    int find_param(const char* name) const;
    int find_block(const char* name) const;
    void reflect_blocks(const char* shader_dir);   // populate blocks_ from shader reflection
    template <class B> void own_(B* b) {
        owned_buffers_.emplace_back(b, [](void* p) { delete static_cast<B*>(p); });
    }

    friend class UniformBlock;

    static const int MAX_SETS = PipelineLayoutDesc::MAX_SETS;

    // A reflected uniform block: its field offsets (from the shader), a CPU mirror, and the
    // Material-owned GPU buffer it uploads to. Backs uniform_block()'s set-by-name editing.
    struct BlockField { std::string name; uint32_t offset = 0; uint32_t size = 0; };
    struct UniformBlockRec {
        std::string             name;                 // binding name (from the .material)
        uint32_t                set = 0, binding = 0, size = 0;
        std::vector<BlockField> fields;
        std::vector<uint8_t>    cpu;                  // CPU mirror (uploaded by flush)
        ConstBuffer             buf;                  // owned GPU buffer (created on first use)
        bool                    created = false;
    };

    GraphicDevice*            device_ = nullptr;
    MaterialDesc              desc_;
    ShaderHandle              shaders_[MaterialDesc::STAGE_COUNT];   // one per stage (invalid = absent)
    DescriptorSetLayoutHandle set_layouts_[MAX_SETS];   // one layout per descriptor set
    bool                      set_used_[MAX_SETS] = {}; // a set with >=1 binding
    int                       set_count_ = 0;           // = max(param.set) + 1
    PipelineLayoutHandle      layout_;
    PipelineHandle            pipeline_;
    std::vector<DescriptorWrite> writes_;    // current bindings (1:1 with desc_.params)
    DescriptorSetHandle       sets_[MAX_SETS];   // rebuilt when bindings change
    bool                      dirty_ = true;
    // Buffers the Material allocated for its own bindings (type-erased deleters).
    std::vector<std::unique_ptr<void, void(*)(void*)>> owned_buffers_;
    std::vector<UniformBlockRec> blocks_;    // reflected uniform blocks (uniform_block())
};

} // namespace gfx
} // namespace window
