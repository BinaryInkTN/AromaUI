#include "ui_animation_utils.h"
#include "aroma_animation.h"
#include "app_state.h"

void animate_node_x(AromaNode *node, int from, int to)
{
    if (!node) return;
    AromaAnimation *anim = aroma_animation_start(
        node, AROMA_ANIM_SLIDE_X, from, to, SETTINGS_ANIM_MS);
    if (anim)
        aroma_animation_set_easing(anim, AROMA_EASE_OUT_CUBIC);
}

int get_node_x(AromaNode *node)
{
    if (!node || !node->node_widget_ptr) return 0;
    return ((AromaRect *)node->node_widget_ptr)->x;
}

void shift_node(AromaNode *node, int delta)
{
    if (!node) return;
    int x = get_node_x(node);
    animate_node_x(node, x, x + delta);
}