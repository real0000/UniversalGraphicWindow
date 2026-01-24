/*
 * window_ios.mm - iOS (UIKit) implementation
 * Backends: OpenGL ES, Metal
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_IOS)

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <string>

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_METAL
#import <Metal/Metal.h>
#define WINDOW_HAS_METAL 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_uiview(void* ui_view, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_METAL
Graphics* create_metal_graphics_uiview(void* ui_view, int width, int height, const Config& config);
#endif

//=============================================================================
// Forward declarations
//=============================================================================

struct Window::Impl;

} // namespace window

//=============================================================================
// Objective-C Classes
//=============================================================================

@interface WindowView : UIView
@property (nonatomic, assign) window::Window::Impl* impl;
- (instancetype)initWithFrame:(CGRect)frame impl:(window::Window::Impl*)impl;
@end

@interface WindowViewController : UIViewController
@property (nonatomic, assign) window::Window::Impl* impl;
@end

//=============================================================================
// Implementation Structure
//=============================================================================

namespace window {

struct Window::Impl {
    UIWindow* ui_window = nil;
    WindowViewController* view_controller = nil;
    WindowView* view = nil;
    bool should_close_flag = false;
    bool visible = true;
    int width = 0;
    int height = 0;
    std::string title;
    Graphics* gfx = nullptr;
};

} // namespace window

//=============================================================================
// WindowView Implementation
//=============================================================================

@implementation WindowView

+ (Class)layerClass {
#ifdef WINDOW_HAS_METAL
    return [CAMetalLayer class];
#elif defined(WINDOW_HAS_OPENGL)
    return [CAEAGLLayer class];
#else
    return [CALayer class];
#endif
}

- (instancetype)initWithFrame:(CGRect)frame impl:(window::Window::Impl*)impl {
    self = [super initWithFrame:frame];
    if (self) {
        _impl = impl;
        self.contentScaleFactor = [UIScreen mainScreen].scale;
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];

    if (_impl) {
        CGRect bounds = self.bounds;
        CGFloat scale = self.contentScaleFactor;
        _impl->width = static_cast<int>(bounds.size.width * scale);
        _impl->height = static_cast<int>(bounds.size.height * scale);
    }
}

@end

//=============================================================================
// WindowViewController Implementation
//=============================================================================

@implementation WindowViewController

- (void)loadView {
    WindowView* view = [[WindowView alloc] initWithFrame:[[UIScreen mainScreen] bounds] impl:_impl];
    self.view = view;
    _impl->view = view;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor blackColor];
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskAll;
}

@end

//=============================================================================
// Window Implementation
//=============================================================================

namespace window {

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    @autoreleasepool {
        Window* window = new Window();
        window->impl = new Window::Impl();
        window->impl->width = config.width;
        window->impl->height = config.height;
        window->impl->title = config.title;

        CGRect frame = [[UIScreen mainScreen] bounds];
        window->impl->ui_window = [[UIWindow alloc] initWithFrame:frame];

        WindowViewController* vc = [[WindowViewController alloc] init];
        vc.impl = window->impl;
        window->impl->view_controller = vc;

        window->impl->ui_window.rootViewController = vc;

        // Update actual size
        CGFloat scale = [[UIScreen mainScreen] scale];
        window->impl->width = static_cast<int>(frame.size.width * scale);
        window->impl->height = static_cast<int>(frame.size.height * scale);

        // Create graphics backend based on config.backend
        Graphics* gfx = nullptr;
        Backend requested = config.backend;
        if (requested == Backend::Auto) {
            requested = get_default_backend();
        }

        switch (requested) {
#ifdef WINDOW_HAS_METAL
            case Backend::Metal:
                gfx = create_metal_graphics_uiview((__bridge void*)window->impl->view, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_uiview((__bridge void*)window->impl->view, window->impl->width, window->impl->height, config);
                break;
#endif
            default:
                break;
        }

        // Fallback to default if requested backend failed or not supported
        if (!gfx && config.backend != Backend::Auto) {
            Backend fallback = get_default_backend();
            switch (fallback) {
#ifdef WINDOW_HAS_METAL
                case Backend::Metal:
                    gfx = create_metal_graphics_uiview((__bridge void*)window->impl->view, window->impl->width, window->impl->height, config);
                    break;
#endif
#ifdef WINDOW_HAS_OPENGL
                case Backend::OpenGL:
                    gfx = create_opengl_graphics_uiview((__bridge void*)window->impl->view, window->impl->width, window->impl->height, config);
                    break;
#endif
                default:
                    break;
            }
        }

        if (!gfx) {
            delete window->impl;
            delete window;
            set_result(Result::ErrorGraphicsInit);
            return nullptr;
        }

        window->impl->gfx = gfx;

        if (config.visible) {
            [window->impl->ui_window makeKeyAndVisible];
        }

        set_result(Result::Success);
        return window;
    }
}

void Window::destroy() {
    @autoreleasepool {
        if (impl) {
            delete impl->gfx;
            impl->ui_window.hidden = YES;
            impl->ui_window = nil;
            impl->view_controller = nil;
            impl->view = nil;

            delete impl;
            impl = nullptr;
        }
    }
    delete this;
}

void Window::show() {
    if (impl && impl->ui_window) {
        [impl->ui_window makeKeyAndVisible];
        impl->visible = true;
    }
}

void Window::hide() {
    if (impl && impl->ui_window) {
        impl->ui_window.hidden = YES;
        impl->visible = false;
    }
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl) {
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    (void)width;
    (void)height;
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (width) *width = impl->width;
        if (height) *height = impl->height;
    }
}

int Window::get_width() const {
    return impl ? impl->width : 0;
}

int Window::get_height() const {
    return impl ? impl->height : 0;
}

bool Window::set_position(int x, int y) {
    (void)x;
    (void)y;
    return false;
}

bool Window::get_position(int* x, int* y) const {
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const {
    return false;
}

bool Window::should_close() const {
    return impl ? impl->should_close_flag : true;
}

void Window::set_should_close(bool close) {
    if (impl) impl->should_close_flag = close;
}

void Window::poll_events() {
    @autoreleasepool {
        while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, YES) == kCFRunLoopRunHandledSource) {
        }
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? (__bridge void*)impl->ui_window : nullptr;
}

void* Window::native_display() const {
    return nullptr;
}

//=============================================================================
// Utility Functions
//=============================================================================

const char* result_to_string(Result result) {
    switch (result) {
        case Result::Success: return "Success";
        case Result::ErrorUnknown: return "Unknown error";
        case Result::ErrorPlatformInit: return "Platform initialization failed";
        case Result::ErrorWindowCreation: return "Window creation failed";
        case Result::ErrorGraphicsInit: return "Graphics initialization failed";
        case Result::ErrorNotSupported: return "Not supported";
        case Result::ErrorInvalidParameter: return "Invalid parameter";
        case Result::ErrorOutOfMemory: return "Out of memory";
        case Result::ErrorDeviceLost: return "Device lost";
        default: return "Unknown";
    }
}

const char* backend_to_string(Backend backend) {
    switch (backend) {
        case Backend::Auto: return "Auto";
        case Backend::OpenGL: return "OpenGL ES";
        case Backend::Vulkan: return "Vulkan";
        case Backend::D3D11: return "Direct3D 11";
        case Backend::D3D12: return "Direct3D 12";
        case Backend::Metal: return "Metal";
        default: return "Unknown";
    }
}

bool is_backend_supported(Backend backend) {
    switch (backend) {
        case Backend::Auto: return true;
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL: return true;
#endif
#ifdef WINDOW_HAS_METAL
        case Backend::Metal: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_METAL
    return Backend::Metal;
#elif defined(WINDOW_HAS_OPENGL)
    return Backend::OpenGL;
#else
    return Backend::Auto;
#endif
}

//=============================================================================
// Graphics Context for External Windows
//=============================================================================

Graphics* Graphics::create(const ExternalWindowConfig& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    if (!config.native_handle) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    if (config.width <= 0 || config.height <= 0) {
        set_result(Result::ErrorInvalidParameter);
        return nullptr;
    }

    // Convert ExternalWindowConfig to Config for backend creation
    Config internal_config;
    internal_config.width = config.width;
    internal_config.height = config.height;
    internal_config.vsync = config.vsync;
    internal_config.samples = config.samples;
    internal_config.red_bits = config.red_bits;
    internal_config.green_bits = config.green_bits;
    internal_config.blue_bits = config.blue_bits;
    internal_config.alpha_bits = config.alpha_bits;
    internal_config.depth_bits = config.depth_bits;
    internal_config.stencil_bits = config.stencil_bits;
    internal_config.back_buffers = config.back_buffers;
    internal_config.backend = config.backend;
    internal_config.shared_graphics = config.shared_graphics;

    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    Graphics* gfx = nullptr;
    UIView* ui_view = (__bridge UIView*)config.native_handle;

    switch (requested) {
#ifdef WINDOW_HAS_METAL
        case Backend::Metal:
            gfx = create_metal_graphics_uiview((__bridge void*)ui_view, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_uiview((__bridge void*)ui_view, config.width, config.height, internal_config);
            break;
#endif
        default:
            break;
    }

    if (!gfx) {
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    set_result(Result::Success);
    return gfx;
}

void Graphics::destroy() {
    delete this;
}

} // namespace window

#endif // WINDOW_PLATFORM_IOS
