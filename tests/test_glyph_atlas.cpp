/*
 * test_glyph_atlas.cpp - Headless unit test for the RAM glyph atlas manager.
 *
 * Exercises font::IGlyphAtlasManager without any window or GPU:
 *   - glyph dedup (same key returns the same slot)
 *   - layer growth when a small atlas fills up
 *   - garbage collection of idle glyphs (and reuse afterwards)
 *   - codepoint fallback resolution
 *   - synthetic bold / italic
 *
 * Skips gracefully (exit 0) if no system font can be loaded.
 */

#include "gui/font/font.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace font;

static int g_fail = 0;
#define CHECK(cond, msg)                                                      \
    do {                                                                      \
        if (!(cond)) { printf("  FAIL: %s\n", (msg)); ++g_fail; }             \
        else         { printf("  ok:   %s\n", (msg)); }                       \
    } while (0)

// Load a Latin primary face we can rely on across platforms.
static IFontFace* load_primary(IFontLibrary* lib) {
    const char* families[] = {
        "DejaVu Sans", "Liberation Sans", "Arial", "FreeSans",
        "Helvetica", "Segoe UI", nullptr
    };
    for (int i = 0; families[i]; ++i) {
        IFontFace* f = lib->load_system_font(FontDescriptor::create(families[i], 16.0f), nullptr);
        if (f) return f;
    }
    return lib->get_default_font(16.0f, nullptr);
}

