# Binding text rendering

Run from the firmware root after installing the ESP-IDF managed components:

```sh
cmake -S tests/display_font -B build/display-font
cmake --build build/display-font -j 4
ctest --test-dir build/display-font --output-on-failure
```

For a worktree sharing installed dependencies, pass
`-DMANAGED_COMPONENTS_DIR=/absolute/path/to/managed_components` at configure time.

The test compiles the firmware's LVGL and built-in fonts. It extracts binding UI
strings from `lcd_display.cc`, decodes every glyph into pixels, and renders the
two reported startup lines as actual LVGL labels while changing fonts. The
original basic font reproduces the missing glyphs; the supplemented font must
render all binding text before any assets or network connection exists.

The generated supplement copies 50 glyphs from `78/xiaozhi-fonts` and falls back
to the original basic font. Regenerate it after changing binding text:

```sh
python3 scripts/generate_binding_font.py \
  --source managed_components/78__xiaozhi-fonts/src/font_puhui_30_4.c \
  --lcd-source main/display/lcd_display.cc \
  --output main/display/fonts/font_puhui_binding_30_4.c
```

Append `--check` to verify the checked-in file without modifying it.

These tests cover font data and LVGL rendering. A device boot must additionally
confirm the startup font selection, runtime asset refresh, and physical display.
