




















#include "test_aroma_table.h"
#include "widgets/aroma_table.h"
#include "widgets/aroma_button.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <string.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                      \
            s_passed++;                                                  \
        } else {                                                         \
            s_failed++;                                                  \
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

    CHECK(aroma_table_create(NULL, 0, 0, 300, 200, 3) == NULL,
          "table rejects NULL parent");
    CHECK(aroma_table_create(root, 0, 0, 300, 200, 0) == NULL,
          "table rejects zero columns");
    CHECK(aroma_table_create(root, 0, 0, 300, 200, -2) == NULL,
          "table rejects negative columns");
    CHECK(aroma_table_create(root, 0, 0, 300, 200, 11) == NULL,
          "table rejects too many columns");

    AromaNode *t = aroma_table_create(root, 0, 0, 300, 200, 3);
    CHECK(t != NULL, "table creates with valid args");
    CHECK(aroma_table_get_row_count(t) == 0, "new table has no rows");
    CHECK(aroma_table_get_row_count(NULL) == 0, "row count NULL-safe");
    CHECK(aroma_table_get_selected_row(t) == -1, "new table nothing selected");

    aroma_table_destroy(t);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_rows_and_cells(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *t = aroma_table_create(root, 0, 0, 300, 200, 2);
    CHECK(t != NULL, "table creates for row tests");

    CHECK(aroma_table_add_row(t) == 0, "first row index 0");
    CHECK(aroma_table_add_row(t) == 1, "second row index 1");
    CHECK(aroma_table_add_row(NULL) == -1, "add row NULL-safe");
    CHECK(aroma_table_get_row_count(t) == 2, "row count tracks adds");

    aroma_table_set_cell_text(t, 0, 0, "hello");
    aroma_table_set_cell_text(t, 1, 1, "world");

    aroma_table_set_cell_text(t, 9, 0, "nope");
    aroma_table_set_cell_text(t, 0, 9, "nope");
    aroma_table_set_cell_text(t, 0, 0, NULL);
    aroma_table_set_cell_text(NULL, 0, 0, "nope");
    CHECK(aroma_table_get_row_count(t) == 2, "bad writes do not add rows");


    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    aroma_table_set_cell_text(t, 0, 1, big);
    CHECK(aroma_table_get_row_count(t) == 2, "long text write safe");

    aroma_table_set_header(t, 0, "Name");
    aroma_table_set_header(t, 5, "ignored");
    aroma_table_set_header(NULL, 0, "ignored");
    aroma_table_set_col_width(t, 0, 150);
    aroma_table_set_col_width(t, 9, 150);
    aroma_table_set_col_width(NULL, 0, 150);
    CHECK(1, "header/width guards safe");

    aroma_table_destroy(t);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_clear_and_rebuild(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *t = aroma_table_create(root, 0, 0, 300, 200, 2);
    CHECK(t != NULL, "table creates for clear tests");

    aroma_table_add_row(t);
    aroma_table_add_row(t);
    aroma_table_add_row(t);
    AromaNode *btn = aroma_button_create(t, "Get", 0, 0, 80, 30);
    aroma_table_set_cell_widget(t, 1, 0, btn);
    aroma_table_set_selected_row(t, 1);
    CHECK(aroma_table_get_selected_row(t) == 1, "selection sticks");


    aroma_table_clear_rows(t, false);
    CHECK(aroma_table_get_row_count(t) == 0, "clear empties rows");
    CHECK(aroma_table_get_selected_row(t) == -1, "clear resets selection");
    CHECK(btn->node_id != 0, "kept widget node survives clear");
    __destroy_node(btn);


    CHECK(aroma_table_add_row(t) == 0, "row index restarts after clear");
    AromaNode *btn2 = aroma_button_create(t, "Get", 0, 0, 80, 30);
    aroma_table_set_cell_widget(t, 0, 1, btn2);


    aroma_table_clear_rows(t, true);
    CHECK(aroma_table_get_row_count(t) == 0, "destroying clear empties rows");

    aroma_table_clear_rows(NULL, true);
    CHECK(1, "clear NULL-safe");

    aroma_table_destroy(t);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_geometry_and_selection(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *t = aroma_table_create(root, 0, 0, 300, 200, 2);
    CHECK(t != NULL, "table creates for geometry tests");

    aroma_table_set_row_height(t, 56);
    aroma_table_set_row_height(t, -5);
    aroma_table_set_row_height(t, 5000);
    aroma_table_set_row_height(NULL, 56);
    CHECK(aroma_table_add_row(t) == 0, "rows work after height change");

    aroma_table_set_header_visible(t, false);
    aroma_table_set_header_visible(t, true);
    aroma_table_set_header_visible(NULL, false);
    CHECK(aroma_table_add_row(t) == 1, "rows work after header toggle");

    aroma_table_set_selected_row(t, 0);
    CHECK(aroma_table_get_selected_row(t) == 0, "select row 0");
    aroma_table_set_selected_row(t, -1);
    CHECK(aroma_table_get_selected_row(t) == -1, "deselect with -1");
    aroma_table_set_selected_row(t, 42);
    CHECK(aroma_table_get_selected_row(t) == -1, "out-of-range select ignored");
    aroma_table_set_selected_row(t, -2);
    CHECK(aroma_table_get_selected_row(t) == -1, "bad deselect ignored");
    aroma_table_set_selected_row(NULL, 0);
    CHECK(aroma_table_get_selected_row(NULL) == -1, "selection NULL-safe");

    aroma_table_set_cell_widget(t, 0, 0, NULL);
    aroma_table_set_cell_widget(t, 9, 0, NULL);
    aroma_table_set_cell_widget(NULL, 0, 0, NULL);
    aroma_table_set_callback(t, NULL, NULL);
    aroma_table_set_callback(NULL, NULL, NULL);
    aroma_table_set_font(t, NULL);
    aroma_table_set_font(NULL, NULL);
    aroma_table_destroy(NULL);
    CHECK(1, "widget/callback/font guards safe");

    aroma_table_destroy(t);
    __destroy_node(root);
    __node_system_destroy();
}

void run_table_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Table Tests ===\n");
    test_create_guards();
    test_rows_and_cells();
    test_clear_and_rebuild();
    test_geometry_and_selection();
    printf("Aroma Table: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
