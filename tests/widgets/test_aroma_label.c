




















#include "test_aroma_label.h"
#include "widgets/aroma_label.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <string.h>

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

    CHECK(aroma_label_create(NULL, "x", 0, 0, LABEL_STYLE_LABEL_MEDIUM) == NULL,
          "label rejects NULL parent");
    CHECK(aroma_label_create(root, NULL, 0, 0, LABEL_STYLE_LABEL_MEDIUM) == NULL,
          "label rejects NULL text");

    AromaNode *label = aroma_label_create(root, "Hello", 10, 20,
                                          LABEL_STYLE_LABEL_MEDIUM);
    CHECK(label != NULL, "label created");
    if (label)
    {
        const char *text = aroma_label_get_text(label);
        CHECK(text && strcmp(text, "Hello") == 0, "label text roundtrips");

        AromaRect *r = aroma_node_get_rect(label);
        CHECK(r && r->x == 10 && r->y == 20, "label position stored");
    }

    CHECK(aroma_label_get_text(NULL) == NULL, "get_text NULL-safe");
    CHECK(aroma_label_get_font(NULL) == NULL, "get_font NULL-safe");

    aroma_label_destroy(NULL);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_set_text(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *label = aroma_label_create(root, "First", 0, 0,
                                          LABEL_STYLE_LABEL_SMALL);
    if (!label)
    {
        CHECK(0, "label created for set_text");
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    aroma_label_set_text(label, "Second");
    const char *text = aroma_label_get_text(label);
    CHECK(text && strcmp(text, "Second") == 0, "set_text updates");


    char big[200];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    aroma_label_set_text(label, big);
    text = aroma_label_get_text(label);
    CHECK(text && strlen(text) == 95, "long text truncates to buffer");
    CHECK(text[95] == '\0', "truncated text stays terminated");


    aroma_label_set_text(label, NULL);
    text = aroma_label_get_text(label);
    CHECK(text && strlen(text) == 95, "NULL set_text keeps content");
    aroma_label_set_text(NULL, "x");

    CHECK(aroma_label_get_scale(label) > 0.0f, "label has positive scale");
    aroma_label_destroy(label);
    __destroy_node(root);
    __node_system_destroy();
}

void run_label_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Label Tests ===\n");
    test_create_guards();
    test_set_text();
    printf("Aroma Label: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
