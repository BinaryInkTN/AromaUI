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

#include "test_aroma_drawlist.h"
#include "aroma_drawlist.h"

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

static void test_lifecycle_null_safe(void)
{
    aroma_drawlist_destroy(NULL);
    aroma_drawlist_reset(NULL);
    aroma_drawlist_begin(NULL);
    aroma_drawlist_end();
    CHECK(!aroma_drawlist_is_active(), "no active list after end");
    CHECK(aroma_drawlist_get_active() == NULL, "active list is NULL");

    AromaDrawList *list = aroma_drawlist_create();
    CHECK(list != NULL, "drawlist created");
    if (!list)
        return;
    aroma_drawlist_begin(list);
    CHECK(aroma_drawlist_is_active(), "list active after begin");
    CHECK(aroma_drawlist_get_active() == list, "active list returned");
    aroma_drawlist_end();
    CHECK(!aroma_drawlist_is_active(), "list inactive after end");
    aroma_drawlist_reset(list);
    aroma_drawlist_destroy(list);
    CHECK(1, "lifecycle null-safe");
}

static void test_command_guards(void)
{
    /* Every command must tolerate a NULL list and degenerate geometry
     * without crashing. Record into a real list too, then reset/destroy
     * (exercises the text-command string copy path). */
    aroma_drawlist_cmd_clear(NULL, 0xFF000000);
    aroma_drawlist_cmd_fill_rect(NULL, 0, 0, 10, 10, 0xFFFFFFFF, false, 0);
    aroma_drawlist_cmd_hollow_rect(NULL, 0, 0, 10, 10, 0xFFFFFFFF, 1, false, 0);
    aroma_drawlist_cmd_arc(NULL, 5, 5, 4, 0, 90, 0xFFFFFFFF, 1);
    aroma_drawlist_cmd_line(NULL, 0, 0, 5, 5, 0xFFFFFFFF, 1, false);
    aroma_drawlist_cmd_text(NULL, NULL, "hi", 0, 0, 0xFFFFFFFF, 1.0f);
    aroma_drawlist_cmd_image(NULL, 0, 0, 10, 10, 1);
    aroma_drawlist_cmd_scissor_push(NULL, 0, 0, 10, 10);
    aroma_drawlist_cmd_scissor_pop(NULL);
    aroma_drawlist_cmd_blur_backdrop(NULL, 0, 0, 10, 10, 8.0f, 0.0f);

    AromaDrawList *list = aroma_drawlist_create();
    if (!list)
    {
        CHECK(0, "drawlist created for guards");
        return;
    }
    aroma_drawlist_cmd_clear(list, 0xFF000000);
    aroma_drawlist_cmd_fill_rect(list, 10, 20, 100, 50, 0xFFFFFFFF, true, 8.0f);
    aroma_drawlist_cmd_hollow_rect(list, 10, 20, 100, 50, 0xFFFFFFFF, 2, true, 8.0f);
    aroma_drawlist_cmd_arc(list, 50, 50, 20, 0.0f, 180.0f, 0xFFFFFFFF, 2);
    aroma_drawlist_cmd_line(list, 0, 0, 100, 100, 0xFFFFFFFF, 2.0f, true);
    {
        char stack_text[] = "stack-copy-me";
        aroma_drawlist_cmd_text(list, NULL, stack_text, 5, 5, 0xFFFFFFFF, 1.0f);
    }
    aroma_drawlist_cmd_text(list, NULL, NULL, 0, 0, 0, 0); /* ignored */
    aroma_drawlist_cmd_image(list, 0, 0, 64, 64, 7);
    aroma_drawlist_cmd_scissor_push(list, 0, 0, 800, 600);
    aroma_drawlist_cmd_scissor_pop(list);
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 100, 100, 12.0f, 6.0f);
    /* Degenerate blur records nothing but must not crash. */
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 0, 100, 12.0f, 0.0f);
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 100, 100, 0.0f, 0.0f);
    aroma_drawlist_reset(list);
    aroma_drawlist_destroy(list);
    CHECK(1, "command guards safe");
}

static void test_blur_command(void)
{
    /* Frosted-glass backdrop command: invalid geometry records nothing,
     * valid geometry records without crashing; reset destroys cleanly. */
    aroma_drawlist_cmd_blur_backdrop(NULL, 0, 0, 100, 100, 12.0f, 6.0f);

    AromaDrawList *list = aroma_drawlist_create();
    if (!list)
    {
        CHECK(0, "drawlist created for blur");
        return;
    }
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 0, 100, 12.0f, 0.0f);
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 100, 100, 0.0f, 0.0f);
    aroma_drawlist_cmd_blur_backdrop(list, 0, 0, 100, 100, -3.0f, 0.0f);
    aroma_drawlist_cmd_blur_backdrop(list, 10, 20, 200, 100, 14.0f, 12.0f);
    aroma_drawlist_reset(list);
    aroma_drawlist_destroy(list);
    CHECK(1, "blur command guards safe");
}

void run_drawlist_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Draw List Tests ===\n");
    test_lifecycle_null_safe();
    test_command_guards();
    test_blur_command();
    printf("Aroma Draw List: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
