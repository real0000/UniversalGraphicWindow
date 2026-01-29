/*
 * input_keyboard.cpp - Cross-platform keyboard input utilities
 */

#include "input_keyboard.hpp"
#include "input_mouse.hpp"
#include <cstring>
#include <cctype>

namespace window {
namespace input {

//=============================================================================
// Key Code Utilities
//=============================================================================

const char* key_to_string(Key key) {
    switch (key) {
        case Key::Unknown: return "Unknown";

        // Letters
        case Key::A: return "A"; case Key::B: return "B"; case Key::C: return "C";
        case Key::D: return "D"; case Key::E: return "E"; case Key::F: return "F";
        case Key::G: return "G"; case Key::H: return "H"; case Key::I: return "I";
        case Key::J: return "J"; case Key::K: return "K"; case Key::L: return "L";
        case Key::M: return "M"; case Key::N: return "N"; case Key::O: return "O";
        case Key::P: return "P"; case Key::Q: return "Q"; case Key::R: return "R";
        case Key::S: return "S"; case Key::T: return "T"; case Key::U: return "U";
        case Key::V: return "V"; case Key::W: return "W"; case Key::X: return "X";
        case Key::Y: return "Y"; case Key::Z: return "Z";

        // Numbers
        case Key::Num0: return "0"; case Key::Num1: return "1"; case Key::Num2: return "2";
        case Key::Num3: return "3"; case Key::Num4: return "4"; case Key::Num5: return "5";
        case Key::Num6: return "6"; case Key::Num7: return "7"; case Key::Num8: return "8";
        case Key::Num9: return "9";

        // Function keys
        case Key::F1: return "F1"; case Key::F2: return "F2"; case Key::F3: return "F3";
        case Key::F4: return "F4"; case Key::F5: return "F5"; case Key::F6: return "F6";
        case Key::F7: return "F7"; case Key::F8: return "F8"; case Key::F9: return "F9";
        case Key::F10: return "F10"; case Key::F11: return "F11"; case Key::F12: return "F12";
        case Key::F13: return "F13"; case Key::F14: return "F14"; case Key::F15: return "F15";
        case Key::F16: return "F16"; case Key::F17: return "F17"; case Key::F18: return "F18";
        case Key::F19: return "F19"; case Key::F20: return "F20"; case Key::F21: return "F21";
        case Key::F22: return "F22"; case Key::F23: return "F23"; case Key::F24: return "F24";

        // Navigation
        case Key::Escape: return "Escape";
        case Key::Tab: return "Tab";
        case Key::CapsLock: return "CapsLock";
        case Key::Shift: return "Shift";
        case Key::Control: return "Control";
        case Key::Alt: return "Alt";
        case Key::Super: return "Super";
        case Key::Space: return "Space";
        case Key::Enter: return "Enter";
        case Key::Backspace: return "Backspace";
        case Key::Delete: return "Delete";
        case Key::Insert: return "Insert";
        case Key::Home: return "Home";
        case Key::End: return "End";
        case Key::PageUp: return "PageUp";
        case Key::PageDown: return "PageDown";
        case Key::Left: return "Left";
        case Key::Right: return "Right";
        case Key::Up: return "Up";
        case Key::Down: return "Down";

        // Modifiers (left/right variants)
        case Key::LeftShift: return "LeftShift";
        case Key::RightShift: return "RightShift";
        case Key::LeftControl: return "LeftControl";
        case Key::RightControl: return "RightControl";
        case Key::LeftAlt: return "LeftAlt";
        case Key::RightAlt: return "RightAlt";
        case Key::LeftSuper: return "LeftSuper";
        case Key::RightSuper: return "RightSuper";

        // Punctuation
        case Key::Grave: return "Grave";
        case Key::Minus: return "Minus";
        case Key::Equal: return "Equal";
        case Key::LeftBracket: return "LeftBracket";
        case Key::RightBracket: return "RightBracket";
        case Key::Backslash: return "Backslash";
        case Key::Semicolon: return "Semicolon";
        case Key::Apostrophe: return "Apostrophe";
        case Key::Comma: return "Comma";
        case Key::Period: return "Period";
        case Key::Slash: return "Slash";

        // Numpad
        case Key::Numpad0: return "Numpad0"; case Key::Numpad1: return "Numpad1";
        case Key::Numpad2: return "Numpad2"; case Key::Numpad3: return "Numpad3";
        case Key::Numpad4: return "Numpad4"; case Key::Numpad5: return "Numpad5";
        case Key::Numpad6: return "Numpad6"; case Key::Numpad7: return "Numpad7";
        case Key::Numpad8: return "Numpad8"; case Key::Numpad9: return "Numpad9";
        case Key::NumpadDecimal: return "NumpadDecimal";
        case Key::NumpadEnter: return "NumpadEnter";
        case Key::NumpadAdd: return "NumpadAdd";
        case Key::NumpadSubtract: return "NumpadSubtract";
        case Key::NumpadMultiply: return "NumpadMultiply";
        case Key::NumpadDivide: return "NumpadDivide";
        case Key::NumLock: return "NumLock";

        // Other
        case Key::PrintScreen: return "PrintScreen";
        case Key::ScrollLock: return "ScrollLock";
        case Key::Pause: return "Pause";
        case Key::Menu: return "Menu";

        default: return "Unknown";
    }
}

// Helper for case-insensitive string comparison
static bool str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

Key string_to_key(const char* str) {
    if (!str || !*str) return Key::Unknown;

    // Single character - check for letter or number
    if (str[1] == '\0') {
        char c = toupper((unsigned char)str[0]);
        if (c >= 'A' && c <= 'Z') return static_cast<Key>(c);
        if (c >= '0' && c <= '9') return static_cast<Key>(c);
    }

    // Letters
    if (str_iequals(str, "A")) return Key::A;
    if (str_iequals(str, "B")) return Key::B;
    if (str_iequals(str, "C")) return Key::C;
    if (str_iequals(str, "D")) return Key::D;
    if (str_iequals(str, "E")) return Key::E;
    if (str_iequals(str, "F")) return Key::F;
    if (str_iequals(str, "G")) return Key::G;
    if (str_iequals(str, "H")) return Key::H;
    if (str_iequals(str, "I")) return Key::I;
    if (str_iequals(str, "J")) return Key::J;
    if (str_iequals(str, "K")) return Key::K;
    if (str_iequals(str, "L")) return Key::L;
    if (str_iequals(str, "M")) return Key::M;
    if (str_iequals(str, "N")) return Key::N;
    if (str_iequals(str, "O")) return Key::O;
    if (str_iequals(str, "P")) return Key::P;
    if (str_iequals(str, "Q")) return Key::Q;
    if (str_iequals(str, "R")) return Key::R;
    if (str_iequals(str, "S")) return Key::S;
    if (str_iequals(str, "T")) return Key::T;
    if (str_iequals(str, "U")) return Key::U;
    if (str_iequals(str, "V")) return Key::V;
    if (str_iequals(str, "W")) return Key::W;
    if (str_iequals(str, "X")) return Key::X;
    if (str_iequals(str, "Y")) return Key::Y;
    if (str_iequals(str, "Z")) return Key::Z;

    // Function keys
    if (str_iequals(str, "F1")) return Key::F1;
    if (str_iequals(str, "F2")) return Key::F2;
    if (str_iequals(str, "F3")) return Key::F3;
    if (str_iequals(str, "F4")) return Key::F4;
    if (str_iequals(str, "F5")) return Key::F5;
    if (str_iequals(str, "F6")) return Key::F6;
    if (str_iequals(str, "F7")) return Key::F7;
    if (str_iequals(str, "F8")) return Key::F8;
    if (str_iequals(str, "F9")) return Key::F9;
    if (str_iequals(str, "F10")) return Key::F10;
    if (str_iequals(str, "F11")) return Key::F11;
    if (str_iequals(str, "F12")) return Key::F12;
    if (str_iequals(str, "F13")) return Key::F13;
    if (str_iequals(str, "F14")) return Key::F14;
    if (str_iequals(str, "F15")) return Key::F15;
    if (str_iequals(str, "F16")) return Key::F16;
    if (str_iequals(str, "F17")) return Key::F17;
    if (str_iequals(str, "F18")) return Key::F18;
    if (str_iequals(str, "F19")) return Key::F19;
    if (str_iequals(str, "F20")) return Key::F20;
    if (str_iequals(str, "F21")) return Key::F21;
    if (str_iequals(str, "F22")) return Key::F22;
    if (str_iequals(str, "F23")) return Key::F23;
    if (str_iequals(str, "F24")) return Key::F24;

    // Navigation and special keys
    if (str_iequals(str, "Escape") || str_iequals(str, "Esc")) return Key::Escape;
    if (str_iequals(str, "Tab")) return Key::Tab;
    if (str_iequals(str, "CapsLock") || str_iequals(str, "Caps")) return Key::CapsLock;
    if (str_iequals(str, "Shift")) return Key::Shift;
    if (str_iequals(str, "Control") || str_iequals(str, "Ctrl")) return Key::Control;
    if (str_iequals(str, "Alt")) return Key::Alt;
    if (str_iequals(str, "Super") || str_iequals(str, "Win") || str_iequals(str, "Cmd") || str_iequals(str, "Meta")) return Key::Super;
    if (str_iequals(str, "Space")) return Key::Space;
    if (str_iequals(str, "Enter") || str_iequals(str, "Return")) return Key::Enter;
    if (str_iequals(str, "Backspace") || str_iequals(str, "Back")) return Key::Backspace;
    if (str_iequals(str, "Delete") || str_iequals(str, "Del")) return Key::Delete;
    if (str_iequals(str, "Insert") || str_iequals(str, "Ins")) return Key::Insert;
    if (str_iequals(str, "Home")) return Key::Home;
    if (str_iequals(str, "End")) return Key::End;
    if (str_iequals(str, "PageUp") || str_iequals(str, "PgUp")) return Key::PageUp;
    if (str_iequals(str, "PageDown") || str_iequals(str, "PgDn") || str_iequals(str, "PgDown")) return Key::PageDown;
    if (str_iequals(str, "Left")) return Key::Left;
    if (str_iequals(str, "Right")) return Key::Right;
    if (str_iequals(str, "Up")) return Key::Up;
    if (str_iequals(str, "Down")) return Key::Down;

    // Modifier variants
    if (str_iequals(str, "LeftShift") || str_iequals(str, "LShift")) return Key::LeftShift;
    if (str_iequals(str, "RightShift") || str_iequals(str, "RShift")) return Key::RightShift;
    if (str_iequals(str, "LeftControl") || str_iequals(str, "LCtrl") || str_iequals(str, "LeftCtrl")) return Key::LeftControl;
    if (str_iequals(str, "RightControl") || str_iequals(str, "RCtrl") || str_iequals(str, "RightCtrl")) return Key::RightControl;
    if (str_iequals(str, "LeftAlt") || str_iequals(str, "LAlt")) return Key::LeftAlt;
    if (str_iequals(str, "RightAlt") || str_iequals(str, "RAlt")) return Key::RightAlt;
    if (str_iequals(str, "LeftSuper") || str_iequals(str, "LSuper") || str_iequals(str, "LWin")) return Key::LeftSuper;
    if (str_iequals(str, "RightSuper") || str_iequals(str, "RSuper") || str_iequals(str, "RWin")) return Key::RightSuper;

    // Punctuation
    if (str_iequals(str, "Grave") || str_iequals(str, "Tilde") || str_iequals(str, "Backtick")) return Key::Grave;
    if (str_iequals(str, "Minus") || str_iequals(str, "Dash")) return Key::Minus;
    if (str_iequals(str, "Equal") || str_iequals(str, "Equals")) return Key::Equal;
    if (str_iequals(str, "LeftBracket") || str_iequals(str, "LBracket")) return Key::LeftBracket;
    if (str_iequals(str, "RightBracket") || str_iequals(str, "RBracket")) return Key::RightBracket;
    if (str_iequals(str, "Backslash")) return Key::Backslash;
    if (str_iequals(str, "Semicolon")) return Key::Semicolon;
    if (str_iequals(str, "Apostrophe") || str_iequals(str, "Quote")) return Key::Apostrophe;
    if (str_iequals(str, "Comma")) return Key::Comma;
    if (str_iequals(str, "Period") || str_iequals(str, "Dot")) return Key::Period;
    if (str_iequals(str, "Slash")) return Key::Slash;

    // Numpad
    if (str_iequals(str, "Numpad0") || str_iequals(str, "Num0") || str_iequals(str, "KP0")) return Key::Numpad0;
    if (str_iequals(str, "Numpad1") || str_iequals(str, "Num1") || str_iequals(str, "KP1")) return Key::Numpad1;
    if (str_iequals(str, "Numpad2") || str_iequals(str, "Num2") || str_iequals(str, "KP2")) return Key::Numpad2;
    if (str_iequals(str, "Numpad3") || str_iequals(str, "Num3") || str_iequals(str, "KP3")) return Key::Numpad3;
    if (str_iequals(str, "Numpad4") || str_iequals(str, "Num4") || str_iequals(str, "KP4")) return Key::Numpad4;
    if (str_iequals(str, "Numpad5") || str_iequals(str, "Num5") || str_iequals(str, "KP5")) return Key::Numpad5;
    if (str_iequals(str, "Numpad6") || str_iequals(str, "Num6") || str_iequals(str, "KP6")) return Key::Numpad6;
    if (str_iequals(str, "Numpad7") || str_iequals(str, "Num7") || str_iequals(str, "KP7")) return Key::Numpad7;
    if (str_iequals(str, "Numpad8") || str_iequals(str, "Num8") || str_iequals(str, "KP8")) return Key::Numpad8;
    if (str_iequals(str, "Numpad9") || str_iequals(str, "Num9") || str_iequals(str, "KP9")) return Key::Numpad9;
    if (str_iequals(str, "NumpadDecimal") || str_iequals(str, "NumDot") || str_iequals(str, "KPDecimal")) return Key::NumpadDecimal;
    if (str_iequals(str, "NumpadEnter") || str_iequals(str, "NumEnter") || str_iequals(str, "KPEnter")) return Key::NumpadEnter;
    if (str_iequals(str, "NumpadAdd") || str_iequals(str, "NumPlus") || str_iequals(str, "KPAdd")) return Key::NumpadAdd;
    if (str_iequals(str, "NumpadSubtract") || str_iequals(str, "NumMinus") || str_iequals(str, "KPSubtract")) return Key::NumpadSubtract;
    if (str_iequals(str, "NumpadMultiply") || str_iequals(str, "NumMul") || str_iequals(str, "KPMultiply")) return Key::NumpadMultiply;
    if (str_iequals(str, "NumpadDivide") || str_iequals(str, "NumDiv") || str_iequals(str, "KPDivide")) return Key::NumpadDivide;
    if (str_iequals(str, "NumLock")) return Key::NumLock;

    // Other
    if (str_iequals(str, "PrintScreen") || str_iequals(str, "Print") || str_iequals(str, "PrtSc")) return Key::PrintScreen;
    if (str_iequals(str, "ScrollLock") || str_iequals(str, "ScrLk")) return Key::ScrollLock;
    if (str_iequals(str, "Pause") || str_iequals(str, "Break")) return Key::Pause;
    if (str_iequals(str, "Menu") || str_iequals(str, "Apps") || str_iequals(str, "ContextMenu")) return Key::Menu;

    return Key::Unknown;
}

char key_to_char(Key key) {
    // Letters
    if (key >= Key::A && key <= Key::Z) {
        return 'a' + (static_cast<int>(key) - static_cast<int>(Key::A));
    }

    // Numbers
    if (key >= Key::Num0 && key <= Key::Num9) {
        return '0' + (static_cast<int>(key) - static_cast<int>(Key::Num0));
    }

    // Punctuation (unshifted)
    switch (key) {
        case Key::Space: return ' ';
        case Key::Grave: return '`';
        case Key::Minus: return '-';
        case Key::Equal: return '=';
        case Key::LeftBracket: return '[';
        case Key::RightBracket: return ']';
        case Key::Backslash: return '\\';
        case Key::Semicolon: return ';';
        case Key::Apostrophe: return '\'';
        case Key::Comma: return ',';
        case Key::Period: return '.';
        case Key::Slash: return '/';
        case Key::Tab: return '\t';
        case Key::Enter: return '\n';
        default: break;
    }

    // Numpad numbers
    if (key >= Key::Numpad0 && key <= Key::Numpad9) {
        return '0' + (static_cast<int>(key) - static_cast<int>(Key::Numpad0));
    }

    switch (key) {
        case Key::NumpadDecimal: return '.';
        case Key::NumpadAdd: return '+';
        case Key::NumpadSubtract: return '-';
        case Key::NumpadMultiply: return '*';
        case Key::NumpadDivide: return '/';
        case Key::NumpadEnter: return '\n';
        default: break;
    }

    return 0;
}

bool is_modifier_key(Key key) {
    switch (key) {
        case Key::Shift:
        case Key::Control:
        case Key::Alt:
        case Key::Super:
        case Key::LeftShift:
        case Key::RightShift:
        case Key::LeftControl:
        case Key::RightControl:
        case Key::LeftAlt:
        case Key::RightAlt:
        case Key::LeftSuper:
        case Key::RightSuper:
        case Key::CapsLock:
        case Key::NumLock:
            return true;
        default:
            return false;
    }
}

bool is_function_key(Key key) {
    return key >= Key::F1 && key <= Key::F24;
}

bool is_numpad_key(Key key) {
    return (key >= Key::Numpad0 && key <= Key::NumLock);
}

bool is_navigation_key(Key key) {
    switch (key) {
        case Key::Left:
        case Key::Right:
        case Key::Up:
        case Key::Down:
        case Key::Home:
        case Key::End:
        case Key::PageUp:
        case Key::PageDown:
        case Key::Insert:
        case Key::Delete:
            return true;
        default:
            return false;
    }
}

bool is_letter_key(Key key) {
    return key >= Key::A && key <= Key::Z;
}

bool is_number_key(Key key) {
    return key >= Key::Num0 && key <= Key::Num9;
}

// Mouse button utilities are in input_mouse.cpp

//=============================================================================
// Event Type Utilities
//=============================================================================

const char* event_type_to_string(EventType type) {
    switch (type) {
        case EventType::None: return "None";

        // Window events
        case EventType::WindowClose: return "WindowClose";
        case EventType::WindowResize: return "WindowResize";
        case EventType::WindowMove: return "WindowMove";
        case EventType::WindowFocus: return "WindowFocus";
        case EventType::WindowBlur: return "WindowBlur";
        case EventType::WindowMinimize: return "WindowMinimize";
        case EventType::WindowMaximize: return "WindowMaximize";
        case EventType::WindowRestore: return "WindowRestore";

        // Keyboard events
        case EventType::KeyDown: return "KeyDown";
        case EventType::KeyUp: return "KeyUp";
        case EventType::KeyRepeat: return "KeyRepeat";
        case EventType::CharInput: return "CharInput";

        // Mouse events
        case EventType::MouseDown: return "MouseDown";
        case EventType::MouseMove: return "MouseMove";
        case EventType::MouseUp: return "MouseUp";
        case EventType::MouseWheel: return "MouseWheel";

        // Touch events
        case EventType::TouchDown: return "TouchDown";
        case EventType::TouchUp: return "TouchUp";
        case EventType::TouchMove: return "TouchMove";

        // System events
        case EventType::DpiChange: return "DpiChange";
        case EventType::DropFile: return "DropFile";

        default: return "Unknown";
    }
}

EventType string_to_event_type(const char* str) {
    if (!str || !*str) return EventType::None;

    // Window events
    if (str_iequals(str, "WindowClose")) return EventType::WindowClose;
    if (str_iequals(str, "WindowResize")) return EventType::WindowResize;
    if (str_iequals(str, "WindowMove")) return EventType::WindowMove;
    if (str_iequals(str, "WindowFocus")) return EventType::WindowFocus;
    if (str_iequals(str, "WindowBlur")) return EventType::WindowBlur;
    if (str_iequals(str, "WindowMinimize")) return EventType::WindowMinimize;
    if (str_iequals(str, "WindowMaximize")) return EventType::WindowMaximize;
    if (str_iequals(str, "WindowRestore")) return EventType::WindowRestore;

    // Keyboard events
    if (str_iequals(str, "KeyDown")) return EventType::KeyDown;
    if (str_iequals(str, "KeyUp")) return EventType::KeyUp;
    if (str_iequals(str, "KeyRepeat")) return EventType::KeyRepeat;
    if (str_iequals(str, "CharInput")) return EventType::CharInput;

    // Mouse events
    if (str_iequals(str, "MouseDown")) return EventType::MouseDown;
    if (str_iequals(str, "MouseMove")) return EventType::MouseMove;
    if (str_iequals(str, "MouseUp")) return EventType::MouseUp;
    if (str_iequals(str, "MouseWheel") || str_iequals(str, "MouseScroll")) return EventType::MouseWheel;

    // Touch events
    if (str_iequals(str, "TouchDown")) return EventType::TouchDown;
    if (str_iequals(str, "TouchUp")) return EventType::TouchUp;
    if (str_iequals(str, "TouchMove")) return EventType::TouchMove;

    // System events
    if (str_iequals(str, "DpiChange")) return EventType::DpiChange;
    if (str_iequals(str, "DropFile")) return EventType::DropFile;

    return EventType::None;
}

bool is_keyboard_event(EventType type) {
    switch (type) {
        case EventType::KeyDown:
        case EventType::KeyUp:
        case EventType::KeyRepeat:
        case EventType::CharInput:
            return true;
        default:
            return false;
    }
}

bool is_mouse_event(EventType type) {
    switch (type) {
        case EventType::MouseDown:
        case EventType::MouseMove:
        case EventType::MouseUp:
        case EventType::MouseWheel:
            return true;
        default:
            return false;
    }
}

bool is_touch_event(EventType type) {
    switch (type) {
        case EventType::TouchDown:
        case EventType::TouchUp:
        case EventType::TouchMove:
            return true;
        default:
            return false;
    }
}

bool is_window_event(EventType type) {
    switch (type) {
        case EventType::WindowClose:
        case EventType::WindowResize:
        case EventType::WindowMove:
        case EventType::WindowFocus:
        case EventType::WindowBlur:
        case EventType::WindowMinimize:
        case EventType::WindowMaximize:
        case EventType::WindowRestore:
            return true;
        default:
            return false;
    }
}

//=============================================================================
// Key Modifier Utilities
//=============================================================================

void keymod_to_string(KeyMod mods, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';

    if (buffer_size < 2) return;

    char* ptr = buffer;
    size_t remaining = buffer_size;

    auto append = [&](const char* str) {
        size_t len = strlen(str);
        if (ptr != buffer && remaining > 1) {
            *ptr++ = '+';
            remaining--;
        }
        if (len < remaining) {
            memcpy(ptr, str, len);
            ptr += len;
            remaining -= len;
        }
    };

    if (has_mod(mods, KeyMod::Control)) append("Ctrl");
    if (has_mod(mods, KeyMod::Shift)) append("Shift");
    if (has_mod(mods, KeyMod::Alt)) append("Alt");
    if (has_mod(mods, KeyMod::Super)) append("Super");
    if (has_mod(mods, KeyMod::CapsLock)) append("CapsLock");
    if (has_mod(mods, KeyMod::NumLock)) append("NumLock");

    *ptr = '\0';
}

KeyMod string_to_keymod(const char* str) {
    if (!str || !*str) return KeyMod::None;

    KeyMod result = KeyMod::None;

    // Parse modifier string (e.g., "Ctrl+Shift" or "Control+Alt")
    const char* ptr = str;
    char token[32];

    while (*ptr) {
        // Skip separators
        while (*ptr == '+' || *ptr == ' ' || *ptr == '-') ptr++;
        if (!*ptr) break;

        // Extract token
        int i = 0;
        while (*ptr && *ptr != '+' && *ptr != ' ' && *ptr != '-' && i < 31) {
            token[i++] = *ptr++;
        }
        token[i] = '\0';

        // Match modifier
        if (str_iequals(token, "Ctrl") || str_iequals(token, "Control")) {
            result = result | KeyMod::Control;
        } else if (str_iequals(token, "Shift")) {
            result = result | KeyMod::Shift;
        } else if (str_iequals(token, "Alt")) {
            result = result | KeyMod::Alt;
        } else if (str_iequals(token, "Super") || str_iequals(token, "Win") || str_iequals(token, "Cmd") || str_iequals(token, "Meta")) {
            result = result | KeyMod::Super;
        } else if (str_iequals(token, "CapsLock") || str_iequals(token, "Caps")) {
            result = result | KeyMod::CapsLock;
        } else if (str_iequals(token, "NumLock") || str_iequals(token, "Num")) {
            result = result | KeyMod::NumLock;
        }
    }

    return result;
}

Key get_generic_modifier(Key key) {
    switch (key) {
        case Key::LeftShift:
        case Key::RightShift:
            return Key::Shift;
        case Key::LeftControl:
        case Key::RightControl:
            return Key::Control;
        case Key::LeftAlt:
        case Key::RightAlt:
            return Key::Alt;
        case Key::LeftSuper:
        case Key::RightSuper:
            return Key::Super;
        default:
            return key;
    }
}

KeyMod key_to_keymod(Key key) {
    switch (key) {
        case Key::Shift:
        case Key::LeftShift:
        case Key::RightShift:
            return KeyMod::Shift;
        case Key::Control:
        case Key::LeftControl:
        case Key::RightControl:
            return KeyMod::Control;
        case Key::Alt:
        case Key::LeftAlt:
        case Key::RightAlt:
            return KeyMod::Alt;
        case Key::Super:
        case Key::LeftSuper:
        case Key::RightSuper:
            return KeyMod::Super;
        case Key::CapsLock:
            return KeyMod::CapsLock;
        case Key::NumLock:
            return KeyMod::NumLock;
        default:
            return KeyMod::None;
    }
}

//=============================================================================
// KeyboardEventDispatcher
//=============================================================================

KeyboardEventDispatcher::KeyboardEventDispatcher()
    : handler_count_(0)
    , needs_sort_(false)
{
    for (int i = 0; i < MAX_KEYBOARD_HANDLERS; i++) {
        handlers_[i] = nullptr;
    }
}

KeyboardEventDispatcher::~KeyboardEventDispatcher() {
    // Handlers are not owned by the dispatcher, so don't delete them
}

bool KeyboardEventDispatcher::add_handler(IKeyboardHandler* handler) {
    if (!handler) return false;

    // Check if already registered
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            return false; // Already registered
        }
    }

