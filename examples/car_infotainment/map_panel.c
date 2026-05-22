#include "map_panel.h"
#include "app_state.h"
#include "aroma.h"
#include "aroma_animation.h"
#include "settings_ui.h"

static void map_zoom_in_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_in((AromaNode *)user_data);
}

static void map_zoom_out_cb(void *user_data)
{
    if (user_data) aroma_map_zoom_out((AromaNode *)user_data);
}

static void toggle_recent_card_cb(void *user_data) 
{ 
    (void)user_data; 
}

static void navigate_map(int index, void *user_data)
{
    AromaNode *map = (AromaNode *)user_data;
    if (!map) return;
    aroma_map_clear_route(map);

    typedef struct {
        double lat, lon;
        const char *start_label, *end_label;
        double end_lat, end_lon;
        int zoom;
    } Route;
    
    static const Route routes[] = {
        { 48.8566,  2.3522, "Start: Paris",       "Home: Versailles",  48.8049,  2.1204, 12 },
        { 51.5074, -0.1278, "Start: London",      "Work: Heathrow",    51.4700, -0.4543, 11 },
        { 52.5200, 13.4050, "Start: Berlin",      "Gym: BER Airport",  52.3667, 13.5033, 11 },
        { 41.9028, 12.4964, "Start: Colosseum",   "Supermarket: FCO",  41.7999, 12.2462, 12 },
        { 48.1351, 11.5820, "Start: Marienplatz", "Cafe: MUC Airport", 48.3537, 11.7861, 11 },
    };
    
    if (index < 0 || index >= (int)(sizeof(routes) / sizeof(routes[0])))
        return;

    const Route *r = &routes[index];
    aroma_map_pan_to(map, r->lat, r->lon);
    aroma_map_set_zoom(map, r->zoom);
    aroma_map_set_route(map, r->lat, r->lon, r->end_lat, r->end_lon, 0xFF35A8FE);
    aroma_map_add_popup_marker(map, r->lat,     r->lon,     0xFF00C853, r->start_label);
    aroma_map_add_popup_marker(map, r->end_lat, r->end_lon, 0xFFD50000, r->end_label);
}

void open_map_panel(void *user_data)
{
    (void)user_data;
    
    if (!state.map_panel || state.map_panel_open)
        return;

    if (state.settings_panel_open)
        close_settings_panel(NULL);

    if (state.map_overlay_background)
        aroma_node_set_hidden(state.map_overlay_background, false);

    aroma_node_set_hidden(state.map_panel, false);
    
    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 450);
    aroma_animation_start(state.recent_lv, AROMA_ANIM_SLIDE_X, WIN_W, MAP_PANEL_OFFSET, 350);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = true;
}

void close_map_panel(void *user_data)
{
    (void)user_data;
    
    if (!state.map_panel || !state.map_panel_open)
        return;

    if (state.map_overlay_background)
        aroma_node_set_hidden(state.map_overlay_background, true);

    AromaAnimation *map_anim = aroma_animation_start(
        state.map_panel, AROMA_ANIM_SLIDE_X, MAP_PANEL_OFFSET, WIN_W, 450);
    aroma_animation_set_easing(map_anim, AROMA_EASE_OUT_CUBIC);
    state.map_panel_open = false;
}

