#pragma once
// Per-backend factory entry points for the render abstraction (GraphicDevice /
// GraphicCommander). The public factory in api_render.cpp dispatches on the
// context's Backend to one of these; each lives in its own api_render_<backend>
// translation unit and self-guards with its WINDOW_SUPPORT_* macro (returning
// ErrorNotSupported / no-op when compiled out).
//
// Backend → file mapping (UGW keys backends by the Backend enum; GL covers its
// variants by compile macro):
//   Backend::OpenGL  → api_render_opengl.cpp  (desktop GL; GLES via EGL and WebGL
//                      via Emscripten are compile-flavours of the same file)
//   Backend::Vulkan  → api_render_vulkan.cpp
//   Backend::D3D11   → api_render_d3d11.cpp
//   Backend::D3D12   → api_render_d3d12.cpp   (D3D12 Ultimate = WINDOW_SUPPORT_D3D12_ULTIMATE)
//   Backend::Metal   → api_render_metal.mm

#include "../graphics_api.hpp"

namespace window {

#define WINDOW_RENDER_BACKEND_DECL(suffix)                                                              \
    GraphicDevice*    create_device_##suffix(Graphics* context, Result* out_result);                    \
    GraphicCommander* create_commander_##suffix(Graphics* context, GraphicDevice* device,               \
                                                QueueType queue, Result* out_result);                   \
    void              submit_commander_##suffix(Graphics* context, GraphicCommander* commander,         \
                                                FenceHandle fence, TimelineSemaphoreHandle timeline,    \
                                                uint64_t timeline_value);

WINDOW_RENDER_BACKEND_DECL(gl)
WINDOW_RENDER_BACKEND_DECL(vulkan)
WINDOW_RENDER_BACKEND_DECL(d3d11)
WINDOW_RENDER_BACKEND_DECL(d3d12)
WINDOW_RENDER_BACKEND_DECL(metal)

#undef WINDOW_RENDER_BACKEND_DECL

} // namespace window
