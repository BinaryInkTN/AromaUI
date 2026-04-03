with open("examples/car_infotainment/main.c", "r") as f:
    text = f.read()

text = text.replace('aroma_map_add_marker(state.map, 37.7749, -122.4194, 0xFFFF0000);',
                    'aroma_map_add_icon_marker(state.map, 37.7749, -122.4194, 0xFFFF0000, AROMA_ICON_PLACE);')

# Let's add multiple icon markers so there's not just one
text = text.replace('aroma_map_add_marker(state.map, 37.8049, -122.4094, 0xFF00FF00);',
                    'aroma_map_add_icon_marker(state.map, 37.8049, -122.4094, 0xFF00FF00, AROMA_ICON_LOCAL_CAFE);')

text = text.replace('aroma_map_add_marker(state.map, 37.7649, -122.4294, 0xFF0000FF);',
                    'aroma_map_add_icon_marker(state.map, 37.7649, -122.4294, 0xFF0000FF, AROMA_ICON_LOCAL_GAS_STATION);')


with open("examples/car_infotainment/main.c", "w") as f:
    f.write(text)
