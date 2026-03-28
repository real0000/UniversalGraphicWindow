/*
 * gui.cpp - GUI Interface Utilities
 *
 * Contains string conversion and common utility functions.
 * Abstract interfaces are implemented by specific backends.
 */

#include "gui.hpp"
#include <algorithm>
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
// WidgetRenderInfo Implementation
// ============================================================================

void WidgetRenderInfo::finalize() {
    draw_order_.clear();
    batch_spans_.clear();

    // Collect a DrawRef from every entry in each typed pool, caching clip for the renderer.
    for (int32_t i = 0; i < (int32_t)colors.size(); ++i)
        draw_order_.push_back({DrawRef::Pool::Color,   i, colors[i].depth,   colors[i].clip,   false});
    for (int32_t i = 0; i < (int32_t)textures.size(); ++i)
        draw_order_.push_back({DrawRef::Pool::Texture, i, textures[i].depth, textures[i].clip, false});
    for (int32_t i = 0; i < (int32_t)slices.size(); ++i)
        draw_order_.push_back({DrawRef::Pool::Slice9,  i, slices[i].depth,   slices[i].clip,   false});
    for (int32_t i = 0; i < (int32_t)texts.size(); ++i)
        draw_order_.push_back({DrawRef::Pool::Text,    i, texts[i].depth,    texts[i].clip,    false});

    // Stable sort by depth — equal-depth entries preserve insertion order.
    std::stable_sort(draw_order_.begin(), draw_order_.end(),
        [](const DrawRef& a, const DrawRef& b) { return a.depth < b.depth; });

    // Mark clip_changed: first entry always triggers a clip update; subsequent entries
    // only when the clip rect differs from the previous entry after depth-sort.
    if (!draw_order_.empty()) {
        draw_order_[0].clip_changed = true;
        for (size_t i = 1; i < draw_order_.size(); ++i) {
            const math::Box& prev = draw_order_[i - 1].clip;
            const math::Box& cur  = draw_order_[i].clip;
            draw_order_[i].clip_changed =
                math::x(math::box_min(cur))  != math::x(math::box_min(prev)) ||
                math::y(math::box_min(cur))  != math::y(math::box_min(prev)) ||
                math::box_width(cur)         != math::box_width(prev)        ||
                math::box_height(cur)        != math::box_height(prev);
        }
    }

    // Build BatchSpan groups for consecutive entries that can share a draw call.
    // Two refs are in the same batch when they use the same pool type AND the
    // same texture key (atlas layer / file path), or are both solid-colour.
    auto same_batch = [&](const DrawRef& a, const DrawRef& b) -> bool {
        if (a.pool != b.pool) return false;
        switch (a.pool) {
            case DrawRef::Pool::Color:
                return true; // All solid draws share the "no-texture" state.
            case DrawRef::Pool::Texture: {
                const TextureCmd& ta = textures[a.index];
                const TextureCmd& tb = textures[b.index];
                if (ta.source_type != tb.source_type) return false;
                if (ta.source_type == TextureSourceType::Memory)
                    return ta.atlas_layer == tb.atlas_layer
                        && ta.memory_data == tb.memory_data;
                if (ta.source_type == TextureSourceType::File)
                    return ta.file_path == tb.file_path
                        || (ta.file_path && tb.file_path
                            && std::strcmp(ta.file_path, tb.file_path) == 0);
                return true; // Generated textures batch together.
            }
            case DrawRef::Pool::Slice9:
                return slices[a.index].atlas_layer == slices[b.index].atlas_layer;
            default:
                return false;
        }
    };

    if (!draw_order_.empty()) {
        BatchSpan current{0, 1};
        for (size_t i = 1; i < draw_order_.size(); ++i) {
            if (same_batch(draw_order_[i - 1], draw_order_[i])) {
                current.count++;
            } else {
                batch_spans_.push_back(current);
                current = {static_cast<int32_t>(i), 1};
            }
        }
        batch_spans_.push_back(current);
    }

    valid_ = true;
}

// ============================================================================
// flatten() — expand Slice9 + Text into Color + Texture primitives
// ============================================================================

