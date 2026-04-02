import re

with open("src/widgets/aroma_map.c", "r") as f:
    text = f.read()

handler = """static bool __map_event_handler(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node || !event->target_node->node_widget_ptr) return false;
    AromaMap* map = (AromaMap*)event->target_node->node_widget_ptr;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra) return false;

    LOG_INFO("MAP EVENT %d (Drag: %d, x: %d y: %d rx: %d ry: %d rw: %d rh: %d)", 
        event->event_type, map->is_dragging, event->data.mouse.x, event->data.mouse.y, map->rect.x, map->rect.y, map->rect.width, map->rect.height);
    
    switch (event->event_type) {"""

text = re.sub(r'static bool __map_event_handler.*?switch \(event->event_type\) \{', handler, text, flags=re.DOTALL)

click_logic = """        case EVENT_TYPE_MOUSE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                map->is_dragging = true;
                map->last_mouse_x = event->data.mouse.x;
                map->last_mouse_y = event->data.mouse.y;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;"""

text = re.sub(r'        case EVENT_TYPE_MOUSE_CLICK:.*?break;', click_logic, text, flags=re.DOTALL)

with open("src/widgets/aroma_map.c", "w") as f:
    f.write(text)
