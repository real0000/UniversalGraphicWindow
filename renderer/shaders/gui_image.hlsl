// gui_image.hlsl — GUI renderer image fragment shader (read at runtime).
// Pairs with gui.hlsl's vs_main; VSOut must match its output layout.
// Full RGBA sampler2D modulated by the vertex colour (tint).

struct VSOut { float4 pos : SV_Position; float3 uvw : TEXCOORD0; float4 color : TEXCOORD1; float4 sdf : TEXCOORD2; };
Texture2D    uImage : register(t1);
SamplerState uSamp  : register(s2);
float4 ps_image(VSOut i) : SV_Target { return uImage.Sample(uSamp, i.uvw.xy) * i.color; }
