import re

with open("src/widgets/aroma_container.c", "r") as f:
    text = f.read()

scroll_block = """
    case EVENT_TYPE_MOUSE_SCROLL:
    {
        int tx = event->data.mouse.x;
        int ty = event->data.mouse.y;
        if (!point_in_rect(tx, ty, &c->rect)) return false;
        
        float sx = event->data.mouse.scroll_x;
        float sy = event->data.mouse.scroll_y;
        
        if (sx == 0.0f && sy == 0.0f) return false;
        
        float old_sx = c->scroll_fx;
        float old_sy = c->scroll_fy;
        
        // Reverse direction? Usually down scroll gives sy > 0 or < 0?
        // Wait, on most systems wheel down (scrolling page down) means negative delta or positive delta?
        // We'll multiply by some sensitivity factor, usually 50.0f
        
        if (can_scroll_v) c->scroll_fy += sy * 50.0f;
        if (can_scroll_h) c->scroll_fx += sx * 50.0f;
        
        clamp_scroll(c);
        
        if (c->scroll_fx != old_sx || c->scroll_fy != old_sy) {
            c->content_dirty = true;
            c->last_scroll_time = aroma_time_now_ms();
            c->scrollbar_opacity = 1.0f;
            ensure_animation_timer(c);
            aroma_node_invalidate(node);
        }
        return true;
    }
"""

pattern = r"(case EVENT_TYPE_MOUSE_MOVE:)"
text = re.sub(pattern, scroll_block.strip() + r"\n\n    \1", text)

with open("src/widgets/aroma_container.c", "w") as f:
    f.write(text)

