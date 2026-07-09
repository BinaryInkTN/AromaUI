#ifndef UI_ANIMATION_UTILS_H
#define UI_ANIMATION_UTILS_H

#include "aroma.h"

void animate_node_x(AromaNode *node, int from, int to);
int get_node_x(AromaNode *node);
void shift_node(AromaNode *node, int delta);

#endif