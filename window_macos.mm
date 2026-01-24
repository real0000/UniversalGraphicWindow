/*
 * window_macos.mm - macOS (Cocoa) implementation
 * Backends: OpenGL, Vulkan (MoltenVK), Metal
 */

#include "window.hpp"

#if defined(WINDOW_PLATFORM_MACOS)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <string>

//=============================================================================
// Backend Configuration
//=============================================================================

#if !defined(WINDOW_NO_OPENGL)
#define WINDOW_HAS_OPENGL 1
#endif

#if !defined(WINDOW_NO_METAL)
#import <Metal/Metal.h>
#define WINDOW_HAS_METAL 1
#endif

#if !defined(WINDOW_NO_VULKAN)
#define VK_USE_PLATFORM_MACOS_MVK
#include <vulkan/vulkan.h>
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_nsview(void* ns_view, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_METAL
Graphics* create_metal_graphics_nsview(void* ns_view, int width, int height, const Config& config);
#endif

//=============================================================================
// Forward declarations
//=============================================================================

struct Window::Impl;

} // namespace window

//=============================================================================
// Objective-C Classes
//=============================================================================

@interface WindowView : NSView
@property (nonatomic, assign) window::Window::Impl* impl;
@end

@interface WindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) window::Window::Impl* impl;
@end

//=============================================================================
// Implementation Structure
//=============================================================================

namespace window {

struct Window::Impl {
    NSWindow* ns_window = nil;
    WindowView* view = nil;
    WindowDelegate* delegate = nil;
    bool should_close_flag = false;
    bool visible = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::string title;
    Graphics* gfx = nullptr;
};

} // namespace window

//=============================================================================
// WindowView Implementation
//=============================================================================

@implementation WindowView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isOpaque {
    return YES;
}

- (BOOL)wantsUpdateLayer {
    return YES;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];

    if (_impl) {
        NSRect bounds = [self bounds];
        CGFloat scale = [[self window] backingScaleFactor];
        _impl->width = static_cast<int>(bounds.size.width * scale);
        _impl->height = static_cast<int>(bounds.size.height * scale);
    }
}

@end

//=============================================================================
// WindowDelegate Implementation
//=============================================================================

@implementation WindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (_impl) {
        _impl->should_close_flag = true;
    }
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    if (_impl) {
        NSWindow* window = notification.object;
        NSRect frame = [[window contentView] frame];
        CGFloat scale = [window backingScaleFactor];
        _impl->width = static_cast<int>(frame.size.width * scale);
        _impl->height = static_cast<int>(frame.size.height * scale);
    }
}

