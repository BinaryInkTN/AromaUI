#include "widgets/aroma_chip.h"
#include "core/aroma_logger.h"
#include "core/aroma_slab_alloc.h"
#include "core/aroma_style.h"
#include "core/aroma_event.h"
#include "aroma_ui.h"
#include "backends/aroma_abi.h"
#include "backends/graphics/aroma_graphics_interface.h"
#include <string.h>
#include <stdlib.h>
#ifdef __ANDROID__
#include "aroma_android.h"
#endif

#define MD3_CHIP_HEIGHT 32
#define MD3_CHIP_MIN_WIDTH 48
#define MD3_CHIP_PADDING_H 12
#define MD3_CHIP_PADDING_V 4
#define MD3_CHIP_LABEL_SPACING 8
#define MD3_CHIP_CORNER_RADIUS 8

#define MD3_STATE_HOVER_OPACITY 0.08f
#define MD3_STATE_FOCUS_OPACITY 0.12f
#define MD3_STATE_PRESS_OPACITY 0.12f
#define MD3_STATE_SELECTED_OPACITY 0.12f

#define MD3_SURFACE_CONTAINER_OPACITY 0.08f
#define MD3_SELECTED_CONTAINER_OPACITY 0.12f

typedef struct AromaChip
{
    AromaRect rect;
    char label[64];
    AromaChipType type;
    bool selected;
    bool is_enabled;
    bool is_hovered;
    bool is_pressed;
    uint32_t bg_color;
    uint32_t text_color;
    uint32_t selected_color;
    uint32_t surface_color;
    uint32_t outline_color;
    float border_radius;
    float state_layer_opacity;
    void (*callback)(void *user_data);
    void *user_data;
    AromaFont *font;
    char icon[8];
    AromaFont *icon_font;
    int text_x;
    int text_y;
    int icon_x;
    int icon_y;
    bool use_theme_colors;
} AromaChip;

static uint32_t __get_chip_container_color(AromaChip *chip, AromaTheme *theme)
{
    if (chip->selected)
    {
        return aroma_color_blend(theme->colors.surface, theme->colors.primary_light,
                                 MD3_SELECTED_CONTAINER_OPACITY);
    }

    return aroma_color_blend(theme->colors.surface, theme->colors.primary_light,
                             MD3_SURFACE_CONTAINER_OPACITY);
}

static uint32_t __get_state_layer_color(AromaChip *chip, AromaTheme *theme)
{
    (void)chip;
    return theme->colors.primary;
}

static float __calculate_state_opacity(AromaChip *chip)
{
    if (!chip->is_enabled)
    {
        return 0.0f;
    }

    if (chip->is_pressed)
    {
        return MD3_STATE_PRESS_OPACITY;
    }
    else if (chip->is_hovered)
    {
        return MD3_STATE_HOVER_OPACITY;
    }
    else if (chip->selected)
    {
        return MD3_STATE_SELECTED_OPACITY;
    }

    return 0.0f;
}

static bool __chip_handle_event(AromaEvent *event, void *user_data)
{
    if (!event || !event->target_node)
        return false;
    AromaChip *chip = (AromaChip *)event->target_node->node_widget_ptr;
    if (!chip || !chip->is_enabled)
        return false;

    AromaRect r = chip->rect;
    bool in_bounds = (event->data.mouse.x >= r.x && event->data.mouse.x <= r.x + r.width &&
                      event->data.mouse.y >= r.y && event->data.mouse.y <= r.y + r.height);

    switch (event->event_type)
    {
    case EVENT_TYPE_MOUSE_ENTER:
        chip->is_hovered = true;
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return true;

    case EVENT_TYPE_MOUSE_EXIT:
        chip->is_hovered = false;
        chip->is_pressed = false;
        aroma_node_invalidate(event->target_node);
        aroma_ui_request_redraw(NULL);
        return false;

    case EVENT_TYPE_MOUSE_CLICK:
        if (in_bounds)
        {
            chip->is_pressed = true;
            aroma_node_invalidate(event->target_node);
            aroma_ui_request_redraw(NULL);
            return true;
        }
        break;

    case EVENT_TYPE_MOUSE_RELEASE:
        if (chip->is_pressed)
        {
            chip->is_pressed = false;
            if (in_bounds)
            {
                if (chip->type == CHIP_TYPE_FILTER)
                {
                    chip->selected = !chip->selected;
                }
                if (chip->callback)
                {
                    chip->callback(chip->user_data);
                }
            }
            aroma_node_invalidate(event->target_node);
            aroma_ui_request_redraw(NULL);
            return in_bounds;
        }
        break;

    default:
        break;
    }

    return false;
}

