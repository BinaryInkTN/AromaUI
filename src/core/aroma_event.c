#include "core/aroma_event.h"
#include "core/aroma_node.h"
#include "core/aroma_common.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_logger.h"
#include "widgets/aroma_dropdown.h"
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

#ifdef AROMA_DEBUG_EVENTS
    #define EVENT_LOG(level, ...) aroma_logger_write(level, "[Event] " __VA_ARGS__)
#else
    #define EVENT_LOG(level, ...) ((void)0)
#endif

typedef struct {
    uint64_t node_id;
    AromaEventListener listeners[AROMA_MAX_LISTENERS_PER_NODE];
    uint32_t listener_count;
} AromaNodeEventListeners;

static struct {
    bool initialized;
    bool shutting_down;
    AromaNodeEventListeners* listener_map;
    uint32_t map_capacity;
    uint32_t map_count;

    AromaEvent* event_queue[AROMA_MAX_EVENT_QUEUE];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;

    AromaNode* root_node;

#ifdef AROMA_THREAD_SAFE
    pthread_mutex_t mutex;
#endif
} g_event_system = {0};

static AromaEvent g_event_pool[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_list[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_head = 0;
static uint32_t g_event_free_count = 0;

static struct {
    int last_x;
    int last_y;
    bool button_down;
    uint64_t hovered_node_id;
} g_mouse_state = {-1, -1, false, 0};

#ifdef AROMA_THREAD_SAFE
    #define EVENT_LOCK() pthread_mutex_lock(&g_event_system.mutex)
    #define EVENT_UNLOCK() pthread_mutex_unlock(&g_event_system.mutex)
#else
    #define EVENT_LOCK() ((void)0)
    #define EVENT_UNLOCK() ((void)0)
#endif

static inline uint32_t hash_node_id(uint64_t node_id, uint32_t capacity) {
    uint32_t hash = (uint32_t)(node_id ^ (node_id >> 32));
    return hash & (capacity - 1);
}

static bool expand_listener_map(void) {
    uint32_t new_capacity = (g_event_system.map_capacity == 0) ? 
        AROMA_MIN_MAP_CAPACITY : g_event_system.map_capacity * 2;

    if (new_capacity < g_event_system.map_capacity) {
        return false; 

    }

    AromaNodeEventListeners* new_map = (AromaNodeEventListeners*)calloc(
        new_capacity, sizeof(AromaNodeEventListeners));
    if (!new_map) return false;

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++) {
        if (g_event_system.listener_map[i].node_id == 0) continue;

        uint32_t idx = hash_node_id(g_event_system.listener_map[i].node_id, new_capacity);
        while (new_map[idx].node_id != 0) {
            idx = (idx + 1) & (new_capacity - 1);
        }
        new_map[idx] = g_event_system.listener_map[i];
    }

    free(g_event_system.listener_map);
    g_event_system.listener_map = new_map;
    g_event_system.map_capacity = new_capacity;

    return true;
}

static AromaNodeEventListeners* aroma_event_get_listeners(uint64_t node_id) {
    if (!g_event_system.initialized || g_event_system.shutting_down || node_id == 0) {
        return NULL;
    }

    EVENT_LOCK();

    if (g_event_system.map_capacity == 0) {
        g_event_system.map_capacity = AROMA_MIN_MAP_CAPACITY;
        g_event_system.listener_map = (AromaNodeEventListeners*)calloc(
            g_event_system.map_capacity, sizeof(AromaNodeEventListeners));
        if (!g_event_system.listener_map) {
            EVENT_UNLOCK();
            return NULL;
        }
    }

    if (g_event_system.map_count * 4 >= g_event_system.map_capacity * 3) {
        if (!expand_listener_map()) {
            EVENT_UNLOCK();
            return NULL;
        }
    }

    uint32_t idx = hash_node_id(node_id, g_event_system.map_capacity);
    while (g_event_system.listener_map[idx].node_id != 0) {
        if (g_event_system.listener_map[idx].node_id == node_id) {
            EVENT_UNLOCK();
            return &g_event_system.listener_map[idx];
        }
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
    }

    g_event_system.listener_map[idx].node_id = node_id;
    g_event_system.listener_map[idx].listener_count = 0;
    g_event_system.map_count++;

    EVENT_UNLOCK();
    return &g_event_system.listener_map[idx];
}

static AromaNodeEventListeners* aroma_event_find_listeners(uint64_t node_id) {
    if (!g_event_system.initialized || g_event_system.shutting_down || 
        node_id == 0 || g_event_system.map_count == 0) {
        return NULL;
    }

    EVENT_LOCK();

    uint32_t idx = hash_node_id(node_id, g_event_system.map_capacity);
    uint32_t start_idx = idx;

    while (g_event_system.listener_map[idx].node_id != 0) {
        if (g_event_system.listener_map[idx].node_id == node_id) {
            EVENT_UNLOCK();
            return &g_event_system.listener_map[idx];
        }
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
        if (idx == start_idx) break;
    }

    EVENT_UNLOCK();
    return NULL;
}

static AromaEvent* aroma_event_alloc(void) {
    EVENT_LOCK();

    if (g_event_free_count == 0 || g_event_system.shutting_down) {
        EVENT_UNLOCK();
        return NULL;
    }

    uint32_t idx = g_event_free_list[g_event_free_head];
    g_event_free_head = (g_event_free_head + 1) % AROMA_MAX_EVENT_QUEUE;
    g_event_free_count--;

    AromaEvent* ev = &g_event_pool[idx];
    memset(ev, 0, sizeof(AromaEvent));

    EVENT_UNLOCK();
    return ev;
}

static void aroma_event_release(AromaEvent* event) {
    if (!event) return;

    EVENT_LOCK();

    uintptr_t idx = (uintptr_t)(event - g_event_pool);
    if (idx < AROMA_MAX_EVENT_QUEUE) {
        uint32_t tail = (g_event_free_head + g_event_free_count) % AROMA_MAX_EVENT_QUEUE;
        g_event_free_list[tail] = (uint32_t)idx;
        g_event_free_count++;
    }

    EVENT_UNLOCK();
}

bool aroma_event_system_init(void) {
    if (g_event_system.initialized) {
        EVENT_LOG(LOG_LEVEL_WARN, "Event system already initialized");
        return true;
    }

    memset(&g_event_system, 0, sizeof(g_event_system));
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    g_mouse_state.last_x = -1;
    g_mouse_state.last_y = -1;

    for (uint32_t i = 0; i < AROMA_MAX_EVENT_QUEUE; i++) {
        g_event_free_list[i] = i;
    }
    g_event_free_count = AROMA_MAX_EVENT_QUEUE;

    g_event_system.map_capacity = AROMA_MIN_MAP_CAPACITY;
    g_event_system.listener_map = (AromaNodeEventListeners*)calloc(
        g_event_system.map_capacity, sizeof(AromaNodeEventListeners));

    if (!g_event_system.listener_map) {
        EVENT_LOG(LOG_LEVEL_ERROR, "Failed to allocate listener map");
        return false;
    }

#ifdef AROMA_THREAD_SAFE
    if (pthread_mutex_init(&g_event_system.mutex, NULL) != 0) {
        EVENT_LOG(LOG_LEVEL_ERROR, "Failed to initialize mutex");
        free(g_event_system.listener_map);
        g_event_system.listener_map = NULL;
        return false;
    }
#endif

    g_event_system.initialized = true;
    EVENT_LOG(LOG_LEVEL_INFO, "Event system initialized");

    return true;
}

void aroma_event_system_shutdown(void) {
    if (!g_event_system.initialized) return;

    EVENT_LOG(LOG_LEVEL_INFO, "Shutting down event system...");
    g_event_system.shutting_down = true;

    aroma_event_process_queue();

    EVENT_LOCK();

    while (g_event_system.queue_head != g_event_system.queue_tail) {
        AromaEvent* ev = g_event_system.event_queue[g_event_system.queue_head];
        if (ev) {
            aroma_event_destroy(ev);
        }
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
    }

    if (g_event_system.listener_map) {
        free(g_event_system.listener_map);
        g_event_system.listener_map = NULL;
    }

#ifdef AROMA_THREAD_SAFE
    pthread_mutex_destroy(&g_event_system.mutex);
#endif

    memset(&g_event_system, 0, sizeof(g_event_system));
    memset(&g_mouse_state, 0, sizeof(g_mouse_state));
    memset(g_event_pool, 0, sizeof(g_event_pool));

    EVENT_LOG(LOG_LEVEL_INFO, "Event system shutdown complete");
}

void aroma_event_set_root(AromaNode* root) { 
    g_event_system.root_node = root; 
    EVENT_LOG(LOG_LEVEL_DEBUG, "Root node set to %p", (void*)root);
}

AromaNode* aroma_event_get_root(void) { 
    return g_event_system.root_node; 
}

AromaEvent* aroma_event_create(AromaEventType event_type, uint64_t target_node_id) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;

    ev->event_type = event_type;
    ev->target_node_id = target_node_id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, target_node_id);

    if (clock_gettime(CLOCK_MONOTONIC, &ev->timestamp) != 0) {
        ev->timestamp.tv_sec = 0;
        ev->timestamp.tv_nsec = 0;
    }

    return ev;
}

