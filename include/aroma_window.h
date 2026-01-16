#ifndef AROMA_WINDOW_H
#define AROMA_WINDOW_H

#include "aroma_backend_interface.h"
#include "aroma_common.h"

#define AROMA_WINDOW_TITLE_MAX_LENGTH 256

typedef struct AromaWindow {
    AromaRect rect;
    char title[AROMA_WINDOW_TITLE_MAX_LENGTH];
} AromaWindow;

#endif