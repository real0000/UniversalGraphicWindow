/*
 * virtual_keyboard_win32.cpp - Windows Virtual Keyboard Implementation
 *
 * Uses the Windows Touch Keyboard (TabTip.exe) and IFrameworkInputPane API.
 * Requires Windows 8 or later for full functionality.
 */

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "virtual_keyboard.hpp"
#include "../../internal/utf8_util.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <initguid.h>
#include <cstring>
#include <cstdio>

// IFrameworkInputPane GUID (Windows 8+)
DEFINE_GUID(CLSID_FrameworkInputPane, 0xD5120AA3, 0x46BA, 0x44C5, 0x82, 0x2D, 0xCA, 0x80, 0x92, 0xC1, 0xFC, 0x72);
DEFINE_GUID(IID_IFrameworkInputPane, 0x5752238B, 0x24F0, 0x495A, 0x82, 0xF1, 0x2F, 0xD5, 0x93, 0x05, 0x67, 0x96);

// IFrameworkInputPaneHandler GUID
DEFINE_GUID(IID_IFrameworkInputPaneHandler, 0x226C537B, 0x1E76, 0x4D9E, 0xA7, 0x60, 0x33, 0xDB, 0x29, 0x92, 0x2F, 0x18);

// ITipInvocation GUID for Windows 10+ Touch Keyboard control
DEFINE_GUID(CLSID_UIHostNoLaunch, 0x4CE576FA, 0x83DC, 0x4F88, 0x95, 0x1C, 0x9D, 0x07, 0x82, 0xB4, 0xE3, 0x76);
DEFINE_GUID(IID_ITipInvocation, 0x37c994e7, 0x432b, 0x4834, 0xa2, 0xf7, 0xdc, 0xe1, 0xf1, 0x3b, 0x83, 0x4b);

// ITipInvocation interface declaration
MIDL_INTERFACE("37c994e7-432b-4834-a2f7-dce1f13b834b")
ITipInvocation : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Toggle(HWND wnd) = 0;
};

namespace vkeyboard {

// ============================================================================
// Windows Implementation
// ============================================================================

class VirtualKeyboardWin32 : public IVirtualKeyboard {
public:
    VirtualKeyboardWin32();
    ~VirtualKeyboardWin32();

    // IVirtualKeyboard implementation
    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    bool is_supported() const override;
    bool is_available() const override;

    Result show() override;
    Result show(const KeyboardConfig& config) override;
    Result hide() override;
    Result toggle() override;

    KeyboardState get_state() const override { return state_; }
    bool is_visible() const override;
    Rect get_frame() const override;
    float get_height() const override;

    void set_config(const KeyboardConfig& config) override { config_ = config; }
    KeyboardConfig get_config() const override { return config_; }

    void set_text_input_delegate(ITextInputDelegate* delegate) override { text_delegate_ = delegate; }
    ITextInputDelegate* get_text_input_delegate() const override { return text_delegate_; }
    void update_text_input_context(const TextInputContext& context) override { text_context_ = context; }

    void begin_text_input() override;
    void end_text_input() override;
    bool is_text_input_active() const override { return text_input_active_; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override { event_handler_ = handler; }

    Result get_available_layouts(KeyboardLayoutList* out_list) const override;
    Result get_current_layout(KeyboardLayoutInfo* out_info) const override;
    Result set_layout(const char* identifier) override;

    void* get_native_handle() const override { return target_hwnd_; }
    void set_target_window(void* hwnd) { target_hwnd_ = static_cast<HWND>(hwnd); }

    void update() override;

private:
    bool launch_touch_keyboard();
    bool close_touch_keyboard();
    HWND find_keyboard_window() const;
    bool is_keyboard_window_visible() const;
    Rect get_keyboard_window_rect() const;
    void update_keyboard_state();
    void notify_state_change(KeyboardState old_state, KeyboardState new_state);

    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;

    // Cached keyboard frame
    Rect cached_frame_;

    // IFrameworkInputPane for Windows 8+ keyboard tracking
    IFrameworkInputPane* input_pane_ = nullptr;

    // Touch keyboard process path
    wchar_t tabtip_path_[MAX_PATH] = {};

    // Target window for keyboard input
    HWND target_hwnd_ = nullptr;

    // Hidden EDIT control to keep Touch Keyboard visible
    HWND hidden_edit_ = nullptr;
    WNDPROC original_edit_proc_ = nullptr;
    static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool create_hidden_edit();
    void destroy_hidden_edit();
    void forward_char_to_target(wchar_t ch);
};

VirtualKeyboardWin32::VirtualKeyboardWin32() {
    // Build path to TabTip.exe
    wchar_t program_files[MAX_PATH];
    if (GetEnvironmentVariableW(L"CommonProgramFiles", program_files, MAX_PATH)) {
        swprintf_s(tabtip_path_, MAX_PATH, L"%s\\Microsoft Shared\\ink\\TabTip.exe", program_files);
    }
}

VirtualKeyboardWin32::~VirtualKeyboardWin32() {
    shutdown();
}

Result VirtualKeyboardWin32::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        return Result::ErrorUnknown;
    }

