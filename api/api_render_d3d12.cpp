// api_render_d3d12.cpp — Direct3D 12 implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Windows only.
//
// This file also covers "D3D12 Ultimate" (mesh shaders, DXR ray tracing, sampler
// feedback, VRS) under WINDOW_SUPPORT_D3D12_ULTIMATE — it is a feature level of
// D3D12, not a separate backend, so it shares this device.
//
// Status: scaffold. Creators are wired into the dispatcher; the real device is
// built on UGW's GraphicsD3D12 (ID3D12Device / command queue / swapchain).
// Reports ErrorNotSupported until the implementation lands.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

namespace window {

#if defined(WINDOW_SUPPORT_D3D12)
// TODO(phase7-backends): real D3D12 GraphicDevice/GraphicCommander.
//  - committed/placed resources + heaps; descriptor heaps (CBV/SRV/UAV + sampler)
//    backing DescriptorSet; ID3D12RootSignature from PipelineLayoutDesc; PSOs from
//    DXIL (+ PSO cache via ID3D12PipelineLibrary); ID3D12GraphicsCommandList as the
//    commander; ID3D12CommandQueue submit; ID3D12Fence (native timeline); copy/
//    resolve; reserved (tiled) resources for sparse; queries (ID3D12QueryHeap).
//  #if defined(WINDOW_SUPPORT_D3D12_ULTIMATE):
//    - DispatchMesh for mesh/amplification; DXR acceleration structures + state
//      objects + DispatchRays for trace_rays().
#endif

GraphicDevice* create_device_d3d12(Graphics* context, Result* out_result) {
    (void)context;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
GraphicCommander* create_commander_d3d12(Graphics* context, GraphicDevice* device,
                                         QueueType queue, Result* out_result) {
    (void)context; (void)device; (void)queue;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void submit_commander_d3d12(Graphics* context, GraphicCommander* commander,
                            FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    (void)context; (void)commander; (void)fence; (void)timeline; (void)value;
}

} // namespace window
