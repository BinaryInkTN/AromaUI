#include "aroma_event.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"
#include "aroma_logger.h"
#include "widgets/aroma_button.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AROMA_MAX_LISTENERS_PER_NODE 16

#define AROMA_MAX_EVENT_QUEUE 256

typedef struct {
    uint64_t node_id;                      

    AromaEventListener listeners[AROMA_MAX_LISTENERS_PER_NODE];
    uint32_t listener_count;
} AromaNodeEventListeners;

static struct {
    bool initialized;
    AromaNodeEventListeners* listener_map;  

    uint32_t map_capacity;
    AromaEvent* event_queue[AROMA_MAX_EVENT_QUEUE];
    uint32_t queue_head;
    uint32_t queue_tail;
    AromaNode* root_node;                    

} g_event_system = {0};

static AromaEvent g_event_pool[AROMA_MAX_EVENT_QUEUE];
static bool g_event_pool_used[AROMA_MAX_EVENT_QUEUE];

static int last_mouse_x = -1;
static int last_mouse_y = -1;
static bool last_was_over_widget = false;

static AromaEvent* aroma_event_alloc(void)
{
    for (uint32_t i = 0; i < AROMA_MAX_EVENT_QUEUE; i++) {
        if (!g_event_pool_used[i]) {
            g_event_pool_used[i] = true;
            memset(&g_event_pool[i], 0, sizeof(AromaEvent));
            return &g_event_pool[i];
        }
    }
    LOG_ERROR("Event pool exhausted");
    return NULL;
}

static void aroma_event_free(AromaEvent* event)
{
    if (!event) return;
    uintptr_t idx = (uintptr_t)(event - g_event_pool);
    if (idx < AROMA_MAX_EVENT_QUEUE) {
        g_event_pool_used[idx] = false;
    }
}

static const char* event_type_names[] = {
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
    "CUSTOM"
};

static AromaNodeEventListeners* aroma_event_get_listeners(uint64_t node_id)
{
    if (!g_event_system.initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++) {
        AromaNodeEventListeners* entry = &g_event_system.listener_map[i];

        if (entry->node_id == node_id) {
            return entry;
        }

        if (entry->node_id == 0) {
            memset(entry, 0, sizeof(AromaNodeEventListeners));
            entry->node_id = node_id;
            return entry;
        }
    }

    return NULL; 

}

static AromaNodeEventListeners* aroma_event_find_listeners(uint64_t node_id)
{
    if (!g_event_system.initialized) {
        return NULL;
    }

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++) {
        AromaNodeEventListeners* entry = &g_event_system.listener_map[i];
        if (entry->node_id == node_id && entry->listener_count > 0) {
            return entry;
        }
    }
    return NULL;
}

static bool aroma_event_propagate_up(AromaEvent* event)
{
    if (!event || !event->target_node) {
        return false;
    }

    AromaNode* current = event->target_node;
    bool handled = false;
    uint32_t depth = 0;
    const uint32_t MAX_PROPAGATION_DEPTH = 1000; 

    while (current && !event->consumed && depth < MAX_PROPAGATION_DEPTH) {

        AromaNodeEventListeners* listeners = aroma_event_find_listeners(current->node_id);

        if (listeners && listeners->listener_count > 0) {
            for (uint32_t j = 0; j < listeners->listener_count; j++) {
                AromaEventListener* listener = &listeners->listeners[j];
                if (listener->event_type == event->event_type && listener->handler) {
                    if (listener->handler(event, listener->user_data)) {
                        handled = true;
                        if (!event->consumed) {
                            aroma_event_consume(event);
                        }
                    }
                }
                if (event->consumed) break;
            }
        }

        current = current->parent_node;
        depth++;
    }

    if (depth >= MAX_PROPAGATION_DEPTH) {
        LOG_WARNING("Event propagation exceeded max depth - possible circular reference");
    }

    return handled;
}

bool aroma_event_system_init(void)
{
    if (g_event_system.initialized) {
        LOG_WARNING("Event system already initialized");
        return true;
    }

    g_event_system.map_capacity = 256;
    g_event_system.listener_map = (AromaNodeEventListeners*)calloc(
        g_event_system.map_capacity, 
        sizeof(AromaNodeEventListeners)
    );

    if (!g_event_system.listener_map) {
        LOG_ERROR("Failed to allocate event listener map");
        return false;
    }

    g_event_system.queue_head = 0;
    g_event_system.queue_tail = 0;
    g_event_system.root_node = NULL;
    g_event_system.initialized = true;

    LOG_INFO("Event system initialized (capacity: %u)", g_event_system.map_capacity);
    return true;
}

