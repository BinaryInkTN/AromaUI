with open("src/widgets/aroma_map.c", "r") as f:
    text = f.read()

import re
text = re.sub(r"static void __map_anim_tick.*?aroma_node_invalidate\(extra->node_ptr\);\n    }\n}\n\n", "", text, flags=re.DOTALL)

tick_func = """
static void __map_anim_tick(void* user_data) {
    if (!user_data) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)user_data;
    if (!extra->node_ptr || !extra->node_ptr->node_widget_ptr) return;
    AromaMap* map = (AromaMap*)extra->node_ptr->node_widget_ptr;
    
    bool changed = false;

    if (fabs(extra->display_zoom - extra->zoom) > 0.001) {
        extra->display_zoom += (extra->zoom - extra->display_zoom) * 0.15;
        changed = true;
    } else {
        extra->display_zoom = extra->zoom;
    }

    if (!map->is_dragging) {
        if (fabs(extra->velocity_x) > 0.1 || fabs(extra->velocity_y) > 0.1) {
            extra->center_px_x += extra->velocity_x;
            extra->center_px_y += extra->velocity_y;
            extra->velocity_x *= 0.90;
            extra->velocity_y *= 0.90;
            changed = true;
        } else {
            extra->velocity_x = 0;
            extra->velocity_y = 0;
        }
    } else {
        extra->velocity_x = 0;
        extra->velocity_y = 0;
    }

    double diff_x = extra->center_px_x - extra->display_px_x;
    double diff_y = extra->center_px_y - extra->display_px_y;
    if (fabs(diff_x) > 0.1 || fabs(diff_y) > 0.1) {
        extra->display_px_x += diff_x * 0.4;
        extra->display_px_y += diff_y * 0.4;
        changed = true;
    } else {
        extra->display_px_x = extra->center_px_x;
        extra->display_px_y = extra->center_px_y;
    }

    if (changed) {
        aroma_node_invalidate(extra->node_ptr);
    }
}
"""

text = text.replace("static void unload_old_zoom_tiles(struct AromaMapExtra* extra) {", tick_func + "\nstatic void unload_old_zoom_tiles(struct AromaMapExtra* extra) {")

with open("src/widgets/aroma_map.c", "w") as f:
    f.write(text)