bool aroma_event_dispatch(AromaEvent* event) {
    if (!event || !event->target_node || g_event_system.shutting_down) {
        return false;
    }

    if (event->consumed) {
        EVENT_LOG(LOG_LEVEL_DEBUG, "Event already consumed");
        return false;
    }

    AromaNode* current = event->target_node;

    while (current && !event->consumed) {
        AromaNodeEventListeners* ls = aroma_event_find_listeners(current->node_id);
        if (ls) {
            for (uint32_t i = 0; i < ls->listener_count && !event->consumed; i++) {
                AromaEventListener* listener = &ls->listeners[i];

                if (listener->event_type == event->event_type) {
                    EVENT_LOG(LOG_LEVEL_TRACE, "Dispatching event %d to node %llu", 
                              event->event_type, (unsigned long long)current->node_id);

                    if (listener->handler(event, listener->user_data)) {
                        return true;
                    }
                }
            }
        }
        current = current->parent_node;
    }

    return false;
}

bool aroma_event_queue(AromaEvent* event) {
    if (!event || !g_event_system.initialized || g_event_system.shutting_down) {
        if (event) aroma_event_destroy(event);
        return false;
    }

    EVENT_LOCK();

    if (g_event_system.queue_count >= AROMA_MAX_EVENT_QUEUE) {
        EVENT_UNLOCK();
        EVENT_LOG(LOG_LEVEL_WARN, "Event queue full, dropping event");
        aroma_event_destroy(event);
        return false;
    }

    g_event_system.event_queue[g_event_system.queue_tail] = event;
    g_event_system.queue_tail = (g_event_system.queue_tail + 1) % AROMA_MAX_EVENT_QUEUE;
    g_event_system.queue_count++;

    EVENT_UNLOCK();

    EVENT_LOG(LOG_LEVEL_TRACE, "Queued event %d (queue size: %u)", 
              event->event_type, g_event_system.queue_count);

    return true;
}

