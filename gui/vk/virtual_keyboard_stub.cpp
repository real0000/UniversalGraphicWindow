/*
 * virtual_keyboard_stub.cpp - Stub Implementation for Unsupported Platforms
 *
 * Provides a minimal implementation that reports "not supported"
 * for platforms without native virtual keyboard support.
 *
 * Also handles WebAssembly which relies on browser's virtual keyboard.
 */

// Only compile for platforms not covered by other implementations
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__ANDROID__) && !defined(__linux__)

#include "virtual_keyboard.hpp"
#include <cstring>

namespace vkeyboard {

class VirtualKeyboardStub : public IVirtualKeyboard {
public:
    VirtualKeyboardStub() {}
    ~VirtualKeyboardStub() {}

    Result initialize() override { return Result::Success; }
    void shutdown() override {}
    bool is_initialized() const override { return true; }

    bool is_supported() const override { return false; }
    bool is_available() const override { return false; }

    Result show() override { return Result::ErrorNotSupported; }
    Result show(const KeyboardConfig& config) override { (void)config; return Result::ErrorNotSupported; }
    Result hide() override { return Result::ErrorNotSupported; }
    Result toggle() override { return Result::ErrorNotSupported; }

    KeyboardState get_state() const override { return KeyboardState::Hidden; }
    bool is_visible() const override { return false; }
    Rect get_frame() const override { return Rect{}; }
    float get_height() const override { return 0.0f; }

    void set_config(const KeyboardConfig& config) override { (void)config; }
    KeyboardConfig get_config() const override { return KeyboardConfig{}; }

    void set_text_input_delegate(ITextInputDelegate* delegate) override { (void)delegate; }
    ITextInputDelegate* get_text_input_delegate() const override { return nullptr; }
    void update_text_input_context(const TextInputContext& context) override { (void)context; }

    void begin_text_input() override {}
    void end_text_input() override {}
    bool is_text_input_active() const override { return false; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override { (void)handler; }

    Result get_available_layouts(KeyboardLayoutList* out_list) const override {
        if (out_list) out_list->count = 0;
        return Result::ErrorNotSupported;
    }

    Result get_current_layout(KeyboardLayoutInfo* out_info) const override {
        (void)out_info;
        return Result::ErrorNotSupported;
    }

    Result set_layout(const char* identifier) override {
        (void)identifier;
        return Result::ErrorNotSupported;
    }

    void* get_native_handle() const override { return nullptr; }
    void update() override {}
};

IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardStub();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

} // namespace vkeyboard

#endif // Unsupported platforms

// ============================================================================
// WebAssembly Implementation
// ============================================================================

#if defined(__EMSCRIPTEN__)

#include "virtual_keyboard.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

namespace vkeyboard {

class VirtualKeyboardWasm : public IVirtualKeyboard {
public:
    VirtualKeyboardWasm();
    ~VirtualKeyboardWasm();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    bool is_supported() const override { return true; }
    bool is_available() const override { return true; }

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

