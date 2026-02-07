/*
 * virtual_keyboard_apple.mm - macOS/iOS Virtual Keyboard Implementation
 *
 * iOS: Uses UIKit keyboard notifications and UITextInputTraits
 * macOS: Uses Accessibility Keyboard and Input Sources
 */

#if defined(__APPLE__)

#include "virtual_keyboard.hpp"
#import <TargetConditionals.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#endif

#include <cstring>

namespace vkeyboard {

// ============================================================================
// iOS Implementation
// ============================================================================

#if TARGET_OS_IOS

@interface VKeyboardObserver : NSObject
@property (nonatomic, assign) IVirtualKeyboardEventHandler* eventHandler;
@property (nonatomic, assign) KeyboardState* keyboardState;
@property (nonatomic, assign) Rect* keyboardFrame;
@end

@implementation VKeyboardObserver

- (instancetype)init {
    self = [super init];
    if (self) {
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(keyboardWillShow:)
            name:UIKeyboardWillShowNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(keyboardDidShow:)
            name:UIKeyboardDidShowNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(keyboardWillHide:)
            name:UIKeyboardWillHideNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(keyboardDidHide:)
            name:UIKeyboardDidHideNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self
            selector:@selector(keyboardWillChangeFrame:)
            name:UIKeyboardWillChangeFrameNotification object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (KeyboardEventData)eventDataFromNotification:(NSNotification*)notification state:(KeyboardState)state {
    KeyboardEventData data;
    data.state = state;

    NSDictionary* userInfo = notification.userInfo;

    CGRect frame = [userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    data.frame.x = frame.origin.x;
    data.frame.y = frame.origin.y;
    data.frame.width = frame.size.width;
    data.frame.height = frame.size.height;

    NSNumber* duration = userInfo[UIKeyboardAnimationDurationUserInfoKey];
    data.animation_duration = duration ? duration.floatValue : 0.25f;

    if (_keyboardFrame) {
        *_keyboardFrame = data.frame;
    }

    return data;
}

- (void)keyboardWillShow:(NSNotification*)notification {
    if (_keyboardState) {
        *_keyboardState = KeyboardState::Showing;
    }
    if (_eventHandler) {
        KeyboardEventData data = [self eventDataFromNotification:notification state:KeyboardState::Showing];
        data.previous_state = KeyboardState::Hidden;
        _eventHandler->on_keyboard_will_show(data);
    }
}

- (void)keyboardDidShow:(NSNotification*)notification {
    if (_keyboardState) {
        *_keyboardState = KeyboardState::Visible;
    }
    if (_eventHandler) {
        KeyboardEventData data = [self eventDataFromNotification:notification state:KeyboardState::Visible];
        data.previous_state = KeyboardState::Showing;
        _eventHandler->on_keyboard_did_show(data);
    }
}

- (void)keyboardWillHide:(NSNotification*)notification {
    if (_keyboardState) {
        *_keyboardState = KeyboardState::Hiding;
    }
    if (_eventHandler) {
        KeyboardEventData data = [self eventDataFromNotification:notification state:KeyboardState::Hiding];
        data.previous_state = KeyboardState::Visible;
        _eventHandler->on_keyboard_will_hide(data);
    }
}

- (void)keyboardDidHide:(NSNotification*)notification {
    if (_keyboardState) {
        *_keyboardState = KeyboardState::Hidden;
    }
    if (_eventHandler) {
        KeyboardEventData data = [self eventDataFromNotification:notification state:KeyboardState::Hidden];
        data.previous_state = KeyboardState::Hiding;
        _eventHandler->on_keyboard_did_hide(data);
    }
}

- (void)keyboardWillChangeFrame:(NSNotification*)notification {
    if (_eventHandler && _keyboardState && *_keyboardState == KeyboardState::Visible) {
        KeyboardEventData data = [self eventDataFromNotification:notification state:KeyboardState::Visible];
        _eventHandler->on_keyboard_frame_changed(data);
    }
}

@end

class VirtualKeyboardIOS : public IVirtualKeyboard {
public:
    VirtualKeyboardIOS();
    ~VirtualKeyboardIOS();

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

    void begin_text_input() override { text_input_active_ = true; }
    void end_text_input() override { text_input_active_ = false; }
    bool is_text_input_active() const override { return text_input_active_; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override;

    Result get_available_layouts(KeyboardLayoutList* out_list) const override;
    Result get_current_layout(KeyboardLayoutInfo* out_info) const override;
    Result set_layout(const char* identifier) override;

    void* get_native_handle() const override { return nullptr; }

    void update() override {}

private:
    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    Rect frame_;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;

    VKeyboardObserver* observer_ = nil;
};

VirtualKeyboardIOS::VirtualKeyboardIOS() {}

VirtualKeyboardIOS::~VirtualKeyboardIOS() {
    shutdown();
}

Result VirtualKeyboardIOS::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    observer_ = [[VKeyboardObserver alloc] init];
    observer_.keyboardState = &state_;
    observer_.keyboardFrame = &frame_;

    initialized_ = true;
    return Result::Success;
}

void VirtualKeyboardIOS::shutdown() {
    if (!initialized_) return;

    observer_ = nil;
    initialized_ = false;
}

Result VirtualKeyboardIOS::show() {
    return show(config_);
}

Result VirtualKeyboardIOS::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;

    // On iOS, showing keyboard requires a first responder UITextField/UITextView
    // This is typically handled by the application's UI framework
    // We can only request the keyboard to be shown via the responder chain

    return Result::Success;
}

Result VirtualKeyboardIOS::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    // Request keyboard dismissal
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] sendAction:@selector(resignFirstResponder)
                                                   to:nil from:nil forEvent:nil];
    });

    return Result::Success;
}

