// api_render_d3d11.cpp — Direct3D 11 implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Windows only.
//
// Status: scaffold. Creators are wired into the dispatcher; the real device is
// built on UGW's GraphicsD3D11 (ID3D11Device / ID3D11DeviceContext / swapchain).
// Reports ErrorNotSupported until the implementation lands.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

namespace window {

#if defined(WINDOW_SUPPORT_D3D11)
// TODO(phase7-backends): real D3D11 GraphicDevice/GraphicCommander.
//  - buffers/textures(+SRV/RTV/DSV/UAV)/samplers; HLSL (DXBC) shaders; input
//    layouts; rasterizer/blend/depth-stencil state objects; the immediate (or a
//    deferred) ID3D11DeviceContext as the commander; D3D11 has no descriptor sets
//    so emulate bind_descriptor_set as PSSetShaderResources/CBV/UAV calls; queries
//    (ID3D11Query); no native fence/timeline pre-11.4 → emulate via Flush + event.
#endif

GraphicDevice* create_device_d3d11(Graphics* context, Result* out_result) {
    (void)context;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
GraphicCommander* create_commander_d3d11(Graphics* context, GraphicDevice* device,
                                         QueueType queue, Result* out_result) {
    (void)context; (void)device; (void)queue;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void submit_commander_d3d11(Graphics* context, GraphicCommander* commander,
                            FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    (void)context; (void)commander; (void)fence; (void)timeline; (void)value;
}

} // namespace window
