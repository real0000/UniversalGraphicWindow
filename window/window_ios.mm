/*
 * window_ios.mm - iOS (UIKit) implementation
 * Backends: OpenGL ES, Metal
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_IOS)

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <string>
#include <mach/mach_time.h>

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
// Utility
//=============================================================================

static double get_event_timestamp() {
    static mach_timebase_info_data_t timebase = {};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t time = mach_absolute_time();
    return static_cast<double>(time * timebase.numer / timebase.denom) / 1e9;
}

//=============================================================================
// Event Callbacks Storage
//=============================================================================

struct EventCallbacks {
    WindowCloseCallback close_callback;
    WindowResizeCallback resize_callback;
    WindowMoveCallback move_callback;
    WindowFocusCallback focus_callback;
    WindowStateCallback state_callback;
    TouchCallback touch_callback;
    DpiChangeCallback dpi_change_callback;
    DropFileCallback drop_file_callback;
};

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
    Window* owner = nullptr;  // Back-pointer for event dispatch
    bool should_close_flag = false;
    bool visible = true;
    int width = 0;
    int height = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Fullscreen; // iOS is always fullscreen

    // Event callbacks
    EventCallbacks callbacks;

    // Touch state (for simulated mouse on single touch)
    float touch_x = 0;
    float touch_y = 0;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;
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
        self.multipleTouchEnabled = YES;
        self.userInteractionEnabled = YES;
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];

    if (_impl) {
        CGRect bounds = self.bounds;
        CGFloat scale = self.contentScaleFactor;
        int newWidth = static_cast<int>(bounds.size.width * scale);
        int newHeight = static_cast<int>(bounds.size.height * scale);

        if (newWidth != _impl->width || newHeight != _impl->height) {
            _impl->width = newWidth;
            _impl->height = newHeight;

            if (_impl->callbacks.resize_callback) {
                window::WindowResizeEvent resizeEvent;
                resizeEvent.type = window::EventType::WindowResize;
                resizeEvent.window = _impl->owner;
                resizeEvent.timestamp = window::get_event_timestamp();
                resizeEvent.width = _impl->width;
                resizeEvent.height = _impl->height;
                resizeEvent.minimized = false;
                _impl->callbacks.resize_callback(resizeEvent);
            }
        }
    }
}

// Touch events
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_impl) return;

    CGFloat scale = self.contentScaleFactor;

    for (UITouch* touch in touches) {
        CGPoint pos = [touch locationInView:self];

        if (_impl->callbacks.touch_callback) {
            window::TouchEvent touchEvent;
            touchEvent.type = window::EventType::TouchDown;
            touchEvent.window = _impl->owner;
            touchEvent.timestamp = window::get_event_timestamp();
            touchEvent.touch_id = static_cast<int>(reinterpret_cast<intptr_t>((__bridge void*)touch) & 0xFFFFFFFF);
            touchEvent.x = static_cast<float>(pos.x * scale);
            touchEvent.y = static_cast<float>(pos.y * scale);
            touchEvent.pressure = static_cast<float>(touch.force / touch.maximumPossibleForce);
            if (touchEvent.pressure == 0 || isnan(touchEvent.pressure)) touchEvent.pressure = 1.0f;
            _impl->callbacks.touch_callback(touchEvent);
        }

        // Store first touch position for simulated mouse
        _impl->touch_x = pos.x * scale;
        _impl->touch_y = pos.y * scale;
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_impl) return;

    CGFloat scale = self.contentScaleFactor;

    for (UITouch* touch in touches) {
        CGPoint pos = [touch locationInView:self];

        if (_impl->callbacks.touch_callback) {
            window::TouchEvent touchEvent;
            touchEvent.type = window::EventType::TouchMove;
            touchEvent.window = _impl->owner;
            touchEvent.timestamp = window::get_event_timestamp();
            touchEvent.touch_id = static_cast<int>(reinterpret_cast<intptr_t>((__bridge void*)touch) & 0xFFFFFFFF);
            touchEvent.x = static_cast<float>(pos.x * scale);
            touchEvent.y = static_cast<float>(pos.y * scale);
            touchEvent.pressure = static_cast<float>(touch.force / touch.maximumPossibleForce);
            if (touchEvent.pressure == 0 || isnan(touchEvent.pressure)) touchEvent.pressure = 1.0f;
            _impl->callbacks.touch_callback(touchEvent);
        }

        _impl->touch_x = pos.x * scale;
        _impl->touch_y = pos.y * scale;
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_impl) return;

    CGFloat scale = self.contentScaleFactor;

    for (UITouch* touch in touches) {
        CGPoint pos = [touch locationInView:self];

        if (_impl->callbacks.touch_callback) {
            window::TouchEvent touchEvent;
            touchEvent.type = window::EventType::TouchUp;
            touchEvent.window = _impl->owner;
            touchEvent.timestamp = window::get_event_timestamp();
            touchEvent.touch_id = static_cast<int>(reinterpret_cast<intptr_t>((__bridge void*)touch) & 0xFFFFFFFF);
            touchEvent.x = static_cast<float>(pos.x * scale);
            touchEvent.y = static_cast<float>(pos.y * scale);
            touchEvent.pressure = 0.0f;
            _impl->callbacks.touch_callback(touchEvent);
        }
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [self touchesEnded:touches withEvent:event];
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
        const WindowConfigEntry& win_cfg = config.windows[0];

        Window* window = new Window();
        window->impl = new Window::Impl();
        window->impl->owner = window;  // Set back-pointer for event dispatch
        window->impl->width = win_cfg.width;
        window->impl->height = win_cfg.height;
        window->impl->title = win_cfg.title;

        // Initialize mouse input system
        window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
        window->impl->mouse_device.set_window(window);

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

        if (win_cfg.visible) {
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

void Window::set_style(WindowStyle style) {
    // iOS windows are always fullscreen, style changes are not supported
    (void)style;
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Fullscreen;
}

void Window::set_fullscreen(bool fullscreen) {
    // iOS is always fullscreen
    (void)fullscreen;
}

bool Window::is_fullscreen() const {
    return true; // iOS is always fullscreen
}

void Window::set_always_on_top(bool always_on_top) {
    // Not applicable on iOS
    (void)always_on_top;
}

bool Window::is_always_on_top() const {
    return false; // Not applicable on iOS
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
// Event Callback Setters
//=============================================================================

void Window::set_close_callback(WindowCloseCallback callback) {
    if (impl) { impl->callbacks.close_callback = callback; }
}

void Window::set_resize_callback(WindowResizeCallback callback) {
    if (impl) { impl->callbacks.resize_callback = callback; }
}

void Window::set_move_callback(WindowMoveCallback callback) {
    if (impl) { impl->callbacks.move_callback = callback; }
}

void Window::set_focus_callback(WindowFocusCallback callback) {
    if (impl) { impl->callbacks.focus_callback = callback; }
}

void Window::set_state_callback(WindowStateCallback callback) {
    if (impl) { impl->callbacks.state_callback = callback; }
}

void Window::set_touch_callback(TouchCallback callback) {
    if (impl) { impl->callbacks.touch_callback = callback; }
}

void Window::set_dpi_change_callback(DpiChangeCallback callback) {
    if (impl) { impl->callbacks.dpi_change_callback = callback; }
}

void Window::set_drop_file_callback(DropFileCallback callback) {
    if (impl) { impl->callbacks.drop_file_callback = callback; }
}

//=============================================================================
// Input State Queries
//=============================================================================

bool Window::is_key_down(Key key) const {
    if (!impl || key == Key::Unknown) return false;
    return impl->keyboard_device.is_key_down(key);
}

bool Window::is_mouse_button_down(MouseButton button) const {
    if (!impl) return false;
    return impl->mouse_device.is_button_down(button);
}

void Window::get_mouse_position(int* x, int* y) const {
    if (impl) {
        // Return last touch position as simulated mouse, or mouse_device position
        impl->mouse_device.get_position(x, y);
        // Fallback to touch position if mouse position is 0
        int mx = 0, my = 0;
        impl->mouse_device.get_position(&mx, &my);
        if (mx == 0 && my == 0) {
            if (x) *x = static_cast<int>(impl->touch_x);
            if (y) *y = static_cast<int>(impl->touch_y);
        }
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

KeyMod Window::get_current_modifiers() const {
    return KeyMod::None;
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

// key_to_string, mouse_button_to_string, event_type_to_string
// are implemented in input/input_keyboard.cpp

//=============================================================================
// Mouse Handler API
//=============================================================================

bool Window::add_mouse_handler(input::IMouseHandler* handler) {
    if (!impl) return false;
    return impl->mouse_dispatcher.add_handler(handler);
}

bool Window::remove_mouse_handler(input::IMouseHandler* handler) {
    if (!impl) return false;
    return impl->mouse_dispatcher.remove_handler(handler);
}

bool Window::remove_mouse_handler(const char* handler_id) {
    if (!impl) return false;
    return impl->mouse_dispatcher.remove_handler(handler_id);
}

input::MouseEventDispatcher* Window::get_mouse_dispatcher() {
    return impl ? &impl->mouse_dispatcher : nullptr;
}

//=============================================================================
// Keyboard Handler API
//=============================================================================

bool Window::add_keyboard_handler(input::IKeyboardHandler* handler) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.add_handler(handler);
}

bool Window::remove_keyboard_handler(input::IKeyboardHandler* handler) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.remove_handler(handler);
}

bool Window::remove_keyboard_handler(const char* handler_id) {
    if (!impl) return false;
    return impl->keyboard_dispatcher.remove_handler(handler_id);
}

input::KeyboardEventDispatcher* Window::get_keyboard_dispatcher() {
    return impl ? &impl->keyboard_dispatcher : nullptr;
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
    internal_config.windows[0].width = config.width;
    internal_config.windows[0].height = config.height;
    internal_config.vsync = config.vsync;
    internal_config.samples = config.samples;
    // Derive color_bits from individual color channel bits
    internal_config.color_bits = config.red_bits + config.green_bits + config.blue_bits + config.alpha_bits;
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
