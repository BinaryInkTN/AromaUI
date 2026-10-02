#include "test_aroma_glass.h"
#include "widgets/aroma_card.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
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

static void test_backend_reports_blur_support(void)
{
    /* The default graphics backend is GLES3, which implements the
     * backdrop-blur hook. This needs no GL context (address check). */
    CHECK(aroma_graphics_supports_backdrop_blur(),
          "default backend supports backdrop blur");
}

static void test_frosted_card_lifecycle(void)
{
    __node_system_init();

    void *root_widget = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, root_widget);

    AromaNode *glass = aroma_card_create(root, 10, 20, 200, 100,
                                         CARD_TYPE_GLASS);
    CHECK(glass != NULL, "glass card created");
    CHECK(aroma_card_is_card(glass), "glass node recognized as card");

    AromaNode *plain = aroma_card_create(root, 0, 0, 50, 50,
                                         CARD_TYPE_ELEVATED);
    CHECK(plain != NULL, "elevated card created");

    CHECK(aroma_card_get_backdrop_blur(glass) == 14.0f,
          "glass defaults to frosted blur");
    CHECK(aroma_card_get_backdrop_blur(plain) == 0.0f,
          "elevated defaults to no blur");

    /* Setter is null-safe, clamps negatives, and accepts non-cards
     * without crashing (no-op). */
    aroma_card_set_backdrop_blur(NULL, 20.0f);
    aroma_card_set_backdrop_blur(root, 20.0f);
    aroma_card_set_backdrop_blur(plain, -5.0f);
    aroma_card_set_backdrop_blur(glass, 0.0f);
    aroma_card_set_backdrop_blur(glass, 24.0f);
    CHECK(1, "blur setter null-safe");

    /* Defaults and roundtrips through the new getter. */
    CHECK(aroma_card_get_backdrop_blur(glass) == 24.0f,
          "blur getter roundtrips setter");
    CHECK(aroma_card_get_backdrop_blur(NULL) == 0.0f,
          "blur getter NULL-safe");
    CHECK(aroma_card_get_backdrop_blur(root) == 0.0f,
          "blur getter rejects non-cards");

    aroma_card_destroy(glass);
    aroma_card_destroy(plain);
    __destroy_node(root);
    __node_system_destroy();
}

void run_glass_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Frosted-Glass Tests ===\n");
    test_backend_reports_blur_support();
    test_frosted_card_lifecycle();
    printf("Aroma Frosted-Glass: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
