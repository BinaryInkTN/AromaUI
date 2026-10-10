




















#include "test_aroma_container.h"
#include "widgets/aroma_container.h"
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

static void test_create_guards(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_container_create(NULL, 0, 0, 100, 100) == NULL,
          "container rejects NULL parent");
    CHECK(aroma_container_create(root, 0, 0, 0, 100) == NULL,
          "container rejects zero width");
    CHECK(aroma_container_create(root, 0, 0, 100, -5) == NULL,
          "container rejects negative height");

    AromaNode *c = aroma_container_create(root, 10, 20, 200, 100);
    CHECK(c != NULL, "container created");
    if (c)
    {
        AromaRect *r = aroma_node_get_rect(c);
        CHECK(r && r->x == 10 && r->y == 20 && r->width == 200 &&
                  r->height == 100,
              "container rect stored");
    }

    aroma_container_destroy(NULL);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_scroll_state(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *c = aroma_container_create(root, 0, 0, 200, 100);
    if (!c)
    {
        CHECK(0, "container created for scroll test");
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    CHECK(!aroma_container_is_scrollable(c), "not scrollable by default");
    CHECK(!aroma_container_is_scrollable(NULL), "NULL is not scrollable");

    int sx = -1, sy = -1;
    aroma_container_get_scroll(c, &sx, &sy);
    CHECK(sx == 0 && sy == 0, "scroll offsets start at zero");
    aroma_container_get_scroll(NULL, &sx, &sy);
    CHECK(sx == 0 && sy == 0, "NULL scroll query zeroes outputs");

    int cw = -1, ch = -1;
    aroma_container_get_content_size(c, &cw, &ch);
    CHECK(cw == 200 && ch == 100, "content size matches rect initially");

    aroma_container_set_scrollable(c, true);
    CHECK(aroma_container_is_scrollable(c), "scrollable after enable");
    aroma_container_set_scrollable(c, true);
    CHECK(aroma_container_is_scrollable(c), "scrollable still on");
    aroma_container_set_scrollable(c, false);
    CHECK(!aroma_container_is_scrollable(c), "not scrollable after disable");
    aroma_container_set_scrollable(NULL, true);

    aroma_container_set_rect(c, 5, 5, 300, 150);
    AromaRect *r = aroma_node_get_rect(c);
    CHECK(r && r->x == 5 && r->width == 300, "set_rect updates geometry");
    aroma_container_set_rect(NULL, 0, 0, 1, 1);

    aroma_container_destroy(c);
    __destroy_node(root);
    __node_system_destroy();
}

void run_container_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Container Tests ===\n");
    test_create_guards();
    test_scroll_state();
    printf("Aroma Container: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
