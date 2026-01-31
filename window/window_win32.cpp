/*
 * window_win32.cpp - Win32 implementation
 * Backends: OpenGL, Vulkan, D3D11, D3D12
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <cstring>
#include <string>

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_D3D11
#define WINDOW_HAS_D3D11 1
#endif

#ifdef WINDOW_SUPPORT_D3D12
#define WINDOW_HAS_D3D12 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// External Graphics Creation Functions (from api_*.cpp)
//=============================================================================

#ifdef WINDOW_HAS_OPENGL
Graphics* create_opengl_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D11
Graphics* create_d3d11_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_D3D12
Graphics* create_d3d12_graphics_hwnd(void* hwnd, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_win32(void* hwnd, int width, int height, const Config& config);
#endif

//=============================================================================
// Key Translation
//=============================================================================

static Key translate_key(WPARAM vk, LPARAM lparam) {
    // Check for extended key flag
    bool extended = (lparam & (1 << 24)) != 0;
    UINT scancode = (lparam >> 16) & 0xFF;

    switch (vk) {
        // Letters
        case 'A': return Key::A; case 'B': return Key::B; case 'C': return Key::C;
        case 'D': return Key::D; case 'E': return Key::E; case 'F': return Key::F;
        case 'G': return Key::G; case 'H': return Key::H; case 'I': return Key::I;
        case 'J': return Key::J; case 'K': return Key::K; case 'L': return Key::L;
        case 'M': return Key::M; case 'N': return Key::N; case 'O': return Key::O;
        case 'P': return Key::P; case 'Q': return Key::Q; case 'R': return Key::R;
        case 'S': return Key::S; case 'T': return Key::T; case 'U': return Key::U;
        case 'V': return Key::V; case 'W': return Key::W; case 'X': return Key::X;
        case 'Y': return Key::Y; case 'Z': return Key::Z;

        // Numbers
        case '0': return Key::Num0; case '1': return Key::Num1; case '2': return Key::Num2;
        case '3': return Key::Num3; case '4': return Key::Num4; case '5': return Key::Num5;
        case '6': return Key::Num6; case '7': return Key::Num7; case '8': return Key::Num8;
        case '9': return Key::Num9;

        // Function keys
        case VK_F1: return Key::F1; case VK_F2: return Key::F2; case VK_F3: return Key::F3;
        case VK_F4: return Key::F4; case VK_F5: return Key::F5; case VK_F6: return Key::F6;
        case VK_F7: return Key::F7; case VK_F8: return Key::F8; case VK_F9: return Key::F9;
        case VK_F10: return Key::F10; case VK_F11: return Key::F11; case VK_F12: return Key::F12;
        case VK_F13: return Key::F13; case VK_F14: return Key::F14; case VK_F15: return Key::F15;
        case VK_F16: return Key::F16; case VK_F17: return Key::F17; case VK_F18: return Key::F18;
        case VK_F19: return Key::F19; case VK_F20: return Key::F20; case VK_F21: return Key::F21;
        case VK_F22: return Key::F22; case VK_F23: return Key::F23; case VK_F24: return Key::F24;

        // Navigation
        case VK_ESCAPE: return Key::Escape;
        case VK_TAB: return Key::Tab;
        case VK_CAPITAL: return Key::CapsLock;
        case VK_SPACE: return Key::Space;
        case VK_RETURN: return extended ? Key::NumpadEnter : Key::Enter;
        case VK_BACK: return Key::Backspace;
        case VK_DELETE: return Key::Delete;
        case VK_INSERT: return Key::Insert;
        case VK_HOME: return Key::Home;
        case VK_END: return Key::End;
        case VK_PRIOR: return Key::PageUp;
        case VK_NEXT: return Key::PageDown;
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;

        // Modifiers
        case VK_SHIFT:
            return (scancode == 0x36) ? Key::RightShift : Key::LeftShift;
        case VK_CONTROL:
            return extended ? Key::RightControl : Key::LeftControl;
        case VK_MENU:
            return extended ? Key::RightAlt : Key::LeftAlt;
        case VK_LWIN: return Key::LeftSuper;
        case VK_RWIN: return Key::RightSuper;

        // Punctuation
        case VK_OEM_3: return Key::Grave;
        case VK_OEM_MINUS: return Key::Minus;
        case VK_OEM_PLUS: return Key::Equal;
        case VK_OEM_4: return Key::LeftBracket;
        case VK_OEM_6: return Key::RightBracket;
        case VK_OEM_5: return Key::Backslash;
        case VK_OEM_1: return Key::Semicolon;
        case VK_OEM_7: return Key::Apostrophe;
        case VK_OEM_COMMA: return Key::Comma;
        case VK_OEM_PERIOD: return Key::Period;
        case VK_OEM_2: return Key::Slash;

        // Numpad
        case VK_NUMPAD0: return Key::Numpad0; case VK_NUMPAD1: return Key::Numpad1;
        case VK_NUMPAD2: return Key::Numpad2; case VK_NUMPAD3: return Key::Numpad3;
        case VK_NUMPAD4: return Key::Numpad4; case VK_NUMPAD5: return Key::Numpad5;
        case VK_NUMPAD6: return Key::Numpad6; case VK_NUMPAD7: return Key::Numpad7;
        case VK_NUMPAD8: return Key::Numpad8; case VK_NUMPAD9: return Key::Numpad9;
        case VK_DECIMAL: return Key::NumpadDecimal;
        case VK_ADD: return Key::NumpadAdd;
        case VK_SUBTRACT: return Key::NumpadSubtract;
        case VK_MULTIPLY: return Key::NumpadMultiply;
        case VK_DIVIDE: return Key::NumpadDivide;
        case VK_NUMLOCK: return Key::NumLock;

        // Other
        case VK_SNAPSHOT: return Key::PrintScreen;
        case VK_SCROLL: return Key::ScrollLock;
        case VK_PAUSE: return Key::Pause;
        case VK_APPS: return Key::Menu;

        default: return Key::Unknown;
    }
}

static KeyMod get_current_key_modifiers() {
    KeyMod mods = KeyMod::None;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods = mods | KeyMod::Shift;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods = mods | KeyMod::Control;
    if (GetKeyState(VK_MENU) & 0x8000) mods = mods | KeyMod::Alt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods = mods | KeyMod::Super;
    if (GetKeyState(VK_CAPITAL) & 0x0001) mods = mods | KeyMod::CapsLock;
    if (GetKeyState(VK_NUMLOCK) & 0x0001) mods = mods | KeyMod::NumLock;
    return mods;
}

static MouseButton translate_mouse_button(UINT msg, WPARAM wparam) {
    switch (msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            return MouseButton::Left;
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            return MouseButton::Right;
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            return MouseButton::Middle;
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            return (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;
        default:
            return MouseButton::Unknown;
    }
}

//=============================================================================
// Window Implementation
//=============================================================================

// Callback storage structure (using std::function)
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

struct Window::Impl {
    HWND hwnd = nullptr;
    Window* owner = nullptr;  // Back-pointer to Window for event dispatch
    bool should_close_flag = false;
    bool visible = false;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    std::string title;
    Graphics* gfx = nullptr;
    WindowStyle style = WindowStyle::Default;
    // For fullscreen toggle restoration
    RECT windowed_rect = {};
    DWORD windowed_style = 0;
    DWORD windowed_ex_style = 0;
    bool is_fullscreen = false;

    // Event callbacks
    EventCallbacks callbacks;

    // Mouse input system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;

    // Input state
    bool mouse_in_window = false;
    bool focused = true;
};

// Helper function to convert WindowStyle flags to Win32 style
static DWORD style_to_win32_style(WindowStyle style) {
    if (has_style(style, WindowStyle::Fullscreen)) {
        return WS_POPUP | WS_VISIBLE;
    }

    DWORD win_style = WS_CLIPSIBLINGS | WS_CLIPCHILDREN;

    if (has_style(style, WindowStyle::TitleBar)) {
        win_style |= WS_CAPTION | WS_SYSMENU;
    }

    if (has_style(style, WindowStyle::Border) && !has_style(style, WindowStyle::TitleBar)) {
        win_style |= WS_POPUP | WS_BORDER;
    } else if (!has_style(style, WindowStyle::TitleBar) && !has_style(style, WindowStyle::Border)) {
        win_style |= WS_POPUP;
    }

    if (has_style(style, WindowStyle::MinimizeButton)) {
        win_style |= WS_MINIMIZEBOX;
    }

    if (has_style(style, WindowStyle::MaximizeButton)) {
        win_style |= WS_MAXIMIZEBOX;
    }

    if (has_style(style, WindowStyle::Resizable)) {
        win_style |= WS_THICKFRAME;
    }

    return win_style;
}

static DWORD style_to_win32_ex_style(WindowStyle style) {
    DWORD ex_style = WS_EX_APPWINDOW;

    if (has_style(style, WindowStyle::AlwaysOnTop)) {
        ex_style |= WS_EX_TOPMOST;
    }

    if (has_style(style, WindowStyle::ToolWindow)) {
        ex_style = (ex_style & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
    }

    return ex_style;
}

static double get_event_timestamp() {
    static LARGE_INTEGER frequency = {};
    static bool initialized = false;
    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = true;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window::Impl* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
            if (impl) {
                impl->should_close_flag = true;
                if (impl->callbacks.close_callback) {
                    WindowCloseEvent event;
                    event.type = EventType::WindowClose;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    impl->callbacks.close_callback(event);
                }
            }
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (impl) {
                int new_width = LOWORD(lparam);
                int new_height = HIWORD(lparam);
                bool minimized = (wparam == SIZE_MINIMIZED);
                bool maximized = (wparam == SIZE_MAXIMIZED);

                impl->width = new_width;
                impl->height = new_height;

                if (impl->callbacks.resize_callback) {
                    WindowResizeEvent event;
                    event.type = EventType::WindowResize;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.width = new_width;
                    event.height = new_height;
                    event.minimized = minimized;
                    impl->callbacks.resize_callback(event);
                }

                if (impl->callbacks.state_callback && (wparam == SIZE_MINIMIZED || wparam == SIZE_MAXIMIZED || wparam == SIZE_RESTORED)) {
                    WindowStateEvent event;
                    event.type = wparam == SIZE_MINIMIZED ? EventType::WindowMinimize :
                                 wparam == SIZE_MAXIMIZED ? EventType::WindowMaximize : EventType::WindowRestore;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.minimized = minimized;
                    event.maximized = maximized;
                    impl->callbacks.state_callback(event);
                }
            }
            return 0;

        case WM_MOVE:
            if (impl) {
                impl->x = (int)(short)LOWORD(lparam);
                impl->y = (int)(short)HIWORD(lparam);

                if (impl->callbacks.move_callback) {
                    WindowMoveEvent event;
                    event.type = EventType::WindowMove;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.x = impl->x;
                    event.y = impl->y;
                    impl->callbacks.move_callback(event);
                }
            }
            return 0;

        case WM_SETFOCUS:
            if (impl) {
                impl->focused = true;
                if (impl->callbacks.focus_callback) {
                    WindowFocusEvent event;
                    event.type = EventType::WindowFocus;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.focused = true;
                    impl->callbacks.focus_callback(event);
                }
            }
            return 0;

        case WM_KILLFOCUS:
            if (impl) {
                impl->focused = false;
                // Reset input state on focus loss to avoid stuck keys
                impl->keyboard_device.reset();
                impl->mouse_device.reset();

                if (impl->callbacks.focus_callback) {
                    WindowFocusEvent event;
                    event.type = EventType::WindowBlur;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.focused = false;
                    impl->callbacks.focus_callback(event);
                }
            }
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (impl) {
                Key key = translate_key(wparam, lparam);
                bool repeat = (lparam & 0x40000000) != 0;
                int scancode = (lparam >> 16) & 0xFF;

                impl->keyboard_device.inject_key_down(key, get_current_key_modifiers(), scancode, repeat, get_event_timestamp());
            }
            // Don't return 0 for SYSKEYDOWN - allow Alt+F4 etc.
            if (msg == WM_KEYDOWN) return 0;
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (impl) {
                Key key = translate_key(wparam, lparam);
                int scancode = (lparam >> 16) & 0xFF;

                impl->keyboard_device.inject_key_up(key, get_current_key_modifiers(), scancode, get_event_timestamp());
            }
            if (msg == WM_KEYUP) return 0;
            break;

        case WM_CHAR:
        case WM_SYSCHAR:
            if (impl) {
                // Filter control characters except for common ones
                if (wparam >= 32 || wparam == '\t' || wparam == '\n' || wparam == '\r') {
                    impl->keyboard_device.inject_char(static_cast<uint32_t>(wparam), get_current_key_modifiers(), get_event_timestamp());
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (impl) {
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);

                // Track mouse for WM_MOUSELEAVE
                if (!impl->mouse_in_window) {
                    impl->mouse_in_window = true;
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = hwnd;
                    TrackMouseEvent(&tme);
                }

                // Inject into mouse device (dispatches to handlers and legacy callback)
                impl->mouse_device.inject_move(x, y, get_current_key_modifiers(), get_event_timestamp());
            }
            return 0;

        case WM_MOUSELEAVE:
            if (impl) {
                impl->mouse_in_window = false;
            }
            return 0;

        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_XBUTTONDBLCLK:
            if (impl) {
                MouseButton button = translate_mouse_button(msg, wparam);
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);
                bool dblclick = (msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK ||
                                 msg == WM_MBUTTONDBLCLK || msg == WM_XBUTTONDBLCLK);

                SetCapture(hwnd);  // Capture mouse for drag operations

                // Inject into mouse device (dispatches to handlers and legacy callback)
                impl->mouse_device.inject_button_down(button, x, y, dblclick ? 2 : 1,
                                                       get_current_key_modifiers(), get_event_timestamp());
            }
            return 0;

        case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:
            if (impl) {
                MouseButton button = translate_mouse_button(msg, wparam);
                int x = GET_X_LPARAM(lparam);
                int y = GET_Y_LPARAM(lparam);

                ReleaseCapture();

                // Inject into mouse device (dispatches to handlers and legacy callback)
                impl->mouse_device.inject_button_up(button, x, y,
                                                     get_current_key_modifiers(), get_event_timestamp());
            }
            return 0;

        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            if (impl) {
                int delta = GET_WHEEL_DELTA_WPARAM(wparam);
                POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                ScreenToClient(hwnd, &pt);

                float dx = (msg == WM_MOUSEHWHEEL) ? static_cast<float>(delta) / WHEEL_DELTA : 0.0f;
                float dy = (msg == WM_MOUSEWHEEL) ? static_cast<float>(delta) / WHEEL_DELTA : 0.0f;

                // Inject into mouse device (dispatches to handlers and legacy callback)
                impl->mouse_device.inject_wheel(dx, dy, pt.x, pt.y,
                                                 get_current_key_modifiers(), get_event_timestamp());
            }
            return 0;

        case WM_DPICHANGED:
            if (impl && impl->callbacks.dpi_change_callback) {
                int dpi = HIWORD(wparam);
                DpiChangeEvent event;
                event.type = EventType::DpiChange;
                event.window = impl->owner;
                event.timestamp = get_event_timestamp();
                event.dpi = dpi;
                event.scale = static_cast<float>(dpi) / 96.0f;
                impl->callbacks.dpi_change_callback(event);

                // Optionally resize window to suggested rect
                RECT* suggested = reinterpret_cast<RECT*>(lparam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;

        case WM_DROPFILES:
            if (impl && impl->callbacks.drop_file_callback) {
                HDROP hdrop = reinterpret_cast<HDROP>(wparam);
                UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);

                if (count > 0) {
                    char** paths = new char*[count];
                    for (UINT i = 0; i < count; i++) {
                        UINT len = DragQueryFileW(hdrop, i, nullptr, 0) + 1;
                        wchar_t* wpath = new wchar_t[len];
                        DragQueryFileW(hdrop, i, wpath, len);

                        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                        paths[i] = new char[utf8_len];
                        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, paths[i], utf8_len, nullptr, nullptr);
                        delete[] wpath;
                    }

                    DropFileEvent event;
                    event.type = EventType::DropFile;
                    event.window = impl->owner;
                    event.timestamp = get_event_timestamp();
                    event.paths = paths;
                    event.count = static_cast<int>(count);
                    impl->callbacks.drop_file_callback(event);

                    for (UINT i = 0; i < count; i++) {
                        delete[] paths[i];
                    }
                    delete[] paths;
                }

                DragFinish(hdrop);
            }
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    static bool class_registered = false;
    static const wchar_t* CLASS_NAME = L"WindowHppClass";

    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = CLASS_NAME;
        if (!RegisterClassExW(&wc)) {
            set_result(Result::ErrorPlatformInit);
            return nullptr;
        }
        class_registered = true;
    }

    // Get window settings from first window entry
    const WindowConfigEntry& win_cfg = config.windows[0];

    // Determine effective style
    WindowStyle effective_style = win_cfg.style;
    if (win_cfg.fullscreen) {
        effective_style = effective_style | WindowStyle::Fullscreen;
    }

    DWORD style = style_to_win32_style(effective_style);
    DWORD ex_style = style_to_win32_ex_style(effective_style);

    int win_width, win_height;
    int pos_x, pos_y;

    if (has_style(effective_style, WindowStyle::Fullscreen)) {
        // Fullscreen: use entire screen
        pos_x = 0;
        pos_y = 0;
        win_width = GetSystemMetrics(SM_CXSCREEN);
        win_height = GetSystemMetrics(SM_CYSCREEN);
    } else {
        RECT rect = { 0, 0, win_cfg.width, win_cfg.height };
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
        win_width = rect.right - rect.left;
        win_height = rect.bottom - rect.top;
        pos_x = win_cfg.x >= 0 ? win_cfg.x : CW_USEDEFAULT;
        pos_y = win_cfg.y >= 0 ? win_cfg.y : CW_USEDEFAULT;
    }

    int title_len = MultiByteToWideChar(CP_UTF8, 0, win_cfg.title, -1, nullptr, 0);
    wchar_t* title_wide = new wchar_t[title_len];
    MultiByteToWideChar(CP_UTF8, 0, win_cfg.title, -1, title_wide, title_len);

    HWND hwnd = CreateWindowExW(ex_style, CLASS_NAME, title_wide, style, pos_x, pos_y, win_width, win_height,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    delete[] title_wide;

    if (!hwnd) {
        set_result(Result::ErrorWindowCreation);
        return nullptr;
    }

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->hwnd = hwnd;
    window->impl->owner = window;  // Set back-pointer for event dispatch
    window->impl->width = win_cfg.width;
    window->impl->height = win_cfg.height;
    window->impl->title = win_cfg.title;
    window->impl->style = effective_style;
    window->impl->is_fullscreen = has_style(effective_style, WindowStyle::Fullscreen);

    // Enable drag-drop
    DragAcceptFiles(hwnd, TRUE);

    RECT win_rect;
    GetWindowRect(hwnd, &win_rect);
    window->impl->x = win_rect.left;
    window->impl->y = win_rect.top;

    // Save windowed state for fullscreen toggle
    if (!window->impl->is_fullscreen) {
        window->impl->windowed_rect = win_rect;
        window->impl->windowed_style = style;
        window->impl->windowed_ex_style = ex_style;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window->impl));

    // Create graphics backend based on config.backend
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_hwnd(hwnd, config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_win32(hwnd, win_cfg.width, win_cfg.height, config);
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
                gfx = create_d3d11_graphics_hwnd(hwnd, config);
                break;
#endif
#ifdef WINDOW_HAS_D3D12
            case Backend::D3D12:
                gfx = create_d3d12_graphics_hwnd(hwnd, config);
                break;
#endif
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_hwnd(hwnd, config);
                break;
#endif
            default:
                break;
        }
    }

    if (!gfx) {
        DestroyWindow(hwnd);
        delete window->impl;
        delete window;
        set_result(Result::ErrorGraphicsInit);
        return nullptr;
    }

    window->impl->gfx = gfx;

    // Initialize mouse input system
    window->impl->mouse_device.set_window(window);
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);

    // Initialize keyboard input system
    window->impl->keyboard_device.set_window(window);
    window->impl->keyboard_device.set_dispatcher(&window->impl->keyboard_dispatcher);

    if (win_cfg.visible) {
        ShowWindow(hwnd, SW_SHOW);
        window->impl->visible = true;
    }

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        if (impl->hwnd) DestroyWindow(impl->hwnd);
        delete impl;
        impl = nullptr;
    }
    delete this;
}

void Window::show() { if (impl && impl->hwnd) { ShowWindow(impl->hwnd, SW_SHOW); impl->visible = true; } }
void Window::hide() { if (impl && impl->hwnd) { ShowWindow(impl->hwnd, SW_HIDE); impl->visible = false; } }
bool Window::is_visible() const { return impl ? impl->visible : false; }

void Window::set_title(const char* title) {
    if (impl && impl->hwnd) {
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
        wchar_t* wide = new wchar_t[len];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, len);
        SetWindowTextW(impl->hwnd, wide);
        delete[] wide;
        impl->title = title;
    }
}

const char* Window::get_title() const { return impl ? impl->title.c_str() : ""; }

void Window::set_size(int width, int height) {
    if (impl && impl->hwnd) {
        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_STYLE));
        RECT rect = { 0, 0, width, height };
        AdjustWindowRect(&rect, style, FALSE);
        SetWindowPos(impl->hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    }
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (width) *width = impl->width;
        if (height) *height = impl->height;
    }
}

int Window::get_width() const { return impl ? impl->width : 0; }
int Window::get_height() const { return impl ? impl->height : 0; }

bool Window::set_position(int x, int y) {
    if (impl && impl->hwnd) {
        SetWindowPos(impl->hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
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

bool Window::supports_position() const { return true; }

void Window::set_style(WindowStyle style) {
    if (!impl || !impl->hwnd) return;

    impl->style = style;

    // Handle fullscreen separately
    if (has_style(style, WindowStyle::Fullscreen) && !impl->is_fullscreen) {
        set_fullscreen(true);
        return;
    } else if (!has_style(style, WindowStyle::Fullscreen) && impl->is_fullscreen) {
        set_fullscreen(false);
    }

    DWORD win_style = style_to_win32_style(style);
    DWORD ex_style = style_to_win32_ex_style(style);

    SetWindowLongPtrW(impl->hwnd, GWL_STYLE, win_style);
    SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, ex_style);

    // Recalculate window size to maintain client area
    RECT rect = { 0, 0, impl->width, impl->height };
    AdjustWindowRectEx(&rect, win_style, FALSE, ex_style);

    SetWindowPos(impl->hwnd, has_style(style, WindowStyle::AlwaysOnTop) ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_FRAMECHANGED);
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Default;
}

void Window::set_fullscreen(bool fullscreen) {
    if (!impl || !impl->hwnd) return;
    if (impl->is_fullscreen == fullscreen) return;

    if (fullscreen) {
        // Save current windowed state
        impl->windowed_style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_STYLE));
        impl->windowed_ex_style = static_cast<DWORD>(GetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE));
        GetWindowRect(impl->hwnd, &impl->windowed_rect);

        // Switch to fullscreen
        DWORD style = WS_POPUP | WS_VISIBLE;
        SetWindowLongPtrW(impl->hwnd, GWL_STYLE, style);
        SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

        int screen_width = GetSystemMetrics(SM_CXSCREEN);
        int screen_height = GetSystemMetrics(SM_CYSCREEN);

        SetWindowPos(impl->hwnd, HWND_TOP, 0, 0, screen_width, screen_height,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        impl->is_fullscreen = true;
        impl->style = impl->style | WindowStyle::Fullscreen;
    } else {
        // Restore windowed state
        SetWindowLongPtrW(impl->hwnd, GWL_STYLE, impl->windowed_style);
        SetWindowLongPtrW(impl->hwnd, GWL_EXSTYLE, impl->windowed_ex_style);

        int x = impl->windowed_rect.left;
        int y = impl->windowed_rect.top;
        int w = impl->windowed_rect.right - impl->windowed_rect.left;
        int h = impl->windowed_rect.bottom - impl->windowed_rect.top;

        SetWindowPos(impl->hwnd, HWND_NOTOPMOST, x, y, w, h,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);

        impl->is_fullscreen = false;
        impl->style = impl->style & ~WindowStyle::Fullscreen;
    }
}

bool Window::is_fullscreen() const {
    return impl ? impl->is_fullscreen : false;
}

void Window::set_always_on_top(bool always_on_top) {
    if (!impl || !impl->hwnd) return;

    if (always_on_top) {
        impl->style = impl->style | WindowStyle::AlwaysOnTop;
        SetWindowPos(impl->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else {
        impl->style = impl->style & ~WindowStyle::AlwaysOnTop;
        SetWindowPos(impl->hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

bool Window::is_always_on_top() const {
    return impl ? has_style(impl->style, WindowStyle::AlwaysOnTop) : false;
}

bool Window::should_close() const { return impl ? impl->should_close_flag : true; }
void Window::set_should_close(bool close) { if (impl) impl->should_close_flag = close; }

void Window::poll_events() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

Graphics* Window::graphics() const { return impl ? impl->gfx : nullptr; }
void* Window::native_handle() const { return impl ? impl->hwnd : nullptr; }
void* Window::native_display() const { return nullptr; }

//=============================================================================
// Event Callback Setters
//=============================================================================

void Window::set_close_callback(WindowCloseCallback callback) {
    if (impl) {
        impl->callbacks.close_callback = callback;
    }
}

void Window::set_resize_callback(WindowResizeCallback callback) {
    if (impl) {
        impl->callbacks.resize_callback = callback;
    }
}

void Window::set_move_callback(WindowMoveCallback callback) {
    if (impl) {
        impl->callbacks.move_callback = callback;
    }
}

void Window::set_focus_callback(WindowFocusCallback callback) {
    if (impl) {
        impl->callbacks.focus_callback = callback;
    }
}

void Window::set_state_callback(WindowStateCallback callback) {
    if (impl) {
        impl->callbacks.state_callback = callback;
    }
}

void Window::set_touch_callback(TouchCallback callback) {
    if (impl) {
        impl->callbacks.touch_callback = callback;
    }
}

void Window::set_dpi_change_callback(DpiChangeCallback callback) {
    if (impl) {
        impl->callbacks.dpi_change_callback = callback;
    }
}

void Window::set_drop_file_callback(DropFileCallback callback) {
    if (impl) {
        impl->callbacks.drop_file_callback = callback;
    }
}

//=============================================================================
// Input State Queries
//=============================================================================

bool Window::is_key_down(Key key) const {
    if (!impl || key == Key::Unknown) return false;
    return impl->keyboard_device.is_key_down(key);
}

bool Window::is_mouse_button_down(MouseButton button) const {
    if (!impl || button == MouseButton::Unknown) return false;
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
    return get_current_key_modifiers();
}

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
    if (!impl) return nullptr;
    return &impl->mouse_dispatcher;
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
    if (!impl) return nullptr;
    return &impl->keyboard_dispatcher;
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
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11: return true;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12: return true;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_D3D11
    return Backend::D3D11;
#elif defined(WINDOW_HAS_OPENGL)
    return Backend::OpenGL;
#elif defined(WINDOW_HAS_D3D12)
    return Backend::D3D12;
#else
    return Backend::Auto;
#endif
}

// key_to_string, mouse_button_to_string, event_type_to_string
// are implemented in input/input_keyboard.cpp

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
    HWND hwnd = static_cast<HWND>(config.native_handle);

    switch (requested) {
#ifdef WINDOW_HAS_D3D11
        case Backend::D3D11:
            gfx = create_d3d11_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_D3D12
        case Backend::D3D12:
            gfx = create_d3d12_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_hwnd(hwnd, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_win32(hwnd, config.width, config.height, internal_config);
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

//=============================================================================
// Window::create_from_config
//=============================================================================

Window* Window::create_from_config(const char* config_filepath, Result* out_result) {
    auto set_result = [&](Result r) { if (out_result) *out_result = r; };

    GraphicsConfig gfx_config;

    // Try to load config file
    if (config_filepath && GraphicsConfig::load(config_filepath, &gfx_config)) {
        // Config loaded and validated
    } else {
        // Use defaults
        gfx_config = GraphicsConfig{};
    }

    // Create window from config
    return Window::create(gfx_config, out_result);
}

} // namespace window

#endif // WINDOW_PLATFORM_WIN32
