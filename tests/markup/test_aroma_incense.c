




















#include "test_aroma_incense.h"
#include "aroma_incense_loader.h"
#include "aroma_animation.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "widgets/aroma_container.h"
#include "widgets/aroma_label.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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







static void test_grid_layout(void)
{
    __node_system_init();
    AromaNode *parent = make_parent();
    if (!parent)
    {
        CHECK(0, "grid parent created");
        __node_system_destroy();
        return;
    }

    const char *src =
        "Window {\n"
        "    width: 320\n"
        "    height: 480\n"
        "    Container { x: 0 y: 0 width: 320 height: 480 layout: grid cols: 2 rows: 2 gap: 8\n"
        "        Label { text: \"A\" x: 0 y: 0 }\n"
        "        Label { text: \"B\" x: 0 y: 0 }\n"
        "        Label { text: \"C\" x: 0 y: 0 }\n"
        "        Label { text: \"D\" x: 0 y: 0 }\n"
        "    }\n"
        "}\n";
    IncenseRegistry *reg = NULL;
    bool ok = IncenseLoadStringIntoParent(src, parent, NULL, NULL, &reg);
    CHECK(ok, "grid document mounts");
    int nerrs = 0;
    IncenseGetErrors(&nerrs);
    CHECK(nerrs == 0, "grid document loads without errors");

    /* One layout pass, as a single frame would perform. */
    aroma_node_update_layout(parent, 0, 0, 1024, 600);

    AromaNode *box = NULL;
    if (parent->child_count > 0)
        box = parent->child_nodes[parent->child_count - 1];
    CHECK(box && box->child_count == 4, "grid has four cells");
    if (box && box->child_count == 4)
    {
        /* 2x2 over 320x480 with gap 8: cells 156x236 at
         * (0,0), (164,0), (0,244), (164,244). Positions must hold
         * without pumping any frames (no corner flash). */
        static const int ex[4] = {0, 164, 0, 164};
        static const int ey[4] = {0, 0, 244, 244};
        for (uint64_t i = 0; i < 4; i++)
        {
            AromaRect *r = aroma_node_get_rect(box->child_nodes[i]);
            char name[64];
            snprintf(name, sizeof(name), "grid cell %llu placed", (unsigned long long)i);
            CHECK(r && r->x == ex[i] && r->y == ey[i], name);
        }
    }
    if (reg)
        IncenseFreeRegistry(reg);
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

static bool write_text_file(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return false;
    size_t len = strlen(text);
    bool ok = len == 0 || fwrite(text, 1, len, fp) == len;
    fclose(fp);
    return ok;
}

static bool make_temp_dir(char *out, size_t cap)
{
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH)
        return false;
    snprintf(out, cap, "%saroma_embed_test_%lu", tmp,
             (unsigned long)GetCurrentProcessId());
    if (!CreateDirectoryA(out, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    return true;
#else
    snprintf(out, cap, "/tmp/aroma_embed_test_XXXXXX");
    return mkdtemp(out) != NULL;
#endif
}

static void test_embed_with_props(void)
{
    char dir[512];
    if (!make_temp_dir(dir, sizeof(dir)))
    {
        CHECK(1, "embed props test (skipped: no temp dir)");
        return;
    }

    /* Deliberately overloads an otherwise generic word ("cards") so the
       substitution has to match whole prop names only. */
    const char *cards =
        "Container {\n"
        "    x: 0\n"
        "    y: 0\n"
        "    width: 100\n"
        "    height: 60\n"
        "    Label {\n"
        "        id: \"card_lbembed_idx\"\n"
        "        text: \"embed_title\"\n"
        "        x: 0\n"
        "        y: 0\n"
        "        size: 12\n"
        "        color: #FFFFFF\n"
        "    }\n"
        "}\n";

    const char *root =
        "Window {\n"
        "    width: 1024\n"
        "    height: 600\n"
        "    @embed \"cards.aroma\" { title: \"First card\", idx: \"1\" }\n"
        "    @embed \"cards.aroma\" { title: \"Second card\", idx: \"2\" }\n"
        "}\n";

    char cards_path[600], root_path[600];
    snprintf(cards_path, sizeof(cards_path), "%s/cards.aroma", dir);
    snprintf(root_path, sizeof(root_path), "%s/root.aroma", dir);
    bool wrote = write_text_file(cards_path, cards) &&
                 write_text_file(root_path, root);
    CHECK(wrote, "embed props test files written");

    if (wrote)
    {
        __node_system_init();
        AromaNode *parent = make_parent();
        IncenseRegistry *reg = NULL;
        bool ok = IncenseLoadFileIntoParent(root_path, parent, NULL, NULL, &reg);
        CHECK(ok, "embed file loads");
        AromaNode *lb1 = IncenseFindWidget(reg, "card_lb1");
        AromaNode *lb2 = IncenseFindWidget(reg, "card_lb2");
        CHECK(lb1 != NULL, "first embed label mounted");
        CHECK(lb2 != NULL, "second embed label mounted");
        if (lb1)
            CHECK(strcmp(aroma_label_get_text(lb1), "First card") == 0,
                  "first embed prop substituted");
        if (lb2)
            CHECK(strcmp(aroma_label_get_text(lb2), "Second card") == 0,
                  "second embed prop overridden");
        if (reg)
            IncenseFreeRegistry(reg);
        __destroy_node(parent->parent_node);
        __node_system_destroy();
    }

    remove(cards_path);
    remove(root_path);
#ifndef _WIN32
    rmdir(dir);
#else
    RemoveDirectoryA(dir);
#endif
}

static void test_example_dashboard_embeds(void)
{
    /* The docs demo ships a flattened copy of this markup; loading the real
       example from the repo root proves the @embed tree resolves too. */
    const char *path = "examples/16_loud_dashboard/ui/app.aroma";
    FILE *probe = fopen(path, "rb");
    if (!probe)
    {
        CHECK(1, "loud dashboard embeds (skipped: example not present)");
        return;
    }
    fclose(probe);

    __node_system_init();
    AromaNode *parent = make_parent();
    IncenseRegistry *reg = NULL;
    IncenseClearErrors();
    bool ok = IncenseLoadFileIntoParent(path, parent, NULL, NULL, &reg);
    (void)ok;

    /* Headless hosts never register the dashboard's C callbacks, so the load
       reports a semantic error for each one. Anything else (syntax errors,
       broken embeds, ...) still fails the test. */
    int err_count = 0;
    const IncenseError *errs = IncenseGetErrors(&err_count);
    int unexpected = 0;
    for (int i = 0; i < err_count; i++)
    {
        if (errs[i].type != INCENSE_ERROR_SYNTAX &&
            errs[i].type != INCENSE_ERROR_SEMANTIC)
            continue;
        if (strstr(errs[i].message, "not registered"))
            continue;
        unexpected++;
    }
    CHECK(unexpected == 0, "loud dashboard only reports unregistered-callback errors");
    CHECK(IncenseFindWidget(reg, "page_analytics") != NULL,
          "loud dashboard analytics page mounted");
    CHECK(IncenseFindWidget(reg, "page_settings") != NULL,
          "loud dashboard settings page mounted");
    IncenseClearErrors();
    if (reg)
        IncenseFreeRegistry(reg);
    __destroy_node(parent->parent_node);
    __node_system_destroy();
}

void run_incense_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Incense Tests ===\n");
    test_rejections();
    test_minimal_document();
    test_grid_layout();
    test_animation_settles_on_layout();
    test_embed_with_props();
    test_example_dashboard_embeds();
    printf("Aroma Incense: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