int main() {
    printf("=== glyph atlas manager test ===\n");

    Result r;
    IFontLibrary* lib = create_font_library(FontBackend::Auto, &r);
    if (!lib) {
        printf("SKIP: no font backend available (%s)\n", result_to_string(r));
        return 0;
    }
    IFontFace* primary = load_primary(lib);
    if (!primary) {
        printf("SKIP: could not load any system font\n");
        destroy_font_library(lib);
        return 0;
    }
    printf("primary font: %s\n", primary->get_family_name());

    const uint32_t gi_A = primary->get_glyph_index('A');
    const uint32_t gi_B = primary->get_glyph_index('B');
    CHECK(gi_A != 0 && gi_B != 0, "primary has glyphs for 'A' and 'B'");

    // ---- 1. dedup + basic acquire ----
    printf("[dedup]\n");
    {
        GlyphAtlasConfig cfg;
        cfg.layer_width = 256; cfg.layer_height = 256; cfg.max_layers = 8;
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, cfg);
        int idx = m->add_font(primary);
        CHECK(idx == 0, "primary registered as font index 0");

        const GlyphSlot* a1 = m->acquire(0, gi_A, 16.0f);
        CHECK(a1 && a1->valid() && a1->pw > 0 && a1->ph > 0, "acquire 'A' returns a packed slot");
        int after_first = m->glyph_count();

        GlyphSlot copy = *a1;
        const GlyphSlot* a2 = m->acquire(0, gi_A, 16.0f);
        CHECK(a2 && a2->layer == copy.layer && a2->px == copy.px && a2->py == copy.py,
              "re-acquiring same key returns identical placement");
        CHECK(m->glyph_count() == after_first, "dedup does not add a new glyph");

        // Different size => different key.
        m->acquire(0, gi_A, 24.0f);
        CHECK(m->glyph_count() == after_first + 1, "different size is a distinct cache entry");

        destroy_glyph_atlas_manager(m);
    }

    // ---- 2. layer growth ----
    printf("[growth]\n");
    {
        GlyphAtlasConfig cfg;
        cfg.layer_width = 128; cfg.layer_height = 128; cfg.max_layers = 64; cfg.padding = 1;
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, cfg);
        m->add_font(primary);
        for (uint32_t c = 33; c <= 126; ++c) {
            uint32_t gi = primary->get_glyph_index(c);
            if (gi) m->acquire(0, gi, 40.0f);
        }
        printf("  layers used: %d, glyphs: %d\n", m->layer_count(), m->glyph_count());
        CHECK(m->layer_count() >= 2, "small atlas grew to multiple layers");

        std::vector<GlyphDirtyRegion> dirty;
        m->take_dirty_regions(dirty);
        CHECK(!dirty.empty(), "dirty regions recorded for uploaded glyphs");
        std::vector<GlyphDirtyRegion> dirty2;
        m->take_dirty_regions(dirty2);
        CHECK(dirty2.empty(), "dirty regions cleared after being taken");

        destroy_glyph_atlas_manager(m);
    }

    // ---- 3. garbage collection ----
    printf("[gc]\n");
    {
        GlyphAtlasConfig cfg;
        cfg.layer_width = 256; cfg.layer_height = 256; cfg.max_layers = 16;
        cfg.gc_idle_frames = 3;
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, cfg);
        m->add_font(primary);
        for (uint32_t c = 'a'; c <= 'j'; ++c) {
            uint32_t gi = primary->get_glyph_index(c);
            if (gi) m->acquire(0, gi, 20.0f);
        }
        int n = m->glyph_count();
        size_t mem = m->memory_usage();
        CHECK(n > 0 && mem > 0, "glyphs acquired with non-zero memory use");

        for (int i = 0; i < 5; ++i) m->begin_frame();   // let them go idle
        int freed = m->collect_garbage();
        CHECK(freed == n, "GC freed all idle glyphs");
        CHECK(m->glyph_count() == 0, "glyph count is zero after GC");
        CHECK(m->memory_usage() == 0, "memory usage is zero after GC");

        const GlyphSlot* again = m->acquire(0, gi_A, 20.0f);
        CHECK(again && again->valid(), "atlas reusable after GC");
        destroy_glyph_atlas_manager(m);
    }

    // ---- 4. codepoint fallback ----
    printf("[fallback]\n");
    {
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, GlyphAtlasConfig{256, 256, 1, 8, 1, 600, 0});
        m->add_font(primary);

        int resolved = -1;
        const GlyphSlot* a = m->acquire_codepoint(0, 'A', 16.0f, &resolved);
        CHECK(a && a->valid() && resolved == 0, "Latin codepoint resolves to the primary font");

        int cjk_resolved = -1;
        const GlyphSlot* cjk = m->acquire_codepoint(0, 0x4E2D /* CJK 'zhong' */, 16.0f, &cjk_resolved);
        CHECK(cjk != nullptr && cjk_resolved >= 0, "CJK codepoint resolves (fallback or .notdef)");
        if (cjk_resolved > 0)
            printf("  CJK fallback font: %s\n", m->get_font(cjk_resolved)->get_family_name());
        else
            printf("  (no CJK fallback font installed; resolved to primary .notdef)\n");

        destroy_glyph_atlas_manager(m);
    }

    // ---- 5. synthetic bold / italic ----
    printf("[bold/italic]\n");
    {
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, GlyphAtlasConfig{256, 256, 1, 8, 1, 600, 0});
        m->add_font(primary);

        const FontDescriptor& d = primary->get_descriptor();
        bool intrinsic_bold   = static_cast<int>(d.weight) >= static_cast<int>(FontWeight::SemiBold);
        bool intrinsic_italic = d.style != FontStyle::Normal;

        const GlyphSlot* reg = m->acquire(0, gi_A, 24.0f, FontWeight::Regular, FontStyle::Normal);
        CHECK(reg && reg->valid(), "regular glyph acquired");
        int reg_w = reg->pw;

        const GlyphSlot* bold = m->acquire(0, gi_A, 24.0f, FontWeight::Bold, FontStyle::Normal);
        CHECK(bold && bold->valid(), "bold glyph acquired");
        if (!intrinsic_bold) CHECK(bold->pw > reg_w, "synthetic bold is wider than regular");

        const GlyphSlot* ital = m->acquire(0, gi_A, 24.0f, FontWeight::Regular, FontStyle::Italic);
        CHECK(ital && ital->valid(), "italic glyph acquired");
        if (!intrinsic_italic) CHECK(ital->pw > reg_w, "synthetic italic is wider (sheared)");

        int expected = 1 + (intrinsic_bold ? 0 : 1) + (intrinsic_italic ? 0 : 1);
        CHECK(m->glyph_count() == expected, "bold/italic synthesis uses distinct cache keys");

        destroy_glyph_atlas_manager(m);
    }

    // ---- 6. shaper + fallback chain integration (mirrors the GUI path) ----
    printf("[shaper]\n");
    {
        IGlyphAtlasManager* m = create_glyph_atlas_manager(lib, GlyphAtlasConfig{512, 512, 1, 16, 1, 600, 0});
        m->add_font(primary);
        ITextShaper* shaper = create_text_shaper(lib, nullptr);
        CHECK(shaper != nullptr, "text shaper created");
        if (shaper) {
            shaper->set_fallback_chain(m->fallback_chain());   // loads platform fallbacks
            std::vector<PositionedGlyph> glyphs;
            const char* mixed = "A\xe4\xb8\xad";   // "A" + CJK U+4E2D
            shaper->shape_text(m->get_font(0), mixed, -1, glyphs, TextLayoutOptions());
            CHECK(!glyphs.empty(), "mixed-language string shaped to glyphs");
            bool all_acquired = !glyphs.empty();
            bool any_fallback = false;
            for (const auto& g : glyphs) {
                if (g.font_index > 0) any_fallback = true;
                if (!m->acquire(g.font_index, g.glyph_index, 16.0f)) all_acquired = false;
            }
            CHECK(all_acquired, "every shaped glyph acquires an atlas slot");
            printf("  shaped %d glyphs across %d fonts; CJK fallback in shaper: %s\n",
                   (int)glyphs.size(), m->font_count(), any_fallback ? "yes" : "no");
            destroy_text_shaper(shaper);
        }
        destroy_glyph_atlas_manager(m);
    }

    destroy_font_library(lib);

    printf("=== %s ===\n", g_fail == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_fail == 0 ? 0 : 1;
}