void aroma_event_system_shutdown(void)
{
    if (!g_event_system.initialized) {
        return;
    }

    while (g_event_system.queue_head != g_event_system.queue_tail) {
        AromaEvent* event = g_event_system.event_queue[g_event_system.queue_head];
        if (event) {
            aroma_event_destroy(event);
        }
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
    }

    if (g_event_system.listener_map) {
        free(g_event_system.listener_map);
        g_event_system.listener_map = NULL;
    }

    memset(g_event_pool_used, 0, sizeof(g_event_pool_used));

    g_event_system.initialized = false;
    LOG_INFO("Event system shutdown");
}

void aroma_event_set_root(AromaNode* root)
{
    g_event_system.root_node = root;
}

AromaNode* aroma_event_get_root(void)
{
    return g_event_system.root_node;
}

AromaEvent* aroma_event_create(AromaEventType event_type, uint64_t target_node_id)
{
    if (event_type >= EVENT_TYPE_COUNT) {
        LOG_ERROR("Invalid event type: %d", event_type);
        return NULL;
    }

    AromaEvent* event = aroma_event_alloc();
    if (!event) {
        return NULL;
    }
    event->event_type = event_type;
    event->target_node_id = target_node_id;
    event->target_node = g_event_system.root_node
        ? __find_node_by_id(g_event_system.root_node, target_node_id)
        : NULL;
    clock_gettime(CLOCK_MONOTONIC, &event->timestamp);
    event->consumed = false;

    return event;
}

bool aroma_event_dispatch(AromaEvent* event)
{
    if (!event) {
        return false;
    }

    if (!g_event_system.initialized) {
        LOG_ERROR("Event system not initialized");
        return false;
    }

    LOG_INFO("Dispatching event: %s to node %llu", 
             aroma_event_type_name(event->event_type), event->target_node_id);

    bool handled = aroma_event_propagate_up(event);

    if (handled) {
        LOG_INFO("Event consumed");
    }

    return handled;
}

bool aroma_event_queue(AromaEvent* event)
{
    if (!event || !g_event_system.initialized) {
        return false;
    }

    uint32_t next_tail = (g_event_system.queue_tail + 1) % AROMA_MAX_EVENT_QUEUE;

    if (next_tail == g_event_system.queue_head) {
        LOG_ERROR("Event queue is full");
        aroma_event_destroy(event); 

        return false;
    }

    g_event_system.event_queue[g_event_system.queue_tail] = event;
    g_event_system.queue_tail = next_tail;

    LOG_INFO("Event queued: %s", aroma_event_type_name(event->event_type));
    return true;
}

void aroma_event_process_queue(void)
{
    if (!g_event_system.initialized) {
        return;
    }

    uint32_t iterations = 0;
    const uint32_t MAX_ITERATIONS = AROMA_MAX_EVENT_QUEUE * 2; 

    while (g_event_system.queue_head != g_event_system.queue_tail && iterations < MAX_ITERATIONS) {
        AromaEvent* event = g_event_system.event_queue[g_event_system.queue_head];
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;

        if (event) {
            aroma_event_dispatch(event);
            aroma_event_destroy(event);
        }
        iterations++;
    }

    if (iterations >= MAX_ITERATIONS) {
        LOG_WARNING("Event queue processing exceeded iteration limit - possible event loop");
    }
}

bool aroma_event_subscribe(uint64_t node_id, AromaEventType event_type,
                          AromaEventHandler handler, void* user_data, uint32_t priority)
{
    if (!g_event_system.initialized || !handler) {
        LOG_ERROR("Event system not initialized or invalid handler");
        return false;
    }

    if (event_type >= EVENT_TYPE_COUNT) {
        LOG_ERROR("Invalid event type: %d", event_type);
        return false;
    }

    AromaNodeEventListeners* listeners = aroma_event_get_listeners(node_id);
    if (!listeners) {
        LOG_ERROR("Failed to get/create listener list");
        return false;
    }

    if (listeners->listener_count >= AROMA_MAX_LISTENERS_PER_NODE) {
        LOG_WARNING("Node %llu reached maximum listeners", node_id);
        return false;
    }

    uint32_t insert_pos = listeners->listener_count;
    for (uint32_t i = 0; i < listeners->listener_count; i++) {
        if (priority > listeners->listeners[i].priority) {
            insert_pos = i;
            break;
        }
    }

    for (uint32_t i = listeners->listener_count; i > insert_pos; i--) {
        listeners->listeners[i] = listeners->listeners[i - 1];
    }

    listeners->listeners[insert_pos].event_type = event_type;
    listeners->listeners[insert_pos].handler = handler;
    listeners->listeners[insert_pos].user_data = user_data;
    listeners->listeners[insert_pos].priority = priority;
    listeners->listener_count++;

    LOG_INFO("Event listener registered: node=%llu, type=%s, priority=%u",
             node_id, aroma_event_type_name(event_type), priority);

    return true;
}

bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType event_type,
                            AromaEventHandler handler)
{
    if (!g_event_system.initialized) {
        return false;
    }

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++) {
        AromaNodeEventListeners* listeners = &g_event_system.listener_map[i];
        if (listeners->node_id != node_id) {
            continue;
        }

        for (uint32_t j = 0; j < listeners->listener_count; j++) {
            AromaEventListener* listener = &listeners->listeners[j];
            if (listener->event_type == event_type && listener->handler == handler) {

                for (uint32_t k = j; k < listeners->listener_count - 1; k++) {
                    listeners->listeners[k] = listeners->listeners[k + 1];
                }
                listeners->listener_count--;

                LOG_INFO("Event listener unregistered: node=%llu, type=%s", 
                         (unsigned long long)node_id, aroma_event_type_name(event_type));
                return true;
            }
        }
    }

    return false;
}

AromaEvent* aroma_event_create_mouse(AromaEventType event_type, uint64_t target_node_id,
                                    int x, int y, uint8_t button)
{
    AromaEvent* event = aroma_event_create(event_type, target_node_id);
    if (!event) {
        return NULL;
    }

    event->data.mouse.x = x;
    event->data.mouse.y = y;
    event->data.mouse.button = button;

    return event;
}

AromaEvent* aroma_event_create_key(AromaEventType event_type, uint64_t target_node_id,
                                   uint32_t key_code, uint16_t modifiers)
{
    AromaEvent* event = aroma_event_create(event_type, target_node_id);
    if (!event) {
        return NULL;
    }

    event->data.key.key_code = key_code;
    event->data.key.modifiers = modifiers;

    return event;
}

AromaEvent* aroma_event_create_custom(uint64_t target_node_id, uint32_t custom_type,
                                      void* data, void (*free_func)(void*))
{
    AromaEvent* event = aroma_event_create(EVENT_TYPE_CUSTOM, target_node_id);
    if (!event) {
        return NULL;
    }

    event->data.custom.custom_type = custom_type;
    event->data.custom.data = data;
    event->data.custom.free_data = free_func;

    return event;
}

void aroma_event_destroy(AromaEvent* event)
{
    if (!event) {
        return;
    }

    if (event->event_type == EVENT_TYPE_CUSTOM && event->data.custom.free_data) {
        event->data.custom.free_data(event->data.custom.data);
    }

    aroma_event_free(event);
}

void aroma_event_consume(AromaEvent* event)
{
    if (event) {
        event->consumed = true;
        LOG_INFO("Event consumed");
    }
}

AromaNode* aroma_event_hit_test(AromaNode* root, int x, int y)
{
    if (!root) {
        return NULL;
    }

    for (uint64_t i = root->child_count; i > 0; i--) {
        AromaNode* child = root->child_nodes[i - 1];
        if (!child) continue;

        if (child->node_type == NODE_TYPE_WIDGET && child->node_widget_ptr) {
            AromaButton* btn = (AromaButton*)child->node_widget_ptr;
            bool in_bounds = (x >= btn->rect.x && x <= (btn->rect.x + btn->rect.width) &&
                              y >= btn->rect.y && y <= (btn->rect.y + btn->rect.height));
            if (in_bounds) {
                return child;
            }

            continue;
        }

        AromaNode* hit = aroma_event_hit_test(child, x, y);
        if (hit) {
            return hit;
        }
    }

    return root;
}

const char* aroma_event_type_name(AromaEventType event_type)
{
    if (event_type < EVENT_TYPE_COUNT) {
        return event_type_names[event_type];
    }
    return "UNKNOWN";
}

