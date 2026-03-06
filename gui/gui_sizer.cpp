/*
 * gui_sizer.cpp - Sizer Layout Implementations
 */

#include "gui_widget_base.hpp"
#include "gui_sizer.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace window {
namespace gui {

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

// Return the preferred (or explicitly overridden) size of an item.
static math::Vec2 item_pref_size(const SizerItem& item) {
    if (item.sizer)  return item.sizer->get_min_size();
    if (!item.widget) return item.fixed_size;
    auto pref = item.widget->get_preferred_size();
    float w = (math::x(item.fixed_size) > 0.0f) ? math::x(item.fixed_size) : math::x(pref);
    float h = (math::y(item.fixed_size) > 0.0f) ? math::y(item.fixed_size) : math::y(pref);
    return math::Vec2(w, h);
}

// Resolve border amounts for each side.
struct BorderSides { float left, right, top, bottom; };

static BorderSides resolve_border(const SizerItem& item) {
    BorderSides b{};
    b.left   = has_flag(item.flags, SizerFlag::Left)   ? item.border : 0.0f;
    b.right  = has_flag(item.flags, SizerFlag::Right)  ? item.border : 0.0f;
    b.top    = has_flag(item.flags, SizerFlag::Top)    ? item.border : 0.0f;
    b.bottom = has_flag(item.flags, SizerFlag::Bottom) ? item.border : 0.0f;
    return b;
}

// Apply final bounds to an item's widget or nested sizer.
static void item_set_bounds(SizerItem& item, const math::Box& bounds) {
    if (item.widget) {
        item.widget->set_bounds(bounds);
    } else if (item.sizer) {
        item.sizer->set_bounds(bounds);
        item.sizer->layout();
    }
}

} // namespace

// ============================================================================
// SizerBase - shared storage and ISizer boilerplate
// ============================================================================

class SizerBase : public ISizer {
protected:
    std::vector<SizerItem> items_;
    math::Box   bounds_;
    math::Vec4  padding_;   // x=left, y=top, z=right, w=bottom
    float       gap_ = 4.0f;

    // Content rectangle (inside padding).
    math::Box content_rect() const {
        float bx = math::x(math::box_min(bounds_));
        float by = math::y(math::box_min(bounds_));
        float bw = math::box_width(bounds_);
        float bh = math::box_height(bounds_);
        return math::make_box(bx + padding_.x,
                              by + padding_.y,
                              bw - padding_.x - padding_.z,
                              bh - padding_.y - padding_.w);
    }

public:
    // --- ISizer: adding items ---

    void add(IGuiWidget* widget, int proportion, SizerFlag flags, float border) override {
        SizerItem item;
        item.widget     = widget;
        item.proportion = proportion;
        item.flags      = flags;
        item.border     = border;
        items_.push_back(item);
    }

    void add(ISizer* child_sizer, int proportion, SizerFlag flags, float border) override {
        SizerItem item;
        item.sizer      = child_sizer;
        item.proportion = proportion;
        item.flags      = flags;
        item.border     = border;
        items_.push_back(item);
    }

    void add_spacer(float size) override {
        SizerItem item;
        item.fixed_size = math::Vec2(size, size);
        items_.push_back(item);
    }

    void add_stretch(int proportion) override {
        SizerItem item;
        item.proportion = proportion;
        items_.push_back(item);
    }

    void insert(int index, IGuiWidget* widget, int proportion, SizerFlag flags, float border) override {
        SizerItem item;
        item.widget     = widget;
        item.proportion = proportion;
        item.flags      = flags;
        item.border     = border;
        int idx = (index < 0 || index > (int)items_.size()) ? (int)items_.size() : index;
        items_.insert(items_.begin() + idx, item);
    }

    // --- ISizer: removing items ---

    void remove(IGuiWidget* widget) override {
        items_.erase(std::remove_if(items_.begin(), items_.end(),
            [widget](const SizerItem& i){ return i.widget == widget; }), items_.end());
    }
    void remove_at(int index) override {
        if (index >= 0 && index < (int)items_.size())
            items_.erase(items_.begin() + index);
    }
    void clear() override { items_.clear(); }

