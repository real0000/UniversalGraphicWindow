// api_render_metal.mm — Metal implementation of the GraphicDevice +
// GraphicCommander abstraction (graphics_api.hpp). Apple platforms only.
//
// Status: scaffold. Creators are wired into the dispatcher; the real device is
// built on UGW's GraphicsMetal (MTLDevice / MTLCommandQueue / CAMetalLayer).
// Reports ErrorNotSupported until the implementation lands.

#include "../graphics_api.hpp"
#include "api_render_internal.hpp"

namespace window {

#if defined(WINDOW_SUPPORT_METAL)
// TODO(phase7-backends): real Metal GraphicDevice/GraphicCommander.
//  - MTLBuffer/MTLTexture(+views)/MTLSamplerState; metallib (MSL) shaders;
//    MTLRenderPipelineState/MTLComputePipelineState + MTLBinaryArchive (PSO cache);
//    argument buffers backing DescriptorSet, MTLRenderCommandEncoder as commander;
//    MTLCommandQueue submit; MTLSharedEvent (timeline) + MTLFence; MTLHeap sparse;
//    MTLAccelerationStructure + intersection for ray tracing.
#endif

GraphicDevice* create_device_metal(Graphics* context, Result* out_result) {
    (void)context;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
GraphicCommander* create_commander_metal(Graphics* context, GraphicDevice* device,
                                         QueueType queue, Result* out_result) {
    (void)context; (void)device; (void)queue;
    if (out_result) *out_result = Result::ErrorNotSupported;
    return nullptr;
}
void submit_commander_metal(Graphics* context, GraphicCommander* commander,
                            FenceHandle fence, TimelineSemaphoreHandle timeline, uint64_t value) {
    (void)context; (void)commander; (void)fence; (void)timeline; (void)value;
}

} // namespace window
