with open("examples/car_infotainment/main.c", "r") as f:
    text = f.read()

# Add map_root
if 'AromaNode *map_root;' not in text:
    text = text.replace('    AromaNode *settings_root;', '    AromaNode *settings_root;\n    AromaNode *map_root;')

# Update tabs creation
old_tabs_4 = """                                          (const char *[]){"Main Screen", "Music", "Phone", "Settings"},
                                          (const char *[]){AROMA_ICON_DASHBOARD, AROMA_ICON_MUSIC_NOTE, AROMA_ICON_PHONE, AROMA_ICON_SETTINGS},
                                          4"""
new_tabs_5 = """                                          (const char *[]){"Main Screen", "Music", "Phone", "Settings", "Map"},
                                          (const char *[]){AROMA_ICON_DASHBOARD, AROMA_ICON_MUSIC_NOTE, AROMA_ICON_PHONE, AROMA_ICON_SETTINGS, AROMA_ICON_MAP},
                                          5"""

text = text.replace(old_tabs_4, new_tabs_5)

# Insert map content
old_assign = """    aroma_tabs_set_content(state.tabs, 3, &state.settings_root, 1);"""
new_assign = """    aroma_tabs_set_content(state.tabs, 3, &state.settings_root, 1);
    
    state.map_root = (AromaNode *)aroma_map_create((AromaNode *)state.window, 0, 70, WIN_W, WIN_H - 150);
    aroma_tabs_set_content(state.tabs, 4, &state.map_root, 1);"""
    
text = text.replace(old_assign, new_assign)

with open("examples/car_infotainment/main.c", "w") as f:
    f.write(text)
