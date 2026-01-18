#ifndef AROMA_EVENT_H
#define AROMA_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct AromaNode AromaNode;
typedef struct AromaEvent AromaEvent;

#define AROMA_KEY_MOD_CAPSLOCK 0x0001u

typedef enum AromaEventType {
    EVENT_TYPE_MOUSE_MOVE,          
    EVENT_TYPE_MOUSE_CLICK,         
    EVENT_TYPE_MOUSE_RELEASE,       
    EVENT_TYPE_MOUSE_ENTER,         
    EVENT_TYPE_MOUSE_EXIT,          
    EVENT_TYPE_MOUSE_HOVER,         
    EVENT_TYPE_MOUSE_DOUBLE_CLICK,  
    EVENT_TYPE_KEY_PRESS,           
    EVENT_TYPE_KEY_RELEASE,         
    EVENT_TYPE_FOCUS_GAINED,        
    EVENT_TYPE_FOCUS_LOST,          
    EVENT_TYPE_CUSTOM,              
    EVENT_TYPE_COUNT                
} AromaEventType;

typedef struct {
    int x;                          
    int y;                          
    int delta_x;                    
    int delta_y;                    
    uint8_t button;                 
    uint8_t clicks;                 
} AromaMouseEventData;

typedef struct {
    uint32_t key_code;              
    uint32_t scan_code;             
    uint16_t modifiers;             
    bool repeat;                    
} AromaKeyEventData;

typedef struct {
    uint32_t custom_type;           
    void* data;                     
    void (*free_data)(void*);       
} AromaCustomEventData;

struct AromaEvent {
    AromaEventType event_type;      
    uint64_t target_node_id;        
    AromaNode* target_node;         
    struct timespec timestamp;      
    bool consumed;                  

    union {
        AromaMouseEventData mouse;
        AromaKeyEventData key;
        AromaCustomEventData custom;
    } data;                         
};

typedef bool (*AromaEventHandler)(AromaEvent* event, void* user_data);

typedef struct {
    AromaEventType event_type;      
    AromaEventHandler handler;      
    void* user_data;                
    uint32_t priority;              
} AromaEventListener;

bool aroma_event_system_init(void);

void aroma_event_system_shutdown(void);

void aroma_event_set_root(AromaNode* root);

AromaNode* aroma_event_get_root(void);

AromaEvent* aroma_event_create(AromaEventType event_type, uint64_t target_node_id);

bool aroma_event_dispatch(AromaEvent* event);

bool aroma_event_queue(AromaEvent* event);

void aroma_event_process_queue(void);

void aroma_event_handle_pointer_move(int x, int y, bool button_down);

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

#endif 

