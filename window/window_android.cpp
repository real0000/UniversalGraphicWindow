/*
 * window_android.cpp - Android (NativeActivity) implementation
 * Backends: OpenGL ES (EGL), Vulkan
 */

#include "window.hpp"
#include "input/input_mouse.hpp"
#include "input/input_keyboard.hpp"

#if defined(WINDOW_PLATFORM_ANDROID)

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <string>
#include <cstring>
#include <cstdint>
#include <ctime>

#define LOG_TAG "WindowHpp"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

//=============================================================================
// Backend Configuration (use CMake-defined macros)
//=============================================================================

#ifdef WINDOW_SUPPORT_OPENGL
#define WINDOW_HAS_OPENGL 1
#endif

#ifdef WINDOW_SUPPORT_VULKAN
#define WINDOW_HAS_VULKAN 1
#endif

namespace window {

//=============================================================================
// Key Translation
//=============================================================================

static Key translate_android_keycode(int32_t keycode) {
    switch (keycode) {
        case AKEYCODE_A: return Key::A; case AKEYCODE_B: return Key::B;
        case AKEYCODE_C: return Key::C; case AKEYCODE_D: return Key::D;
        case AKEYCODE_E: return Key::E; case AKEYCODE_F: return Key::F;
        case AKEYCODE_G: return Key::G; case AKEYCODE_H: return Key::H;
        case AKEYCODE_I: return Key::I; case AKEYCODE_J: return Key::J;
        case AKEYCODE_K: return Key::K; case AKEYCODE_L: return Key::L;
        case AKEYCODE_M: return Key::M; case AKEYCODE_N: return Key::N;
        case AKEYCODE_O: return Key::O; case AKEYCODE_P: return Key::P;
        case AKEYCODE_Q: return Key::Q; case AKEYCODE_R: return Key::R;
        case AKEYCODE_S: return Key::S; case AKEYCODE_T: return Key::T;
        case AKEYCODE_U: return Key::U; case AKEYCODE_V: return Key::V;
        case AKEYCODE_W: return Key::W; case AKEYCODE_X: return Key::X;
        case AKEYCODE_Y: return Key::Y; case AKEYCODE_Z: return Key::Z;
        case AKEYCODE_0: return Key::Num0; case AKEYCODE_1: return Key::Num1;
        case AKEYCODE_2: return Key::Num2; case AKEYCODE_3: return Key::Num3;
        case AKEYCODE_4: return Key::Num4; case AKEYCODE_5: return Key::Num5;
        case AKEYCODE_6: return Key::Num6; case AKEYCODE_7: return Key::Num7;
        case AKEYCODE_8: return Key::Num8; case AKEYCODE_9: return Key::Num9;
        case AKEYCODE_ESCAPE: return Key::Escape;
        case AKEYCODE_TAB: return Key::Tab;
        case AKEYCODE_SPACE: return Key::Space;
        case AKEYCODE_ENTER: return Key::Enter;
        case AKEYCODE_DEL: return Key::Backspace;
        case AKEYCODE_FORWARD_DEL: return Key::Delete;
        case AKEYCODE_INSERT: return Key::Insert;
        case AKEYCODE_MOVE_HOME: return Key::Home;
        case AKEYCODE_MOVE_END: return Key::End;
        case AKEYCODE_PAGE_UP: return Key::PageUp;
        case AKEYCODE_PAGE_DOWN: return Key::PageDown;
        case AKEYCODE_DPAD_LEFT: return Key::Left;
        case AKEYCODE_DPAD_RIGHT: return Key::Right;
        case AKEYCODE_DPAD_UP: return Key::Up;
        case AKEYCODE_DPAD_DOWN: return Key::Down;
        case AKEYCODE_SHIFT_LEFT: return Key::LeftShift;
        case AKEYCODE_SHIFT_RIGHT: return Key::RightShift;
        case AKEYCODE_CTRL_LEFT: return Key::LeftControl;
        case AKEYCODE_CTRL_RIGHT: return Key::RightControl;
        case AKEYCODE_ALT_LEFT: return Key::LeftAlt;
        case AKEYCODE_ALT_RIGHT: return Key::RightAlt;
        case AKEYCODE_META_LEFT: return Key::LeftSuper;
        case AKEYCODE_META_RIGHT: return Key::RightSuper;
        case AKEYCODE_MENU: return Key::Menu;
        case AKEYCODE_BACK: return Key::Escape;  // Back button maps to Escape
        default: return Key::Unknown;
    }
}

static KeyMod get_android_modifiers(int32_t meta_state) {
    KeyMod mods = KeyMod::None;
    if (meta_state & AMETA_SHIFT_ON) mods = mods | KeyMod::Shift;
    if (meta_state & AMETA_CTRL_ON) mods = mods | KeyMod::Control;
    if (meta_state & AMETA_ALT_ON) mods = mods | KeyMod::Alt;
    if (meta_state & AMETA_META_ON) mods = mods | KeyMod::Super;
    if (meta_state & AMETA_CAPS_LOCK_ON) mods = mods | KeyMod::CapsLock;
    if (meta_state & AMETA_NUM_LOCK_ON) mods = mods | KeyMod::NumLock;
    return mods;
}

static double get_event_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
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
Graphics* create_opengl_graphics_android(void* native_window, int width, int height, const Config& config);
#endif

#ifdef WINDOW_HAS_VULKAN
Graphics* create_vulkan_graphics_android(void* native_window, int width, int height, const Config& config);
#endif

//=============================================================================
// Implementation Structure
//=============================================================================

struct Window::Impl {
    ANativeActivity* activity = nullptr;
    ANativeWindow* native_window = nullptr;
    ALooper* looper = nullptr;
    AInputQueue* input_queue = nullptr;
    Window* owner = nullptr;  // Back-pointer for event dispatch
    bool should_close_flag = false;
    bool visible = false;
    bool has_focus = false;
    int width = 0;
    int height = 0;
    std::string title;
    Graphics* gfx = nullptr;
    Config config;
    WindowStyle style = WindowStyle::Fullscreen; // Android NativeActivity is always fullscreen

