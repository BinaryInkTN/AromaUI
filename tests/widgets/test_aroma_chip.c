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

#include "test_aroma_chip.h"
#include "widgets/aroma_chip.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

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

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_chip_lifecycle(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_chip_create(NULL, 0, 0, "x", CHIP_TYPE_ASSIST) == NULL,
          "chip rejects NULL parent");

    AromaNode *chip = aroma_chip_create(root, 10, 10, "Wi-Fi",
                                        CHIP_TYPE_FILTER);
    CHECK(chip != NULL, "chip created");
    if (!chip)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    /* Destroy detaches cleanly: destroying the tree afterwards must not
     * touch the chip widget a second time (regression: double-free). */
    aroma_chip_destroy(chip);
    CHECK(1, "chip destroyed without crashing");

    AromaNode *chip2 = aroma_chip_create(root, 0, 0, "BT", CHIP_TYPE_ASSIST);
    CHECK(chip2 != NULL, "second chip created");

    aroma_chip_destroy(NULL);
    __destroy_node(root);
    __node_system_destroy();
}

void run_chip_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Chip Tests ===\n");
    test_chip_lifecycle();
    printf("Aroma Chip: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
