<b> Author: AHMED ALI Mohamed Yassine </b>

<br/>

All widget helper functions return:

    AromaNode*

This represents the widget node inserted into the UI tree.

---

## 1. Button

Creates a standard clickable button.

    AromaNode* aroma_ui_button(
        AromaNode* parent,
        const char* text,
        int x, int y, int width, int height,
        bool (*on_click)(AromaNode*, void*),
        void* user_data,
        AromaFont* font
    );

Parameters:
- parent: Parent node
- text: Button label
- x, y: Position relative to parent
- width, height: Dimensions
- on_click: Click callback
- user_data: Passed to callback
- font: Optional font

---

## 2. Button with Icon

    AromaNode* aroma_ui_button_with_icon(
        AromaNode* parent,
        const char* text,
        int x, int y, int width, int height,
        bool (*on_click)(AromaNode*, void*),
        void* user_data,
        AromaFont* font,
        const char* icon_code,
        AromaFont* icon_font
    );

Adds an icon to a standard button.

---

## 3. Label

    AromaNode* aroma_ui_label(
        AromaNode* parent,
        const char* text,
        int x, int y,
        AromaLabelStyle style,
        AromaFont* font
    );

Displays styled text.

---

## 4. Container

    AromaNode* aroma_ui_container(
        AromaNode* parent,
        int x, int y, int width, int height,
        AromaLayoutMode layout_mode,
        AromaFlexDirection flex_dir,
        AromaJustifyContent justify,
        AromaAlignItems align
    );

Generic layout container supporting flex configuration.

---

## 5. Image

From file:

    AromaNode* aroma_ui_image(
        AromaNode* parent,
        const char* path,
        int x, int y, int width, int height
    );

From memory:

    AromaNode* aroma_ui_image_mem(
        AromaNode* parent,
        unsigned char* data,
        size_t len,
        int x, int y, int width, int height
    );

---

## 6. Icon

    AromaNode* aroma_ui_icon(
        AromaNode* parent,
        const char* icon_code,
        int x, int y, int size,
        uint32_t color,
        AromaFont* font
    );

Renders an icon using an icon font.

---

## 7. Checkbox

    AromaNode* aroma_ui_checkbox(
        AromaNode* parent,
        const char* label,
        int x, int y, int width, int height,
        void (*callback)(bool, void*),
        void* user_data,
        AromaFont* font
    );

---

## 8. Radio Button

    AromaNode* aroma_ui_radiobutton(
        AromaNode* parent,
        const char* label,
        int x, int y, int width, int height,
        int group_id,
        void (*callback)(void*),
        void* user_data,
        AromaFont* font
    );

Grouped selection control.

---

## 9. Switch

    AromaNode* aroma_ui_switch(
        AromaNode* parent,
        int x, int y, int width, int height,
        bool initial_state,
        bool (*on_change)(AromaNode*, void*),
        void* user_data
    );

Boolean toggle control.

---

## 10. Slider

    AromaNode* aroma_ui_slider(
        AromaNode* parent,
        int x, int y, int width, int height,
        int min, int max, int value,
        bool (*on_change)(AromaNode*, void*),
        void* user_data
    );

Numeric range selector.

---

## 11. Textbox

    AromaNode* aroma_ui_textbox(
        AromaNode* parent,
        int x, int y, int width, int height,
        const char* placeholder,
        bool (*on_text_changed)(AromaNode*, const char*, void*),
        void* user_data,
        AromaFont* font
    );

Text input field.

---

## 12. Floating Action Button

    AromaNode* aroma_ui_fab(
        AromaNode* parent,
        int x, int y,
        AromaFABSize size,
        const char* icon_text,
        void (*callback)(void*),
        void* user_data,
        AromaFont* font
    );

Circular primary action button.

---

## 13. Icon Button

    AromaNode* aroma_ui_iconbutton(
        AromaNode* parent,
        const char* icon,
        int x, int y,
        int size,
        AromaIconButtonVariant variant,
        void (*callback)(void*),
        void* user_data,
        AromaFont* font
    );

Button that displays only an icon.

---

