#include "core/aroma_event.h"
#include "aroma_gesture.h"
#include "core/aroma_node.h"
#include "aroma_ui.h"
#include "core/aroma_common.h"
#include "widgets/aroma_container.h"
#include "widgets/aroma_listview.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_logger.h"
#include "widgets/aroma_dropdown.h"
#include "widgets/aroma_stepper.h"
#include "widgets/aroma_calendar.h"
#include "widgets/aroma_datepicker.h"
#include "widgets/aroma_timepicker.h"
#include "backends/aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <assert.h>

#ifdef AROMA_THREAD_SAFE
#include <pthread.h>
#endif

#define AROMA_MAX_LISTENERS_PER_NODE 16
#define AROMA_MAX_EVENT_QUEUE 256
#define AROMA_MIN_MAP_CAPACITY 16
#define AROMA_NODE_CACHE_SIZE 32
#define AROMA_MAX_TOUCHES 10

typedef struct
{
    uint64_t node_id;
    AromaEventListener listeners[AROMA_MAX_LISTENERS_PER_NODE];
    uint32_t listener_count;
} AromaNodeEventListeners;

typedef struct
{
    uint64_t node_id;
    AromaNode *node_ptr;
    uint32_t last_access;
} AromaNodeCache;

static struct
{
    bool initialized;
    bool shutting_down;
    AromaNodeEventListeners *listener_map;
    uint32_t map_capacity;
    uint32_t map_count;
    AromaEvent *event_queue[AROMA_MAX_EVENT_QUEUE];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    AromaNode *root_node;
    AromaNodeCache node_cache[AROMA_NODE_CACHE_SIZE];
    uint32_t cache_counter;
#ifdef AROMA_THREAD_SAFE
    pthread_mutex_t mutex;
#endif
} g_event_system = {0};

