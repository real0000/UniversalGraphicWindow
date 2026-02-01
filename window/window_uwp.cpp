/*
 * window_uwp.cpp - UWP (Universal Windows Platform) implementation
 * Backends: OpenGL (via ANGLE/EGL), Vulkan, D3D11, D3D12
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_UWP)

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Devices.Input.h>

#include <string>
#include <chrono>

using namespace winrt;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;
using namespace Windows::UI::ViewManagement;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace Windows::System;
using namespace Windows::UI::Input;

namespace window {

//=============================================================================
// Key Translation
//=============================================================================

static Key translate_virtual_key(VirtualKey vk) {
    switch (vk) {
        case VirtualKey::A: return Key::A; case VirtualKey::B: return Key::B;
        case VirtualKey::C: return Key::C; case VirtualKey::D: return Key::D;
        case VirtualKey::E: return Key::E; case VirtualKey::F: return Key::F;
        case VirtualKey::G: return Key::G; case VirtualKey::H: return Key::H;
        case VirtualKey::I: return Key::I; case VirtualKey::J: return Key::J;
        case VirtualKey::K: return Key::K; case VirtualKey::L: return Key::L;
        case VirtualKey::M: return Key::M; case VirtualKey::N: return Key::N;
        case VirtualKey::O: return Key::O; case VirtualKey::P: return Key::P;
        case VirtualKey::Q: return Key::Q; case VirtualKey::R: return Key::R;
        case VirtualKey::S: return Key::S; case VirtualKey::T: return Key::T;
        case VirtualKey::U: return Key::U; case VirtualKey::V: return Key::V;
        case VirtualKey::W: return Key::W; case VirtualKey::X: return Key::X;
        case VirtualKey::Y: return Key::Y; case VirtualKey::Z: return Key::Z;
        case VirtualKey::Number0: return Key::Num0; case VirtualKey::Number1: return Key::Num1;
        case VirtualKey::Number2: return Key::Num2; case VirtualKey::Number3: return Key::Num3;
        case VirtualKey::Number4: return Key::Num4; case VirtualKey::Number5: return Key::Num5;
        case VirtualKey::Number6: return Key::Num6; case VirtualKey::Number7: return Key::Num7;
        case VirtualKey::Number8: return Key::Num8; case VirtualKey::Number9: return Key::Num9;
        case VirtualKey::F1: return Key::F1; case VirtualKey::F2: return Key::F2;
        case VirtualKey::F3: return Key::F3; case VirtualKey::F4: return Key::F4;
        case VirtualKey::F5: return Key::F5; case VirtualKey::F6: return Key::F6;
        case VirtualKey::F7: return Key::F7; case VirtualKey::F8: return Key::F8;
        case VirtualKey::F9: return Key::F9; case VirtualKey::F10: return Key::F10;
        case VirtualKey::F11: return Key::F11; case VirtualKey::F12: return Key::F12;
        case VirtualKey::Escape: return Key::Escape;
        case VirtualKey::Tab: return Key::Tab;
        case VirtualKey::CapitalLock: return Key::CapsLock;
        case VirtualKey::Space: return Key::Space;
        case VirtualKey::Enter: return Key::Enter;
        case VirtualKey::Back: return Key::Backspace;
        case VirtualKey::Delete: return Key::Delete;
        case VirtualKey::Insert: return Key::Insert;
        case VirtualKey::Home: return Key::Home;
        case VirtualKey::End: return Key::End;
        case VirtualKey::PageUp: return Key::PageUp;
        case VirtualKey::PageDown: return Key::PageDown;
        case VirtualKey::Left: return Key::Left;
        case VirtualKey::Right: return Key::Right;
        case VirtualKey::Up: return Key::Up;
        case VirtualKey::Down: return Key::Down;
        case VirtualKey::Shift: return Key::Shift;
        case VirtualKey::LeftShift: return Key::LeftShift;
        case VirtualKey::RightShift: return Key::RightShift;
        case VirtualKey::Control: return Key::Control;
        case VirtualKey::LeftControl: return Key::LeftControl;
        case VirtualKey::RightControl: return Key::RightControl;
        case VirtualKey::Menu: return Key::Alt;
        case VirtualKey::LeftMenu: return Key::LeftAlt;
        case VirtualKey::RightMenu: return Key::RightAlt;
        case VirtualKey::LeftWindows: return Key::LeftSuper;
        case VirtualKey::RightWindows: return Key::RightSuper;
        case VirtualKey::NumberPad0: return Key::Numpad0;
        case VirtualKey::NumberPad1: return Key::Numpad1;
        case VirtualKey::NumberPad2: return Key::Numpad2;
        case VirtualKey::NumberPad3: return Key::Numpad3;
        case VirtualKey::NumberPad4: return Key::Numpad4;
        case VirtualKey::NumberPad5: return Key::Numpad5;
        case VirtualKey::NumberPad6: return Key::Numpad6;
        case VirtualKey::NumberPad7: return Key::Numpad7;
        case VirtualKey::NumberPad8: return Key::Numpad8;
        case VirtualKey::NumberPad9: return Key::Numpad9;
        case VirtualKey::Decimal: return Key::NumpadDecimal;
        case VirtualKey::Add: return Key::NumpadAdd;
        case VirtualKey::Subtract: return Key::NumpadSubtract;
        case VirtualKey::Multiply: return Key::NumpadMultiply;
        case VirtualKey::Divide: return Key::NumpadDivide;
        case VirtualKey::NumberKeyLock: return Key::NumLock;
        case VirtualKey::Scroll: return Key::ScrollLock;
        case VirtualKey::Pause: return Key::Pause;
        case VirtualKey::Application: return Key::Menu;
        default: return Key::Unknown;
    }
}

static KeyMod get_uwp_modifiers(CoreWindow const& window) {
    KeyMod mods = KeyMod::None;
    auto state = window.GetKeyState(VirtualKey::Shift);
    if ((state & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down) mods = mods | KeyMod::Shift;
    state = window.GetKeyState(VirtualKey::Control);
    if ((state & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down) mods = mods | KeyMod::Control;
    state = window.GetKeyState(VirtualKey::Menu);
    if ((state & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down) mods = mods | KeyMod::Alt;
    state = window.GetKeyState(VirtualKey::LeftWindows);
    if ((state & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down) mods = mods | KeyMod::Super;
    state = window.GetKeyState(VirtualKey::CapitalLock);
    if ((state & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked) mods = mods | KeyMod::CapsLock;
    state = window.GetKeyState(VirtualKey::NumberKeyLock);
    if ((state & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked) mods = mods | KeyMod::NumLock;
    return mods;
}

static double get_event_timestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration<double>(duration).count();
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
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define WINDOW_HAS_VULKAN 1
#endif

#ifdef WINDOW_SUPPORT_D3D11
#define WINDOW_HAS_D3D11 1
#endif

#ifdef WINDOW_SUPPORT_D3D12
#define WINDOW_HAS_D3D12 1
#endif

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D11
Graphics* create_d3d11_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D12
Graphics* create_d3d12_graphics_corewindow(void* core_window, int width, int height, const Config& config);
#endif

//=============================================================================
// Window Implementation
//=============================================================================

struct Window::Impl {
    CoreWindow core_window = nullptr;
    Window* owner = nullptr;  // Back-pointer for event dispatch
    bool should_close_flag = false;
    bool visible = true;
    bool focused = true;
    int width = 0;
    int height = 0;
    float dpi = 96.0f;
    std::string title;
    Graphics* gfx = nullptr;
    bool owns_graphics = true;  // Whether this window owns its graphics context
    WindowStyle style = WindowStyle::Default;
    bool is_fullscreen = false;

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

// Global window instance for UWP
static Window* g_uwp_window = nullptr;

Window* create_window_impl(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    // In UWP, we need to get the CoreWindow from the current thread
    CoreWindow core_window = CoreWindow::GetForCurrentThread();
    if (!core_window) {
        set_result(Result::ErrorPlatformInit);
        return nullptr;
    }

    const WindowConfigEntry& win_cfg = config.windows[0];

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->owner = window;  // Set back-pointer for event dispatch
    window->impl->core_window = core_window;
    window->impl->title = win_cfg.title;
    window->impl->owns_graphics = (config.shared_graphics == nullptr);

    // Initialize mouse input system
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
    window->impl->mouse_device.set_window(window);

    // Get window size
    auto bounds = core_window.Bounds();
    window->impl->width = static_cast<int>(bounds.Width);
    window->impl->height = static_cast<int>(bounds.Height);

    // Get DPI
    DisplayInformation display_info = DisplayInformation::GetForCurrentView();
    window->impl->dpi = display_info.LogicalDpi();

    g_uwp_window = window;

    // Setup event handlers
    core_window.Closed([](CoreWindow const&, CoreWindowEventArgs const&) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->should_close_flag = true;

            if (g_uwp_window->impl->callbacks.close_callback) {
                WindowCloseEvent event;
                event.type = EventType::WindowClose;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                g_uwp_window->impl->callbacks.close_callback(event);
            }
        }
    });

    core_window.SizeChanged([](CoreWindow const&, WindowSizeChangedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->width = static_cast<int>(args.Size().Width);
            g_uwp_window->impl->height = static_cast<int>(args.Size().Height);

            if (g_uwp_window->impl->callbacks.resize_callback) {
                WindowResizeEvent event;
                event.type = EventType::WindowResize;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                event.width = g_uwp_window->impl->width;
                event.height = g_uwp_window->impl->height;
                event.minimized = false;
                g_uwp_window->impl->callbacks.resize_callback(event);
            }
        }
    });

    core_window.VisibilityChanged([](CoreWindow const&, VisibilityChangedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->visible = args.Visible();
        }
    });

    core_window.Activated([](CoreWindow const&, WindowActivatedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            bool focused = (args.WindowActivationState() != CoreWindowActivationState::Deactivated);
            g_uwp_window->impl->focused = focused;

            if (!focused) {
                // Reset key states and mouse on focus loss
                memset(g_uwp_window->impl->key_states, 0, sizeof(g_uwp_window->impl->key_states));
                g_uwp_window->impl->mouse_device.reset();
            }

            if (g_uwp_window->impl->callbacks.focus_callback) {
                WindowFocusEvent event;
                event.type = focused ? EventType::WindowFocus : EventType::WindowBlur;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                event.focused = focused;
                g_uwp_window->impl->callbacks.focus_callback(event);
            }
        }
    });

    core_window.KeyDown([](CoreWindow const& sender, KeyEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            Key key = translate_virtual_key(args.VirtualKey());
            if (key != Key::Unknown && static_cast<int>(key) < 512) {
                g_uwp_window->impl->key_states[static_cast<int>(key)] = true;
            }

            if (g_uwp_window->impl->callbacks.key_callback) {
                KeyEvent event;
                event.type = args.KeyStatus().WasKeyDown ? EventType::KeyRepeat : EventType::KeyDown;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                event.key = key;
                event.modifiers = get_uwp_modifiers(sender);
                event.scancode = args.KeyStatus().ScanCode;
                event.repeat = args.KeyStatus().WasKeyDown;
                g_uwp_window->impl->callbacks.key_callback(event, g_uwp_window->impl->callbacks.key_user_data);
            }
        }
    });

    core_window.KeyUp([](CoreWindow const& sender, KeyEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            Key key = translate_virtual_key(args.VirtualKey());
            if (key != Key::Unknown && static_cast<int>(key) < 512) {
                g_uwp_window->impl->key_states[static_cast<int>(key)] = false;
            }

            if (g_uwp_window->impl->callbacks.key_callback) {
                KeyEvent event;
                event.type = EventType::KeyUp;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                event.key = key;
                event.modifiers = get_uwp_modifiers(sender);
                event.scancode = args.KeyStatus().ScanCode;
                event.repeat = false;
                g_uwp_window->impl->callbacks.key_callback(event, g_uwp_window->impl->callbacks.key_user_data);
            }
        }
    });

    core_window.CharacterReceived([](CoreWindow const& sender, CharacterReceivedEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl && g_uwp_window->impl->callbacks.char_callback) {
            uint32_t codepoint = args.KeyCode();
            if (codepoint >= 32 || codepoint == '\t' || codepoint == '\n' || codepoint == '\r') {
                CharEvent event;
                event.type = EventType::CharInput;
                event.window = g_uwp_window->impl->owner;
                event.timestamp = get_event_timestamp();
                event.codepoint = codepoint;
                event.modifiers = get_uwp_modifiers(sender);
                g_uwp_window->impl->callbacks.char_callback(event, g_uwp_window->impl->callbacks.char_user_data);
            }
        }
    });

    core_window.PointerMoved([](CoreWindow const& sender, PointerEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            auto point = args.CurrentPoint();
            int x = static_cast<int>(point.Position().X);
            int y = static_cast<int>(point.Position().Y);
            g_uwp_window->impl->mouse_device.inject_move(x, y, get_uwp_modifiers(sender), get_event_timestamp());
        }
    });

    core_window.PointerPressed([](CoreWindow const& sender, PointerEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            auto point = args.CurrentPoint();
            auto props = point.Properties();
            int x = static_cast<int>(point.Position().X);
            int y = static_cast<int>(point.Position().Y);

            MouseButton btn = MouseButton::Unknown;
            if (props.IsLeftButtonPressed()) btn = MouseButton::Left;
            else if (props.IsRightButtonPressed()) btn = MouseButton::Right;
            else if (props.IsMiddleButtonPressed()) btn = MouseButton::Middle;
            else if (props.IsXButton1Pressed()) btn = MouseButton::X1;
            else if (props.IsXButton2Pressed()) btn = MouseButton::X2;

            g_uwp_window->impl->mouse_device.inject_button_down(btn, x, y, 1, get_uwp_modifiers(sender), get_event_timestamp());
        }
    });

    core_window.PointerReleased([](CoreWindow const& sender, PointerEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            auto point = args.CurrentPoint();
            int x = static_cast<int>(point.Position().X);
            int y = static_cast<int>(point.Position().Y);

            // Determine which button was released by checking which is now false
            MouseButton btn = MouseButton::Unknown;
            auto props = point.Properties();
            if (!props.IsLeftButtonPressed() && g_uwp_window->impl->mouse_device.is_button_down(MouseButton::Left)) btn = MouseButton::Left;
            else if (!props.IsRightButtonPressed() && g_uwp_window->impl->mouse_device.is_button_down(MouseButton::Right)) btn = MouseButton::Right;
            else if (!props.IsMiddleButtonPressed() && g_uwp_window->impl->mouse_device.is_button_down(MouseButton::Middle)) btn = MouseButton::Middle;
            else if (!props.IsXButton1Pressed() && g_uwp_window->impl->mouse_device.is_button_down(MouseButton::X1)) btn = MouseButton::X1;
            else if (!props.IsXButton2Pressed() && g_uwp_window->impl->mouse_device.is_button_down(MouseButton::X2)) btn = MouseButton::X2;

            g_uwp_window->impl->mouse_device.inject_button_up(btn, x, y, get_uwp_modifiers(sender), get_event_timestamp());
        }
    });

    core_window.PointerWheelChanged([](CoreWindow const& sender, PointerEventArgs const& args) {
        if (g_uwp_window && g_uwp_window->impl) {
            auto point = args.CurrentPoint();
            auto props = point.Properties();
            int x = static_cast<int>(point.Position().X);
            int y = static_cast<int>(point.Position().Y);

            float dy = static_cast<float>(props.MouseWheelDelta()) / 120.0f;
            float dx = 0;
            if (props.IsHorizontalMouseWheel()) {
                dx = dy;
                dy = 0;
            }
            g_uwp_window->impl->mouse_device.inject_wheel(dx, dy, x, y, get_uwp_modifiers(sender), get_event_timestamp());
        }
    });

    core_window.PointerEntered([](CoreWindow const& sender, PointerEventArgs const& args) {
        (void)sender;
        (void)args;
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->mouse_in_window = true;
        }
    });

    core_window.PointerExited([](CoreWindow const& sender, PointerEventArgs const& args) {
        (void)sender;
        (void)args;
        if (g_uwp_window && g_uwp_window->impl) {
            g_uwp_window->impl->mouse_in_window = false;
        }
    });

    // Use shared graphics if provided, otherwise create new one
    void* core_window_abi = winrt::get_abi(core_window);
    Graphics* gfx = config.shared_graphics;

    if (!gfx) {
        // Create graphics backend based on config.backend
        Backend requested = config.backend;
        if (requested == Backend::Auto) {
            requested = get_default_backend();
        }

        switch (requested) {
#ifdef WINDOW_HAS_D3D11
            case Backend::D3D11:
                gfx = create_d3d11_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_D3D12
            case Backend::D3D12:
                gfx = create_d3d12_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                break;
#endif
            default:
                break;
        }

        // Fallback to default if requested backend failed or not supported
        if (!gfx && config.backend != Backend::Auto) {
            Backend fallback = get_default_backend();
            switch (fallback) {
#ifdef WINDOW_HAS_D3D11
                case Backend::D3D11:
                    gfx = create_d3d11_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                    break;
#endif
#ifdef WINDOW_HAS_D3D12
                case Backend::D3D12:
                    gfx = create_d3d12_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                    break;
#endif
#ifdef WINDOW_HAS_OPENGL
                case Backend::OpenGL:
                    gfx = create_opengl_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                    break;
#endif
#ifdef WINDOW_HAS_VULKAN
                case Backend::Vulkan:
                    gfx = create_vulkan_graphics_corewindow(core_window_abi, window->impl->width, window->impl->height, config);
                    break;
#endif
                default:
                    break;
            }
        }

        if (!gfx) {
            delete window->impl;
            delete window;
            g_uwp_window = nullptr;
            set_result(Result::ErrorGraphicsInit);
            return nullptr;
        }
    }

    window->impl->gfx = gfx;

    // Set title
    auto view = ApplicationView::GetForCurrentView();
    int len = MultiByteToWideChar(CP_UTF8, 0, win_cfg.title, -1, nullptr, 0);
    wchar_t* wide = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, win_cfg.title, -1, wide, len);
    view.Title(wide);
    delete[] wide;

    if (win_cfg.visible) {
        core_window.Activate();
    }

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        if (impl->owns_graphics && impl->gfx) {
            impl->gfx->destroy();
        }
        delete impl;
        impl = nullptr;
    }
    g_uwp_window = nullptr;
    delete this;
}

void Window::show() {
    if (impl && impl->core_window) {
        impl->core_window.Activate();
        impl->visible = true;
    }
}

void Window::hide() {
    // UWP doesn't support hiding the main window
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    if (impl) {
        auto view = ApplicationView::GetForCurrentView();
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        wchar_t* wide = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, len);
        view.Title(wide);
        delete[] wide;
        impl->title = title;
    }
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    if (impl) {
        auto view = ApplicationView::GetForCurrentView();
        view.TryResizeView(Size(static_cast<float>(width), static_cast<float>(height)));
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
    // UWP doesn't allow positioning windows
    (void)x; (void)y;
    return false;
}

bool Window::get_position(int* x, int* y) const {
    // UWP windows don't have a position in the traditional sense
    if (x) *x = 0;
    if (y) *y = 0;
    return false;
}

bool Window::supports_position() const {
    return false;
}

void Window::set_style(WindowStyle style) {
    if (!impl) return;

    impl->style = style;

    // Handle fullscreen
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl) return;
    if (impl->is_fullscreen == fullscreen) return;

    auto view = ApplicationView::GetForCurrentView();

    if (fullscreen) {
        if (view.TryEnterFullScreenMode()) {
            impl->is_fullscreen = true;
            impl->style = impl->style | WindowStyle::Fullscreen;
        }
    } else {
        view.ExitFullScreenMode();
        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    // UWP doesn't support always-on-top for regular apps
    if (impl) {
        if (always_on_top) {
            impl->style = impl->style | WindowStyle::AlwaysOnTop;
        } else {
            impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        }
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
    if (impl && impl->core_window) {
        impl->core_window.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? winrt::get_abi(impl->core_window) : nullptr;
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
        impl->mouse_device.get_position(x, y);
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
}

KeyMod Window::get_current_modifiers() const {
    if (!impl || !impl->core_window) return KeyMod::None;
    return get_uwp_modifiers(impl->core_window);
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
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11: return true;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_D3D11
    return Backend::D3D11;
#elif defined(WINDOW_HAS_D3D12)
    return Backend::D3D12;
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
    void* core_window = config.native_handle;

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_corewindow(core_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_corewindow(core_window, config.width, config.height, internal_config);
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

#endif // WINDOW_PLATFORM_UWP
