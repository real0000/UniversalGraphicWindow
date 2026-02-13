/*
 * gui.cpp - GUI Interface Utilities
 *
 * Contains string conversion and common utility functions.
 * Abstract interfaces are implemented by specific backends.
 */

#include "gui.hpp"
#include <cmath>

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

const char* scroll_view_size_to_string(ScrollViewSize size) {
    switch (size) {
        case ScrollViewSize::Compact: return "Compact";
        case ScrollViewSize::Small:   return "Small";
        case ScrollViewSize::Medium:  return "Medium";
        case ScrollViewSize::Large:   return "Large";
        case ScrollViewSize::Custom:  return "Custom";
        default:                      return "Unknown";
    }
}

// ============================================================================
// Viewport Implementation
// ============================================================================

math::Vec2 Viewport::to_ui(const math::Vec2& p) const {
    return math::Vec2((math::x(p) / scale) + math::x(bounds.min_corner()) - math::x(offset),
                 (math::y(p) / scale) + math::y(bounds.min_corner()) - math::y(offset));
}

math::Vec2 Viewport::to_viewport(const math::Vec2& p) const {
    return math::Vec2((math::x(p) - math::x(bounds.min_corner()) + math::x(offset)) * scale,
                 (math::y(p) - math::y(bounds.min_corner()) + math::y(offset)) * scale);
}

math::Box Viewport::get_visible_box() const {
    float bx = math::x(bounds.min_corner()) - math::x(offset);
    float by = math::y(bounds.min_corner()) - math::y(offset);
    return math::make_box(bx, by, math::box_width(bounds) / scale, math::box_height(bounds) / scale);
}

// ============================================================================
// SliceBorder Implementation
// ============================================================================

bool SliceBorder::is_zero() const {
    return left == 0.0f && top == 0.0f && right == 0.0f && bottom == 0.0f;
}

SliceBorder SliceBorder::uniform(float size) {
    return { size, size, size, size };
}

// ============================================================================
// TextureEntry Implementation
// ============================================================================

bool TextureEntry::same_texture(const TextureEntry& other) const {
    if (source_type != other.source_type) return false;
    switch (source_type) {
        case TextureSourceType::File:
            return file_path == other.file_path ||
                   (file_path && other.file_path && std::strcmp(file_path, other.file_path) == 0);
        case TextureSourceType::Memory:
            return memory_data == other.memory_data && memory_size == other.memory_size;
        case TextureSourceType::Generated:
            return true;  // Generated textures batch together
        default:
            return false;
    }
}

// ============================================================================
// WidgetRenderInfo Implementation
// ============================================================================

void WidgetRenderInfo::sort_and_batch() {
    if (textures.empty()) {
        batches.clear();
        return;
    }

    // Sort by depth first, then group by texture source for batching
    std::sort(textures.begin(), textures.end(),
        [](const TextureEntry& a, const TextureEntry& b) {
            if (a.depth != b.depth) return a.depth < b.depth;
            // Secondary sort by source type and pointer for grouping
            if (a.source_type != b.source_type)
                return static_cast<int>(a.source_type) < static_cast<int>(b.source_type);
            if (a.source_type == TextureSourceType::File)
                return a.file_path < b.file_path;
            if (a.source_type == TextureSourceType::Memory)
                return a.memory_data < b.memory_data;
            return false;
        });

    // Build batches
    batches.clear();
    RenderBatch current_batch;
    current_batch.source_type = textures[0].source_type;
    current_batch.file_path = textures[0].file_path;
    current_batch.memory_data = textures[0].memory_data;
    current_batch.memory_size = textures[0].memory_size;
    current_batch.start_index = 0;
    current_batch.count = 1;

    for (size_t i = 1; i < textures.size(); ++i) {
        const auto& entry = textures[i];
        // Same texture source can be batched
        if (entry.same_texture(textures[i - 1])) {
            current_batch.count++;
        } else {
            batches.push_back(current_batch);
            current_batch.source_type = entry.source_type;
            current_batch.file_path = entry.file_path;
            current_batch.memory_data = entry.memory_data;
            current_batch.memory_size = entry.memory_size;
            current_batch.start_index = static_cast<int32_t>(i);
            current_batch.count = 1;
        }
    }
    batches.push_back(current_batch);
}

// ============================================================================
// Animation String Conversion Functions
// ============================================================================

const char* animation_easing_to_string(AnimationEasing easing) {
    switch (easing) {
        case AnimationEasing::Linear:         return "Linear";
        case AnimationEasing::EaseIn:         return "EaseIn";
        case AnimationEasing::EaseOut:        return "EaseOut";
        case AnimationEasing::EaseInOut:      return "EaseInOut";
        case AnimationEasing::EaseInQuad:     return "EaseInQuad";
        case AnimationEasing::EaseOutQuad:    return "EaseOutQuad";
        case AnimationEasing::EaseInOutQuad:  return "EaseInOutQuad";
        case AnimationEasing::EaseInCubic:    return "EaseInCubic";
        case AnimationEasing::EaseOutCubic:   return "EaseOutCubic";
        case AnimationEasing::EaseInOutCubic: return "EaseInOutCubic";
        case AnimationEasing::EaseInElastic:  return "EaseInElastic";
        case AnimationEasing::EaseOutElastic: return "EaseOutElastic";
        case AnimationEasing::EaseInOutElastic: return "EaseInOutElastic";
        case AnimationEasing::EaseInBounce:   return "EaseInBounce";
        case AnimationEasing::EaseOutBounce:  return "EaseOutBounce";
        case AnimationEasing::EaseInOutBounce: return "EaseInOutBounce";
        default:                              return "Unknown";
    }
}