static AromaEvent g_event_pool[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_list[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_head  = 0;
static uint32_t g_event_free_count = 0;

static struct
{
    int last_x;
    int last_y;
    bool button_down;
    uint64_t hovered_node_id;
    AromaNode *last_hover_node;
} g_mouse_state = {-1, -1, false, 0, NULL};

static uint64_t g_touch_captures[AROMA_MAX_TOUCHES]     = {0};
static uint64_t g_scroll_captures[AROMA_MAX_TOUCHES]    = {0};
static bool     g_scroll_intercepting[AROMA_MAX_TOUCHES] = {false};
static int      g_down_x[AROMA_MAX_TOUCHES]             = {0};
static int      g_down_y[AROMA_MAX_TOUCHES]             = {0};

static AromaNode *find_scrollable_ancestor(AromaNode *start);
static AromaNode *find_node_cached(uint64_t node_id);

/* Swipe takeover: a touch that starts on content (button, slider, label)
 * can still become a scroll. Once the finger moves past the touch slop
 * along an axis the captured ScrollView can actually scroll, the gesture
 * is handed to the container and the child receives a cancel, mirroring
 * platform touch-interception behavior. */
static bool swipe_should_take_over(int id, int x, int y, uint64_t scroll_id)
{
    if (aroma_datepicker_any_popup_open() ||
        aroma_timepicker_any_popup_open() ||
        aroma_calendar_any_popup_open())
        return false;
    AromaNode *scroll_node = find_node_cached(scroll_id);
    if (!scroll_node || !aroma_container_is_scrollable(scroll_node))
        return false;
    int dx = x - g_down_x[id];
    int dy = y - g_down_y[id];
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int slop = aroma_container_touch_slop_px();
    if (slop < 1)
        slop = 1;
    AromaScrollDirection dir = aroma_container_get_scroll_direction(scroll_node);
    int cw = 0, ch = 0;
    aroma_container_get_content_size(scroll_node, &cw, &ch);
    AromaRect *r = aroma_node_get_rect(scroll_node);
    int vw = r ? r->width : 0;
    int vh = r ? r->height : 0;
    bool can_h = (dir & AROMA_SCROLL_HORIZONTAL) && cw > vw;
    bool can_v = (dir & AROMA_SCROLL_VERTICAL) && ch > vh;
    if (can_h && adx >= slop && adx > ady)
        return true;
    if (can_v && ady >= slop && ady > adx)
        return true;
    return false;
}

#ifdef AROMA_THREAD_SAFE
#define EVENT_LOCK()   pthread_mutex_lock(&g_event_system.mutex)
#define EVENT_UNLOCK() pthread_mutex_unlock(&g_event_system.mutex)
#else
#define EVENT_LOCK()   ((void)0)
#define EVENT_UNLOCK() ((void)0)
#endif

static inline bool event_node_valid(const AromaNode *n)
{
    if (!n) return false;
    if (((uintptr_t)n % _Alignof(AromaNode)) != 0) return false;
    return true;
}

static bool is_descendant_of(const AromaNode *node, const AromaNode *ancestor)
{
    const AromaNode *cur = node;
    while (event_node_valid(cur))
    {
        if (cur == ancestor)
            return true;
        cur = cur->parent_node;
    }
    return false;
}

/*
 * Child nodes are positioned in their scroll container's content
 * coordinates. A hit is only valid while it remains inside every scrollable
 * ancestor's viewport.
 */
static bool point_in_scrollable_ancestors(const AromaNode *node, int x, int y)
{
    const AromaNode *cur = node->parent_node;

    while (event_node_valid(cur))
    {
        if (cur->node_type == NODE_TYPE_CONTAINER &&
            aroma_container_is_scrollable((AromaNode *)cur))
        {
            AromaRect *viewport = aroma_node_get_rect((AromaNode *)cur);
            if (viewport)
            {
                /*
                 * A scroll container's rect is in its parent's content
                 * coordinates. Translate it through only its scrollable
                 * ancestors so the viewport is compared in screen space.
                 * The container's own scroll does not move its viewport.
                 */
                int viewport_x = viewport->x;
                int viewport_y = viewport->y;
                const AromaNode *ancestor = cur->parent_node;
                while (event_node_valid(ancestor))
                {
                    if (ancestor->node_type == NODE_TYPE_CONTAINER &&
                        aroma_container_is_scrollable((AromaNode *)ancestor))
                    {
                        int scroll_x = 0;
                        int scroll_y = 0;
                        aroma_container_get_scroll((AromaNode *)ancestor,
                                                   &scroll_x, &scroll_y);
                        viewport_x -= scroll_x;
                        viewport_y -= scroll_y;
                    }
                    ancestor = ancestor->parent_node;
                }

                if (x < viewport_x ||
                    x >= viewport_x + viewport->width ||
                    y < viewport_y ||
                    y >= viewport_y + viewport->height)
                    return false;
            }
        }
        cur = cur->parent_node;
    }
    return true;
}


static inline uint32_t hash_node_id(uint64_t node_id, uint32_t capacity)
{
    uint32_t hash = (uint32_t)(node_id ^ (node_id >> 32));
    return hash & (capacity - 1);
}

static bool expand_listener_map(void)
{
    uint32_t new_capacity = (g_event_system.map_capacity == 0)
                                ? AROMA_MIN_MAP_CAPACITY
                                : g_event_system.map_capacity * 2;
    if (new_capacity < g_event_system.map_capacity) return false;

    AromaNodeEventListeners *new_map =
        (AromaNodeEventListeners *)calloc(new_capacity, sizeof(AromaNodeEventListeners));
    if (!new_map) return false;

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++)
    {
        uint64_t id = g_event_system.listener_map[i].node_id;
        if (id == 0 || id == UINT64_MAX) continue;
        uint32_t idx = hash_node_id(id, new_capacity);
        while (new_map[idx].node_id != 0)
            idx = (idx + 1) & (new_capacity - 1);
        new_map[idx] = g_event_system.listener_map[i];
    }
    free(g_event_system.listener_map);
    g_event_system.listener_map  = new_map;
    g_event_system.map_capacity  = new_capacity;
    return true;
}

static AromaNode *find_node_cached(uint64_t node_id)
{
    if (!node_id || !g_event_system.root_node) return NULL;

    /* The tree mutates on the UI thread (add/remove) while workers
     * resolve targets here: hold the lock for the whole walk. */
    EVENT_LOCK();
    for (int i = 0; i < AROMA_NODE_CACHE_SIZE; i++)
    {
        if (g_event_system.node_cache[i].node_id == node_id)
        {
            g_event_system.node_cache[i].last_access = ++g_event_system.cache_counter;
            AromaNode *hit = g_event_system.node_cache[i].node_ptr;
            EVENT_UNLOCK();
            return hit;
        }
    }

    AromaNode *node = __find_node_by_id(g_event_system.root_node, node_id);
    if (node)
    {
        uint32_t oldest = 0;
        for (int i = 1; i < AROMA_NODE_CACHE_SIZE; i++)
        {
            if (g_event_system.node_cache[i].last_access <
                g_event_system.node_cache[oldest].last_access)
                oldest = i;
        }
        g_event_system.node_cache[oldest].node_id    = node_id;
        g_event_system.node_cache[oldest].node_ptr   = node;
        g_event_system.node_cache[oldest].last_access = ++g_event_system.cache_counter;
    }
    EVENT_UNLOCK();
    return node;
}

static AromaNodeEventListeners *aroma_event_get_listeners(uint64_t node_id)
{
    if (!g_event_system.initialized || g_event_system.shutting_down || node_id == 0)
        return NULL;

    EVENT_LOCK();
    if (g_event_system.map_capacity == 0)
    {
        g_event_system.map_capacity = AROMA_MIN_MAP_CAPACITY;
        g_event_system.listener_map = (AromaNodeEventListeners *)calloc(
            g_event_system.map_capacity, sizeof(AromaNodeEventListeners));
        if (!g_event_system.listener_map) { EVENT_UNLOCK(); return NULL; }
    }
    if (g_event_system.map_count * 4 >= g_event_system.map_capacity * 3)
    {
        if (!expand_listener_map()) { EVENT_UNLOCK(); return NULL; }
    }

    uint32_t idx           = hash_node_id(node_id, g_event_system.map_capacity);
    uint32_t tombstone_idx = UINT32_MAX;
    while (g_event_system.listener_map[idx].node_id != 0)
    {
        if (g_event_system.listener_map[idx].node_id == node_id)
        {
            EVENT_UNLOCK();
            return &g_event_system.listener_map[idx];
        }
        if (tombstone_idx == UINT32_MAX &&
            g_event_system.listener_map[idx].node_id == UINT64_MAX)
            tombstone_idx = idx;
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
    }
    if (tombstone_idx != UINT32_MAX) idx = tombstone_idx;
    g_event_system.listener_map[idx].node_id       = node_id;
    g_event_system.listener_map[idx].listener_count = 0;
    g_event_system.map_count++;
    EVENT_UNLOCK();
    return &g_event_system.listener_map[idx];
}

static AromaNodeEventListeners *aroma_event_find_listeners(uint64_t node_id)
{
    if (!g_event_system.initialized || g_event_system.shutting_down ||
        node_id == 0 || g_event_system.map_count == 0)
        return NULL;

    EVENT_LOCK();
    uint32_t idx       = hash_node_id(node_id, g_event_system.map_capacity);
    uint32_t start_idx = idx;
    while (g_event_system.listener_map[idx].node_id != 0)
    {
        if (g_event_system.listener_map[idx].node_id == node_id)
        {
            EVENT_UNLOCK();
            return &g_event_system.listener_map[idx];
        }
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
        if (idx == start_idx) break;
    }
    EVENT_UNLOCK();
    return NULL;
}

static AromaEvent *aroma_event_alloc(void)
{
    EVENT_LOCK();
    if (g_event_free_count == 0 || g_event_system.shutting_down)
    {
        EVENT_UNLOCK();
        return NULL;
    }
    uint32_t idx = g_event_free_list[g_event_free_head];
    g_event_free_head  = (g_event_free_head + 1) % AROMA_MAX_EVENT_QUEUE;
    g_event_free_count--;
    AromaEvent *ev = &g_event_pool[idx];
    memset(ev, 0, sizeof(AromaEvent));
    EVENT_UNLOCK();
    return ev;
}

static void aroma_event_release(AromaEvent *event)
{
    if (!event) return;
    EVENT_LOCK();
    uintptr_t idx = (uintptr_t)(event - g_event_pool);
    if (idx < AROMA_MAX_EVENT_QUEUE && g_event_free_count < AROMA_MAX_EVENT_QUEUE)
    {
        uint32_t tail = (g_event_free_head + g_event_free_count) % AROMA_MAX_EVENT_QUEUE;
        g_event_free_list[tail] = (uint32_t)idx;
        g_event_free_count++;
    }
    EVENT_UNLOCK();
}


bool aroma_event_system_init(void)
{
    if (g_event_system.initialized) return true;
    memset(&g_event_system, 0, sizeof(g_event_system));
    memset(&g_mouse_state,  0, sizeof(g_mouse_state));
    g_mouse_state.last_x = -1;
    g_mouse_state.last_y = -1;
    for (uint32_t i = 0; i < AROMA_MAX_EVENT_QUEUE; i++)
        g_event_free_list[i] = i;
    g_event_free_count = AROMA_MAX_EVENT_QUEUE;

    g_event_system.map_capacity = AROMA_MIN_MAP_CAPACITY;
    g_event_system.listener_map = (AromaNodeEventListeners *)calloc(
        g_event_system.map_capacity, sizeof(AromaNodeEventListeners));
    if (!g_event_system.listener_map) return false;

#ifdef AROMA_THREAD_SAFE
    if (pthread_mutex_init(&g_event_system.mutex, NULL) != 0)
    {
        free(g_event_system.listener_map);
        g_event_system.listener_map = NULL;
        return false;
    }
#endif
    g_event_system.initialized = true;
    return true;
}

void aroma_event_system_shutdown(void)
{
    if (!g_event_system.initialized) return;
    g_event_system.shutting_down = true;
    aroma_event_process_queue();

    /* Drain without holding the lock across destroy: destroying an event
     * releases its pool slot which takes the same (non-recursive) mutex,
     * so holding it here would self-deadlock whenever the queue is
     * non-empty at shutdown. Collect under lock, destroy after unlock. */
    AromaEvent *pending[AROMA_MAX_EVENT_QUEUE];
    size_t n_pending = 0;
    EVENT_LOCK();
    while (g_event_system.queue_head != g_event_system.queue_tail &&
           n_pending < AROMA_MAX_EVENT_QUEUE)
    {
        pending[n_pending++] =
            g_event_system.event_queue[g_event_system.queue_head];
        g_event_system.event_queue[g_event_system.queue_head] = NULL;
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
        if (g_event_system.queue_count > 0)
            g_event_system.queue_count--;
    }
    if (g_event_system.listener_map)
    {
        free(g_event_system.listener_map);
        g_event_system.listener_map = NULL;
    }
    g_event_system.map_capacity = 0;
    g_event_system.map_count = 0;
    EVENT_UNLOCK();
    for (size_t i = 0; i < n_pending; i++)
        if (pending[i]) aroma_event_destroy(pending[i]);
#ifdef AROMA_THREAD_SAFE
    pthread_mutex_destroy(&g_event_system.mutex);
#endif
    memset(&g_event_system, 0, sizeof(g_event_system));
    memset(&g_mouse_state,  0, sizeof(g_mouse_state));
    memset(g_event_pool,    0, sizeof(g_event_pool));
}

void aroma_event_set_root(AromaNode *root)
{
    g_event_system.root_node = root;
}

AromaNode *aroma_event_get_root(void)
{
    return g_event_system.root_node;
}


AromaEvent *aroma_event_create(AromaEventType event_type, uint64_t target_node_id)
{
    if (!g_event_system.initialized || g_event_system.shutting_down ||
        event_type >= EVENT_TYPE_COUNT || target_node_id == 0)
        return NULL;

    AromaNode *target = find_node_cached(target_node_id);
    if (!target) return NULL;

    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;

    ev->event_type     = event_type;
    ev->target_node_id = target_node_id;
    ev->target_node    = target;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}


static bool aroma_event_dispatch_internal(AromaEvent *event,
                                          uint64_t *consuming_node_id)
{
    if (!event || g_event_system.shutting_down) return false;

    if (!event_node_valid(event->target_node)) return false;

    AromaNode *current = event->target_node;
    if (consuming_node_id) *consuming_node_id = 0;

    while (event_node_valid(current) && !event->consumed)
    {
        AromaNodeEventListeners *ls = aroma_event_find_listeners(current->node_id);
        if (ls)
        {
            for (uint32_t i = 0; i < ls->listener_count && !event->consumed; i++)
            {
                AromaEventListener *listener = &ls->listeners[i];
                if (listener->event_type == event->event_type)
                {
                    bool result = listener->handler(event, listener->user_data);
                    if (result || event->consumed) {
                        event->consumed = true;
                        if (consuming_node_id)
                            *consuming_node_id = current->node_id;
                    }
                }
            }
        }
        AromaNode *next = current->parent_node;
        current = next;
    }

    return event->consumed;
}

bool aroma_event_dispatch(AromaEvent *event)
{
    return aroma_event_dispatch_internal(event, NULL);
}

bool aroma_event_queue(AromaEvent *event)
{
    if (!event || !g_event_system.initialized || g_event_system.shutting_down)
    {
        if (event) aroma_event_destroy(event);
        return false;
    }
    EVENT_LOCK();
    if (g_event_system.queue_count >= AROMA_MAX_EVENT_QUEUE)
    {
        EVENT_UNLOCK();
        aroma_event_destroy(event);
        return false;
    }
    g_event_system.event_queue[g_event_system.queue_tail] = event;
    g_event_system.queue_tail  = (g_event_system.queue_tail + 1) % AROMA_MAX_EVENT_QUEUE;
    g_event_system.queue_count++;
    EVENT_UNLOCK();
    return true;
}

void aroma_event_process_queue(void)
{
    if (!g_event_system.initialized) return;

    while (g_event_system.queue_head != g_event_system.queue_tail)
    {
        EVENT_LOCK();
        AromaEvent *ev = g_event_system.event_queue[g_event_system.queue_head];
        g_event_system.queue_head  = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
        g_event_system.queue_count--;
        EVENT_UNLOCK();

        if (ev)
        {
            aroma_event_dispatch(ev);
            aroma_event_destroy(ev);
        }
    }
    aroma_event_resync_hover();
}


void aroma_event_handle_touch(int id, int x, int y, int state)
{
    if (!g_event_system.root_node || g_event_system.shutting_down ||
        id < 0 || id >= AROMA_MAX_TOUCHES)
    {
        LOG_INFO("EVT_REJECT: root=%p shut=%d id=%d state=%d",
                 (void *)g_event_system.root_node, g_event_system.shutting_down, id, state);
        return;
    }

    if (state == 1)
    {
        aroma_dropdown_handle_outside_touch(x, y);
        for (int i = 0; i < AROMA_MAX_TOUCHES; i++)
        {
            if (g_scroll_intercepting[i])
            {
                return;
            }
        }
    }

    uint64_t  target_id = 0;
    AromaNode *target   = NULL;

    if (state == 1)
    {
        target    = aroma_event_hit_test(g_event_system.root_node, x, y);
        target_id = target ? target->node_id : 0;
        g_touch_captures[id] = target_id;
        g_down_x[id] = x;
        g_down_y[id] = y;
        AromaNode *scr = target ? find_scrollable_ancestor(target) : NULL;
        /*
         * The expanded dropdown list is a temporary overlay. Its gesture is
         * owned by the dropdown, not by the scroll view containing the
         * dropdown field.
         */
        AromaNode *overlay_target = NULL;
        if (target && aroma_dropdown_overlay_hit_test(x, y, &overlay_target) &&
            overlay_target == target)
            scr = NULL;
        /* Picker popups are modal overlays with the same ownership: while
         * open, the picker owns the gesture, never the ScrollView behind
         * its field. */
        if (target &&
            aroma_calendar_overlay_hit_test(x, y, &overlay_target) &&
            overlay_target == target)
            scr = NULL;
        if (target &&
            aroma_datepicker_overlay_hit_test(x, y, &overlay_target) &&
            overlay_target == target)
            scr = NULL;
        if (target &&
            aroma_timepicker_overlay_hit_test(x, y, &overlay_target) &&
            overlay_target == target)
            scr = NULL;
        /* Dropdown fields and their expanded overlays own the complete
         * gesture. Do not let the containing ScrollView steal a drag after
         * the dropdown receives TOUCH_DOWN. */
        if (target && target->draw_cb == aroma_dropdown_draw)
            scr = NULL;
        /* The numeric stepper is a vertical drag widget like the dropdown
         * list: it owns the complete gesture. Do not let the containing
         * ScrollView steal a vertical drag after the stepper receives
         * TOUCH_DOWN, otherwise the picker value and the page scroll
         * together. Steps mode is tap-only and stays scrollable. */
        if (target && target->draw_cb == aroma_stepper_draw &&
            aroma_stepper_owns_touch(target))
            scr = NULL;
        if (target &&
            ((target->draw_cb == aroma_calendar_draw &&
              aroma_calendar_is_popup_open(target)) ||
             (target->draw_cb == aroma_datepicker_draw &&
              aroma_datepicker_is_popup_open(target)) ||
             (target->draw_cb == aroma_timepicker_draw &&
              aroma_timepicker_is_popup_open(target))))
            scr = NULL;
        if (!scr && target &&
            target->node_type == NODE_TYPE_CONTAINER &&
            aroma_container_is_scrollable(target))
            scr = target;

        g_scroll_captures[id] = scr ? scr->node_id : 0;
    }
    else
    {
        target_id = g_touch_captures[id];
        /*
         * A ScrollView may be the initial hit when its child has just
         * entered the viewport during scrolling. For a non-intercepted
         * release, retarget to the currently visible descendant so list
         * items and other widgets remain clickable after scrolling.
         */
        if (state == 0 && target_id != 0 && !g_scroll_intercepting[id])
        {
            AromaNode *release_target =
                aroma_event_hit_test(g_event_system.root_node, x, y);
            AromaNode *captured_target = find_node_cached(target_id);
            if (release_target && captured_target &&
                release_target != captured_target &&
                is_descendant_of(release_target, captured_target))
            {
                target_id = release_target->node_id;
            }
        }
        if (target_id == 0)
        {
            target    = aroma_event_hit_test(g_event_system.root_node, x, y);
            target_id = target ? target->node_id : 0;
        }
    }

    AromaEventType type;
    if      (state == 1) type = EVENT_TYPE_TOUCH_DOWN;
    else if (state == 0) type = EVENT_TYPE_TOUCH_UP;
    else                 type = EVENT_TYPE_TOUCH_MOVE;

    uint64_t scroll_id  = g_scroll_captures[id];
    bool     intercepted = false;

    if (g_scroll_intercepting[id])
    {
        AromaEvent *sev = aroma_event_create(type, scroll_id);
        if (sev)
        {
            sev->data.touch.id = id;
            sev->data.touch.x  = x;
            sev->data.touch.y  = y;
            aroma_event_dispatch(sev);
            intercepted = sev->consumed;
            aroma_event_destroy(sev);
        }
        else
        {
            LOG_WARNING("Failed to create scroll touch event for node %llu",
                        (unsigned long long)scroll_id);
        }
    }

    if (!g_scroll_intercepting[id] && target_id != 0)
    {
        if (type == EVENT_TYPE_TOUCH_MOVE && scroll_id != 0 && target_id != scroll_id &&
            swipe_should_take_over(id, x, y, scroll_id))
        {
            g_scroll_intercepting[id] = true;
            AromaEvent *cancel = aroma_event_create(EVENT_TYPE_TOUCH_UP, target_id);
            if (cancel)
            {
                cancel->data.touch.id = id;
                cancel->data.touch.x = -1;
                cancel->data.touch.y = -1;
                aroma_event_dispatch(cancel);
                aroma_event_destroy(cancel);
            }
            AromaEvent *down = aroma_event_create(EVENT_TYPE_TOUCH_DOWN, scroll_id);
            if (down)
            {
                down->data.touch.id = id;
                down->data.touch.x = g_down_x[id];
                down->data.touch.y = g_down_y[id];
                aroma_event_dispatch(down);
                aroma_event_destroy(down);
            }
        }
        if (g_scroll_intercepting[id])
        {
            AromaEvent *sev = aroma_event_create(type, scroll_id);
            if (sev)
            {
                sev->data.touch.id = id;
                sev->data.touch.x  = x;
                sev->data.touch.y  = y;
                aroma_event_dispatch(sev);
                aroma_event_destroy(sev);
            }
            intercepted = true;
        }
        else
        {
            AromaEvent *evt = aroma_event_create(type, target_id);
            if (evt)
            {
                evt->data.touch.id = id;
                evt->data.touch.x  = x;
                evt->data.touch.y  = y;
                uint64_t consuming_node_id = 0;
                aroma_event_dispatch_internal(evt, &consuming_node_id);
                if (consuming_node_id == scroll_id && scroll_id != 0)
                    intercepted = true;
                aroma_event_destroy(evt);
            }
        }
    }

    if (intercepted && !g_scroll_intercepting[id])
    {
        g_scroll_intercepting[id] = true;
        if (target_id != 0 && target_id != scroll_id)
        {
            AromaEvent *cancel = aroma_event_create(EVENT_TYPE_TOUCH_UP, target_id);
            if (cancel)
            {
                cancel->data.touch.id = id;
                cancel->data.touch.x = -1;
                cancel->data.touch.y = -1;
                aroma_event_dispatch(cancel);
                aroma_event_destroy(cancel);
            }
        }
    }

    if (state == 0)
    {
        g_touch_captures[id]     = 0;
        g_scroll_captures[id]    = 0;
        g_scroll_intercepting[id] = false;
    }

    aroma_gesture_handle_touch(id, x, y, state);
}

void aroma_event_handle_pointer_move(int x, int y, bool button_down)
{
    if (!g_event_system.root_node || g_event_system.shutting_down) return;

    bool was_down = g_mouse_state.button_down;
    g_mouse_state.button_down = button_down;
    g_mouse_state.last_x      = x;
    g_mouse_state.last_y      = y;

    AromaNode *target    = aroma_event_hit_test(g_event_system.root_node, x, y);
    uint64_t  current_id = target ? target->node_id : 0;

#ifdef __ANDROID__
    if (button_down && !was_down)
    {
        bool scroll_active = false;
        for (int i = 0; i < AROMA_MAX_TOUCHES; i++)
        {
            if (g_scroll_intercepting[i]) { scroll_active = true; break; }
        }
        if (!scroll_active)
        {
            AromaEvent *ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_CLICK, current_id, x, y, 0);
            if (ev)
            {
                aroma_event_dispatch(ev);
                aroma_event_destroy(ev);
            }
        }
    }
#else
    (void)was_down;
#endif

    if (button_down)
    {
        AromaNode *focused = aroma_ui_get_focused_node();
        if (event_node_valid(focused) && focused->node_id != current_id)
        {
            AromaEvent *focus_ev = aroma_event_create(EVENT_TYPE_FOCUS_LOST, focused->node_id);
            if (focus_ev)
            {
                aroma_event_dispatch(focus_ev);
                aroma_event_destroy(focus_ev);
            }
            aroma_ui_clear_focused_node(focused);
            AromaPlatformInterface *platform = aroma_backend_abi.get_platform_interface();
            if (platform && platform->hide_keyboard)
                platform->hide_keyboard();
        }
    }

    if (button_down && !was_down)
        aroma_gesture_handle_touch(0, x, y, 1);
    else if (button_down && was_down)
        aroma_gesture_handle_touch(0, x, y, 2);
    else if (!button_down && was_down)
        aroma_gesture_handle_touch(0, x, y, 0);
}

