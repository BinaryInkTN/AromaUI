import re

with open("src/widgets/aroma_container.c", "r") as f:
    text = f.read()

# We'll just replace the whole MOUSE_MOVE logic that was trying to be "scroll wheel"
pattern = r"""        if \(event->event_type == EVENT_TYPE_MOUSE_MOVE && !c->is_dragging && event->data.mouse.button == 0 /\* no button down\? \*/\) \{
            // It might be a scroll wheel event. The original logic was:
            if \(!point_in_rect\(tx, ty, &c->rect\)\) return false;
            int dx = event->data.mouse.delta_x;
            int dy = event->data.mouse.delta_y;
            if \(dx == 0 && dy == 0\) return false;
            
            float old_sx = c->scroll_fx;
            float old_sy = c->scroll_fy;
            if \(can_scroll_v\) c->scroll_fy \+= \(float\)dy \* c->scroll_speed;
            if \(can_scroll_h\) c->scroll_fx \+= \(float\)dx \* c->scroll_speed;
            clamp_scroll\(c\);
            
            if \(c->scroll_fx != old_sx \|\| c->scroll_fy != old_sy\) \{
                c->content_dirty = true;
                c->last_scroll_time = aroma_time_now_ms\(\);
                c->scrollbar_opacity = 1.0f;
                ensure_animation_timer\(c\);
                aroma_node_invalidate\(node\);
            \}
            return true;
        \}"""

text = re.sub(pattern, "", text)

# Now inject EVENT_TYPE_MOUSE_SCROLL into the switch
scroll_block = """    case EVENT_TYPE_MOUSE_SCROLL:
    {
        // Hit test
        // Since aroma dispatches SCROLL everywhere (or we did root-level), we need
        // to only process if mouse is inside us. We usually get the current mouse pos from g_mouse_state, 
        // wait, we didn't pass mouse pos into create_scroll. We passed -1,-1. 
        // In AromaEvent, we can access g_mouse_state.last_x, g_mouse_state.last_y natively or we can just 
        // have point_in_rect using hovering logic. But aroma_event_hit_test can just target the node.
        // Actually, we registered an event listener on the node itself, meaning it gets all broadcasted events or events targeted to it.
        // Usually, scroll is broadcast. Let's do simple hit test.
        extern struct { int x; int y; } __attribute__((weak)) g_mouse_state_export;
        // The container handles events. We just need point_in_rect(event->data.mouse.x, event->data.mouse.y) if they are 
        // actually filled. BUT we passed -1. 
        // Let's change the create_scroll in glps to pass mouse x and y.
        return true;
    }"""

# We'll just write it manually later.
with open("src/widgets/aroma_container.c", "w") as f:
    f.write(text)