Result VirtualKeyboardIOS::toggle() {
    if (is_visible()) {
        return hide();
    } else {
        return show();
    }
}

void VirtualKeyboardIOS::set_event_handler(IVirtualKeyboardEventHandler* handler) {
    event_handler_ = handler;
    if (observer_) {
        observer_.eventHandler = handler;
    }
}

Result VirtualKeyboardIOS::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (!out_list) {
        return Result::ErrorInvalidParameter;
    }

    out_list->count = 0;

    // Get active input modes
    NSArray* inputModes = [[UITextInputMode activeInputModes] copy];

    for (UITextInputMode* mode in inputModes) {
        if (out_list->count >= MAX_KEYBOARD_LAYOUTS) break;

        KeyboardLayoutInfo& info = out_list->layouts[out_list->count];

        NSString* identifier = mode.primaryLanguage;
        if (identifier) {
            strncpy(info.identifier, [identifier UTF8String], sizeof(info.identifier) - 1);
            strncpy(info.language_code, [identifier UTF8String], MAX_LANGUAGE_CODE_LENGTH - 1);

            // Get localized display name
            NSLocale* locale = [NSLocale currentLocale];
            NSString* displayName = [locale localizedStringForLanguageCode:identifier];
            if (displayName) {
                strncpy(info.display_name, [displayName UTF8String], sizeof(info.display_name) - 1);
            }

            info.is_current = (mode == [UITextInputMode currentInputMode]);
            out_list->count++;
        }
    }

    return Result::Success;
}

Result VirtualKeyboardIOS::get_current_layout(KeyboardLayoutInfo* out_info) const {
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    UITextInputMode* current = [UITextInputMode currentInputMode];
    if (!current) {
        return Result::ErrorUnknown;
    }

    NSString* identifier = current.primaryLanguage;
    if (identifier) {
        strncpy(out_info->identifier, [identifier UTF8String], sizeof(out_info->identifier) - 1);
        strncpy(out_info->language_code, [identifier UTF8String], MAX_LANGUAGE_CODE_LENGTH - 1);

        NSLocale* locale = [NSLocale currentLocale];
        NSString* displayName = [locale localizedStringForLanguageCode:identifier];
        if (displayName) {
            strncpy(out_info->display_name, [displayName UTF8String], sizeof(out_info->display_name) - 1);
        }

        out_info->is_current = true;
    }

    return Result::Success;
}

Result VirtualKeyboardIOS::set_layout(const char* identifier) {
    // iOS doesn't allow programmatic keyboard layout switching
    (void)identifier;
    return Result::ErrorNotSupported;
}

IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardIOS();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

// ============================================================================
// macOS Implementation
// ============================================================================

#else // macOS

class VirtualKeyboardMacOS : public IVirtualKeyboard {
public:
    VirtualKeyboardMacOS();
    ~VirtualKeyboardMacOS();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    bool is_supported() const override { return true; }
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
    bool is_accessibility_keyboard_visible() const;

    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;
};

VirtualKeyboardMacOS::VirtualKeyboardMacOS() {}

VirtualKeyboardMacOS::~VirtualKeyboardMacOS() {
    shutdown();
}

Result VirtualKeyboardMacOS::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    initialized_ = true;
    return Result::Success;
}

void VirtualKeyboardMacOS::shutdown() {
    initialized_ = false;
}

bool VirtualKeyboardMacOS::is_available() const {
    // Accessibility Keyboard is always available on macOS
    return true;
}

Result VirtualKeyboardMacOS::show() {
    return show(config_);
}

Result VirtualKeyboardMacOS::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;

    // Open Accessibility Keyboard via AppleScript
    NSString* script = @"tell application \"System Events\" to tell process \"Accessibility Keyboard\" to set visible to true";

    // First, try to open Keyboard Viewer
    NSString* keyboardViewerScript = @"tell application \"Keyboard Viewer\" to activate";

    NSAppleScript* appleScript = [[NSAppleScript alloc] initWithSource:keyboardViewerScript];
    NSDictionary* errorInfo = nil;
    [appleScript executeAndReturnError:&errorInfo];

    if (errorInfo) {
        // Try System Preferences keyboard viewer
        NSURL* url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.keyboard?Keyboard"];
        [[NSWorkspace sharedWorkspace] openURL:url];
    }

    state_ = KeyboardState::Visible;
    return Result::Success;
}

Result VirtualKeyboardMacOS::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    // Close Keyboard Viewer
    NSString* script = @"tell application \"Keyboard Viewer\" to quit";
    NSAppleScript* appleScript = [[NSAppleScript alloc] initWithSource:script];
    [appleScript executeAndReturnError:nil];

    state_ = KeyboardState::Hidden;
    return Result::Success;
}

Result VirtualKeyboardMacOS::toggle() {
    if (is_visible()) {
        return hide();
    } else {
        return show();
    }
}

bool VirtualKeyboardMacOS::is_visible() const {
    return is_accessibility_keyboard_visible();
}

bool VirtualKeyboardMacOS::is_accessibility_keyboard_visible() const {
    // Check if Keyboard Viewer is running
    NSArray* apps = [[NSWorkspace sharedWorkspace] runningApplications];
    for (NSRunningApplication* app in apps) {
        if ([app.bundleIdentifier isEqualToString:@"com.apple.KeyboardViewer"]) {
            return !app.isHidden;
        }
    }
    return false;
}

Rect VirtualKeyboardMacOS::get_frame() const {
    // macOS virtual keyboard doesn't have a fixed frame like mobile
    // Return an empty rect
    return Rect{};
}

float VirtualKeyboardMacOS::get_height() const {
    return 0.0f;
}

void VirtualKeyboardMacOS::update() {
    if (!initialized_) return;

    KeyboardState old_state = state_;
    state_ = is_visible() ? KeyboardState::Visible : KeyboardState::Hidden;

    if (state_ != old_state && event_handler_) {
        KeyboardEventData data;
        data.state = state_;
        data.previous_state = old_state;

        if (state_ == KeyboardState::Visible) {
            event_handler_->on_keyboard_did_show(data);
        } else {
            event_handler_->on_keyboard_did_hide(data);
        }
    }
}

