




















#include "test_aroma_incense.h"
#include "aroma_incense_loader.h"
#include "aroma_animation.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "widgets/aroma_container.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

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

static AromaNode *make_parent(void)
{
    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    return aroma_container_create(root, 0, 0, 1024, 600);
}

static void test_rejections(void)
{
    __node_system_init();
    AromaNode *parent = make_parent();
    IncenseRegistry *reg = NULL;
    int nerrs = 0;

    CHECK(!IncenseLoadStringIntoParent(NULL, parent, NULL, NULL, &reg),
          "loader rejects NULL source");
    CHECK(!IncenseLoadStringIntoParent("", parent, NULL, NULL, &reg),
          "loader rejects empty source");
    CHECK(!IncenseLoadStringIntoParent("this is not ui markup {{{",
                                       parent, NULL, NULL, &reg),
          "loader rejects garbage");
    CHECK(IncenseGetErrors(&nerrs) != NULL || nerrs >= 0,
          "error list query safe");
    CHECK(!IncenseLoadFileIntoParent("/nonexistent/missing.aroma", parent,
                                     NULL, NULL, &reg),
          "loader rejects missing file");
    CHECK(!IncenseLoadFileIntoParent(NULL, parent, NULL, NULL, &reg),
          "loader rejects NULL path");

    CHECK(!IncenseLoadStringIntoParent("{{{", parent, NULL, NULL, NULL),
          "loader rejects garbage without registry");

    if (reg)
        IncenseFreeRegistry(reg);
    __destroy_node(parent->parent_node);
    __node_system_destroy();
}

static void test_minimal_document(void)
{
    __node_system_init();
    AromaNode *parent = make_parent();
    if (!parent)
    {
        CHECK(0, "incense parent created");
        __node_system_destroy();
        return;
    }
    uint64_t before = parent->child_count;



    const char *src =
        "Window {\n"
        "    width: 1024\n"
        "    height: 600\n"
        "    Container { x: 10 y: 10 width: 200 height: 100 }\n"
        "}\n";
    IncenseRegistry *reg = NULL;
    bool ok = IncenseLoadStringIntoParent(src, parent, NULL, NULL, &reg);
    CHECK(ok, "minimal document mounts");
    CHECK(parent->child_count > before, "mount adds child nodes");
    if (reg)
        IncenseFreeRegistry(reg);
    else
        CHECK(0, "mount returns a registry");

    __destroy_node(parent->parent_node);
    __node_system_destroy();
}







static void test_animation_settles_on_layout(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    AromaNode *parent = make_parent();
    if (!parent || !parent->parent_node)
    {
        CHECK(0, "animation parent created");
        __node_system_destroy();
        return;
    }



    AromaRect *root_rect = aroma_node_get_rect(parent->parent_node);
    root_rect->x = 0;
    root_rect->y = 0;
    root_rect->width = 1024;
    root_rect->height = 600;


    AromaRect *pr = aroma_node_get_rect(parent);
    pr->x = 0;
    pr->y = 76;
    pr->width = 392;
    pr->height = 724;

    const char *src =
        "Window {\n"
        "    width: 392\n"
        "    height: 724\n"
        "    Container { x: 0 y: 0 width: 392 height: 724\n"
        "        Dropdown { x: 24 y: 196 width: 343 height: 40 }\n"
        "        Button {\n"
        "            text: \"Primary action\"\n"
        "            animation: slide_y\n"
        "            animation_start_val: 300\n"
        "            animation_end_val: 276\n"
        "            animation_duration: 120\n"
        "            x: 24\n"
        "            y: 276\n"
        "            width: 245\n"
        "            height: 44\n"
        "        }\n"
        "    }\n"
        "}\n";
    IncenseRegistry *reg = NULL;

    bool ok = IncenseLoadStringIntoParent(src, parent, NULL, NULL, &reg);
    CHECK(ok, "theming snippet mounts");
    if (!ok)
    {
        if (reg)
            IncenseFreeRegistry(reg);
        __destroy_node(parent->parent_node);
        __node_system_destroy();
        return;
    }


    aroma_node_update_layout(parent->parent_node, 0, 0, 1024, 600);
    for (int i = 0; i < 60; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(25);
#else
        usleep(25000);
#endif
    }

    AromaRect *dd = NULL;
    AromaRect *btn = NULL;
    AromaNode *stack[256];
    int top = 0;
    stack[top++] = parent;
    while (top > 0)
    {
        AromaNode *n = stack[--top];
        if (!n || !n->node_widget_ptr)
            continue;
        AromaRect *r = aroma_node_get_rect(n);
        if (r && r->width == 343 && r->height == 40)
            dd = r;
        if (r && r->width == 245 && r->height == 44)
            btn = r;
        if (n->child_nodes)
            for (uint64_t k = 0; k < n->child_count && top < 256; k++)
                if (n->child_nodes[k])
                    stack[top++] = n->child_nodes[k];
    }
    CHECK(dd != NULL, "dropdown rect found");
    CHECK(btn != NULL, "animated button rect found");
    if (dd && btn)
    {

        CHECK(dd->y == 272, "dropdown keeps layout position");
        CHECK(btn->y == 352, "slide lands on parent+end, not on dropdown");
        CHECK(btn->y >= dd->y + dd->height, "button clears the dropdown");
    }
    aroma_animation_cleanup_all();
    if (reg)
        IncenseFreeRegistry(reg);
    __destroy_node(parent->parent_node);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

void run_incense_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Incense Tests ===\n");
    test_rejections();
    test_minimal_document();
    test_animation_settles_on_layout();
    printf("Aroma Incense: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
