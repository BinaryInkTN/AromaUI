import re

with open("src/widgets/aroma_map.c", "r") as f:
    text = f.read()

# Update MapMarker struct
old_struct = """typedef struct {
    double lat;
    double lon;
    uint32_t color;
} MapMarker;"""

new_struct = """typedef struct {
    double lat;
    double lon;
    uint32_t color;
    const char* icon_code;
} MapMarker;"""

text = text.replace(old_struct, new_struct)


def marker_replacer(match):
    original = match.group(0)
    # We want to replace the `aroma_gfx_draw_circle` with conditional text drawing if icon_code is set
    new_code = """
        if (extra->markers[i].icon_code) {
            uint32_t shadow = 0xAA000000;
            // Draw symbol
            // Get font
            AromaFont* m_font = aroma_get_material_font(24);
            if (m_font) {
                // shadow
                aroma_gfx_render_text(m_font, extra->markers[i].icon_code, mx - 12 + 1, my - 12 + 1, shadow);
                aroma_gfx_render_text(m_font, extra->markers[i].icon_code, mx - 12, my - 12, extra->markers[i].color);
            }
        } else {
            aroma_gfx_draw_circle(mx, my, 8, extra->markers[i].color, true);
            aroma_gfx_draw_circle(mx, my, 8, 0xFFFFFFFF, false);
            aroma_gfx_draw_circle(mx, my, 9, 0x88000000, false);
        }
    """
    
    # Simple replace
    return re.sub(r"aroma_gfx_draw_circle\(mx, my, 8, extra->markers\[i\]\.color, true\);\s*aroma_gfx_draw_circle\(mx, my, 8, 0xFFFFFFFF, false\);\s*aroma_gfx_draw_circle\(mx, my, 9, 0x88000000, false\);", new_code, original)

text = re.sub(r"for\s*\(\w+\s*i\s*=\s*0;\s*i\s*<\s*extra->marker_count;\s*i\+\+\)\s*\{.*?(aroma_gfx_draw_circle.*?);.*?}", marker_replacer, text, flags=re.DOTALL)


# Add new function
add_func = """void aroma_map_add_marker(AromaNode* node, double lat, double lon, uint32_t color) {
    if (!node) return;
    AromaMap* map = (AromaMap*)node->widget_data;
    if (!map || !map->extra) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    
    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->markers[extra->marker_count].icon_code = NULL;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}

void aroma_map_add_icon_marker(AromaNode* node, double lat, double lon, uint32_t color, const char* icon_code) {
    if (!node) return;
    AromaMap* map = (AromaMap*)node->widget_data;
    if (!map || !map->extra) return;
    struct AromaMapExtra* extra = (struct AromaMapExtra*)map->extra;
    
    if (extra->marker_count < MAX_MARKERS) {
        extra->markers[extra->marker_count].lat = lat;
        extra->markers[extra->marker_count].lon = lon;
        extra->markers[extra->marker_count].color = color;
        extra->markers[extra->marker_count].icon_code = icon_code;
        extra->marker_count++;
        aroma_node_invalidate(node);
    }
}"""

old_add_func = r"void aroma_map_add_marker\(AromaNode\* node, double lat, double lon, uint32_t color\) \{.*?aroma_node_invalidate\(node\);\n\s*\}"

text = re.sub(old_add_func, add_func, text, flags=re.DOTALL)

with open("src/widgets/aroma_map.c", "w") as f:
    f.write(text)