static void __chip_update_layout(AromaChip *chip)
{
    if (!chip)
        return;

    chip->rect.height = MD3_CHIP_HEIGHT;
    chip->border_radius = (float)MD3_CHIP_CORNER_RADIUS;

    int icon_width = 0;
    float icon_scale = 1.0f;

    if (chip->icon[0] != '\0' && chip->icon_font)
    {
        int default_icon_h = aroma_font_get_line_height(chip->icon_font);
        if (default_icon_h > 0)
        {
            int target_h = chip->rect.height * 0.6f;
            int max_width = chip->rect.width - (MD3_CHIP_PADDING_H * 2);
            if (target_h > max_width * 0.8f)
                target_h = max_width * 0.8f;

            icon_scale = (float)target_h / (float)default_icon_h;
            icon_width = aroma_font_get_px_size(chip->icon_font) * icon_scale;
        }
    }

    int label_width = 0;
    if (chip->label[0] != '\0')
    {
        if (chip->font)
        {
            label_width = aroma_font_get_line_width(chip->font, chip->label);
        }
        else
        {
            label_width = (int)strlen(chip->label) * 8;
        }
    }

    int content_width = 0;
    if (icon_width > 0 && label_width > 0)
    {
        content_width = icon_width + MD3_CHIP_LABEL_SPACING + label_width;
    }
    else if (icon_width > 0)
    {
        content_width = icon_width;
    }
    else
    {
        content_width = label_width;
    }

    chip->rect.width = MD3_CHIP_PADDING_H * 2 + content_width;

    if (chip->rect.width < MD3_CHIP_MIN_WIDTH)
    {
        chip->rect.width = MD3_CHIP_MIN_WIDTH;
    }

    int content_start_x = chip->rect.x + (chip->rect.width - content_width) / 2;

    if (icon_width > 0 && chip->icon_font)
    {
        chip->icon_x = content_start_x;
        int icon_line_h = aroma_font_get_line_height(chip->icon_font) * icon_scale;
        chip->icon_y = chip->rect.y + (chip->rect.height - icon_line_h) / 2;
    }

    if (label_width > 0)
    {
        chip->text_x = icon_width > 0 ? content_start_x + icon_width + MD3_CHIP_LABEL_SPACING : content_start_x;

        if (chip->font)
        {
            int label_h = aroma_font_get_line_height(chip->font);
            chip->text_y = chip->rect.y + (chip->rect.height - label_h) / 2;
        }
        else
        {
            chip->text_y = chip->rect.y + chip->rect.height / 2;
        }
    }
}

AromaNode *aroma_chip_create(AromaNode *parent, int x, int y, const char *label, AromaChipType type)
{
    if (!parent)
        return NULL;

    AromaChip *chip = (AromaChip *)aroma_widget_alloc(sizeof(AromaChip));
    if (!chip)
        return NULL;

    memset(chip, 0, sizeof(AromaChip));
#ifdef __ANDROID__
x = aroma_android_dp_to_px(x);
y = aroma_android_dp_to_px(y);
#endif
    AromaTheme theme = aroma_theme_get_global();

    chip->rect.x = x;
    chip->rect.y = y;
    chip->type = type;
    chip->selected = false;
    chip->is_enabled = true;
    chip->is_hovered = false;
    chip->is_pressed = false;
    chip->state_layer_opacity = 0.0f;

    chip->bg_color = __get_chip_container_color(chip, &theme);
    chip->text_color = theme.colors.text_primary;
    chip->selected_color = aroma_color_blend(theme.colors.surface,
                                             theme.colors.primary_light,
                                             MD3_SELECTED_CONTAINER_OPACITY);
    chip->surface_color = theme.colors.surface;
    chip->outline_color = theme.colors.border;

    if (label)
    {
        strncpy(chip->label, label, sizeof(chip->label) - 1);
        chip->label[sizeof(chip->label) - 1] = '\0';
    }
    else
    {
        chip->label[0] = '\0';
    }

    chip->callback = NULL;
    chip->user_data = NULL;
    chip->font = NULL;
    chip->icon[0] = '\0';
    chip->icon_font = NULL;
    chip->use_theme_colors = true;

    __chip_update_layout(chip);

    AromaNode *node = __add_child_node(NODE_TYPE_WIDGET, parent, chip);
    if (!node)
    {
        aroma_widget_free(chip);
        return NULL;
    }
    aroma_node_set_draw_cb(node, aroma_chip_draw);

    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_ENTER, __chip_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_EXIT, __chip_handle_event, NULL, 60);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __chip_handle_event, NULL, 70);
    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_RELEASE, __chip_handle_event, NULL, 70);

#ifdef ESP32
    aroma_node_invalidate(node);
#endif

    return node;
}

void aroma_chip_set_callback(AromaNode *chip_node, void (*callback)(void *user_data), void *user_data)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;
    chip->callback = callback;
    chip->user_data = user_data;
}

