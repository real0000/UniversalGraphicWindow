// gui.hlsl — GUI renderer shaders (read at runtime from renderer/shaders/).
//
// One vertex shader feeds two fragment shaders (atlas/solid/SDF and image).
// Vertex format: pos.xy | uvw (z selects the fragment path) | rgba | sdf.
//   uvw.z <  -1.5  → procedural SDF shape (rounded box / circle / ring);
//                     uvw.xy = local position, sdf = (half_w, half_h, radius, border)
//   uvw.z <   0.0  → solid colour quad
//   uvw.z >= 4096  → RGBA colour-emoji glyph (layer = z - 4096)
//   otherwise      → R8 SDF text glyph (layer = z)
// Compiled cross-API by the built-in shader_compiler; the .hlsl is the source
// of truth (no prebuilt blobs).

cbuffer Proj : register(b0) { float4x4 uProjection; };

struct VSIn  { float2 pos : TEXCOORD0; float3 uvw : TEXCOORD1; float4 color : TEXCOORD2; float4 sdf : TEXCOORD3; };
struct VSOut { float4 pos : SV_Position; float3 uvw : TEXCOORD0; float4 color : TEXCOORD1; float4 sdf : TEXCOORD2; };

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos   = mul(uProjection, float4(i.pos, 0.0, 1.0));
    o.uvw   = i.uvw;
    o.color = i.color;
    o.sdf   = i.sdf;
    return o;
}

Texture2DArray uAtlas      : register(t1);   // R8 signed-distance-field glyph atlas (text)
SamplerState   uSamp       : register(s2);
Texture2DArray uColorAtlas : register(t3);   // RGBA colour-emoji glyph atlas

float4 ps_atlas(VSOut i) : SV_Target {
    // Procedural SDF shape (layer = -2): rounded box / circle / bordered ring.
    // uvw.xy = fragment position in the shape's local space (centred at 0);
    // sdf = (half_w, half_h, corner_radius, border_width). One quad, curves stay
    // smooth at any scale, AA is free via the screen-space derivative.
    if (i.uvw.z < -1.5) {
        float2 p = i.uvw.xy;
        float2 b = i.sdf.xy;
        float  r = i.sdf.z;
        float  bw = i.sdf.w;
        float2 q = abs(p) - b + r;
        float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        if (bw > 0.0) d = abs(d) - bw * 0.5;              // filled → outline ring
        float aa = fwidth(d);
        float a = aa > 0.0 ? clamp(0.5 - d / aa, 0.0, 1.0) : (d <= 0.0 ? 1.0 : 0.0);
        return float4(i.color.rgb, i.color.a * a);
    }
    if (i.uvw.z < 0.0) return i.color;                    // solid colour quad
    if (i.uvw.z >= 4096.0) {                              // colour emoji: sample RGBA directly
        float3 cuvw = float3(i.uvw.xy, i.uvw.z - 4096.0);
        float4 c = uColorAtlas.Sample(uSamp, cuvw);
        return float4(c.rgb, c.a * i.color.a);
    }
    // SDF text glyph: 0.5 = the glyph edge. Threshold with screen-space-derivative AA,
    // so the same atlas stays crisp at any scale; tint by the vertex colour (the 染色).
    float d  = uAtlas.Sample(uSamp, i.uvw).r;
    float aa = fwidth(d);
    float a  = aa > 0.0 ? smoothstep(0.5 - aa, 0.5 + aa, d) : step(0.5, d);
    return float4(i.color.rgb, i.color.a * a);
}