Result VirtualKeyboardMacOS::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (!out_list) {
        return Result::ErrorInvalidParameter;
    }

    out_list->count = 0;

    // Get input sources
    CFArrayRef sources = TISCreateInputSourceList(nullptr, false);
    if (!sources) {
        return Result::ErrorUnknown;
    }

    TISInputSourceRef currentSource = TISCopyCurrentKeyboardInputSource();
    CFStringRef currentID = currentSource ?
        (CFStringRef)TISGetInputSourceProperty(currentSource, kTISPropertyInputSourceID) : nullptr;

    CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count && out_list->count < MAX_KEYBOARD_LAYOUTS; ++i) {
        TISInputSourceRef source = (TISInputSourceRef)CFArrayGetValueAtIndex(sources, i);

        // Only include keyboard layouts
        CFStringRef category = (CFStringRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceCategory);
        if (!category || !CFEqual(category, kTISCategoryKeyboardInputSource)) {
            continue;
        }

        KeyboardLayoutInfo& info = out_list->layouts[out_list->count];

        // Get identifier
        CFStringRef sourceID = (CFStringRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceID);
        if (sourceID) {
            CFStringGetCString(sourceID, info.identifier, sizeof(info.identifier), kCFStringEncodingUTF8);
        }

        // Get display name
        CFStringRef localizedName = (CFStringRef)TISGetInputSourceProperty(source, kTISPropertyLocalizedName);
        if (localizedName) {
            CFStringGetCString(localizedName, info.display_name, sizeof(info.display_name), kCFStringEncodingUTF8);
        }

        // Get language
        CFArrayRef languages = (CFArrayRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceLanguages);
        if (languages && CFArrayGetCount(languages) > 0) {
            CFStringRef lang = (CFStringRef)CFArrayGetValueAtIndex(languages, 0);
            CFStringGetCString(lang, info.language_code, MAX_LANGUAGE_CODE_LENGTH, kCFStringEncodingUTF8);
        }

        // Check if current
        info.is_current = (currentID && sourceID && CFEqual(sourceID, currentID));

        out_list->count++;
    }

    if (currentSource) {
        CFRelease(currentSource);
    }
    CFRelease(sources);

    return Result::Success;
}

Result VirtualKeyboardMacOS::get_current_layout(KeyboardLayoutInfo* out_info) const {
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    if (!source) {
        return Result::ErrorUnknown;
    }

    // Get identifier
    CFStringRef sourceID = (CFStringRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceID);
    if (sourceID) {
        CFStringGetCString(sourceID, out_info->identifier, sizeof(out_info->identifier), kCFStringEncodingUTF8);
    }

    // Get display name
    CFStringRef localizedName = (CFStringRef)TISGetInputSourceProperty(source, kTISPropertyLocalizedName);
    if (localizedName) {
        CFStringGetCString(localizedName, out_info->display_name, sizeof(out_info->display_name), kCFStringEncodingUTF8);
    }

    // Get language
    CFArrayRef languages = (CFArrayRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceLanguages);
    if (languages && CFArrayGetCount(languages) > 0) {
        CFStringRef lang = (CFStringRef)CFArrayGetValueAtIndex(languages, 0);
        CFStringGetCString(lang, out_info->language_code, MAX_LANGUAGE_CODE_LENGTH, kCFStringEncodingUTF8);
    }

    out_info->is_current = true;

    CFRelease(source);
    return Result::Success;
}

Result VirtualKeyboardMacOS::set_layout(const char* identifier) {
    if (!identifier) {
        return Result::ErrorInvalidParameter;
    }

    CFStringRef sourceID = CFStringCreateWithCString(kCFAllocatorDefault, identifier, kCFStringEncodingUTF8);
    if (!sourceID) {
        return Result::ErrorInvalidParameter;
    }

    // Find the input source
    CFMutableDictionaryRef properties = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    CFDictionarySetValue(properties, kTISPropertyInputSourceID, sourceID);

    CFArrayRef sources = TISCreateInputSourceList(properties, false);
    CFRelease(properties);
    CFRelease(sourceID);

    if (!sources || CFArrayGetCount(sources) == 0) {
        if (sources) CFRelease(sources);
        return Result::ErrorInvalidParameter;
    }

    TISInputSourceRef source = (TISInputSourceRef)CFArrayGetValueAtIndex(sources, 0);
    OSStatus status = TISSelectInputSource(source);
    CFRelease(sources);

    return (status == noErr) ? Result::Success : Result::ErrorUnknown;
}

IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardMacOS();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

#endif // TARGET_OS_IOS

} // namespace vkeyboard

#endif // __APPLE__
