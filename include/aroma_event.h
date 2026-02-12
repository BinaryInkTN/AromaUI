/**
 * @file aroma_event.h
 * @brief Input and system event handling subsystem.
 *
 * Defines the event structures, types, and the event dispatcher mechanism used to
 * propagate inputs (mouse, keyboard, etc.) and system events (window resize) to UI nodes.
 */
#ifndef AROMA_EVENT_H
#define AROMA_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaNode AromaNode;
typedef struct AromaEvent AromaEvent;

/** @brief Modifier flag for Caps Lock. */
#define AROMA_KEY_MOD_CAPSLOCK 0x0001u

/**
 * @brief Enumeration of supported event types.
 */
typedef enum AromaEventType {
    EVENT_TYPE_MOUSE_MOVE,        /**< Pointer moved. */
    EVENT_TYPE_MOUSE_CLICK,       /**< Mouse button pressed. */
    EVENT_TYPE_MOUSE_RELEASE,     /**< Mouse button released. */
    EVENT_TYPE_MOUSE_ENTER,       /**< Pointer entered node bounds. */
    EVENT_TYPE_MOUSE_EXIT,        /**< Pointer left node bounds. */
    EVENT_TYPE_MOUSE_HOVER,       /**< Pointer hovering over node. */
    EVENT_TYPE_MOUSE_DOUBLE_CLICK,/**< Double click detected. */
    EVENT_TYPE_KEY_PRESS,         /**< Keyboard key pressed. */
    EVENT_TYPE_KEY_RELEASE,       /**< Keyboard key released. */
    EVENT_TYPE_FOCUS_GAINED,      /**< Node gained keyboard focus. */
    EVENT_TYPE_FOCUS_LOST,        /**< Node lost keyboard focus. */
    EVENT_TYPE_WINDOW_RESIZE,     /**< Host window resized. */
    EVENT_TYPE_TOUCH_DOWN,        /**< Touch point down. */
    EVENT_TYPE_TOUCH_UP,          /**< Touch point up. */
    EVENT_TYPE_TOUCH_MOVE,        /**< Touch point moved. */
    EVENT_TYPE_CUSTOM,            /**< User-defined custom event. */
    EVENT_TYPE_COUNT              /**< Total number of event types. */
} AromaEventType;

/**
 * @brief Data associated with touch events.
 */
typedef struct {
    int id;         /**< Touch pointer ID. */
    int x;          /**< X coordinate. */
    int y;          /**< Y coordinate. */
} AromaTouchEventData;

/**
 * @brief Data associated with mouse/pointer events.
 */
typedef struct {
    int x;          /**< X coordinate relative to screen/window. */
    int y;          /**< Y coordinate relative to screen/window. */
    int delta_x;    /**< X movement delta since last event. */
    int delta_y;    /**< Y movement delta since last event. */
    uint8_t button; /**< Method/button index (e.g. 0=Left, 1=Right). */
    uint8_t clicks; /**< Click count (e.g. 1=single, 2=double). */
} AromaMouseEventData;

/**
 * @brief Data associated with window resize events.
 */
typedef struct {
    int width;      /**< New window width. */
    int height;     /**< New window height. */
} AromaWindowResizeEventData;

/**
 * @brief Data associated with keyboard events.
 */
typedef struct {
    uint32_t key_code;    /**< Virtual key code. */
    uint32_t scan_code;   /**< Hardware scan code. */
    uint16_t modifiers;   /**< Active modifiers (Shift, Ctrl, etc.). */
    bool repeat;          /**< True if this is a repeat key press. */
} AromaKeyEventData;

/**
 * @brief Data for custom user-defined events.
 */
typedef struct {
    uint32_t custom_type;       /**< User-defined type identifier. */
    void* data;                 /**< Pointer to custom data payload. */
    void (*free_data)(void*);  /**< Destructor for the payload. */
} AromaCustomEventData;

/**
 * @brief The main event structure passed to handlers.
 */
