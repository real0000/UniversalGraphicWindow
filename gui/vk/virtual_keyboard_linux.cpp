/*
 * virtual_keyboard_linux.cpp - Linux Virtual Keyboard Implementation
 *
 * Limited support for virtual keyboards on Linux desktop.
 * - Wayland: Uses zwp_text_input_v3 protocol (when available)
 * - X11: Launches external virtual keyboard applications (onboard, florence, etc.)
 * - IBus/Fcitx: Input method framework integration
 */

#if defined(__linux__) && !defined(__ANDROID__)

#include "virtual_keyboard.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>

namespace vkeyboard {

// Known virtual keyboard applications on Linux
static const char* VIRTUAL_KEYBOARD_APPS[] = {
    "onboard",              // GNOME on-screen keyboard
    "florence",             // GTK virtual keyboard
    "squeekboard",          // Phosh/mobile Linux
    "maliit-keyboard",      // Qt-based virtual keyboard
    "matchbox-keyboard",    // Lightweight keyboard
    "xvkbd",                // X virtual keyboard
    "kvkbd",                // KDE virtual keyboard
    nullptr
};

class VirtualKeyboardLinux : public IVirtualKeyboard {
public:
    VirtualKeyboardLinux();
    ~VirtualKeyboardLinux();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    bool is_supported() const override { return true; }
    bool is_available() const override { return available_keyboard_ != nullptr; }

    Result show() override;
    Result show(const KeyboardConfig& config) override;
    Result hide() override;
    Result toggle() override;

    KeyboardState get_state() const override { return state_; }
    bool is_visible() const override { return state_ == KeyboardState::Visible; }
    Rect get_frame() const override { return frame_; }
    float get_height() const override { return frame_.height; }

    void set_config(const KeyboardConfig& config) override { config_ = config; }
    KeyboardConfig get_config() const override { return config_; }

    void set_text_input_delegate(ITextInputDelegate* delegate) override { text_delegate_ = delegate; }
    ITextInputDelegate* get_text_input_delegate() const override { return text_delegate_; }
    void update_text_input_context(const TextInputContext& context) override { text_context_ = context; }

    void begin_text_input() override { text_input_active_ = true; }
    void end_text_input() override { text_input_active_ = false; }
    bool is_text_input_active() const override { return text_input_active_; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override { event_handler_ = handler; }

    Result get_available_layouts(KeyboardLayoutList* out_list) const override;
    Result get_current_layout(KeyboardLayoutInfo* out_info) const override;
    Result set_layout(const char* identifier) override;

    void* get_native_handle() const override { return nullptr; }

    void update() override;

private:
    bool find_available_keyboard();
    bool is_command_available(const char* command) const;
    bool launch_keyboard(const char* command);
    bool kill_keyboard();
    bool is_keyboard_process_running() const;
    void detect_session_type();

    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    Rect frame_;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;

    const char* available_keyboard_ = nullptr;
    pid_t keyboard_pid_ = 0;
    bool is_wayland_ = false;
    bool is_x11_ = false;
};

VirtualKeyboardLinux::VirtualKeyboardLinux() {}

VirtualKeyboardLinux::~VirtualKeyboardLinux() {
    shutdown();
}

Result VirtualKeyboardLinux::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    detect_session_type();
    find_available_keyboard();

