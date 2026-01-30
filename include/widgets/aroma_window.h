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
#ifdef __cplusplus
}
#endif
#endif
