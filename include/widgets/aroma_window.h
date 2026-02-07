#ifndef AROMA_WINDOW_H
#define AROMA_WINDOW_H

#include "aroma_common.h"
#include "aroma_node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AromaWindow
{
    AromaRect rect;
    char* title;
    uint16_t window_id;
} AromaWindow;

AromaNode* aroma_window_create(const char* title, int x, int y, int width, int height);
void aroma_window_get_size(AromaNode* window_node, int* width, int* height);

/**
 * @brief Sets the fullscreen state of the window (Android only)
 *
 * @param window_node The window node
 * @param enable true to enable fullscreen, false to disable
 */
void aroma_window_set_fullscreen(AromaNode* window_node, bool enable);

#ifdef __cplusplus
}
#endif
#endif
