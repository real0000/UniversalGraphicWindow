#pragma once
// renderer/light/light.hpp
//
// Light sources + the GPU buffer that feeds them to a shader. This subsystem owns the
// DATA side of lighting only: it defines every light type a forward/clustered shader
// cares about, packs them into a fixed, cross-API memory layout, and keeps a constant
// buffer in sync with the host-side scene. It does NOT shade anything - there is no
// pipeline, no render pass, no BRDF here. A shader (e.g. renderer/shaders/pbr.hlsl)
// binds the buffers this class manages and does the actual lighting math.
//
// Why a constant (uniform) buffer and not a storage buffer? Lights are read in the
// vertex/pixel stages, and on D3D11 a storage buffer (raw UAV) only binds to the compute
// stage - a uniform buffer binds to VS+PS+CS on every backend. A fixed-capacity uniform
// array indexed by a per-pixel loop is the portable forward-rendering layout, so that is
// what LightBuffer keeps. Two buffers are managed:
//
//   * lights   - GpuLight[capacity]; only the first `count` entries are live.
//   * globals  - { ambient(rgb), count } header (frame-level lighting constants).
//
// Shader side: include renderer/shaders/lights.hlsli for a ready-made GpuLight struct,
// the cbuffer declarations, and accessors/evaluation (light_count(), get_light(i),
// light_eval()). The layout below is what that header mirrors (HLSL; the GLSL/SPIR-V
// layout is identical - std140-compatible, 16-byte aligned, GpuLight == 64 bytes). Match
// these to LightBuffer::pack():
//
//   struct GpuLight {            // 4 x float4 = 64 bytes
//       float3 position;  float range;        // point/spot/area world pos; falloff radius (0 = none)
//       float3 direction; float intensity;    // dir light travels (normalised); intensity
//       float3 color;     float type;         // linear RGB; LightType as float (0..3)
//       float  spot_scale; float spot_offset; // spot cone: saturate(cos*scale+offset)
//       float  area_width; float area_height; // area-light extents
//   };
//   cbuffer Lights  : register(b?) { GpuLight uLights[CAP]; };
//   cbuffer Globals : register(b?) { float3 uAmbient; uint uLightCount; };
//
//   // per light, with N = surface normal, P = world pos, L = unit vector to the light:
//   //   directional: L = -direction;                              atten = 1
//   //   point/area:  L = normalize(position - P); d = length(...); atten = falloff(d, range)
//   //   spot:        as point, then *= saturate(dot(direction, -L) * spot_scale + spot_offset)
//
// Header-declared / cpp-defined, namespace window::gfx, matching the rest of renderer/.
// Gated by WINDOW_ENABLE_RENDERER (built alongside the buffer wrappers it uses).

#include "../../graphics_api.hpp"
#include "../../math_util.hpp"
#include "../buffer/gpu_buffer.hpp"

#include <cstdint>
#include <vector>

namespace window {
namespace gfx {

//=============================================================================
// LightType - every light source the buffer layout understands.
//=============================================================================
// The numeric values are part of the GPU contract (packed into GpuLight.type); a shader
// branches on them. Ambient is intentionally NOT a type here - it is a single global
// term (LightBuffer::set_ambient), not a per-light source.
enum class LightType : uint32_t {
    Directional = 0,  // infinitely far, parallel rays (the sun). Uses direction only.
    Point       = 1,  // omnidirectional from a position, distance falloff.
    Spot        = 2,  // a point light limited to a cone (inner/outer angle).
    Area        = 3,  // a finite rectangle that emits from one side (size + direction).
};

//=============================================================================
// Light - the host-side description of one source.
//=============================================================================
// Plain editable fields plus factory helpers that fill in the ones a given type uses.
// Fields a type ignores are simply not packed (a Point ignores cone angles, etc.).
// Construct directly or via Light::directional()/point()/spot()/area().
struct Light {
    LightType  type      = LightType::Directional;
    math::Vec3 position  = {0.0f, 0.0f, 0.0f};   // point/spot/area world position
    math::Vec3 direction = {0.0f, -1.0f, 0.0f};  // direction the light travels (dir/spot/area)
    math::Vec3 color     = {1.0f, 1.0f, 1.0f};   // linear RGB (not gamma)
    float      intensity = 1.0f;                  // scalar multiplier on color
    float      range     = 0.0f;                  // point/spot/area falloff radius (0 = no attenuation)
    float      inner_cone_deg = 0.0f;             // spot: full-bright half-angle (degrees)
    float      outer_cone_deg = 45.0f;            // spot: cutoff half-angle (degrees, > inner)
    float      area_width  = 1.0f;                // area: rectangle width
    float      area_height = 1.0f;                // area: rectangle height
    bool       enabled   = true;                  // disabled lights are skipped (not packed)