void aroma_chip_set_selected(AromaNode *chip_node, bool selected)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;
    chip->selected = selected;
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_enabled(AromaNode *chip_node, bool enabled)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;
    chip->is_enabled = enabled;
    chip->state_layer_opacity = 0.0f;
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_font(AromaNode *chip_node, AromaFont *font)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;
    chip->font = font;
    __chip_update_layout(chip);
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_icon(AromaNode *chip_node, const char *icon, AromaFont *icon_font)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;

    if (icon)
    {
        strncpy(chip->icon, icon, 7);
        chip->icon[7] = '\0';
    }
    else
    {
        chip->icon[0] = '\0';
    }
    chip->icon_font = icon_font;
    __chip_update_layout(chip);
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_text(AromaNode *chip_node, const char *text)
{
    if (!chip_node || !text)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;

    strncpy(chip->label, text, sizeof(chip->label) - 1);
    chip->label[sizeof(chip->label) - 1] = '\0';

    __chip_update_layout(chip);
    aroma_node_invalidate(chip_node);
}

void aroma_chip_set_colors(AromaNode *chip_node, uint32_t bg_color, uint32_t text_color, uint32_t selected_color)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (!chip)
        return;
    chip->bg_color = bg_color;
    chip->text_color = text_color;
    chip->selected_color = selected_color;
    chip->use_theme_colors = false;
    aroma_node_invalidate(chip_node);
}

void aroma_chip_draw(AromaNode *chip_node, size_t window_id)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (aroma_node_is_hidden(chip_node))
        return;
    if (!chip)
        return;

    AromaGraphicsInterface *gfx = aroma_backend_abi.get_graphics_interface();
    if (!gfx)
        return;

    if (chip->use_theme_colors)
    {
        AromaTheme theme = aroma_theme_get_global();
        chip->bg_color = __get_chip_container_color(chip, &theme);
        chip->text_color = theme.colors.text_primary;
        chip->selected_color = aroma_color_blend(theme.colors.surface,
                                                 theme.colors.primary_light,
                                                 MD3_SELECTED_CONTAINER_OPACITY);
        chip->surface_color = theme.colors.surface;
        chip->outline_color = theme.colors.border;
    }

    float target_opacity = __calculate_state_opacity(chip);
    chip->state_layer_opacity = chip->state_layer_opacity +
                                (target_opacity - chip->state_layer_opacity) * 0.3f;

    float content_alpha = chip->is_enabled ? 1.0f : 0.38f;

    uint32_t container_color = chip->selected ? chip->selected_color : chip->bg_color;

    if (chip->state_layer_opacity > 0.0f)
    {
        AromaTheme theme = aroma_theme_get_global();
        uint32_t state_color = __get_state_layer_color(chip, &theme);
        container_color = aroma_color_blend(container_color, state_color,
                                            chip->state_layer_opacity);
    }

    if (!chip->is_enabled)
    {
        container_color = aroma_color_blend(container_color, chip->surface_color, 0.88f);
    }

    gfx->fill_rectangle(window_id, chip->rect.x, chip->rect.y,
                        chip->rect.width, chip->rect.height,
                        container_color, true, chip->border_radius);

    if (chip->type == CHIP_TYPE_INPUT || chip->type == CHIP_TYPE_FILTER)
    {
        uint32_t outline_color = chip->outline_color;
        if (!chip->is_enabled)
        {
            outline_color = aroma_color_blend(outline_color, chip->surface_color, 0.88f);
        }

        gfx->draw_hollow_rectangle(window_id, chip->rect.x, chip->rect.y,
                                   chip->rect.width, chip->rect.height,
                                   outline_color, 1, true, chip->border_radius);
    }

    uint32_t draw_text_color = chip->text_color;
    uint32_t draw_icon_color = chip->text_color;
    if (!chip->is_enabled)
    {
        draw_text_color = aroma_color_blend(chip->text_color, chip->surface_color, 1.0f - content_alpha);
        draw_icon_color = aroma_color_blend(chip->text_color, chip->surface_color, 1.0f - content_alpha);
    }

    if (chip->icon[0] != '\0' && chip->icon_font && gfx->render_text)
    {
        int default_icon_h = aroma_font_get_line_height(chip->icon_font);
        if (default_icon_h > 0)
        {
            int target_h = chip->rect.height * 0.6f;
            int max_width = chip->rect.width - (MD3_CHIP_PADDING_H * 2);
            if (target_h > max_width * 0.8f)
                target_h = max_width * 0.8f;

            float scale = (float)target_h / (float)default_icon_h;
            int icon_line_h = default_icon_h * scale;
            int icon_y = chip->rect.y + (chip->rect.height - icon_line_h) / 2;

            gfx->render_text(window_id, chip->icon_font, chip->icon,
                           chip->icon_x, icon_y, draw_icon_color, scale);
        }
    }

    if (gfx->render_text && chip->font && chip->label[0])
    {
        gfx->render_text(window_id, chip->font, chip->label,
                        chip->text_x, chip->text_y,
                        draw_text_color, 1.0f);
    }
}

void aroma_chip_destroy(AromaNode *chip_node)
{
    if (!chip_node)
        return;
    AromaChip *chip = (AromaChip *)chip_node->node_widget_ptr;
    if (chip)
        aroma_widget_free(chip);
}