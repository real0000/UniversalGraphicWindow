/*
 * window.cpp - Platform-independent window utility functions
 */

#include "window.hpp"
#include <cstring>
#include <cctype>

namespace window {

//=============================================================================
// Helper Functions
//=============================================================================

static bool str_iequals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

//=============================================================================
// Cursor Type Utilities
//=============================================================================

const char* cursor_type_to_string(CursorType type) {
    switch (type) {
        case CursorType::Arrow: return "Arrow";
        case CursorType::IBeam: return "IBeam";
        case CursorType::Crosshair: return "Crosshair";
        case CursorType::Hand: return "Hand";
        case CursorType::ResizeH: return "ResizeH";
        case CursorType::ResizeV: return "ResizeV";
        case CursorType::ResizeNESW: return "ResizeNESW";
        case CursorType::ResizeNWSE: return "ResizeNWSE";
        case CursorType::ResizeAll: return "ResizeAll";
        case CursorType::NotAllowed: return "NotAllowed";
        case CursorType::Wait: return "Wait";
        case CursorType::WaitArrow: return "WaitArrow";
        case CursorType::Help: return "Help";
        case CursorType::Hidden: return "Hidden";
        case CursorType::Custom: return "Custom";
        default: return "Unknown";
    }
}

CursorType string_to_cursor_type(const char* str) {
    if (!str || !*str) return CursorType::Arrow;

    if (str_iequals(str, "Arrow") || str_iequals(str, "Default") || str_iequals(str, "Normal")) return CursorType::Arrow;
    if (str_iequals(str, "IBeam") || str_iequals(str, "Text") || str_iequals(str, "Caret")) return CursorType::IBeam;
    if (str_iequals(str, "Crosshair") || str_iequals(str, "Cross")) return CursorType::Crosshair;
    if (str_iequals(str, "Hand") || str_iequals(str, "Pointer") || str_iequals(str, "Link")) return CursorType::Hand;
    if (str_iequals(str, "ResizeH") || str_iequals(str, "ResizeHorizontal") || str_iequals(str, "SizeWE") || str_iequals(str, "EWResize")) return CursorType::ResizeH;
    if (str_iequals(str, "ResizeV") || str_iequals(str, "ResizeVertical") || str_iequals(str, "SizeNS") || str_iequals(str, "NSResize")) return CursorType::ResizeV;
    if (str_iequals(str, "ResizeNESW") || str_iequals(str, "SizeNESW") || str_iequals(str, "NESWResize")) return CursorType::ResizeNESW;
    if (str_iequals(str, "ResizeNWSE") || str_iequals(str, "SizeNWSE") || str_iequals(str, "NWSEResize")) return CursorType::ResizeNWSE;
    if (str_iequals(str, "ResizeAll") || str_iequals(str, "Move") || str_iequals(str, "SizeAll")) return CursorType::ResizeAll;
    if (str_iequals(str, "NotAllowed") || str_iequals(str, "No") || str_iequals(str, "Forbidden") || str_iequals(str, "Unavailable")) return CursorType::NotAllowed;
    if (str_iequals(str, "Wait") || str_iequals(str, "Busy") || str_iequals(str, "Loading")) return CursorType::Wait;
    if (str_iequals(str, "WaitArrow") || str_iequals(str, "AppStarting") || str_iequals(str, "Progress")) return CursorType::WaitArrow;
    if (str_iequals(str, "Help") || str_iequals(str, "Question")) return CursorType::Help;
    if (str_iequals(str, "Hidden") || str_iequals(str, "None") || str_iequals(str, "Invisible")) return CursorType::Hidden;
    if (str_iequals(str, "Custom")) return CursorType::Custom;

    return CursorType::Arrow;
}

//=============================================================================
// WindowStyle string conversion
//=============================================================================

static void trim_whitespace_internal(char* str) {
    if (!str) return;

    // Trim leading
    char* start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start++;
    }

    // Trim trailing
    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }

    // Move trimmed string to beginning
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
}