    // Check capacity
    if (handler_count_ >= MAX_KEYBOARD_HANDLERS) {
        return false;
    }

    handlers_[handler_count_++] = handler;
    needs_sort_ = true;
    return true;
}

bool KeyboardEventDispatcher::remove_handler(IKeyboardHandler* handler) {
    if (!handler) return false;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] == handler) {
            // Shift remaining handlers down
            for (int j = i; j < handler_count_ - 1; j++) {
                handlers_[j] = handlers_[j + 1];
            }
            handlers_[--handler_count_] = nullptr;
            return true;
        }
    }
    return false;
}

bool KeyboardEventDispatcher::remove_handler(const char* handler_id) {
    if (!handler_id) return false;

    IKeyboardHandler* handler = find_handler(handler_id);
    if (handler) {
        return remove_handler(handler);
    }
    return false;
}

int KeyboardEventDispatcher::get_handler_count() const {
    return handler_count_;
}

IKeyboardHandler* KeyboardEventDispatcher::get_handler(int index) const {
    if (index < 0 || index >= handler_count_) {
        return nullptr;
    }
    return handlers_[index];
}

IKeyboardHandler* KeyboardEventDispatcher::find_handler(const char* handler_id) const {
    if (!handler_id) return nullptr;

    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && strcmp(handlers_[i]->get_handler_id(), handler_id) == 0) {
            return handlers_[i];
        }
    }
    return nullptr;
}

