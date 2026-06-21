// lights.hlsli - shader-side access to the renderer/light GPU buffers.
//
// Include this in an HLSL shader to read the lights that LightBuffer (renderer/light/
// light.hpp) uploads. It mirrors the C++ GpuLight / GpuLightGlobals byte layout exactly,
// declares the two constant buffers, and gives you ready-made accessors + per-light
// evaluation so a forward shader is just a loop. It does NO BRDF: light_eval() returns the
// direction to the light and the (attenuated) radiance; you multiply by your own shading.
//
// Authoring matches the rest of renderer/shaders: write HLSL, compile OFFLINE per backend
//   SPIR-V (Vulkan, and OpenGL 4.6 via ARB_gl_spirv):
//     dxc -T ps_6_0 -E ps_main -DVK_SPIRV -spirv your.hlsl -Fo your.ps_main.spv
//   GLSL (OpenGL):  spirv-cross your.ps_main.spv --version 460 --no-es > your.ps_main.glsl
//   DXBC (D3D11/12): dxc -T ps_6_0 -E ps_main your.hlsl -Fo your.ps_main.dxbc   (or fxc)
// The backends consume the resulting blob; nothing here is compiled at runtime (except GL,
// which compiles the generated GLSL).
//
// ---- Usage -----------------------------------------------------------------------------
//   // (optional) override before including; defaults shown:
//   #define LIGHT_CAPACITY         256   // MUST be >= the LightBuffer capacity you bound
//   #define LIGHT_LIGHTS_BINDING   9     // register/binding of the GpuLight[] array cbuffer
//   #define LIGHT_GLOBALS_BINDING  10    // register/binding of the {ambient,count} cbuffer
//   #include "lights.hlsli"              // declares the two cbuffers at those bindings
//
//   float3 Lo = light_ambient() * albedo;          // flat ambient term
//   for (uint i = 0; i < light_count(); ++i) {
//       LightSample s = light_eval(get_light(i), worldPos);
//       float NdotL  = max(dot(N, s.L), 0.0);
//       Lo += my_brdf(N, V, s.L, ...) * s.radiance * NdotL;
//   }
//
// Bind order from C++:  lightBuffer.bind(cmd, LIGHT_LIGHTS_BINDING, LIGHT_GLOBALS_BINDING);
// (and on the descriptor-set backends, set 0, those binding numbers.)

#ifndef LIGHTS_HLSLI_INCLUDED
#define LIGHTS_HLSLI_INCLUDED

//----------------------------------------------------------------------------------------
// Configuration
//----------------------------------------------------------------------------------------
#ifndef LIGHT_CAPACITY
#define LIGHT_CAPACITY 256
#endif
#ifndef LIGHT_LIGHTS_BINDING
#define LIGHT_LIGHTS_BINDING 9
#endif
#ifndef LIGHT_GLOBALS_BINDING
#define LIGHT_GLOBALS_BINDING 10
#endif

// Cross-API binding tag: dxc -DVK_SPIRV stamps a descriptor-set binding for SPIR-V; the
// DXBC path ignores it and uses register() (same convention as pbr.hlsl).
#ifndef VKB
  #ifdef VK_SPIRV
    #define VKB(n) [[vk::binding(n)]]
  #else
    #define VKB(n)
  #endif
#endif

// Paste a register letter and a (possibly macro) index into one token, e.g. b + 9 -> b9.
#define LIGHT_REG_(p, n) p##n
#define LIGHT_REG(p, n)  LIGHT_REG_(p, n)

//----------------------------------------------------------------------------------------
// Light type ids (must match window::gfx::LightType)
//----------------------------------------------------------------------------------------
#define LIGHT_TYPE_DIRECTIONAL 0u
#define LIGHT_TYPE_POINT       1u
#define LIGHT_TYPE_SPOT        2u
#define LIGHT_TYPE_AREA        3u

