// Mesh-shader source for example_gpu_features (D3D12 mesh pipeline check).
//
// Compiled to DXIL and embedded as gpu_features_mesh_dxil.h — mesh shaders need Shader
// Model 6.5, which the built-in runtime compiler (HLSL -> DXBC via d3dcompiler) cannot
// produce, so this one shader is prebuilt like the other examples' bytecode.
//
// Regenerate (Windows SDK 10.0.28000 or newer):
//   dxc -T ms_6_5 -E ms_main -Fo mesh.dxil gpu_features_mesh.hlsl
//   dxc -T ps_6_5 -E ps_main -Fo pix.dxil  gpu_features_mesh.hlsl
// then embed both as byte arrays (see the header's comment).
//
// The mesh shader emits one oversized triangle covering the whole target and the pixel
// shader paints it green, so a correct dispatch fills the render target — a mesh pipeline
// that silently failed to rasterize leaves the clear colour behind and is caught.

struct VOut {
    float4 pos : SV_Position;
};

[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void ms_main(out vertices VOut verts[3], out indices uint3 tris[1]) {
    SetMeshOutputCounts(3, 1);
    verts[0].pos = float4(-1.0, -1.0, 0.0, 1.0);
    verts[1].pos = float4( 3.0, -1.0, 0.0, 1.0);
    verts[2].pos = float4(-1.0,  3.0, 0.0, 1.0);
    tris[0] = uint3(0, 1, 2);
}

float4 ps_main(VOut i) : SV_Target {
    return float4(0.0, 1.0, 0.0, 1.0);
}
