import re

with open("src/backends/platforms/aroma_platform_glps.c", "r") as f:
    text = f.read()

# Replace the current glps_scroll_callback body
pattern = r"static void glps_scroll_callback.*?\{.*?AromaEvent \*ev = aroma_event_create_scroll.*?if \(ev\) aroma_event_queue\(ev\);\n\}"
replacement = """static void glps_scroll_callback(size_t window_id, GLPS_SCROLL_AXES axe,
                                 GLPS_SCROLL_SOURCE source, double value,
                                 int discrete, bool is_stopped, void *data)
{
    if (value == 0) return;
    
    float scroll_x = (axe == GLPS_SCROLL_H_AXIS) ? (float)value : 0.0f;
    float scroll_y = (axe == GLPS_SCROLL_V_AXIS) ? (float)value : 0.0f;
    
    int mx = (int)platform_ctx.last_mouse_x;
    int my = (int)platform_ctx.last_mouse_y;
    
    AromaNode *root = aroma_event_get_root();
    if (!root) return;
    
    AromaNode *target = aroma_event_hit_test(root, mx, my);
    uint64_t node_id = target ? target->node_id : root->node_id;
    
    AromaEvent *ev = aroma_event_create_scroll(node_id, mx, my, scroll_x, scroll_y);
    if (ev) aroma_event_queue(ev);
}"""

text = re.sub(pattern, replacement, text, flags=re.DOTALL)

with open("src/backends/platforms/aroma_platform_glps.c", "w") as f:
    f.write(text)

