/*
 * gui.cpp - GUI Interface Utilities
 *
 * Contains string conversion and common utility functions.
 * Abstract interfaces are implemented by specific backends.
 */

#include "gui.hpp"

namespace window {
namespace gui {

// ============================================================================
// String Conversion Functions
// ============================================================================

const char* gui_result_to_string(GuiResult result) {
    switch (result) {
        case GuiResult::Success:              return "Success";
        case GuiResult::ErrorUnknown:         return "Unknown error";
        case GuiResult::ErrorNotInitialized:  return "Not initialized";
        case GuiResult::ErrorInvalidParameter: return "Invalid parameter";
        case GuiResult::ErrorOutOfMemory:     return "Out of memory";
        case GuiResult::ErrorWidgetNotFound:  return "Widget not found";
        case GuiResult::ErrorLayoutFailed:    return "Layout failed";
        case GuiResult::ErrorViewportNotFound: return "Viewport not found";
        default:                              return "Unknown";
    }
}

const char* widget_type_to_string(WidgetType type) {
    switch (type) {
        case WidgetType::Custom:      return "Custom";
        case WidgetType::Container:   return "Container";
        case WidgetType::Panel:       return "Panel";
        case WidgetType::Button:      return "Button";
        case WidgetType::Label:       return "Label";
        case WidgetType::TextInput:   return "TextInput";
        case WidgetType::Checkbox:    return "Checkbox";
        case WidgetType::RadioButton: return "RadioButton";
        case WidgetType::Slider:      return "Slider";
        case WidgetType::ProgressBar: return "ProgressBar";
        case WidgetType::ScrollArea:  return "ScrollArea";
        case WidgetType::ListBox:     return "ListBox";
        case WidgetType::ComboBox:    return "ComboBox";
        case WidgetType::TabControl:  return "TabControl";
        case WidgetType::TreeView:    return "TreeView";
        case WidgetType::Image:       return "Image";
        case WidgetType::Separator:   return "Separator";
        case WidgetType::Spacer:      return "Spacer";
        default:                      return "Unknown";
    }
}

const char* widget_state_to_string(WidgetState state) {
    switch (state) {
        case WidgetState::Normal:   return "Normal";
        case WidgetState::Hovered:  return "Hovered";
        case WidgetState::Pressed:  return "Pressed";
        case WidgetState::Focused:  return "Focused";
        case WidgetState::Disabled: return "Disabled";
        default:                    return "Unknown";
    }
}

const char* gui_event_type_to_string(GuiEventType type) {
    switch (type) {
        case GuiEventType::None:             return "None";
        case GuiEventType::Click:            return "Click";
        case GuiEventType::DoubleClick:      return "DoubleClick";
        case GuiEventType::Hover:            return "Hover";
        case GuiEventType::Focus:            return "Focus";
        case GuiEventType::Blur:             return "Blur";
        case GuiEventType::ValueChanged:     return "ValueChanged";
        case GuiEventType::TextChanged:      return "TextChanged";
        case GuiEventType::SelectionChanged: return "SelectionChanged";
        case GuiEventType::DragStart:        return "DragStart";
        case GuiEventType::DragMove:         return "DragMove";
        case GuiEventType::DragEnd:          return "DragEnd";
        case GuiEventType::Scroll:           return "Scroll";
        case GuiEventType::KeyPress:         return "KeyPress";
        case GuiEventType::KeyRelease:       return "KeyRelease";
        case GuiEventType::Resize:           return "Resize";
        case GuiEventType::Close:            return "Close";
        default:                             return "Unknown";
    }
}

} // namespace gui
} // namespace window