void aroma_event_resync_hover(void)
{
#if !defined(__ANDROID__) && !defined(__linux__)
    if (!g_event_system.root_node || g_event_system.shutting_down ||
        g_mouse_state.last_x < 0 || g_mouse_state.last_y < 0)
        return;

    AromaNode *target    = aroma_event_hit_test(g_event_system.root_node,
                                                 g_mouse_state.last_x,
                                                 g_mouse_state.last_y);
    uint64_t  current_id = target ? target->node_id : 0;

    if (current_id != g_mouse_state.hovered_node_id)
    {
        if (g_mouse_state.hovered_node_id != 0)
        {
            AromaNode *old = find_node_cached(g_mouse_state.hovered_node_id);
            if (event_node_valid(old))
            {
                AromaEvent *ev = aroma_event_create_mouse(
                    EVENT_TYPE_MOUSE_EXIT, old->node_id,
                    g_mouse_state.last_x, g_mouse_state.last_y, 0);
                if (ev)
                {
                    aroma_event_dispatch(ev);
                    aroma_event_destroy(ev);
                }
            }
        }
        if (event_node_valid(target))
        {
            AromaEvent *ev = aroma_event_create_mouse(
                EVENT_TYPE_MOUSE_ENTER, target->node_id,
                g_mouse_state.last_x, g_mouse_state.last_y, 0);
            if (ev)
            {
                aroma_event_dispatch(ev);
                aroma_event_destroy(ev);
            }
        }
        g_mouse_state.hovered_node_id  = current_id;
        g_mouse_state.last_hover_node  = target;
    }
#endif
}


