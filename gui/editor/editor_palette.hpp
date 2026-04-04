/*
 * editor_palette.hpp - Widget Palette Panel
 *
 * Displays available widget types that can be dragged onto the canvas.
 * Groups widgets by category (containers, controls, display, input, layout).
 */

#ifndef GUI_EDITOR_PALETTE_HPP
#define GUI_EDITOR_PALETTE_HPP

#include "gui/gui.hpp"
#include <vector>
#include <string>

namespace window {
namespace gui {
namespace editor {

// ============================================================================
// Palette item — one draggable widget type
// ============================================================================

struct PaletteItem {
    WidgetType type;
    const char* display_name;
    const char* icon_name;      // for renderer to resolve
    const char* category;
    const char* tooltip;
};

// ============================================================================
// EditorPalette — manages the palette panel UI
// ============================================================================

class EditorPalette {
public:
    void initialize(IGuiContext* ctx);
    void shutdown();

    // Build the palette items list
    void populate_items();

    // Get the root widget for embedding in the editor layout
    IGuiWidget* get_root_widget() const { return m_root; }

    // Currently dragged item (null if not dragging)
    const PaletteItem* get_dragging_item() const { return m_dragging; }
    bool is_dragging() const { return m_dragging != nullptr; }

    // Called by editor to handle drag state
    void begin_drag(int item_index);
    void end_drag();
    void cancel_drag();

    // Access items
    int get_item_count() const { return static_cast<int>(m_items.size()); }
    const PaletteItem& get_item(int index) const { return m_items[index]; }

    // Category management
    int get_category_count() const { return static_cast<int>(m_categories.size()); }
    const char* get_category(int index) const { return m_categories[index].c_str(); }
    bool is_category_expanded(int index) const;
    void toggle_category(int index);

    // Update (for hover/drag feedback)
    void update(float dt, const math::Vec2& mouse_pos);

    // Get the hovered item index (-1 if none)
    int get_hovered_item() const { return m_hovered_item; }

private:
    IGuiContext* m_ctx = nullptr;
    IGuiWidget* m_root = nullptr;

    std::vector<PaletteItem> m_items;
    std::vector<std::string> m_categories;
    std::vector<bool> m_category_expanded;

    const PaletteItem* m_dragging = nullptr;
    int m_hovered_item = -1;
};

} // namespace editor
} // namespace gui
} // namespace window

#endif // GUI_EDITOR_PALETTE_HPP