    // Try to create IFrameworkInputPane (Windows 8+)
    hr = CoCreateInstance(
        CLSID_FrameworkInputPane,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IFrameworkInputPane,
        reinterpret_cast<void**>(&input_pane_)
    );

    // It's OK if this fails - we can still use TabTip directly
    if (FAILED(hr)) {
        input_pane_ = nullptr;
    }

    initialized_ = true;
    update_keyboard_state();

    return Result::Success;
}

void VirtualKeyboardWin32::shutdown() {
    if (!initialized_) return;

    destroy_hidden_edit();

    if (input_pane_) {
        input_pane_->Release();
        input_pane_ = nullptr;
    }

    CoUninitialize();
    initialized_ = false;
}

// Global pointer for subclass procedure to access instance
static VirtualKeyboardWin32* g_vk_instance = nullptr;

bool VirtualKeyboardWin32::create_hidden_edit() {
    if (hidden_edit_) return true;  // Already created

    if (!target_hwnd_) {
        printf("[VK] No target window set, cannot create hidden edit\n");
        return false;
    }

    // Register a simple window class for our container
    static bool class_registered = false;
    static const wchar_t* class_name = L"VK_HiddenEditContainer";

    if (!class_registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = class_name;
        if (!RegisterClassExW(&wc)) {
            printf("[VK] Failed to register window class\n");
            return false;
        }
        class_registered = true;
    }

    // Create hidden edit as a child of target window
    // Position it off-screen but keep it focusable
    hidden_edit_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL,
        -100, -100, 10, 10,  // Off-screen position
        target_hwnd_,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );

    if (!hidden_edit_) {
        DWORD err = GetLastError();
        printf("[VK] Failed to create hidden edit: %lu\n", err);
        return false;
    }

    // Subclass the edit control to intercept input
    g_vk_instance = this;
    original_edit_proc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hidden_edit_, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(edit_subclass_proc))
    );

    printf("[VK] Hidden edit control created\n");
    return true;
}

void VirtualKeyboardWin32::destroy_hidden_edit() {
    if (hidden_edit_) {
        if (original_edit_proc_) {
            SetWindowLongPtrW(hidden_edit_, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(original_edit_proc_));
            original_edit_proc_ = nullptr;
        }
        DestroyWindow(hidden_edit_);
        hidden_edit_ = nullptr;
        g_vk_instance = nullptr;
        printf("[VK] Hidden edit control destroyed\n");
    }
}

LRESULT CALLBACK VirtualKeyboardWin32::edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    VirtualKeyboardWin32* self = g_vk_instance;
    if (!self || !self->original_edit_proc_) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
        case WM_CHAR: {
            // Directly handle character via text delegate (avoids duplicate)
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch >= 32) {
                self->forward_char_to_target(ch);
                // Clear edit and return - don't let edit process it
                SetWindowTextW(hwnd, L"");
                return 0;
            } else if (ch == L'\b') {
                // Backspace
                if (self->text_delegate_) {
                    self->text_delegate_->delete_backward(1);
                    printf("[VK] Backspace\n");
                }
                SetWindowTextW(hwnd, L"");
                return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            // Handle special keys that don't generate WM_CHAR
            WORD vk = LOWORD(wParam);
            if (vk == VK_DELETE && self->text_delegate_) {
                self->text_delegate_->delete_forward(1);
                printf("[VK] Delete\n");
                return 0;
            }
            break;
        }

        case WM_KILLFOCUS: {
            // When edit loses focus, the keyboard might hide
            break;
        }
    }

    return CallWindowProcW(self->original_edit_proc_, hwnd, msg, wParam, lParam);
}

void VirtualKeyboardWin32::forward_char_to_target(wchar_t ch) {
    // Directly call text delegate instead of posting message
    if (text_delegate_) {
        wchar_t wch[2] = { ch, L'\0' };
        std::string utf8 = internal::wide_to_utf8(wch);
        text_delegate_->insert_text(utf8.c_str());
        printf("[VK] Inserted char: U+%04X '%s'\n", static_cast<unsigned int>(ch), utf8.c_str());
    }
}

