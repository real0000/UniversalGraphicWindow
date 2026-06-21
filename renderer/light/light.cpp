// renderer/light/light.cpp - implementation of the light data + GPU buffer manager.
//
// This file is purely about marshalling light parameters into the cross-API buffer
// layout documented in light.hpp. No shading happens here.

#include "light.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace window {
namespace gfx {

//=============================================================================
// Light factories
//=============================================================================

Light Light::directional(const math::Vec3& dir, const math::Vec3& color, float intensity) {
    Light l;
    l.type      = LightType::Directional;
    l.direction = dir;
    l.color     = color;
    l.intensity = intensity;
    l.range     = 0.0f;             // directional light never attenuates
    return l;
}

Light Light::point(const math::Vec3& pos, const math::Vec3& color, float intensity, float range) {
    Light l;
    l.type      = LightType::Point;
    l.position  = pos;
    l.color     = color;
    l.intensity = intensity;
    l.range     = range;
    return l;
}

Light Light::spot(const math::Vec3& pos, const math::Vec3& dir, const math::Vec3& color,
                  float intensity, float range, float inner_deg, float outer_deg) {
    Light l;
    l.type           = LightType::Spot;
    l.position       = pos;
    l.direction      = dir;
    l.color          = color;
    l.intensity      = intensity;
    l.range          = range;
    l.inner_cone_deg = inner_deg;
    l.outer_cone_deg = outer_deg;
    return l;
}

Light Light::area(const math::Vec3& pos, const math::Vec3& dir, float width, float height,
                  const math::Vec3& color, float intensity, float range) {
    Light l;
    l.type        = LightType::Area;
    l.position    = pos;
    l.direction   = dir;
    l.color       = color;
    l.intensity   = intensity;
    l.range       = range;
    l.area_width  = width;
    l.area_height = height;
    return l;
}

//=============================================================================
// Packing - host Light -> 64-byte GpuLight
//=============================================================================

GpuLight LightBuffer::pack(const Light& l) {
    GpuLight g{};

    g.position[0] = l.position.x;
    g.position[1] = l.position.y;
    g.position[2] = l.position.z;
    g.range       = l.range > 0.0f ? l.range : 0.0f;

    // Store a unit direction; a degenerate direction falls back to straight down so the
    // shader never divides by a zero-length vector.
    math::Vec3 dir = math::normalize_or(l.direction, math::Vec3(0.0f, -1.0f, 0.0f));
    g.direction[0] = dir.x;
    g.direction[1] = dir.y;
    g.direction[2] = dir.z;
    g.intensity    = l.intensity;

    g.color[0] = l.color.x;
    g.color[1] = l.color.y;
    g.color[2] = l.color.z;
    g.type     = (float)(uint32_t)l.type;

    // Spot cone -> smooth-edge scale/offset: spot = saturate(cosAngle * scale + offset),
    // where cosAngle = dot(direction, -L). Defaults give scale=0, offset=1 (always 1) for
    // non-spot lights, so a shader can apply the term unconditionally if it wants.
    if (l.type == LightType::Spot) {
        float cos_outer = std::cos(math::degrees_to_radians(l.outer_cone_deg));
        float cos_inner = std::cos(math::degrees_to_radians(l.inner_cone_deg));
        float denom     = std::max(cos_inner - cos_outer, 1e-4f);
        g.spot_scale    = 1.0f / denom;
        g.spot_offset   = -cos_outer * g.spot_scale;
    } else {
        g.spot_scale  = 0.0f;
        g.spot_offset = 1.0f;
    }

    g.area_width  = l.area_width;
    g.area_height = l.area_height;
    return g;
}

//=============================================================================
// LightBuffer - lifetime
//=============================================================================

bool LightBuffer::init(GraphicDevice* device, uint32_t max_lights) {
    shutdown();
    if (!device || max_lights == 0) return false;

    device_   = device;
    capacity_ = max_lights;

    // The light array: a uniform buffer sized for the worst case. Default usage so it can
    // be both updated (UpdateSubresource / vkCmdUpdateBuffer) and read back for tests.
    if (!lights_.create(device, capacity_ * (uint32_t)sizeof(GpuLight),
                        ResourceUsage::Default, nullptr, "light.lights")) {
        shutdown();
        return false;
    }
    GpuLightGlobals init_globals{};
    init_globals.ambient[0] = ambient_.x;
    init_globals.ambient[1] = ambient_.y;
    init_globals.ambient[2] = ambient_.z;
    init_globals.light_count = 0;
    if (!globals_.create(device, (uint32_t)sizeof(GpuLightGlobals),
                         ResourceUsage::Default, &init_globals, "light.globals")) {
        shutdown();
        return false;
    }

    cpu_.clear();
    packed_.clear();
    packed_.reserve(capacity_);
    live_count_    = 0;
    dirty_         = true;
    globals_dirty_ = true;
    return true;
}

void LightBuffer::shutdown() {
    lights_.destroy();
    globals_.destroy();
    cpu_.clear();
    packed_.clear();
    device_     = nullptr;
    capacity_   = 0;
    live_count_ = 0;
    dirty_         = true;
    globals_dirty_ = true;
}

//=============================================================================
// LightBuffer - host editing
//=============================================================================

int LightBuffer::add(const Light& l) {
    if (cpu_.size() >= capacity_) return -1;   // GPU array is full
    cpu_.push_back(l);
    dirty_ = true;
    return (int)cpu_.size() - 1;
}

bool LightBuffer::set(int index, const Light& l) {
    if (index < 0 || index >= (int)cpu_.size()) return false;
    cpu_[index] = l;
    dirty_ = true;
    return true;
}

Light* LightBuffer::get(int index) {
    if (index < 0 || index >= (int)cpu_.size()) return nullptr;
    dirty_ = true;   // caller may mutate through the pointer
    return &cpu_[index];
}

const Light* LightBuffer::get(int index) const {
    if (index < 0 || index >= (int)cpu_.size()) return nullptr;
    return &cpu_[index];
}

bool LightBuffer::remove(int index) {
    if (index < 0 || index >= (int)cpu_.size()) return false;
    // Swap-remove: cheap, but indices are not stable across a remove.
    cpu_[index] = cpu_.back();
    cpu_.pop_back();
    dirty_ = true;
    return true;
}

void LightBuffer::clear() {
    if (cpu_.empty()) return;
    cpu_.clear();
    dirty_ = true;
}

void LightBuffer::set_ambient(const math::Vec3& a) {
    ambient_       = a;
    globals_dirty_ = true;
}

//=============================================================================
// LightBuffer - GPU sync
//=============================================================================

void LightBuffer::update() {
    if (!valid()) return;

    if (dirty_) {
        // Compact: only enabled lights get a GPU slot, so the shader loops 0..count.
        packed_.clear();
        for (const Light& l : cpu_) {
            if (l.enabled) packed_.push_back(pack(l));
        }
        const uint32_t n = (uint32_t)packed_.size();
        if (n > 0) {
            lights_.update(packed_.data(), n * (uint32_t)sizeof(GpuLight), 0);
        }
        if (n != live_count_) globals_dirty_ = true;   // count changed -> globals stale
        live_count_ = n;
        dirty_      = false;
    }

    if (globals_dirty_) {
        GpuLightGlobals g{};
        g.ambient[0]   = ambient_.x;
        g.ambient[1]   = ambient_.y;
        g.ambient[2]   = ambient_.z;
        g.light_count  = live_count_;
        globals_.set(g);
        globals_dirty_ = false;
    }
}

//=============================================================================
// LightBuffer - binding
//=============================================================================

void LightBuffer::bind(GraphicCommander* cmd, uint32_t lights_slot, uint32_t globals_slot) const {
    if (!cmd) return;
    if (lights_.valid())  cmd->bind_uniform_buffer(lights_slot,  lights_.handle(),  0, 0);
    if (globals_.valid()) cmd->bind_uniform_buffer(globals_slot, globals_.handle(), 0, 0);
}

} // namespace gfx
} // namespace window
