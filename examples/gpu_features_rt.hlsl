// Ray-tracing shader library for example_gpu_features (DXR trace_rays check).
//
// Compiled to a DXIL LIBRARY (lib_6_3) and embedded as gpu_features_rt_dxil.h — a ray
// tracing pipeline is assembled by naming exports out of one library, not by binding one
// shader per stage, so this is a single blob with several entry points.
//
// Regenerate (Windows SDK 10.0.28000 or newer):
//   dxc -T lib_6_3 -Fo rt.dxil gpu_features_rt.hlsl
//
// Global bindings (shared by every shader, from the pipeline's global root signature):
//   t0 = the top-level acceleration structure
//   u0 = the output image
// Local binding (per shader-record, from the hit group's LOCAL root signature):
//   b0 = the hit colour. This is the point of the test: the value travels in the shader
//        binding table record itself, not in any globally bound resource, so reading it
//        back proves per-record arguments are encoded and addressed correctly.

// The [[vk::...]] attributes are read only when compiling to SPIR-V and ignored for DXIL,
// so one source serves both. They are needed because the two APIs bind differently:
//   * D3D12 keeps t0 and u0 in separate register spaces, so both can be "0".
//     Vulkan has ONE binding number space per set, so they must differ.
//   * A per-record argument is a local root signature constant buffer in DXR, but a
//     "shader record buffer" in Vulkan — same bytes in the shader binding table, different
//     declaration.
[[vk::binding(0, 0)]] RaytracingAccelerationStructure uScene : register(t0);
[[vk::binding(1, 0)]] RWTexture2D<float4> uOut : register(u0);

struct HitCB { float4 color; };
[[vk::shader_record_ext]] ConstantBuffer<HitCB> uHit : register(b0);
static const float4 uHitColor = uHit.color;

struct Payload { float4 color; };

[shader("raygeneration")]
void rgen_main() {
    uint2 px = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;

    // Fire straight down -Z through a plane at z = -1, so the triangle covers a known,
    // checkable part of the image rather than the whole of it.
    RayDesc ray;
    ray.Origin = float3((px.x + 0.5) / dim.x * 2.0 - 1.0,
                        (px.y + 0.5) / dim.y * 2.0 - 1.0,
                        1.0);
    ray.Direction = float3(0.0, 0.0, -1.0);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    Payload p;
    p.color = float4(0, 0, 0, 1);
    TraceRay(uScene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
    uOut[px] = p.color;
}

[shader("miss")]
void miss_main(inout Payload p) {
    p.color = float4(0.0, 0.0, 1.0, 1.0);   // blue where nothing was hit
}

[shader("closesthit")]
void chit_main(inout Payload p, in BuiltInTriangleIntersectionAttributes attr) {
    p.color = uHitColor;                    // colour supplied by THIS record's local args
}