struct AromaEvent {
    AromaEventType event_type;      /**< Type of the event. */
    uint64_t target_node_id;        /**< ID of the target node (0 for broadcast/root). */
    AromaNode* target_node;         /**< Pointer to target node (resolved by dispatcher). */
    struct timespec timestamp;      /**< Time when the event occurred. */
    bool consumed;                  /**< True if event propagation should stop. */

    union {
        AromaMouseEventData mouse;     /**< Mouse event payload. */
        AromaTouchEventData touch;     /**< Touch event payload. */
        AromaKeyEventData key;         /**< Keyboard event payload. */
        AromaWindowResizeEventData resize; /**< Window resize payload. */
        AromaCustomEventData custom;   /**< Custom event payload. */
    } data;
};

/**
 * @brief Callback function prototype for handling events.
 * @param event The event to handle.
 * @param user_data User data associated with the handler.
 * @return true if the event was handled/consumed, false otherwise.
 */
typedef bool (*AromaEventHandler)(AromaEvent* event, void* user_data);

/**
 * @brief Structure representing a registered event listener.
 */
typedef struct {
    AromaEventType event_type; /**< Event type this listener is interested in. */
    AromaEventHandler handler; /**< Callback function. */
    void* user_data;           /**< User context. */
    uint32_t priority;         /**< Higher priority listeners run first. */
} AromaEventListener;

/**
 * @brief Initialize the event system.
 * @return true on success.
 */
bool aroma_event_system_init(void);

/**
 * @brief Shutdown the event system.
 */
void aroma_event_system_shutdown(void);

/**
 * @brief Set the global event root (usually the window root).
 * @param root The root node.
 */
void aroma_event_set_root(AromaNode* root);

/**
 * @brief Get the current event root node.
 * @return Pointer to the root node.
 */
AromaNode* aroma_event_get_root(void);

/**
 * @brief Create a new event object.
 * @param event_type Type of the event.
 * @param target_node_id Target node ID (can be 0).
 * @return Pointer to the new event, or NULL.
 */
AromaEvent* aroma_event_create(AromaEventType event_type, uint64_t target_node_id);

/**
 * @brief Dispatch an event immediately to the scene graph.
 * @param event The event to dispatch.
 * @return true if the event was consumed.
 */
bool aroma_event_dispatch(AromaEvent* event);

/**
 * @brief Add an event to the processing queue.
 * @param event The event to queue.
 * @return true on success.
 */
bool aroma_event_queue(AromaEvent* event);

/**
 * @brief Process all pending events in the queue.
 */
void aroma_event_process_queue(void);

/**
 * @brief Inject a touch event into the system.
 * 
 * @param id Touch pointer ID.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param state 0=UP, 1=DOWN, 2=MOVE.
 */
void aroma_event_handle_touch(int id, int x, int y, int state);

/**
 * @brief Helper to inject a pointer move event into the system.
 * @param x Current X coordinate.
 * @param y Current Y coordinate.
 * @param button_down True if a button is currently pressed.
 */
void aroma_event_handle_pointer_move(int x, int y, bool button_down);

void aroma_event_resync_hover(void);

bool aroma_event_subscribe(uint64_t node_id, AromaEventType event_type,
                          AromaEventHandler handler, void* user_data, uint32_t priority);

bool aroma_event_unsubscribe(uint64_t node_id, AromaEventType event_type,
                            AromaEventHandler handler);

AromaEvent* aroma_event_create_mouse(AromaEventType event_type, uint64_t target_node_id,
                                    int x, int y, uint8_t button);

AromaEvent* aroma_event_create_key(AromaEventType event_type, uint64_t target_node_id,
                                   uint32_t key_code, uint16_t modifiers);

AromaEvent* aroma_event_create_custom(uint64_t target_node_id, uint32_t custom_type,
                                      void* data, void (*free_func)(void*));

void aroma_event_destroy(AromaEvent* event);

void aroma_event_consume(AromaEvent* event);

AromaNode* aroma_event_hit_test(AromaNode* root, int x, int y);

const char* aroma_event_type_name(AromaEventType event_type);
#ifdef __cplusplus
}
#endif
#endif
