// pbr.hlsl — physically-based metallic-roughness surface shader (Cook-Torrance BRDF).
//
// Authoring baseline for PbrRenderer. Consumes the asset MeshVertex layout
// (pos/normal/tangent/uv0/uv1/color) and the asset GpuMaterial textures. One
// directional light + flat ambient. Cross-API: dxc -spirv → SPIR-V (Vulkan + OpenGL
// via GL_ARB_gl_spirv), and the D3D backend compiles this HLSL at runtime (D3DCompile).
//
//   fxc/dxc DXBC : (D3D backend compiles the source at runtime — no blob needed)
//   SPIR-V       : dxc -T vs_6_0 -E vs_main -spirv pbr.hlsl -Fo pbr.vs_main.spv
//                  dxc -T ps_6_0 -E ps_main -spirv pbr.hlsl -Fo pbr.ps_main.spv
//
// VKB(n) tags a resource with its descriptor-set binding for the SPIR-V path (dxc
// -DVK_SPIRV); fxc/DXBC ignores it and uses register() instead. The RHI maps a binding
// number straight to the backend slot per type (UBO→bN, texture→tN, sampler→sN), so the
// register() index equals the binding (textures start at t3, the shared sampler at s8).
#ifdef VK_SPIRV
#define VKB(n) [[vk::binding(n)]]
#else
#define VKB(n)
#endif

VKB(0) cbuffer Frame    : register(b0) {
    float4x4 uViewProj;
    float3   uCameraPos;   float _fpad0;
    float3   uLightDir;    float _fpad1;   // direction the light travels (world space)
    float3   uLightColor;  float uLightIntensity;
    float3   uAmbient;     float _fpad2;
};
VKB(1) cbuffer Object   : register(b1) {
    float4x4 uModel;
    float4x4 uNormalMat;   // inverse-transpose of uModel (upper 3x3 in a 4x4)
};
VKB(2) cbuffer Material : register(b2) {
    float4 uBaseColor;     // base-colour factor (RGBA)
    float4 uEmissive;      // emissive factor (RGB) + exposure in .a (unused, pad)
    float  uMetallic;
    float  uRoughness;
    float  uOcclusion;     // occlusion strength
    float  uNormalScale;
};

VKB(3) Texture2D    uBaseColorTex  : register(t3);
VKB(4) Texture2D    uNormalTex     : register(t4);
VKB(5) Texture2D    uMetalRoughTex : register(t5);  // G = roughness, B = metallic
VKB(6) Texture2D    uEmissiveTex   : register(t6);
VKB(7) Texture2D    uOcclusionTex  : register(t7);  // R = ambient occlusion
VKB(8) SamplerState uSamp          : register(s8);

struct VSIn {
    float3 pos     : TEXCOORD0;
    float3 normal  : TEXCOORD1;
    float4 tangent : TEXCOORD2;   // xyz tangent, w = bitangent sign
    float2 uv0     : TEXCOORD3;
    float2 uv1     : TEXCOORD4;
    float4 color   : TEXCOORD5;
};
struct VSOut {
    float4 pos      : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float4 tangent  : TEXCOORD2;
    float2 uv       : TEXCOORD3;
    float4 color    : TEXCOORD4;
};

VSOut vs_main(VSIn i) {
    VSOut o;
    float4 wp   = mul(uModel, float4(i.pos, 1.0));
    o.worldPos  = wp.xyz;
    o.pos       = mul(uViewProj, wp);
    o.normal    = normalize(mul((float3x3)uNormalMat, i.normal));
    o.tangent   = float4(normalize(mul((float3x3)uModel, i.tangent.xyz)), i.tangent.w);
    o.uv        = i.uv0;
    o.color     = i.color;
    return o;
}

static const float PI = 3.14159265359;

float3 fresnel_schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}
float distribution_ggx(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}
float geometry_smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float4 ps_main(VSOut i) : SV_Target {
    float4 baseSample = uBaseColorTex.Sample(uSamp, i.uv);
    float3 albedo     = baseSample.rgb * uBaseColor.rgb * i.color.rgb;
    float  alpha      = baseSample.a   * uBaseColor.a   * i.color.a;

    float3 mr        = uMetalRoughTex.Sample(uSamp, i.uv).rgb;
    float  roughness = saturate(mr.g * uRoughness);
    float  metallic  = saturate(mr.b * uMetallic);
    roughness        = clamp(roughness, 0.04, 1.0);

    // Tangent-space normal map → world space.
    float3 N = normalize(i.normal);
    float3 T = normalize(i.tangent.xyz - N * dot(N, i.tangent.xyz));
    float3 B = cross(N, T) * i.tangent.w;
    float3 nTS = uNormalTex.Sample(uSamp, i.uv).xyz * 2.0 - 1.0;
    nTS.xy *= uNormalScale;
    N = normalize(T * nTS.x + B * nTS.y + N * nTS.z);

    float3 V = normalize(uCameraPos - i.worldPos);
    float3 L = normalize(-uLightDir);
    float3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float  D  = distribution_ggx(NdotH, roughness);
    float  G  = geometry_smith(NdotV, NdotL, roughness);
    float3 F  = fresnel_schlick(VdotH, F0);

    float3 spec = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-4);
    float3 kd   = (1.0 - F) * (1.0 - metallic);
    float3 radiance = uLightColor * uLightIntensity;
    float3 Lo   = (kd * albedo / PI + spec) * radiance * NdotL;

    float occ = lerp(1.0, uOcclusionTex.Sample(uSamp, i.uv).r, uOcclusion);
    float3 ambient  = uAmbient * albedo * occ;
    float3 emissive = uEmissiveTex.Sample(uSamp, i.uv).rgb * uEmissive.rgb;

    float3 color = ambient + Lo + emissive;
    color = color / (color + 1.0);              // Reinhard tonemap
    color = pow(color, 1.0 / 2.2);              // → approx sRGB
    return float4(color, alpha);
}
