/*
 * gui_enums.hpp - Foundational GUI enums
 *
 * The small, dependency-free enums the rest of the GUI (and the centralized
 * style structs in gui_styles.hpp) build on. Split out of gui.hpp so gui_styles.hpp
 * can be a normal top-level header (with its own namespace) that still sees
 * Alignment/WidgetType etc. — gui.hpp includes this before opening its namespace.
 */

#ifndef WINDOW_GUI_ENUMS_HPP
#define WINDOW_GUI_ENUMS_HPP

#include <cstdint>

namespace window {
namespace gui {

enum class GuiResult : uint8_t {
    Success = 0,
    ErrorUnknown,
    ErrorNotInitialized,
    ErrorInvalidParameter,
    ErrorOutOfMemory,
    ErrorWidgetNotFound,
    ErrorLayoutFailed,
    ErrorViewportNotFound
};

enum class Alignment : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

enum class LayoutDirection : uint8_t {
    Horizontal = 0,
    Vertical
};

enum class SizeMode : uint8_t {
    Fixed = 0,      // Fixed pixel size
    Relative,       // Relative to parent (0.0-1.0)
    Auto,           // Size to content
    Fill            // Fill remaining space
};

enum class WidgetState : uint8_t {
    Normal = 0,
    Hovered,
    Pressed,
    Focused,
    Disabled
};

enum class WidgetType : uint8_t {
    Custom = 0,
    Container,
    Panel,
    Button,
    Label,          // Text display
    TextInput,      // Editable text
    Checkbox,
    RadioButton,
    Slider,
    ProgressBar,
    ScrollArea,
    ListBox,
    ComboBox,
    TabControl,
    TreeView,
    Image,
    Separator,
    Spacer,
    CanvasView      // pannable/zoomable world-coordinate container (gui_canvas.hpp)
};

enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    X1,
    X2
};

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_ENUMS_HPP