void KeyboardEventDispatcher::sort_handlers() {
    if (!needs_sort_ || handler_count_ <= 1) {
        needs_sort_ = false;
        return;
    }

    // Simple insertion sort (handler count is small, max 16)
    for (int i = 1; i < handler_count_; i++) {
        IKeyboardHandler* key = handlers_[i];
        int key_priority = key->get_priority();
        int j = i - 1;

        // Sort in descending order of priority (higher priority first)
        while (j >= 0 && handlers_[j]->get_priority() < key_priority) {
            handlers_[j + 1] = handlers_[j];
            j--;
        }
        handlers_[j + 1] = key;
    }

    needs_sort_ = false;
}

bool KeyboardEventDispatcher::dispatch_key(const KeyEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    // Dispatch to handlers in priority order
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_key(event)) {
            return true; // Event consumed
        }
    }

    return false;
}

bool KeyboardEventDispatcher::dispatch_char(const CharEvent& event) {
    if (needs_sort_) {
        sort_handlers();
    }

    // Dispatch to handlers in priority order
    for (int i = 0; i < handler_count_; i++) {
        if (handlers_[i] && handlers_[i]->on_char(event)) {
            return true; // Event consumed
        }
    }

    return false;
}

//=============================================================================
// DefaultKeyboardDevice
//=============================================================================

