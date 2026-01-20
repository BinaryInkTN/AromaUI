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

    uint32_t map_count;
    AromaEvent* event_queue[AROMA_MAX_EVENT_QUEUE];
    uint32_t queue_head;
    uint32_t queue_tail;
    AromaNode* root_node;
} g_event_system = {0};

static AromaEvent g_event_pool[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_list[AROMA_MAX_EVENT_QUEUE];
static uint32_t g_event_free_head = 0;
static uint32_t g_event_free_count = 0;

static int last_mouse_x = -1;
static int last_mouse_y = -1;
static uint64_t last_hovered_node_id = 0;

static inline uint32_t hash_node_id(uint64_t node_id, uint32_t capacity) {
    uint32_t hash = (uint32_t)(node_id ^ (node_id >> 32));
    return hash & (capacity - 1); 
}

static void expand_listener_map(void) {
    uint32_t new_capacity = (g_event_system.map_capacity < 16) ? 16 : g_event_system.map_capacity * 2;
    AromaNodeEventListeners* new_map = (AromaNodeEventListeners*)calloc(new_capacity, sizeof(AromaNodeEventListeners));
    if (!new_map) return;

    for (uint32_t i = 0; i < g_event_system.map_capacity; i++) {
        if (g_event_system.listener_map[i].node_id == 0) continue;
        uint32_t idx = hash_node_id(g_event_system.listener_map[i].node_id, new_capacity);
        while (new_map[idx].node_id != 0) idx = (idx + 1) & (new_capacity - 1);
        new_map[idx] = g_event_system.listener_map[i];
    }
    free(g_event_system.listener_map);
    g_event_system.listener_map = new_map;
    g_event_system.map_capacity = new_capacity;
}

static AromaNodeEventListeners* aroma_event_get_listeners(uint64_t node_id) {
    if (!g_event_system.initialized) return NULL;
    if (g_event_system.map_count * 2 >= g_event_system.map_capacity) expand_listener_map();
    uint32_t idx = hash_node_id(node_id, g_event_system.map_capacity);
    while (g_event_system.listener_map[idx].node_id != 0) {
        if (g_event_system.listener_map[idx].node_id == node_id) return &g_event_system.listener_map[idx];
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
    }
    g_event_system.listener_map[idx].node_id = node_id;
    g_event_system.map_count++;
    return &g_event_system.listener_map[idx];
}

static AromaNodeEventListeners* aroma_event_find_listeners(uint64_t node_id) {
    if (!g_event_system.initialized || g_event_system.map_count == 0) return NULL;
    uint32_t idx = hash_node_id(node_id, g_event_system.map_capacity);
    while (g_event_system.listener_map[idx].node_id != 0) {
        if (g_event_system.listener_map[idx].node_id == node_id) return &g_event_system.listener_map[idx];
        idx = (idx + 1) & (g_event_system.map_capacity - 1);
    }
    return NULL;
}

static AromaEvent* aroma_event_alloc(void) {
    if (g_event_free_count == 0) return NULL;
    uint32_t idx = g_event_free_list[g_event_free_head];
    g_event_free_head = (g_event_free_head + 1) % AROMA_MAX_EVENT_QUEUE;
    g_event_free_count--;
    memset(&g_event_pool[idx], 0, sizeof(AromaEvent));
    return &g_event_pool[idx];
}

bool aroma_event_system_init(void) {
    if (g_event_system.initialized) return true;
    g_event_system.map_capacity = 32;
    g_event_system.listener_map = (AromaNodeEventListeners*)calloc(g_event_system.map_capacity, sizeof(AromaNodeEventListeners));
    for (uint32_t i = 0; i < AROMA_MAX_EVENT_QUEUE; i++) g_event_free_list[i] = i;
    g_event_free_count = AROMA_MAX_EVENT_QUEUE;
    g_event_system.initialized = (g_event_system.listener_map != NULL);
    return g_event_system.initialized;
}

void aroma_event_system_shutdown(void) {
    if (!g_event_system.initialized) return;
    aroma_event_process_queue();
    if (g_event_system.listener_map) free(g_event_system.listener_map);
    memset(&g_event_system, 0, sizeof(g_event_system));
}

AromaNode* aroma_event_hit_test(AromaNode* node, int x, int y) {
    if (!node) return NULL;
    if (node->is_hidden) return NULL;

    if (!node->parent_node) {
        AromaNode* overlay_target = NULL;
        if (aroma_dropdown_overlay_hit_test(x, y, &overlay_target)) {
            return overlay_target;
        }
    }

    AromaNode* best = NULL;
    int32_t best_z = INT32_MIN;

    for (uint64_t i = 0; i < node->child_count; i++) {
        AromaNode* hit = aroma_event_hit_test(node->child_nodes[i], x, y);
        if (hit && hit->z_index >= best_z) {
            best = hit;
            best_z = hit->z_index;
        }
    }

    if (node->node_type == NODE_TYPE_WIDGET && node->node_widget_ptr) {
        AromaRect* r = (AromaRect*)node->node_widget_ptr;
        if (x >= r->x && x < (r->x + r->width) && y >= r->y && y < (r->y + r->height)) {
            if (node->z_index >= best_z) {
                best = node;
                best_z = node->z_index;
            }
        }
    }

    return best;
}

void aroma_event_handle_pointer_move(int x, int y, bool button_down) {
    if (!g_event_system.root_node) return;
    AromaNode* target = aroma_event_hit_test(g_event_system.root_node, x, y);
    uint64_t current_id = target ? target->node_id : 0;

    if (current_id != last_hovered_node_id) {
        if (last_hovered_node_id != 0) {
            AromaNode* old = __find_node_by_id(g_event_system.root_node, last_hovered_node_id);
            if (old) {
                AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_EXIT, old->node_id, x, y, 0);
                if (ev) { aroma_event_dispatch(ev); aroma_event_destroy(ev); }
            }
        }
        if (target) {
            AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_ENTER, target->node_id, x, y, 0);
            if (ev) { aroma_event_dispatch(ev); aroma_event_destroy(ev); }
        }
        last_hovered_node_id = current_id;
    }
    last_mouse_x = x; last_mouse_y = y;
}

