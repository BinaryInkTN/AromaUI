#include "test_aroma_video.h"
#include "widgets/aroma_video.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "aroma_event.h"

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

static void test_video_create_validation(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    CHECK(aroma_video_create(NULL, NULL, 0, 0, 100, 100) == NULL,
          "video rejects NULL parent");
    CHECK(aroma_video_create(root, NULL, 0, 0, 0, 100) == NULL,
          "video rejects zero width");
    CHECK(aroma_video_create(root, NULL, 0, 0, 100, -5) == NULL,
          "video rejects negative height");

    AromaNode *v = aroma_video_create(root, NULL, 10, 20, 320, 180);
    CHECK(v != NULL, "video created in push mode");
    if (v)
    {
        CHECK(aroma_video_get_state(v) == AROMA_VIDEO_STOPPED, "push video starts stopped");
        CHECK(!aroma_video_is_playing(v), "push video not playing initially");
        CHECK(aroma_video_get_duration(v) == 0.0, "push video duration unknown");
        CHECK(aroma_video_get_texture_id(v) == 0, "no texture before frames");
    }

    __destroy_node(root);
    __node_system_destroy();
}

static void test_video_bad_file(void)
{
    __node_system_init();
    AromaNode *root = make_root();

    AromaNode *v = aroma_video_create(root, "/nonexistent/path/video.mpg",
                                      0, 0, 320, 180);
    CHECK(v != NULL, "video node created even for missing file");
    if (v)
    {
        CHECK(aroma_video_get_state(v) == AROMA_VIDEO_ERROR, "missing file reports error");
        aroma_video_play(v);
        CHECK(!aroma_video_is_playing(v), "play ignored in error state");
        CHECK(!aroma_video_seek(v, 1.0), "seek fails in error state");
    }

    AromaNode *m = aroma_video_create_from_memory(root, NULL, 0, 0, 0, 100, 100);
    CHECK(m == NULL, "memory source rejects NULL data");

    __destroy_node(root);
    __node_system_destroy();
}

static void test_video_state_machine(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *v = aroma_video_create(root, NULL, 0, 0, 320, 180);
    CHECK(v != NULL, "state machine node created");
    if (!v)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }

    aroma_video_play(v);
    CHECK(aroma_video_is_playing(v), "play sets playing");
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_PLAYING, "state playing after play");

    aroma_video_pause(v);
    CHECK(!aroma_video_is_playing(v), "pause clears playing");
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_PAUSED, "state paused after pause");

    aroma_video_play(v);
    aroma_video_stop(v);
    CHECK(!aroma_video_is_playing(v), "stop clears playing");
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_STOPPED, "state stopped after stop");

    aroma_video_set_looping(v, true);
    CHECK(aroma_video_get_looping(v), "looping enabled");
    aroma_video_set_looping(v, false);
    CHECK(!aroma_video_get_looping(v), "looping disabled");

    aroma_video_set_muted(v, false);
    CHECK(!aroma_video_get_muted(v), "unmuted");
    aroma_video_set_muted(v, true);
    CHECK(aroma_video_get_muted(v), "muted");

    aroma_video_set_corner_radius(v, 12.0f);
    CHECK(aroma_video_get_corner_radius(v) == 12.0f, "corner radius set");
    aroma_video_set_corner_radius(v, 10000.0f);
    CHECK(aroma_video_get_corner_radius(v) <= 90.0f + 0.001f, "corner radius clamped");

    aroma_video_set_scale_mode(v, AROMA_IMAGE_SCALE_COVER);
    CHECK(aroma_video_get_scale_mode(v) == AROMA_IMAGE_SCALE_COVER, "scale mode cover");
    aroma_video_set_scale_mode(v, (AromaImageScaleMode)99);
    CHECK(aroma_video_get_scale_mode(v) == AROMA_IMAGE_SCALE_COVER, "bad scale mode ignored");

    aroma_video_set_size(v, 640, 360);
    aroma_video_set_position(v, 5, 6);
    aroma_video_set_click_to_toggle(v, false);
    aroma_video_set_click_to_toggle(v, true);

    CHECK(!aroma_video_push_frame(NULL, NULL, 0, 0), "push rejects NULL node");
    CHECK(!aroma_video_push_frame(v, NULL, 16, 16), "push rejects NULL pixels");
    CHECK(!aroma_video_push_frame(v, (unsigned char *)"x", -1, 16), "push rejects bad size");
    CHECK(aroma_video_get_source(v) != NULL, "push source non-null");

    aroma_video_set_source(v, NULL);
    CHECK(aroma_video_get_state(v) == AROMA_VIDEO_STOPPED, "set_source NULL resets");

    CHECK(aroma_video_get_video_width(v) == 0, "no video dims in push mode");
    CHECK(aroma_video_get_video_height(v) == 0, "no video height in push mode");
    CHECK(aroma_video_get_framerate(v) == 0.0, "no framerate in push mode");
    CHECK(aroma_video_get_position(v) == 0.0, "position zero");

    __destroy_node(root);
    __node_system_destroy();
}