AromaNode *aroma_event_hit_test(AromaNode *root, int x, int y)
{
    if (!event_node_valid(root)) return NULL;
    if (root->is_hidden || g_event_system.shutting_down) return NULL;
    if (root->parent_node && !point_in_scrollable_ancestors(root, x, y))
        return NULL;

    if (!root->parent_node)
    {
        AromaNode *overlay_target = NULL;
        if (aroma_dropdown_overlay_hit_test(x, y, &overlay_target))
            return overlay_target;
        if (aroma_calendar_overlay_hit_test(x, y, &overlay_target))
            return overlay_target;
        if (aroma_datepicker_overlay_hit_test(x, y, &overlay_target))
            return overlay_target;
        if (aroma_timepicker_overlay_hit_test(x, y, &overlay_target))
            return overlay_target;
    }

    AromaNode *best   = NULL;
    int32_t    best_z = INT32_MIN;

    if (root->child_count > AROMA_MAX_CHILD_NODES)
    {
        LOG_ERROR("hit_test: node %llu has corrupt child_count %llu",
                  (unsigned long long)root->node_id,
                  (unsigned long long)root->child_count);
    }
    else
    {
        for (uint64_t i = 0; i < root->child_count; i++)
        {
            AromaNode *child = root->child_nodes[i];
            if (!event_node_valid(child)) continue;

            AromaNode *hit = aroma_event_hit_test(child, x, y);
            if (hit && hit->z_index >= best_z)
            {
                best   = hit;
                best_z = hit->z_index;
            }
        }
    }if (root->node_type == NODE_TYPE_WIDGET ||
        root->node_type == NODE_TYPE_CONTAINER)
    {
        /*
         * A ListView is the content child of an owning ScrollView. Its own
         * rect is deliberately only the viewport height, while its rows
         * extend through the parent's scrollable content. Hit testing the
         * ListView against that rect after adding the parent's scroll offset
         * rejects rows revealed later in the list. Use the owner's viewport
         * as the ListView hit area instead.
         */
        if (root->draw_cb == aroma_listview_draw &&
            root->parent_node &&
            aroma_container_is_scrollable(root->parent_node))
        {
            AromaRect *viewport = aroma_node_get_rect(root->parent_node);
            if (viewport)
            {
                int viewport_x = viewport->x;
                int viewport_y = viewport->y;
                AromaNode *ancestor = root->parent_node->parent_node;
                while (event_node_valid(ancestor))
                {
                    if (ancestor->node_type == NODE_TYPE_CONTAINER &&
                        aroma_container_is_scrollable(ancestor))
                    {
                        int sx = 0;
                        int sy = 0;
                        aroma_container_get_scroll(ancestor, &sx, &sy);
                        viewport_x -= sx;
                        viewport_y -= sy;
                    }
                    ancestor = ancestor->parent_node;
                }

                if (x >= viewport_x && x < viewport_x + viewport->width &&
                    y >= viewport_y && y < viewport_y + viewport->height)
                {
                    bool should_win = (root->z_index >= best_z);
                    if (should_win)
                    {
                        best = root;
                        best_z = root->z_index;
                    }
                }
                return best;
            }
        }

        int adjusted_x = x;
        int adjusted_y = y;

        // Traverse ALL ancestors to accumulate every scroll offset
        AromaNode *cur = root->parent_node;
        while (event_node_valid(cur))
        {
            if (cur->node_type == NODE_TYPE_CONTAINER && aroma_container_is_scrollable(cur))
            {
                int delta_x = 0, delta_y = 0;
                aroma_container_get_scroll(cur, &delta_x, &delta_y);
                adjusted_x += delta_x;
                adjusted_y += delta_y;
            }
            cur = cur->parent_node;
        }

        AromaRect *bounds = aroma_node_get_rect(root);
        
        if (bounds &&
            adjusted_x >= bounds->x && adjusted_x < (bounds->x + bounds->width) &&
            adjusted_y >= bounds->y && adjusted_y < (bounds->y + bounds->height))
        {
            bool should_win = (root->node_type == NODE_TYPE_CONTAINER)
                                  ? (best == NULL)
                                  : (root->z_index >= best_z);
            if (should_win)
            {
                best   = root;
                best_z = root->z_index;
            }
        }
    }
    return best;
}