void aroma_event_resync_hover(void) {
    if (!g_event_system.root_node) return;

    AromaNode* target = aroma_event_hit_test(g_event_system.root_node, last_mouse_x, last_mouse_y);
    uint64_t current_id = target ? target->node_id : 0;

    if (current_id != last_hovered_node_id) {
        if (last_hovered_node_id != 0) {
            AromaNode* old = __find_node_by_id(g_event_system.root_node, last_hovered_node_id);
            if (old) {
                AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_EXIT, old->node_id, last_mouse_x, last_mouse_y, 0);
                if (ev) { aroma_event_dispatch(ev); aroma_event_destroy(ev); }
            }
        }
        if (target) {
            AromaEvent* ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_ENTER, target->node_id, last_mouse_x, last_mouse_y, 0);
            if (ev) { aroma_event_dispatch(ev); aroma_event_destroy(ev); }
        }
        last_hovered_node_id = current_id;
    }
}

bool aroma_event_dispatch(AromaEvent* event) {
    if (!event || !event->target_node) return false;
    AromaNode* current = event->target_node;
    while (current && !event->consumed) {
        AromaNodeEventListeners* ls = aroma_event_find_listeners(current->node_id);
        if (ls) {
            for (uint32_t i = 0; i < ls->listener_count; i++) {
                if (ls->listeners[i].event_type == event->event_type) {
                    if (ls->listeners[i].handler(event, ls->listeners[i].user_data)) return true;
                }
            }
        }
        current = current->parent_node;
    }
    return false;
}

