#include "shm_reader.h"
#include <stdio.h>
#include <unistd.h>
#include <aroma.h>


int main ()
{

    aroma_ui_init();
    AromaFont *text_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 100);
    AromaFont *small_font = aroma_font_create_from_memory(aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, 28);
    AromaWindow *window = aroma_ui_create_window("Telemetry Dashboard", 1024, 600);
    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);
    shm_reader_t *reader = shm_reader_init("/sdv_telemetry_shm");
    AromaNode* label_speed_header = aroma_ui_label((AromaNode*)window, "Speed", 50, 180, LABEL_STYLE_LABEL_LARGE, small_font);
    AromaNode* label_speed = aroma_ui_label((AromaNode*)window, "60", 50, 200, LABEL_STYLE_LABEL_LARGE, text_font);
    AromaNode* left_divider = aroma_ui_divider((AromaNode*)window, 300, 100, 450, DIVIDER_ORIENTATION_VERTICAL );
    AromaNode* right_divider = aroma_ui_divider((AromaNode*)window, 700, 100, 450, DIVIDER_ORIENTATION_VERTICAL );
    while (true) {
        telemetry_state_t state;
        if (shm_reader_get_state(reader, &state)) {
            printf("Got new telemetry state: seq=%u, speed=%.2f km/h\n",
                   state.seq, state.speed_kmh);
            char speed_text[16];
            snprintf(speed_text, sizeof(speed_text), "%d", (int)state.speed_kmh);
            aroma_label_set_text(label_speed, speed_text);

        }
        // workaround
        aroma_node_invalidate((AromaNode*)window);

        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(100000); 
    }
}