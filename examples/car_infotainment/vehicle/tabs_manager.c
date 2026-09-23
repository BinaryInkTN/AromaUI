#include "tabs_manager.h"
#include "app_state.h"
#include "aroma_animation.h"
#include "voice_handler.h"

#define TAB_CATEGORY_VEHICLE 0
#define TAB_CATEGORY_SETTINGS 1
#define TAB_CATEGORY_MEDIA 2
#define TAB_CATEGORY_NAVIGATION 3
#define TAB_CATEGORY_PHONE 4
#define TAB_CATEGORY_CLIMATE 5

void build_tabs(void)
{

    state.tabs = aroma_ui_tabs_with_icons(
        (AromaNode *)state.window, 0, WIN_H - 80, WIN_W, 80,
        (const char *[]){"Vehicle", "Settings", "Media", "Navigation", "Phone", "Climate"},
        (const char *[]){AROMA_ICON_VISIBILITY, AROMA_ICON_SETTINGS, AROMA_ICON_EDIT, AROMA_ICON_NAVIGATION, AROMA_ICON_BRIGHTNESS_1, AROMA_ICON_EDIT},
        6, NULL, NULL, state.ui_font, state.tab_font);

    if (state.tabs) {
        aroma_node_set_z_index(state.tabs, Z_LAYER_MAP_BUTTON);

        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_VEHICLE, (AromaNode **)&state.vehicle_view_root, 1);
        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_SETTINGS, &state.settings_panel_node, 1);
        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_MEDIA, &state.music_content_card, 1);
        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_NAVIGATION, &state.nav_content, 1);
        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_PHONE, &state.phone_content_card, 1);
        aroma_tabs_set_content(state.tabs, TAB_CATEGORY_CLIMATE, &state.ac_content, 1);
    }
}

void navigate_to_tab(int index)
{
    if (state.tabs) {
        aroma_tabs_set_selected(state.tabs, index);
    }
}

void setup_grid_view(void)
{
    if (!state.grid_container) {
        state.grid_container = aroma_ui_container(
            (AromaNode *)state.window,
            WIN_W / 4, WIN_H / 4, WIN_W / 2, WIN_H / 2,
            AROMA_LAYOUT_MODE_GRID, AROMA_FLEX_ROW, AROMA_JUSTIFY_SPACE_BETWEEN, AROMA_ALIGN_STRETCH);
        aroma_node_set_z_index(state.grid_container, Z_LAYER_MAP_PANEL);
    }

    if (state.grid_items_count < 6) {
        state.grid_items[state.grid_items_count++] = state.music_app_icon;
        state.grid_items[state.grid_items_count++] = state.phone_app_icon;
        state.grid_items[state.grid_items_count++] = state.settings_icon;
        state.grid_items[state.grid_items_count++] = state.navigation_icon;
        state.grid_items[state.grid_items_count++] = state.climate_icon;
        state.grid_items[state.grid_items_count++] = state.vehicle_view_icon;
    }

    for (int i = 0; i < state.grid_items_count; i++) {
        AromaNode *item = state.grid_items[i];
        if (item) {
            AromaRect *rect = aroma_node_get_rect(item);
            if (rect) {
                int cols = 3;
                int row = i / cols;
                int col = i % cols;
                int gap = 10;
                int cell_width = (WIN_W / 3) - gap;
                int cell_height = 80;
                rect->x = col * (cell_width + gap) + gap;
                rect->y = row * (cell_height + gap) + gap + WIN_H / 4;
                rect->width = cell_width;
                rect->height = cell_height;
                aroma_node_invalidate(item);
            }
        }
    }
}