    // ---- Factories (the conventional way to build each type) -----------------
    static Light directional(const math::Vec3& dir, const math::Vec3& color,
                             float intensity = 1.0f);
    static Light point(const math::Vec3& pos, const math::Vec3& color,
                       float intensity = 1.0f, float range = 10.0f);
    static Light spot(const math::Vec3& pos, const math::Vec3& dir, const math::Vec3& color,
                      float intensity = 1.0f, float range = 10.0f,
                      float inner_deg = 20.0f, float outer_deg = 30.0f);
    static Light area(const math::Vec3& pos, const math::Vec3& dir, float width, float height,
                      const math::Vec3& color, float intensity = 1.0f, float range = 10.0f);
};

//=============================================================================
// GpuLight - the packed, shader-facing representation (64 bytes, std140-safe).
//=============================================================================
// Produced by LightBuffer::pack(); never edited by hand. The four float4 rows match the
// shader struct documented at the top of this file. Spot cone angles are pre-baked into a
// scale/offset so the shader is one madd + saturate (the Frostbite smooth-cone form).
struct GpuLight {
    float    position[3];   float range;        // row 0
    float    direction[3];  float intensity;    // row 1
    float    color[3];      float type;         // row 2 (type stored as float; exact for 0..3)
    float    spot_scale;    float spot_offset;  // row 3 ...
    float    area_width;    float area_height;  // ... row 3
};
static_assert(sizeof(GpuLight) == 64, "GpuLight must be 64 bytes (std140 4x float4)");

// Frame-level lighting constants uploaded next to the array.
struct GpuLightGlobals {
    float    ambient[3];     // flat ambient term (linear RGB)
    uint32_t light_count;    // number of live entries in the lights buffer
};
static_assert(sizeof(GpuLightGlobals) == 16, "GpuLightGlobals must be 16 bytes");

//=============================================================================
// LightBuffer - owns the host light list and the two GPU buffers that mirror it.
//=============================================================================
// Edit lights on the CPU (add/set/remove/clear/get); call update() once per frame to
// push any changes to the GPU; bind() to expose them to a shader. update() is a no-op
// when nothing changed, and only re-uploads the live range. Disabled lights occupy a
// host slot but are compacted out of the GPU array, so the shader loops 0..light_count.
class LightBuffer {
public:
    static constexpr uint32_t kDefaultCapacity = 256;

    LightBuffer() = default;
    ~LightBuffer() { shutdown(); }
    LightBuffer(const LightBuffer&) = delete;
    LightBuffer& operator=(const LightBuffer&) = delete;

    // Allocate the GPU buffers for up to `max_lights` live entries. Must be called before
    // any GPU sync. Safe to call once; re-init shuts the previous buffers down first.
    bool init(GraphicDevice* device, uint32_t max_lights = kDefaultCapacity);
    void shutdown();
    bool valid() const { return device_ != nullptr && lights_.valid(); }

    // ---- Host-side scene editing (nothing touches the GPU until update()) ----
    int      add(const Light& l);          // append; returns index, or -1 if at capacity
    bool     set(int index, const Light& l);
    Light*   get(int index);               // mutable access; marks the buffer dirty
    const Light* get(int index) const;
    bool     remove(int index);            // swap-remove: the last light moves into `index`
    void     clear();                      // remove all lights (keeps the allocation)
    int      count() const { return (int)cpu_.size(); }       // host lights (incl. disabled)
    uint32_t capacity() const { return capacity_; }

    void       set_ambient(const math::Vec3& a);
    math::Vec3 ambient() const { return ambient_; }

    // ---- GPU sync -----------------------------------------------------------
    // Pack the enabled host lights into the GPU layout and upload them plus the globals
    // (ambient + live count). Cheap no-op when nothing changed since the last call.
    void update();

    // Force the next update() to re-upload everything (e.g. after mutating via get()).
    void mark_dirty() { dirty_ = true; }

    // ---- Binding / access ---------------------------------------------------
    // Bind both buffers for a draw: the light array to `lights_slot`, the globals header
    // to `globals_slot`. Call update() first so the GPU data is current.
    void bind(GraphicCommander* cmd, uint32_t lights_slot, uint32_t globals_slot) const;

    const ConstBuffer& lights_buffer()  const { return lights_; }
    const ConstBuffer& globals_buffer() const { return globals_; }
    // Number of live (enabled) lights as of the last update().
    uint32_t live_count() const { return live_count_; }

    // Pack one host light into its 64-byte GPU form (pure; no device needed).
    static GpuLight pack(const Light& l);

private:
    GraphicDevice*        device_   = nullptr;
    ConstBuffer           lights_;            // GpuLight[capacity_]
    ConstBuffer           globals_;           // GpuLightGlobals
    std::vector<Light>    cpu_;               // host scene (includes disabled lights)
    std::vector<GpuLight> packed_;            // upload scratch (enabled lights only)
    math::Vec3            ambient_  = {0.03f, 0.03f, 0.03f};
    uint32_t              capacity_ = 0;
    uint32_t              live_count_ = 0;    // enabled lights uploaded last update()
    bool                  dirty_   = true;    // host lights changed since last update()
    bool                  globals_dirty_ = true;
};

} // namespace gfx
} // namespace window
