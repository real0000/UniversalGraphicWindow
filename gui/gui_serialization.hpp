/*
 * gui_serialization.hpp - GUI Save/Load Interface
 *
 * Provides serialization of GUI widget trees to JSON and binary formats.
 * Includes version info for backward-compatible deserialization.
 *
 * Serializes: widget hierarchy, properties, styles, items (list/tree/tab/menu),
 * sizer layout, and animation definitions.  Does NOT serialize runtime state
 * (focus, hover, pressed, scroll inertia) or callback pointers.
 *
 * Usage:
 *   // Save
 *   GuiSerializeResult r = gui_save(context, "layout.json", GuiSerializeFormat::Json);
 *   GuiSerializeResult r = gui_save(context, "layout.bin",  GuiSerializeFormat::Binary);
 *
 *   // Save to memory buffer
 *   std::vector<uint8_t> buf;
 *   GuiSerializeResult r = gui_save(context, buf, GuiSerializeFormat::Json);
 *
 *   // Load
 *   GuiSerializeResult r = gui_load(context, "layout.json", GuiSerializeFormat::Json);
 *   GuiSerializeResult r = gui_load(context, buf.data(), buf.size(), GuiSerializeFormat::Binary);
 */

#ifndef WINDOW_GUI_SERIALIZATION_HPP
#define WINDOW_GUI_SERIALIZATION_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace window {
namespace gui {

// Forward declarations
class IGuiContext;
class IGuiWidget;

// ============================================================================
// Version
// ============================================================================

// Format version is stored in every serialized file/buffer.
// Bump MINOR for additive changes (new widget types, new fields with defaults).
// Bump MAJOR only for breaking layout changes.
static constexpr uint16_t GUI_SERIALIZE_VERSION_MAJOR = 1;
static constexpr uint16_t GUI_SERIALIZE_VERSION_MINOR = 0;

struct GuiSerializeVersion {
    uint16_t major = GUI_SERIALIZE_VERSION_MAJOR;
    uint16_t minor = GUI_SERIALIZE_VERSION_MINOR;

    bool is_compatible(const GuiSerializeVersion& file_ver) const {
        // Same major = compatible; loader handles missing fields from older minors.
        return file_ver.major == major;
    }
};

// ============================================================================
// Format
// ============================================================================

enum class GuiSerializeFormat : uint8_t {
    Json   = 0,     // Human-readable JSON
    Binary = 1      // Compact binary (little-endian)
};

// ============================================================================
// Result
// ============================================================================

enum class GuiSerializeResult : uint8_t {
    Success = 0,
    ErrorFileOpen,          // Could not open file for read/write
    ErrorFileWrite,         // Write failed (disk full, permission)
    ErrorFileRead,          // Read failed (truncated, I/O error)
    ErrorParse,             // JSON parse error or binary corruption
    ErrorVersionMismatch,   // Major version mismatch
    ErrorInvalidData,       // Logical error (unknown widget type, bad reference)
    ErrorOutOfMemory,
    ErrorInvalidParameter   // null context, empty path, etc.
};

const char* gui_serialize_result_to_string(GuiSerializeResult result);

// ============================================================================
// Options
// ============================================================================

struct GuiSerializeOptions {
    // Save options
    bool save_styles         = true;    // Include per-widget style overrides
    bool save_items          = true;    // Include list/tree/tab/menu items
    bool save_animations     = true;    // Include animation definitions
    bool save_sizers         = true;    // Include sizer layout info
    bool pretty_print        = true;    // JSON: human-readable indentation
    int  json_indent         = 2;       // JSON: spaces per indent level

    // Load options
    bool clear_before_load   = true;    // Destroy existing widgets before loading
    bool skip_unknown_fields = true;    // Ignore fields not recognized (forward compat)
    bool skip_unknown_types  = true;    // Skip widgets with unknown type (forward compat)
};

// ============================================================================
// File-based API
// ============================================================================

// Save the entire GUI context (root widget tree + overlays) to a file.
GuiSerializeResult gui_save(const IGuiContext* context,
                            const char* filepath,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options = {});

// Load a GUI layout from a file into an existing context.
GuiSerializeResult gui_load(IGuiContext* context,
                            const char* filepath,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options = {});

// ============================================================================
// Memory-buffer API
// ============================================================================

// Save to an in-memory buffer (appended to `out_buffer`).
GuiSerializeResult gui_save(const IGuiContext* context,
                            std::vector<uint8_t>& out_buffer,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options = {});

// Load from an in-memory buffer.
GuiSerializeResult gui_load(IGuiContext* context,
                            const uint8_t* data, size_t size,
                            GuiSerializeFormat format,
                            const GuiSerializeOptions& options = {});

// ============================================================================
// Single-widget API (save/load a subtree)
// ============================================================================

// Save a single widget and its entire subtree.
GuiSerializeResult gui_save_widget(const IGuiWidget* widget,
                                   std::vector<uint8_t>& out_buffer,
                                   GuiSerializeFormat format,
                                   const GuiSerializeOptions& options = {});

// Load a widget subtree and return the root.  Caller owns the returned widget
// and must eventually call context->destroy_widget() on it.
GuiSerializeResult gui_load_widget(IGuiContext* context,
                                   const uint8_t* data, size_t size,
                                   GuiSerializeFormat format,
                                   IGuiWidget** out_widget,
                                   const GuiSerializeOptions& options = {});

} // namespace gui
} // namespace window

#endif // WINDOW_GUI_SERIALIZATION_HPP