    // --- ISizer: item access ---

    int get_item_count() const override { return (int)items_.size(); }
    const SizerItem& get_item(int index) const override { return items_[index]; }
    SizerItem* find_item(IGuiWidget* widget) override {
        for (auto& item : items_)
            if (item.widget == widget) return &item;
        return nullptr;
    }
    void set_item_visible(IGuiWidget* widget, bool visible) override {
        if (auto* item = find_item(widget)) item->visible = visible;
    }

    // --- ISizer: geometry ---

    void       set_bounds(const math::Box& b) override { bounds_ = b; }
    math::Box  get_bounds() const override { return bounds_; }

    // --- ISizer: padding ---

    void set_padding(float all) override {
        padding_ = math::Vec4(all, all, all, all);
    }
    void set_padding(float h, float v) override {
        padding_ = math::Vec4(h, v, h, v);
    }
    void set_padding(float left, float top, float right, float bottom) override {
        padding_ = math::Vec4(left, top, right, bottom);
    }
    math::Vec4 get_padding() const override { return padding_; }

    // --- ISizer: gap ---

    void  set_gap(float g) override { gap_ = g; }
    float get_gap() const override { return gap_; }
};

// ============================================================================
// BoxSizer
// ============================================================================

class BoxSizer : public SizerBase, public IBoxSizer {
    LayoutDirection dir_;

    // ---- Forward all SizerBase overrides to avoid ambiguity ----
    void add(IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::add(w,p,f,b); }
    void add(ISizer* s, int p, SizerFlag f, float b) override { SizerBase::add(s,p,f,b); }
    void add_spacer(float sz) override { SizerBase::add_spacer(sz); }
    void add_stretch(int p) override { SizerBase::add_stretch(p); }
    void insert(int idx, IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::insert(idx,w,p,f,b); }
    void remove(IGuiWidget* w) override { SizerBase::remove(w); }
    void remove_at(int i) override { SizerBase::remove_at(i); }
    void clear() override { SizerBase::clear(); }
    int get_item_count() const override { return SizerBase::get_item_count(); }
    const SizerItem& get_item(int i) const override { return SizerBase::get_item(i); }
    SizerItem* find_item(IGuiWidget* w) override { return SizerBase::find_item(w); }
    void set_item_visible(IGuiWidget* w, bool v) override { SizerBase::set_item_visible(w,v); }
    void set_bounds(const math::Box& b) override { SizerBase::set_bounds(b); }
    math::Box get_bounds() const override { return SizerBase::get_bounds(); }
    void set_padding(float a) override { SizerBase::set_padding(a); }
    void set_padding(float h, float v) override { SizerBase::set_padding(h,v); }
    void set_padding(float l, float t, float r, float b) override { SizerBase::set_padding(l,t,r,b); }
    math::Vec4 get_padding() const override { return SizerBase::get_padding(); }
    void set_gap(float g) override { SizerBase::set_gap(g); }
    float get_gap() const override { return SizerBase::get_gap(); }

public:
    explicit BoxSizer(LayoutDirection dir) : dir_(dir) {}

    LayoutDirection get_direction() const override { return dir_; }

    math::Vec2 get_min_size() const override {
        bool horiz = (dir_ == LayoutDirection::Horizontal);
        float main_total = 0.0f, cross_max = 0.0f;
        int n = 0;
        for (const auto& item : items_) {
            if (!item.visible) continue;
            auto pref = item_pref_size(item);
            auto b    = resolve_border(item);
            float main_sz  = (horiz ? math::x(pref) : math::y(pref)) + (horiz ? b.left+b.right : b.top+b.bottom);
            float cross_sz = (horiz ? math::y(pref) : math::x(pref)) + (horiz ? b.top+b.bottom : b.left+b.right);
            if (item.proportion <= 0) main_total += main_sz;
            cross_max = std::max(cross_max, cross_sz);
            ++n;
        }
        if (n > 1) main_total += gap_ * (n - 1);
        float pad_main  = horiz ? (padding_.x + padding_.z) : (padding_.y + padding_.w);
        float pad_cross = horiz ? (padding_.y + padding_.w) : (padding_.x + padding_.z);
        float w = horiz ? (main_total + pad_main)  : (cross_max + pad_cross);
        float h = horiz ? (cross_max + pad_cross) : (main_total + pad_main);
        return math::Vec2(w, h);
    }