static AromaNode *find_scrollable_ancestor(AromaNode *start)
{
    if (!event_node_valid(start)) return NULL;

    AromaNode *cur = start->parent_node;
    while (event_node_valid(cur))
    {
        if (cur->node_type == NODE_TYPE_CONTAINER &&
            aroma_container_is_scrollable(cur))
            return cur;
        AromaNode *next = cur->parent_node;
        cur = next;
    }
    return NULL;
}


bool aroma_event_subscribe(uint64_t node_id, AromaEventType type,
                           AromaEventHandler handler, void *user_data,
                           uint32_t priority)
{
    if (!handler || node_id == 0 || g_event_system.shutting_down) return false;

    AromaNodeEventListeners *ls = aroma_event_get_listeners(node_id);
    if (!ls) return false;

    EVENT_LOCK();
    for (uint32_t i = 0; i < ls->listener_count; i++)
    {
        if (ls->listeners[i].event_type == type &&
            ls->listeners[i].handler    == handler &&
            ls->listeners[i].user_data  == user_data)
        {
            EVENT_UNLOCK();
            return false;
        }
    }
    if (ls->listener_count >= AROMA_MAX_LISTENERS_PER_NODE)
    {
        EVENT_UNLOCK();
        return false;
    }
    uint32_t pos = ls->listener_count;
    for (uint32_t i = 0; i < ls->listener_count; i++)
    {
        if (priority > ls->listeners[i].priority) { pos = i; break; }
    }
    for (uint32_t i = ls->listener_count; i > pos; i--)
        ls->listeners[i] = ls->listeners[i - 1];

    ls->listeners[pos] = (AromaEventListener){
        .event_type = type,
        .handler    = handler,
        .user_data  = user_data,
        .priority   = priority
    };
    ls->listener_count++;
    EVENT_UNLOCK();
    return true;
}

bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType type,
                             AromaEventHandler handler)
{
    if (!handler || node_id == 0) return false;

    AromaNodeEventListeners *ls = aroma_event_find_listeners(node_id);
    if (!ls) return false;

    EVENT_LOCK();
    for (uint32_t i = 0; i < ls->listener_count; i++)
    {
        if (ls->listeners[i].event_type == type &&
            ls->listeners[i].handler    == handler)
        {
            for (uint32_t k = i; k < ls->listener_count - 1; k++)
                ls->listeners[k] = ls->listeners[k + 1];
            ls->listener_count--;
            if (ls->listener_count == 0)
            {
                ls->node_id = UINT64_MAX;
                g_event_system.map_count--;
            }
            EVENT_UNLOCK();
            return true;
        }
    }
    EVENT_UNLOCK();
    return false;
}

void aroma_event_lock(void)
{
    EVENT_LOCK();
}

void aroma_event_unlock(void)
{
    EVENT_UNLOCK();
}

void aroma_event_forget_node(uint64_t node_id)
{    if (!node_id || !g_event_system.initialized)
        return;
    /* Collect first (destroying events releases pool slots, which takes
     * the same lock, so payload cleanup happens after unlock). Queue
     * slots are nulled in place rather than compacted: process_queue may
     * be mid-iteration above us on this thread (destroy inside dispatch),
     * and it already skips NULL slots safely. */
    AromaEvent *dropped[AROMA_MAX_EVENT_QUEUE];
    size_t n_dropped = 0;
    EVENT_LOCK();
    for (int i = 0; i < AROMA_NODE_CACHE_SIZE; i++) {
        if (g_event_system.node_cache[i].node_id == node_id) {
            g_event_system.node_cache[i].node_id = 0;
            g_event_system.node_cache[i].node_ptr = NULL;
        }
    }
    if (g_event_system.map_count > 0 && g_event_system.map_capacity > 0) {
        uint32_t idx = hash_node_id(node_id, g_event_system.map_capacity);
        uint32_t start = idx;
        while (g_event_system.listener_map[idx].node_id != 0) {
            if (g_event_system.listener_map[idx].node_id == node_id) {
                g_event_system.listener_map[idx].node_id = UINT64_MAX;
                g_event_system.listener_map[idx].listener_count = 0;
                g_event_system.map_count--;
                break;
            }
            idx = (idx + 1) & (g_event_system.map_capacity - 1);
            if (idx == start)
                break;
        }
    }
    {
        /* Null matching slots in place without touching queue_count:
         * the count tracks buffer occupancy (head..tail distance) and is
         * decremented exactly once by process_queue when it dequeues each
         * slot (even NULL holes). Decrementing here as well would
         * double-decrement and wrap queue_count to UINT32_MAX, making the
         * queue look permanently full so all future inputs are dropped
         * (this froze car_infotainment on Linux after any node teardown
         * with a queued event). Snapshot the bound: queue_count must not
         * be used as a mutating loop limit. Compaction is avoided on
         * purpose: process_queue may be mid-iteration above us on this
         * thread (destroy inside dispatch) and already skips NULL safely. */
        uint32_t n = g_event_system.queue_count;
        uint32_t cur = g_event_system.queue_head;
        for (uint32_t k = 0; k < n; k++) {
            AromaEvent *ev = g_event_system.event_queue[cur];
            if (ev && ev->target_node_id == node_id) {
                g_event_system.event_queue[cur] = NULL;
                if (n_dropped < AROMA_MAX_EVENT_QUEUE)
                    dropped[n_dropped++] = ev;
            }
            cur = (cur + 1) % AROMA_MAX_EVENT_QUEUE;
        }
    }
    EVENT_UNLOCK();
    for (size_t k = 0; k < n_dropped; k++)
        aroma_event_destroy(dropped[k]);
}


