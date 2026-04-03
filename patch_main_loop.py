with open("examples/car_infotainment/main.c", "r") as f:
    text = f.read()

# Make actual_map a global variable
text = text.replace("AromaNode* actual_map = ", "actual_map = ")
text = "AromaNode* actual_map = NULL;\n" + text

nav_code = """
        if (voice_nav_trigger) {
            double lat = 37.7749, lon = -122.4194;
            if (strstr(voice_nav_dest, "paris")) { lat = 48.8566; lon = 2.3522; }
            else if (strstr(voice_nav_dest, "london")) { lat = 51.5074; lon = -0.1278; }
            else if (strstr(voice_nav_dest, "new york")) { lat = 40.7128; lon = -74.0060; }
            else if (strstr(voice_nav_dest, "tokyo")) { lat = 35.6762; lon = 139.6503; }
            else if (strstr(voice_nav_dest, "berlin")) { lat = 52.5200; lon = 13.4050; }
            
            if (actual_map) {
                aroma_map_set_zoom(actual_map, 10);
                aroma_map_pan_to(actual_map, lat, lon);
            }
            voice_nav_trigger = false;
        }
"""

text = text.replace("if (voice_target_tab != -1) {", nav_code + "        if (voice_target_tab != -1) {")

with open("examples/car_infotainment/main.c", "w") as f:
    f.write(text)