void WidgetRenderInfo::flatten(IGuiTextRasterizer* rasterizer) {
    using namespace math;

    // ---- Expand Slice9 entries ----
    for (const auto& s : slices) {
        float px = x(box_min(s.dest)), py = y(box_min(s.dest));
        float pw = box_width(s.dest),  ph = box_height(s.dest);
        float bl = std::min(s.border.left,   pw * 0.5f);
        float br = std::min(s.border.right,  pw * 0.5f);
        float bt = std::min(s.border.top,    ph * 0.5f);
        float bb = std::min(s.border.bottom, ph * 0.5f);
        float cx = px + bl,       cy = py + bt;
        float cw = pw - bl - br,  ch = ph - bt - bb;

        if (s.atlas_layer < 0) {
            // Solid 9-slice → Color rects
            auto emit = [&](float rx, float ry, float rw, float rh) {
                if (rw > 0 && rh > 0)
                    colors.push_back({make_box(rx,ry,rw,rh), s.color,
                                      DrawShape::Rect, s.depth, s.clip});
            };
            // Top
            emit(px, py, bl, bt);
            emit(cx, py, cw, bt);
            emit(px+pw-br, py, br, bt);
            // Middle
            if (ch > 0) {
                emit(px, cy, bl, ch);
                if (s.center_mode != SliceCenterMode::Hidden)
                    emit(cx, cy, cw, ch);
                emit(px+pw-br, cy, br, ch);
            }
            // Bottom
            emit(px, py+ph-bb, bl, bb);
            emit(cx, py+ph-bb, cw, bb);
            emit(px+pw-br, py+ph-bb, br, bb);
        } else {
            // Textured 9-slice → Texture quads
            float u0 = x(box_min(s.uv)), v0 = y(box_min(s.uv));
            float u1 = x(box_max(s.uv)), v1 = y(box_max(s.uv));
            float uw = u1 - u0, vh = v1 - v0;
            float ssw = x(s.source_size), ssh = y(s.source_size);
            float ucl, ucr, vct, vcb;
            if (ssw > 0 && ssh > 0) {
                ucl = u0 + (bl / ssw) * uw;
                ucr = u1 - (br / ssw) * uw;
                vct = v0 + (bt / ssh) * vh;
                vcb = v1 - (bb / ssh) * vh;
            } else {
                ucl = u0 + uw * 0.25f; ucr = u0 + uw * 0.75f;
                vct = v0 + vh * 0.25f; vcb = v0 + vh * 0.75f;
            }
            auto quad = [&](float dx, float dy, float dw, float dh,
                            float su0, float sv0, float su1, float sv1) {
                if (dw > 0 && dh > 0)
                    textures.push_back({TextureSourceType::Memory, nullptr, nullptr, 0,
                                        s.atlas_layer,
                                        make_box(dx,dy,dw,dh),
                                        make_box(su0,sv0,su1-su0,sv1-sv0),
                                        s.tint, s.depth, s.clip});
            };
            // Top
            quad(px,         py,         bl,   bt,   u0,  v0,  ucl, vct);
            quad(cx,         py,         cw,   bt,   ucl, v0,  ucr, vct);
            quad(px+pw-br,   py,         br,   bt,   ucr, v0,  u1,  vct);
            // Middle
            if (ch > 0) {
                quad(px,       cy, bl, ch, u0,  vct, ucl, vcb);
                if (s.center_mode != SliceCenterMode::Hidden)
                    quad(cx,   cy, cw, ch, ucl, vct, ucr, vcb);
                quad(px+pw-br, cy, br, ch, ucr, vct, u1,  vcb);
            }
            // Bottom
            quad(px,         py+ph-bb, bl, bb, u0,  vcb, ucl, v1);
            quad(cx,         py+ph-bb, cw, bb, ucl, vcb, ucr, v1);
            quad(px+pw-br,   py+ph-bb, br, bb, ucr, vcb, u1,  v1);
        }
    }
    slices.clear();

    // ---- Expand Text entries ----
    if (rasterizer) {
        float time = rasterizer->get_time();
        for (const auto& t : texts) {
            float px = x(box_min(t.dest)), py = y(box_min(t.dest));
            float pw = box_width(t.dest),  ph = box_height(t.dest);

            // Rasterize text body
            if (!t.text.empty()) {
                auto q = rasterizer->rasterize(t.text.c_str(), t.font_size, nullptr);
                if (q.valid()) {
                    float tx = px, ty = py;
                    // Alignment
                    switch (t.alignment) {
                        case Alignment::Center:
                            tx = px + (pw - q.width) * 0.5f;
                            ty = py + (ph - q.height) * 0.5f;
                            break;
                        case Alignment::CenterRight:
                            tx = px + pw - q.width;
                            ty = py + (ph - q.height) * 0.5f;
                            break;
                        default: // CenterLeft and others
                            tx = px + 2;
                            ty = py + (ph - q.height) * 0.5f;
                            break;
                    }
                    textures.push_back({TextureSourceType::Memory, nullptr, nullptr, 0,
                                        q.atlas_layer,
                                        make_box(tx, ty, (float)q.width, (float)q.height),
                                        make_box(q.u0, q.v0, q.u1 - q.u0, q.v1 - q.v0),
                                        Vec4(t.color.x, t.color.y, t.color.z, t.color.w),
                                        t.depth, t.clip});
                }
            }

            // Selection highlight
            if (t.sel_start >= 0 && t.sel_end > t.sel_start) {
                float sx = px + 2 + rasterizer->measure_advance(t.text.c_str(), t.sel_start,
                                                                  t.font_size, nullptr);
                float ex = px + 2 + rasterizer->measure_advance(t.text.c_str(), t.sel_end,
                                                                  t.font_size, nullptr);
                colors.push_back({make_box(sx, py + 2, ex - sx, ph - 4),
                                  t.sel_bg_color, DrawShape::Rect, t.depth - 1, t.clip});
            }

            // Blinking cursor
            if (t.show_cursor && fmodf(time, 1.0f) < 0.5f) {
                float cx_pos = px + 2 + rasterizer->measure_advance(
                    t.text.c_str(), t.cursor_pos, t.font_size, nullptr);
                colors.push_back({make_box(cx_pos, py + 3, 1.0f, ph - 6),
                                  t.cursor_color, DrawShape::Rect, t.depth + 1, t.clip});
            }
        }
    }
    texts.clear();

    // Re-finalize draw order with only Color + Texture entries
    finalize();
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
