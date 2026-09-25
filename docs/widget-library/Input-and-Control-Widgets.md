
Interactive widgets: buttons, checkboxes, switches, sliders, textboxes, and chips.

## Buttons

```c
static bool on_button_click(AromaNode *node, void *ud) {
    (void)node;
    (void)ud;
    /* handle click */
    return true;
}

AromaNode *btn = aroma_ui_button(root, "Click Me", 20, 20, 160, 48,
                                 on_button_click, NULL, NULL);
```

Variants: `standard`, `filled`, `tonal`, `outlined`

## Icon Button

```c
AromaNode *ib = aroma_ui_icon_button(root, AROMA_ICON_SETTINGS, 300, 20, 48, 48);
```

## Checkbox

```c
static void on_checkbox_toggle(bool checked, void *ud) {
    (void)checked;
    (void)ud;
    /* handle toggle */
}

AromaNode *cb = aroma_ui_checkbox(root, "Enable notifications", 20, 100, 300, 32,
                                  on_checkbox_toggle, NULL, NULL);
```

## Radio Button

```c
#define SETTINGS_GROUP 1

AromaNode *rb1 = aroma_ui_radiobutton(root, "Option A", 20, 140, 200, 32,
                                      SETTINGS_GROUP, NULL, NULL, NULL);
AromaNode *rb2 = aroma_ui_radiobutton(root, "Option B", 20, 180, 200, 32,
                                      SETTINGS_GROUP, NULL, NULL, NULL);
```

Radio buttons in the same group are mutually exclusive.

## Switch

```c
static bool on_switch_toggle(AromaNode *node, void *ud) {
    (void)node;
    (void)ud;
    /* handle toggle */
    return true;
}

AromaNode *sw = aroma_ui_switch(root, 20, 220, 56, 28, true,
                                on_switch_toggle, NULL);
```

## Slider

```c
static bool on_slider_change(AromaNode *node, void *ud) {
    (void)node;
    (void)ud;
    /* handle value change */
    return true;
}

AromaNode *slider = aroma_ui_slider(root, 20, 280, 260, 32, 0, 100, 50,
                                    on_slider_change, NULL);
```

## Textbox

```c
static bool on_text_change(AromaNode *node, const char *text, void *ud) {
    (void)node;
    (void)text;
    (void)ud;
    /* handle text change */
    return true;
}

AromaNode *tb = aroma_ui_textbox(root, 20, 340, 260, 48, "Enter name...",
                                 on_text_change, NULL, NULL);
```

## Chip

```c
AromaNode *chip = aroma_ui_chip(root, "Filter", 20, 400, 100, 36, CHIP_TYPE_FILTER);
aroma_chip_set_selected(chip, true);
```

| Type | Use Case |
|---|---|
| `CHIP_TYPE_ACTION` | Single action |
| `CHIP_TYPE_CHOICE` | Selection from options |
| `CHIP_TYPE_FILTER` | Toggleable filter |
| `CHIP_TYPE_INPUT` | Text input chip |

## Live demo

```incense-demo inputs
```

```incense-demo buttons
```

```incense-demo slider
```

```incense-demo dropdown
```

```incense-demo toggles
```

## What's Next

- Learn [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Explore the [Map Widget](Map-Widget.md) for interactive maps.
- Try [Incense](Incense-Sandbox.md) for rapid prototyping.