    // Event callbacks
    EventCallbacks callbacks;

    // Input state
    float touch_x = 0;
    float touch_y = 0;

    // Mouse input handler system
    input::MouseEventDispatcher mouse_dispatcher;
    input::DefaultMouseDevice mouse_device;

    // Keyboard input handler system
    input::KeyboardEventDispatcher keyboard_dispatcher;
    input::DefaultKeyboardDevice keyboard_device;
};

// Global window instance for Android callbacks
static Window* g_android_window = nullptr;

//=============================================================================
// Graphics Initialization
//=============================================================================

static Graphics* create_graphics(ANativeWindow* native_window, int width, int height, const Config& config) {
    Graphics* gfx = nullptr;
    Backend requested = config.backend;
    if (requested == Backend::Auto) {
        requested = get_default_backend();
    }

    // Try requested backend first
    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_android(native_window, width, height, config);
            if (gfx) {
                LOGI("Created OpenGL ES graphics backend");
                return gfx;
            }
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_android(native_window, width, height, config);
            if (gfx) {
                LOGI("Created Vulkan graphics backend");
                return gfx;
            }
            break;
#endif
        default:
            break;
    }

    // Fallback to default if requested backend failed or not supported
    if (!gfx && config.backend != Backend::Auto) {
        Backend fallback = get_default_backend();
        switch (fallback) {
#ifdef WINDOW_HAS_OPENGL
            case Backend::OpenGL:
                gfx = create_opengl_graphics_android(native_window, width, height, config);
                if (gfx) {
                    LOGI("Created OpenGL ES graphics backend (fallback)");
                    return gfx;
                }
                break;
#endif
#ifdef WINDOW_HAS_VULKAN
            case Backend::Vulkan:
                gfx = create_vulkan_graphics_android(native_window, width, height, config);
                if (gfx) {
                    LOGI("Created Vulkan graphics backend (fallback)");
                    return gfx;
                }
                break;
#endif
            default:
                break;
        }
    }

    LOGE("Failed to create any graphics backend");
    return nullptr;
}

//=============================================================================
// Native Activity Callbacks
//=============================================================================

