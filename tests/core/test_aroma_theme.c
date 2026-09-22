/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test_aroma_theme.h"
#include "aroma_style.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static void test_global_theme(void)
{
    /* The global theme self-initializes and is stable across calls. */
    AromaTheme a = aroma_theme_get_global();
    AromaTheme b = aroma_theme_get_global();
    CHECK(a.colors.surface == b.colors.surface &&
              a.colors.primary == b.colors.primary &&
              a.colors.background == b.colors.background,
          "global theme stable");

    /* An explicit set roundtrips through the getter. */
    AromaTheme custom = aroma_theme_create_default();
    custom.colors.primary = aroma_color_rgb(1, 2, 3);
    aroma_theme_set_global(&custom);
    AromaTheme back = aroma_theme_get_global();
    CHECK(back.colors.primary == aroma_color_rgb(1, 2, 3),
          "set_global roundtrips");

    /* NULL set is ignored, previous theme survives. */
    aroma_theme_set_global(NULL);
    back = aroma_theme_get_global();
    CHECK(back.colors.primary == aroma_color_rgb(1, 2, 3),
          "NULL set_global keeps theme");

    /* Restore defaults for other suites. */
    custom = aroma_theme_create_default();
    aroma_theme_set_global(&custom);
}

static void test_theme_presets(void)
{
    AromaTheme blue = aroma_theme_create_material_preset_dark(
        AROMA_THEME_MATERIAL_BLUE);
    AromaTheme teal = aroma_theme_create_material_preset_dark(
        AROMA_THEME_MATERIAL_TEAL);
    CHECK(blue.colors.primary == 0x8AB4F8, "blue preset primary exact");
    CHECK(teal.colors.primary == 0x4DB6AC, "teal preset primary exact");
    CHECK(blue.colors.primary != teal.colors.primary,
          "presets differ by accent");
}

void run_theme_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Theme Tests ===\n");
    test_global_theme();
    test_theme_presets();
    printf("Aroma Theme: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
