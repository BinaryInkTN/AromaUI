












#include "test_aroma_font_measure.h"
#include "aroma_font.h"
#include "aroma_material_font.h"
#include "aroma_material_icons.h"

#include <stdio.h>

extern unsigned char aroma_ubuntu_ttf[];
extern unsigned int aroma_ubuntu_ttf_len;

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

static void test_null_safety(void)
{
    CHECK(aroma_font_get_line_width(NULL, "Hello") == 0,
          "NULL font measures 0");
}

static void test_ascii_stable(void)
{
    AromaFont *f = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                 aroma_ubuntu_ttf_len, 24);
    CHECK(f != NULL, "ubuntu font loads");
    if (!f)
        return;
    CHECK(aroma_font_get_line_width(f, "") == 0, "empty string is 0");
    CHECK(aroma_font_get_line_width(f, NULL) == 0, "NULL text is 0");
    CHECK(aroma_font_get_line_width(f, "Hello") == 56,
          "ascii width pinned (Hello@24px)");
    CHECK(aroma_font_get_line_width(f, "AB") ==
              aroma_font_get_line_width(f, "A") +
                  aroma_font_get_line_width(f, "B"),
          "ascii widths add up");
    aroma_font_destroy(f);
}

static void test_utf8_codepoints(void)
{
    AromaFont *icons = aroma_font_create_from_memory(icon_ttf, icon_ttf_len,
                                                     64);
    CHECK(icons != NULL, "material font loads");
    if (icons)
    {

        CHECK(aroma_font_get_line_width(icons, AROMA_ICON_APPS) == 64,
              "icon glyph measures one advance");
        CHECK(aroma_font_get_line_width(icons, AROMA_ICON_APPS) * 2 ==
                  aroma_font_get_line_width(icons, AROMA_ICON_APPS
                                                       AROMA_ICON_APPS),
              "repeated icon scales per codepoint");
        aroma_font_destroy(icons);
    }

    AromaFont *f = aroma_font_create_from_memory(aroma_ubuntu_ttf,
                                                 aroma_ubuntu_ttf_len, 24);
    CHECK(f != NULL, "ubuntu font loads (utf8)");
    if (!f)
        return;

    CHECK(aroma_font_get_line_width(f, "Caf\xc3\xa9") == 48,
          "two-byte glyph measures one advance");
    aroma_font_destroy(f);
}

void run_font_measure_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Font Measure Tests ===\n");
    test_null_safety();
    test_ascii_stable();
    test_utf8_codepoints();
    printf("Aroma Font Measure: %d passed, %d failed\n", s_passed, s_failed);
    *passed += s_passed;
    *failed += s_failed;
}
