#ifndef AROMA_EVENT_H
#define AROMA_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct AromaNode AromaNode;
typedef struct AromaEvent AromaEvent;

/**
 * @brief Event types supported by the Aroma event system
 */
typedef enum AromaEventType {
    EVENT_TYPE_MOUSE_MOVE,          /**< Mouse movement */
    EVENT_TYPE_MOUSE_CLICK,         /**< Mouse button clicked */
    EVENT_TYPE_MOUSE_RELEASE,       /**< Mouse button released */
    EVENT_TYPE_MOUSE_ENTER,         /**< Mouse entered widget bounds */
    EVENT_TYPE_MOUSE_EXIT,          /**< Mouse exited widget bounds */
    EVENT_TYPE_MOUSE_HOVER,         /**< Mouse hovering over widget */
    EVENT_TYPE_MOUSE_DOUBLE_CLICK,  /**< Double click */
    EVENT_TYPE_KEY_PRESS,           /**< Keyboard key pressed */
    EVENT_TYPE_KEY_RELEASE,         /**< Keyboard key released */
    EVENT_TYPE_FOCUS_GAINED,        /**< Widget gained focus */
    EVENT_TYPE_FOCUS_LOST,          /**< Widget lost focus */
    EVENT_TYPE_CUSTOM,              /**< Custom application event */
    EVENT_TYPE_COUNT                /**< Total event type count */
} AromaEventType;

/**
 * @brief Mouse event data
 */
typedef struct {
    int x;                          /**< Mouse X position */
    int y;                          /**< Mouse Y position */
    int delta_x;                    /**< Mouse X movement delta */
    int delta_y;                    /**< Mouse Y movement delta */
    uint8_t button;                 /**< Mouse button (0=left, 1=middle, 2=right) */
    uint8_t clicks;                 /**< Number of consecutive clicks */
} AromaMouseEventData;

/**
 * @brief Keyboard event data
 */
typedef struct {
    uint32_t key_code;              /**< Key code */
    uint32_t scan_code;             /**< Scan code */
    uint16_t modifiers;             /**< Modifier flags (shift, ctrl, alt) */
    bool repeat;                    /**< Key repeat event */
} AromaKeyEventData;

/**
 * @brief Custom event data
 */
typedef struct {
    uint32_t custom_type;           /**< Custom event type ID */
    void* data;                     /**< Custom data pointer */
    void (*free_data)(void*);       /**< Cleanup function for custom data */
} AromaCustomEventData;

/**
 * @brief Main event structure
 */
struct AromaEvent {
    AromaEventType event_type;      /**< Type of event */
    uint64_t target_node_id;        /**< Target node ID */
    AromaNode* target_node;         /**< Target node pointer */
    struct timespec timestamp;      /**< Event timestamp */
    bool consumed;                  /**< Whether event was consumed */
    
    union {
        AromaMouseEventData mouse;
        AromaKeyEventData key;
        AromaCustomEventData custom;
    } data;                         /**< Event-specific data */
};

/**
 * @brief Event handler callback type
 * @param event The event that occurred
 * @param user_data User-defined data
 * @return true if event was handled/consumed, false otherwise
 */
typedef bool (*AromaEventHandler)(AromaEvent* event, void* user_data);

/**
 * @brief Event listener registration
 */
typedef struct {
    AromaEventType event_type;      /**< Type of event to listen for */
    AromaEventHandler handler;      /**< Handler callback */
    void* user_data;                /**< User-defined data */
    uint32_t priority;              /**< Handler priority (higher = called first) */
} AromaEventListener;

/**
 * Initialize the event system
 * @return true on success
 */
bool aroma_event_system_init(void);

/**
 * Shutdown the event system
 */
void aroma_event_system_shutdown(void);

/**
 * Set the root node used for hit-testing and ID lookups
 * @param root Scene graph root node
 */
void aroma_event_set_root(AromaNode* root);

/**
 * Get the currently configured root node
 * @return Scene graph root node or NULL
 */
AromaNode* aroma_event_get_root(void);

/**
 * Create a new event
 * @param event_type Type of event
 * @param target_node_id Target node ID
 * @return Allocated event or NULL on failure
 */
AromaEvent* aroma_event_create(AromaEventType event_type, uint64_t target_node_id);

/**
 * Dispatch an event to the scene graph
 * @param event Event to dispatch
 * @return true if event was handled
 */
bool aroma_event_dispatch(AromaEvent* event);

/**
 * Queue an event for later dispatch
 * @param event Event to queue
 * @return true on success
 */
bool aroma_event_queue(AromaEvent* event);

/**
 * Process all queued events
 */
void aroma_event_process_queue(void);

/**
 * Subscribe to an event on a specific node
 * @param node_id Node to subscribe to
 * @param event_type Event type to listen for
 * @param handler Event handler function
 * @param user_data User data for callback
 * @param priority Handler priority
 * @return true on success
 */
bool aroma_event_subscribe(uint64_t node_id, AromaEventType event_type, 
                          AromaEventHandler handler, void* user_data, uint32_t priority);

/**
 * Unsubscribe from an event on a node
 * @param node_id Node ID
 * @param event_type Event type
 * @param handler Handler to remove
 * @return true on success
 */
bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType event_type, 
                            AromaEventHandler handler);

/**
 * Create a mouse event
 * @param event_type Type of mouse event
 * @param target_node_id Target node
 * @param x Mouse X position
 * @param y Mouse Y position
 * @param button Mouse button
 * @return Allocated event or NULL
 */
AromaEvent* aroma_event_create_mouse(AromaEventType event_type, uint64_t target_node_id,
                                    int x, int y, uint8_t button);

/**
 * Create a keyboard event
 * @param event_type Type of keyboard event
 * @param target_node_id Target node
 * @param key_code Key code
 * @param modifiers Modifier flags
 * @return Allocated event or NULL
 */
AromaEvent* aroma_event_create_key(AromaEventType event_type, uint64_t target_node_id,
                                   uint32_t key_code, uint16_t modifiers);

/**
 * Create a custom event
 * @param target_node_id Target node
 * @param custom_type Custom event type
 * @param data Custom data
 * @param free_func Cleanup function
 * @return Allocated event or NULL
 */
AromaEvent* aroma_event_create_custom(uint64_t target_node_id, uint32_t custom_type,
                                      void* data, void (*free_func)(void*));

/**
 * Destroy an event and free resources
 * @param event Event to destroy
 */
void aroma_event_destroy(AromaEvent* event);

/**
 * Mark event as consumed (stops propagation)
 * @param event Event to consume
 */
void aroma_event_consume(AromaEvent* event);

/**
 * Hit test to find node at screen position
 * @param root Root node
 * @param x X position
 * @param y Y position
 * @return Node at position or NULL
 */
AromaNode* aroma_event_hit_test(AromaNode* root, int x, int y);

/**
 * Get human-readable event type name
 * @param event_type Event type
 * @return String name of event type
 */
const char* aroma_event_type_name(AromaEventType event_type);
// Mouse handling functions (separate from event system)
void aroma_event_update_mouse_position(int x, int y);
void aroma_event_get_mouse_position(int* x, int* y);
bool aroma_event_is_mouse_over_widget(void);
AromaNode* aroma_event_get_hover_node(void);

// Mouse click helpers
void aroma_event_handle_mouse_click(uint8_t button, bool pressed);
void aroma_event_handle_double_click(uint8_t button);

// Keyboard helper
void aroma_event_handle_key(uint32_t key_code, bool pressed);

// Queue management
uint32_t aroma_event_queue_size(void);
void aroma_event_clear_queue_public(void);
#endif // AROMA_EVENT_H
