#ifndef AROMA_INCENSE_LOADER_H
#define AROMA_INCENSE_LOADER_H

#include "aroma_incense.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_window.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INCENSE_CALLBACK_VOID_PTR,
    INCENSE_CALLBACK_INT_STRING_PTR,
    INCENSE_CALLBACK_NODE_INT_PTR,
    INCENSE_CALLBACK_BOOL_PTR,
    INCENSE_CALLBACK_INT_PTR,
    INCENSE_CALLBACK_BOOL_BOOL_PTR,
    INCENSE_CALLBACK_NODE_STRING_PTR
} IncenseCallbackType;

void IncenseRegisterCallback(const char *name, IncenseCallbackType type, void *fn, void *userdata);
void IncenseClearCallbacks(void);

AromaWindow *IncenseLoad(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font);
AromaWindow *IncenseLoadFile(const char *path, AromaFont *font, AromaFont *icon_font);
AromaWindow *IncenseLoadString(const char *source, AromaFont *font, AromaFont *icon_font);

#ifdef __cplusplus
}
#endif

#endif