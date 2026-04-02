with open('/home/yassine/AromaUI/src/widgets/aroma_map.c', 'r') as f:
    text = f.read()

import re

new_handler = """static bool __map_event_handler(AromaEvent* event, void* user_data) {
    if (!event || !event->target_node || !event->target_node->node_widget_ptr) return false;
    AromaMap* map = (AromaMap*)event->target_node->node_widget_ptr;

    switch (event->event_type) {
        case EVENT_TYPE_MOUSE_DOUBLE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                map->zoom *= 2.0;
                if (map->zoom > 10.0) map->zoom = 10.0;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;

        case EVENT_TYPE_MOUSE_CLICK:
            if (event->data.mouse.x >= map->rect.x && event->data.mouse.x <= map->rect.x + map->rect.width &&
                event->data.mouse.y >= map->rect.y && event->data.mouse.y <= map->rect.y + map->rect.height) {
                if (event->data.mouse.button == 1) { // right click
                    map->zoom *= 0.5;
                    if (map->zoom < 0.25) map->zoom = 0.25;
                } else {
                    map->is_dragging = true;
                    map->last_mouse_x = event->data.mouse.x;
                    map->last_mouse_y = event->data.mouse.y;
                }
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
            
        case EVENT_TYPE_MOUSE_MOVE:
            if (map->is_dragging) {
                int dx = event->data.mouse.x - map->last_mouse_x;
                int dy = event->data.mouse.y - map->last_mouse_y;
                
                map->offset_x += dx;
                map->offset_y += dy;
                
                map->last_mouse_x = event->data.mouse.x;
                map->last_mouse_y = event->data.mouse.y;
                
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;
            
        case EVENT_TYPE_MOUSE_RELEASE:
            if (map->is_dragging) {
                map->is_dragging = false;
                aroma_node_invalidate(event->target_node);
                return true;
            }
            break;

        default:
            break;
    }
    return false;
}"""

# Replace the handler using regex
start_idx = text.find('static bool __map_event_handler')
end_idx = text.find('static void __map_draw')

text = text[:start_idx] + new_handler + '\n\n' + text[end_idx:]

with open('/home/yassine/AromaUI/src/widgets/aroma_map.c', 'w') as f:
    f.write(text)