    void layout() override {
        bool horiz = (dir_ == LayoutDirection::Horizontal);
        auto cr    = content_rect();
        float cx   = math::x(math::box_min(cr));
        float cy   = math::y(math::box_min(cr));
        float cw   = math::box_width(cr);
        float ch   = math::box_height(cr);
        float area_main  = horiz ? cw : ch;
        float area_cross = horiz ? ch : cw;

        // Pass 1: measure fixed items, accumulate proportions
        int   n_visible       = 0;
        float fixed_total     = 0.0f;
        int   total_prop      = 0;
        for (const auto& item : items_) {
            if (!item.visible) continue;
            ++n_visible;
            auto pref = item_pref_size(item);
            auto b    = resolve_border(item);
            if (item.proportion <= 0) {
                float main_border = horiz ? b.left+b.right : b.top+b.bottom;
                fixed_total += (horiz ? math::x(pref) : math::y(pref)) + main_border;
            } else {
                total_prop += item.proportion;
            }
        }
        float gap_total = (n_visible > 1) ? gap_ * (n_visible - 1) : 0.0f;
        float avail     = std::max(0.0f, area_main - fixed_total - gap_total);

        // Pass 2: assign widget bounds
        float pos = horiz ? cx : cy;  // position along main axis
        for (auto& item : items_) {
            if (!item.visible) continue;
            auto pref = item_pref_size(item);
            auto b    = resolve_border(item);

            // Main-axis slot size (includes border space on main sides)
            float main_border = horiz ? (b.left + b.right) : (b.top + b.bottom);
            float slot_main;
            if (item.proportion <= 0) {
                slot_main = (horiz ? math::x(pref) : math::y(pref)) + main_border;
            } else {
                slot_main = (total_prop > 0) ? (avail * item.proportion / (float)total_prop) : 0.0f;
            }

            // Skip pure spacers and stretches (no widget or sizer)
            if (!item.widget && !item.sizer) {
                pos += slot_main + gap_;
                continue;
            }

            // Widget size in main axis (inside border)
            float widget_main = std::max(0.0f, slot_main - main_border);

            // Cross-axis borders and sizing
            float cross_b_lo = horiz ? b.top    : b.left;
            float cross_b_hi = horiz ? b.bottom : b.right;
            float avail_cross = area_cross - cross_b_lo - cross_b_hi;

            float widget_cross, widget_cross_pos;
            if (has_flag(item.flags, SizerFlag::Expand)) {
                widget_cross     = std::max(0.0f, avail_cross);
                widget_cross_pos = (horiz ? cy : cx) + cross_b_lo;
            } else {
                float pref_cross = horiz ? math::y(pref) : math::x(pref);
                widget_cross     = std::min(pref_cross, std::max(0.0f, avail_cross));
                float cross_start = (horiz ? cy : cx) + cross_b_lo;
                if (has_flag(item.flags, SizerFlag::Center)) {
                    widget_cross_pos = cross_start + (avail_cross - widget_cross) * 0.5f;
                } else {
                    widget_cross_pos = cross_start;
                }
            }

            // Build final box
            float wx, wy, ww, wh;
            float main_pos = pos + (horiz ? b.left : b.top);
            if (horiz) {
                wx = main_pos;      wy = widget_cross_pos;
                ww = widget_main;   wh = widget_cross;
            } else {
                wx = widget_cross_pos; wy = main_pos;
                ww = widget_cross;     wh = widget_main;
            }
            item_set_bounds(item, math::make_box(wx, wy, ww, wh));

            pos += slot_main + gap_;
        }
    }
};

// ============================================================================
// GridSizer
// ============================================================================

class GridSizer : public SizerBase, public IGridSizer {
    int   cols_ = 2;
    float hgap_ = 0.0f;
    float vgap_ = 0.0f;

