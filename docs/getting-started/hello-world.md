This document explains a minimal AromaUI application written in C. It demonstrates initialization, layout creation, event handling, rendering, and cleanup, along with Android support.

---

## Overview

The application creates a fullscreen window containing:

* A centered label displaying a greeting message
* A button that updates the message when clicked

---

## Application Structure

### State Management

The application uses a structure to store UI elements and runtime data:

```c
typedef struct
{
    AromaNode *greeting_label;
    AromaNode *root_container;
    AromaNode *btn_click;
    char greeting_text[64];
    int click_count;
    AromaFont *title_font;
    AromaFont *button_font;
    AromaWindow *window;
} AppState;
```

#### Description

* `greeting_label`: Label displaying the current message
* `root_container`: Main layout container
* `btn_click`: Button widget
* `greeting_text`: Buffer holding the label text
* `click_count`: Tracks how many times the button was pressed
* `title_font`: Font used for the label
* `button_font`: Font used for the button
* `window`: Main application window

---

## Initialization

```c
if (!aroma_ui_init())
{
    printf("Failed to initialise AromaUI\n");
    return 1;
}
```

Initializes the AromaUI system. This must be called before any other UI operations.

---

## Theme Setup

```c
AromaTheme theme = aroma_theme_create_material_blue_dark();
aroma_ui_set_theme(&theme);
```

Creates and applies a Material Design dark theme.

---

## Window Creation

```c
state.window = aroma_ui_create_window(
    "AromaUI Hello World",
    aroma_android_dp_to_px(400),
    aroma_android_dp_to_px(600)
);
```

Creates the main window.

On Android:

* The title is ignored
* The window automatically uses the device screen size

Enable fullscreen:

```c
aroma_window_set_fullscreen((AromaNode *)state.window, true);
```

---

## Font Loading

```c
state.title_font = aroma_font_create_from_memory(
    aroma_ubuntu_ttf, aroma_ubuntu_ttf_len,
    aroma_android_sp_to_px(36)
);

state.button_font = aroma_font_create_from_memory(
    aroma_ubuntu_ttf, aroma_ubuntu_ttf_len,
    aroma_android_sp_to_px(20)
);
```

Loads fonts from memory using embedded font data.

---

## Layout System

```c
state.root_container = aroma_ui_container(
    (AromaNode *)state.window,
    0, 0, w, h,
    AROMA_LAYOUT_MODE_FLEX,
    AROMA_FLEX_COLUMN,
    AROMA_JUSTIFY_CENTER,
    AROMA_ALIGN_CENTER
);
```

Creates a flex container that:

* Arranges children vertically
* Centers them horizontally and vertically

Spacing between elements:

```c
aroma_node_set_gap(state.root_container, aroma_android_dp_to_px(32));
```

---

## Label Creation

```c
state.greeting_label = aroma_ui_label(
    state.root_container,
    state.greeting_text,
    0, 0,
    LABEL_STYLE_LABEL_LARGE,
    state.title_font
);
```

Displays the greeting text.

---

## Button Creation

```c
state.btn_click = aroma_ui_button(
    state.root_container,
    "Click me!",
    0, 0,
    btn_width,
    btn_height,
    on_click,
    &state,
    state.button_font
);
```

Creates a button and binds it to a click handler.

---

## Event Handling

```c
static bool on_click(AromaNode *btn, void *data)
{
    (void)btn;
    AppState *state = (AppState *)data;

    state->click_count++;
    snprintf(state->greeting_text, sizeof(state->greeting_text),
             "Hello, World! (%d)", state->click_count);

    aroma_label_set_text(state->greeting_label, state->greeting_text);
    return true;
}
```

Each click:

* Increments the counter
* Updates the label text

---

## Main Loop

```c
while (aroma_ui_is_running())
{
    aroma_ui_process_events();
    aroma_ui_render(state.window);
}
```

Handles:

* Input events (mouse, keyboard, touch)
* Rendering of the UI

---

## Cleanup

```c
if (state.btn_click)
    aroma_button_destroy(state.btn_click);

if (state.greeting_label)
    aroma_label_destroy(state.greeting_label);

if (state.root_container)
    aroma_container_destroy(state.root_container);

if (state.title_font)
    aroma_font_destroy(state.title_font);

if (state.button_font)
    aroma_font_destroy(state.button_font);

aroma_ui_destroy_window(state.window);
aroma_ui_shutdown();
```

Ensures all allocated resources are properly released.

---

## Android Support

To run on Android, include the native entry point:

```c
#ifdef __ANDROID__
#include <android_native_app_glue.h>

void android_main(struct android_app *state)
{
    aroma_android_set_app(state);
    main(0, NULL);
}
#endif
```

This connects AromaUI to the Android native activity lifecycle.

---

## Notes

* Window title and size are ignored on Android
* Density-independent units (`dp`, `sp`) ensure consistent UI scaling
* The layout system is based on flexbox principles
* Fonts are loaded from memory, allowing embedded assets

---

## Summary

This example demonstrates the core workflow of an AromaUI application:

1. Initialize the UI system
2. Set a theme
3. Create a window
4. Build a layout
5. Add widgets
6. Handle user interaction
7. Run the main loop
8. Clean up resources

This structure serves as a foundation for building more complex cross-platform GUI applications using AromaUI.
