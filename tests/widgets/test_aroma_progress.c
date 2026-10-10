




















#include "test_aroma_progress.h"
#include "widgets/aroma_progressbar.h"
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

static void test_progress_range(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_progressbar_create(NULL, 0, 0, 200, 12, 0) == NULL,
          "progressbar rejects NULL parent");

    AromaNode *bar = aroma_progressbar_create(root, 10, 10, 200, 12, 0);
    CHECK(bar != NULL, "progressbar created");
    if (!bar)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    aroma_progressbar_set_progress(bar, 0.5f);
    float v = aroma_progressbar_get_progress(bar);
    CHECK(v > 0.49f && v < 0.51f, "progress roundtrips");

    aroma_progressbar_set_progress(bar, -2.0f);
    CHECK(aroma_progressbar_get_progress(bar) == 0.0f,
          "progress clamps to zero");
    aroma_progressbar_set_progress(bar, 5.0f);
    CHECK(aroma_progressbar_get_progress(bar) == 1.0f,
          "progress clamps to one");

    aroma_progressbar_set_progress(NULL, 0.5f);
    CHECK(aroma_progressbar_get_progress(NULL) == 0.0f,
          "get_progress NULL-safe");

    aroma_progressbar_destroy(NULL);
    aroma_progressbar_destroy(bar);
    __destroy_node(root);
    __node_system_destroy();
}

void run_progress_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Progress Tests ===\n");
    test_progress_range();
    printf("Aroma Progress: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