static void on_native_window_created(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowCreated");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->native_window = window;
        g_android_window->impl->width = ANativeWindow_getWidth(window);
        g_android_window->impl->height = ANativeWindow_getHeight(window);
        g_android_window->impl->visible = true;

        // Create graphics backend if not already created
        if (!g_android_window->impl->gfx) {
            g_android_window->impl->gfx = create_graphics(
                window,
                g_android_window->impl->width,
                g_android_window->impl->height,
                g_android_window->impl->config
            );
        }
    }
}

static void on_native_window_destroyed(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowDestroyed");
    if (g_android_window && g_android_window->impl) {
        // Destroy graphics backend
        delete g_android_window->impl->gfx;
        g_android_window->impl->gfx = nullptr;
        g_android_window->impl->native_window = nullptr;
        g_android_window->impl->visible = false;
    }
}

static void on_native_window_resized(ANativeActivity* activity, ANativeWindow* window) {
    LOGI("onNativeWindowResized");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->width = ANativeWindow_getWidth(window);
        g_android_window->impl->height = ANativeWindow_getHeight(window);
    }
}

static void on_window_focus_changed(ANativeActivity* activity, int has_focus) {
    LOGI("onWindowFocusChanged: %d", has_focus);
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->has_focus = (has_focus != 0);

        if (g_android_window->impl->callbacks.focus_callback) {
            WindowFocusEvent event;
            event.type = has_focus ? EventType::WindowFocus : EventType::WindowBlur;
            event.window = g_android_window->impl->owner;
            event.timestamp = get_event_timestamp();
            event.focused = has_focus != 0;
            g_android_window->impl->callbacks.focus_callback(event);
        }

        // Reset key states on focus loss
        if (!has_focus) {
            memset(g_android_window->impl->key_states, 0, sizeof(g_android_window->impl->key_states));
        }
    }
}

static void on_pause(ANativeActivity* activity) {
    LOGI("onPause");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->visible = false;
    }
}

static void on_resume(ANativeActivity* activity) {
    LOGI("onResume");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->visible = true;
    }
}

static void on_destroy(ANativeActivity* activity) {
    LOGI("onDestroy");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->should_close_flag = true;
    }
}

static void on_start(ANativeActivity* activity) {
    LOGI("onStart");
}

static void on_stop(ANativeActivity* activity) {
    LOGI("onStop");
}

static void on_configuration_changed(ANativeActivity* activity) {
    LOGI("onConfigurationChanged");
}

static void on_low_memory(ANativeActivity* activity) {
    LOGI("onLowMemory");
}

static void on_input_queue_created(ANativeActivity* activity, AInputQueue* queue) {
    LOGI("onInputQueueCreated");
    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->input_queue = queue;
        if (g_android_window->impl->looper) {
            AInputQueue_attachLooper(queue, g_android_window->impl->looper, 1, nullptr, nullptr);
        }
    }
}

static void on_input_queue_destroyed(ANativeActivity* activity, AInputQueue* queue) {
    LOGI("onInputQueueDestroyed");
    if (g_android_window && g_android_window->impl) {
        if (g_android_window->impl->input_queue == queue) {
            AInputQueue_detachLooper(queue);
            g_android_window->impl->input_queue = nullptr;
        }
    }
}

static void process_key_event(Window::Impl* impl, AInputEvent* event) {
    int32_t action = AKeyEvent_getAction(event);
    int32_t keycode = AKeyEvent_getKeyCode(event);
    int32_t meta_state = AKeyEvent_getMetaState(event);
    int32_t repeat_count = AKeyEvent_getRepeatCount(event);

    Key key = translate_android_keycode(keycode);
    bool pressed = (action == AKEY_EVENT_ACTION_DOWN);
    bool is_repeat = (repeat_count > 0);

    if (key != Key::Unknown && static_cast<int>(key) < 512) {
        impl->key_states[static_cast<int>(key)] = pressed;
    }

    if (impl->callbacks.key_callback) {
        KeyEvent key_event;
        if (action == AKEY_EVENT_ACTION_DOWN) {
            key_event.type = is_repeat ? EventType::KeyRepeat : EventType::KeyDown;
        } else {
            key_event.type = EventType::KeyUp;
        }
        key_event.window = impl->owner;
        key_event.timestamp = get_event_timestamp();
        key_event.key = key;
        key_event.modifiers = get_android_modifiers(meta_state);
        key_event.scancode = AKeyEvent_getScanCode(event);
        key_event.repeat = is_repeat;
        impl->callbacks.key_callback(key_event);
    }
}

