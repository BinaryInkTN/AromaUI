
Interactive widgets: buttons, checkboxes, switches, sliders, textboxes, and chips.

## Buttons

```c
AromaNode *btn = aroma_ui_button(root, "Click Me", 20, 20, 160, 48);
aroma_button_set_on_click(btn, [](AromaNode *node, void *ud) {
    // handle click
}, NULL);
```

Variants: `standard`, `filled`, `tonal`, `outlined`

## Icon Button

```c
AromaNode *ib = aroma_ui_icon_button(root, AROMA_ICON_SETTINGS, 300, 20, 48, 48);
```

## Checkbox

```c
AromaNode *cb = aroma_ui_checkbox(root, "Enable notifications", 20, 100, 300, 32);
aroma_checkbox_set_on_change(cb, [](bool checked, void *ud) {
    // handle toggle
}, NULL);
```

## Radio Button

```c
AromaRadioGroup group;
aroma_radiobutton_group_init(&group);
AromaNode *rb1 = aroma_ui_radiobutton(root, "Option A", &group, 20, 140, 200, 32);
AromaNode *rb2 = aroma_ui_radiobutton(root, "Option B", &group, 20, 180, 200, 32);
```

Radio buttons in the same group are mutually exclusive.

## Switch

```c
AromaNode *sw = aroma_ui_switch(root, 20, 220, 56, 28, true);
aroma_switch_set_on_change(sw, [](bool value, void *ud) {
    // handle toggle
}, NULL);
```

## Slider

```c
AromaNode *slider = aroma_ui_slider(root, 20, 280, 260, 32, 0, 100, 50);
aroma_slider_set_on_change(slider, [](int value, void *ud) {
    // handle value change
}, NULL);
```

## Textbox

```c
AromaNode *tb = aroma_ui_textbox(root, "Enter name...", 20, 340, 260, 48);
aroma_textbox_set_on_change(tb, [](const char *text, void *ud) {
    // handle text change
}, NULL);
aroma_textbox_set_on_submit(tb, [](const char *text, void *ud) {
    // handle Enter key
}, NULL);
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

## What's Next

- Learn [Layout & Navigation](Layout-and-Navigation-Widgets.md) for containers and scrolling.
- Explore the [Map Widget](Map-Widget.md) for interactive maps.
- Try [Incense](../widget-library/wasm/incense_sandbox/index.html) for rapid prototyping.
