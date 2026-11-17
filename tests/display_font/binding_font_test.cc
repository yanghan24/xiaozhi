#include "lvgl.h"
#include "binding_texts.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

LV_FONT_DECLARE(font_puhui_basic_30_4);
LV_FONT_DECLARE(font_puhui_binding_30_4);

static void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static std::set<uint32_t> BindingCodepoints() {
    std::set<uint32_t> codepoints;
    for (const char* text : binding_texts) {
        const auto* p = reinterpret_cast<const unsigned char*>(text);
        while (*p) {
            uint32_t cp = *p++;
            unsigned extra = 0;
            if (cp >= 0xf0) { cp &= 7; extra = 3; }
            else if (cp >= 0xe0) { cp &= 15; extra = 2; }
            else if (cp >= 0xc0) { cp &= 31; extra = 1; }
            for (unsigned i = 0; i < extra; ++i) {
                Require((*p & 0xc0) == 0x80, "invalid UTF-8 fixture");
                cp = (cp << 6) | (*p++ & 63);
            }
            codepoints.insert(cp);
        }
    }
    return codepoints;
}

// This uses LVGL's actual glyph lookup and bitmap renderer, not source inspection.
static bool HasVisibleGlyph(const lv_font_t* font, uint32_t cp) {
    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0) || glyph.is_placeholder)
        return false;
    if (cp == ' ') return glyph.adv_w > 0;
    if (glyph.box_w == 0 || glyph.box_h == 0) return false;
    lv_draw_buf_t* buffer = lv_draw_buf_create(
        glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    Require(buffer != nullptr, "allocate glyph buffer");
    const auto* bitmap = static_cast<const lv_draw_buf_t*>(
        lv_font_get_glyph_bitmap(&glyph, buffer));
    bool visible = false;
    if (bitmap != nullptr) {
        for (unsigned y = 0; y < glyph.box_h; ++y)
            for (unsigned x = 0; x < glyph.box_w; ++x)
                visible |= bitmap->data[y * bitmap->header.stride + x] != 0;
    }
    lv_font_glyph_release_draw_data(&glyph);
    lv_draw_buf_destroy(buffer);
    return visible;
}

static std::vector<unsigned char> Snapshot(lv_obj_t* label, const lv_font_t* font) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_update_layout(label);
    lv_draw_buf_t* pixels = lv_snapshot_take(label, LV_COLOR_FORMAT_RGB888);
    Require(pixels != nullptr, "snapshot text label");
    std::vector<unsigned char> result(
        pixels->data, pixels->data + pixels->header.stride * pixels->header.h);
    lv_draw_buf_destroy(pixels);
    return result;
}

int main() {
    lv_init();
    Require(font_puhui_binding_30_4.line_height == font_puhui_basic_30_4.line_height,
            "fallback has the same line height");
    Require(font_puhui_binding_30_4.base_line == font_puhui_basic_30_4.base_line,
            "fallback has the same baseline");
    const auto codepoints = BindingCodepoints();
    unsigned missing_from_basic = 0;
    for (uint32_t cp : codepoints) {
        missing_from_basic += !HasVisibleGlyph(&font_puhui_basic_30_4, cp);
        if (!HasVisibleGlyph(&font_puhui_binding_30_4, cp)) {
            std::fprintf(stderr, "FAIL: binding text has no visible glyph for U+%04X\n", cp);
            return 1;
        }
    }
    Require(missing_from_basic != 0, "baseline reproduces missing binding text");

    lv_display_t* display = lv_display_create(800, 480);
    Require(display != nullptr, "create host display");
    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_obj_set_size(label, 420, 48);
    lv_obj_set_style_bg_color(label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    for (const char* text : {"设备尚未配置", "请联系维护人员"}) {
        lv_label_set_text(label, text);
        const auto basic = Snapshot(label, &font_puhui_basic_30_4);
        const auto complete = Snapshot(label, &font_puhui_binding_30_4);
        Require(basic != complete, "complete font changes the actual label pixels");
        Require(Snapshot(label, &font_puhui_basic_30_4) == basic,
                "switching fonts redraws the baseline consistently");
        Require(Snapshot(label, &font_puhui_binding_30_4) == complete,
                "switching back restores the complete label pixels");
    }
    lv_display_delete(display);
    std::printf("PASS: %zu binding codepoints render; basic is missing %u; label pixels refresh after font changes\n",
                codepoints.size(), missing_from_basic);
    lv_deinit();
}