static void process_motion_event(Window::Impl* impl, AInputEvent* event) {
    int32_t action = AMotionEvent_getAction(event);
    int32_t action_masked = action & AMOTION_EVENT_ACTION_MASK;
    int32_t pointer_index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    size_t pointer_count = AMotionEvent_getPointerCount(event);

    // Handle all pointers for touch events
    for (size_t i = 0; i < pointer_count; i++) {
        int32_t pointer_id = AMotionEvent_getPointerId(event, i);
        float x = AMotionEvent_getX(event, i);
        float y = AMotionEvent_getY(event, i);
        float pressure = AMotionEvent_getPressure(event, i);

        bool is_active_pointer = (i == pointer_index);

        EventType touch_type = EventType::None;

        if (is_active_pointer) {
            switch (action_masked) {
                case AMOTION_EVENT_ACTION_DOWN:
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    touch_type = EventType::TouchDown;
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_POINTER_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    touch_type = EventType::TouchUp;
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    touch_type = EventType::TouchMove;
                    break;
            }
        } else if (action_masked == AMOTION_EVENT_ACTION_MOVE) {
            touch_type = EventType::TouchMove;
        }

        if (touch_type != EventType::None && impl->callbacks.touch_callback) {
            TouchEvent touch_event;
            touch_event.type = touch_type;
            touch_event.window = impl->owner;
            touch_event.timestamp = get_event_timestamp();
            touch_event.touch_id = pointer_id;
            touch_event.x = x;
            touch_event.y = y;
            touch_event.pressure = pressure;
            impl->callbacks.touch_callback(touch_event);
        }

        // Store position of first touch for simulated mouse
        if (i == 0) {
            impl->touch_x = x;
            impl->touch_y = y;
        }
    }
}

//=============================================================================
// Native Activity Entry Point
//=============================================================================

extern "C" void ANativeActivity_onCreate(ANativeActivity* activity,
                                          void* savedState, size_t savedStateSize) {
    LOGI("ANativeActivity_onCreate");

    activity->callbacks->onNativeWindowCreated = on_native_window_created;
    activity->callbacks->onNativeWindowDestroyed = on_native_window_destroyed;
    activity->callbacks->onNativeWindowResized = on_native_window_resized;
    activity->callbacks->onWindowFocusChanged = on_window_focus_changed;
    activity->callbacks->onPause = on_pause;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onDestroy = on_destroy;
    activity->callbacks->onStart = on_start;
    activity->callbacks->onStop = on_stop;
    activity->callbacks->onConfigurationChanged = on_configuration_changed;
    activity->callbacks->onLowMemory = on_low_memory;
    activity->callbacks->onInputQueueCreated = on_input_queue_created;
    activity->callbacks->onInputQueueDestroyed = on_input_queue_destroyed;

    if (g_android_window && g_android_window->impl) {
        g_android_window->impl->activity = activity;
    }

    activity->instance = g_android_window;
}

//=============================================================================
// Window Implementation
//=============================================================================

Window* Window::create(const Config& config, Result* out_result) {
    auto set_result = [&](Result r) {
        if (out_result) *out_result = r;
    };

    Window* window = new Window();
    window->impl = new Window::Impl();
    window->impl->owner = window;  // Set back-pointer for event dispatch
    window->impl->width = config.width;
    window->impl->height = config.height;
    window->impl->title = config.title;
    window->impl->config = config;

    // Initialize mouse input system
    window->impl->mouse_device.set_dispatcher(&window->impl->mouse_dispatcher);
    window->impl->mouse_device.set_window(window);

    g_android_window = window;

    // Get looper for the current thread
    window->impl->looper = ALooper_forThread();
    if (!window->impl->looper) {
        window->impl->looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    }

    // Note: On Android, the actual window surface and graphics are created when
    // onNativeWindowCreated is called by the system

    set_result(Result::Success);
    return window;
}

void Window::destroy() {
    if (impl) {
        delete impl->gfx;
        impl->gfx = nullptr;

        if (impl->activity) {
            ANativeActivity_finish(impl->activity);
        }
        delete impl;
        impl = nullptr;
    }
    g_android_window = nullptr;
    delete this;
}

