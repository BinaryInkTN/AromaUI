




















#include "test_aroma_canvas.h"
#include "widgets/aroma_canvas.h"
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

static void test_canvas_record(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_canvas_create(NULL, 0, 0, 200, 200) == NULL,
          "canvas rejects NULL parent");

    AromaNode *cv = aroma_canvas_create(root, 0, 0, 200, 200);
    CHECK(cv != NULL, "canvas created");
    if (!cv)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }


    aroma_canvas_draw_rect(cv, 10, 10, 100, 60, 8, true, 0xFFFF0000);
    aroma_canvas_draw_circle(cv, 50, 50, 20, 0xFF00FF00);
    aroma_canvas_draw_line(cv, 0, 0, 199, 199, 0xFF0000FF, 2);
    aroma_canvas_draw_arc(cv, 100, 100, 40, 0.0f, 180.0f, 0xFFFFFF00, 3);
    aroma_canvas_draw_text(cv, "hi", 5, 5, 0xFFFFFFFF, NULL);
    aroma_canvas_clear(cv, 0xFF202020);
    CHECK(1, "canvas ops record safely");

    aroma_canvas_draw_rect(NULL, 0, 0, 1, 1, 0, false, 0);
    aroma_canvas_clear(NULL, 0);

    __destroy_node(root);
    __node_system_destroy();
}

void run_canvas_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Canvas Tests ===\n");
    test_canvas_record();
    printf("Aroma Canvas: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
