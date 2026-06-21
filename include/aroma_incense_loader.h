#ifndef AROMA_INCENSE_LOADER_H
#define AROMA_INCENSE_LOADER_H

#include "aroma_incense.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_window.h"
#include <stdbool.h>

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

typedef enum {
    INCENSE_ERROR_SYNTAX,
    INCENSE_ERROR_SEMANTIC,
    INCENSE_ERROR_WARNING,
    INCENSE_ERROR_SUGGESTION
} IncenseErrorType;

typedef struct {
    IncenseErrorType type;
    int line;
    int column;
    char message[256];
    char suggestion[128];
    char context[64];
} IncenseError;

void IncenseRegisterCallback(const char *name, IncenseCallbackType type, void *fn, void *userdata);
void IncenseClearCallbacks(void);

void IncenseRegisterFont(const char *name, AromaFont *font);

void IncenseSetVerboseErrors(bool verbose);
const IncenseError *IncenseGetErrors(int *count);
int IncenseGetErrorCount(void);
bool IncenseHasFatalError(void);
void IncenseClearErrors(void);

typedef struct IncenseRegistry IncenseRegistry;

AromaNode *IncenseFindWidget(const IncenseRegistry *registry, const char *id);
void IncenseFreeRegistry(IncenseRegistry *registry);

AromaWindow *IncenseLoad(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font);
AromaWindow *IncenseLoadFile(const char *path, AromaFont *font, AromaFont *icon_font);
AromaWindow *IncenseLoadString(const char *source, AromaFont *font, AromaFont *icon_font);

AromaWindow *IncenseLoadEx(const IncenseDocument *doc,
                           AromaFont *font, AromaFont *icon_font,
                           IncenseRegistry **out_registry);

AromaWindow *IncenseLoadFileEx(const char *path,
                               AromaFont *font, AromaFont *icon_font,
                               IncenseRegistry **out_registry);

AromaWindow *IncenseLoadStringEx(const char *source,
                                 AromaFont *font, AromaFont *icon_font,
                                 IncenseRegistry **out_registry);

int IncenseHotReloadStart(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry);
int IncenseHotReloadCheck(void);
bool IncenseHotReloadForce(int watcher_index);
int IncenseHotReloadForceAll(void);
AromaWindow *IncenseHotReloadGetWindow(int watcher_index);
void IncenseHotReloadSetCallback(int watcher_index, void (*on_reload)(AromaWindow *));
void IncenseHotReloadSetErrorCallback(int watcher_index, void (*on_error)(const char *));
void IncenseHotReloadStop(int watcher_index);
void IncenseHotReloadStopAll(void);

#ifdef __cplusplus
}
#endif

#endif