void Window::show() {
    // Android manages window visibility
    if (impl) impl->visible = true;
}

void Window::hide() {
    // Android manages window visibility
    if (impl) impl->visible = false;
}

bool Window::is_visible() const {
    return impl ? impl->visible : false;
}

void Window::set_title(const char* title) {
    // Would require JNI to set activity title
    if (impl) impl->title = title;
}

const char* Window::get_title() const {
    return impl ? impl->title.c_str() : "";
}

void Window::set_size(int width, int height) {
    // Android windows are managed by the system
    (void)width;
    (void)height;
}

void Window::get_size(int* width, int* height) const {
    if (impl) {
        if (impl->native_window) {
            if (width) *width = ANativeWindow_getWidth(impl->native_window);
            if (height) *height = ANativeWindow_getHeight(impl->native_window);
        } else {
            if (width) *width = impl->width;
            if (height) *height = impl->height;
        }
    }
}

int Window::get_width() const {
    if (impl && impl->native_window) {
        return ANativeWindow_getWidth(impl->native_window);
    }
    return impl ? impl->width : 0;
}

int Window::get_height() const {
    if (impl && impl->native_window) {
        return ANativeWindow_getHeight(impl->native_window);
    }
    return impl ? impl->height : 0;
}

bool Window::set_position(int x, int y) {
    // Android doesn't support window positioning
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
    // Android NativeActivity windows are always fullscreen, style changes not supported
    (void)style;
}

WindowStyle Window::get_style() const {
    return impl ? impl->style : WindowStyle::Fullscreen;
}

void Window::set_fullscreen(bool fullscreen) {
    // Android is always fullscreen
    (void)fullscreen;
}

bool Window::is_fullscreen() const {
    return true; // Android NativeActivity is always fullscreen
}

void Window::set_always_on_top(bool always_on_top) {
    // Not applicable on Android
    (void)always_on_top;
}

bool Window::is_always_on_top() const {
    return false; // Not applicable on Android
}

bool Window::should_close() const {
    return impl ? impl->should_close_flag : true;
}

void Window::set_should_close(bool close) {
    if (impl) impl->should_close_flag = close;
}

void Window::poll_events() {
    if (!impl) return;

    if (impl->looper) {
        int events;
        void* data;

        // Poll all pending events
        while (ALooper_pollAll(0, nullptr, &events, &data) >= 0) {
            // Events processed
        }
    }

    // Process input events
    if (impl->input_queue) {
        AInputEvent* event = nullptr;
        while (AInputQueue_getEvent(impl->input_queue, &event) >= 0) {
            if (AInputQueue_preDispatchEvent(impl->input_queue, event)) {
                continue;
            }

            int32_t handled = 0;
            int32_t event_type = AInputEvent_getType(event);

            if (event_type == AINPUT_EVENT_TYPE_KEY) {
                process_key_event(impl, event);
                handled = 1;
            } else if (event_type == AINPUT_EVENT_TYPE_MOTION) {
                process_motion_event(impl, event);
                handled = 1;
            }

            AInputQueue_finishEvent(impl->input_queue, event, handled);
        }
    }
}

Graphics* Window::graphics() const {
    return impl ? impl->gfx : nullptr;
}

void* Window::native_handle() const {
    return impl ? impl->native_window : nullptr;
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
        // Return mouse_device position, with fallback to touch position
        impl->mouse_device.get_position(x, y);
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
    return KeyMod::None;  // No modifier key state tracking on Android
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
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan: return true;
#endif
        default: return false;
    }
}

Backend get_default_backend() {
#ifdef WINDOW_HAS_OPENGL
    return Backend::OpenGL;
#elif defined(WINDOW_HAS_VULKAN)
    return Backend::Vulkan;
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
    ANativeWindow* native_window = static_cast<ANativeWindow*>(config.native_handle);

    switch (requested) {
#ifdef WINDOW_HAS_OPENGL
        case Backend::OpenGL:
            gfx = create_opengl_graphics_android(native_window, config.width, config.height, internal_config);
            break;
#endif
#ifdef WINDOW_HAS_VULKAN
        case Backend::Vulkan:
            gfx = create_vulkan_graphics_android(native_window, config.width, config.height, internal_config);
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

#endif // WINDOW_PLATFORM_ANDROID