bool VirtualKeyboardWin32::is_supported() const {
    // Check if we're on Windows 8 or later (touch keyboard available)
    OSVERSIONINFOEXW osvi = { sizeof(osvi) };
    DWORDLONG condition_mask = 0;
    VER_SET_CONDITION(condition_mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(condition_mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    osvi.dwMajorVersion = 6;
    osvi.dwMinorVersion = 2;  // Windows 8

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION, condition_mask) != FALSE;
}

bool VirtualKeyboardWin32::is_available() const {
    // Check if TabTip.exe exists
    DWORD attrs = GetFileAttributesW(tabtip_path_);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

Result VirtualKeyboardWin32::show() {
    return show(config_);
}

Result VirtualKeyboardWin32::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;

    if (!is_available()) {
        return Result::ErrorNoKeyboardAvailable;
    }

    KeyboardState old_state = state_;

    if (launch_touch_keyboard()) {
        state_ = KeyboardState::Showing;
        notify_state_change(old_state, state_);
        return Result::Success;
    }

    return Result::ErrorUnknown;
}

Result VirtualKeyboardWin32::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    KeyboardState old_state = state_;

    if (close_touch_keyboard()) {
        state_ = KeyboardState::Hiding;
        notify_state_change(old_state, state_);
        return Result::Success;
    }

    return Result::ErrorUnknown;
}

Result VirtualKeyboardWin32::toggle() {
    if (is_visible()) {
        return hide();
    } else {
        return show();
    }
}

bool VirtualKeyboardWin32::is_visible() const {
    return is_keyboard_window_visible();
}

Rect VirtualKeyboardWin32::get_frame() const {
    return get_keyboard_window_rect();
}

float VirtualKeyboardWin32::get_height() const {
    Rect frame = get_frame();
    return frame.height;
}

void VirtualKeyboardWin32::begin_text_input() {
    text_input_active_ = true;

    // Focus hidden edit to receive keyboard input
    if (hidden_edit_) {
        ShowWindow(hidden_edit_, SW_SHOW);
        SetFocus(hidden_edit_);
    }
}

void VirtualKeyboardWin32::end_text_input() {
    text_input_active_ = false;

    // Return focus to target window
    if (target_hwnd_) {
        SetFocus(target_hwnd_);
    }
}

Result VirtualKeyboardWin32::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (!out_list) {
        return Result::ErrorInvalidParameter;
    }

    out_list->count = 0;

    // Get keyboard layouts
    HKL layouts[MAX_KEYBOARD_LAYOUTS];
    int count = GetKeyboardLayoutList(MAX_KEYBOARD_LAYOUTS, layouts);

    HKL current_layout = GetKeyboardLayout(0);

    for (int i = 0; i < count && i < MAX_KEYBOARD_LAYOUTS; ++i) {
        KeyboardLayoutInfo& info = out_list->layouts[out_list->count];

        // Get layout identifier
        LANGID lang_id = LOWORD(reinterpret_cast<uintptr_t>(layouts[i]));
        snprintf(info.identifier, sizeof(info.identifier), "%08X",
                 static_cast<unsigned int>(reinterpret_cast<uintptr_t>(layouts[i])));

        // Get language code
        char lang_code[16];
        GetLocaleInfoA(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SISO639LANGNAME, lang_code, sizeof(lang_code));
        strncpy_s(info.language_code, lang_code, MAX_LANGUAGE_CODE_LENGTH - 1);

        // Get display name
        wchar_t display_name[128];
        if (GetLocaleInfoW(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SLANGUAGE, display_name, 128)) {
            internal::wide_to_utf8(display_name, info.display_name, sizeof(info.display_name));
        }

        info.is_current = (layouts[i] == current_layout);
        out_list->count++;
    }

    return Result::Success;
}

Result VirtualKeyboardWin32::get_current_layout(KeyboardLayoutInfo* out_info) const {
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    HKL current_layout = GetKeyboardLayout(0);
    LANGID lang_id = LOWORD(reinterpret_cast<uintptr_t>(current_layout));

    snprintf(out_info->identifier, sizeof(out_info->identifier), "%08X",
             static_cast<unsigned int>(reinterpret_cast<uintptr_t>(current_layout)));

    char lang_code[16];
    GetLocaleInfoA(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SISO639LANGNAME, lang_code, sizeof(lang_code));
    strncpy_s(out_info->language_code, lang_code, MAX_LANGUAGE_CODE_LENGTH - 1);

    wchar_t display_name[128];
    if (GetLocaleInfoW(MAKELCID(lang_id, SORT_DEFAULT), LOCALE_SLANGUAGE, display_name, 128)) {
        internal::wide_to_utf8(display_name, out_info->display_name, sizeof(out_info->display_name));
    }

    out_info->is_current = true;

    return Result::Success;
}