//----------------------------------------------------------------------------------------
// GpuLight - 64 bytes, field-for-field identical to the C++ struct (4x float4 in a cbuffer:
// HLSL packs each float3+trailing float into one 16-byte register, matching the C++ POD).
//----------------------------------------------------------------------------------------
struct GpuLight {
    float3 position;    float range;        // world position (point/spot/area); falloff radius (0 = none)
    float3 direction;   float intensity;    // unit direction the light TRAVELS; scalar intensity
    float3 color;       float type;         // linear RGB; LightType as float (use light_type())
    float  spot_scale;  float spot_offset;  // spot cone: saturate(cos*scale + offset)
    float  area_width;  float area_height;  // area-light rectangle extents
};

// Per-light result of light_eval(): everything the BRDF needs except the BRDF itself.
struct LightSample {
    float3 L;            // unit vector from the surface TOWARD the light
    float3 radiance;     // color * intensity * attenuation (incl. spot cone / distance)
    float  attenuation;  // scalar attenuation alone (1.0 for directional), for inspection
};

//----------------------------------------------------------------------------------------
// Constant buffers (declared automatically at the configured bindings)
//----------------------------------------------------------------------------------------
VKB(LIGHT_LIGHTS_BINDING)
cbuffer LightArrayBlock : register(LIGHT_REG(b, LIGHT_LIGHTS_BINDING)) {
    GpuLight gLights[LIGHT_CAPACITY];
};
VKB(LIGHT_GLOBALS_BINDING)
cbuffer LightGlobalsBlock : register(LIGHT_REG(b, LIGHT_GLOBALS_BINDING)) {
    float3 gAmbient;       // flat ambient term (linear RGB)
    uint   gLightCount;    // number of live entries in gLights
};

//----------------------------------------------------------------------------------------
// Accessors
//----------------------------------------------------------------------------------------
uint     light_count()        { return gLightCount; }
float3   light_ambient()      { return gAmbient; }
GpuLight get_light(uint i)    { return gLights[i]; }
uint     light_type(GpuLight l) { return (uint)(l.type + 0.5); }   // robust float->id

//----------------------------------------------------------------------------------------
// Distance attenuation: inverse-square with a smooth window so the contribution reaches
// exactly 0 at `range` (Karis/Frostbite). range <= 0 means "no falloff" (pure 1/d^2).
//----------------------------------------------------------------------------------------
float light_distance_attenuation(float dist, float range) {
    float d2  = dist * dist;
    float inv = 1.0 / max(d2, 1e-4);
    if (range <= 0.0) return inv;
    float f      = d2 / (range * range);          // 0 at light, 1 at range
    float window = saturate(1.0 - f * f);
    return inv * window * window;
}

// Spot cone factor in [0,1] from the pre-baked scale/offset. `L` is surface->light (unit).
// Non-spot lights pack scale=0, offset=1, so this returns 1 for them too.
float light_spot_factor(GpuLight l, float3 L) {
    return saturate(dot(l.direction, -L) * l.spot_scale + l.spot_offset);
}

//----------------------------------------------------------------------------------------
// Evaluate one light at a world-space surface point. Handles every LightType:
//   directional -> L = -direction, no attenuation
//   point/area  -> L toward position, distance attenuation (area treated as a point emitter;
//                  area_width/height are carried for shaders that do real area integration)
//   spot        -> point + cone factor
//----------------------------------------------------------------------------------------
LightSample light_eval(GpuLight l, float3 world_pos) {
    LightSample s;
    uint  t     = light_type(l);
    float atten = 1.0;

    if (t == LIGHT_TYPE_DIRECTIONAL) {
        s.L = -l.direction;
    } else {
        float3 to_light = l.position - world_pos;
        float  dist     = length(to_light);
        s.L   = (dist > 1e-5) ? (to_light / dist) : float3(0.0, 1.0, 0.0);
        atten = light_distance_attenuation(dist, l.range);
        if (t == LIGHT_TYPE_SPOT) atten *= light_spot_factor(l, s.L);
    }

    s.attenuation = atten;
    s.radiance    = l.color * (l.intensity * atten);
    return s;
}

#endif // LIGHTS_HLSLI_INCLUDED
