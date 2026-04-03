import re

with open("examples/car_infotainment/main.c", "r") as f:
    main_text = f.read()

# Add queue_voice_navigation to main.c
nav_vars_decl = """static char voice_nav_dest[128] = {0};
static bool voice_nav_trigger = false;

void queue_voice_navigation(const char* dest) {
    pthread_mutex_lock(&voice_mutex);
    strncpy(voice_nav_dest, dest, sizeof(voice_nav_dest)-1);
    voice_nav_trigger = true;
    pthread_mutex_unlock(&voice_mutex);
}"""

main_text = main_text.replace("static int voice_partial_timeout = 0;", nav_vars_decl + "\nstatic int voice_partial_timeout = 0;")


# Inside main loop, handle voice_nav_trigger
nav_loop_code = """
        if (voice_nav_trigger) {
            voice_nav_trigger = false;
            // Fake coordinates for demo
            double lat = 37.7749, lon = -122.4194; // Default
            if (strstr(voice_nav_dest, "paris")) { lat = 48.8566; lon = 2.3522; }
            else if (strstr(voice_nav_dest, "london")) { lat = 51.5074; lon = -0.1278; }
            else if (strstr(voice_nav_dest, "new york")) { lat = 40.7128; lon = -74.0060; }
            else if (strstr(voice_nav_dest, "tokyo")) { lat = 35.6762; lon = 139.6503; }
            else if (strstr(voice_nav_dest, "berlin")) { lat = 52.5200; lon = 13.4050; }
            
            navigate_to_tab(4); // Switch to map tab
            // wait we need the actual_map node!
            // I'll grab map_root children[0] or we can just make actual_map static.
        }
"""
# Replace:
#         if (voice_target_tab != -1) {
with open("examples/car_infotainment/main.c", "w") as f:
    f.write(main_text)


with open("examples/car_infotainment/voice_control.c", "r") as f:
    vc_text = f.read()

vc_ext_decl = """extern void queue_voice_navigation(const char* dest);"""
vc_text = vc_text.replace("extern void queue_voice_music_action(int action);", "extern void queue_voice_music_action(int action);\n" + vc_ext_decl)

nav_code = """        } else if (strstr(text, "navigate to")) {
            char* dest = strstr(text, "navigate to") + 11;
            while(*dest == ' ') dest++;
            printf("Voice Intent: NAVIGATE TO %s\\n", dest);
            char msg[128];
            snprintf(msg, sizeof(msg), "Navigating to %s", dest);
            aroma_voice_speak(msg);
            queue_voice_navigation(dest);
            queue_voice_action(4, false, false, msg);
"""

vc_text = vc_text.replace("        } else if (strstr(text, \"call\") || strstr(text, \"dial\")) {", nav_code + "        } else if (strstr(text, \"call\") || strstr(text, \"dial\")) {", 1)

with open("examples/car_infotainment/voice_control.c", "w") as f:
    f.write(vc_text)

