#ifndef AROMA_LOADING_H
#define AROMA_LOADING_H

#include "aroma_common.h"
#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

AromaNode* aroma_loading_create(AromaNode* parent, int x, int y, int radius, int thickness, uint32_t color);

#ifdef __cplusplus
}
#endif

#endif // AROMA_LOADING_H
