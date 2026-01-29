/*
 * window_macos.mm - macOS (Cocoa) implementation
 * Backends: OpenGL, Vulkan (MoltenVK), Metal
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_MACOS)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Carbon/Carbon.h>  // For key codes
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

#ifdef WINDOW_SUPPORT_VULKAN
#define VK_USE_PLATFORM_MACOS_MVK
#include <vulkan/vulkan.h>
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// Key Translation
//=============================================================================

static Key translate_keycode(unsigned short keyCode) {
    switch (keyCode) {
        case kVK_ANSI_A: return Key::A; case kVK_ANSI_B: return Key::B;
        case kVK_ANSI_C: return Key::C; case kVK_ANSI_D: return Key::D;
        case kVK_ANSI_E: return Key::E; case kVK_ANSI_F: return Key::F;
        case kVK_ANSI_G: return Key::G; case kVK_ANSI_H: return Key::H;
        case kVK_ANSI_I: return Key::I; case kVK_ANSI_J: return Key::J;
        case kVK_ANSI_K: return Key::K; case kVK_ANSI_L: return Key::L;
        case kVK_ANSI_M: return Key::M; case kVK_ANSI_N: return Key::N;
        case kVK_ANSI_O: return Key::O; case kVK_ANSI_P: return Key::P;
        case kVK_ANSI_Q: return Key::Q; case kVK_ANSI_R: return Key::R;
        case kVK_ANSI_S: return Key::S; case kVK_ANSI_T: return Key::T;
        case kVK_ANSI_U: return Key::U; case kVK_ANSI_V: return Key::V;
        case kVK_ANSI_W: return Key::W; case kVK_ANSI_X: return Key::X;
        case kVK_ANSI_Y: return Key::Y; case kVK_ANSI_Z: return Key::Z;
        case kVK_ANSI_0: return Key::Num0; case kVK_ANSI_1: return Key::Num1;
        case kVK_ANSI_2: return Key::Num2; case kVK_ANSI_3: return Key::Num3;
        case kVK_ANSI_4: return Key::Num4; case kVK_ANSI_5: return Key::Num5;
        case kVK_ANSI_6: return Key::Num6; case kVK_ANSI_7: return Key::Num7;
        case kVK_ANSI_8: return Key::Num8; case kVK_ANSI_9: return Key::Num9;
        case kVK_F1: return Key::F1; case kVK_F2: return Key::F2;
        case kVK_F3: return Key::F3; case kVK_F4: return Key::F4;
        case kVK_F5: return Key::F5; case kVK_F6: return Key::F6;
        case kVK_F7: return Key::F7; case kVK_F8: return Key::F8;
        case kVK_F9: return Key::F9; case kVK_F10: return Key::F10;
        case kVK_F11: return Key::F11; case kVK_F12: return Key::F12;
        case kVK_Escape: return Key::Escape;
        case kVK_Tab: return Key::Tab;
        case kVK_CapsLock: return Key::CapsLock;
        case kVK_Space: return Key::Space;
        case kVK_Return: return Key::Enter;
        case kVK_Delete: return Key::Backspace;
        case kVK_ForwardDelete: return Key::Delete;
        case kVK_Help: return Key::Insert;
        case kVK_Home: return Key::Home;
        case kVK_End: return Key::End;
        case kVK_PageUp: return Key::PageUp;
        case kVK_PageDown: return Key::PageDown;
        case kVK_LeftArrow: return Key::Left;
        case kVK_RightArrow: return Key::Right;
        case kVK_UpArrow: return Key::Up;
        case kVK_DownArrow: return Key::Down;
        case kVK_Shift: return Key::LeftShift;
        case kVK_RightShift: return Key::RightShift;
        case kVK_Control: return Key::LeftControl;
        case kVK_RightControl: return Key::RightControl;
        case kVK_Option: return Key::LeftAlt;
        case kVK_RightOption: return Key::RightAlt;
        case kVK_Command: return Key::LeftSuper;
        case kVK_RightCommand: return Key::RightSuper;
        case kVK_ANSI_Grave: return Key::Grave;
        case kVK_ANSI_Minus: return Key::Minus;
        case kVK_ANSI_Equal: return Key::Equal;
        case kVK_ANSI_LeftBracket: return Key::LeftBracket;
        case kVK_ANSI_RightBracket: return Key::RightBracket;
        case kVK_ANSI_Backslash: return Key::Backslash;
        case kVK_ANSI_Semicolon: return Key::Semicolon;
        case kVK_ANSI_Quote: return Key::Apostrophe;
        case kVK_ANSI_Comma: return Key::Comma;
        case kVK_ANSI_Period: return Key::Period;
        case kVK_ANSI_Slash: return Key::Slash;
        case kVK_ANSI_Keypad0: return Key::Numpad0;
        case kVK_ANSI_Keypad1: return Key::Numpad1;
        case kVK_ANSI_Keypad2: return Key::Numpad2;
        case kVK_ANSI_Keypad3: return Key::Numpad3;
        case kVK_ANSI_Keypad4: return Key::Numpad4;
        case kVK_ANSI_Keypad5: return Key::Numpad5;
        case kVK_ANSI_Keypad6: return Key::Numpad6;
        case kVK_ANSI_Keypad7: return Key::Numpad7;
        case kVK_ANSI_Keypad8: return Key::Numpad8;
        case kVK_ANSI_Keypad9: return Key::Numpad9;
        case kVK_ANSI_KeypadDecimal: return Key::NumpadDecimal;
        case kVK_ANSI_KeypadEnter: return Key::NumpadEnter;
        case kVK_ANSI_KeypadPlus: return Key::NumpadAdd;
        case kVK_ANSI_KeypadMinus: return Key::NumpadSubtract;
        case kVK_ANSI_KeypadMultiply: return Key::NumpadMultiply;
        case kVK_ANSI_KeypadDivide: return Key::NumpadDivide;
        case kVK_ANSI_KeypadClear: return Key::NumLock;
        default: return Key::Unknown;
    }
}

static KeyMod get_cocoa_modifiers(NSEventModifierFlags flags) {
    KeyMod mods = KeyMod::None;
    if (flags & NSEventModifierFlagShift) mods = mods | KeyMod::Shift;
    if (flags & NSEventModifierFlagControl) mods = mods | KeyMod::Control;
    if (flags & NSEventModifierFlagOption) mods = mods | KeyMod::Alt;
    if (flags & NSEventModifierFlagCommand) mods = mods | KeyMod::Super;
    if (flags & NSEventModifierFlagCapsLock) mods = mods | KeyMod::CapsLock;
    return mods;
}

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
    WindowCloseCallback close_callback = nullptr;
    void* close_user_data = nullptr;

    WindowResizeCallback resize_callback = nullptr;
    void* resize_user_data = nullptr;

    WindowMoveCallback move_callback = nullptr;
    void* move_user_data = nullptr;

    WindowFocusCallback focus_callback = nullptr;
    void* focus_user_data = nullptr;

    WindowStateCallback state_callback = nullptr;
    void* state_user_data = nullptr;

    TouchCallback touch_callback = nullptr;
    void* touch_user_data = nullptr;

    DpiChangeCallback dpi_change_callback = nullptr;
    void* dpi_change_user_data = nullptr;

    DropFileCallback drop_file_callback = nullptr;
    void* drop_file_user_data = nullptr;
};

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
    Window* owner = nullptr;  // Back-pointer for event dispatch
    bool should_close_flag = false;
    bool visible = false;
    bool focused = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    bool is_fullscreen = false;
    // For fullscreen toggle restoration
    NSRect windowed_frame = NSZeroRect;
    NSWindowStyleMask windowed_style_mask = 0;

    // Event callbacks
    EventCallbacks callbacks;

    // Input state
    bool mouse_in_window = false;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;
};

// Helper to convert WindowStyle to NSWindowStyleMask
static NSWindowStyleMask style_to_ns_style_mask(WindowStyle style) {
    if (has_style(style, WindowStyle::Fullscreen)) {
        return NSWindowStyleMaskBorderless;
    }

    NSWindowStyleMask mask = 0;

    if (has_style(style, WindowStyle::TitleBar)) {
        mask |= NSWindowStyleMaskTitled;
    }

    if (has_style(style, WindowStyle::CloseButton)) {
        mask |= NSWindowStyleMaskClosable;
    }

    if (has_style(style, WindowStyle::MinimizeButton)) {
        mask |= NSWindowStyleMaskMiniaturizable;
    }

    if (has_style(style, WindowStyle::Resizable)) {
        mask |= NSWindowStyleMaskResizable;
    }

    // Borderless if no title bar and no border
    if (!has_style(style, WindowStyle::TitleBar) && !has_style(style, WindowStyle::Border)) {
        mask = NSWindowStyleMaskBorderless;
    }

    return mask;
}

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

// Keyboard events
- (void)keyDown:(NSEvent*)event {
    if (!_impl) return;

    window::Key key = window::translate_keycode([event keyCode]);
    if (key != window::Key::Unknown && static_cast<int>(key) < 512) {
        _impl->key_states[static_cast<int>(key)] = true;
    }

    bool isRepeat = [event isARepeat];

    if (_impl->callbacks.key_callback) {
        window::KeyEvent keyEvent;
        keyEvent.type = isRepeat ? window::EventType::KeyRepeat : window::EventType::KeyDown;
        keyEvent.window = _impl->owner;
        keyEvent.timestamp = window::get_event_timestamp();
        keyEvent.key = key;
        keyEvent.modifiers = window::get_cocoa_modifiers([event modifierFlags]);
        keyEvent.scancode = [event keyCode];
        keyEvent.repeat = isRepeat;
        _impl->callbacks.key_callback(keyEvent, _impl->callbacks.key_user_data);
    }

    // Character input
    if (_impl->callbacks.char_callback) {
        NSString* chars = [event characters];
        if ([chars length] > 0) {
            unichar c = [chars characterAtIndex:0];
            if (c >= 32 || c == '\t' || c == '\n' || c == '\r') {
                window::CharEvent charEvent;
                charEvent.type = window::EventType::CharInput;
                charEvent.window = _impl->owner;
                charEvent.timestamp = window::get_event_timestamp();
                charEvent.codepoint = c;
                charEvent.modifiers = window::get_cocoa_modifiers([event modifierFlags]);
                _impl->callbacks.char_callback(charEvent, _impl->callbacks.char_user_data);
            }
        }
    }
}

- (void)keyUp:(NSEvent*)event {
    if (!_impl) return;

    window::Key key = window::translate_keycode([event keyCode]);
    if (key != window::Key::Unknown && static_cast<int>(key) < 512) {
        _impl->key_states[static_cast<int>(key)] = false;
    }

    if (_impl->callbacks.key_callback) {
        window::KeyEvent keyEvent;
        keyEvent.type = window::EventType::KeyUp;
        keyEvent.window = _impl->owner;
        keyEvent.timestamp = window::get_event_timestamp();
        keyEvent.key = key;
        keyEvent.modifiers = window::get_cocoa_modifiers([event modifierFlags]);
        keyEvent.scancode = [event keyCode];
        keyEvent.repeat = false;
        _impl->callbacks.key_callback(keyEvent, _impl->callbacks.key_user_data);
    }
}

- (void)flagsChanged:(NSEvent*)event {
    // Handle modifier key changes
    if (!_impl || !_impl->callbacks.key_callback) return;

    window::Key key = window::translate_keycode([event keyCode]);
    bool pressed = false;

    NSEventModifierFlags flags = [event modifierFlags];
    switch ([event keyCode]) {
        case kVK_Shift:
        case kVK_RightShift:
            pressed = (flags & NSEventModifierFlagShift) != 0;
            break;
        case kVK_Control:
        case kVK_RightControl:
            pressed = (flags & NSEventModifierFlagControl) != 0;
            break;
        case kVK_Option:
        case kVK_RightOption:
            pressed = (flags & NSEventModifierFlagOption) != 0;
            break;
        case kVK_Command:
        case kVK_RightCommand:
            pressed = (flags & NSEventModifierFlagCommand) != 0;
            break;
        case kVK_CapsLock:
            pressed = (flags & NSEventModifierFlagCapsLock) != 0;
            break;
    }

    if (key != window::Key::Unknown && static_cast<int>(key) < 512) {
        _impl->key_states[static_cast<int>(key)] = pressed;
    }

    window::KeyEvent keyEvent;
    keyEvent.type = pressed ? window::EventType::KeyDown : window::EventType::KeyUp;
    keyEvent.window = _impl->owner;
    keyEvent.timestamp = window::get_event_timestamp();
    keyEvent.key = key;
    keyEvent.modifiers = window::get_cocoa_modifiers(flags);
    keyEvent.scancode = [event keyCode];
    keyEvent.repeat = false;
    _impl->callbacks.key_callback(keyEvent, _impl->callbacks.key_user_data);
}

// Mouse events
- (void)mouseDown:(NSEvent*)event {
    [self handleMouseButton:event button:window::MouseButton::Left pressed:YES];
}

- (void)mouseUp:(NSEvent*)event {
    [self handleMouseButton:event button:window::MouseButton::Left pressed:NO];
}

- (void)rightMouseDown:(NSEvent*)event {
    [self handleMouseButton:event button:window::MouseButton::Right pressed:YES];
}

- (void)rightMouseUp:(NSEvent*)event {
    [self handleMouseButton:event button:window::MouseButton::Right pressed:NO];
}

- (void)otherMouseDown:(NSEvent*)event {
    window::MouseButton btn = window::MouseButton::Middle;
    if ([event buttonNumber] == 3) btn = window::MouseButton::X1;
    else if ([event buttonNumber] == 4) btn = window::MouseButton::X2;
    [self handleMouseButton:event button:btn pressed:YES];
}

- (void)otherMouseUp:(NSEvent*)event {
    window::MouseButton btn = window::MouseButton::Middle;
    if ([event buttonNumber] == 3) btn = window::MouseButton::X1;
    else if ([event buttonNumber] == 4) btn = window::MouseButton::X2;
    [self handleMouseButton:event button:btn pressed:NO];
}

- (void)handleMouseButton:(NSEvent*)event button:(window::MouseButton)button pressed:(BOOL)pressed {
    if (!_impl) return;

    NSPoint pos = [self convertPoint:[event locationInWindow] fromView:nil];
    CGFloat scale = [[self window] backingScaleFactor];
    int x = static_cast<int>(pos.x * scale);
    int y = static_cast<int>((_impl->height / scale - pos.y) * scale);  // Flip Y
    window::KeyMod modifiers = window::get_cocoa_modifiers([event modifierFlags]);
    double timestamp = window::get_event_timestamp();

    if (pressed) {
        _impl->mouse_device.inject_button_down(button, x, y, static_cast<int>([event clickCount]), modifiers, timestamp);
    } else {
        _impl->mouse_device.inject_button_up(button, x, y, modifiers, timestamp);
    }
}

- (void)mouseMoved:(NSEvent*)event {
    [self handleMouseMove:event];
}

- (void)mouseDragged:(NSEvent*)event {
    [self handleMouseMove:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
    [self handleMouseMove:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
    [self handleMouseMove:event];
}

- (void)handleMouseMove:(NSEvent*)event {
    if (!_impl) return;

    NSPoint pos = [self convertPoint:[event locationInWindow] fromView:nil];
    CGFloat scale = [[self window] backingScaleFactor];
    int x = static_cast<int>(pos.x * scale);
    int y = static_cast<int>((_impl->height / scale - pos.y) * scale);  // Flip Y

    _impl->mouse_device.inject_move(x, y, window::get_cocoa_modifiers([event modifierFlags]), window::get_event_timestamp());
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_impl) return;

    NSPoint pos = [self convertPoint:[event locationInWindow] fromView:nil];
    CGFloat scale = [[self window] backingScaleFactor];
    int x = static_cast<int>(pos.x * scale);
    int y = static_cast<int>((_impl->height / scale - pos.y) * scale);

    _impl->mouse_device.inject_wheel(
        static_cast<float>([event scrollingDeltaX]),
        static_cast<float>([event scrollingDeltaY]),
        x, y,
        window::get_cocoa_modifiers([event modifierFlags]),
        window::get_event_timestamp()
    );
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    if (!_impl) return;
    _impl->mouse_in_window = true;
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    if (!_impl) return;
    _impl->mouse_in_window = false;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];

    // Remove old tracking areas
    for (NSTrackingArea* area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }

    // Add new tracking area for mouse enter/exit
    NSTrackingArea* trackingArea = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
        options:(NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                 NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
        owner:self
        userInfo:nil];
    [self addTrackingArea:trackingArea];
}

@end

//=============================================================================
// WindowDelegate Implementation
//=============================================================================

@implementation WindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (_impl) {
        _impl->should_close_flag = true;

        if (_impl->callbacks.close_callback) {
            window::WindowCloseEvent closeEvent;
            closeEvent.type = window::EventType::WindowClose;
            closeEvent.window = _impl->owner;
            closeEvent.timestamp = window::get_event_timestamp();
            _impl->callbacks.close_callback(closeEvent, _impl->callbacks.close_user_data);
        }
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

        if (_impl->callbacks.resize_callback) {
            window::WindowResizeEvent resizeEvent;
            resizeEvent.type = window::EventType::WindowResize;
            resizeEvent.window = _impl->owner;
            resizeEvent.timestamp = window::get_event_timestamp();
            resizeEvent.width = _impl->width;
            resizeEvent.height = _impl->height;
            resizeEvent.minimized = false;
            _impl->callbacks.resize_callback(resizeEvent, _impl->callbacks.resize_user_data);
        }
    }
}

- (void)windowDidMove:(NSNotification*)notification {
    if (_impl) {
        NSWindow* window = notification.object;
        NSRect frame = [window frame];
        _impl->x = static_cast<int>(frame.origin.x);
        _impl->y = static_cast<int>(frame.origin.y);

        if (_impl->callbacks.move_callback) {
            window::WindowMoveEvent moveEvent;
            moveEvent.type = window::EventType::WindowMove;
            moveEvent.window = _impl->owner;
            moveEvent.timestamp = window::get_event_timestamp();
            moveEvent.x = _impl->x;
            moveEvent.y = _impl->y;
            _impl->callbacks.move_callback(moveEvent, _impl->callbacks.move_user_data);
        }
    }
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    if (_impl) {
        _impl->visible = false;

        if (_impl->callbacks.state_callback) {
            window::WindowStateEvent stateEvent;
            stateEvent.type = window::EventType::WindowMinimize;
            stateEvent.window = _impl->owner;
            stateEvent.timestamp = window::get_event_timestamp();
            stateEvent.minimized = true;
            stateEvent.maximized = false;
            _impl->callbacks.state_callback(stateEvent, _impl->callbacks.state_user_data);
        }
    }
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    if (_impl) {
        _impl->visible = true;

        if (_impl->callbacks.state_callback) {
            window::WindowStateEvent stateEvent;
            stateEvent.type = window::EventType::WindowRestore;
            stateEvent.window = _impl->owner;
            stateEvent.timestamp = window::get_event_timestamp();
            stateEvent.minimized = false;
            stateEvent.maximized = false;
            _impl->callbacks.state_callback(stateEvent, _impl->callbacks.state_user_data);
        }
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (_impl) {
        _impl->focused = true;

        if (_impl->callbacks.focus_callback) {
            window::WindowFocusEvent focusEvent;
            focusEvent.type = window::EventType::WindowFocus;
            focusEvent.window = _impl->owner;
            focusEvent.timestamp = window::get_event_timestamp();
            focusEvent.focused = true;
            _impl->callbacks.focus_callback(focusEvent, _impl->callbacks.focus_user_data);
        }
    }
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (_impl) {
        _impl->focused = false;
        // Reset key states on focus loss
        memset(_impl->key_states, 0, sizeof(_impl->key_states));
        _impl->mouse_device.reset();

        if (_impl->callbacks.focus_callback) {
            window::WindowFocusEvent focusEvent;
            focusEvent.type = window::EventType::WindowBlur;
            focusEvent.window = _impl->owner;
            focusEvent.timestamp = window::get_event_timestamp();
            focusEvent.focused = false;
            _impl->callbacks.focus_callback(focusEvent, _impl->callbacks.focus_user_data);
        }
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
        window->impl->owner = window;  // Set back-pointer for event dispatch
        window->impl->width = config.width;
        window->impl->height = config.height;
        window->impl->title = config.title;

        // Combine config.style with legacy config.resizable flag
        WindowStyle effective_style = config.style;
        if (!config.resizable) {
            effective_style = effective_style & ~WindowStyle::Resizable;
        }
        window->impl->style = effective_style;
        window->impl->is_fullscreen = has_style(effective_style, WindowStyle::Fullscreen);

        NSWindowStyleMask styleMask = style_to_ns_style_mask(effective_style);

        NSRect frame;
        if (has_style(effective_style, WindowStyle::Fullscreen)) {
            frame = [[NSScreen mainScreen] frame];
        } else {
            frame = NSMakeRect(0, 0, config.width, config.height);
        }

        NSWindow* nsWindow = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:styleMask
                        backing:NSBackingStoreBuffered
                          defer:NO];

        // Set level for always-on-top
        if (has_style(effective_style, WindowStyle::AlwaysOnTop)) {
            [nsWindow setLevel:NSFloatingWindowLevel];
        }

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

        // Initialize mouse input system
        window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
        window->impl->mouse_device.set_window(window);

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

void Window::set_style(WindowStyle style) {
    if (!impl || !impl->ns_window) return;

    impl->style = style;

    // Handle fullscreen
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
        return;
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    NSWindowStyleMask styleMask = style_to_ns_style_mask(style);
    [impl->ns_window setStyleMask:styleMask];

    // Handle always-on-top
    if (has_style(style, WindowStyle::AlwaysOnTop)) {
        [impl->ns_window setLevel:NSFloatingWindowLevel];
    } else {
        [impl->ns_window setLevel:NSNormalWindowLevel];
    }
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !impl->ns_window) return;
    if (impl->is_fullscreen == fullscreen) return;

    @autoreleasepool {
        if (fullscreen) {
            // Save windowed state
            impl->windowed_frame = [impl->ns_window frame];
            impl->windowed_style_mask = [impl->ns_window styleMask];

            // Enter fullscreen
            [impl->ns_window setStyleMask:NSWindowStyleMaskBorderless];
            [impl->ns_window setFrame:[[NSScreen mainScreen] frame] display:YES animate:NO];
            [impl->ns_window setLevel:NSMainMenuWindowLevel + 1];

            impl->is_fullscreen = true;
            impl->style = impl->style | WindowStyle::Fullscreen;
        } else {
            // Restore windowed state
            [impl->ns_window setStyleMask:impl->windowed_style_mask];
            [impl->ns_window setFrame:impl->windowed_frame display:YES animate:NO];
            [impl->ns_window setLevel:has_style(impl->style, WindowStyle::AlwaysOnTop) ?
                NSFloatingWindowLevel : NSNormalWindowLevel];

            impl->is_fullscreen = false;
            impl->style = impl->style & ~WindowStyle::Fullscreen;
        }
    }
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    if (!impl || !impl->ns_window) return;

    if (always_on_top) {
        impl->style = impl->style | WindowStyle::AlwaysOnTop;
        [impl->ns_window setLevel:NSFloatingWindowLevel];
    } else {
        impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        [impl->ns_window setLevel:NSNormalWindowLevel];
    }
}

bool Window::is_always_on_top() const {
    return impl ? has_style(impl->style, WindowStyle::AlwaysOnTop) : false;
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
// Event Callback Setters
//=============================================================================

void Window::set_close_callback(WindowCloseCallback callback, void* user_data) {
    if (impl) { impl->callbacks.close_callback = callback; impl->callbacks.close_user_data = user_data; }
}

void Window::set_resize_callback(WindowResizeCallback callback, void* user_data) {
    if (impl) { impl->callbacks.resize_callback = callback; impl->callbacks.resize_user_data = user_data; }
}

void Window::set_move_callback(WindowMoveCallback callback, void* user_data) {
    if (impl) { impl->callbacks.move_callback = callback; impl->callbacks.move_user_data = user_data; }
}

void Window::set_focus_callback(WindowFocusCallback callback, void* user_data) {
    if (impl) { impl->callbacks.focus_callback = callback; impl->callbacks.focus_user_data = user_data; }
}

void Window::set_state_callback(WindowStateCallback callback, void* user_data) {
    if (impl) { impl->callbacks.state_callback = callback; impl->callbacks.state_user_data = user_data; }
}

void Window::set_touch_callback(TouchCallback callback, void* user_data) {
    if (impl) { impl->callbacks.touch_callback = callback; impl->callbacks.touch_user_data = user_data; }
}

void Window::set_dpi_change_callback(DpiChangeCallback callback, void* user_data) {
    if (impl) { impl->callbacks.dpi_change_callback = callback; impl->callbacks.dpi_change_user_data = user_data; }
}

void Window::set_drop_file_callback(DropFileCallback callback, void* user_data) {
    if (impl) { impl->callbacks.drop_file_callback = callback; impl->callbacks.drop_file_user_data = user_data; }
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
        impl->mouse_device.get_position(x, y);
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

KeyMod Window::get_current_modifiers() const {
    if (!impl) return KeyMod::None;
    NSEventModifierFlags flags = [NSEvent modifierFlags];
    return get_cocoa_modifiers(flags);
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
    NSView* ns_view = (__bridge NSView*)config.native_handle;

    switch (requested) {
#ifdef WINDOW_HAS_METAL
        case Backend::Metal:
            gfx = create_metal_graphics_nsview((__bridge void*)ns_view, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_nsview((__bridge void*)ns_view, config.width, config.height, internal_config);
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

#endif // WINDOW_PLATFORM_MACOS