bool aroma_event_queue(AromaEvent* event) {
    if (!event) return false;
    uint32_t next_tail = (g_event_system.queue_tail + 1) % AROMA_MAX_EVENT_QUEUE;
    if (next_tail == g_event_system.queue_head) { aroma_event_destroy(event); return false; }
    g_event_system.event_queue[g_event_system.queue_tail] = event;
    g_event_system.queue_tail = next_tail;
    return true;
}

void aroma_event_process_queue(void) {
    while (g_event_system.queue_head != g_event_system.queue_tail) {
        AromaEvent* ev = g_event_system.event_queue[g_event_system.queue_head];
        g_event_system.queue_head = (g_event_system.queue_head + 1) % AROMA_MAX_EVENT_QUEUE;
        if (ev) { aroma_event_dispatch(ev); aroma_event_destroy(ev); }
    }

    aroma_event_resync_hover();
}

bool aroma_event_subscribe(uint64_t node_id, AromaEventType type, AromaEventHandler h, void* data, uint32_t priority) {
    AromaNodeEventListeners* ls = aroma_event_get_listeners(node_id);
    if (!ls || ls->listener_count >= AROMA_MAX_LISTENERS_PER_NODE) return false;
    uint32_t pos = ls->listener_count;
    for (uint32_t i = 0; i < ls->listener_count; i++) {
        if (priority > ls->listeners[i].priority) { pos = i; break; }
    }
    for (uint32_t i = ls->listener_count; i > pos; i--) ls->listeners[i] = ls->listeners[i - 1];
    ls->listeners[pos] = (AromaEventListener){type, h, data, priority};
    ls->listener_count++;
    return true;
}

bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType type, AromaEventHandler h) {
    AromaNodeEventListeners* ls = aroma_event_find_listeners(node_id);
    if (!ls) return false;
    for (uint32_t i = 0; i < ls->listener_count; i++) {
        if (ls->listeners[i].event_type == type && ls->listeners[i].handler == h) {
            for (uint32_t k = i; k < ls->listener_count - 1; k++) ls->listeners[k] = ls->listeners[k + 1];
            ls->listener_count--;
            return true;
        }
    }
    return false;
}

AromaEvent* aroma_event_create_mouse(AromaEventType type, uint64_t id, int x, int y, uint8_t btn) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type = type;
    ev->target_node_id = id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, id);
    ev->data.mouse.x = x; ev->data.mouse.y = y; ev->data.mouse.button = btn;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}

AromaEvent* aroma_event_create_key(AromaEventType type, uint64_t id, uint32_t code, uint16_t mods) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type = type;
    ev->target_node_id = id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, id);
    ev->data.key.key_code = code; ev->data.key.modifiers = mods;
    return ev;
}

AromaEvent* aroma_event_create_custom(uint64_t id, uint32_t type, void* data, void (*free_func)(void*)) {
    AromaEvent* ev = aroma_event_alloc();
    if (!ev) return NULL;
    ev->event_type = EVENT_TYPE_CUSTOM;
    ev->target_node_id = id;
    ev->target_node = __find_node_by_id(g_event_system.root_node, id);
    ev->data.custom.custom_type = type; ev->data.custom.data = data; ev->data.custom.free_data = free_func;
    return ev;
}

void aroma_event_destroy(AromaEvent* event) {
    if (!event) return;
    if (event->event_type == EVENT_TYPE_CUSTOM && event->data.custom.free_data) event->data.custom.free_data(event->data.custom.data);
    uintptr_t idx = (uintptr_t)(event - g_event_pool);
    uint32_t tail = (g_event_free_head + g_event_free_count) % AROMA_MAX_EVENT_QUEUE;
    g_event_free_list[tail] = (uint32_t)idx;
    g_event_free_count++;
}

void aroma_event_set_root(AromaNode* root) { g_event_system.root_node = root; }
AromaNode* aroma_event_get_root(void) { return g_event_system.root_node; }
void aroma_event_consume(AromaEvent* ev) { if (ev) ev->consumed = true; }