Result VirtualKeyboardWin32::set_layout(const char* identifier) {
    if (!identifier) {
        return Result::ErrorInvalidParameter;
    }

    // Parse layout identifier (HKL as hex string)
    unsigned int hkl_value = 0;
    if (sscanf_s(identifier, "%X", &hkl_value) != 1) {
        return Result::ErrorInvalidParameter;
    }

    HKL hkl = reinterpret_cast<HKL>(static_cast<uintptr_t>(hkl_value));

    // Activate the layout
    if (ActivateKeyboardLayout(hkl, 0) == nullptr) {
        return Result::ErrorUnknown;
    }

    return Result::Success;
}

void VirtualKeyboardWin32::update() {
    if (!initialized_) return;

    KeyboardState old_state = state_;
    update_keyboard_state();

    if (state_ != old_state) {
        notify_state_change(old_state, state_);
    }

    // Update cached frame
    Rect new_frame = get_keyboard_window_rect();
    if (new_frame.width != cached_frame_.width || new_frame.height != cached_frame_.height ||
        new_frame.x != cached_frame_.x || new_frame.y != cached_frame_.y) {

        cached_frame_ = new_frame;

        if (event_handler_ && state_ == KeyboardState::Visible) {
            KeyboardEventData data;
            data.state = state_;
            data.frame = new_frame;
            event_handler_->on_keyboard_frame_changed(data);
        }
    }
}

bool VirtualKeyboardWin32::launch_touch_keyboard() {
    // Create hidden edit control to keep keyboard visible
    if (!create_hidden_edit()) {
        printf("[VK] Warning: Could not create hidden edit control\n");
    }

    // Method 1: Try ITipInvocation COM interface (Windows 10+)
    // This is the most reliable way to show the Touch Keyboard
    printf("[VK] Trying ITipInvocation COM interface...\n");
    ITipInvocation* tip_invocation = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_UIHostNoLaunch,
        nullptr,
        CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER,
        IID_ITipInvocation,
        reinterpret_cast<void**>(&tip_invocation)
    );

    if (SUCCEEDED(hr) && tip_invocation) {
        // Toggle the keyboard - pass the desktop window handle
        hr = tip_invocation->Toggle(GetDesktopWindow());
        tip_invocation->Release();

        if (SUCCEEDED(hr)) {
            printf("[VK] ITipInvocation::Toggle succeeded\n");

            // Wait a moment for keyboard to appear
            Sleep(200);

            // Focus the hidden edit to receive input
            if (hidden_edit_) {
                printf("[VK] Focusing hidden edit control\n");
                ShowWindow(hidden_edit_, SW_SHOW);
                SetFocus(hidden_edit_);
            }
            return true;
        }
        printf("[VK] ITipInvocation::Toggle failed: 0x%08lX\n", hr);
    } else {
        printf("[VK] ITipInvocation not available (hr=0x%08lX), trying TabTip.exe\n", hr);
    }

    // Method 2: Launch TabTip.exe directly (fallback)
    printf("[VK] Launching TabTip.exe: %ls\n", tabtip_path_);
    HINSTANCE result = ShellExecuteW(nullptr, L"open", tabtip_path_, nullptr, nullptr, SW_SHOWNORMAL);
    intptr_t code = reinterpret_cast<intptr_t>(result);
    printf("[VK] TabTip result: %lld\n", static_cast<long long>(code));

    if (code <= 32) {
        printf("[VK] TabTip failed, trying osk.exe\n");
        result = ShellExecuteW(nullptr, L"open", L"osk.exe", nullptr, nullptr, SW_SHOWNORMAL);
        code = reinterpret_cast<intptr_t>(result);
        if (code > 32) {
            Sleep(300);
            if (target_hwnd_) {
                SetForegroundWindow(target_hwnd_);
            }
            return true;
        }
        return false;
    }

    // Wait for TabTip window to appear
    HWND tabtip_hwnd = nullptr;
    for (int i = 0; i < 30; i++) {
        Sleep(100);
        tabtip_hwnd = FindWindowW(L"IPTip_Main_Window", nullptr);
        if (!tabtip_hwnd) {
            tabtip_hwnd = FindWindowW(L"IPTIP_Main_Window", nullptr);
        }
        if (tabtip_hwnd && IsWindowVisible(tabtip_hwnd)) {
            printf("[VK] TabTip window found and visible\n");
            break;
        }
    }

    // Focus the hidden edit to receive input
    if (hidden_edit_) {
        printf("[VK] Focusing hidden edit control\n");
        ShowWindow(hidden_edit_, SW_SHOW);
        SetFocus(hidden_edit_);
    }

    return tabtip_hwnd != nullptr || hidden_edit_ != nullptr;
}

