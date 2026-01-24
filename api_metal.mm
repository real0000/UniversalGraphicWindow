/*
 * api_metal.mm - Metal graphics implementation (macOS/iOS)
 */

#include "window.hpp"

#if (defined(__APPLE__) && !defined(WINDOW_NO_METAL))

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#endif

#include <string>

namespace window {

//=============================================================================
// Metal Format Conversion
//=============================================================================

static MTLPixelFormat get_metal_format(int r, int g, int b, int a) {
    if (r == 8 && g == 8 && b == 8 && a == 8) return MTLPixelFormatBGRA8Unorm;
    if (r == 10 && g == 10 && b == 10 && a == 2) return MTLPixelFormatBGR10A2Unorm;
    if (r == 16 && g == 16 && b == 16 && a == 16) return MTLPixelFormatRGBA16Float;
    return MTLPixelFormatBGRA8Unorm;
}

//=============================================================================
// Metal Graphics Implementation
//=============================================================================

class GraphicsMetal : public Graphics {
public:
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> command_queue = nil;
    CAMetalLayer* layer = nil;
    std::string device_name;
    bool owns_device = true;
    bool vsync = true;

    ~GraphicsMetal() override {
        layer = nil;
        command_queue = nil;
        if (owns_device) device = nil;
    }

    Backend get_backend() const override { return Backend::Metal; }
    const char* get_backend_name() const override { return "Metal"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        if (layer) {
            layer.drawableSize = CGSizeMake(width, height);
        }
        return true;
    }

    void present() override {
        // Metal presentation is done via command buffer presentDrawable:
        // This is a no-op as user should use native Metal APIs
    }

    void make_current() override {
        // Metal doesn't have a "make current" concept like OpenGL
    }

    void* native_device() const override { return (__bridge void*)device; }
    void* native_context() const override { return (__bridge void*)command_queue; }
    void* native_swapchain() const override { return (__bridge void*)layer; }
};

//=============================================================================
// Creation for NSView (macOS)
//=============================================================================

#if TARGET_OS_OSX
Graphics* create_metal_graphics_nsview(void* ns_view, int width, int height, const Config& config) {
    @autoreleasepool {
        NSView* view = (__bridge NSView*)ns_view;
        if (!view) return nullptr;

        id<MTLDevice> device = nil;
        bool owns_device = true;

        // Check for shared device
        if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Metal) {
            device = (__bridge id<MTLDevice>)config.shared_graphics->native_device();
            owns_device = false;
        } else {
            device = MTLCreateSystemDefaultDevice();
            if (!device) return nullptr;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return nullptr;

        // Make the view layer-backed
        [view setWantsLayer:YES];

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = get_metal_format(config.red_bits, config.green_bits, config.blue_bits, config.alpha_bits);
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(width, height);

        [view setLayer:layer];

        GraphicsMetal* gfx = new GraphicsMetal();
        gfx->device = device;
        gfx->command_queue = queue;
        gfx->layer = layer;
        gfx->device_name = [[device name] UTF8String];
        gfx->owns_device = owns_device;

        return gfx;
    }
}
#endif

//=============================================================================
// Creation for UIView (iOS)
//=============================================================================

#if TARGET_OS_IOS || TARGET_OS_TV
Graphics* create_metal_graphics_uiview(void* ui_view, int width, int height, const Config& config) {
    @autoreleasepool {
        UIView* view = (__bridge UIView*)ui_view;
        if (!view) return nullptr;

        id<MTLDevice> device = nil;
        bool owns_device = true;

        // Check for shared device
        if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Metal) {
            device = (__bridge id<MTLDevice>)config.shared_graphics->native_device();
            owns_device = false;
        } else {
            device = MTLCreateSystemDefaultDevice();
            if (!device) return nullptr;
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return nullptr;

        // Get or create metal layer
        CAMetalLayer* layer = nil;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
            layer = (CAMetalLayer*)view.layer;
        } else {
            layer = [CAMetalLayer layer];
            layer.frame = view.layer.bounds;
            [view.layer addSublayer:layer];
        }

        layer.device = device;
        layer.pixelFormat = get_metal_format(config.red_bits, config.green_bits, config.blue_bits, config.alpha_bits);
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(width, height);
        layer.contentsScale = [UIScreen mainScreen].scale;

        GraphicsMetal* gfx = new GraphicsMetal();
        gfx->device = device;
        gfx->command_queue = queue;
        gfx->layer = layer;
        gfx->device_name = [[device name] UTF8String];
        gfx->owns_device = owns_device;

        return gfx;
    }
}
#endif

//=============================================================================
// Creation for CAMetalLayer (generic)
//=============================================================================

Graphics* create_metal_graphics_layer(void* metal_layer, const Config& config) {
    @autoreleasepool {
        CAMetalLayer* layer = (__bridge CAMetalLayer*)metal_layer;
        if (!layer) return nullptr;

        id<MTLDevice> device = nil;
        bool owns_device = true;

        // Check for shared device first
        if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::Metal) {
            device = (__bridge id<MTLDevice>)config.shared_graphics->native_device();
            owns_device = false;
            layer.device = device;
        } else {
            device = layer.device;
            if (!device) {
                device = MTLCreateSystemDefaultDevice();
                if (!device) return nullptr;
                layer.device = device;
            }
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return nullptr;

        layer.pixelFormat = get_metal_format(config.red_bits, config.green_bits, config.blue_bits, config.alpha_bits);
        layer.framebufferOnly = YES;

        GraphicsMetal* gfx = new GraphicsMetal();
        gfx->device = device;
        gfx->command_queue = queue;
        gfx->layer = layer;
        gfx->device_name = [[device name] UTF8String];
        gfx->owns_device = owns_device;

        return gfx;
    }
}

} // namespace window

#endif // __APPLE__ && !WINDOW_NO_METAL
