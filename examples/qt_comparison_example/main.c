#include <aroma.h>

int main()
{
    aroma_ui_init();
    AromaWindow *window =  aroma_ui_create_window( "Aroma UI Example", 800, 600 );
    while (aroma_ui_is_running())
    {
        aroma_ui_process_events();
        aroma_ui_render(window);
    }
    aroma_ui_shutdown();
}