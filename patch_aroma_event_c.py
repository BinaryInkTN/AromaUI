import re

with open("src/core/aroma_event.c", "r") as f:
    text = f.read()

# Add aroma_event_create_scroll
func = """AromaEvent *aroma_event_create_scroll(uint64_t node_id, int x, int y, float scroll_x, float scroll_y)
{
    AromaEvent *ev = aroma_event_alloc();
    if (!ev)
        return NULL;
    ev->event_type = EVENT_TYPE_MOUSE_SCROLL;
    ev->target_node_id = node_id;
    ev->target_node = find_node_cached(node_id);
    ev->data.mouse.x = x;
    ev->data.mouse.y = y;
    ev->data.mouse.scroll_x = scroll_x;
    ev->data.mouse.scroll_y = scroll_y;
    clock_gettime(CLOCK_MONOTONIC, &ev->timestamp);
    return ev;
}
"""
text += "\n" + func

# Also let's add it to aroma_event_type_name
pattern = r'(case EVENT_TYPE_MOUSE_DOUBLE_CLICK:\s*return "MOUSE_DOUBLE_CLICK";)'
replacement = r'\1\n        case EVENT_TYPE_MOUSE_SCROLL: return "MOUSE_SCROLL";'
text = re.sub(pattern, replacement, text)

with open("src/core/aroma_event.c", "w") as f:
    f.write(text)

