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

#include "test_aroma_incense.h"
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
    /* NULL registry out-param is accepted. */
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

    /* No fonts available headless: only font-free widgets are mounted.
     * Container creation is font-independent. */
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

void run_incense_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Incense Tests ===\n");
    test_rejections();
    test_minimal_document();
    printf("Aroma Incense: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
