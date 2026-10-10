#include "test_aroma_event_forget.h"
#include "aroma_event.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_custom_calls = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                  \
            s_passed++;                                                  \
        } else {                                                         \
            s_failed++;                                                  \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static bool record_custom(AromaEvent *ev, void *ud)
{
    (void)ud;
    if (ev && ev->event_type == EVENT_TYPE_CUSTOM)
        s_custom_calls++;
    return true;
}

static void test_queued_event_dropped_on_destroy(void)
{
    __node_system_init();
    aroma_event_system_init();
    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *cw = aroma_widget_alloc(32);
    AromaNode *child = __add_child_node(NODE_TYPE_WIDGET, root, cw);
    aroma_event_set_root(root);
    uint64_t id = child->node_id;

    CHECK(aroma_event_subscribe(id, EVENT_TYPE_CUSTOM, record_custom, NULL,
                                10),
          "subscribe custom on child");
    AromaEvent *ev = aroma_event_create_custom(id, 77, NULL, NULL);
    CHECK(ev != NULL, "custom event created");
    CHECK(aroma_event_queue(ev), "custom event queued");

    /* Destroy while the event is still queued: the queue entry, the
     * target cache, and the listeners must all go away. */
    __destroy_node(child);
    s_custom_calls = 0;
    aroma_event_process_queue();
    CHECK(s_custom_calls == 0, "queued event dropped after destroy");

    /* New events for the dead id resolve to NULL and never dispatch. */
    AromaEvent *ev2 = aroma_event_create_custom(id, 77, NULL, NULL);
    CHECK(ev2 != NULL, "event object still creatable");
    CHECK(ev2->target_node == NULL, "dead id resolves to NULL");
    CHECK(!aroma_event_dispatch(ev2), "dispatch to dead id is dropped");
    CHECK(s_custom_calls == 0, "no handler ran for dead id");
    aroma_event_destroy(ev2);

    aroma_event_forget_node(0);
    aroma_event_forget_node(id);
    CHECK(1, "redundant forget is safe");

    aroma_event_system_shutdown();
    __destroy_node(root);
    __node_system_destroy();
}

static void test_forget_without_system(void)
{
    /* Must be safe before init / after shutdown. */
    aroma_event_forget_node(12345);
    CHECK(1, "forget with no system is safe");
}

void run_event_forget_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Event Forget Tests ===\n");
    test_forget_without_system();
    test_queued_event_dropped_on_destroy();
    printf("Aroma Event Forget: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
