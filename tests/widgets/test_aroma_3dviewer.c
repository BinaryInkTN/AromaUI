




















#include "test_aroma_3dviewer.h"
#include "widgets/aroma_3d_viewer.h"
#include "aroma_3d.h"
#include "aroma_timer.h"
#include "aroma_incense_loader.h"
#include "widgets/aroma_container.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

static void test_viewer_basics(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_3d_viewer_create(NULL, 0, 0, 100, 100) == NULL,
          "viewer rejects NULL parent");

    AromaNode *viewer = aroma_3d_viewer_create(root, 20, 76, 280, 280);
    CHECK(viewer != NULL, "viewer created");
    if (!viewer)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_3d_viewer_get_model(viewer) == NULL, "viewer starts model-free");
    CHECK(aroma_3d_viewer_get_interactive(viewer) == true, "viewer interactive by default");
    CHECK(aroma_3d_viewer_get_model(NULL) == NULL, "viewer get model NULL safe");
    CHECK(aroma_3d_viewer_get_interactive(NULL) == false, "viewer get interactive NULL safe");

    aroma_3d_viewer_set_interactive(viewer, false);
    CHECK(aroma_3d_viewer_get_interactive(viewer) == false, "viewer toggles interactive off");
    aroma_3d_viewer_set_interactive(viewer, true);
    CHECK(aroma_3d_viewer_get_interactive(viewer) == true, "viewer toggles interactive on");
    aroma_3d_viewer_set_interactive(NULL, true);
    aroma_3d_viewer_set_model(NULL, NULL);

    __destroy_node(root);
    __node_system_destroy();
}

static void test_incense_cube(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *parent = aroma_container_create(root, 0, 0, 320, 480);
    IncenseRegistry *reg = NULL;

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "ThreeDViewer { id: \"studio\" x: 20 y: 76 width: 280 height: 280 "
              "model: cube auto_rotate: true interactive: true } }",
              parent, NULL, NULL, &reg),
          "incense loads cube viewer");
    AromaNode *viewer = reg ? IncenseFindWidget(reg, "studio") : NULL;
    CHECK(viewer != NULL, "cube viewer registered by id");
    if (viewer)
    {
        CHECK(aroma_3d_viewer_get_model(viewer) != NULL, "cube model attached");
        CHECK(aroma_3d_viewer_get_interactive(viewer) == true, "cube viewer stays interactive");
    }
    if (reg)
    {
        IncenseFreeRegistry(reg);
        reg = NULL;
    }

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "ThreeDViewer { id: \"empty\" x: 20 y: 76 width: 280 height: 280 } }",
              parent, NULL, NULL, &reg),
          "incense loads model-free viewer");
    AromaNode *empty = reg ? IncenseFindWidget(reg, "empty") : NULL;
    CHECK(empty != NULL, "model-free viewer registered by id");
    if (empty)
        CHECK(aroma_3d_viewer_get_model(empty) == NULL, "model-free viewer has no model");
    if (reg)
    {
        IncenseFreeRegistry(reg);
        reg = NULL;
    }

    CHECK(IncenseLoadStringIntoParent(
              "Window { width: 320 height: 480 title: \"T\" "
              "ThreeDViewer { id: \"fox\" x: 20 y: 76 width: 280 height: 280 "
              "model: fox auto_rotate: true interactive: true } }",
              parent, NULL, NULL, &reg),
          "incense loads embedded fox");
    AromaNode *fox = reg ? IncenseFindWidget(reg, "fox") : NULL;
    CHECK(fox != NULL, "fox viewer registered by id");
    if (fox)
    {
        Aroma3DModel *model = aroma_3d_viewer_get_model(fox);
        CHECK(model != NULL, "fox model attached");
        if (model)
            CHECK(aroma_3d_get_mesh_count(model) == 1, "fox has one mesh");
        CHECK(aroma_3d_viewer_get_interactive(fox) == true, "fox viewer stays interactive");
    }
    if (reg)
        IncenseFreeRegistry(reg);

    __destroy_node(root);
    __node_system_destroy();
}

static float viewer_theta(AromaNode *viewer)
{
    Aroma3DCamera cam;
    memset(&cam, 0, sizeof(cam));
    if (!aroma_3d_viewer_get_camera(viewer, &cam))
        return 0.0f;
    return cam.theta;
}

static void test_auto_rotate_timer(void)
{
    aroma_timer_init();
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *parent = aroma_container_create(root, 0, 0, 320, 480);

    AromaNode *viewer = aroma_3d_viewer_create(parent, 20, 76, 280, 280);
    CHECK(viewer != NULL, "spin viewer created");
    if (!viewer)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }




    aroma_3d_viewer_set_auto_rotate(viewer, true);
    float t0 = viewer_theta(viewer);
    float t1 = t0, t2 = t0, t3 = t0;
    for (uint64_t t = 0; t <= 6300; t += 33)
    {
        aroma_timer_tick(t);
        if (t1 == t0 && t >= 1000)
            t1 = viewer_theta(viewer);
        if (t2 == t0 && t >= 3000)
            t2 = viewer_theta(viewer);
        t3 = viewer_theta(viewer);
    }
    CHECK(t1 < t0 && t2 < t1 && t3 < t2, "auto-rotate spins for over 6s");
    (void)t0;

    aroma_3d_viewer_set_auto_rotate(viewer, false);
    float held = viewer_theta(viewer);
    for (uint64_t t = 6333; t <= 9300; t += 33)
        aroma_timer_tick(t);
    CHECK(viewer_theta(viewer) == held, "auto-rotate stops when disabled");

    __destroy_node(root);
    __node_system_destroy();
}

void run_3dviewer_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    test_viewer_basics();
    test_incense_cube();
    test_auto_rotate_timer();
    printf("[3dviewer] passed=%d failed=%d\n", s_passed, s_failed);
    if (passed)
        *passed += s_passed;
    if (failed)
        *failed += s_failed;
}