    initialized_ = true;
    return Result::Success;
}

void VirtualKeyboardLinux::shutdown() {
    if (!initialized_) return;

    // Kill any launched keyboard process
    if (keyboard_pid_ > 0) {
        kill_keyboard();
    }

    initialized_ = false;
}

void VirtualKeyboardLinux::detect_session_type() {
    // Check XDG_SESSION_TYPE
    const char* session_type = getenv("XDG_SESSION_TYPE");
    if (session_type) {
        is_wayland_ = (strcmp(session_type, "wayland") == 0);
        is_x11_ = (strcmp(session_type, "x11") == 0);
    }

    // Fallback: check WAYLAND_DISPLAY and DISPLAY
    if (!is_wayland_ && !is_x11_) {
        is_wayland_ = (getenv("WAYLAND_DISPLAY") != nullptr);
        is_x11_ = (getenv("DISPLAY") != nullptr);
    }
}

bool VirtualKeyboardLinux::find_available_keyboard() {
    available_keyboard_ = nullptr;

    // Check for specific keyboards based on desktop environment
    const char* desktop = getenv("XDG_CURRENT_DESKTOP");

    // GNOME prefers onboard
    if (desktop && strstr(desktop, "GNOME")) {
        if (is_command_available("onboard")) {
            available_keyboard_ = "onboard";
            return true;
        }
    }

    // KDE Plasma prefers kvkbd
    if (desktop && (strstr(desktop, "KDE") || strstr(desktop, "Plasma"))) {
        if (is_command_available("kvkbd")) {
            available_keyboard_ = "kvkbd";
            return true;
        }
    }

    // Mobile Linux (Phosh) uses squeekboard
    if (desktop && strstr(desktop, "Phosh")) {
        if (is_command_available("squeekboard")) {
            available_keyboard_ = "squeekboard";
            return true;
        }
    }

    // Check other keyboards in order of preference
    for (int i = 0; VIRTUAL_KEYBOARD_APPS[i] != nullptr; ++i) {
        if (is_command_available(VIRTUAL_KEYBOARD_APPS[i])) {
            available_keyboard_ = VIRTUAL_KEYBOARD_APPS[i];
            return true;
        }
    }

    return false;
}

bool VirtualKeyboardLinux::is_command_available(const char* command) const {
    char path[1024];
    snprintf(path, sizeof(path), "which %s > /dev/null 2>&1", command);
    return (system(path) == 0);
}

Result VirtualKeyboardLinux::show() {
    return show(config_);
}

Result VirtualKeyboardLinux::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;

    if (!available_keyboard_) {
        return Result::ErrorNoKeyboardAvailable;
    }

    // If already visible, return success
    if (state_ == KeyboardState::Visible && is_keyboard_process_running()) {
        return Result::Success;
    }

    KeyboardState old_state = state_;
    state_ = KeyboardState::Showing;

    if (event_handler_) {
        KeyboardEventData data;
        data.state = KeyboardState::Showing;
        data.previous_state = old_state;
        event_handler_->on_keyboard_will_show(data);
    }

    if (launch_keyboard(available_keyboard_)) {
        state_ = KeyboardState::Visible;

        if (event_handler_) {
            KeyboardEventData data;
            data.state = KeyboardState::Visible;
            data.previous_state = KeyboardState::Showing;
            event_handler_->on_keyboard_did_show(data);
        }

        return Result::Success;
    }

    state_ = KeyboardState::Hidden;
    return Result::ErrorUnknown;
}

Result VirtualKeyboardLinux::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    if (state_ == KeyboardState::Hidden) {
        return Result::Success;
    }

    KeyboardState old_state = state_;
    state_ = KeyboardState::Hiding;

    if (event_handler_) {
        KeyboardEventData data;
        data.state = KeyboardState::Hiding;
        data.previous_state = old_state;
        event_handler_->on_keyboard_will_hide(data);
    }

    bool success = kill_keyboard();

    state_ = KeyboardState::Hidden;

    if (event_handler_) {
        KeyboardEventData data;
        data.state = KeyboardState::Hidden;
        data.previous_state = KeyboardState::Hiding;
        event_handler_->on_keyboard_did_hide(data);
    }

    return success ? Result::Success : Result::ErrorUnknown;
}

Result VirtualKeyboardLinux::toggle() {
    if (state_ == KeyboardState::Visible || state_ == KeyboardState::Showing) {
        return hide();
    } else {
        return show();
    }
}

void VirtualKeyboardLinux::update() {
    if (!initialized_) return;

    // Check if the keyboard process is still running
    if (keyboard_pid_ > 0) {
        int status;
        pid_t result = waitpid(keyboard_pid_, &status, WNOHANG);

        if (result == keyboard_pid_) {
            // Process has exited
            keyboard_pid_ = 0;

            if (state_ == KeyboardState::Visible) {
                KeyboardState old_state = state_;
                state_ = KeyboardState::Hidden;

                if (event_handler_) {
                    KeyboardEventData data;
                    data.state = KeyboardState::Hidden;
                    data.previous_state = old_state;
                    event_handler_->on_keyboard_did_hide(data);
                }
            }
        }
    }
}

bool VirtualKeyboardLinux::launch_keyboard(const char* command) {
    pid_t pid = fork();

    if (pid == 0) {
        // Child process
        // Redirect stdout/stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        // Execute the keyboard
        execlp(command, command, nullptr);

        // If exec fails, exit
        _exit(1);
    } else if (pid > 0) {
        // Parent process
        keyboard_pid_ = pid;
        return true;
    }

    return false;
}