    // ---- Forward all SizerBase overrides ----
    void add(IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::add(w,p,f,b); }
    void add(ISizer* s, int p, SizerFlag f, float b) override { SizerBase::add(s,p,f,b); }
    void add_spacer(float sz) override { SizerBase::add_spacer(sz); }
    void add_stretch(int p) override { SizerBase::add_stretch(p); }
    void insert(int idx, IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::insert(idx,w,p,f,b); }
    void remove(IGuiWidget* w) override { SizerBase::remove(w); }
    void remove_at(int i) override { SizerBase::remove_at(i); }
    void clear() override { SizerBase::clear(); }
    int get_item_count() const override { return SizerBase::get_item_count(); }
    const SizerItem& get_item(int i) const override { return SizerBase::get_item(i); }
    SizerItem* find_item(IGuiWidget* w) override { return SizerBase::find_item(w); }
    void set_item_visible(IGuiWidget* w, bool v) override { SizerBase::set_item_visible(w,v); }
    void set_bounds(const math::Box& b) override { SizerBase::set_bounds(b); }
    math::Box get_bounds() const override { return SizerBase::get_bounds(); }
    void set_padding(float a) override { SizerBase::set_padding(a); }
    void set_padding(float h, float v) override { SizerBase::set_padding(h,v); }
    void set_padding(float l, float t, float r, float b) override { SizerBase::set_padding(l,t,r,b); }
    math::Vec4 get_padding() const override { return SizerBase::get_padding(); }
    void set_gap(float g) override { SizerBase::set_gap(g); vgap_ = g; hgap_ = g; }
    float get_gap() const override { return SizerBase::get_gap(); }

public:
    GridSizer(int cols, float hgap, float vgap) : cols_(std::max(1,cols)), hgap_(hgap), vgap_(vgap) {}

    int   get_cols() const override { return cols_; }
    void  set_cols(int c) override { cols_ = std::max(1, c); }
    float get_hgap() const override { return hgap_; }
    void  set_hgap(float g) override { hgap_ = g; }
    float get_vgap() const override { return vgap_; }
    void  set_vgap(float g) override { vgap_ = g; }

    // Compute max cell size from all visible items
    math::Vec2 cell_size() const {
        float cw = 0.0f, ch = 0.0f;
        for (const auto& item : items_) {
            if (!item.visible) continue;
            auto pref = item_pref_size(item);
            cw = std::max(cw, math::x(pref));
            ch = std::max(ch, math::y(pref));
        }
        return math::Vec2(cw, ch);
    }

    int visible_count() const {
        int n = 0;
        for (const auto& item : items_) if (item.visible) ++n;
        return n;
    }

    math::Vec2 get_min_size() const override {
        int n = visible_count();
        if (n == 0) return math::Vec2(padding_.x + padding_.z, padding_.y + padding_.w);
        int rows = (n + cols_ - 1) / cols_;
        auto cell = cell_size();
        float w = cols_ * math::x(cell) + (cols_ - 1) * hgap_ + padding_.x + padding_.z;
        float h = rows * math::y(cell) + (rows - 1) * vgap_ + padding_.y + padding_.w;
        return math::Vec2(w, h);
    }

    void layout() override {
        auto cr  = content_rect();
        float cx = math::x(math::box_min(cr));
        float cy = math::y(math::box_min(cr));
        float cw = math::box_width(cr);
        float ch = math::box_height(cr);

        // Cell size: divide content evenly by cols (respecting hgap).
        int n = visible_count();
        if (n == 0) return;
        int rows = (n + cols_ - 1) / cols_;
        float cell_w = (cols_ > 1) ? (cw - (cols_ - 1) * hgap_) / cols_ : cw;
        float cell_h = (rows > 1) ? (ch - (rows - 1) * vgap_) / rows : ch;
        cell_w = std::max(0.0f, cell_w);
        cell_h = std::max(0.0f, cell_h);

        int col = 0, row = 0;
        for (auto& item : items_) {
            if (!item.visible) continue;
            if (!item.widget && !item.sizer) { /* spacer: advance grid position */ goto next; }
            {
                float wx = cx + col * (cell_w + hgap_);
                float wy = cy + row * (cell_h + vgap_);
                auto b = resolve_border(item);
                float iw = std::max(0.0f, cell_w - b.left - b.right);
                float ih = std::max(0.0f, cell_h - b.top  - b.bottom);
                float ix = wx + b.left;
                float iy = wy + b.top;
                if (!has_flag(item.flags, SizerFlag::Expand)) {
                    auto pref = item_pref_size(item);
                    float pw = std::min(math::x(pref), iw);
                    float ph = std::min(math::y(pref), ih);
                    if (has_flag(item.flags, SizerFlag::Center)) {
                        ix += (iw - pw) * 0.5f;
                        iy += (ih - ph) * 0.5f;
                    }
                    iw = pw; ih = ph;
                }
                item_set_bounds(item, math::make_box(ix, iy, iw, ih));
            }
        next:
            if (++col >= cols_) { col = 0; ++row; }
        }
    }
};

