import re

with open("src/widgets/aroma_map.c", "r") as f:
    text = f.read()

# We need to inject __map_anim_tick and update __map_draw
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

draw_search = r"static void __map_draw\(AromaNode\* node, size_t window_id\).*?gfx->draw_image\(window_id, draw_x, draw_y, TILE_SIZE, TILE_SIZE, found_tile->texture_id\);"
import textwrap

new_draw_text = """
    int z = (int)round(extra->display_zoom);
    if (z < 0) z = 0; if (z > 18) z = 18;
    double scale = pow(2.0, extra->display_zoom - z);
    
    double center_x = extra->display_px_x * pow(2.0, extra->display_zoom - extra->zoom);
    double center_y = extra->display_px_y * pow(2.0, extra->display_zoom - extra->zoom);

    double max_px = (double)((1 << z) * TILE_SIZE);
    
    double view_tl_x = center_x - map->rect.width / 2.0;
    double view_tl_y = center_y - map->rect.height / 2.0;
    
    double current_tile_size = TILE_SIZE * scale;
    int tx_start = (int)floor(view_tl_x / current_tile_size) - 1;
    int ty_start = (int)floor(view_tl_y / current_tile_size) - 1;
    int tx_end = (int)floor((view_tl_x + map->rect.width) / current_tile_size) + 1;
    int ty_end = (int)floor((view_tl_y + map->rect.height) / current_tile_size) + 1;

    for (int y = ty_start; y <= ty_end; y++) {
        for (int x = tx_start; x <= tx_end; x++) {
            
            if (y < 0 || y >= (1<<z)) continue;
            int wrapped_x = (x % (1<<z) + (1<<z)) % (1<<z); 

            char filepath[256];
            snprintf(filepath, sizeof(filepath), "%s/osm_%s_%d_%d_%d.png", TILE_CACHE_DIR, theme_is_dark ? "dark" : "light", z, wrapped_x, y);

            MapTile* found_tile = NULL;
            int oldest_idx = -1;
            uint64_t oldest_seq = UINT64_MAX;

            for (int i=0; i<MAX_TILES_MEM; i++) {
                if (extra->tiles[i].valid) {
                    if (extra->tiles[i].z == z && extra->tiles[i].x == wrapped_x && extra->tiles[i].y == y) {
                        found_tile = &extra->tiles[i];
                        break;
                    }
                    if (extra->tiles[i].access_seq < oldest_seq) {
                        oldest_seq = extra->tiles[i].access_seq;
                        oldest_idx = i;
                    }
                } else {
                    oldest_idx = i;
                    oldest_seq = 0;
                }
            }

            if (found_tile) {
                found_tile->access_seq = ++extra->access_counter;
            } else if (oldest_idx != -1) {
                found_tile = &extra->tiles[oldest_idx];
                if (found_tile->valid && found_tile->is_ready && found_tile->texture_id != 0 && gfx && gfx->unload_image) {
                    gfx->unload_image(found_tile->texture_id);
                }
                found_tile->valid = true;
                found_tile->z = z;
                found_tile->x = wrapped_x;
                found_tile->y = y;
                found_tile->is_loading = false;
                found_tile->is_ready = false;
                found_tile->access_seq = ++extra->access_counter;
                found_tile->texture_id = 0;
            }

            if (found_tile && !found_tile->is_ready) {
                if (access(filepath, F_OK) != -1) {
                    if (found_tile->is_loading) {
                        found_tile->is_loading = false;
                    }
                    if (gfx && gfx->load_image) {
                        found_tile->texture_id = gfx->load_image(filepath);
                        found_tile->is_ready = true;
                    }
                } else if (!found_tile->is_loading) {
                    found_tile->is_loading = true;
                    if (!request_tile_download(z, wrapped_x, y, theme_is_dark, filepath, node->node_id)) {
                        found_tile->is_loading = false;
                    }
                }
            }

            int draw_x = map->rect.x + (int)(x * current_tile_size - view_tl_x);
            int draw_y = map->rect.y + (int)(y * current_tile_size - view_tl_y);
            int draw_size = (int)(current_tile_size) + 1;

            if (found_tile && found_tile->is_ready && found_tile->texture_id != 0 && gfx && gfx->draw_image) {
                gfx->draw_image(window_id, draw_x, draw_y, draw_size, draw_size, found_tile->texture_id);
"""

res = re.sub(
    r"    int z = extra->zoom;.*?gfx->draw_image\(window_id, draw_x, draw_y, TILE_SIZE, TILE_SIZE, found_tile->texture_id\);", 
    new_draw_text, 
    text, 
    flags=re.DOTALL
)

res = tick_func + "\n" + res

with open("src/widgets/aroma_map.c", "w") as f:
    f.write(res)
