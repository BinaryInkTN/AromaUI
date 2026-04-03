import re

with open("src/backends/platforms/aroma_platform_glps.c", "r") as f:
    text = f.read()

callback = """
static void glps_scroll_callback(size_t window_id, GLPS_SCROLL_AXES axe,
                                 GLPS_SCROLL_SOURCE source, double value,
                                 int discrete, bool is_stopped, void *data)
{
    if (value == 0) return;
    
    float scroll_x = (axe == GLPS_SCROLL_H_AXIS) ? (float)value : 0.0f;
    float scroll_y = (axe == GLPS_SCROLL_V_AXIS) ? (float)value : 0.0f;
    
    AromaEvent *ev = aroma_event_create_scroll(0, -1, -1, scroll_x, scroll_y);
    if (ev) aroma_event_queue(ev);
}
"""

# Insert callback before initialize
pattern_insert = r"(int initialize\(\))"
text = re.sub(pattern_insert, callback + r"\n\1", text)

# Register callback
pattern_reg = r"(glps_wm_set_mouse_click_callback\(platform_ctx\.wm, glps_mouse_click_callback, NULL\);)"
text = re.sub(pattern_reg, r"\1\n    glps_wm_set_scroll_callback(platform_ctx.wm, glps_scroll_callback, NULL);", text)

with open("src/backends/platforms/aroma_platform_glps.c", "w") as f:
    f.write(text)