// ============================================================================
// FlowSizer
// ============================================================================

class FlowSizer : public SizerBase, public IFlowSizer {
    LayoutDirection dir_      = LayoutDirection::Horizontal;
    float           line_gap_ = 4.0f;

    // ---- Forward all SizerBase overrides ----
    void add(IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::add(w,p,f,b); }
    void add(ISizer* s, int p, SizerFlag f, float b) override { SizerBase::add(s,p,f,b); }
    void add_spacer(float sz) override { SizerBase::add_spacer(sz); }
    void add_stretch(int p) override { SizerBase::add_stretch(p); }
    void insert(int idx, IGuiWidget* w, int p, SizerFlag f, float b) override { SizerBase::insert(idx,w,p,f,b); }
    void remove(IGuiWidget* w) override { SizerBase::remove(w); }
    void remove_at(int i) override { SizerBase::remove_at(i); }
    void clear() override { SizerBase::clear(); }
    int get_item_count() const override { return SizerBase::get_item_count(); }
    const SizerItem& get_item(int i) const override { return SizerBase::get_item(i); }
    SizerItem* find_item(IGuiWidget* w) override { return SizerBase::find_item(w); }
    void set_item_visible(IGuiWidget* w, bool v) override { SizerBase::set_item_visible(w,v); }
    void set_bounds(const math::Box& b) override { SizerBase::set_bounds(b); }
    math::Box get_bounds() const override { return SizerBase::get_bounds(); }
    void set_padding(float a) override { SizerBase::set_padding(a); }
    void set_padding(float h, float v) override { SizerBase::set_padding(h,v); }
    void set_padding(float l, float t, float r, float b) override { SizerBase::set_padding(l,t,r,b); }
    math::Vec4 get_padding() const override { return SizerBase::get_padding(); }
    void set_gap(float g) override { SizerBase::set_gap(g); }
    float get_gap() const override { return SizerBase::get_gap(); }

public:
    explicit FlowSizer(LayoutDirection dir) : dir_(dir) {}

    LayoutDirection get_direction() const override { return dir_; }
    float get_line_gap() const override { return line_gap_; }
    void  set_line_gap(float g) override { line_gap_ = g; }

    math::Vec2 get_min_size() const override {
        // Minimum: all items on one line (no wrapping).
        bool horiz = (dir_ == LayoutDirection::Horizontal);
        float main_total = 0.0f, cross_max = 0.0f;
        int n = 0;
        for (const auto& item : items_) {
            if (!item.visible) continue;
            auto pref = item_pref_size(item);
            auto b    = resolve_border(item);
            float main_sz  = (horiz ? math::x(pref) : math::y(pref)) + (horiz ? b.left+b.right : b.top+b.bottom);
            float cross_sz = (horiz ? math::y(pref) : math::x(pref)) + (horiz ? b.top+b.bottom : b.left+b.right);
            main_total += main_sz;
            cross_max = std::max(cross_max, cross_sz);
            ++n;
        }
        if (n > 1) main_total += gap_ * (n - 1);
        float pad_main  = horiz ? (padding_.x + padding_.z) : (padding_.y + padding_.w);
        float pad_cross = horiz ? (padding_.y + padding_.w) : (padding_.x + padding_.z);
        return math::Vec2(
            horiz ? main_total + pad_main  : cross_max + pad_cross,
            horiz ? cross_max + pad_cross  : main_total + pad_main);
    }