AromaEvent *aroma_event_create_mouse(AromaEventType type, uint64_t node_id,
                                     int x, int y, uint8_t button)
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = type;
    ev->target_node_id = node_id;
    ev->target_node    = find_node_cached(node_id);
    ev->data.mouse.x      = x;
    ev->data.mouse.y      = y;
    ev->data.mouse.button = button;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent *aroma_event_create_key(AromaEventType type, uint64_t node_id,
                                   uint32_t key_code, uint16_t modifiers)
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = type;
    ev->target_node_id = node_id;
    ev->target_node    = find_node_cached(node_id);
    ev->data.key.key_code  = key_code;
    ev->data.key.modifiers = modifiers;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent *aroma_event_create_resize(uint64_t node_id, int width, int height)
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = EVENT_TYPE_WINDOW_RESIZE;
    ev->target_node_id = node_id;
    ev->target_node    = find_node_cached(node_id);
    ev->data.resize.width  = width;
    ev->data.resize.height = height;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent *aroma_event_create_custom(uint64_t node_id, uint32_t custom_type,
                                      void *data, void (*free_func)(void *))
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = EVENT_TYPE_CUSTOM;
    ev->target_node_id = node_id;
    ev->target_node    = find_node_cached(node_id);
    ev->data.custom.custom_type = custom_type;
    ev->data.custom.data        = data;
    ev->data.custom.free_data   = free_func;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent *aroma_event_create_scroll(uint64_t node_id, int x, int y,
                                      float scroll_x, float scroll_y)
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = EVENT_TYPE_MOUSE_SCROLL;
    ev->target_node_id = node_id;
    ev->target_node    = find_node_cached(node_id);
    ev->data.mouse.x        = x;
    ev->data.mouse.y        = y;
    ev->data.mouse.scroll_x = scroll_x;
    ev->data.mouse.scroll_y = scroll_y;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent *aroma_event_create_back(uint64_t target_node_id)
{
    if (!g_event_system.initialized || g_event_system.shutting_down)
        return NULL;
    uint64_t node_id = target_node_id;
    AromaNode *target = NULL;
    if (node_id != 0)
    {
        target = find_node_cached(node_id);
        if (!target)
            return NULL;
    }
    else
    {
        target = g_event_system.root_node;
        if (!target)
            return NULL;
        node_id = target->node_id;
    }
    AromaEvent *ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type     = EVENT_TYPE_BACK_PRESS;
    ev->target_node_id = node_id;
    ev->target_node    = target;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}