void aroma_event_process_queue(void) {
    if (!g_event_system.initialized || g_event_system.shutting_down) return;

    EVENT_LOG(LOG_LEVEL_TRACE, "Processing event queue (size: %u)", g_event_system.queue_count);

    uint32_t processed = 0;

    while (g_event_system.queue_head != g_event_system.queue_tail) {
        EVENT_LOCK();
        AromaEvent* ev = g_event_system.event_queue[g_event_system.queue_head];
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
        g_event_system.queue_count--;
        EVENT_UNLOCK();

        if (ev) {
            aroma_event_dispatch(ev);
            aroma_event_destroy(ev);
            processed++;
        }
    }

    aroma_event_resync_hover();

    EVENT_LOG(LOG_LEVEL_TRACE, "Processed %u events", processed);
}

void aroma_event_handle_pointer_move(int x, int y, bool button_down) {
    if (!g_event_system.root_node || g_event_system.shutting_down) return;

    g_mouse_state.button_down = button_down;

    int delta_x = x - g_mouse_state.last_x;
    int delta_y = y - g_mouse_state.last_y;
    g_mouse_state.last_x = x;
    g_mouse_state.last_y = y;

    AromaNode* target = aroma_event_hit_test(g_event_system.root_node, x, y);
    uint64_t current_id = target ? target->node_id : 0;

    if (current_id != g_mouse_state.hovered_node_id) {

        if (g_mouse_state.hovered_node_id != 0) {
            AromaNode* old = __find_node_by_id(g_event_system.root_node, g_mouse_state.hovered_node_id);
            if (old) {
                AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_EXIT, 
                    old->node_id, x, y, 0);
                if (ev) {
                    aroma_event_dispatch(ev);
                    aroma_event_destroy(ev);
                }
            }
        }

        if (target) {
            AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_ENTER, 
                target->node_id, x, y, 0);
            if (ev) {
                aroma_event_dispatch(ev);
                aroma_event_destroy(ev);
            }
        }

        g_mouse_state.hovered_node_id = current_id;
    }

    if (target) {
        AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_MOVE, 
            target->node_id, x, y, button_down ? 1 : 0);
        if (ev) {
            aroma_event_dispatch(ev);
            aroma_event_destroy(ev);
        }
    }
}