static int s_finished_calls = 0;
static void on_finished(AromaNode *node, void *ud)
{
    (void)node;
    (void)ud;
    s_finished_calls++;
}

static void test_video_callbacks(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *v = aroma_video_create(root, NULL, 0, 0, 100, 100);
    CHECK(v != NULL, "callback node created");
    if (v)
    {
        s_finished_calls = 0;
        aroma_video_set_on_finished(v, on_finished, NULL);
        aroma_video_set_on_frame(v, NULL, NULL);
        aroma_video_set_on_error(v, NULL, NULL);
        CHECK(s_finished_calls == 0, "finished not fired spuriously");
    }
    __destroy_node(root);
    __node_system_destroy();
}

static void test_video_controls(void)
{
    __node_system_init();
    aroma_event_system_init();
    AromaNode *root = make_root();
    aroma_event_set_root(root);
    // 320x180 at origin: play button center (23, 157), track x 44..310 at y 157.
    AromaNode *v = aroma_video_create(root, NULL, 0, 0, 320, 180);
    CHECK(v != NULL, "controls node created");
    if (!v)
    {
        __destroy_node(root);
        __node_system_destroy();
        return;
    }
    CHECK(aroma_video_get_show_controls(v), "controls shown by default");
    CHECK(aroma_video_get_controls_visible(v), "controls visible by default");
    aroma_video_set_font(v, NULL);
    aroma_video_set_controls_timeout(v, 500);
    aroma_video_set_show_controls(v, false);
    CHECK(!aroma_video_get_show_controls(v), "controls hide via api");
    CHECK(!aroma_video_get_controls_visible(v), "hidden controls not visible");
    aroma_video_set_show_controls(v, true);
    CHECK(aroma_video_get_controls_visible(v), "controls reshown");
    aroma_video_set_controls_visible(v, false);
    CHECK(!aroma_video_get_controls_visible(v), "visible cleared via api");
    aroma_video_set_controls_visible(v, true);

    aroma_video_play(v);
    CHECK(aroma_video_is_playing(v), "playing before button tap");
    AromaEvent *dn = aroma_event_create(EVENT_TYPE_MOUSE_CLICK, v->node_id);
    AromaEvent *up = aroma_event_create(EVENT_TYPE_MOUSE_RELEASE, v->node_id);
    CHECK(dn && up, "button tap events created");
    if (dn && up)
    {
        dn->data.mouse.x = 23;
        dn->data.mouse.y = 157;
        up->data.mouse.x = 23;
        up->data.mouse.y = 157;
        aroma_event_dispatch(dn);
        aroma_event_dispatch(up);
        aroma_event_destroy(dn);
        aroma_event_destroy(up);
    }
    CHECK(!aroma_video_is_playing(v), "play button pauses");
    CHECK(aroma_video_get_controls_visible(v), "tap keeps controls up");

    dn = aroma_event_create(EVENT_TYPE_MOUSE_CLICK, v->node_id);
    up = aroma_event_create(EVENT_TYPE_MOUSE_RELEASE, v->node_id);
    if (dn && up)
    {
        dn->data.mouse.x = 200;
        dn->data.mouse.y = 157;
        up->data.mouse.x = 200;
        up->data.mouse.y = 157;
        aroma_video_play(v);
        aroma_event_dispatch(dn);
        aroma_event_dispatch(up);
        aroma_event_destroy(dn);
        aroma_event_destroy(up);
    }
    CHECK(aroma_video_is_playing(v), "track tap resumes after scrub");

    __destroy_node(root);
    __node_system_destroy();
}

void run_video_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Video Tests ===\n");
    test_video_create_validation();
    test_video_bad_file();
    test_video_state_machine();
    test_video_callbacks();
    test_video_controls();
    printf("Aroma Video: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
