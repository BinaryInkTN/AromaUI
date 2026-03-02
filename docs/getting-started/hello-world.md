

Below is a minimal AromaUI example:
```c
#include "aroma.h"

int main(int argc, char** argv)
{
    if (!aroma_ui_init()) {
        return 1;
    }

    AromaTheme theme = aroma_theme_create_material_black();
    aroma_ui_set_theme(&theme);

    AromaWindow* win = aroma_ui_create_window("Hello World!", 400, 800);
    aroma_window_set_fullscreen((AromaNode*)win, true);


    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(win);
    }

    aroma_ui_shutdown();
    return 0;
}
```

For Android support, include the native entry point:

```c
#ifdef __ANDROID__
#include <android_native_app_glue.h>
void android_main(struct android_app* state)
{
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif
```

## Explanation of the Code
- `aroma_ui_init()`: Initializes the AromaUI system. Must be called before any other UI functions.
- `aroma_theme_create_material_black()`: Creates a Material Design black theme.
- `aroma_ui_set_theme()`: Sets the current theme for the UI.
- `aroma_ui_create_window()`: Creates a new window with the specified title and dimensions
- `aroma_window_set_fullscreen()`: Sets the window to fullscreen mode.
- `aroma_ui_is_running()`: Checks if the UI is still running (i.e the window is open).
- `aroma_ui_process_events()`: Processes input events (e.g. keyboard, mouse, touch).
- `aroma_ui_render()`: Renders the UI to the specified window.
- `aroma_ui_shutdown()`: Cleans up and shuts down the AromaUI system.    

<b> Window dimensions and title are both ignored on Android, as the app will always inherit screen dimensions and use the app name from the manifest. </b>