bool VirtualKeyboardLinux::kill_keyboard() {
    if (keyboard_pid_ <= 0) {
        return true;
    }

    // Try graceful termination first
    if (kill(keyboard_pid_, SIGTERM) == 0) {
        // Wait a bit for graceful shutdown
        usleep(100000); // 100ms

        int status;
        pid_t result = waitpid(keyboard_pid_, &status, WNOHANG);

        if (result == keyboard_pid_) {
            keyboard_pid_ = 0;
            return true;
        }

        // Force kill if still running
        kill(keyboard_pid_, SIGKILL);
        waitpid(keyboard_pid_, &status, 0);
    }

    keyboard_pid_ = 0;
    return true;
}

bool VirtualKeyboardLinux::is_keyboard_process_running() const {
    if (keyboard_pid_ <= 0) {
        return false;
    }

    // Check if process exists
    return (kill(keyboard_pid_, 0) == 0);
}

Result VirtualKeyboardLinux::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (!out_list) {
        return Result::ErrorInvalidParameter;
    }

    out_list->count = 0;

    // Try to get keyboard layouts using setxkbmap
    FILE* pipe = popen("setxkbmap -query 2>/dev/null | grep layout | awk '{print $2}'", "r");
    if (pipe) {
        char layouts[256];
        if (fgets(layouts, sizeof(layouts), pipe)) {
            // Remove newline
            layouts[strcspn(layouts, "\n")] = 0;

            // Split by comma
            char* token = strtok(layouts, ",");
            while (token && out_list->count < MAX_KEYBOARD_LAYOUTS) {
                KeyboardLayoutInfo& info = out_list->layouts[out_list->count];
                strncpy(info.identifier, token, sizeof(info.identifier) - 1);
                strncpy(info.language_code, token, MAX_LANGUAGE_CODE_LENGTH - 1);
                strncpy(info.display_name, token, sizeof(info.display_name) - 1);
                info.is_current = (out_list->count == 0); // First is current
                out_list->count++;
                token = strtok(nullptr, ",");
            }
        }
        pclose(pipe);
    }

    // Fallback: try localectl
    if (out_list->count == 0) {
        pipe = popen("localectl list-x11-keymap-layouts 2>/dev/null | head -32", "r");
        if (pipe) {
            char line[128];
            while (fgets(line, sizeof(line), pipe) && out_list->count < MAX_KEYBOARD_LAYOUTS) {
                line[strcspn(line, "\n")] = 0;
                if (strlen(line) > 0) {
                    KeyboardLayoutInfo& info = out_list->layouts[out_list->count];
                    strncpy(info.identifier, line, sizeof(info.identifier) - 1);
                    strncpy(info.language_code, line, MAX_LANGUAGE_CODE_LENGTH - 1);
                    strncpy(info.display_name, line, sizeof(info.display_name) - 1);
                    out_list->count++;
                }
            }
            pclose(pipe);
        }
    }

    return Result::Success;
}

Result VirtualKeyboardLinux::get_current_layout(KeyboardLayoutInfo* out_info) const {
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    // Use setxkbmap to get current layout
    FILE* pipe = popen("setxkbmap -query 2>/dev/null | grep layout | awk '{print $2}'", "r");
    if (pipe) {
        char layout[64];
        if (fgets(layout, sizeof(layout), pipe)) {
            layout[strcspn(layout, "\n")] = 0;

            // Get first layout if multiple
            char* comma = strchr(layout, ',');
            if (comma) *comma = 0;

            strncpy(out_info->identifier, layout, sizeof(out_info->identifier) - 1);
            strncpy(out_info->language_code, layout, MAX_LANGUAGE_CODE_LENGTH - 1);
            strncpy(out_info->display_name, layout, sizeof(out_info->display_name) - 1);
            out_info->is_current = true;

            pclose(pipe);
            return Result::Success;
        }
        pclose(pipe);
    }

    return Result::ErrorUnknown;
}

Result VirtualKeyboardLinux::set_layout(const char* identifier) {
    if (!identifier) {
        return Result::ErrorInvalidParameter;
    }

    char command[256];
    snprintf(command, sizeof(command), "setxkbmap %s 2>/dev/null", identifier);

    if (system(command) == 0) {
        return Result::Success;
    }

    return Result::ErrorUnknown;
}

// Factory functions
IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardLinux();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

} // namespace vkeyboard

#endif // __linux__ && !__ANDROID__