DefaultKeyboardDevice::DefaultKeyboardDevice()
    : dispatcher_(nullptr)
    , window_(nullptr)
    , active_(true)
{
    for (int i = 0; i < MAX_KEY_STATES; i++) {
        key_states_[i] = false;
    }
}

DefaultKeyboardDevice::~DefaultKeyboardDevice() {
    // Dispatcher is not owned
}

const char* DefaultKeyboardDevice::get_device_id() const {
    return "default_keyboard";
}

bool DefaultKeyboardDevice::is_active() const {
    return active_;
}

void DefaultKeyboardDevice::set_dispatcher(KeyboardEventDispatcher* dispatcher) {
    dispatcher_ = dispatcher;
}

void DefaultKeyboardDevice::set_window(Window* window) {
    window_ = window;
}

void DefaultKeyboardDevice::inject_key_down(Key key, KeyMod modifiers, int scancode, bool repeat, double timestamp) {
    int index = static_cast<int>(key);
    if (index >= 0 && index < MAX_KEY_STATES) {
        key_states_[index] = true;
    }

    if (dispatcher_) {
        KeyEvent event;
        event.type = repeat ? EventType::KeyRepeat : EventType::KeyDown;
        event.window = window_;
        event.timestamp = timestamp;
        event.key = key;
        event.modifiers = modifiers;
        event.scancode = scancode;
        event.repeat = repeat;
        dispatcher_->dispatch_key(event);
    }
}

