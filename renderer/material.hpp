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
    std::string  shader_source;             // HLSL authoring file, e.g. "quad.hlsl"
    std::string  vertex_entry = "vs_main";
    std::string  pixel_entry  = "ps_main";
    VertexLayout vertex_layout;
    BlendState        blend;
    DepthStencilState depth_stencil;
    RasterizerState   rasterizer;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    std::vector<MaterialParam> params;

    static bool load(const char* path, MaterialDesc* out);   // parse a .material INI
};

class Material {
public:
    // Load a .material config + the shader blob for `device`'s backend from shader_dir.
    static Material* create(GraphicDevice* device, const char* material_path,
                            const char* shader_dir, Result* out_result = nullptr);
    void destroy();

    // Bind resources by the names declared in [bindings].
    void set_uniform_buffer(const char* name, BufferHandle h, uint32_t offset = 0, uint32_t size = 0);
    void set_texture(const char* name, TextureHandle h);
    void set_sampler(const char* name, SamplerHandle h);

    // set_pipeline + bind the descriptor set built from the current resources.
    void bind(GraphicCommander* cmd);

    PipelineHandle        pipeline() const { return pipeline_; }
    PipelineLayoutHandle  layout()   const { return layout_; }
    const MaterialDesc&   desc()     const { return desc_; }

private:
    Material() = default;
    int find_param(const char* name) const;

    GraphicDevice*            device_ = nullptr;
    MaterialDesc              desc_;
    ShaderHandle              vs_, ps_;
    DescriptorSetLayoutHandle set_layout_;
    PipelineLayoutHandle      layout_;
    PipelineHandle            pipeline_;
    std::vector<DescriptorWrite> writes_;    // current bindings (1:1 with desc_.params)
    DescriptorSetHandle       set_;          // rebuilt when bindings change
    bool                      dirty_ = true;
};

} // namespace gfx
} // namespace window
