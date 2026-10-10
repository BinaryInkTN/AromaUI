




















#include "test_aroma_segmented.h"
#include "widgets/aroma_segmented.h"
#include "widgets/aroma_tabs.h"
#include "widgets/aroma_button.h"
#include "aroma_incense_loader.h"
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

static void test_segmented(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    const char *labels[] = {"Years", "Months", "Days", "All Photos"};

    CHECK(aroma_segmented_create(NULL, 0, 0, 280, 32, labels, 4) == NULL,
          "segmented rejects NULL parent");
    CHECK(aroma_segmented_create(root, 0, 0, 280, 32, NULL, 4) == NULL,
          "segmented rejects NULL labels");
    CHECK(aroma_segmented_create(root, 0, 0, 280, 32, labels, 0) == NULL,
          "segmented rejects empty count");

    AromaNode *seg = aroma_segmented_create(root, 20, 72, 280, 32, labels, 4);
    CHECK(seg != NULL, "segmented created");
    if (!seg)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_segmented_get_selected(seg) == 0, "segmented starts on first");
    CHECK(aroma_segmented_get_selected(NULL) == -1, "segmented get NULL returns -1");

    aroma_segmented_set_selected(seg, 3);
    CHECK(aroma_segmented_get_selected(seg) == 3, "segmented selects last");
    aroma_segmented_set_selected(seg, 3);
    CHECK(aroma_segmented_get_selected(seg) == 3, "segmented reselect stable");
    aroma_segmented_set_selected(seg, 99);
    CHECK(aroma_segmented_get_selected(seg) == 3, "segmented ignores overflow");
    aroma_segmented_set_selected(seg, -1);
    CHECK(aroma_segmented_get_selected(seg) == 3, "segmented ignores negative");
    aroma_segmented_set_selected(NULL, 1);
    aroma_segmented_set_on_change(seg, NULL, NULL);
    aroma_segmented_set_font(seg, NULL);
    aroma_segmented_setup_events(seg, NULL, NULL);

    aroma_segmented_destroy(NULL);
    aroma_segmented_destroy(seg);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_tabs_variant(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    const char *labels[] = {"Library", "Albums"};

    AromaNode *tabs = aroma_tabs_create(root, 0, 424, 320, 56, labels, 2);
    CHECK(tabs != NULL, "tabs created for variant test");
    if (!tabs)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_tabs_get_variant(tabs) == TABS_VARIANT_TOP, "tabs default to top variant");
    CHECK(aroma_tabs_get_variant(NULL) == TABS_VARIANT_TOP, "tabs variant NULL returns top");

    aroma_tabs_set_variant(tabs, TABS_VARIANT_BAR);
    CHECK(aroma_tabs_get_variant(tabs) == TABS_VARIANT_BAR, "tabs switch to bar variant");
    aroma_tabs_set_variant(tabs, TABS_VARIANT_TOP);
    CHECK(aroma_tabs_get_variant(tabs) == TABS_VARIANT_TOP, "tabs switch back to top");
    aroma_tabs_set_variant(tabs, (AromaTabsVariant)99);
    CHECK(aroma_tabs_get_variant(tabs) == TABS_VARIANT_TOP, "tabs ignore invalid variant");
    aroma_tabs_set_variant(NULL, TABS_VARIANT_BAR);

    aroma_tabs_destroy(tabs);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_button_types(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *btn = aroma_button_create(root, "See all", 20, 300, 280, 44);
    CHECK(btn != NULL, "button created for type test");
    if (!btn)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_FILLED, "button defaults to filled");
    CHECK(aroma_button_get_type(NULL) == BUTTON_TYPE_FILLED, "button type NULL returns filled");

    aroma_button_set_type(btn, BUTTON_TYPE_TONAL);
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_TONAL, "button switches to tonal");
    aroma_button_set_type(btn, BUTTON_TYPE_OUTLINED);
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_OUTLINED, "button switches to outlined");
    aroma_button_set_type(btn, BUTTON_TYPE_TEXT);
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_TEXT, "button switches to text");
    aroma_button_set_type(btn, BUTTON_TYPE_ELEVATED);
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_ELEVATED, "button switches to elevated");
    aroma_button_set_type(btn, (AromaButtonType)99);
    CHECK(aroma_button_get_type(btn) == BUTTON_TYPE_ELEVATED, "button ignores invalid type");
    aroma_button_set_type(NULL, BUTTON_TYPE_TONAL);

    aroma_button_destroy(btn);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_incense_bindings(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *parent = aroma_container_create(root, 0, 0, 320, 480);
    IncenseRegistry *reg = NULL;

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "Button { x: 20 y: 300 width: 280 height: 44 text: \"See all\" type: tonal } }",
              parent, NULL, NULL, &reg),
          "incense loads typed button");

    CHECK(parent->child_count == 1, "typed button builds one widget");
    if (parent->child_count == 1)
        CHECK(aroma_button_get_type(parent->child_nodes[0]) == BUTTON_TYPE_TONAL,
              "incense button type applied");
    if (reg)
    {
        IncenseFreeRegistry(reg);
        reg = NULL;
    }

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "Tabs { x: 0 y: 424 width: 320 height: 56 variant: bar selected: 1 "
              "Tab { text: \"Library\" } Tab { text: \"Albums\" } } }",
              parent, NULL, NULL, &reg),
          "incense loads bar tabs");
    CHECK(parent->child_count == 2, "bar tabs append one widget");
    if (parent->child_count == 2)
    {
        AromaNode *tabs_node = parent->child_nodes[1];
        CHECK(aroma_tabs_get_variant(tabs_node) == TABS_VARIANT_BAR,
              "incense tabs variant applied");
        CHECK(aroma_tabs_get_selected(tabs_node) == 1,
              "incense tabs selected applied");
    }
    if (reg)
    {
        IncenseFreeRegistry(reg);
        reg = NULL;
    }

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "SegmentedControl { x: 20 y: 72 width: 280 height: 32 selected: 1 "
              "Segment { text: \"Years\" } Segment { text: \"Days\" } } }",
              parent, NULL, NULL, &reg),
          "incense loads segmented control");
    CHECK(parent->child_count == 3, "segmented control appends one widget");
    if (parent->child_count == 3)
        CHECK(aroma_segmented_get_selected(parent->child_nodes[2]) == 1,
              "incense segmented selected applied");
    if (reg)
        IncenseFreeRegistry(reg);

    __destroy_node(root);
    __node_system_destroy();
}

void run_segmented_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    test_segmented();
    test_tabs_variant();
    test_button_types();
    test_incense_bindings();
    printf("[segmented] passed=%d failed=%d\n", s_passed, s_failed);
    if (passed)
        *passed += s_passed;
    if (failed)
        *failed += s_failed;
}
