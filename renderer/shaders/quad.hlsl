// quad.hlsl — sample Material shader (HLSL is the authoring baseline).
// Textured quad: position+uv+color vertex, a transform cbuffer, a texture+sampler.
// Compiled per backend: SPIR-V (glslang/dxc) for Vulkan & GL(via SPIRV-Cross→GLSL),
// DXBC/DXIL (fxc/dxc) for D3D, MSL (SPIRV-Cross) for Metal. vk::binding picks the
// descriptor-set bindings used by the .material config.
[[vk::binding(0)]] cbuffer Transform { float4x4 uProj; };
[[vk::binding(1)]] Texture2D    uTex;
[[vk::binding(2)]] SamplerState uSamp;

struct VSIn  { float2 pos : TEXCOORD0; float2 uv : TEXCOORD1; float4 color : TEXCOORD2; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 color : TEXCOORD1; };

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos   = mul(uProj, float4(i.pos, 0.0, 1.0));
    o.uv    = i.uv;
    o.color = i.color;
    return o;
}
float4 ps_main(VSOut i) : SV_Target { return uTex.Sample(uSamp, i.uv) * i.color; }
