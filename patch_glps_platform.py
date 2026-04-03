import re

with open("src/backends/platforms/aroma_platform_glps.c", "r") as f:
    text = f.read()

callback = """
static void glps_scroll_callback(size_t window_id, GLPS_SCROLL_AXES axe,
                                 GLPS_SCROLL_SOURCE source, double value,
                                 int discrete, bool is_stopped, void *data)
{
    if (value == 0) return;
    
    // We assume aroma_event system keeps track of current mouse x,y 
    // or we can just send 0,0 and rely on the dispatcher to route to hovered node
    // Let's use get_mouse_state if we could, but aroma_event.c tracks it via g_mouse_state
    // Wait, the hovered node should be under mouse.
    
    float scroll_x = (axe == GLPS_SCROLL_H_AXIS) ? (float)value : 0.0f;
    float scroll_y = (axe == GLPS_SCROLL_V_AXIS) ? (float)value : 0.0f;
    
    // To properly target the element, we can send to ROOT and let the capture phase do hit-testing
    // Wait, AromaEvent sends to node_id. If 0, it dispatches and hit tests if it's hit-testable type ?
    // No, aroma_event_handle_pointer_move updates hover. We can inject a scroll event at root (id=0).
    AromaEvent *ev = aroma_event_create_scroll(0, -1, -1, scroll_x, scroll_y);
    if (ev) aroma_event_queue(ev);
}
"""

# Insert callback before init_platform
pattern_insert = r"(uint8_t init_platform\(void\))"
text = re.sub(pattern_insert, callback + r"\n\1", text)

# Register callback
pattern_reg = r"(glps_wm_set_mouse_click_callback\(platform_ctx\.wm, glps_mouse_click_callback, NULL\);)"
text = re.sub(pattern_reg, r"\1\n    glps_wm_set_scroll_callback(platform_ctx.wm, glps_scroll_callback, NULL);", text)

with open("src/backends/platforms/aroma_platform_glps.c", "w") as f:
    f.write(text)

