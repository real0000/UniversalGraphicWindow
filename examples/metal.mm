/*
 * metal.mm - Metal window example (macOS only)
 */

#include "window.hpp"

#ifdef WINDOW_PLATFORM_MACOS

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cstdio>
#include <cmath>

int main() {
    @autoreleasepool {
        window::Config config;
        config.title = "Metal Example";
        config.width = 800;
        config.height = 600;
        config.graphics_api = window::GraphicsAPI::Metal;

        window::Result result;
        window::Window* win = window::Window::create(config, &result);

        if (result != window::Result::Success) {
            printf("Failed to create window: %s\n", window::result_to_string(result));
            return 1;
        }

        const window::GraphicsContext* ctx = win->get_graphics_context();
        id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->metal.device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)ctx->metal.queue;
        CAMetalLayer* layer = (__bridge CAMetalLayer*)ctx->metal.layer;

        printf("Metal context created!\n");
        printf("Device: %s\n", [[device name] UTF8String]);

        float time = 0.0f;

        while (!win->should_close()) {
            @autoreleasepool {
                win->poll_events();

                // Get next drawable
                id<CAMetalDrawable> drawable = [layer nextDrawable];
                if (!drawable) {
                    continue;
                }

                // Create render pass descriptor
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = drawable.texture;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;

                // Animate clear color
                float r = (sinf(time) + 1.0f) * 0.5f;
                float g = (sinf(time + 2.0f) + 1.0f) * 0.5f;
                float b = (sinf(time + 4.0f) + 1.0f) * 0.5f;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(r * 0.3, g * 0.3, b * 0.3, 1.0);

                // Create command buffer and encoder
                id<MTLCommandBuffer> cmd_buffer = [queue commandBuffer];
                id<MTLRenderCommandEncoder> encoder = [cmd_buffer renderCommandEncoderWithDescriptor:pass];
                [encoder endEncoding];

                // Present and commit
                [cmd_buffer presentDrawable:drawable];
                [cmd_buffer commit];

                time += 0.016f;
            }
        }

        win->destroy();
    }
    return 0;
}

#else

#include <cstdio>

int main() {
    printf("Metal example is only available on macOS.\n");
    return 0;
}

#endif