void build_map_panel(AromaNode *window)
{
    state.map_overlay_background = aroma_ui_container(
        window, 0, 0, WIN_W, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_ROW,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_overlay_background, Z_LAYER_MAP_PANEL - 1);
    aroma_node_set_hidden(state.map_overlay_background, true);

    state.map_panel = aroma_ui_container(
        window, MAP_PANEL_OFFSET, 0, MAP_PANEL_WIDTH, WIN_H,
        AROMA_LAYOUT_MODE_NONE, AROMA_FLEX_COLUMN,
        AROMA_JUSTIFY_START, AROMA_ALIGN_STRETCH);
    aroma_node_set_z_index(state.map_panel, Z_LAYER_MAP_PANEL);
    aroma_node_set_hidden(state.map_panel, true);
    state.map_panel_open = false;

    state.map_node = aroma_ui_map(state.map_panel, 0, 0, MAP_PANEL_WIDTH, WIN_H);
    aroma_node_set_z_index(state.map_node, Z_LAYER_MAP_PANEL);
    aroma_map_set_show_attribution(state.map_node, false);
    aroma_map_set_center(state.map_node, 48.8566, 2.3522);
    aroma_map_set_zoom(state.map_node, 12);
    aroma_map_set_route(state.map_node, 48.8566, 2.3522, 48.8049, 2.1204, 0xFF35A8FE);
    aroma_map_add_popup_marker(state.map_node, 48.8566, 2.3522, 0xFF00C853, "Start: Paris");
    aroma_map_add_popup_marker(state.map_node, 48.8049, 2.1204, 0xFFD50000, "Home: Versailles");
    aroma_map_add_icon_marker(state.map_node, 48.8606, 2.3376, 0xFFFFD600, AROMA_ICON_STAR);

    AromaNode *zoom_in = aroma_ui_iconbutton(
        state.map_panel, AROMA_ICON_ADD, 
        MAP_PANEL_WIDTH - 70, 120, 50, 
        ICON_BUTTON_FILLED, map_zoom_in_cb, (void *)state.map_node, state.icon_font);
    
    AromaNode *zoom_out = aroma_ui_iconbutton(
        state.map_panel, AROMA_ICON_REMOVE, 
        MAP_PANEL_WIDTH - 70, 180, 50, 
        ICON_BUTTON_FILLED, map_zoom_out_cb, (void *)state.map_node, state.icon_font);
    
    aroma_button_set_colors(zoom_in, 
        state.theme.colors.primary, state.theme.colors.primary, 
        state.theme.colors.secondary, state.theme.colors.text_primary);
    aroma_button_set_colors(zoom_out, 
        state.theme.colors.primary, state.theme.colors.primary, 
        state.theme.colors.secondary, state.theme.colors.text_primary);
    
    aroma_node_set_z_index(zoom_in, Z_LAYER_MAP_CONTROLS);
    aroma_node_set_z_index(zoom_out, Z_LAYER_MAP_CONTROLS);

    AromaNode *recent_card = aroma_ui_card(
        state.map_panel, WIN_W - 390, WIN_H - 450, 300, 280, CARD_TYPE_FILLED);
    aroma_node_set_z_index(recent_card, Z_LAYER_MAP_CONTROLS);

    AromaNode *recent_label = aroma_ui_label(
        recent_card, "Recently Visited", 20, 20, 
        LABEL_STYLE_LABEL_LARGE, state.ui_font);
    aroma_node_set_z_index(recent_label, Z_LAYER_MAP_CONTROLS + 1);

    AromaNode *recent_close = aroma_ui_iconbutton(
        recent_card, AROMA_ICON_CLOSE, 240, 10, 40, 
        ICON_BUTTON_OUTLINED, toggle_recent_card_cb, (void *)recent_card, state.icon_font);
    aroma_node_set_z_index(recent_close, Z_LAYER_MAP_CONTROLS + 1);
    aroma_node_set_hidden(recent_close, true);

    state.recent_lv = aroma_ui_listview(
        recent_card, 0, 60, 300, 200, 
        navigate_map, state.map_node, state.ui_font);
    aroma_listview_add_item(state.recent_lv, "Home", "123 Main St", NULL);
    aroma_listview_add_item(state.recent_lv, "Work", "456 Business Rd", NULL);
    aroma_listview_add_item(state.recent_lv, "Gym", "789 Fitness Ave", NULL);

    AromaNode *scroll_container = aroma_listview_get_scroll_container(state.recent_lv);
    aroma_node_set_z_index(scroll_container, Z_LAYER_MAP_CONTROLS + 1);

    AromaNode *close_map = aroma_ui_iconbutton(
        state.map_panel, AROMA_ICON_CLOSE, 20, 20, 50,
        ICON_BUTTON_FILLED, close_map_panel, NULL, state.icon_font);
    aroma_node_set_z_index(close_map, Z_LAYER_MAP_CLOSE);
}