with open("src/widgets/aroma_container.c", "r") as f:
    text = f.read()

# For EVENT_TYPE_TOUCH_DOWN:
td_patt = "    case EVENT_TYPE_TOUCH_DOWN:\\n    {\\n        int tx = event->data.touch.x;\\n        int ty = event->data.touch.y;"
td_repl = """    case EVENT_TYPE_MOUSE_CLICK:
    case EVENT_TYPE_TOUCH_DOWN:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_CLICK) {
            if (event->data.mouse.clicks == 0) return false;
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }"""
text = text.replace(td_patt, td_repl)

# For EVENT_TYPE_TOUCH_MOVE:
tm_patt = "    case EVENT_TYPE_TOUCH_MOVE:\\n    {\\n        int tx = event->data.touch.x;\\n        int ty = event->data.touch.y;"
tm_repl = """    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_TOUCH_MOVE:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_MOVE) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
        
        if (event->event_type == EVENT_TYPE_MOUSE_MOVE && !c->is_dragging && event->data.mouse.button == 0 /* no button down? */) {
            // It might be a scroll wheel event. The original logic was:
            if (!point_in_rect(tx, ty, &c->rect)) return false;
            int dx = event->data.mouse.delta_x;
            int dy = event->data.mouse.delta_y;
            if (dx == 0 && dy == 0) return false;
            
            float old_sx = c->scroll_fx;
            float old_sy = c->scroll_fy;
            if (can_scroll_v) c->scroll_fy += (float)dy * c->scroll_speed;
            if (can_scroll_h) c->scroll_fx += (float)dx * c->scroll_speed;
            clamp_scroll(c);
            
            if (c->scroll_fx != old_sx || c->scroll_fy != old_sy) {
                c->content_dirty = true;
                c->last_scroll_time = aroma_time_now_ms();
                c->scrollbar_opacity = 1.0f;
                ensure_animation_timer(c);
                aroma_node_invalidate(node);
            }
            return true;
        }"""
text = text.replace(tm_patt, tm_repl)


# For EVENT_TYPE_TOUCH_UP:
tu_patt = "    case EVENT_TYPE_TOUCH_UP:\\n    {\\n        int tx = event->data.touch.x;\\n        int ty = event->data.touch.y;"
tu_repl = """    case EVENT_TYPE_MOUSE_RELEASE:
    case EVENT_TYPE_TOUCH_UP:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_RELEASE) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }"""
text = text.replace(tu_patt, tu_repl)

text = text.replace("event->data.touch.id", "id")

import re
text = re.sub(r"    case EVENT_TYPE_MOUSE_MOVE:\n    \{\n        int mx = event->data\.mouse\.x;.*?return true;\n    \}", "", text, flags=re.DOTALL)


with open("src/widgets/aroma_container.c", "w") as f:
    f.write(text)