void aroma_event_destroy(AromaEvent *event)
{
    if (!event) return;
    if (event->event_type == EVENT_TYPE_CUSTOM &&
        event->data.custom.free_data &&
        event->data.custom.data)
    {
        event->data.custom.free_data(event->data.custom.data);
    }
    aroma_event_release(event);
}

void aroma_event_consume(AromaEvent *ev)
{
    if (ev) ev->consumed = true;
}

const char *aroma_event_type_name(AromaEventType event_type)
{
    static const char *names[] = {
        "MOUSE_MOVE", "MOUSE_CLICK", "MOUSE_RELEASE", "MOUSE_ENTER",
        "MOUSE_EXIT", "MOUSE_HOVER", "MOUSE_DOUBLE_CLICK", "MOUSE_SCROLL",
        "KEY_PRESS", "KEY_RELEASE", "FOCUS_GAINED", "FOCUS_LOST",
        "WINDOW_RESIZE", "TOUCH_DOWN", "TOUCH_UP", "TOUCH_MOVE",
        "CUSTOM", "BACK_PRESS", "UNKNOWN"
    };
    if ((int)event_type < 0)
        return "UNKNOWN";
    if (event_type < EVENT_TYPE_COUNT) return names[event_type];
    return names[EVENT_TYPE_COUNT];
}