void aroma_event_resync_hover(void) {
    if (!g_event_system.root_node || g_event_system.shutting_down) return;

    if (g_mouse_state.last_x < 0 || g_mouse_state.last_y < 0) return;

    AromaNode* target = aroma_event_hit_test(g_event_system.root_node, 
        g_mouse_state.last_x, g_mouse_state.last_y);
    uint64_t current_id = target ? target->node_id : 0;

    if (current_id != g_mouse_state.hovered_node_id) {
        if (g_mouse_state.hovered_node_id != 0) {
            AromaNode* old = __find_node_by_id(g_event_system.root_node, g_mouse_state.hovered_node_id);
            if (old) {
                AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_EXIT, 
                    old->node_id, g_mouse_state.last_x, g_mouse_state.last_y, 0);
                if (ev) {
                    aroma_event_dispatch(ev);
                    aroma_event_destroy(ev);
                }
            }
        }

        if (target) {
            AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_ENTER, 
                target->node_id, g_mouse_state.last_x, g_mouse_state.last_y, 0);
            if (ev) {
                aroma_event_dispatch(ev);
                aroma_event_destroy(ev);
            }
        }

        g_mouse_state.hovered_node_id = current_id;
    }
}

AromaNode* aroma_event_hit_test(AromaNode* root, int x, int y) {
    if (!root || root->is_hidden || g_event_system.shutting_down) {
        return NULL;
    }

    if (!root->parent_node) {
        AromaNode* overlay_target = NULL;
        if (aroma_dropdown_overlay_hit_test(x, y, &overlay_target)) {
            return overlay_target;
        }
    }

    AromaNode* best = NULL;
    int32_t best_z = INT32_MIN;

    for (uint64_t i = 0; i < root->child_count; i++) {
        AromaNode* child = root->child_nodes[i];
        if (!child) continue;

        AromaNode* hit = aroma_event_hit_test(child, x, y);
        if (hit && hit->z_index >= best_z) {
            best = hit;
            best_z = hit->z_index;
        }
    }

    if (root->node_type == NODE_TYPE_WIDGET && root->node_widget_ptr) {
        AromaRect* bounds = (AromaRect*)root->node_widget_ptr;

        if (x >= bounds->x && x < (bounds->x + bounds->width) &&
            y >= bounds->y && y < (bounds->y + bounds->height)) {

            if (root->z_index >= best_z) {
                best = root;
                best_z = root->z_index;
            }
        }
    }

    return best;
}

bool aroma_event_subscribe(uint64_t node_id, AromaEventType type, 
                          AromaEventHandler handler, void* user_data, uint32_t priority) {
    if (!handler || node_id == 0 || g_event_system.shutting_down) {
        return false;
    }

    AromaNodeEventListeners* ls = aroma_event_get_listeners(node_id);
    if (!ls) {
        EVENT_LOG(LOG_LEVEL_ERROR, "Failed to get listeners for node %llu", 
                  (unsigned long long)node_id);
        return false;
    }

    EVENT_LOCK();

    for (uint32_t i = 0; i < ls->listener_count; i++) {
        if (ls->listeners[i].event_type == type && 
            ls->listeners[i].handler == handler &&
            ls->listeners[i].user_data == user_data) {
            EVENT_UNLOCK();
            EVENT_LOG(LOG_LEVEL_WARN, "Duplicate listener for node %llu, event %d", 
                      (unsigned long long)node_id, type);
            return false;
        }
    }

    if (ls->listener_count >= AROMA_MAX_LISTENERS_PER_NODE) {
        EVENT_UNLOCK();
        EVENT_LOG(LOG_LEVEL_ERROR, "Too many listeners for node %llu", 
                  (unsigned long long)node_id);
        return false;
    }

    uint32_t pos = ls->listener_count;
    for (uint32_t i = 0; i < ls->listener_count; i++) {
        if (priority > ls->listeners[i].priority) {
            pos = i;
            break;
        }
    }

    for (uint32_t i = ls->listener_count; i > pos; i--) {
        ls->listeners[i] = ls->listeners[i - 1];
    }

    ls->listeners[pos] = (AromaEventListener){
        .event_type = type,
        .handler = handler,
        .user_data = user_data,
        .priority = priority
    };
    ls->listener_count++;

    EVENT_UNLOCK();

    EVENT_LOG(LOG_LEVEL_DEBUG, "Added listener for node %llu, event %d (priority: %u)", 
              (unsigned long long)node_id, type, priority);

    return true;
}

bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType type, 
                            AromaEventHandler handler) {
    if (!handler || node_id == 0) return false;

    AromaNodeEventListeners* ls = aroma_event_find_listeners(node_id);
    if (!ls) return false;

    EVENT_LOCK();

    for (uint32_t i = 0; i < ls->listener_count; i++) {
        if (ls->listeners[i].event_type == type && 
            ls->listeners[i].handler == handler) {

            for (uint32_t k = i; k < ls->listener_count - 1; k++) {
                ls->listeners[k] = ls->listeners[k + 1];
            }

            ls->listener_count--;

            if (ls->listener_count == 0) {
                ls->node_id = 0;
                g_event_system.map_count--;
            }

            EVENT_UNLOCK();

            EVENT_LOG(LOG_LEVEL_DEBUG, "Removed listener for node %llu, event %d", 
                      (unsigned long long)node_id, type);

            return true;
        }
    }

    EVENT_UNLOCK();
    return false;
}

AromaEvent* aroma_event_create_mouse(AromaEventType type, uint64_t node_id, 
                                     int x, int y, uint8_t button) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;

    ev->event_type = type;
    ev->target_node_id = node_id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, node_id);

    ev->data.mouse.x = x;
    ev->data.mouse.y = y;
    ev->data.mouse.button = button;

    if (clock_gettime(CLOCK_MONOTONIC, &ev->timestamp) != 0) {
        ev->timestamp.tv_sec = 0;
        ev->timestamp.tv_nsec = 0;
    }

    return ev;
}

AromaEvent* aroma_event_create_key(AromaEventType type, uint64_t node_id, 
                                   uint32_t key_code, uint16_t modifiers) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;

    ev->event_type = type;
    ev->target_node_id = node_id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, node_id);

    ev->data.key.key_code = key_code;
    ev->data.key.modifiers = modifiers;

    if (clock_gettime(CLOCK_MONOTONIC, &ev->timestamp) != 0) {
        ev->timestamp.tv_sec = 0;
        ev->timestamp.tv_nsec = 0;
    }

    return ev;
}

AromaEvent* aroma_event_create_custom(uint64_t node_id, uint32_t custom_type,
                                      void* data, void (*free_func)(void*)) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;

    ev->event_type = EVENT_TYPE_CUSTOM;
    ev->target_node_id = node_id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, node_id);

    ev->data.custom.custom_type = custom_type;
    ev->data.custom.data = data;
    ev->data.custom.free_data = free_func;

    if (clock_gettime(CLOCK_MONOTONIC, &ev->timestamp) != 0) {
        ev->timestamp.tv_sec = 0;
        ev->timestamp.tv_nsec = 0;
    }

    return ev;
}

void aroma_event_destroy(AromaEvent* event) {
    if (!event) return;

    if (event->event_type == EVENT_TYPE_CUSTOM && 
        event->data.custom.free_data && 
        event->data.custom.data) {
        event->data.custom.free_data(event->data.custom.data);
    }

    aroma_event_release(event);
}

void aroma_event_consume(AromaEvent* ev) { 
    if (ev) ev->consumed = true; 
}

const char* aroma_event_type_name(AromaEventType event_type) {
    static const char* names[] = {
        "MOUSE_MOVE",
        "MOUSE_CLICK", 
        "MOUSE_RELEASE",
        "MOUSE_ENTER",
        "MOUSE_EXIT",
        "MOUSE_HOVER",
        "MOUSE_DOUBLE_CLICK",
        "KEY_PRESS",
        "KEY_RELEASE",
        "FOCUS_GAINED",
        "FOCUS_LOST",
        "CUSTOM",
        "UNKNOWN"
    };

    if (event_type < EVENT_TYPE_COUNT) {
        return names[event_type];
    }
    return names[EVENT_TYPE_COUNT];
}