bool VirtualKeyboardWin32::close_touch_keyboard() {
    bool closed = false;

    // Try to close osk.exe
    HWND osk = FindWindowW(L"OSKMainClass", nullptr);
    if (osk) {
        printf("[VK] Closing OSK window\n");
        PostMessage(osk, WM_CLOSE, 0, 0);
        closed = true;
    }

    // Try to close TabTip
    HWND hwnd = find_keyboard_window();
    if (hwnd && hwnd != osk) {
        printf("[VK] Closing TabTip window\n");
        PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        closed = true;
    }

    // Alternative: Try to find TabTip main window
    HWND tabtip_hwnd = FindWindowW(L"IPTip_Main_Window", nullptr);
    if (tabtip_hwnd && tabtip_hwnd != hwnd) {
        PostMessage(tabtip_hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        closed = true;
    }

    return closed;
}

HWND VirtualKeyboardWin32::find_keyboard_window() const {
    // Try osk.exe first (classic On-Screen Keyboard)
    HWND osk = FindWindowW(L"OSKMainClass", nullptr);
    if (osk) return osk;

    // Try different window class names used by touch keyboard
    const wchar_t* class_names[] = {
        L"IPTip_Main_Window",           // Windows 8/8.1
        L"IPTIP_Main_Window",           // Windows 10
        L"Windows.UI.Core.CoreWindow"   // Windows 10 touch keyboard
    };

    for (const auto& class_name : class_names) {
        HWND hwnd = FindWindowW(class_name, nullptr);
        if (hwnd) {
            return hwnd;
        }
    }

    return nullptr;
}

bool VirtualKeyboardWin32::is_keyboard_window_visible() const {
    HWND hwnd = find_keyboard_window();
    if (!hwnd) {
        return false;
    }

    // Check if window is visible and not minimized
    if (!IsWindowVisible(hwnd)) {
        return false;
    }

    // Additional check: window should have non-zero size
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        int width = rect.right - rect.left;
        int height = rect.bottom - rect.top;
        return width > 0 && height > 0;
    }

    return false;
}

Rect VirtualKeyboardWin32::get_keyboard_window_rect() const {
    Rect result;

    HWND hwnd = find_keyboard_window();
    if (hwnd) {
        RECT rect;
        if (GetWindowRect(hwnd, &rect)) {
            result.x = static_cast<float>(rect.left);
            result.y = static_cast<float>(rect.top);
            result.width = static_cast<float>(rect.right - rect.left);
            result.height = static_cast<float>(rect.bottom - rect.top);
        }
    }

    return result;
}

void VirtualKeyboardWin32::update_keyboard_state() {
    bool visible = is_keyboard_window_visible();

    switch (state_) {
        case KeyboardState::Hidden:
            if (visible) {
                state_ = KeyboardState::Visible;
            }
            break;

        case KeyboardState::Showing:
            if (visible) {
                state_ = KeyboardState::Visible;
            }
            break;

        case KeyboardState::Visible:
            if (!visible) {
                state_ = KeyboardState::Hidden;
            }
            break;

        case KeyboardState::Hiding:
            if (!visible) {
                state_ = KeyboardState::Hidden;
            }
            break;
    }
}

void VirtualKeyboardWin32::notify_state_change(KeyboardState old_state, KeyboardState new_state) {
    if (!event_handler_) return;

    KeyboardEventData data;
    data.state = new_state;
    data.previous_state = old_state;
    data.frame = get_frame();
    data.animation_duration = 0.25f;  // Approximate

    if (new_state == KeyboardState::Showing) {
        event_handler_->on_keyboard_will_show(data);
    } else if (new_state == KeyboardState::Visible && old_state == KeyboardState::Showing) {
        event_handler_->on_keyboard_did_show(data);
    } else if (new_state == KeyboardState::Hiding) {
        event_handler_->on_keyboard_will_hide(data);
    } else if (new_state == KeyboardState::Hidden && old_state == KeyboardState::Hiding) {
        event_handler_->on_keyboard_did_hide(data);
    }
}

// ============================================================================
// Factory Functions
// ============================================================================

IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardWin32();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

} // namespace vkeyboard

#endif // _WIN32
