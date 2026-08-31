#include "include/aroma_ui.h"
#include <stdio.h>

int main() {
    printf("Before init: g_ui_initialized=%d\n", g_ui_initialized);
    bool ret = aroma_ui_init();
    printf("After init: ret=%d, g_ui_initialized=%d\n", ret, g_ui_initialized);
    return 0;
}
