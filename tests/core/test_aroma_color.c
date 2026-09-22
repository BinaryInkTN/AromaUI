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

#include "test_aroma_color.h"
#include "aroma_style.h"

#include <stdio.h>
#include <stdint.h>

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

static void test_packing(void)
{
    CHECK(aroma_color_rgba(0x11, 0x22, 0x33, 0x44) == 0x44112233u,
          "rgba packs as 0xAARRGGBB");
    CHECK(aroma_color_rgb(0x11, 0x22, 0x33) == 0x00112233u,
          "rgb packs with zero alpha");
    CHECK(aroma_color_rgba(0, 0, 0, 0) == 0u, "rgba zero is zero");
    CHECK(aroma_color_rgba(255, 255, 255, 255) == 0xFFFFFFFFu,
          "rgba max is all ones");

    uint8_t r = 0, g = 0, b = 0;
    aroma_color_extract_rgb(0x44112233u, &r, &g, &b);
    CHECK(r == 0x11 && g == 0x22 && b == 0x33, "extract roundtrips");
    /* Alpha is not part of the RGB extraction. */
    r = g = b = 0;
    aroma_color_extract_rgb(0xFF000000u, &r, &g, &b);
    CHECK(r == 0 && g == 0 && b == 0, "extract ignores alpha");
    /* NULL outputs are safe. */
    aroma_color_extract_rgb(0x12345678u, NULL, NULL, NULL);
    aroma_color_extract_rgb(0x12345678u, &r, NULL, NULL);
    CHECK(r == 0x34, "partial extract works");
}

static void test_blend(void)
{
    uint32_t black = aroma_color_rgb(0, 0, 0);
    uint32_t white = aroma_color_rgb(255, 255, 255);
    CHECK(aroma_color_blend(black, white, 0.0f) == black,
          "blend 0 returns first color");
    CHECK(aroma_color_blend(black, white, 1.0f) == white,
          "blend 1 returns second color");
    /* Midpoint truncates toward zero: 255 * 0.5 = 127.5 -> 127. */
    CHECK(aroma_color_blend(black, white, 0.5f) == aroma_color_rgb(127, 127, 127),
          "blend midpoint is exact");
    uint32_t red = aroma_color_rgb(255, 0, 0);
    uint32_t blue = aroma_color_rgb(0, 0, 255);
    uint8_t r, g, b;
    aroma_color_extract_rgb(aroma_color_blend(red, blue, 0.5f), &r, &g, &b);
    CHECK(r == 127 && g == 0 && b == 127, "blend mixes channels");
}

static void test_adjust(void)
{
    uint32_t gray = aroma_color_rgb(100, 100, 100);
    CHECK(aroma_color_adjust(gray, 0.0f) == gray, "adjust zero is identity");
    /* Extreme factors clamp instead of wrapping. */
    CHECK(aroma_color_adjust(aroma_color_rgb(0, 0, 0), 10.0f) ==
              aroma_color_rgb(255, 255, 255),
          "adjust clamps to white");
    CHECK(aroma_color_adjust(aroma_color_rgb(255, 255, 255), -10.0f) ==
              aroma_color_rgb(0, 0, 0),
          "adjust clamps to black");
    uint8_t r, g, b;
    aroma_color_extract_rgb(aroma_color_adjust(gray, 0.2f), &r, &g, &b);
    CHECK(r == 151 && g == 151 && b == 151, "adjust lightens evenly");
}

void run_color_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Color Tests ===\n");
    test_packing();
    test_blend();
    test_adjust();
    printf("Aroma Color: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
