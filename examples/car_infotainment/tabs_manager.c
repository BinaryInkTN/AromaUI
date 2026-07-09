#include "tabs_manager.h"
#include "app_state.h"

void build_tabs(void)
{
    state.tabs = aroma_ui_tabs_with_icons(
        (AromaNode *)state.window, 0, WIN_H - 80, WIN_W, 80,
        (const char *[]){"Vehicle View", "Settings"},
        (const char *[]){AROMA_ICON_VISIBILITY, AROMA_ICON_SETTINGS},
        2, NULL, NULL, state.ui_font, state.tab_font);
    
    if (state.tabs) {
        aroma_node_set_z_index(state.tabs, Z_LAYER_MAP_BUTTON);

        aroma_tabs_set_content(state.tabs, 0, (AromaNode **)&state.vehicle_view_root, 1);
        aroma_tabs_set_content(state.tabs, 1, &state.settings_panel_node, 1);
    }
}

void navigate_to_tab(int index)
{
    if (state.tabs) {
        aroma_tabs_set_selected(state.tabs, index);
    }
}