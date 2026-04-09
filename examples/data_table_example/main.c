#include <aroma.h>
#include <unistd.h>

int main()
{
    aroma_ui_init();
    AromaTheme theme = aroma_theme_create_material_blue_dark();
    aroma_ui_set_theme(&theme);
    // Make the window smaller
    AromaWindow *window = aroma_ui_create_window("Data Table Example", 480, 320);

    AromaFont *font = aroma_font_create_from_memory(
        aroma_ubuntu_ttf,
        aroma_ubuntu_ttf_len,
        16);
    AromaNode *history_table = aroma_ui_table(window, 0, 0, 480, 320, 3, NULL, NULL);
    if (history_table) {
        aroma_node_set_flex_grow(history_table->parent_node, 1.0f); // Grow the scroll container

        aroma_table_set_col_width(history_table, 0, 180);
        aroma_table_set_col_width(history_table, 1, 200);
        aroma_table_set_col_width(history_table, 2, 100);

        aroma_table_set_header(history_table, 0, "Name");
        aroma_table_set_header(history_table, 1, "Number");
        aroma_table_set_header(history_table, 2, "Type");

        for (int i = 0; i < 50; ++i) {
            aroma_table_add_row(history_table);
            char name[32];
            char number[32];
            snprintf(name, sizeof(name), "Contact %d", i + 1);
            snprintf(number, sizeof(number), "+1 555 %04d", 1000 + i);
            aroma_table_set_cell_text(history_table, i, 0, name);
            aroma_table_set_cell_text(history_table, i, 1, number);
            if (i % 3 == 0) {
                aroma_table_set_cell_text(history_table, i, 2, "Mobile");
            } else if (i % 3 == 1) {
                aroma_table_set_cell_text(history_table, i, 2, "Home");
            } else {
                aroma_table_set_cell_text(history_table, i, 2, "Work");
            }
        }

        aroma_table_set_font(history_table, font);
    }
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
        usleep(16000); // ~60 FPS
    }

    aroma_ui_destroy_window(window);
    aroma_ui_shutdown();
}