- (void)windowDidMove:(NSNotification*)notification {
    if (_impl) {
        NSWindow* window = notification.object;
        NSRect frame = [window frame];
        _impl->x = static_cast<int>(frame.origin.x);
        _impl->y = static_cast<int>(frame.origin.y);
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    if (_impl) {
        _impl->visible = false;
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    if (_impl) {
        _impl->visible = true;
    }
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
        // Ensure NSApplication is initialized
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        // Create menu bar if not exists
        if ([NSApp mainMenu] == nil) {
            NSMenu* menubar = [[NSMenu alloc] init];
            NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
            [menubar addItem:appMenuItem];
            [NSApp setMainMenu:menubar];

            NSMenu* appMenu = [[NSMenu alloc] init];
            NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:@"Quit"
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
            [appMenu addItem:quitItem];
            [appMenuItem setSubmenu:appMenu];
        }

        Window* window = new Window();
        window->impl = new Window::Impl();
        window->impl->width = config.width;
        window->impl->height = config.height;
        window->impl->title = config.title;

        NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                   NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskMiniaturizable;
        if (config.resizable) {
            style |= NSWindowStyleMaskResizable;
        }

        NSRect frame = NSMakeRect(0, 0, config.width, config.height);
        NSWindow* nsWindow = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];

        [nsWindow setTitle:[NSString stringWithUTF8String:config.title]];

        // Create content view
        WindowView* view = [[WindowView alloc] initWithFrame:frame];
        view.impl = window->impl;
        [nsWindow setContentView:view];
        window->impl->view = view;

        // Create delegate
        WindowDelegate* delegate = [[WindowDelegate alloc] init];
        delegate.impl = window->impl;
        [nsWindow setDelegate:delegate];
        window->impl->delegate = delegate;

        window->impl->ns_window = nsWindow;

        // Set position
        if (config.x >= 0 && config.y >= 0) {
            [nsWindow setFrameOrigin:NSMakePoint(config.x, config.y)];
            window->impl->x = config.x;
            window->impl->y = config.y;
        } else {
            [nsWindow center];
            NSRect windowFrame = [nsWindow frame];
            window->impl->x = static_cast<int>(windowFrame.origin.x);
            window->impl->y = static_cast<int>(windowFrame.origin.y);
        }

        // Update actual size with retina scale
        CGFloat scale = [nsWindow backingScaleFactor];
        window->impl->width = static_cast<int>(config.width * scale);
        window->impl->height = static_cast<int>(config.height * scale);

        // Create graphics backend based on config.backend
        Graphics* gfx = nullptr;
        Backend requested = config.backend;
        if (requested == Backend::Auto) {
            requested = get_default_backend();
        }

        switch (requested) {
#ifdef WINDOW_HAS_METAL
            case Backend::Metal:
                gfx = create_metal_graphics_nsview((__bridge void*)view, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_nsview((__bridge void*)view, window->impl->width, window->impl->height, config);
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
                    gfx = create_metal_graphics_nsview((__bridge void*)view, window->impl->width, window->impl->height, config);
                    break;
#endif
#ifdef WINDOW_HAS_OPENGL
                case Backend::OpenGL:
                    gfx = create_opengl_graphics_nsview((__bridge void*)view, window->impl->width, window->impl->height, config);
                    break;
#endif
                default:
                    break;
            }
        }

        if (!gfx) {
            [nsWindow close];
            delete window->impl;
            delete window;
            set_result(Result::ErrorGraphicsInit);
            return nullptr;
        }

        window->impl->gfx = gfx;

        if (config.visible) {
            [nsWindow makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            window->impl->visible = true;
        }

        set_result(Result::Success);
        return window;
    }
}

void Window::destroy() {
    @autoreleasepool {
        if (impl) {
            delete impl->gfx;
            if (impl->ns_window) {
                [impl->ns_window close];
                impl->ns_window = nil;
            }
            impl->delegate = nil;
            impl->view = nil;

            delete impl;
            impl = nullptr;
        }
    }
    delete this;
}

void Window::show() {
    if (impl && impl->ns_window) {
        [impl->ns_window makeKeyAndOrderFront:nil];
        impl->visible = true;
    }
}

void Window::hide() {
    if (impl && impl->ns_window) {
        [impl->ns_window orderOut:nil];
        impl->visible = false;
    }
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl && impl->ns_window) {
        [impl->ns_window setTitle:[NSString stringWithUTF8String:title]];
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl && impl->ns_window) {
        NSRect frame = [impl->ns_window frame];
        NSRect contentRect = [impl->ns_window contentRectForFrameRect:frame];
        contentRect.size.width = width;
        contentRect.size.height = height;
        frame = [impl->ns_window frameRectForContentRect:contentRect];
        [impl->ns_window setFrame:frame display:YES animate:NO];
    }
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
    if (impl && impl->ns_window) {
        [impl->ns_window setFrameOrigin:NSMakePoint(x, y)];
        impl->x = x;
        impl->y = y;
        return true;
    }
    return false;
}

bool Window::get_position(int* x, int* y) const {
    if (impl) {
        if (x) *x = impl->x;
        if (y) *y = impl->y;
        return true;
    }
    return false;
}

bool Window::supports_position() const {
    return true;
}

bool Window::should_close() const {
    return impl ? impl->should_close_flag : true;
}

void Window::set_should_close(bool close) {
    if (impl) impl->should_close_flag = close;
}

void Window::poll_events() {
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? (__bridge void*)impl->ns_window : nullptr;
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
        case Backend::OpenGL: return "OpenGL";
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
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan: return true;
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

} // namespace window

#endif // WINDOW_PLATFORM_MACOS
