#ifndef AROMA_INCENSE_LOADER_H
#define AROMA_INCENSE_LOADER_H

#include "aroma_incense.h"
#include "aroma_node.h"
#include "aroma_font.h"
#include "aroma_window.h"
#include <stdbool.h>
#include <stdint.h>

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

typedef enum {
    INCENSE_STATE_INT,
    INCENSE_STATE_FLOAT,
    INCENSE_STATE_BOOL,
    INCENSE_STATE_STRING
} IncenseStateType;

typedef struct {
    char key[64];
    IncenseStateType type;
    union {
        int    i;
        float  f;
        int    b;
        char   s[256];
    } val;
} IncenseStateEntry;

typedef void (*IncenseStateObserver)(const char *key, const IncenseStateEntry *entry, void *userdata);

typedef struct {
    IncenseStateObserver fn;
    void *userdata;
    char key[64];
} IncenseStateObserverEntry;

typedef struct {
    char   name[64];
    void  *fn;
    void  *userdata;
} IncenseLambdaEntry;

void IncenseRegisterCallback(const char *name, IncenseCallbackType type, void *fn, void *userdata);
void IncenseClearCallbacks(void);

void IncenseRegisterFont(const char *name, AromaFont *font);

void IncenseSetVerboseErrors(bool verbose);
const IncenseError *IncenseGetErrors(int *count);
int  IncenseGetErrorCount(void);
bool IncenseHasFatalError(void);
void IncenseClearErrors(void);

void  IncenseStateSetInt(const char *key, int value);
void  IncenseStateSetFloat(const char *key, float value);
void  IncenseStateSetBool(const char *key, bool value);
void  IncenseStateSetString(const char *key, const char *value);
bool  IncenseStateGetInt(const char *key, int *out);
bool  IncenseStateGetFloat(const char *key, float *out);
bool  IncenseStateGetBool(const char *key, bool *out);
bool  IncenseStateGetString(const char *key, char *out, size_t out_len);
bool  IncenseStateExists(const char *key);
void  IncenseStateDelete(const char *key);
void  IncenseStateClear(void);
int   IncenseStateAddObserver(const char *key, IncenseStateObserver fn, void *userdata);
void  IncenseStateRemoveObserver(int observer_id);
void  IncenseStateClearObservers(void);

void  IncenseRegisterLambda(const char *name, void *fn, void *userdata);
void  IncenseClearLambdas(void);
bool  IncenseLambdaExists(const char *name);

typedef struct IncenseRegistry IncenseRegistry;

AromaNode *IncenseFindWidget(const IncenseRegistry *registry, const char *id);
void       IncenseFreeRegistry(IncenseRegistry *registry);

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

int          IncenseHotReloadStart(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry);
int          IncenseHotReloadCheck(void);
bool         IncenseHotReloadForce(int watcher_index);
int          IncenseHotReloadForceAll(void);
AromaWindow *IncenseHotReloadGetWindow(int watcher_index);
void         IncenseHotReloadSetCallback(int watcher_index, void (*on_reload)(AromaWindow *));
void         IncenseHotReloadSetErrorCallback(int watcher_index, void (*on_error)(const char *));
void         IncenseHotReloadStop(int watcher_index);
void         IncenseHotReloadStopAll(void);

#ifdef __cplusplus
}
#endif

#endif