bool parse_window_style(const char* value, WindowStyle* out) {
    if (!value || !out) return false;

    // Parse as flags separated by |
    WindowStyle result = WindowStyle::None;
    char buffer[512];
    strncpy(buffer, value, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char* token = strtok(buffer, "|");
    while (token) {
        trim_whitespace_internal(token);
        if (strcmp(token, "none") == 0 || strcmp(token, "None") == 0) {
            // None doesn't add anything
        } else if (strcmp(token, "titlebar") == 0 || strcmp(token, "TitleBar") == 0) {
            result |= WindowStyle::TitleBar;
        } else if (strcmp(token, "border") == 0 || strcmp(token, "Border") == 0) {
            result |= WindowStyle::Border;
        } else if (strcmp(token, "closebutton") == 0 || strcmp(token, "CloseButton") == 0) {
            result |= WindowStyle::CloseButton;
        } else if (strcmp(token, "minimizebutton") == 0 || strcmp(token, "MinimizeButton") == 0) {
            result |= WindowStyle::MinimizeButton;
        } else if (strcmp(token, "maximizebutton") == 0 || strcmp(token, "MaximizeButton") == 0) {
            result |= WindowStyle::MaximizeButton;
        } else if (strcmp(token, "resizable") == 0 || strcmp(token, "Resizable") == 0) {
            result |= WindowStyle::Resizable;
        } else if (strcmp(token, "fullscreen") == 0 || strcmp(token, "Fullscreen") == 0) {
            result |= WindowStyle::Fullscreen;
        } else if (strcmp(token, "alwaysontop") == 0 || strcmp(token, "AlwaysOnTop") == 0) {
            result |= WindowStyle::AlwaysOnTop;
        } else if (strcmp(token, "toolwindow") == 0 || strcmp(token, "ToolWindow") == 0) {
            result |= WindowStyle::ToolWindow;
        } else if (strcmp(token, "default") == 0 || strcmp(token, "Default") == 0) {
            result |= WindowStyle::Default;
        } else if (strcmp(token, "borderless") == 0 || strcmp(token, "Borderless") == 0) {
            // Borderless is None, doesn't add anything
        } else if (strcmp(token, "fixedsize") == 0 || strcmp(token, "FixedSize") == 0) {
            result |= WindowStyle::FixedSize;
        }
        token = strtok(nullptr, "|");
    }

    *out = result;
    return true;
}

void window_style_to_string(WindowStyle style, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;

    buffer[0] = '\0';
    bool first = true;

    auto append_flag = [&](const char* name) {
        if (!first) {
            strncat(buffer, " | ", buffer_size - strlen(buffer) - 1);
        }
        strncat(buffer, name, buffer_size - strlen(buffer) - 1);
        first = false;
    };

    if (style == WindowStyle::None) {
        strncpy(buffer, "none", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    if (has_style(style, WindowStyle::TitleBar)) append_flag("titlebar");
    if (has_style(style, WindowStyle::Border)) append_flag("border");
    if (has_style(style, WindowStyle::CloseButton)) append_flag("closebutton");
    if (has_style(style, WindowStyle::MinimizeButton)) append_flag("minimizebutton");
    if (has_style(style, WindowStyle::MaximizeButton)) append_flag("maximizebutton");
    if (has_style(style, WindowStyle::Resizable)) append_flag("resizable");
    if (has_style(style, WindowStyle::Fullscreen)) append_flag("fullscreen");
    if (has_style(style, WindowStyle::AlwaysOnTop)) append_flag("alwaysontop");
    if (has_style(style, WindowStyle::ToolWindow)) append_flag("toolwindow");

    if (buffer[0] == '\0') {
        strncpy(buffer, "none", buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
}

const char* window_style_flag_to_string(WindowStyle flag) {
    switch (static_cast<uint32_t>(flag)) {
        case static_cast<uint32_t>(WindowStyle::None): return "none";
        case static_cast<uint32_t>(WindowStyle::TitleBar): return "titlebar";
        case static_cast<uint32_t>(WindowStyle::Border): return "border";
        case static_cast<uint32_t>(WindowStyle::CloseButton): return "closebutton";
        case static_cast<uint32_t>(WindowStyle::MinimizeButton): return "minimizebutton";
        case static_cast<uint32_t>(WindowStyle::MaximizeButton): return "maximizebutton";
        case static_cast<uint32_t>(WindowStyle::Resizable): return "resizable";
        case static_cast<uint32_t>(WindowStyle::Fullscreen): return "fullscreen";
        case static_cast<uint32_t>(WindowStyle::AlwaysOnTop): return "alwaysontop";
        case static_cast<uint32_t>(WindowStyle::ToolWindow): return "toolwindow";
        default: return "unknown";
    }
}

} // namespace window
