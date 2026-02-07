/*
 * virtual_keyboard.cpp - Platform-Independent Utilities
 *
 * Contains string conversion and common utility functions.
 */

#include "virtual_keyboard.hpp"

namespace vkeyboard {

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* result_to_string(Result result) {
    switch (result) {
        case Result::Success:               return "Success";
        case Result::ErrorUnknown:          return "Unknown error";
        case Result::ErrorNotSupported:     return "Not supported on this platform";
        case Result::ErrorNotInitialized:   return "Not initialized";
        case Result::ErrorAlreadyInitialized: return "Already initialized";
        case Result::ErrorNoKeyboardAvailable: return "No virtual keyboard available";
        case Result::ErrorPermissionDenied: return "Permission denied";
        case Result::ErrorInvalidParameter: return "Invalid parameter";
        case Result::ErrorNotFocused:       return "No text input focused";
        default:                            return "Unknown";
    }
}

const char* keyboard_type_to_string(KeyboardType type) {
    switch (type) {
        case KeyboardType::Default:           return "Default";
        case KeyboardType::Text:              return "Text";
        case KeyboardType::Number:            return "Number";
        case KeyboardType::Phone:             return "Phone";
        case KeyboardType::Email:             return "Email";
        case KeyboardType::URL:               return "URL";
        case KeyboardType::Password:          return "Password";
        case KeyboardType::Search:            return "Search";
        case KeyboardType::Decimal:           return "Decimal";
        case KeyboardType::NamePhone:         return "NamePhone";
        case KeyboardType::Twitter:           return "Twitter";
        case KeyboardType::WebSearch:         return "WebSearch";
        case KeyboardType::ASCII:             return "ASCII";
        case KeyboardType::NumberPunctuation: return "NumberPunctuation";
        default:                              return "Unknown";
    }
}

const char* keyboard_state_to_string(KeyboardState state) {
    switch (state) {
        case KeyboardState::Hidden:   return "Hidden";
        case KeyboardState::Showing:  return "Showing";
        case KeyboardState::Visible:  return "Visible";
        case KeyboardState::Hiding:   return "Hiding";
        default:                      return "Unknown";
    }
}

const char* return_key_type_to_string(ReturnKeyType type) {
    switch (type) {
        case ReturnKeyType::Default:       return "Default";
        case ReturnKeyType::Done:          return "Done";
        case ReturnKeyType::Go:            return "Go";
        case ReturnKeyType::Next:          return "Next";
        case ReturnKeyType::Search:        return "Search";
        case ReturnKeyType::Send:          return "Send";
        case ReturnKeyType::Join:          return "Join";
        case ReturnKeyType::Route:         return "Route";
        case ReturnKeyType::Continue:      return "Continue";
        case ReturnKeyType::EmergencyCall: return "EmergencyCall";
        default:                           return "Unknown";
    }
}

// ============================================================================
// Platform Detection
// ============================================================================

const char* get_platform_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IOS
        return "iOS";
    #else
        return "macOS";
    #endif
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__EMSCRIPTEN__)
    return "WebAssembly";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

bool is_virtual_keyboard_supported() {
#if defined(_WIN32)
    return true;  // Windows 8+ touch keyboard
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IOS
        return true;  // iOS always has virtual keyboard
    #else
        return true;  // macOS has accessibility keyboard
    #endif
#elif defined(__ANDROID__)
    return true;  // Android soft keyboard
#elif defined(__EMSCRIPTEN__)
    return true;  // Mobile web browsers
#elif defined(__linux__)
    return true;  // Limited support via IBus/Fcitx or Wayland
#else
    return false;
#endif
}

} // namespace vkeyboard
