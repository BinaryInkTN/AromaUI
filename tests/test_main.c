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

// Test runner. Suites live in category directories that mirror the
// framework layout (see tests/CMakeLists.txt):
//   core/    - foundations: memory, scene graph, events, draw list,
//              timing, style (headless, no GPU).
//   widgets/ - every widget, headless (no GPU, no display).
//   markup/  - Incense UI markup loading, headless.
// GPU-backed blur tests live in test_aroma_blur_gl.c (separate binary,
// Linux + EGL only).

#include "test_aroma_slab_alloc.h"
#include "test_aroma_node.h"
#include "test_aroma_event_system.h"
#include "test_aroma_drawlist.h"
#include "test_aroma_timing.h"
#include "test_aroma_color.h"
#include "test_aroma_theme.h"
#include "test_aroma_font_measure.h"
#include "test_aroma_button.h"
#include "test_aroma_container.h"
#include "test_aroma_label.h"
#include "test_aroma_glass.h"
#include "test_aroma_inputs.h"
#include "test_aroma_slider.h"
#include "test_aroma_progress.h"
#include "test_aroma_chip.h"
#include "test_aroma_selection.h"
#include "test_aroma_overlays.h"
#include "test_aroma_display.h"
#include "test_aroma_forms.h"
#include "test_aroma_canvas.h"
#include "test_aroma_table.h"
#include "test_aroma_incense.h"
#include <stdio.h>

static int s_total_passed = 0;
static int s_total_failed = 0;
static int s_cat_passed = 0;
static int s_cat_failed = 0;

static void begin_category(const char *name)
{
    s_cat_passed = 0;
    s_cat_failed = 0;
    printf("\n--- %s ---\n", name);
}

static void tally(int passed, int failed)
{
    s_cat_passed += passed;
    s_cat_failed += failed;
    s_total_passed += passed;
    s_total_failed += failed;
}

static void end_category(void)
{
    printf("Category: %d passed, %d failed\n", s_cat_passed, s_cat_failed);
}

int main(void)
{
    int passed, failed;

    /* ---- core/ ---- */
    begin_category("core");
    run_slab_allocator_tests(&passed, &failed);
    tally(passed, failed);
    run_node_tests(&passed, &failed);
    tally(passed, failed);
    run_event_tests(&passed, &failed);
    tally(passed, failed);
    run_drawlist_tests(&passed, &failed);
    tally(passed, failed);
    run_timing_tests(&passed, &failed);
    tally(passed, failed);
    run_color_tests(&passed, &failed);
    tally(passed, failed);
    run_theme_tests(&passed, &failed);
    tally(passed, failed);
    run_font_measure_tests(&passed, &failed);
    tally(passed, failed);
    end_category();

    /* ---- widgets/ ---- */
    begin_category("widgets");
    run_button_tests(&passed, &failed);
    tally(passed, failed);
    run_container_tests(&passed, &failed);
    tally(passed, failed);
    run_label_tests(&passed, &failed);
    tally(passed, failed);
    run_glass_tests(&passed, &failed);
    tally(passed, failed);
    run_inputs_tests(&passed, &failed);
    tally(passed, failed);
    run_slider_tests(&passed, &failed);
    tally(passed, failed);
    run_progress_tests(&passed, &failed);
    tally(passed, failed);
    run_chip_tests(&passed, &failed);
    tally(passed, failed);
    run_selection_tests(&passed, &failed);
    tally(passed, failed);
    run_overlays_tests(&passed, &failed);
    tally(passed, failed);
    run_display_tests(&passed, &failed);
    tally(passed, failed);
    run_forms_tests(&passed, &failed);
    tally(passed, failed);
    run_canvas_tests(&passed, &failed);
    tally(passed, failed);
    run_table_tests(&passed, &failed);
    tally(passed, failed);
    end_category();

    /* ---- markup/ ---- */
    begin_category("markup");
    run_incense_tests(&passed, &failed);
    tally(passed, failed);
    end_category();

    printf("\n=== Summary ===\n");
    printf("Total: %d passed, %d failed\n", s_total_passed, s_total_failed);

    if (s_total_failed == 0)
    {
        printf("\nAll tests passed!\n");
    }
    else
    {
        printf("\nSome tests failed!\n");
    }

    return s_total_failed > 0 ? 1 : 0;
}