    void layout() override {
        bool horiz = (dir_ == LayoutDirection::Horizontal);
        auto cr    = content_rect();
        float cx   = math::x(math::box_min(cr));
        float cy   = math::y(math::box_min(cr));
        float cw   = math::box_width(cr);
        float ch   = math::box_height(cr);
        float area_main  = horiz ? cw : ch;

        float line_pos  = horiz ? cy : cx;   // current line start (cross axis)
        float main_pos  = horiz ? cx : cy;   // current position along main axis
        float line_cross = 0.0f;             // tallest item on current line

        // Two-pass per line: first measure, then place.
        // We use a simple greedy wrap.
        int i = 0;
        int n = (int)items_.size();

        while (i < n) {
            // --- Measure this line ---
            int line_start = i;
            float line_main = 0.0f;
            float lc = 0.0f;
            int line_items = 0;

            while (i < n) {
                const auto& item = items_[i];
                if (!item.visible) { ++i; continue; }
                auto pref = item_pref_size(item);
                auto b    = resolve_border(item);
                float item_main  = (horiz ? math::x(pref) : math::y(pref)) + (horiz ? b.left+b.right : b.top+b.bottom);
                float item_cross = (horiz ? math::y(pref) : math::x(pref)) + (horiz ? b.top+b.bottom : b.left+b.right);

                float needed = (line_items > 0) ? line_main + gap_ + item_main : item_main;
                if (line_items > 0 && needed > area_main) break;  // wrap
                line_main += (line_items > 0 ? gap_ : 0.0f) + item_main;
                lc = std::max(lc, item_cross);
                ++line_items;
                ++i;
            }
            if (line_items == 0) { ++i; continue; }

            // --- Place items on this line ---
            main_pos = horiz ? cx : cy;
            for (int j = line_start; j < i; ) {
                auto& item = items_[j];
                ++j;
                if (!item.visible) continue;
                auto pref = item_pref_size(item);
                auto b    = resolve_border(item);
                float item_main  = (horiz ? math::x(pref) : math::y(pref));
                float item_cross = (horiz ? math::y(pref) : math::x(pref));
                float main_border_lo  = horiz ? b.left : b.top;
                float main_border_hi  = horiz ? b.right : b.bottom;
                float cross_border_lo = horiz ? b.top : b.left;
                float cross_border_hi = horiz ? b.bottom : b.right;

                float slot_cross = lc - cross_border_lo - cross_border_hi;

                float widget_cross;
                float cross_off;
                if (has_flag(item.flags, SizerFlag::Expand)) {
                    widget_cross = std::max(0.0f, slot_cross);
                    cross_off = 0.0f;
                } else {
                    widget_cross = std::min(item_cross, std::max(0.0f, slot_cross));
                    cross_off = has_flag(item.flags, SizerFlag::Center)
                                ? (slot_cross - widget_cross) * 0.5f : 0.0f;
                }

                float wx, wy, ww, wh;
                if (horiz) {
                    wx = main_pos + main_border_lo;
                    wy = line_pos + cross_border_lo + cross_off;
                    ww = item_main;
                    wh = widget_cross;
                } else {
                    wx = line_pos + cross_border_lo + cross_off;
                    wy = main_pos + main_border_lo;
                    ww = widget_cross;
                    wh = item_main;
                }
                if (item.widget || item.sizer)
                    item_set_bounds(item, math::make_box(wx, wy, ww, wh));

                main_pos += item_main + main_border_lo + main_border_hi + gap_;
            }

            line_pos += lc + line_gap_;
        }
    }
};

// ============================================================================
// Factory functions
// ============================================================================

IBoxSizer* create_box_sizer(LayoutDirection direction) {
    return new BoxSizer(direction);
}

IGridSizer* create_grid_sizer(int cols, float hgap, float vgap) {
    return new GridSizer(cols, hgap, vgap);
}

IFlowSizer* create_flow_sizer(LayoutDirection direction) {
    return new FlowSizer(direction);
}

void destroy_sizer(ISizer* sizer) {
    delete sizer;
}

} // namespace gui
} // namespace window