## 14. Chip

    AromaNode* aroma_ui_chip(
        AromaNode* parent,
        const char* label,
        int x, int y,
        AromaChipType type,
        void (*callback)(void*),
        void* user_data,
        AromaFont* font
    );

With icon:

    AromaNode* aroma_ui_chip_with_icon(
        AromaNode* parent,
        const char* label,
        int x, int y,
        AromaChipType type,
        void (*callback)(void*),
        void* user_data,
        AromaFont* font,
        const char* icon_code,
        AromaFont* icon_font
    );

Compact selectable element.

---

## 15. Card

    AromaNode* aroma_ui_card(
        AromaNode* parent,
        int x, int y, int width, int height,
        AromaCardType type
    );

Surface container with elevation or outline.

---

## 16. Progress Bar

    AromaNode* aroma_ui_progressbar(
        AromaNode* parent,
        int x, int y, int width, int height,
        AromaProgressType type,
        float progress
    );

Determinate or indeterminate progress indicator.

---

## 17. Divider

    AromaNode* aroma_ui_divider(
        AromaNode* parent,
        int x, int y, int length,
        AromaDividerOrientation orientation
    );

Horizontal or vertical separator.

---

## 18. List View

    AromaNode* aroma_ui_listview(
        AromaNode* parent,
        int x, int y, int width, int height,
        void (*callback)(int, void*),
        void* user_data,
        AromaFont* font
    );

Scrollable list container.

---

## 19. Menu

    AromaNode* aroma_ui_menu(
        AromaNode* parent,
        int x, int y,
        AromaFont* font
    );

Popup or contextual menu.

---

## 20. Dialog

    AromaNode* aroma_ui_dialog(
        AromaNode* parent,
        const char* title,
        const char* message,
        int width, int height,
        AromaDialogType type,
        AromaFont* font
    );

Modal dialog component.

---

## 21. Tabs

    AromaNode* aroma_ui_tabs(
        AromaNode* parent,
        int x, int y, int width, int height,
        const char** labels, int count,
        void (*on_change)(AromaNode*, int, void*),
        void* user_data,
        AromaFont* font
    );

With icons:

    AromaNode* aroma_ui_tabs_with_icons(
        AromaNode* parent,
        int x, int y, int width, int height,
        const char** labels,
        const char** icons,
        int count,
        void (*on_change)(AromaNode*, int, void*),
        void* user_data,
        AromaFont* font,
        AromaFont* icon_font
    );

---

## 22. Snackbar

    AromaNode* aroma_ui_snackbar(
        AromaNode* parent,
        const char* message,
        int duration_ms,
        AromaFont* font
    );

Temporary bottom notification.

---

## 23. Tooltip

    AromaNode* aroma_ui_tooltip(
        AromaNode* parent,
        const char* text,
        int x, int y,
        AromaTooltipPosition pos,
        AromaFont* font
    );

Hover hint element.

---

## 24. Sidebar

    AromaNode* aroma_ui_sidebar(
        AromaNode* parent,
        int x, int y, int width, int height,
        const char** labels, int count,
        void (*on_select)(AromaNode*, int, void*),
        void* user_data,
        AromaFont* font
    );

With icons:

    AromaNode* aroma_ui_sidebar_with_icons(
        AromaNode* parent,
        int x, int y, int width, int height,
        const char** labels,
        const char** icons,
        int count,
        void (*on_select)(AromaNode*, int, void*),
        void* user_data,
        AromaFont* font,
        AromaFont* icon_font
    );

---

## 25. Dropdown

    AromaNode* aroma_ui_dropdown(
        AromaNode* parent,
        int x, int y, int width, int height,
        char** options,
        int option_count,
        void (*on_selection_changed)(int, const char*, void*),
        void* user_data,
        AromaFont* font
    );

Selectable list popup.

---

## 26. Debug Overlay

    AromaNode* aroma_ui_debug_overlay(
        AromaNode* parent,
        int x, int y, int width,
        AromaFont* font
    );

Displays runtime diagnostics.

---

## 27. Window Node

    AromaNode* aroma_ui_window(
        const char* title,
        int width, int height,
        bool fullscreen
    );

Creates a top level window node.