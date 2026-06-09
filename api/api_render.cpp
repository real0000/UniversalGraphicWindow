// api_render.cpp — public factory for the render abstraction (graphics_api.hpp).
// Dispatches create_device / create_commander / submit_commander on the context's
// Backend to the matching api_render_<backend> translation unit. Always compiled;
// a backend that is disabled returns ErrorNotSupported from its creator.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

namespace window {

#if !defined(__APPLE__)
// The Metal TU (api_render_metal.mm) is only compiled on Apple platforms; define
// its creators as not-supported stubs elsewhere so the dispatcher always links.
GraphicDevice*    create_device_metal(Graphics*, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
GraphicCommander* create_commander_metal(Graphics*, GraphicDevice*, QueueType, Result* r) { if (r) *r = Result::ErrorNotSupported; return nullptr; }
void              submit_commander_metal(Graphics*, GraphicCommander*, FenceHandle, TimelineSemaphoreHandle, uint64_t) {}
#endif

GraphicDevice* create_device(Graphics* context, Result* out_result) {
    if (!context) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    switch (context->get_backend()) {
        case Backend::OpenGL: return create_device_gl(context, out_result);     // + GLES / WebGL flavours
        case Backend::Vulkan: return create_device_vulkan(context, out_result);
        case Backend::D3D11:  return create_device_d3d11(context, out_result);
        case Backend::D3D12:  return create_device_d3d12(context, out_result);  // + D3D12 Ultimate
        case Backend::Metal:  return create_device_metal(context, out_result);
        default: break;
    }
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void destroy_device(GraphicDevice* device) { delete device; }   // virtual dtor; backend-agnostic

GraphicCommander* create_commander(Graphics* context, GraphicDevice* device, Result* out_result) {
    return create_commander(context, device, QueueType::Graphics, out_result);
}
GraphicCommander* create_commander(Graphics* context, GraphicDevice* device, QueueType queue, Result* out_result) {
    if (!context || !device) { if (out_result) *out_result = Result::ErrorInvalidParameter; return nullptr; }
    switch (context->get_backend()) {
        case Backend::OpenGL: return create_commander_gl(context, device, queue, out_result);
        case Backend::Vulkan: return create_commander_vulkan(context, device, queue, out_result);
        case Backend::D3D11:  return create_commander_d3d11(context, device, queue, out_result);
        case Backend::D3D12:  return create_commander_d3d12(context, device, queue, out_result);
        case Backend::Metal:  return create_commander_metal(context, device, queue, out_result);
        default: break;
    }
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void destroy_commander(GraphicCommander* commander) { delete commander; }

namespace {
void dispatch_submit(Graphics* context, GraphicCommander* commander,
                     FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    if (!context || !commander) return;
    switch (context->get_backend()) {
        case Backend::OpenGL: submit_commander_gl(context, commander, fence, timeline, value); return;
        case Backend::Vulkan: submit_commander_vulkan(context, commander, fence, timeline, value); return;
        case Backend::D3D11:  submit_commander_d3d11(context, commander, fence, timeline, value); return;
        case Backend::D3D12:  submit_commander_d3d12(context, commander, fence, timeline, value); return;
        case Backend::Metal:  submit_commander_metal(context, commander, fence, timeline, value); return;
        default: return;
    }
}
} // namespace

void submit_commander(Graphics* context, GraphicCommander* commander) {
    dispatch_submit(context, commander, FenceHandle{}, TimelineSemaphoreHandle{}, 0);
}
void submit_commander(Graphics* context, GraphicCommander* commander, FenceHandle signal_fence) {
    dispatch_submit(context, commander, signal_fence, TimelineSemaphoreHandle{}, 0);
}
void submit_commander(Graphics* context, GraphicCommander* commander, TimelineSemaphoreHandle signal, uint64_t value) {
    dispatch_submit(context, commander, FenceHandle{}, signal, value);
}

} // namespace window