    void begin_text_input() override;
    void end_text_input() override;
    bool is_text_input_active() const override { return text_input_active_; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override { event_handler_ = handler; }

    Result get_available_layouts(KeyboardLayoutList* out_list) const override;
    Result get_current_layout(KeyboardLayoutInfo* out_info) const override;
    Result set_layout(const char* identifier) override;

    void* get_native_handle() const override { return nullptr; }
    void update() override;

    // Static instance for callbacks
    static VirtualKeyboardWasm* instance;

private:
    void create_hidden_input();
    void remove_hidden_input();
    const char* get_input_type() const;

    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    Rect frame_;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;
    bool has_hidden_input_ = false;
};

VirtualKeyboardWasm* VirtualKeyboardWasm::instance = nullptr;

VirtualKeyboardWasm::VirtualKeyboardWasm() {
    instance = this;
}

VirtualKeyboardWasm::~VirtualKeyboardWasm() {
    shutdown();
    if (instance == this) {
        instance = nullptr;
    }
}

Result VirtualKeyboardWasm::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    initialized_ = true;
    return Result::Success;
}

void VirtualKeyboardWasm::shutdown() {
    if (!initialized_) return;

    remove_hidden_input();
    initialized_ = false;
}

void VirtualKeyboardWasm::create_hidden_input() {
    if (has_hidden_input_) return;

    const char* input_type = get_input_type();

    EM_ASM({
        var inputType = UTF8ToString($0);
        var input = document.createElement('input');
        input.id = 'vkeyboard-hidden-input';
        input.type = inputType;
        input.style.position = 'fixed';
        input.style.left = '0';
        input.style.top = '0';
        input.style.width = '1px';
        input.style.height = '1px';
        input.style.opacity = '0';
        input.style.pointerEvents = 'none';
        input.autocomplete = 'off';
        input.autocapitalize = 'off';
        input.spellcheck = false;

        input.addEventListener('input', function(e) {
            // Forward input to native code if needed
        });

        input.addEventListener('blur', function(e) {
            // Handle blur
        });

        document.body.appendChild(input);
    }, input_type);

    has_hidden_input_ = true;
}

void VirtualKeyboardWasm::remove_hidden_input() {
    if (!has_hidden_input_) return;

    EM_ASM({
        var input = document.getElementById('vkeyboard-hidden-input');
        if (input) {
            input.remove();
        }
    });

    has_hidden_input_ = false;
}

const char* VirtualKeyboardWasm::get_input_type() const {
    switch (config_.type) {
        case KeyboardType::Number:
        case KeyboardType::Decimal:
            return "number";
        case KeyboardType::Phone:
            return "tel";
        case KeyboardType::Email:
            return "email";
        case KeyboardType::URL:
            return "url";
        case KeyboardType::Password:
            return "password";
        case KeyboardType::Search:
            return "search";
        default:
            return "text";
    }
}

Result VirtualKeyboardWasm::show() {
    return show(config_);
}

Result VirtualKeyboardWasm::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;
    create_hidden_input();

    // Focus the hidden input to trigger virtual keyboard
    EM_ASM({
        var input = document.getElementById('vkeyboard-hidden-input');
        if (input) {
            input.focus();
        }
    });

    state_ = KeyboardState::Visible;

    if (event_handler_) {
        KeyboardEventData data;
        data.state = KeyboardState::Visible;
        event_handler_->on_keyboard_did_show(data);
    }

    return Result::Success;
}

Result VirtualKeyboardWasm::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    // Blur the hidden input
    EM_ASM({
        var input = document.getElementById('vkeyboard-hidden-input');
        if (input) {
            input.blur();
        }
    });

    state_ = KeyboardState::Hidden;

    if (event_handler_) {
        KeyboardEventData data;
        data.state = KeyboardState::Hidden;
        event_handler_->on_keyboard_did_hide(data);
    }

    return Result::Success;
}

Result VirtualKeyboardWasm::toggle() {
    if (is_visible()) {
        return hide();
    } else {
        return show();
    }
}

void VirtualKeyboardWasm::begin_text_input() {
    text_input_active_ = true;
    show();
}

void VirtualKeyboardWasm::end_text_input() {
    text_input_active_ = false;
    hide();
}

void VirtualKeyboardWasm::update() {
    // Check if visual viewport API is available for keyboard detection
    // This is a hint for mobile browsers
}

Result VirtualKeyboardWasm::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (out_list) {
        out_list->count = 0;
    }
    // Browser controls keyboard layout
    return Result::ErrorNotSupported;
}

Result VirtualKeyboardWasm::get_current_layout(KeyboardLayoutInfo* out_info) const {
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    // Try to get navigator.language
    EM_ASM({
        var lang = navigator.language || 'en';
        stringToUTF8(lang, $0, 16);
    }, out_info->language_code);

    strncpy(out_info->display_name, "Browser Keyboard", sizeof(out_info->display_name) - 1);
    out_info->is_current = true;

    return Result::Success;
}

Result VirtualKeyboardWasm::set_layout(const char* identifier) {
    (void)identifier;
    return Result::ErrorNotSupported;
}

IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardWasm();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

} // namespace vkeyboard

#endif // __EMSCRIPTEN__