const char* animation_state_to_string(AnimationState state) {
    switch (state) {
        case AnimationState::Idle:      return "Idle";
        case AnimationState::Playing:   return "Playing";
        case AnimationState::Paused:    return "Paused";
        case AnimationState::Completed: return "Completed";
        default:                        return "Unknown";
    }
}

const char* animation_target_to_string(AnimationTarget target) {
    switch (target) {
        case AnimationTarget::PositionX: return "PositionX";
        case AnimationTarget::PositionY: return "PositionY";
        case AnimationTarget::Position:  return "Position";
        case AnimationTarget::Width:     return "Width";
        case AnimationTarget::Height:    return "Height";
        case AnimationTarget::Size:      return "Size";
        case AnimationTarget::Opacity:   return "Opacity";
        case AnimationTarget::ColorR:    return "ColorR";
        case AnimationTarget::ColorG:    return "ColorG";
        case AnimationTarget::ColorB:    return "ColorB";
        case AnimationTarget::ColorA:    return "ColorA";
        case AnimationTarget::Color:     return "Color";
        case AnimationTarget::Rotation:  return "Rotation";
        case AnimationTarget::ScaleX:    return "ScaleX";
        case AnimationTarget::ScaleY:    return "ScaleY";
        case AnimationTarget::Scale:     return "Scale";
        default:                         return "Unknown";
    }
}

const char* animation_loop_to_string(AnimationLoop loop) {
    switch (loop) {
        case AnimationLoop::None:     return "None";
        case AnimationLoop::Loop:     return "Loop";
        case AnimationLoop::PingPong: return "PingPong";
        default:                      return "Unknown";
    }
}

// ============================================================================
// Page String Conversion Functions
// ============================================================================

const char* page_transition_type_to_string(PageTransitionType type) {
    switch (type) {
        case PageTransitionType::None:       return "None";
        case PageTransitionType::Fade:       return "Fade";
        case PageTransitionType::SlideLeft:  return "SlideLeft";
        case PageTransitionType::SlideRight: return "SlideRight";
        case PageTransitionType::SlideUp:    return "SlideUp";
        case PageTransitionType::SlideDown:  return "SlideDown";
        case PageTransitionType::Push:       return "Push";
        case PageTransitionType::Pop:        return "Pop";
        case PageTransitionType::Zoom:       return "Zoom";
        case PageTransitionType::Flip:       return "Flip";
        case PageTransitionType::Custom:     return "Custom";
        default:                             return "Unknown";
    }
}

const char* page_state_to_string(PageState state) {
    switch (state) {
        case PageState::Hidden:   return "Hidden";
        case PageState::Entering: return "Entering";
        case PageState::Active:   return "Active";
        case PageState::Leaving:  return "Leaving";
        case PageState::Paused:   return "Paused";
        default:                  return "Unknown";
    }
}

// ============================================================================
// Easing Functions
// ============================================================================

static float ease_out_bounce(float t) {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    } else if (t < 2.0f / 2.75f) {
        t -= 1.5f / 2.75f;
        return 7.5625f * t * t + 0.75f;
    } else if (t < 2.5f / 2.75f) {
        t -= 2.25f / 2.75f;
        return 7.5625f * t * t + 0.9375f;
    } else {
        t -= 2.625f / 2.75f;
        return 7.5625f * t * t + 0.984375f;
    }
}

float apply_easing(AnimationEasing easing, float t) {
    // Clamp t to [0, 1]
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    constexpr float PI = 3.14159265358979323846f;

    switch (easing) {
        case AnimationEasing::Linear:
            return t;

        case AnimationEasing::EaseIn:
        case AnimationEasing::EaseInQuad:
            return t * t;

        case AnimationEasing::EaseOut:
        case AnimationEasing::EaseOutQuad:
            return t * (2.0f - t);

        case AnimationEasing::EaseInOut:
        case AnimationEasing::EaseInOutQuad:
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

        case AnimationEasing::EaseInCubic:
            return t * t * t;

        case AnimationEasing::EaseOutCubic: {
            float f = t - 1.0f;
            return f * f * f + 1.0f;
        }

        case AnimationEasing::EaseInOutCubic:
            return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;

        case AnimationEasing::EaseInElastic: {
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            float p = 0.3f;
            float s = p / 4.0f;
            float post = std::pow(2.0f, 10.0f * (t - 1.0f));
            return -(post * std::sin((t - 1.0f - s) * (2.0f * PI) / p));
        }

        case AnimationEasing::EaseOutElastic: {
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            float p = 0.3f;
            float s = p / 4.0f;
            return std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2.0f * PI) / p) + 1.0f;
        }

        case AnimationEasing::EaseInOutElastic: {
            if (t == 0.0f) return 0.0f;
            if (t == 1.0f) return 1.0f;
            float p = 0.45f;
            float s = p / 4.0f;
            t = t * 2.0f;
            if (t < 1.0f) {
                float post = std::pow(2.0f, 10.0f * (t - 1.0f));
                return -0.5f * (post * std::sin((t - 1.0f - s) * (2.0f * PI) / p));
            }
            float post = std::pow(2.0f, -10.0f * (t - 1.0f));
            return post * std::sin((t - 1.0f - s) * (2.0f * PI) / p) * 0.5f + 1.0f;
        }

        case AnimationEasing::EaseInBounce:
            return 1.0f - ease_out_bounce(1.0f - t);

        case AnimationEasing::EaseOutBounce:
            return ease_out_bounce(t);

        case AnimationEasing::EaseInOutBounce:
            return t < 0.5f
                ? (1.0f - ease_out_bounce(1.0f - 2.0f * t)) * 0.5f
                : (1.0f + ease_out_bounce(2.0f * t - 1.0f)) * 0.5f;

        default:
            return t;
    }
}

} // namespace gui
} // namespace window
