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

static MTLPixelFormat get_metal_format_from_color_bits(int color_bits) {
    // color_bits: 16, 24, 32, or 64 (HDR)
    if (color_bits >= 64) return MTLPixelFormatRGBA16Float;  // 64-bit HDR
    if (color_bits >= 32) return MTLPixelFormatBGRA8Unorm;   // 32-bit standard
    return MTLPixelFormatBGRA8Unorm;  // Default
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

    void get_capabilities(GraphicsCapabilities* out_caps) const override {
        if (!out_caps || !device) return;
        GraphicsCapabilities& c = *out_caps;

        // API version: Metal version maps to OS; approximate using GPU family
        c.api_version_major = 3;  // Metal 3 on Apple Silicon / macOS 13
        c.api_version_minor = 0;
        c.shader_model      = 6.0f;  // MSL is roughly SM 6.x capable

        // Texture limits
        // Apple Silicon GPUs support up to 16384; older Intel/AMD GPUs 8192
#if TARGET_OS_OSX
        if ([device supportsFamily:MTLGPUFamilyApple7]) {
            c.max_texture_size = 16384;
        } else {
            c.max_texture_size = 8192;
        }
#else
        // iOS: A14+ supports 16384, older 8192
        c.max_texture_size = [device supportsFamily:MTLGPUFamilyApple7] ? 16384 : 8192;
#endif
        c.max_texture_3d_size      = 2048;
        c.max_texture_cube_size    = c.max_texture_size;
        c.max_texture_array_layers = 2048;
        c.max_mip_levels           = 15;  // log2(16384) + 1
        c.max_framebuffer_width    = c.max_texture_size;
        c.max_framebuffer_height   = c.max_texture_size;

        // Framebuffer / MSAA
        c.max_color_attachments = 8;
        c.max_samples           = [device supportsTextureSampleCount:8] ? 8 :
                                  [device supportsTextureSampleCount:4] ? 4 : 1;

        // Sampling
        c.max_texture_bindings = 128;
        c.max_anisotropy       = 16;

        // Vertex / buffer limits
        c.max_vertex_attributes   = 31;
        c.max_vertex_buffers      = 31;
        c.max_uniform_buffer_size = 64 * 1024;         // 64 KB inline constant buffer
        c.max_uniform_bindings    = 31;
        c.max_storage_bindings    = 31;
        c.max_viewports           = 16;
        c.max_scissor_rects       = 16;

        // Compute
        c.max_compute_group_size_x = 1024;
        c.max_compute_group_size_y = 1024;
        c.max_compute_group_size_z = 64;
        c.max_compute_group_total  = 1024;
        c.max_compute_dispatch_x   = 0;  // No fixed limit in Metal
        c.max_compute_dispatch_y   = 0;
        c.max_compute_dispatch_z   = 0;

        // Pipeline features
        c.compute_shaders     = true;
        c.geometry_shaders    = false;  // Not supported in Metal
        c.tessellation        = true;   // Metal tessellation via compute + post-tess
        c.mesh_shaders        = [device supportsFamily:MTLGPUFamilyApple7] || [device supportsFamily:MTLGPUFamilyMac2];
        c.instancing          = true;
        c.indirect_draw       = true;
        c.multi_draw_indirect = false;  // No direct equivalent in Metal
        c.base_vertex_draw    = true;
        c.occlusion_query     = true;
        c.timestamp_query     = [device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary];
        c.depth_clamp         = true;
        c.fill_mode_wireframe = true;
        c.conservative_raster = false;  // Not in Metal

        // Texture features
        c.texture_arrays = c.texture_3d = c.cube_maps = c.cube_map_arrays = true;
        c.render_to_texture = c.read_write_textures   = true;
        c.floating_point_textures = c.integer_textures = true;
        c.srgb_framebuffer = c.srgb_textures = true;
        c.hdr_output       = [device supportsFamily:MTLGPUFamilyApple6] ||
                             [device supportsFamily:MTLGPUFamilyMac1];
        c.depth32f = c.stencil8 = true;

        // Compressed formats
        c.texture_compression_astc = [device supportsFamily:MTLGPUFamilyApple2] ||
                                     [device supportsFamily:MTLGPUFamilyMacCatalyst1];
#if TARGET_OS_OSX
        c.texture_compression_bc   = true;  // BC supported on all macOS GPUs
        c.texture_compression_etc2 = false; // ETC not natively in Metal on macOS
#else
        c.texture_compression_bc   = false;
        c.texture_compression_etc2 = true;  // ETC2 on iOS via GPU family
#endif

        // Blend
        c.independent_blend = true;
        c.dual_source_blend = false;  // Not in Metal
        c.logic_ops         = false;  // Not in Metal

        // Tearing / HDR
        c.tearing_support   = false;  // Metal manages VSync internally

        // VRAM: recommendedMaxWorkingSetSize gives a practical upper bound
        c.vram_dedicated_bytes = [device recommendedMaxWorkingSetSize];
    }
};

// Helper to resolve swap mode from config
static SwapMode resolve_swap_mode(const Config& config) {
    if (config.swap_mode != SwapMode::Auto) {
        return config.swap_mode;
    }
    return config.vsync ? SwapMode::Fifo : SwapMode::Immediate;
}

// Configure Metal layer vsync based on swap mode
static void configure_layer_vsync(CAMetalLayer* layer, SwapMode swap_mode) {
#if TARGET_OS_OSX
    // macOS 10.13+ supports displaySyncEnabled
    if (@available(macOS 10.13, *)) {
        // Immediate mode disables vsync, all other modes enable it
        layer.displaySyncEnabled = (swap_mode != SwapMode::Immediate);
    }
#elif TARGET_OS_IOS || TARGET_OS_TV
    // iOS 10.3+ supports displaySyncEnabled
    if (@available(iOS 10.3, tvOS 10.2, *)) {
        layer.displaySyncEnabled = (swap_mode != SwapMode::Immediate);
    }
#endif
}

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
        layer.pixelFormat = get_metal_format_from_color_bits(config.color_bits);
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(width, height);
        configure_layer_vsync(layer, resolve_swap_mode(config));

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
        layer.pixelFormat = get_metal_format_from_color_bits(config.color_bits);
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake(width, height);
        layer.contentsScale = [UIScreen mainScreen].scale;
        configure_layer_vsync(layer, resolve_swap_mode(config));

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

        layer.pixelFormat = get_metal_format_from_color_bits(config.color_bits);
        layer.framebufferOnly = YES;
        configure_layer_vsync(layer, resolve_swap_mode(config));

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
