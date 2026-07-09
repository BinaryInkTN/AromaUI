#include "easter_egg.h"
#include "app_state.h"
#include <stdlib.h>
#include <limits.h>
#include "aroma_animation.h"

static float bounce_start_x, bounce_start_y, bounce_end_x, bounce_end_y;

static void bounce_anim_cb(AromaNode *target, float t, void *user_data)
{
    (void)user_data;
    if (!target) return;
    AromaRect *r = (AromaRect *)target->node_widget_ptr;
    if (r) {
        r->x = bounce_start_x + (bounce_end_x - bounce_start_x) * t;
        r->y = bounce_start_y + (bounce_end_y - bounce_start_y) * t;
    }
}

static void interact_easter_egg_cb(void *user_data)
{
    (void)user_data;
    if (!state.easter_egg_icon) return;
    AromaRect *r = (AromaRect *)state.easter_egg_icon->node_widget_ptr;
    if (!r) return;
    bounce_start_x = r->x;
    bounce_start_y = r->y;
    bounce_end_x = 50 + (rand() % (WIN_W - 200));
    bounce_end_y = 50 + (rand() % (WIN_H - 200));
    AromaAnimation *anim = aroma_animation_start_custom(
        state.easter_egg_icon, 0.0f, 1.0f, 800, bounce_anim_cb, NULL);
    if (anim)
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_ELASTIC);
}

static void close_easter_egg_cb(void *user_data)
{
    (void)user_data;
    if (state.easter_egg_overlay)
        aroma_node_set_hidden(state.easter_egg_overlay, true);
}

void build_easter_egg_ui(AromaNode *window)
{
    state.easter_egg_overlay = aroma_ui_card(window, 0, 0, WIN_W, WIN_H, CARD_TYPE_GLASS);
    if (!state.easter_egg_overlay) return;
    aroma_node_set_z_index(state.easter_egg_overlay, INT_MAX);
    aroma_node_set_hidden(state.easter_egg_overlay, true);

    state.easter_egg_icon = aroma_ui_iconbutton(
        state.easter_egg_overlay, AROMA_ICON_BUG_REPORT,
        WIN_W / 2 - 50, WIN_H / 2 - 50, 100,
        ICON_BUTTON_FILLED, interact_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(state.easter_egg_icon, INT_MAX);

    AromaNode *close_btn = aroma_ui_iconbutton(
        state.easter_egg_overlay, AROMA_ICON_CLOSE,
        WIN_W - 80, 30, 50,
        ICON_BUTTON_OUTLINED, close_easter_egg_cb, NULL, state.icon_font);
    aroma_node_set_z_index(close_btn, INT_MAX);
}