void DefaultKeyboardDevice::inject_key_up(Key key, KeyMod modifiers, int scancode, double timestamp) {
    int index = static_cast<int>(key);
    if (index >= 0 && index < MAX_KEY_STATES) {
        key_states_[index] = false;
    }

    if (dispatcher_) {
        KeyEvent event;
        event.type = EventType::KeyUp;
        event.window = window_;
        event.timestamp = timestamp;
        event.key = key;
        event.modifiers = modifiers;
        event.scancode = scancode;
        event.repeat = false;
        dispatcher_->dispatch_key(event);
    }
}

void DefaultKeyboardDevice::inject_char(uint32_t codepoint, KeyMod modifiers, double timestamp) {
    if (dispatcher_) {
        CharEvent event;
        event.type = EventType::CharInput;
        event.window = window_;
        event.timestamp = timestamp;
        event.codepoint = codepoint;
        event.modifiers = modifiers;
        dispatcher_->dispatch_char(event);
    }
}

bool DefaultKeyboardDevice::is_key_down(Key key) const {
    int index = static_cast<int>(key);
    if (index >= 0 && index < MAX_KEY_STATES) {
        return key_states_[index];
    }
    return false;
}

void DefaultKeyboardDevice::reset() {
    for (int i = 0; i < MAX_KEY_STATES; i++) {
        key_states_[i] = false;
    }
}

} // namespace input

//=============================================================================
// Global utility function implementations (for backward compatibility)
// These call into the input namespace versions
//=============================================================================

const char* key_to_string(Key key) {
    return input::key_to_string(key);
}

const char* mouse_button_to_string(MouseButton button) {
    return input::mouse_button_to_string(button);
}

const char* event_type_to_string(EventType type) {
    return input::event_type_to_string(type);
}

} // namespace window
