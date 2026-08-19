#include "aroma_incense_loader.h"
#include "aroma_incense.h"
#include "aroma_ui.h"
#include "aroma_animation.h"
#include "aroma_material_icons.h"
#include "core/aroma_logger.h"
#include "widgets/aroma_canvas.h"
#include "widgets/aroma_debug_overlay.h"
#include "widgets/aroma_dropdown.h"
#include "widgets/aroma_gif.h"
#include "widgets/aroma_icon.h"
#include "widgets/aroma_loading.h"
#include "widgets/aroma_map.h"
#include "widgets/aroma_menu.h"
#include "widgets/aroma_radiobutton.h"
#include "widgets/aroma_sidebar.h"
#include "widgets/aroma_table.h"
#include "widgets/aroma_tabs.h"
#include "widgets/aroma_tooltip.h"
#include "widgets/aroma_container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define stat _stat
#else
#include <unistd.h>
#include <ctype.h>
#endif

#define MAX_CALLBACKS 128
#define MAX_NAMED_WIDGETS 256
#define MAX_PROPS 64
#define MAX_CHILDREN 64
#define MAX_ITEM_NODES 64
#define MAX_ERRORS 256
#define MAX_EMBED_DEPTH 64
#define MAX_EMBED_PATH_LEN 4096
#define MAX_EMBED_SIZE (10 * 1024 * 1024)
#define MAX_LOAD_FILE_SIZE (64 * 1024 * 1024)
#define MAX_FONTS 32
#define MAX_HOT_RELOAD_WATCHERS 8
#define CB_HASH_SIZE 256
#define WR_HASH_SIZE 512
#define ICON_HASH_SIZE 2048
#define FONT_HASH_SIZE 64
#define MAX_STATE_ENTRIES 256
#define STATE_HASH_SIZE 256
#define MAX_STATE_OBSERVERS 64
#define MAX_EMBED_PROPS 32

typedef struct
{
    char name[64];
    IncenseCallbackType type;
    void *fn;
    void *userdata;
    int next;
} CallbackEntry;
typedef struct
{
    char id[64];
    AromaNode *node;
    int next;
} NamedWidget;
typedef struct
{
    NamedWidget items[MAX_NAMED_WIDGETS];
    int count;
    int buckets[WR_HASH_SIZE];
} WidgetRegistry;
struct IncenseRegistry
{
    WidgetRegistry reg;
};
typedef struct
{
    const char *key;
    const char *value;
} Prop;
typedef struct
{
    Prop items[MAX_PROPS];
    int count;
    IncenseNode *node;
} PropBag;
typedef struct
{
    char name[32];
    AromaFont *font;
    int next;
} FontEntry;
typedef struct
{
    FontEntry items[MAX_FONTS];
    int count;
    int buckets[FONT_HASH_SIZE];
} FontRegistry;
typedef struct
{
    WidgetRegistry *registry;
    FontRegistry *font_registry;
    AromaFont *default_font;
    AromaFont *icon_font;
} BuildCtx;
typedef AromaNode *(*WidgetBuilder)(IncenseNode *, AromaNode *, BuildCtx *);
typedef struct
{
    const char *name;
    WidgetBuilder build;
} WidgetEntry;
static void build_children(IncenseNode *node, AromaNode *parent, BuildCtx *ctx);
typedef struct
{
    char file_path[512];
    time_t last_modified;
    bool active;
    bool reload_in_progress;
    AromaWindow *window;
    AromaFont *font;
    AromaFont *icon_font;
    IncenseRegistry **out_registry;
    void (*on_reload)(AromaWindow *);
    void (*on_error)(const char *);
} HotReloadWatcher;

typedef struct
{
    char paths[MAX_EMBED_DEPTH][MAX_EMBED_PATH_LEN];
    size_t depth;
} EmbedStack;

typedef struct
{
    IncenseStateEntry entries[MAX_STATE_ENTRIES];
    int count;
    int buckets[STATE_HASH_SIZE];
    int next_idx[MAX_STATE_ENTRIES];
} StateStore;

typedef struct
{
    IncenseStateObserverEntry observers[MAX_STATE_OBSERVERS];
    bool active[MAX_STATE_OBSERVERS];
    int count;
} ObserverStore;

typedef struct
{
    char key[64];
    char value[256];
} EmbedProp;

typedef struct
{
    EmbedProp props[MAX_EMBED_PROPS];
    int count;
} EmbedPropStore;

static struct
{
    IncenseError errors[MAX_ERRORS];
    int count;
    bool has_fatal_error;
    bool verbose;
} g_err;

static CallbackEntry s_callbacks[MAX_CALLBACKS];
static int s_callback_count = 0;
static int s_cb_buckets[CB_HASH_SIZE];
static bool s_cb_init = false;
static FontRegistry *s_global_font_registry = NULL;
static HotReloadWatcher s_hot_watchers[MAX_HOT_RELOAD_WATCHERS];
static int s_hot_watcher_count = 0;
static bool s_widget_table_disabled = false;
static EmbedPropStore s_embed_props;

static StateStore s_state;
static bool s_state_init = false;
static ObserverStore s_observers;
static bool s_observers_init = false;

static inline uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    if (!s)
        return h;
    while (*s)
    {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static inline const char *safe_str(const char *s) { return s ? s : ""; }

#define COLOR_RED "\033[1;31m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_CYAN "\033[1;36m"
#define COLOR_GREEN "\033[1;32m"
#define COLOR_RESET "\033[0m"

static void err_clear(void)
{
    g_err.count = 0;
    g_err.has_fatal_error = false;
}

static void err_log_one(const IncenseError *e)
{
    if (!e)
        return;
    switch (e->type)
    {
    case INCENSE_ERROR_SYNTAX:
        LOG_ERROR("%s[%d:%d] SYNTAX ERROR: %s %s", COLOR_RED, e->line, e->column, e->message, COLOR_RESET);
        break;
    case INCENSE_ERROR_SEMANTIC:
        LOG_ERROR("%s[%d:%d] SEMANTIC ERROR: %s %s", COLOR_RED, e->line, e->column, e->message, COLOR_RESET);
        break;
    case INCENSE_ERROR_WARNING:
        LOG_WARNING("%s[%d:%d] WARNING: %s %s", COLOR_YELLOW, e->line, e->column, e->message, COLOR_RESET);
        break;
    case INCENSE_ERROR_SUGGESTION:
        LOG_INFO("%s[%d:%d] SUGGESTION: %s %s", COLOR_CYAN, e->line, e->column, e->message, COLOR_RESET);
        break;
    default:
        LOG_ERROR("%s[%d:%d] UNKNOWN ERROR TYPE: %s %s", COLOR_RED, e->line, e->column, e->message, COLOR_RESET);
        break;
    }
}

static void err_add_ex(IncenseErrorType type, int line, int col, const char *context, const char *fmt, ...)
{
    if (g_err.count < 0)
        g_err.count = 0;
    if (g_err.count >= MAX_ERRORS)
    {
        if (type == INCENSE_ERROR_SYNTAX || type == INCENSE_ERROR_SEMANTIC)
            g_err.has_fatal_error = true;
        return;
    }
    if (!fmt)
        fmt = "(no message)";
    IncenseError *e = &g_err.errors[g_err.count];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->line = line;
    e->column = col;
    if (context)
    {
        strncpy(e->context, context, sizeof(e->context) - 1);
        e->context[sizeof(e->context) - 1] = '\0';
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, args);
    va_end(args);
    e->message[sizeof(e->message) - 1] = '\0';
    if (type == INCENSE_ERROR_SYNTAX || type == INCENSE_ERROR_SEMANTIC)
        g_err.has_fatal_error = true;
    g_err.count++;
    err_log_one(e);
}

static void err_add(IncenseErrorType type, int line, int col, const char *fmt, ...)
{
    if (g_err.count < 0)
        g_err.count = 0;
    if (g_err.count >= MAX_ERRORS)
    {
        if (type == INCENSE_ERROR_SYNTAX || type == INCENSE_ERROR_SEMANTIC)
            g_err.has_fatal_error = true;
        return;
    }
    if (!fmt)
        fmt = "(no message)";
    IncenseError *e = &g_err.errors[g_err.count];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->line = line;
    e->column = col;
    va_list args;
    va_start(args, fmt);
    vsnprintf(e->message, sizeof(e->message), fmt, args);
    va_end(args);
    e->message[sizeof(e->message) - 1] = '\0';
    if (type == INCENSE_ERROR_SYNTAX || type == INCENSE_ERROR_SEMANTIC)
        g_err.has_fatal_error = true;
    g_err.count++;
    err_log_one(e);
}

static void err_add_suggestion(const char *s)
{
    if (!s)
        return;
    if (g_err.count <= 0)
    {
        err_add(INCENSE_ERROR_SUGGESTION, 0, 0, "%s", s);
        return;
    }
    if (g_err.count > MAX_ERRORS)
        return;
    IncenseError *e = &g_err.errors[g_err.count - 1];
    strncpy(e->suggestion, s, sizeof(e->suggestion) - 1);
    e->suggestion[sizeof(e->suggestion) - 1] = '\0';
    LOG_INFO("  %s-> Suggestion: %s%s", COLOR_GREEN, s, COLOR_RESET);
}

#define ND_LINE(n) ((n) ? (n)->line : 0)
#define ND_COL(n) ((n) ? (n)->column : 0)
#define ERR_SYNTAX_N(n, ...) err_add(INCENSE_ERROR_SYNTAX, ND_LINE(n), ND_COL(n), __VA_ARGS__)
#define ERR_SYNTAX_N_CTX(n, ctx, ...) err_add_ex(INCENSE_ERROR_SYNTAX, ND_LINE(n), ND_COL(n), ctx, __VA_ARGS__)
#define ERR_SEMANTIC_N(n, ...) err_add(INCENSE_ERROR_SEMANTIC, ND_LINE(n), ND_COL(n), __VA_ARGS__)
#define ERR_WARN_N(n, ...) err_add(INCENSE_ERROR_WARNING, ND_LINE(n), ND_COL(n), __VA_ARGS__)
#define ERR_WARN_N_CTX(n, ctx, ...) err_add_ex(INCENSE_ERROR_WARNING, ND_LINE(n), ND_COL(n), ctx, __VA_ARGS__)
#define ERR_SUGGEST(fmt, ...)                         \
    do                                                \
    {                                                 \
        char _s[256];                                 \
        snprintf(_s, sizeof(_s), fmt, ##__VA_ARGS__); \
        err_add_suggestion(_s);                       \
    } while (0)

static void embed_props_clear(void)
{
    memset(&s_embed_props, 0, sizeof(s_embed_props));
}

static void embed_props_add(const char *key, const char *value)
{
    if (!key || !value || s_embed_props.count >= MAX_EMBED_PROPS)
        return;
    for (int i = 0; i < s_embed_props.count; i++)
    {
        if (strcmp(s_embed_props.props[i].key, key) == 0)
        {
            strncpy(s_embed_props.props[i].value, value, sizeof(s_embed_props.props[i].value) - 1);
            return;
        }
    }
    strncpy(s_embed_props.props[s_embed_props.count].key, key, sizeof(s_embed_props.props[0].key) - 1);
    strncpy(s_embed_props.props[s_embed_props.count].value, value, sizeof(s_embed_props.props[0].value) - 1);
    s_embed_props.count++;
}

static const char *embed_props_get(const char *key)
{
    if (!key)
        return NULL;
    for (int i = 0; i < s_embed_props.count; i++)
    {
        if (strcmp(s_embed_props.props[i].key, key) == 0)
            return s_embed_props.props[i].value;
    }
    return NULL;
}

static void embed_props_parse_from_source(const char *source)
{
    if (!source)
        return;
    const char *p = source;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;
        if (strncmp(p, "const embed_", 12) == 0)
        {
            p += 12;
            const char *key_start = p;
            while (*p && *p != ' ' && *p != '=' && *p != '\n')
                p++;
            size_t key_len = p - key_start;
            while (*p && *p != '"' && *p != '\n')
                p++;
            if (*p == '"')
            {
                p++;
                const char *val_start = p;
                while (*p && *p != '"' && *p != '\n')
                    p++;
                if (*p == '"')
                {
                    size_t val_len = p - val_start;
                    if (key_len > 0 && val_len > 0)
                    {
                        char key[64], value[256];
                        memcpy(key, key_start, key_len);
                        key[key_len] = '\0';
                        memcpy(value, val_start, val_len);
                        value[val_len] = '\0';
                        embed_props_add(key, value);
                    }
                    p++;
                }
            }
        }
        else
        {
            p++;
        }
    }
}

static void state_init_once(void)
{
    if (s_state_init)
        return;
    memset(&s_state, 0, sizeof(s_state));
    memset(s_state.buckets, -1, sizeof(s_state.buckets));
    memset(s_state.next_idx, -1, sizeof(s_state.next_idx));
    s_state_init = true;
}

static void observers_init_once(void)
{
    if (s_observers_init)
        return;
    memset(&s_observers, 0, sizeof(s_observers));
    s_observers_init = true;
}

static IncenseStateEntry *state_find(const char *key)
{
    if (!key || !key[0])
        return NULL;
    state_init_once();
    uint32_t slot = fnv1a(key) & (STATE_HASH_SIZE - 1);
    int idx = s_state.buckets[slot];
    while (idx >= 0 && idx < s_state.count)
    {
        if (strcmp(s_state.entries[idx].key, key) == 0)
            return &s_state.entries[idx];
        idx = s_state.next_idx[idx];
    }
    return NULL;
}

static IncenseStateEntry *state_get_or_create(const char *key)
{
    if (!key || !key[0] || strlen(key) >= 64)
        return NULL;
    state_init_once();
    IncenseStateEntry *existing = state_find(key);
    if (existing)
        return existing;
    if (s_state.count >= MAX_STATE_ENTRIES)
    {
        LOG_ERROR("State store full, cannot add key '%s'", key);
        return NULL;
    }
    IncenseStateEntry *e = &s_state.entries[s_state.count];
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    uint32_t slot = fnv1a(key) & (STATE_HASH_SIZE - 1);
    s_state.next_idx[s_state.count] = s_state.buckets[slot];
    s_state.buckets[slot] = s_state.count;
    s_state.count++;
    return e;
}

static void state_notify_observers(const char *key, const IncenseStateEntry *entry)
{
    observers_init_once();
    for (int i = 0; i < s_observers.count; i++)
    {
        if (!s_observers.active[i])
            continue;
        IncenseStateObserverEntry *obs = &s_observers.observers[i];
        if (obs->key[0] == '\0' || strcmp(obs->key, key) == 0)
        {
            if (obs->fn)
                obs->fn(key, entry, obs->userdata);
        }
    }
}

void IncenseStateSetInt(const char *key, int value)
{
    IncenseStateEntry *e = state_get_or_create(key);
    if (!e)
        return;
    e->type = INCENSE_STATE_INT;
    e->val.i = value;
    state_notify_observers(key, e);
}

void IncenseStateSetFloat(const char *key, float value)
{
    IncenseStateEntry *e = state_get_or_create(key);
    if (!e)
        return;
    e->type = INCENSE_STATE_FLOAT;
    e->val.f = value;
    state_notify_observers(key, e);
}

void IncenseStateSetBool(const char *key, bool value)
{
    IncenseStateEntry *e = state_get_or_create(key);
    if (!e)
        return;
    e->type = INCENSE_STATE_BOOL;
    e->val.b = value ? 1 : 0;
    state_notify_observers(key, e);
}

void IncenseStateSetString(const char *key, const char *value)
{
    IncenseStateEntry *e = state_get_or_create(key);
    if (!e)
        return;
    e->type = INCENSE_STATE_STRING;
    if (value)
    {
        strncpy(e->val.s, value, sizeof(e->val.s) - 1);
        e->val.s[sizeof(e->val.s) - 1] = '\0';
    }
    else
    {
        e->val.s[0] = '\0';
    }
    state_notify_observers(key, e);
}

bool IncenseStateGetInt(const char *key, int *out)
{
    IncenseStateEntry *e = state_find(key);
    if (!e || !out)
        return false;
    if (e->type == INCENSE_STATE_INT)
    {
        *out = e->val.i;
        return true;
    }
    if (e->type == INCENSE_STATE_BOOL)
    {
        *out = e->val.b;
        return true;
    }
    if (e->type == INCENSE_STATE_FLOAT)
    {
        *out = (int)e->val.f;
        return true;
    }
    return false;
}

bool IncenseStateGetFloat(const char *key, float *out)
{
    IncenseStateEntry *e = state_find(key);
    if (!e || !out)
        return false;
    if (e->type == INCENSE_STATE_FLOAT)
    {
        *out = e->val.f;
        return true;
    }
    if (e->type == INCENSE_STATE_INT)
    {
        *out = (float)e->val.i;
        return true;
    }
    if (e->type == INCENSE_STATE_BOOL)
    {
        *out = (float)e->val.b;
        return true;
    }
    return false;
}

bool IncenseStateGetBool(const char *key, bool *out)
{
    IncenseStateEntry *e = state_find(key);
    if (!e || !out)
        return false;
    if (e->type == INCENSE_STATE_BOOL)
    {
        *out = e->val.b != 0;
        return true;
    }
    if (e->type == INCENSE_STATE_INT)
    {
        *out = e->val.i != 0;
        return true;
    }
    if (e->type == INCENSE_STATE_FLOAT)
    {
        *out = e->val.f != 0.0f;
        return true;
    }
    if (e->type == INCENSE_STATE_STRING)
    {
        *out = e->val.s[0] != '\0';
        return true;
    }
    return false;
}

bool IncenseStateGetString(const char *key, char *out, size_t out_len)
{
    IncenseStateEntry *e = state_find(key);
    if (!e || !out || out_len == 0)
        return false;
    if (e->type == INCENSE_STATE_STRING)
    {
        strncpy(out, e->val.s, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    if (e->type == INCENSE_STATE_INT)
    {
        snprintf(out, out_len, "%d", e->val.i);
        return true;
    }
    if (e->type == INCENSE_STATE_FLOAT)
    {
        snprintf(out, out_len, "%g", e->val.f);
        return true;
    }
    if (e->type == INCENSE_STATE_BOOL)
    {
        strncpy(out, e->val.b ? "true" : "false", out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

bool IncenseStateExists(const char *key)
{
    return state_find(key) != NULL;
}

void IncenseStateDelete(const char *key)
{
    if (!key || !key[0])
        return;
    state_init_once();
    uint32_t slot = fnv1a(key) & (STATE_HASH_SIZE - 1);
    int *prev_next = &s_state.buckets[slot];
    int idx = *prev_next;
    while (idx >= 0 && idx < s_state.count)
    {
        if (strcmp(s_state.entries[idx].key, key) == 0)
        {
            *prev_next = s_state.next_idx[idx];
            int last = s_state.count - 1;
            if (idx != last)
            {
                s_state.entries[idx] = s_state.entries[last];
                s_state.next_idx[idx] = s_state.next_idx[last];
                uint32_t moved_slot = fnv1a(s_state.entries[idx].key) & (STATE_HASH_SIZE - 1);
                int *mp = &s_state.buckets[moved_slot];
                while (*mp >= 0 && *mp != last)
                    mp = &s_state.next_idx[*mp];
                if (*mp == last)
                    *mp = idx;
            }
            s_state.count--;
            return;
        }
        prev_next = &s_state.next_idx[idx];
        idx = *prev_next;
    }
}

void IncenseStateClear(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(s_state.buckets, -1, sizeof(s_state.buckets));
    memset(s_state.next_idx, -1, sizeof(s_state.next_idx));
    s_state_init = true;
}

int IncenseStateAddObserver(const char *key, IncenseStateObserver fn, void *userdata)
{
    if (!fn)
        return -1;
    observers_init_once();
    for (int i = 0; i < s_observers.count; i++)
    {
        if (!s_observers.active[i])
        {
            s_observers.observers[i].fn = fn;
            s_observers.observers[i].userdata = userdata;
            if (key)
            {
                strncpy(s_observers.observers[i].key, key, 63);
                s_observers.observers[i].key[63] = '\0';
            }
            else
                s_observers.observers[i].key[0] = '\0';
            s_observers.active[i] = true;
            return i;
        }
    }
    if (s_observers.count >= MAX_STATE_OBSERVERS)
    {
        LOG_ERROR("Observer store full");
        return -1;
    }
    int id = s_observers.count++;
    s_observers.observers[id].fn = fn;
    s_observers.observers[id].userdata = userdata;
    if (key)
    {
        strncpy(s_observers.observers[id].key, key, 63);
        s_observers.observers[id].key[63] = '\0';
    }
    else
        s_observers.observers[id].key[0] = '\0';
    s_observers.active[id] = true;
    return id;
}

void IncenseStateRemoveObserver(int observer_id)
{
    if (observer_id < 0 || observer_id >= s_observers.count)
        return;
    s_observers.active[observer_id] = false;
}

void IncenseStateClearObservers(void)
{
    memset(&s_observers, 0, sizeof(s_observers));
    s_observers_init = true;
}

static void font_registry_init(FontRegistry *reg)
{
    if (!reg)
        return;
    reg->count = 0;
    memset(reg->buckets, -1, sizeof(reg->buckets));
}

static void font_registry_register(FontRegistry *reg, const char *name, AromaFont *font)
{
    if (!reg || !name || !name[0] || !font)
        return;
    uint32_t slot = fnv1a(name) & (FONT_HASH_SIZE - 1);
    for (int i = reg->buckets[slot]; i >= 0 && i < reg->count; i = reg->items[i].next)
    {
        if (strcmp(reg->items[i].name, name) == 0)
        {
            reg->items[i].font = font;
            return;
        }
    }
    if (reg->count >= MAX_FONTS)
    {
        LOG_ERROR("Font registry full, cannot register '%s'", name);
        return;
    }
    FontEntry *e = &reg->items[reg->count];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->font = font;
    e->next = reg->buckets[slot];
    reg->buckets[slot] = reg->count++;
}

static AromaFont *font_registry_find(const FontRegistry *reg, const char *name)
{
    if (!reg || !name || !name[0])
        return NULL;
    uint32_t slot = fnv1a(name) & (FONT_HASH_SIZE - 1);
    for (int i = reg->buckets[slot]; i >= 0 && i < reg->count; i = reg->items[i].next)
        if (strcmp(reg->items[i].name, name) == 0)
            return reg->items[i].font;
    return NULL;
}

void IncenseRegisterFont(const char *name, AromaFont *font)
{
    if (!name || !name[0] || !font)
    {
        LOG_WARNING("IncenseRegisterFont called with invalid arguments");
        return;
    }
    if (!s_global_font_registry)
    {
        s_global_font_registry = calloc(1, sizeof(FontRegistry));
        if (!s_global_font_registry)
        {
            LOG_ERROR("Failed to allocate global font registry");
            return;
        }
        font_registry_init(s_global_font_registry);
    }
    font_registry_register(s_global_font_registry, name, font);
}

void IncenseSetVerboseErrors(bool verbose) { g_err.verbose = verbose; }
const IncenseError *IncenseGetErrors(int *count)
{
    if (count)
        *count = g_err.count;
    return g_err.errors;
}
int IncenseGetErrorCount(void) { return g_err.count; }
bool IncenseHasFatalError(void) { return g_err.has_fatal_error; }
void IncenseClearErrors(void) { err_clear(); }

static void cb_init_buckets(void)
{
    memset(s_cb_buckets, -1, sizeof(s_cb_buckets));
    s_cb_init = true;
}

void IncenseRegisterCallback(const char *name, IncenseCallbackType type, void *fn, void *userdata)
{
    if (!name || !name[0] || !fn)
    {
        LOG_WARNING("IncenseRegisterCallback called with invalid arguments");
        return;
    }
    if (strlen(name) >= sizeof(s_callbacks[0].name))
    {
        LOG_ERROR("Callback name '%s' too long, maximum %zu characters", name, sizeof(s_callbacks[0].name) - 1);
        return;
    }
    if (!s_cb_init)
        cb_init_buckets();
    uint32_t slot = fnv1a(name) & (CB_HASH_SIZE - 1);
    for (int i = s_cb_buckets[slot]; i >= 0 && i < s_callback_count; i = s_callbacks[i].next)
    {
        if (strcmp(s_callbacks[i].name, name) == 0)
        {
            s_callbacks[i].type = type;
            s_callbacks[i].fn = fn;
            s_callbacks[i].userdata = userdata;
            return;
        }
    }
    if (s_callback_count >= MAX_CALLBACKS)
    {
        LOG_ERROR("Callback registry full, cannot register '%s'", name);
        return;
    }
    CallbackEntry *e = &s_callbacks[s_callback_count];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->type = type;
    e->fn = fn;
    e->userdata = userdata;
    e->next = s_cb_buckets[slot];
    s_cb_buckets[slot] = s_callback_count++;
}

void IncenseClearCallbacks(void)
{
    s_callback_count = 0;
    if (s_cb_init)
        memset(s_cb_buckets, -1, sizeof(s_cb_buckets));
}

static inline CallbackEntry *callback_find(const char *name)
{
    if (!name || !name[0] || !s_cb_init)
        return NULL;
    uint32_t slot = fnv1a(name) & (CB_HASH_SIZE - 1);
    for (int i = s_cb_buckets[slot]; i >= 0 && i < s_callback_count; i = s_callbacks[i].next)
        if (strcmp(s_callbacks[i].name, name) == 0)
            return &s_callbacks[i];
    return NULL;
}

static inline void registry_init(WidgetRegistry *reg)
{
    if (!reg)
        return;
    reg->count = 0;
    memset(reg->buckets, -1, sizeof(reg->buckets));
}

static void registry_register(WidgetRegistry *reg, const char *id, AromaNode *node)
{
    if (!reg || !id || !id[0] || !node)
        return;
    if (strlen(id) >= sizeof(reg->items[0].id))
    {
        LOG_WARNING("Widget id '%s' too long, maximum %zu characters, truncating", id, sizeof(reg->items[0].id) - 1);
    }
    uint32_t slot = fnv1a(id) & (WR_HASH_SIZE - 1);
    for (int i = reg->buckets[slot]; i >= 0 && i < reg->count; i = reg->items[i].next)
    {
        if (strcmp(reg->items[i].id, id) == 0)
        {
            LOG_WARNING("Duplicate widget id '%s' - overwriting", id);
            reg->items[i].node = node;
            return;
        }
    }
    if (reg->count >= MAX_NAMED_WIDGETS)
    {
        LOG_ERROR("Widget registry full, cannot register id '%s'", id);
        return;
    }
    NamedWidget *w = &reg->items[reg->count];
    strncpy(w->id, id, sizeof(w->id) - 1);
    w->id[sizeof(w->id) - 1] = '\0';
    w->node = node;
    w->next = reg->buckets[slot];
    reg->buckets[slot] = reg->count++;
}

static inline AromaNode *registry_find(const WidgetRegistry *reg, const char *id)
{
    if (!reg || !id || !id[0])
        return NULL;
    uint32_t slot = fnv1a(id) & (WR_HASH_SIZE - 1);
    for (int i = reg->buckets[slot]; i >= 0 && i < reg->count; i = reg->items[i].next)
        if (strcmp(reg->items[i].id, id) == 0)
            return reg->items[i].node;
    return NULL;
}

AromaNode *IncenseFindWidget(const IncenseRegistry *registry, const char *id)
{
    if (!registry || !id || !id[0])
        return NULL;
    AromaNode *node = registry_find(&registry->reg, id);
    if (!node)
        LOG_WARNING("Widget with id '%s' not found in registry", id);
    return node;
}

void IncenseFreeRegistry(IncenseRegistry *registry)
{
    if (registry)
        free(registry);
}

typedef struct
{
    const char *name;
    const char *codepoint;
} IconMapping;

static const IconMapping ICON_MAP[] = {
    {"AROMA_ICON_3D_ROTATION", AROMA_ICON_3D_ROTATION},
    {"AROMA_ICON_AC_UNIT", AROMA_ICON_AC_UNIT},
    {"AROMA_ICON_ACCESS_ALARM", AROMA_ICON_ACCESS_ALARM},
    {"AROMA_ICON_ACCESS_ALARMS", AROMA_ICON_ACCESS_ALARMS},
    {"AROMA_ICON_ACCESS_TIME", AROMA_ICON_ACCESS_TIME},
    {"AROMA_ICON_ACCESSIBILITY", AROMA_ICON_ACCESSIBILITY},
    {"AROMA_ICON_ACCESSIBLE", AROMA_ICON_ACCESSIBLE},
    {"AROMA_ICON_ACCOUNT_BALANCE", AROMA_ICON_ACCOUNT_BALANCE},
    {"AROMA_ICON_ACCOUNT_BALANCE_WALLET", AROMA_ICON_ACCOUNT_BALANCE_WALLET},
    {"AROMA_ICON_ACCOUNT_BOX", AROMA_ICON_ACCOUNT_BOX},
    {"AROMA_ICON_ACCOUNT_CIRCLE", AROMA_ICON_ACCOUNT_CIRCLE},
    {"AROMA_ICON_ADB", AROMA_ICON_ADB},
    {"AROMA_ICON_ADD", AROMA_ICON_ADD},
    {"AROMA_ICON_ADD_A_PHOTO", AROMA_ICON_ADD_A_PHOTO},
    {"AROMA_ICON_ADD_ALARM", AROMA_ICON_ADD_ALARM},
    {"AROMA_ICON_ADD_ALERT", AROMA_ICON_ADD_ALERT},
    {"AROMA_ICON_ADD_BOX", AROMA_ICON_ADD_BOX},
    {"AROMA_ICON_ADD_CIRCLE", AROMA_ICON_ADD_CIRCLE},
    {"AROMA_ICON_ADD_CIRCLE_OUTLINE", AROMA_ICON_ADD_CIRCLE_OUTLINE},
    {"AROMA_ICON_ADD_LOCATION", AROMA_ICON_ADD_LOCATION},
    {"AROMA_ICON_ADD_SHOPPING_CART", AROMA_ICON_ADD_SHOPPING_CART},
    {"AROMA_ICON_ADD_TO_PHOTOS", AROMA_ICON_ADD_TO_PHOTOS},
    {"AROMA_ICON_ADD_TO_QUEUE", AROMA_ICON_ADD_TO_QUEUE},
    {"AROMA_ICON_ADJUST", AROMA_ICON_ADJUST},
    {"AROMA_ICON_AIRLINE_SEAT_FLAT", AROMA_ICON_AIRLINE_SEAT_FLAT},
    {"AROMA_ICON_AIRLINE_SEAT_FLAT_ANGLED", AROMA_ICON_AIRLINE_SEAT_FLAT_ANGLED},
    {"AROMA_ICON_AIRLINE_SEAT_INDIVIDUAL_SUITE", AROMA_ICON_AIRLINE_SEAT_INDIVIDUAL_SUITE},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA", AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_NORMAL", AROMA_ICON_AIRLINE_SEAT_LEGROOM_NORMAL},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_REDUCED", AROMA_ICON_AIRLINE_SEAT_LEGROOM_REDUCED},
    {"AROMA_ICON_AIRLINE_SEAT_RECLINE_EXTRA", AROMA_ICON_AIRLINE_SEAT_RECLINE_EXTRA},
    {"AROMA_ICON_AIRLINE_SEAT_RECLINE_NORMAL", AROMA_ICON_AIRLINE_SEAT_RECLINE_NORMAL},
    {"AROMA_ICON_AIRPLANEMODE_ACTIVE", AROMA_ICON_AIRPLANEMODE_ACTIVE},
    {"AROMA_ICON_AIRPLANEMODE_INACTIVE", AROMA_ICON_AIRPLANEMODE_INACTIVE},
    {"AROMA_ICON_AIRPLAY", AROMA_ICON_AIRPLAY},
    {"AROMA_ICON_AIRPORT_SHUTTLE", AROMA_ICON_AIRPORT_SHUTTLE},
    {"AROMA_ICON_ALARM", AROMA_ICON_ALARM},
    {"AROMA_ICON_ALARM_ADD", AROMA_ICON_ALARM_ADD},
    {"AROMA_ICON_ALARM_OFF", AROMA_ICON_ALARM_OFF},
    {"AROMA_ICON_ALARM_ON", AROMA_ICON_ALARM_ON},
    {"AROMA_ICON_ALBUM", AROMA_ICON_ALBUM},
    {"AROMA_ICON_ALL_INCLUSIVE", AROMA_ICON_ALL_INCLUSIVE},
    {"AROMA_ICON_ALL_OUT", AROMA_ICON_ALL_OUT},
    {"AROMA_ICON_ANDROID", AROMA_ICON_ANDROID},
    {"AROMA_ICON_ANNOUNCEMENT", AROMA_ICON_ANNOUNCEMENT},
    {"AROMA_ICON_APPS", AROMA_ICON_APPS},
    {"AROMA_ICON_ARCHIVE", AROMA_ICON_ARCHIVE},
    {"AROMA_ICON_ARROW_BACK", AROMA_ICON_ARROW_BACK},
    {"AROMA_ICON_ARROW_BACK_IOS", AROMA_ICON_ARROW_BACK_IOS},
    {"AROMA_ICON_ARROW_DOWNWARD", AROMA_ICON_ARROW_DOWNWARD},
    {"AROMA_ICON_ARROW_DROP_DOWN", AROMA_ICON_ARROW_DROP_DOWN},
    {"AROMA_ICON_ARROW_DROP_DOWN_CIRCLE", AROMA_ICON_ARROW_DROP_DOWN_CIRCLE},
    {"AROMA_ICON_ARROW_DROP_UP", AROMA_ICON_ARROW_DROP_UP},
    {"AROMA_ICON_ARROW_FORWARD", AROMA_ICON_ARROW_FORWARD},
    {"AROMA_ICON_ARROW_FORWARD_IOS", AROMA_ICON_ARROW_FORWARD_IOS},
    {"AROMA_ICON_ARROW_LEFT", AROMA_ICON_ARROW_LEFT},
    {"AROMA_ICON_ARROW_RIGHT", AROMA_ICON_ARROW_RIGHT},
    {"AROMA_ICON_ARROW_UPWARD", AROMA_ICON_ARROW_UPWARD},
    {"AROMA_ICON_ART_TRACK", AROMA_ICON_ART_TRACK},
    {"AROMA_ICON_ASPECT_RATIO", AROMA_ICON_ASPECT_RATIO},
    {"AROMA_ICON_ASSESSMENT", AROMA_ICON_ASSESSMENT},
    {"AROMA_ICON_ASSIGNMENT", AROMA_ICON_ASSIGNMENT},
    {"AROMA_ICON_ASSIGNMENT_IND", AROMA_ICON_ASSIGNMENT_IND},
    {"AROMA_ICON_ASSIGNMENT_LATE", AROMA_ICON_ASSIGNMENT_LATE},
    {"AROMA_ICON_ASSIGNMENT_RETURN", AROMA_ICON_ASSIGNMENT_RETURN},
    {"AROMA_ICON_ASSIGNMENT_RETURNED", AROMA_ICON_ASSIGNMENT_RETURNED},
    {"AROMA_ICON_ASSIGNMENT_TURNED_IN", AROMA_ICON_ASSIGNMENT_TURNED_IN},
    {"AROMA_ICON_ASSISTANT", AROMA_ICON_ASSISTANT},
    {"AROMA_ICON_ASSISTANT_PHOTO", AROMA_ICON_ASSISTANT_PHOTO},
    {"AROMA_ICON_ATTACH_FILE", AROMA_ICON_ATTACH_FILE},
    {"AROMA_ICON_ATTACH_MONEY", AROMA_ICON_ATTACH_MONEY},
    {"AROMA_ICON_ATTACHMENT", AROMA_ICON_ATTACHMENT},
    {"AROMA_ICON_AUDIOTRACK", AROMA_ICON_AUDIOTRACK},
    {"AROMA_ICON_AUTORENEW", AROMA_ICON_AUTORENEW},
    {"AROMA_ICON_AV_TIMER", AROMA_ICON_AV_TIMER},
    {"AROMA_ICON_BACKSPACE", AROMA_ICON_BACKSPACE},
    {"AROMA_ICON_BACKUP", AROMA_ICON_BACKUP},
    {"AROMA_ICON_BATTERY_ALERT", AROMA_ICON_BATTERY_ALERT},
    {"AROMA_ICON_BATTERY_CHARGING_FULL", AROMA_ICON_BATTERY_CHARGING_FULL},
    {"AROMA_ICON_BATTERY_FULL", AROMA_ICON_BATTERY_FULL},
    {"AROMA_ICON_BATTERY_STD", AROMA_ICON_BATTERY_STD},
    {"AROMA_ICON_BATTERY_UNKNOWN", AROMA_ICON_BATTERY_UNKNOWN},
    {"AROMA_ICON_BEACH_ACCESS", AROMA_ICON_BEACH_ACCESS},
    {"AROMA_ICON_BEENHERE", AROMA_ICON_BEENHERE},
    {"AROMA_ICON_BLOCK", AROMA_ICON_BLOCK},
    {"AROMA_ICON_BLUETOOTH", AROMA_ICON_BLUETOOTH},
    {"AROMA_ICON_BLUETOOTH_AUDIO", AROMA_ICON_BLUETOOTH_AUDIO},
    {"AROMA_ICON_BLUETOOTH_CONNECTED", AROMA_ICON_BLUETOOTH_CONNECTED},
    {"AROMA_ICON_BLUETOOTH_DISABLED", AROMA_ICON_BLUETOOTH_DISABLED},
    {"AROMA_ICON_BLUETOOTH_SEARCHING", AROMA_ICON_BLUETOOTH_SEARCHING},
    {"AROMA_ICON_BLUR_CIRCULAR", AROMA_ICON_BLUR_CIRCULAR},
    {"AROMA_ICON_BLUR_LINEAR", AROMA_ICON_BLUR_LINEAR},
    {"AROMA_ICON_BLUR_OFF", AROMA_ICON_BLUR_OFF},
    {"AROMA_ICON_BLUR_ON", AROMA_ICON_BLUR_ON},
    {"AROMA_ICON_BOOK", AROMA_ICON_BOOK},
    {"AROMA_ICON_BOOKMARK", AROMA_ICON_BOOKMARK},
    {"AROMA_ICON_BOOKMARK_BORDER", AROMA_ICON_BOOKMARK_BORDER},
    {"AROMA_ICON_BORDER_ALL", AROMA_ICON_BORDER_ALL},
    {"AROMA_ICON_BORDER_BOTTOM", AROMA_ICON_BORDER_BOTTOM},
    {"AROMA_ICON_BORDER_CLEAR", AROMA_ICON_BORDER_CLEAR},
    {"AROMA_ICON_BORDER_COLOR", AROMA_ICON_BORDER_COLOR},
    {"AROMA_ICON_BORDER_HORIZONTAL", AROMA_ICON_BORDER_HORIZONTAL},
    {"AROMA_ICON_BORDER_INNER", AROMA_ICON_BORDER_INNER},
    {"AROMA_ICON_BORDER_LEFT", AROMA_ICON_BORDER_LEFT},
    {"AROMA_ICON_BORDER_OUTER", AROMA_ICON_BORDER_OUTER},
    {"AROMA_ICON_BORDER_RIGHT", AROMA_ICON_BORDER_RIGHT},
    {"AROMA_ICON_BORDER_STYLE", AROMA_ICON_BORDER_STYLE},
    {"AROMA_ICON_BORDER_TOP", AROMA_ICON_BORDER_TOP},
    {"AROMA_ICON_BORDER_VERTICAL", AROMA_ICON_BORDER_VERTICAL},
    {"AROMA_ICON_BRANDING_WATERMARK", AROMA_ICON_BRANDING_WATERMARK},
    {"AROMA_ICON_BRIGHTNESS_1", AROMA_ICON_BRIGHTNESS_1},
    {"AROMA_ICON_BRIGHTNESS_2", AROMA_ICON_BRIGHTNESS_2},
    {"AROMA_ICON_BRIGHTNESS_3", AROMA_ICON_BRIGHTNESS_3},
    {"AROMA_ICON_BRIGHTNESS_4", AROMA_ICON_BRIGHTNESS_4},
    {"AROMA_ICON_BRIGHTNESS_5", AROMA_ICON_BRIGHTNESS_5},
    {"AROMA_ICON_BRIGHTNESS_6", AROMA_ICON_BRIGHTNESS_6},
    {"AROMA_ICON_BRIGHTNESS_7", AROMA_ICON_BRIGHTNESS_7},
    {"AROMA_ICON_BRIGHTNESS_AUTO", AROMA_ICON_BRIGHTNESS_AUTO},
    {"AROMA_ICON_BRIGHTNESS_HIGH", AROMA_ICON_BRIGHTNESS_HIGH},
    {"AROMA_ICON_BRIGHTNESS_LOW", AROMA_ICON_BRIGHTNESS_LOW},
    {"AROMA_ICON_BRIGHTNESS_MEDIUM", AROMA_ICON_BRIGHTNESS_MEDIUM},
    {"AROMA_ICON_BROKEN_IMAGE", AROMA_ICON_BROKEN_IMAGE},
    {"AROMA_ICON_BRUSH", AROMA_ICON_BRUSH},
    {"AROMA_ICON_BUBBLE_CHART", AROMA_ICON_BUBBLE_CHART},
    {"AROMA_ICON_BUG_REPORT", AROMA_ICON_BUG_REPORT},
    {"AROMA_ICON_BUILD", AROMA_ICON_BUILD},
    {"AROMA_ICON_BURST_MODE", AROMA_ICON_BURST_MODE},
    {"AROMA_ICON_BUSINESS", AROMA_ICON_BUSINESS},
    {"AROMA_ICON_BUSINESS_CENTER", AROMA_ICON_BUSINESS_CENTER},
    {"AROMA_ICON_CACHED", AROMA_ICON_CACHED},
    {"AROMA_ICON_CAKE", AROMA_ICON_CAKE},
    {"AROMA_ICON_CALL", AROMA_ICON_CALL},
    {"AROMA_ICON_CALL_END", AROMA_ICON_CALL_END},
    {"AROMA_ICON_CALL_MADE", AROMA_ICON_CALL_MADE},
    {"AROMA_ICON_CALL_MERGE", AROMA_ICON_CALL_MERGE},
    {"AROMA_ICON_CALL_MISSED", AROMA_ICON_CALL_MISSED},
    {"AROMA_ICON_CALL_MISSED_OUTGOING", AROMA_ICON_CALL_MISSED_OUTGOING},
    {"AROMA_ICON_CALL_RECEIVED", AROMA_ICON_CALL_RECEIVED},
    {"AROMA_ICON_CALL_SPLIT", AROMA_ICON_CALL_SPLIT},
    {"AROMA_ICON_CALL_TO_ACTION", AROMA_ICON_CALL_TO_ACTION},
    {"AROMA_ICON_CAMERA", AROMA_ICON_CAMERA},
    {"AROMA_ICON_CAMERA_ALT", AROMA_ICON_CAMERA_ALT},
    {"AROMA_ICON_CAMERA_ENHANCE", AROMA_ICON_CAMERA_ENHANCE},
    {"AROMA_ICON_CAMERA_FRONT", AROMA_ICON_CAMERA_FRONT},
    {"AROMA_ICON_CAMERA_REAR", AROMA_ICON_CAMERA_REAR},
    {"AROMA_ICON_CAMERA_ROLL", AROMA_ICON_CAMERA_ROLL},
    {"AROMA_ICON_CANCEL", AROMA_ICON_CANCEL},
    {"AROMA_ICON_CARD_GIFTCARD", AROMA_ICON_CARD_GIFTCARD},
    {"AROMA_ICON_CARD_MEMBERSHIP", AROMA_ICON_CARD_MEMBERSHIP},
    {"AROMA_ICON_CARD_TRAVEL", AROMA_ICON_CARD_TRAVEL},
    {"AROMA_ICON_CASINO", AROMA_ICON_CASINO},
    {"AROMA_ICON_CAST", AROMA_ICON_CAST},
    {"AROMA_ICON_CAST_CONNECTED", AROMA_ICON_CAST_CONNECTED},
    {"AROMA_ICON_CENTER_FOCUS_STRONG", AROMA_ICON_CENTER_FOCUS_STRONG},
    {"AROMA_ICON_CENTER_FOCUS_WEAK", AROMA_ICON_CENTER_FOCUS_WEAK},
    {"AROMA_ICON_CHANGE_HISTORY", AROMA_ICON_CHANGE_HISTORY},
    {"AROMA_ICON_CHAT", AROMA_ICON_CHAT},
    {"AROMA_ICON_CHAT_BUBBLE", AROMA_ICON_CHAT_BUBBLE},
    {"AROMA_ICON_CHAT_BUBBLE_OUTLINE", AROMA_ICON_CHAT_BUBBLE_OUTLINE},
    {"AROMA_ICON_CHECK", AROMA_ICON_CHECK},
    {"AROMA_ICON_CHECK_BOX", AROMA_ICON_CHECK_BOX},
    {"AROMA_ICON_CHECK_BOX_OUTLINE_BLANK", AROMA_ICON_CHECK_BOX_OUTLINE_BLANK},
    {"AROMA_ICON_CHECK_CIRCLE", AROMA_ICON_CHECK_CIRCLE},
    {"AROMA_ICON_CHEVRON_LEFT", AROMA_ICON_CHEVRON_LEFT},
    {"AROMA_ICON_CHEVRON_RIGHT", AROMA_ICON_CHEVRON_RIGHT},
    {"AROMA_ICON_CHILD_CARE", AROMA_ICON_CHILD_CARE},
    {"AROMA_ICON_CHILD_FRIENDLY", AROMA_ICON_CHILD_FRIENDLY},
    {"AROMA_ICON_CHROME_READER_MODE", AROMA_ICON_CHROME_READER_MODE},
    {"AROMA_ICON_CLASS", AROMA_ICON_CLASS},
    {"AROMA_ICON_CLEAR", AROMA_ICON_CLEAR},
    {"AROMA_ICON_CLEAR_ALL", AROMA_ICON_CLEAR_ALL},
    {"AROMA_ICON_CLOSE", AROMA_ICON_CLOSE},
    {"AROMA_ICON_CLOSED_CAPTION", AROMA_ICON_CLOSED_CAPTION},
    {"AROMA_ICON_CLOUD", AROMA_ICON_CLOUD},
    {"AROMA_ICON_CLOUD_CIRCLE", AROMA_ICON_CLOUD_CIRCLE},
    {"AROMA_ICON_CLOUD_DONE", AROMA_ICON_CLOUD_DONE},
    {"AROMA_ICON_CLOUD_DOWNLOAD", AROMA_ICON_CLOUD_DOWNLOAD},
    {"AROMA_ICON_CLOUD_OFF", AROMA_ICON_CLOUD_OFF},
    {"AROMA_ICON_CLOUD_QUEUE", AROMA_ICON_CLOUD_QUEUE},
    {"AROMA_ICON_CLOUD_UPLOAD", AROMA_ICON_CLOUD_UPLOAD},
    {"AROMA_ICON_CODE", AROMA_ICON_CODE},
    {"AROMA_ICON_COLLECTIONS", AROMA_ICON_COLLECTIONS},
    {"AROMA_ICON_COLLECTIONS_BOOKMARK", AROMA_ICON_COLLECTIONS_BOOKMARK},
    {"AROMA_ICON_COLOR_LENS", AROMA_ICON_COLOR_LENS},
    {"AROMA_ICON_COLORIZE", AROMA_ICON_COLORIZE},
    {"AROMA_ICON_COMMENT", AROMA_ICON_COMMENT},
    {"AROMA_ICON_COMPARE", AROMA_ICON_COMPARE},
    {"AROMA_ICON_COMPARE_ARROWS", AROMA_ICON_COMPARE_ARROWS},
    {"AROMA_ICON_COMPUTER", AROMA_ICON_COMPUTER},
    {"AROMA_ICON_CONFIRMATION_NUMBER", AROMA_ICON_CONFIRMATION_NUMBER},
    {"AROMA_ICON_CONTACT_MAIL", AROMA_ICON_CONTACT_MAIL},
    {"AROMA_ICON_CONTACT_PHONE", AROMA_ICON_CONTACT_PHONE},
    {"AROMA_ICON_CONTACTS", AROMA_ICON_CONTACTS},
    {"AROMA_ICON_CONTENT_COPY", AROMA_ICON_CONTENT_COPY},
    {"AROMA_ICON_CONTENT_CUT", AROMA_ICON_CONTENT_CUT},
    {"AROMA_ICON_CONTENT_PASTE", AROMA_ICON_CONTENT_PASTE},
    {"AROMA_ICON_CONTROL_POINT", AROMA_ICON_CONTROL_POINT},
    {"AROMA_ICON_CONTROL_POINT_DUPLICATE", AROMA_ICON_CONTROL_POINT_DUPLICATE},
    {"AROMA_ICON_COPYRIGHT", AROMA_ICON_COPYRIGHT},
    {"AROMA_ICON_CREATE", AROMA_ICON_CREATE},
    {"AROMA_ICON_CREATE_NEW_FOLDER", AROMA_ICON_CREATE_NEW_FOLDER},
    {"AROMA_ICON_CREDIT_CARD", AROMA_ICON_CREDIT_CARD},
    {"AROMA_ICON_CROP", AROMA_ICON_CROP},
    {"AROMA_ICON_CROP_16_9", AROMA_ICON_CROP_16_9},
    {"AROMA_ICON_CROP_3_2", AROMA_ICON_CROP_3_2},
    {"AROMA_ICON_CROP_5_4", AROMA_ICON_CROP_5_4},
    {"AROMA_ICON_CROP_7_5", AROMA_ICON_CROP_7_5},
    {"AROMA_ICON_CROP_DIN", AROMA_ICON_CROP_DIN},
    {"AROMA_ICON_CROP_FREE", AROMA_ICON_CROP_FREE},
    {"AROMA_ICON_CROP_LANDSCAPE", AROMA_ICON_CROP_LANDSCAPE},
    {"AROMA_ICON_CROP_ORIGINAL", AROMA_ICON_CROP_ORIGINAL},
    {"AROMA_ICON_CROP_PORTRAIT", AROMA_ICON_CROP_PORTRAIT},
    {"AROMA_ICON_CROP_ROTATE", AROMA_ICON_CROP_ROTATE},
    {"AROMA_ICON_CROP_SQUARE", AROMA_ICON_CROP_SQUARE},
    {"AROMA_ICON_DASHBOARD", AROMA_ICON_DASHBOARD},
    {"AROMA_ICON_DATA_USAGE", AROMA_ICON_DATA_USAGE},
    {"AROMA_ICON_DATE_RANGE", AROMA_ICON_DATE_RANGE},
    {"AROMA_ICON_DEHAZE", AROMA_ICON_DEHAZE},
    {"AROMA_ICON_DELETE", AROMA_ICON_DELETE},
    {"AROMA_ICON_DELETE_FOREVER", AROMA_ICON_DELETE_FOREVER},
    {"AROMA_ICON_DELETE_SWEEP", AROMA_ICON_DELETE_SWEEP},
    {"AROMA_ICON_DESCRIPTION", AROMA_ICON_DESCRIPTION},
    {"AROMA_ICON_DESKTOP_MAC", AROMA_ICON_DESKTOP_MAC},
    {"AROMA_ICON_DESKTOP_WINDOWS", AROMA_ICON_DESKTOP_WINDOWS},
    {"AROMA_ICON_DETAILS", AROMA_ICON_DETAILS},
    {"AROMA_ICON_DEVELOPER_BOARD", AROMA_ICON_DEVELOPER_BOARD},
    {"AROMA_ICON_DEVELOPER_MODE", AROMA_ICON_DEVELOPER_MODE},
    {"AROMA_ICON_DEVICE_HUB", AROMA_ICON_DEVICE_HUB},
    {"AROMA_ICON_DEVICES", AROMA_ICON_DEVICES},
    {"AROMA_ICON_DEVICES_OTHER", AROMA_ICON_DEVICES_OTHER},
    {"AROMA_ICON_DIALER_SIP", AROMA_ICON_DIALER_SIP},
    {"AROMA_ICON_DIALPAD", AROMA_ICON_DIALPAD},
    {"AROMA_ICON_DIRECTIONS", AROMA_ICON_DIRECTIONS},
    {"AROMA_ICON_DIRECTIONS_BIKE", AROMA_ICON_DIRECTIONS_BIKE},
    {"AROMA_ICON_DIRECTIONS_BOAT", AROMA_ICON_DIRECTIONS_BOAT},
    {"AROMA_ICON_DIRECTIONS_BUS", AROMA_ICON_DIRECTIONS_BUS},
    {"AROMA_ICON_DIRECTIONS_CAR", AROMA_ICON_DIRECTIONS_CAR},
    {"AROMA_ICON_DIRECTIONS_RAILWAY", AROMA_ICON_DIRECTIONS_RAILWAY},
    {"AROMA_ICON_DIRECTIONS_RUN", AROMA_ICON_DIRECTIONS_RUN},
    {"AROMA_ICON_DIRECTIONS_SUBWAY", AROMA_ICON_DIRECTIONS_SUBWAY},
    {"AROMA_ICON_DIRECTIONS_TRANSIT", AROMA_ICON_DIRECTIONS_TRANSIT},
    {"AROMA_ICON_DIRECTIONS_WALK", AROMA_ICON_DIRECTIONS_WALK},
    {"AROMA_ICON_DISC_FULL", AROMA_ICON_DISC_FULL},
    {"AROMA_ICON_DNS", AROMA_ICON_DNS},
    {"AROMA_ICON_DO_NOT_DISTURB", AROMA_ICON_DO_NOT_DISTURB},
    {"AROMA_ICON_DO_NOT_DISTURB_ALT", AROMA_ICON_DO_NOT_DISTURB_ALT},
    {"AROMA_ICON_DO_NOT_DISTURB_OFF", AROMA_ICON_DO_NOT_DISTURB_OFF},
    {"AROMA_ICON_DO_NOT_DISTURB_ON", AROMA_ICON_DO_NOT_DISTURB_ON},
    {"AROMA_ICON_DOCK", AROMA_ICON_DOCK},
    {"AROMA_ICON_DOMAIN", AROMA_ICON_DOMAIN},
    {"AROMA_ICON_DONE", AROMA_ICON_DONE},
    {"AROMA_ICON_DONE_ALL", AROMA_ICON_DONE_ALL},
    {"AROMA_ICON_DONUT_LARGE", AROMA_ICON_DONUT_LARGE},
    {"AROMA_ICON_DONUT_SMALL", AROMA_ICON_DONUT_SMALL},
    {"AROMA_ICON_DRAFTS", AROMA_ICON_DRAFTS},
    {"AROMA_ICON_DRAG_HANDLE", AROMA_ICON_DRAG_HANDLE},
    {"AROMA_ICON_DRIVE_ETA", AROMA_ICON_DRIVE_ETA},
    {"AROMA_ICON_DVR", AROMA_ICON_DVR},
    {"AROMA_ICON_EDIT", AROMA_ICON_EDIT},
    {"AROMA_ICON_EDIT_LOCATION", AROMA_ICON_EDIT_LOCATION},
    {"AROMA_ICON_EJECT", AROMA_ICON_EJECT},
    {"AROMA_ICON_EMAIL", AROMA_ICON_EMAIL},
    {"AROMA_ICON_ENHANCED_ENCRYPTION", AROMA_ICON_ENHANCED_ENCRYPTION},
    {"AROMA_ICON_EQUALIZER", AROMA_ICON_EQUALIZER},
    {"AROMA_ICON_ERROR", AROMA_ICON_ERROR},
    {"AROMA_ICON_ERROR_OUTLINE", AROMA_ICON_ERROR_OUTLINE},
    {"AROMA_ICON_EURO_SYMBOL", AROMA_ICON_EURO_SYMBOL},
    {"AROMA_ICON_EV_STATION", AROMA_ICON_EV_STATION},
    {"AROMA_ICON_EVENT", AROMA_ICON_EVENT},
    {"AROMA_ICON_EVENT_AVAILABLE", AROMA_ICON_EVENT_AVAILABLE},
    {"AROMA_ICON_EVENT_BUSY", AROMA_ICON_EVENT_BUSY},
    {"AROMA_ICON_EVENT_NOTE", AROMA_ICON_EVENT_NOTE},
    {"AROMA_ICON_EVENT_SEAT", AROMA_ICON_EVENT_SEAT},
    {"AROMA_ICON_EXIT_TO_APP", AROMA_ICON_EXIT_TO_APP},
    {"AROMA_ICON_EXPAND_LESS", AROMA_ICON_EXPAND_LESS},
    {"AROMA_ICON_EXPAND_MORE", AROMA_ICON_EXPAND_MORE},
    {"AROMA_ICON_EXPLICIT", AROMA_ICON_EXPLICIT},
    {"AROMA_ICON_EXPLORE", AROMA_ICON_EXPLORE},
    {"AROMA_ICON_EXPOSURE", AROMA_ICON_EXPOSURE},
    {"AROMA_ICON_EXPOSURE_NEG_1", AROMA_ICON_EXPOSURE_NEG_1},
    {"AROMA_ICON_EXPOSURE_NEG_2", AROMA_ICON_EXPOSURE_NEG_2},
    {"AROMA_ICON_EXPOSURE_PLUS_1", AROMA_ICON_EXPOSURE_PLUS_1},
    {"AROMA_ICON_EXPOSURE_PLUS_2", AROMA_ICON_EXPOSURE_PLUS_2},
    {"AROMA_ICON_EXPOSURE_ZERO", AROMA_ICON_EXPOSURE_ZERO},
    {"AROMA_ICON_EXTENSION", AROMA_ICON_EXTENSION},
    {"AROMA_ICON_FACE", AROMA_ICON_FACE},
    {"AROMA_ICON_FAST_FORWARD", AROMA_ICON_FAST_FORWARD},
    {"AROMA_ICON_FAST_REWIND", AROMA_ICON_FAST_REWIND},
    {"AROMA_ICON_FAVORITE", AROMA_ICON_FAVORITE},
    {"AROMA_ICON_FAVORITE_BORDER", AROMA_ICON_FAVORITE_BORDER},
    {"AROMA_ICON_FEATURED_PLAY_LIST", AROMA_ICON_FEATURED_PLAY_LIST},
    {"AROMA_ICON_FEATURED_VIDEO", AROMA_ICON_FEATURED_VIDEO},
    {"AROMA_ICON_FEEDBACK", AROMA_ICON_FEEDBACK},
    {"AROMA_ICON_FIBER_DVR", AROMA_ICON_FIBER_DVR},
    {"AROMA_ICON_FIBER_MANUAL_RECORD", AROMA_ICON_FIBER_MANUAL_RECORD},
    {"AROMA_ICON_FIBER_NEW", AROMA_ICON_FIBER_NEW},
    {"AROMA_ICON_FIBER_PIN", AROMA_ICON_FIBER_PIN},
    {"AROMA_ICON_FIBER_SMART_RECORD", AROMA_ICON_FIBER_SMART_RECORD},
    {"AROMA_ICON_FILE_DOWNLOAD", AROMA_ICON_FILE_DOWNLOAD},
    {"AROMA_ICON_FILE_UPLOAD", AROMA_ICON_FILE_UPLOAD},
    {"AROMA_ICON_FILTER", AROMA_ICON_FILTER},
    {"AROMA_ICON_FILTER_1", AROMA_ICON_FILTER_1},
    {"AROMA_ICON_FILTER_2", AROMA_ICON_FILTER_2},
    {"AROMA_ICON_FILTER_3", AROMA_ICON_FILTER_3},
    {"AROMA_ICON_FILTER_4", AROMA_ICON_FILTER_4},
    {"AROMA_ICON_FILTER_5", AROMA_ICON_FILTER_5},
    {"AROMA_ICON_FILTER_6", AROMA_ICON_FILTER_6},
    {"AROMA_ICON_FILTER_7", AROMA_ICON_FILTER_7},
    {"AROMA_ICON_FILTER_8", AROMA_ICON_FILTER_8},
    {"AROMA_ICON_FILTER_9", AROMA_ICON_FILTER_9},
    {"AROMA_ICON_FILTER_9_PLUS", AROMA_ICON_FILTER_9_PLUS},
    {"AROMA_ICON_FILTER_B_AND_W", AROMA_ICON_FILTER_B_AND_W},
    {"AROMA_ICON_FILTER_CENTER_FOCUS", AROMA_ICON_FILTER_CENTER_FOCUS},
    {"AROMA_ICON_FILTER_DRAMA", AROMA_ICON_FILTER_DRAMA},
    {"AROMA_ICON_FILTER_FRAMES", AROMA_ICON_FILTER_FRAMES},
    {"AROMA_ICON_FILTER_HDR", AROMA_ICON_FILTER_HDR},
    {"AROMA_ICON_FILTER_LIST", AROMA_ICON_FILTER_LIST},
    {"AROMA_ICON_FILTER_NONE", AROMA_ICON_FILTER_NONE},
    {"AROMA_ICON_FILTER_TILT_SHIFT", AROMA_ICON_FILTER_TILT_SHIFT},
    {"AROMA_ICON_FILTER_VINTAGE", AROMA_ICON_FILTER_VINTAGE},
    {"AROMA_ICON_FIND_IN_PAGE", AROMA_ICON_FIND_IN_PAGE},
    {"AROMA_ICON_FIND_REPLACE", AROMA_ICON_FIND_REPLACE},
    {"AROMA_ICON_FINGERPRINT", AROMA_ICON_FINGERPRINT},
    {"AROMA_ICON_FIRST_PAGE", AROMA_ICON_FIRST_PAGE},
    {"AROMA_ICON_FITNESS_CENTER", AROMA_ICON_FITNESS_CENTER},
    {"AROMA_ICON_FLAG", AROMA_ICON_FLAG},
    {"AROMA_ICON_FLARE", AROMA_ICON_FLARE},
    {"AROMA_ICON_FLASH_AUTO", AROMA_ICON_FLASH_AUTO},
    {"AROMA_ICON_FLASH_OFF", AROMA_ICON_FLASH_OFF},
    {"AROMA_ICON_FLASH_ON", AROMA_ICON_FLASH_ON},
    {"AROMA_ICON_FLIGHT", AROMA_ICON_FLIGHT},
    {"AROMA_ICON_FLIGHT_LAND", AROMA_ICON_FLIGHT_LAND},
    {"AROMA_ICON_FLIGHT_TAKEOFF", AROMA_ICON_FLIGHT_TAKEOFF},
    {"AROMA_ICON_FLIP", AROMA_ICON_FLIP},
    {"AROMA_ICON_FLIP_TO_BACK", AROMA_ICON_FLIP_TO_BACK},
    {"AROMA_ICON_FLIP_TO_FRONT", AROMA_ICON_FLIP_TO_FRONT},
    {"AROMA_ICON_FOLDER", AROMA_ICON_FOLDER},
    {"AROMA_ICON_FOLDER_OPEN", AROMA_ICON_FOLDER_OPEN},
    {"AROMA_ICON_FOLDER_SHARED", AROMA_ICON_FOLDER_SHARED},
    {"AROMA_ICON_FOLDER_SPECIAL", AROMA_ICON_FOLDER_SPECIAL},
    {"AROMA_ICON_FONT_DOWNLOAD", AROMA_ICON_FONT_DOWNLOAD},
    {"AROMA_ICON_FORMAT_ALIGN_CENTER", AROMA_ICON_FORMAT_ALIGN_CENTER},
    {"AROMA_ICON_FORMAT_ALIGN_JUSTIFY", AROMA_ICON_FORMAT_ALIGN_JUSTIFY},
    {"AROMA_ICON_FORMAT_ALIGN_LEFT", AROMA_ICON_FORMAT_ALIGN_LEFT},
    {"AROMA_ICON_FORMAT_ALIGN_RIGHT", AROMA_ICON_FORMAT_ALIGN_RIGHT},
    {"AROMA_ICON_FORMAT_BOLD", AROMA_ICON_FORMAT_BOLD},
    {"AROMA_ICON_FORMAT_CLEAR", AROMA_ICON_FORMAT_CLEAR},
    {"AROMA_ICON_FORMAT_COLOR_FILL", AROMA_ICON_FORMAT_COLOR_FILL},
    {"AROMA_ICON_FORMAT_COLOR_RESET", AROMA_ICON_FORMAT_COLOR_RESET},
    {"AROMA_ICON_FORMAT_COLOR_TEXT", AROMA_ICON_FORMAT_COLOR_TEXT},
    {"AROMA_ICON_FORMAT_INDENT_DECREASE", AROMA_ICON_FORMAT_INDENT_DECREASE},
    {"AROMA_ICON_FORMAT_INDENT_INCREASE", AROMA_ICON_FORMAT_INDENT_INCREASE},
    {"AROMA_ICON_FORMAT_ITALIC", AROMA_ICON_FORMAT_ITALIC},
    {"AROMA_ICON_FORMAT_LINE_SPACING", AROMA_ICON_FORMAT_LINE_SPACING},
    {"AROMA_ICON_FORMAT_LIST_BULLETED", AROMA_ICON_FORMAT_LIST_BULLETED},
    {"AROMA_ICON_FORMAT_LIST_NUMBERED", AROMA_ICON_FORMAT_LIST_NUMBERED},
    {"AROMA_ICON_FORMAT_PAINT", AROMA_ICON_FORMAT_PAINT},
    {"AROMA_ICON_FORMAT_QUOTE", AROMA_ICON_FORMAT_QUOTE},
    {"AROMA_ICON_FORMAT_SHAPES", AROMA_ICON_FORMAT_SHAPES},
    {"AROMA_ICON_FORMAT_SIZE", AROMA_ICON_FORMAT_SIZE},
    {"AROMA_ICON_FORMAT_STRIKETHROUGH", AROMA_ICON_FORMAT_STRIKETHROUGH},
    {"AROMA_ICON_FORMAT_TEXTDIRECTION_L_TO_R", AROMA_ICON_FORMAT_TEXTDIRECTION_L_TO_R},
    {"AROMA_ICON_FORMAT_TEXTDIRECTION_R_TO_L", AROMA_ICON_FORMAT_TEXTDIRECTION_R_TO_L},
    {"AROMA_ICON_FORMAT_UNDERLINED", AROMA_ICON_FORMAT_UNDERLINED},
    {"AROMA_ICON_FORUM", AROMA_ICON_FORUM},
    {"AROMA_ICON_FORWARD", AROMA_ICON_FORWARD},
    {"AROMA_ICON_FORWARD_10", AROMA_ICON_FORWARD_10},
    {"AROMA_ICON_FORWARD_30", AROMA_ICON_FORWARD_30},
    {"AROMA_ICON_FORWARD_5", AROMA_ICON_FORWARD_5},
    {"AROMA_ICON_FREE_BREAKFAST", AROMA_ICON_FREE_BREAKFAST},
    {"AROMA_ICON_FULLSCREEN", AROMA_ICON_FULLSCREEN},
    {"AROMA_ICON_FULLSCREEN_EXIT", AROMA_ICON_FULLSCREEN_EXIT},
    {"AROMA_ICON_FUNCTIONS", AROMA_ICON_FUNCTIONS},
    {"AROMA_ICON_G_TRANSLATE", AROMA_ICON_G_TRANSLATE},
    {"AROMA_ICON_GAMEPAD", AROMA_ICON_GAMEPAD},
    {"AROMA_ICON_GAMES", AROMA_ICON_GAMES},
    {"AROMA_ICON_GAVEL", AROMA_ICON_GAVEL},
    {"AROMA_ICON_GESTURE", AROMA_ICON_GESTURE},
    {"AROMA_ICON_GET_APP", AROMA_ICON_GET_APP},
    {"AROMA_ICON_GIF", AROMA_ICON_GIF},
    {"AROMA_ICON_GOLF_COURSE", AROMA_ICON_GOLF_COURSE},
    {"AROMA_ICON_GPS_FIXED", AROMA_ICON_GPS_FIXED},
    {"AROMA_ICON_GPS_NOT_FIXED", AROMA_ICON_GPS_NOT_FIXED},
    {"AROMA_ICON_GPS_OFF", AROMA_ICON_GPS_OFF},
    {"AROMA_ICON_GRADE", AROMA_ICON_GRADE},
    {"AROMA_ICON_GRADIENT", AROMA_ICON_GRADIENT},
    {"AROMA_ICON_GRAIN", AROMA_ICON_GRAIN},
    {"AROMA_ICON_GRAPHIC_EQ", AROMA_ICON_GRAPHIC_EQ},
    {"AROMA_ICON_GRID_OFF", AROMA_ICON_GRID_OFF},
    {"AROMA_ICON_GRID_ON", AROMA_ICON_GRID_ON},
    {"AROMA_ICON_GROUP", AROMA_ICON_GROUP},
    {"AROMA_ICON_GROUP_ADD", AROMA_ICON_GROUP_ADD},
    {"AROMA_ICON_GROUP_WORK", AROMA_ICON_GROUP_WORK},
    {"AROMA_ICON_HD", AROMA_ICON_HD},
    {"AROMA_ICON_HDR_OFF", AROMA_ICON_HDR_OFF},
    {"AROMA_ICON_HDR_ON", AROMA_ICON_HDR_ON},
    {"AROMA_ICON_HDR_STRONG", AROMA_ICON_HDR_STRONG},
    {"AROMA_ICON_HDR_WEAK", AROMA_ICON_HDR_WEAK},
    {"AROMA_ICON_HEADSET", AROMA_ICON_HEADSET},
    {"AROMA_ICON_HEADSET_MIC", AROMA_ICON_HEADSET_MIC},
    {"AROMA_ICON_HEALING", AROMA_ICON_HEALING},
    {"AROMA_ICON_HEARING", AROMA_ICON_HEARING},
    {"AROMA_ICON_HELP", AROMA_ICON_HELP},
    {"AROMA_ICON_HELP_OUTLINE", AROMA_ICON_HELP_OUTLINE},
    {"AROMA_ICON_HIGH_QUALITY", AROMA_ICON_HIGH_QUALITY},
    {"AROMA_ICON_HIGHLIGHT", AROMA_ICON_HIGHLIGHT},
    {"AROMA_ICON_HIGHLIGHT_OFF", AROMA_ICON_HIGHLIGHT_OFF},
    {"AROMA_ICON_HISTORY", AROMA_ICON_HISTORY},
    {"AROMA_ICON_HOME", AROMA_ICON_HOME},
    {"AROMA_ICON_HOTEL", AROMA_ICON_HOTEL},
    {"AROMA_ICON_HOT_TUB", AROMA_ICON_HOT_TUB},
    {"AROMA_ICON_HOURGLASS_EMPTY", AROMA_ICON_HOURGLASS_EMPTY},
    {"AROMA_ICON_HOURGLASS_FULL", AROMA_ICON_HOURGLASS_FULL},
    {"AROMA_ICON_HTTP", AROMA_ICON_HTTP},
    {"AROMA_ICON_HTTPS", AROMA_ICON_HTTPS},
    {"AROMA_ICON_IMAGE", AROMA_ICON_IMAGE},
    {"AROMA_ICON_IMAGE_ASPECT_RATIO", AROMA_ICON_IMAGE_ASPECT_RATIO},
    {"AROMA_ICON_IMPORT_CONTACTS", AROMA_ICON_IMPORT_CONTACTS},
    {"AROMA_ICON_IMPORT_EXPORT", AROMA_ICON_IMPORT_EXPORT},
    {"AROMA_ICON_IMPORTANT_DEVICES", AROMA_ICON_IMPORTANT_DEVICES},
    {"AROMA_ICON_INBOX", AROMA_ICON_INBOX},
    {"AROMA_ICON_INDETERMINATE_CHECK_BOX", AROMA_ICON_INDETERMINATE_CHECK_BOX},
    {"AROMA_ICON_INFO", AROMA_ICON_INFO},
    {"AROMA_ICON_INFO_OUTLINE", AROMA_ICON_INFO_OUTLINE},
    {"AROMA_ICON_INPUT", AROMA_ICON_INPUT},
    {"AROMA_ICON_INSERT_CHART", AROMA_ICON_INSERT_CHART},
    {"AROMA_ICON_INSERT_COMMENT", AROMA_ICON_INSERT_COMMENT},
    {"AROMA_ICON_INSERT_DRIVE_FILE", AROMA_ICON_INSERT_DRIVE_FILE},
    {"AROMA_ICON_INSERT_EMOTICON", AROMA_ICON_INSERT_EMOTICON},
    {"AROMA_ICON_INSERT_INVITATION", AROMA_ICON_INSERT_INVITATION},
    {"AROMA_ICON_INSERT_LINK", AROMA_ICON_INSERT_LINK},
    {"AROMA_ICON_INSERT_PHOTO", AROMA_ICON_INSERT_PHOTO},
    {"AROMA_ICON_INVERT_COLORS", AROMA_ICON_INVERT_COLORS},
    {"AROMA_ICON_INVERT_COLORS_OFF", AROMA_ICON_INVERT_COLORS_OFF},
    {"AROMA_ICON_ISO", AROMA_ICON_ISO},
    {"AROMA_ICON_KEYBOARD", AROMA_ICON_KEYBOARD},
    {"AROMA_ICON_KEYBOARD_ARROW_DOWN", AROMA_ICON_KEYBOARD_ARROW_DOWN},
    {"AROMA_ICON_KEYBOARD_ARROW_LEFT", AROMA_ICON_KEYBOARD_ARROW_LEFT},
    {"AROMA_ICON_KEYBOARD_ARROW_RIGHT", AROMA_ICON_KEYBOARD_ARROW_RIGHT},
    {"AROMA_ICON_KEYBOARD_ARROW_UP", AROMA_ICON_KEYBOARD_ARROW_UP},
    {"AROMA_ICON_KEYBOARD_BACKSPACE", AROMA_ICON_KEYBOARD_BACKSPACE},
    {"AROMA_ICON_KEYBOARD_CAPSLOCK", AROMA_ICON_KEYBOARD_CAPSLOCK},
    {"AROMA_ICON_KEYBOARD_HIDE", AROMA_ICON_KEYBOARD_HIDE},
    {"AROMA_ICON_KEYBOARD_RETURN", AROMA_ICON_KEYBOARD_RETURN},
    {"AROMA_ICON_KEYBOARD_TAB", AROMA_ICON_KEYBOARD_TAB},
    {"AROMA_ICON_KEYBOARD_VOICE", AROMA_ICON_KEYBOARD_VOICE},
    {"AROMA_ICON_KITCHEN", AROMA_ICON_KITCHEN},
    {"AROMA_ICON_LABEL", AROMA_ICON_LABEL},
    {"AROMA_ICON_LABEL_OUTLINE", AROMA_ICON_LABEL_OUTLINE},
    {"AROMA_ICON_LANDSCAPE", AROMA_ICON_LANDSCAPE},
    {"AROMA_ICON_LANGUAGE", AROMA_ICON_LANGUAGE},
    {"AROMA_ICON_LAPTOP", AROMA_ICON_LAPTOP},
    {"AROMA_ICON_LAPTOP_CHROMEBOOK", AROMA_ICON_LAPTOP_CHROMEBOOK},
    {"AROMA_ICON_LAPTOP_MAC", AROMA_ICON_LAPTOP_MAC},
    {"AROMA_ICON_LAPTOP_WINDOWS", AROMA_ICON_LAPTOP_WINDOWS},
    {"AROMA_ICON_LAST_PAGE", AROMA_ICON_LAST_PAGE},
    {"AROMA_ICON_LAUNCH", AROMA_ICON_LAUNCH},
    {"AROMA_ICON_LAYERS", AROMA_ICON_LAYERS},
    {"AROMA_ICON_LAYERS_CLEAR", AROMA_ICON_LAYERS_CLEAR},
    {"AROMA_ICON_LEAK_ADD", AROMA_ICON_LEAK_ADD},
    {"AROMA_ICON_LEAK_REMOVE", AROMA_ICON_LEAK_REMOVE},
    {"AROMA_ICON_LENS", AROMA_ICON_LENS},
    {"AROMA_ICON_LIBRARY_ADD", AROMA_ICON_LIBRARY_ADD},
    {"AROMA_ICON_LIBRARY_BOOKS", AROMA_ICON_LIBRARY_BOOKS},
    {"AROMA_ICON_LIBRARY_MUSIC", AROMA_ICON_LIBRARY_MUSIC},
    {"AROMA_ICON_LIGHTBULB_OUTLINE", AROMA_ICON_LIGHTBULB_OUTLINE},
    {"AROMA_ICON_LINE_STYLE", AROMA_ICON_LINE_STYLE},
    {"AROMA_ICON_LINE_WEIGHT", AROMA_ICON_LINE_WEIGHT},
    {"AROMA_ICON_LINEAR_SCALE", AROMA_ICON_LINEAR_SCALE},
    {"AROMA_ICON_LINK", AROMA_ICON_LINK},
    {"AROMA_ICON_LINKED_CAMERA", AROMA_ICON_LINKED_CAMERA},
    {"AROMA_ICON_LIST", AROMA_ICON_LIST},
    {"AROMA_ICON_LIVE_HELP", AROMA_ICON_LIVE_HELP},
    {"AROMA_ICON_LIVE_TV", AROMA_ICON_LIVE_TV},
    {"AROMA_ICON_LOCAL_ACTIVITY", AROMA_ICON_LOCAL_ACTIVITY},
    {"AROMA_ICON_LOCAL_AIRPORT", AROMA_ICON_LOCAL_AIRPORT},
    {"AROMA_ICON_LOCAL_ATM", AROMA_ICON_LOCAL_ATM},
    {"AROMA_ICON_LOCAL_BAR", AROMA_ICON_LOCAL_BAR},
    {"AROMA_ICON_LOCAL_CAFE", AROMA_ICON_LOCAL_CAFE},
    {"AROMA_ICON_LOCAL_CAR_WASH", AROMA_ICON_LOCAL_CAR_WASH},
    {"AROMA_ICON_LOCAL_CONVENIENCE_STORE", AROMA_ICON_LOCAL_CONVENIENCE_STORE},
    {"AROMA_ICON_LOCAL_DINING", AROMA_ICON_LOCAL_DINING},
    {"AROMA_ICON_LOCAL_DRINK", AROMA_ICON_LOCAL_DRINK},
    {"AROMA_ICON_LOCAL_FLORIST", AROMA_ICON_LOCAL_FLORIST},
    {"AROMA_ICON_LOCAL_GAS_STATION", AROMA_ICON_LOCAL_GAS_STATION},
    {"AROMA_ICON_LOCAL_GROCERY_STORE", AROMA_ICON_LOCAL_GROCERY_STORE},
    {"AROMA_ICON_LOCAL_HOSPITAL", AROMA_ICON_LOCAL_HOSPITAL},
    {"AROMA_ICON_LOCAL_HOTEL", AROMA_ICON_LOCAL_HOTEL},
    {"AROMA_ICON_LOCAL_LAUNDRY_SERVICE", AROMA_ICON_LOCAL_LAUNDRY_SERVICE},
    {"AROMA_ICON_LOCAL_LIBRARY", AROMA_ICON_LOCAL_LIBRARY},
    {"AROMA_ICON_LOCAL_MALL", AROMA_ICON_LOCAL_MALL},
    {"AROMA_ICON_LOCAL_MOVIES", AROMA_ICON_LOCAL_MOVIES},
    {"AROMA_ICON_LOCAL_OFFER", AROMA_ICON_LOCAL_OFFER},
    {"AROMA_ICON_LOCAL_PARKING", AROMA_ICON_LOCAL_PARKING},
    {"AROMA_ICON_LOCAL_PHARMACY", AROMA_ICON_LOCAL_PHARMACY},
    {"AROMA_ICON_LOCAL_PHONE", AROMA_ICON_LOCAL_PHONE},
    {"AROMA_ICON_LOCAL_PIZZA", AROMA_ICON_LOCAL_PIZZA},
    {"AROMA_ICON_LOCAL_PLAY", AROMA_ICON_LOCAL_PLAY},
    {"AROMA_ICON_LOCAL_POST_OFFICE", AROMA_ICON_LOCAL_POST_OFFICE},
    {"AROMA_ICON_LOCAL_PRINTSHOP", AROMA_ICON_LOCAL_PRINTSHOP},
    {"AROMA_ICON_LOCAL_SEE", AROMA_ICON_LOCAL_SEE},
    {"AROMA_ICON_LOCAL_SHIPPING", AROMA_ICON_LOCAL_SHIPPING},
    {"AROMA_ICON_LOCAL_TAXI", AROMA_ICON_LOCAL_TAXI},
    {"AROMA_ICON_LOCATION_CITY", AROMA_ICON_LOCATION_CITY},
    {"AROMA_ICON_LOCATION_DISABLED", AROMA_ICON_LOCATION_DISABLED},
    {"AROMA_ICON_LOCATION_OFF", AROMA_ICON_LOCATION_OFF},
    {"AROMA_ICON_LOCATION_ON", AROMA_ICON_LOCATION_ON},
    {"AROMA_ICON_LOCATION_SEARCHING", AROMA_ICON_LOCATION_SEARCHING},
    {"AROMA_ICON_LOCK", AROMA_ICON_LOCK},
    {"AROMA_ICON_LOCK_OPEN", AROMA_ICON_LOCK_OPEN},
    {"AROMA_ICON_LOCK_OUTLINE", AROMA_ICON_LOCK_OUTLINE},
    {"AROMA_ICON_LOOKS", AROMA_ICON_LOOKS},
    {"AROMA_ICON_LOOKS_3", AROMA_ICON_LOOKS_3},
    {"AROMA_ICON_LOOKS_4", AROMA_ICON_LOOKS_4},
    {"AROMA_ICON_LOOKS_5", AROMA_ICON_LOOKS_5},
    {"AROMA_ICON_LOOKS_6", AROMA_ICON_LOOKS_6},
    {"AROMA_ICON_LOOKS_ONE", AROMA_ICON_LOOKS_ONE},
    {"AROMA_ICON_LOOKS_TWO", AROMA_ICON_LOOKS_TWO},
    {"AROMA_ICON_LOOP", AROMA_ICON_LOOP},
    {"AROMA_ICON_LOUPE", AROMA_ICON_LOUPE},
    {"AROMA_ICON_LOW_PRIORITY", AROMA_ICON_LOW_PRIORITY},
    {"AROMA_ICON_LOYALTY", AROMA_ICON_LOYALTY},
    {"AROMA_ICON_MAIL", AROMA_ICON_MAIL},
    {"AROMA_ICON_MAIL_OUTLINE", AROMA_ICON_MAIL_OUTLINE},
    {"AROMA_ICON_MAP", AROMA_ICON_MAP},
    {"AROMA_ICON_MARKUNREAD", AROMA_ICON_MARKUNREAD},
    {"AROMA_ICON_MARKUNREAD_MAILBOX", AROMA_ICON_MARKUNREAD_MAILBOX},
    {"AROMA_ICON_MEMORY", AROMA_ICON_MEMORY},
    {"AROMA_ICON_MENU", AROMA_ICON_MENU},
    {"AROMA_ICON_MERGE_TYPE", AROMA_ICON_MERGE_TYPE},
    {"AROMA_ICON_MESSAGE", AROMA_ICON_MESSAGE},
    {"AROMA_ICON_MIC", AROMA_ICON_MIC},
    {"AROMA_ICON_MIC_NONE", AROMA_ICON_MIC_NONE},
    {"AROMA_ICON_MIC_OFF", AROMA_ICON_MIC_OFF},
    {"AROMA_ICON_MMS", AROMA_ICON_MMS},
    {"AROMA_ICON_MODE_COMMENT", AROMA_ICON_MODE_COMMENT},
    {"AROMA_ICON_MODE_EDIT", AROMA_ICON_MODE_EDIT},
    {"AROMA_ICON_MONETIZATION_ON", AROMA_ICON_MONETIZATION_ON},
    {"AROMA_ICON_MONEY_OFF", AROMA_ICON_MONEY_OFF},
    {"AROMA_ICON_MONOCHROME_PHOTOS", AROMA_ICON_MONOCHROME_PHOTOS},
    {"AROMA_ICON_MOOD", AROMA_ICON_MOOD},
    {"AROMA_ICON_MOOD_BAD", AROMA_ICON_MOOD_BAD},
    {"AROMA_ICON_MORE", AROMA_ICON_MORE},
    {"AROMA_ICON_MORE_HORIZ", AROMA_ICON_MORE_HORIZ},
    {"AROMA_ICON_MORE_VERT", AROMA_ICON_MORE_VERT},
    {"AROMA_ICON_MOTORCYCLE", AROMA_ICON_MOTORCYCLE},
    {"AROMA_ICON_MOUSE", AROMA_ICON_MOUSE},
    {"AROMA_ICON_MOVE_TO_INBOX", AROMA_ICON_MOVE_TO_INBOX},
    {"AROMA_ICON_MOVIE", AROMA_ICON_MOVIE},
    {"AROMA_ICON_MOVIE_CREATION", AROMA_ICON_MOVIE_CREATION},
    {"AROMA_ICON_MOVIE_FILTER", AROMA_ICON_MOVIE_FILTER},
    {"AROMA_ICON_MULTILINE_CHART", AROMA_ICON_MULTILINE_CHART},
    {"AROMA_ICON_MUSIC_NOTE", AROMA_ICON_MUSIC_NOTE},
    {"AROMA_ICON_MUSIC_VIDEO", AROMA_ICON_MUSIC_VIDEO},
    {"AROMA_ICON_MY_LOCATION", AROMA_ICON_MY_LOCATION},
    {"AROMA_ICON_NATURE", AROMA_ICON_NATURE},
    {"AROMA_ICON_NATURE_PEOPLE", AROMA_ICON_NATURE_PEOPLE},
    {"AROMA_ICON_NAVIGATE_BEFORE", AROMA_ICON_NAVIGATE_BEFORE},
    {"AROMA_ICON_NAVIGATE_NEXT", AROMA_ICON_NAVIGATE_NEXT},
    {"AROMA_ICON_NAVIGATION", AROMA_ICON_NAVIGATION},
    {"AROMA_ICON_NEAR_ME", AROMA_ICON_NEAR_ME},
    {"AROMA_ICON_NETWORK_CELL", AROMA_ICON_NETWORK_CELL},
    {"AROMA_ICON_NETWORK_CHECK", AROMA_ICON_NETWORK_CHECK},
    {"AROMA_ICON_NETWORK_LOCKED", AROMA_ICON_NETWORK_LOCKED},
    {"AROMA_ICON_NETWORK_WIFI", AROMA_ICON_NETWORK_WIFI},
    {"AROMA_ICON_NEW_RELEASES", AROMA_ICON_NEW_RELEASES},
    {"AROMA_ICON_NEXT_WEEK", AROMA_ICON_NEXT_WEEK},
    {"AROMA_ICON_NFC", AROMA_ICON_NFC},
    {"AROMA_ICON_NO_ENCRYPTION", AROMA_ICON_NO_ENCRYPTION},
    {"AROMA_ICON_NO_SIM", AROMA_ICON_NO_SIM},
    {"AROMA_ICON_NOT_INTERESTED", AROMA_ICON_NOT_INTERESTED},
    {"AROMA_ICON_NOTE", AROMA_ICON_NOTE},
    {"AROMA_ICON_NOTE_ADD", AROMA_ICON_NOTE_ADD},
    {"AROMA_ICON_NOTIFICATIONS", AROMA_ICON_NOTIFICATIONS},
    {"AROMA_ICON_NOTIFICATIONS_ACTIVE", AROMA_ICON_NOTIFICATIONS_ACTIVE},
    {"AROMA_ICON_NOTIFICATIONS_NONE", AROMA_ICON_NOTIFICATIONS_NONE},
    {"AROMA_ICON_NOTIFICATIONS_OFF", AROMA_ICON_NOTIFICATIONS_OFF},
    {"AROMA_ICON_NOTIFICATIONS_PAUSED", AROMA_ICON_NOTIFICATIONS_PAUSED},
    {"AROMA_ICON_OFFLINE_PIN", AROMA_ICON_OFFLINE_PIN},
    {"AROMA_ICON_ONDEMAND_VIDEO", AROMA_ICON_ONDEMAND_VIDEO},
    {"AROMA_ICON_OPACITY", AROMA_ICON_OPACITY},
    {"AROMA_ICON_OPEN_IN_BROWSER", AROMA_ICON_OPEN_IN_BROWSER},
    {"AROMA_ICON_OPEN_IN_NEW", AROMA_ICON_OPEN_IN_NEW},
    {"AROMA_ICON_OPEN_WITH", AROMA_ICON_OPEN_WITH},
    {"AROMA_ICON_PAGES", AROMA_ICON_PAGES},
    {"AROMA_ICON_PAGEVIEW", AROMA_ICON_PAGEVIEW},
    {"AROMA_ICON_PALETTE", AROMA_ICON_PALETTE},
    {"AROMA_ICON_PAN_TOOL", AROMA_ICON_PAN_TOOL},
    {"AROMA_ICON_PANORAMA", AROMA_ICON_PANORAMA},
    {"AROMA_ICON_PANORAMA_FISH_EYE", AROMA_ICON_PANORAMA_FISH_EYE},
    {"AROMA_ICON_PANORAMA_HORIZONTAL", AROMA_ICON_PANORAMA_HORIZONTAL},
    {"AROMA_ICON_PANORAMA_VERTICAL", AROMA_ICON_PANORAMA_VERTICAL},
    {"AROMA_ICON_PANORAMA_WIDE_ANGLE", AROMA_ICON_PANORAMA_WIDE_ANGLE},
    {"AROMA_ICON_PARTY_MODE", AROMA_ICON_PARTY_MODE},
    {"AROMA_ICON_PAUSE", AROMA_ICON_PAUSE},
    {"AROMA_ICON_PAUSE_CIRCLE_FILLED", AROMA_ICON_PAUSE_CIRCLE_FILLED},
    {"AROMA_ICON_PAUSE_CIRCLE_OUTLINE", AROMA_ICON_PAUSE_CIRCLE_OUTLINE},
    {"AROMA_ICON_PAYMENT", AROMA_ICON_PAYMENT},
    {"AROMA_ICON_PEOPLE", AROMA_ICON_PEOPLE},
    {"AROMA_ICON_PEOPLE_OUTLINE", AROMA_ICON_PEOPLE_OUTLINE},
    {"AROMA_ICON_PERM_CAMERA_MIC", AROMA_ICON_PERM_CAMERA_MIC},
    {"AROMA_ICON_PERM_CONTACT_CALENDAR", AROMA_ICON_PERM_CONTACT_CALENDAR},
    {"AROMA_ICON_PERM_DATA_SETTING", AROMA_ICON_PERM_DATA_SETTING},
    {"AROMA_ICON_PERM_DEVICE_INFORMATION", AROMA_ICON_PERM_DEVICE_INFORMATION},
    {"AROMA_ICON_PERM_IDENTITY", AROMA_ICON_PERM_IDENTITY},
    {"AROMA_ICON_PERM_MEDIA", AROMA_ICON_PERM_MEDIA},
    {"AROMA_ICON_PERM_PHONE_MSG", AROMA_ICON_PERM_PHONE_MSG},
    {"AROMA_ICON_PERM_SCAN_WIFI", AROMA_ICON_PERM_SCAN_WIFI},
    {"AROMA_ICON_PERSON", AROMA_ICON_PERSON},
    {"AROMA_ICON_PERSON_ADD", AROMA_ICON_PERSON_ADD},
    {"AROMA_ICON_PERSON_OUTLINE", AROMA_ICON_PERSON_OUTLINE},
    {"AROMA_ICON_PERSON_PIN", AROMA_ICON_PERSON_PIN},
    {"AROMA_ICON_PERSON_PIN_CIRCLE", AROMA_ICON_PERSON_PIN_CIRCLE},
    {"AROMA_ICON_PERSONAL_VIDEO", AROMA_ICON_PERSONAL_VIDEO},
    {"AROMA_ICON_PETS", AROMA_ICON_PETS},
    {"AROMA_ICON_PHONE", AROMA_ICON_PHONE},
    {"AROMA_ICON_PHONE_ANDROID", AROMA_ICON_PHONE_ANDROID},
    {"AROMA_ICON_PHONE_BLUETOOTH_SPEAKER", AROMA_ICON_PHONE_BLUETOOTH_SPEAKER},
    {"AROMA_ICON_PHONE_FORWARDED", AROMA_ICON_PHONE_FORWARDED},
    {"AROMA_ICON_PHONE_IN_TALK", AROMA_ICON_PHONE_IN_TALK},
    {"AROMA_ICON_PHONE_IPHONE", AROMA_ICON_PHONE_IPHONE},
    {"AROMA_ICON_PHONE_LOCKED", AROMA_ICON_PHONE_LOCKED},
    {"AROMA_ICON_PHONE_MISSED", AROMA_ICON_PHONE_MISSED},
    {"AROMA_ICON_PHONE_PAUSED", AROMA_ICON_PHONE_PAUSED},
    {"AROMA_ICON_PHONELINK", AROMA_ICON_PHONELINK},
    {"AROMA_ICON_PHONELINK_ERASE", AROMA_ICON_PHONELINK_ERASE},
    {"AROMA_ICON_PHONELINK_LOCK", AROMA_ICON_PHONELINK_LOCK},
    {"AROMA_ICON_PHONELINK_OFF", AROMA_ICON_PHONELINK_OFF},
    {"AROMA_ICON_PHONELINK_RING", AROMA_ICON_PHONELINK_RING},
    {"AROMA_ICON_PHONELINK_SETUP", AROMA_ICON_PHONELINK_SETUP},
    {"AROMA_ICON_PHOTO", AROMA_ICON_PHOTO},
    {"AROMA_ICON_PHOTO_ALBUM", AROMA_ICON_PHOTO_ALBUM},
    {"AROMA_ICON_PHOTO_CAMERA", AROMA_ICON_PHOTO_CAMERA},
    {"AROMA_ICON_PHOTO_FILTER", AROMA_ICON_PHOTO_FILTER},
    {"AROMA_ICON_PHOTO_LIBRARY", AROMA_ICON_PHOTO_LIBRARY},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_ACTUAL", AROMA_ICON_PHOTO_SIZE_SELECT_ACTUAL},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_LARGE", AROMA_ICON_PHOTO_SIZE_SELECT_LARGE},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_SMALL", AROMA_ICON_PHOTO_SIZE_SELECT_SMALL},
    {"AROMA_ICON_PICTURE_AS_PDF", AROMA_ICON_PICTURE_AS_PDF},
    {"AROMA_ICON_PICTURE_IN_PICTURE", AROMA_ICON_PICTURE_IN_PICTURE},
    {"AROMA_ICON_PICTURE_IN_PICTURE_ALT", AROMA_ICON_PICTURE_IN_PICTURE_ALT},
    {"AROMA_ICON_PIE_CHART", AROMA_ICON_PIE_CHART},
    {"AROMA_ICON_PIE_CHART_OUTLINED", AROMA_ICON_PIE_CHART_OUTLINED},
    {"AROMA_ICON_PIN_DROP", AROMA_ICON_PIN_DROP},
    {"AROMA_ICON_PLACE", AROMA_ICON_PLACE},
    {"AROMA_ICON_PLAY_ARROW", AROMA_ICON_PLAY_ARROW},
    {"AROMA_ICON_PLAY_CIRCLE_FILLED", AROMA_ICON_PLAY_CIRCLE_FILLED},
    {"AROMA_ICON_PLAY_CIRCLE_OUTLINE", AROMA_ICON_PLAY_CIRCLE_OUTLINE},
    {"AROMA_ICON_PLAY_FOR_WORK", AROMA_ICON_PLAY_FOR_WORK},
    {"AROMA_ICON_PLAYLIST_ADD", AROMA_ICON_PLAYLIST_ADD},
    {"AROMA_ICON_PLAYLIST_ADD_CHECK", AROMA_ICON_PLAYLIST_ADD_CHECK},
    {"AROMA_ICON_PLAYLIST_PLAY", AROMA_ICON_PLAYLIST_PLAY},
    {"AROMA_ICON_PLUS_ONE", AROMA_ICON_PLUS_ONE},
    {"AROMA_ICON_POLL", AROMA_ICON_POLL},
    {"AROMA_ICON_POLYMER", AROMA_ICON_POLYMER},
    {"AROMA_ICON_POOL", AROMA_ICON_POOL},
    {"AROMA_ICON_PORTABLE_WIFI_OFF", AROMA_ICON_PORTABLE_WIFI_OFF},
    {"AROMA_ICON_PORTRAIT", AROMA_ICON_PORTRAIT},
    {"AROMA_ICON_POWER", AROMA_ICON_POWER},
    {"AROMA_ICON_POWER_INPUT", AROMA_ICON_POWER_INPUT},
    {"AROMA_ICON_POWER_SETTINGS_NEW", AROMA_ICON_POWER_SETTINGS_NEW},
    {"AROMA_ICON_PREGNANT_WOMAN", AROMA_ICON_PREGNANT_WOMAN},
    {"AROMA_ICON_PRESENT_TO_ALL", AROMA_ICON_PRESENT_TO_ALL},
    {"AROMA_ICON_PRINT", AROMA_ICON_PRINT},
    {"AROMA_ICON_PRIORITY_HIGH", AROMA_ICON_PRIORITY_HIGH},
    {"AROMA_ICON_PUBLIC", AROMA_ICON_PUBLIC},
    {"AROMA_ICON_PUBLISH", AROMA_ICON_PUBLISH},
    {"AROMA_ICON_QUERY_BUILDER", AROMA_ICON_QUERY_BUILDER},
    {"AROMA_ICON_QUESTION_ANSWER", AROMA_ICON_QUESTION_ANSWER},
    {"AROMA_ICON_QUEUE", AROMA_ICON_QUEUE},
    {"AROMA_ICON_QUEUE_MUSIC", AROMA_ICON_QUEUE_MUSIC},
    {"AROMA_ICON_QUEUE_PLAY_NEXT", AROMA_ICON_QUEUE_PLAY_NEXT},
    {"AROMA_ICON_RADIO", AROMA_ICON_RADIO},
    {"AROMA_ICON_RADIO_BUTTON_CHECKED", AROMA_ICON_RADIO_BUTTON_CHECKED},
    {"AROMA_ICON_RADIO_BUTTON_UNCHECKED", AROMA_ICON_RADIO_BUTTON_UNCHECKED},
    {"AROMA_ICON_RATE_REVIEW", AROMA_ICON_RATE_REVIEW},
    {"AROMA_ICON_RECEIPT", AROMA_ICON_RECEIPT},
    {"AROMA_ICON_RECENT_ACTORS", AROMA_ICON_RECENT_ACTORS},
    {"AROMA_ICON_RECORD_VOICE_OVER", AROMA_ICON_RECORD_VOICE_OVER},
    {"AROMA_ICON_REDEEM", AROMA_ICON_REDEEM},
    {"AROMA_ICON_REDO", AROMA_ICON_REDO},
    {"AROMA_ICON_REFRESH", AROMA_ICON_REFRESH},
    {"AROMA_ICON_REMOVE", AROMA_ICON_REMOVE},
    {"AROMA_ICON_REMOVE_CIRCLE", AROMA_ICON_REMOVE_CIRCLE},
    {"AROMA_ICON_REMOVE_CIRCLE_OUTLINE", AROMA_ICON_REMOVE_CIRCLE_OUTLINE},
    {"AROMA_ICON_REMOVE_FROM_QUEUE", AROMA_ICON_REMOVE_FROM_QUEUE},
    {"AROMA_ICON_REMOVE_RED_EYE", AROMA_ICON_REMOVE_RED_EYE},
    {"AROMA_ICON_REMOVE_SHOPPING_CART", AROMA_ICON_REMOVE_SHOPPING_CART},
    {"AROMA_ICON_REORDER", AROMA_ICON_REORDER},
    {"AROMA_ICON_REPEAT", AROMA_ICON_REPEAT},
    {"AROMA_ICON_REPEAT_ONE", AROMA_ICON_REPEAT_ONE},
    {"AROMA_ICON_REPLAY", AROMA_ICON_REPLAY},
    {"AROMA_ICON_REPLAY_10", AROMA_ICON_REPLAY_10},
    {"AROMA_ICON_REPLAY_30", AROMA_ICON_REPLAY_30},
    {"AROMA_ICON_REPLAY_5", AROMA_ICON_REPLAY_5},
    {"AROMA_ICON_REPLY", AROMA_ICON_REPLY},
    {"AROMA_ICON_REPLY_ALL", AROMA_ICON_REPLY_ALL},
    {"AROMA_ICON_REPORT", AROMA_ICON_REPORT},
    {"AROMA_ICON_REPORT_PROBLEM", AROMA_ICON_REPORT_PROBLEM},
    {"AROMA_ICON_RESTAURANT", AROMA_ICON_RESTAURANT},
    {"AROMA_ICON_RESTAURANT_MENU", AROMA_ICON_RESTAURANT_MENU},
    {"AROMA_ICON_RESTORE", AROMA_ICON_RESTORE},
    {"AROMA_ICON_RESTORE_PAGE", AROMA_ICON_RESTORE_PAGE},
    {"AROMA_ICON_RING_VOLUME", AROMA_ICON_RING_VOLUME},
    {"AROMA_ICON_ROOM", AROMA_ICON_ROOM},
    {"AROMA_ICON_ROOM_SERVICE", AROMA_ICON_ROOM_SERVICE},
    {"AROMA_ICON_ROTATE_90_DEGREES_CCW", AROMA_ICON_ROTATE_90_DEGREES_CCW},
    {"AROMA_ICON_ROTATE_LEFT", AROMA_ICON_ROTATE_LEFT},
    {"AROMA_ICON_ROTATE_RIGHT", AROMA_ICON_ROTATE_RIGHT},
    {"AROMA_ICON_ROUNDED_CORNER", AROMA_ICON_ROUNDED_CORNER},
    {"AROMA_ICON_ROUTER", AROMA_ICON_ROUTER},
    {"AROMA_ICON_ROWING", AROMA_ICON_ROWING},
    {"AROMA_ICON_RSS_FEED", AROMA_ICON_RSS_FEED},
    {"AROMA_ICON_RV_HOOKUP", AROMA_ICON_RV_HOOKUP},
    {"AROMA_ICON_SATELLITE", AROMA_ICON_SATELLITE},
    {"AROMA_ICON_SAVE", AROMA_ICON_SAVE},
    {"AROMA_ICON_SCANNER", AROMA_ICON_SCANNER},
    {"AROMA_ICON_SCHEDULE", AROMA_ICON_SCHEDULE},
    {"AROMA_ICON_SCHOOL", AROMA_ICON_SCHOOL},
    {"AROMA_ICON_SCREEN_LOCK_LANDSCAPE", AROMA_ICON_SCREEN_LOCK_LANDSCAPE},
    {"AROMA_ICON_SCREEN_LOCK_PORTRAIT", AROMA_ICON_SCREEN_LOCK_PORTRAIT},
    {"AROMA_ICON_SCREEN_LOCK_ROTATION", AROMA_ICON_SCREEN_LOCK_ROTATION},
    {"AROMA_ICON_SCREEN_ROTATION", AROMA_ICON_SCREEN_ROTATION},
    {"AROMA_ICON_SCREEN_SHARE", AROMA_ICON_SCREEN_SHARE},
    {"AROMA_ICON_SD_CARD", AROMA_ICON_SD_CARD},
    {"AROMA_ICON_SD_STORAGE", AROMA_ICON_SD_STORAGE},
    {"AROMA_ICON_SEARCH", AROMA_ICON_SEARCH},
    {"AROMA_ICON_SECURITY", AROMA_ICON_SECURITY},
    {"AROMA_ICON_SELECT_ALL", AROMA_ICON_SELECT_ALL},
    {"AROMA_ICON_SEND", AROMA_ICON_SEND},
    {"AROMA_ICON_SENTIMENT_DISSATISFIED", AROMA_ICON_SENTIMENT_DISSATISFIED},
    {"AROMA_ICON_SENTIMENT_NEUTRAL", AROMA_ICON_SENTIMENT_NEUTRAL},
    {"AROMA_ICON_SENTIMENT_SATISFIED", AROMA_ICON_SENTIMENT_SATISFIED},
    {"AROMA_ICON_SENTIMENT_VERY_DISSATISFIED", AROMA_ICON_SENTIMENT_VERY_DISSATISFIED},
    {"AROMA_ICON_SENTIMENT_VERY_SATISFIED", AROMA_ICON_SENTIMENT_VERY_SATISFIED},
    {"AROMA_ICON_SETTINGS", AROMA_ICON_SETTINGS},
    {"AROMA_ICON_SETTINGS_APPLICATIONS", AROMA_ICON_SETTINGS_APPLICATIONS},
    {"AROMA_ICON_SETTINGS_BACKUP_RESTORE", AROMA_ICON_SETTINGS_BACKUP_RESTORE},
    {"AROMA_ICON_SETTINGS_BLUETOOTH", AROMA_ICON_SETTINGS_BLUETOOTH},
    {"AROMA_ICON_SETTINGS_BRIGHTNESS", AROMA_ICON_SETTINGS_BRIGHTNESS},
    {"AROMA_ICON_SETTINGS_CELL", AROMA_ICON_SETTINGS_CELL},
    {"AROMA_ICON_SETTINGS_ETHERNET", AROMA_ICON_SETTINGS_ETHERNET},
    {"AROMA_ICON_SETTINGS_INPUT_ANTENNA", AROMA_ICON_SETTINGS_INPUT_ANTENNA},
    {"AROMA_ICON_SETTINGS_INPUT_COMPONENT", AROMA_ICON_SETTINGS_INPUT_COMPONENT},
    {"AROMA_ICON_SETTINGS_INPUT_COMPOSITE", AROMA_ICON_SETTINGS_INPUT_COMPOSITE},
    {"AROMA_ICON_SETTINGS_INPUT_HDMI", AROMA_ICON_SETTINGS_INPUT_HDMI},
    {"AROMA_ICON_SETTINGS_INPUT_SVIDEO", AROMA_ICON_SETTINGS_INPUT_SVIDEO},
    {"AROMA_ICON_SETTINGS_OVERSCAN", AROMA_ICON_SETTINGS_OVERSCAN},
    {"AROMA_ICON_SETTINGS_PHONE", AROMA_ICON_SETTINGS_PHONE},
    {"AROMA_ICON_SETTINGS_POWER", AROMA_ICON_SETTINGS_POWER},
    {"AROMA_ICON_SETTINGS_REMOTE", AROMA_ICON_SETTINGS_REMOTE},
    {"AROMA_ICON_SETTINGS_SYSTEM_DAYDREAM", AROMA_ICON_SETTINGS_SYSTEM_DAYDREAM},
    {"AROMA_ICON_SETTINGS_VOICE", AROMA_ICON_SETTINGS_VOICE},
    {"AROMA_ICON_SHARE", AROMA_ICON_SHARE},
    {"AROMA_ICON_SHOP", AROMA_ICON_SHOP},
    {"AROMA_ICON_SHOP_TWO", AROMA_ICON_SHOP_TWO},
    {"AROMA_ICON_SHOPPING_BASKET", AROMA_ICON_SHOPPING_BASKET},
    {"AROMA_ICON_SHOPPING_CART", AROMA_ICON_SHOPPING_CART},
    {"AROMA_ICON_SHORT_TEXT", AROMA_ICON_SHORT_TEXT},
    {"AROMA_ICON_SHOW_CHART", AROMA_ICON_SHOW_CHART},
    {"AROMA_ICON_SHUFFLE", AROMA_ICON_SHUFFLE},
    {"AROMA_ICON_SIGNAL_CELLULAR_4_BAR", AROMA_ICON_SIGNAL_CELLULAR_4_BAR},
    {"AROMA_ICON_SIGNAL_CELLULAR_CONNECTED_NO_INTERNET_4_BAR", AROMA_ICON_SIGNAL_CELLULAR_CONNECTED_NO_INTERNET_4_BAR},
    {"AROMA_ICON_SIGNAL_CELLULAR_NO_SIM", AROMA_ICON_SIGNAL_CELLULAR_NO_SIM},
    {"AROMA_ICON_SIGNAL_CELLULAR_NULL", AROMA_ICON_SIGNAL_CELLULAR_NULL},
    {"AROMA_ICON_SIGNAL_CELLULAR_OFF", AROMA_ICON_SIGNAL_CELLULAR_OFF},
    {"AROMA_ICON_SIGNAL_WIFI_4_BAR", AROMA_ICON_SIGNAL_WIFI_4_BAR},
    {"AROMA_ICON_SIGNAL_WIFI_4_BAR_LOCK", AROMA_ICON_SIGNAL_WIFI_4_BAR_LOCK},
    {"AROMA_ICON_SIGNAL_WIFI_OFF", AROMA_ICON_SIGNAL_WIFI_OFF},
    {"AROMA_ICON_SIM_CARD", AROMA_ICON_SIM_CARD},
    {"AROMA_ICON_SIM_CARD_ALERT", AROMA_ICON_SIM_CARD_ALERT},
    {"AROMA_ICON_SKIP_NEXT", AROMA_ICON_SKIP_NEXT},
    {"AROMA_ICON_SKIP_PREVIOUS", AROMA_ICON_SKIP_PREVIOUS},
    {"AROMA_ICON_SLIDESHOW", AROMA_ICON_SLIDESHOW},
    {"AROMA_ICON_SLOW_MOTION_VIDEO", AROMA_ICON_SLOW_MOTION_VIDEO},
    {"AROMA_ICON_SMARTPHONE", AROMA_ICON_SMARTPHONE},
    {"AROMA_ICON_SMOKE_FREE", AROMA_ICON_SMOKE_FREE},
    {"AROMA_ICON_SMOKING_ROOMS", AROMA_ICON_SMOKING_ROOMS},
    {"AROMA_ICON_SMS", AROMA_ICON_SMS},
    {"AROMA_ICON_SMS_FAILED", AROMA_ICON_SMS_FAILED},
    {"AROMA_ICON_SNOOZE", AROMA_ICON_SNOOZE},
    {"AROMA_ICON_SORT", AROMA_ICON_SORT},
    {"AROMA_ICON_SORT_BY_ALPHA", AROMA_ICON_SORT_BY_ALPHA},
    {"AROMA_ICON_SPA", AROMA_ICON_SPA},
    {"AROMA_ICON_SPACE_BAR", AROMA_ICON_SPACE_BAR},
    {"AROMA_ICON_SPEAKER", AROMA_ICON_SPEAKER},
    {"AROMA_ICON_SPEAKER_GROUP", AROMA_ICON_SPEAKER_GROUP},
    {"AROMA_ICON_SPEAKER_NOTES", AROMA_ICON_SPEAKER_NOTES},
    {"AROMA_ICON_SPEAKER_NOTES_OFF", AROMA_ICON_SPEAKER_NOTES_OFF},
    {"AROMA_ICON_SPEAKER_PHONE", AROMA_ICON_SPEAKER_PHONE},
    {"AROMA_ICON_SPELLCHECK", AROMA_ICON_SPELLCHECK},
    {"AROMA_ICON_STAR", AROMA_ICON_STAR},
    {"AROMA_ICON_STAR_BORDER", AROMA_ICON_STAR_BORDER},
    {"AROMA_ICON_STAR_HALF", AROMA_ICON_STAR_HALF},
    {"AROMA_ICON_STARS", AROMA_ICON_STARS},
    {"AROMA_ICON_STAY_CURRENT_LANDSCAPE", AROMA_ICON_STAY_CURRENT_LANDSCAPE},
    {"AROMA_ICON_STAY_CURRENT_PORTRAIT", AROMA_ICON_STAY_CURRENT_PORTRAIT},
    {"AROMA_ICON_STAY_PRIMARY_LANDSCAPE", AROMA_ICON_STAY_PRIMARY_LANDSCAPE},
    {"AROMA_ICON_STAY_PRIMARY_PORTRAIT", AROMA_ICON_STAY_PRIMARY_PORTRAIT},
    {"AROMA_ICON_STOP", AROMA_ICON_STOP},
    {"AROMA_ICON_STOP_SCREEN_SHARE", AROMA_ICON_STOP_SCREEN_SHARE},
    {"AROMA_ICON_STORAGE", AROMA_ICON_STORAGE},
    {"AROMA_ICON_STORE", AROMA_ICON_STORE},
    {"AROMA_ICON_STORE_MALL_DIRECTORY", AROMA_ICON_STORE_MALL_DIRECTORY},
    {"AROMA_ICON_STRAIGHTEN", AROMA_ICON_STRAIGHTEN},
    {"AROMA_ICON_STREETVIEW", AROMA_ICON_STREETVIEW},
    {"AROMA_ICON_STRIKETHROUGH_S", AROMA_ICON_STRIKETHROUGH_S},
    {"AROMA_ICON_STYLE", AROMA_ICON_STYLE},
    {"AROMA_ICON_SUBDIRECTORY_ARROW_LEFT", AROMA_ICON_SUBDIRECTORY_ARROW_LEFT},
    {"AROMA_ICON_SUBDIRECTORY_ARROW_RIGHT", AROMA_ICON_SUBDIRECTORY_ARROW_RIGHT},
    {"AROMA_ICON_SUBJECT", AROMA_ICON_SUBJECT},
    {"AROMA_ICON_SUBSCRIPTIONS", AROMA_ICON_SUBSCRIPTIONS},
    {"AROMA_ICON_SUBTITLES", AROMA_ICON_SUBTITLES},
    {"AROMA_ICON_SUBWAY", AROMA_ICON_SUBWAY},
    {"AROMA_ICON_SUPERVISOR_ACCOUNT", AROMA_ICON_SUPERVISOR_ACCOUNT},
    {"AROMA_ICON_SURROUND_SOUND", AROMA_ICON_SURROUND_SOUND},
    {"AROMA_ICON_SWAP_CALLS", AROMA_ICON_SWAP_CALLS},
    {"AROMA_ICON_SWAP_HORIZ", AROMA_ICON_SWAP_HORIZ},
    {"AROMA_ICON_SWAP_VERT", AROMA_ICON_SWAP_VERT},
    {"AROMA_ICON_SWAP_VERTICAL_CIRCLE", AROMA_ICON_SWAP_VERTICAL_CIRCLE},
    {"AROMA_ICON_SWITCH_CAMERA", AROMA_ICON_SWITCH_CAMERA},
    {"AROMA_ICON_SWITCH_VIDEO", AROMA_ICON_SWITCH_VIDEO},
    {"AROMA_ICON_SYNC", AROMA_ICON_SYNC},
    {"AROMA_ICON_SYNC_DISABLED", AROMA_ICON_SYNC_DISABLED},
    {"AROMA_ICON_SYNC_PROBLEM", AROMA_ICON_SYNC_PROBLEM},
    {"AROMA_ICON_SYSTEM_UPDATE", AROMA_ICON_SYSTEM_UPDATE},
    {"AROMA_ICON_SYSTEM_UPDATE_ALT", AROMA_ICON_SYSTEM_UPDATE_ALT},
    {"AROMA_ICON_TAB", AROMA_ICON_TAB},
    {"AROMA_ICON_TAB_UNSELECTED", AROMA_ICON_TAB_UNSELECTED},
    {"AROMA_ICON_TABLET", AROMA_ICON_TABLET},
    {"AROMA_ICON_TABLET_ANDROID", AROMA_ICON_TABLET_ANDROID},
    {"AROMA_ICON_TABLET_MAC", AROMA_ICON_TABLET_MAC},
    {"AROMA_ICON_TAG_FACES", AROMA_ICON_TAG_FACES},
    {"AROMA_ICON_TAP_AND_PLAY", AROMA_ICON_TAP_AND_PLAY},
    {"AROMA_ICON_TERRAIN", AROMA_ICON_TERRAIN},
    {"AROMA_ICON_TEXT_FIELDS", AROMA_ICON_TEXT_FIELDS},
    {"AROMA_ICON_TEXT_FORMAT", AROMA_ICON_TEXT_FORMAT},
    {"AROMA_ICON_TEXTSMS", AROMA_ICON_TEXTSMS},
    {"AROMA_ICON_TEXTURE", AROMA_ICON_TEXTURE},
    {"AROMA_ICON_THEATERS", AROMA_ICON_THEATERS},
    {"AROMA_ICON_THUMB_DOWN", AROMA_ICON_THUMB_DOWN},
    {"AROMA_ICON_THUMB_UP", AROMA_ICON_THUMB_UP},
    {"AROMA_ICON_THUMBS_UP_DOWN", AROMA_ICON_THUMBS_UP_DOWN},
    {"AROMA_ICON_TIME_TO_LEAVE", AROMA_ICON_TIME_TO_LEAVE},
    {"AROMA_ICON_TIMELAPSE", AROMA_ICON_TIMELAPSE},
    {"AROMA_ICON_TIMELINE", AROMA_ICON_TIMELINE},
    {"AROMA_ICON_TIMER", AROMA_ICON_TIMER},
    {"AROMA_ICON_TIMER_10", AROMA_ICON_TIMER_10},
    {"AROMA_ICON_TIMER_3", AROMA_ICON_TIMER_3},
    {"AROMA_ICON_TIMER_OFF", AROMA_ICON_TIMER_OFF},
    {"AROMA_ICON_TITLE", AROMA_ICON_TITLE},
    {"AROMA_ICON_TOC", AROMA_ICON_TOC},
    {"AROMA_ICON_TODAY", AROMA_ICON_TODAY},
    {"AROMA_ICON_TOLL", AROMA_ICON_TOLL},
    {"AROMA_ICON_TONALITY", AROMA_ICON_TONALITY},
    {"AROMA_ICON_TOUCH_APP", AROMA_ICON_TOUCH_APP},
    {"AROMA_ICON_TOYS", AROMA_ICON_TOYS},
    {"AROMA_ICON_TRACK_CHANGES", AROMA_ICON_TRACK_CHANGES},
    {"AROMA_ICON_TRAFFIC", AROMA_ICON_TRAFFIC},
    {"AROMA_ICON_TRAIN", AROMA_ICON_TRAIN},
    {"AROMA_ICON_TRAM", AROMA_ICON_TRAM},
    {"AROMA_ICON_TRANSFER_WITHIN_A_STATION", AROMA_ICON_TRANSFER_WITHIN_A_STATION},
    {"AROMA_ICON_TRANSFORM", AROMA_ICON_TRANSFORM},
    {"AROMA_ICON_TRANSLATE", AROMA_ICON_TRANSLATE},
    {"AROMA_ICON_TRENDING_DOWN", AROMA_ICON_TRENDING_DOWN},
    {"AROMA_ICON_TRENDING_FLAT", AROMA_ICON_TRENDING_FLAT},
    {"AROMA_ICON_TRENDING_UP", AROMA_ICON_TRENDING_UP},
    {"AROMA_ICON_TUNE", AROMA_ICON_TUNE},
    {"AROMA_ICON_TURNED_IN", AROMA_ICON_TURNED_IN},
    {"AROMA_ICON_TURNED_IN_NOT", AROMA_ICON_TURNED_IN_NOT},
    {"AROMA_ICON_TV", AROMA_ICON_TV},
    {"AROMA_ICON_UNARCHIVE", AROMA_ICON_UNARCHIVE},
    {"AROMA_ICON_UNDO", AROMA_ICON_UNDO},
    {"AROMA_ICON_UNFOLD_LESS", AROMA_ICON_UNFOLD_LESS},
    {"AROMA_ICON_UNFOLD_MORE", AROMA_ICON_UNFOLD_MORE},
    {"AROMA_ICON_UPDATE", AROMA_ICON_UPDATE},
    {"AROMA_ICON_USB", AROMA_ICON_USB},
    {"AROMA_ICON_VERIFIED_USER", AROMA_ICON_VERIFIED_USER},
    {"AROMA_ICON_VERTICAL_ALIGN_BOTTOM", AROMA_ICON_VERTICAL_ALIGN_BOTTOM},
    {"AROMA_ICON_VERTICAL_ALIGN_CENTER", AROMA_ICON_VERTICAL_ALIGN_CENTER},
    {"AROMA_ICON_VERTICAL_ALIGN_TOP", AROMA_ICON_VERTICAL_ALIGN_TOP},
    {"AROMA_ICON_VIBRATION", AROMA_ICON_VIBRATION},
    {"AROMA_ICON_VIDEO_CALL", AROMA_ICON_VIDEO_CALL},
    {"AROMA_ICON_VIDEO_LABEL", AROMA_ICON_VIDEO_LABEL},
    {"AROMA_ICON_VIDEO_LIBRARY", AROMA_ICON_VIDEO_LIBRARY},
    {"AROMA_ICON_VIDEOCAM", AROMA_ICON_VIDEOCAM},
    {"AROMA_ICON_VIDEOCAM_OFF", AROMA_ICON_VIDEOCAM_OFF},
    {"AROMA_ICON_VIDEOGAME_ASSET", AROMA_ICON_VIDEOGAME_ASSET},
    {"AROMA_ICON_VIEW_AGENDA", AROMA_ICON_VIEW_AGENDA},
    {"AROMA_ICON_VIEW_ARRAY", AROMA_ICON_VIEW_ARRAY},
    {"AROMA_ICON_VIEW_CAROUSEL", AROMA_ICON_VIEW_CAROUSEL},
    {"AROMA_ICON_VIEW_COLUMN", AROMA_ICON_VIEW_COLUMN},
    {"AROMA_ICON_VIEW_COMFY", AROMA_ICON_VIEW_COMFY},
    {"AROMA_ICON_VIEW_COMPACT", AROMA_ICON_VIEW_COMPACT},
    {"AROMA_ICON_VIEW_DAY", AROMA_ICON_VIEW_DAY},
    {"AROMA_ICON_VIEW_HEADLINE", AROMA_ICON_VIEW_HEADLINE},
    {"AROMA_ICON_VIEW_LIST", AROMA_ICON_VIEW_LIST},
    {"AROMA_ICON_VIEW_MODULE", AROMA_ICON_VIEW_MODULE},
    {"AROMA_ICON_VIEW_QUILT", AROMA_ICON_VIEW_QUILT},
    {"AROMA_ICON_VIEW_STREAM", AROMA_ICON_VIEW_STREAM},
    {"AROMA_ICON_VIEW_WEEK", AROMA_ICON_VIEW_WEEK},
    {"AROMA_ICON_VIGNETTE", AROMA_ICON_VIGNETTE},
    {"AROMA_ICON_VISIBILITY", AROMA_ICON_VISIBILITY},
    {"AROMA_ICON_VISIBILITY_OFF", AROMA_ICON_VISIBILITY_OFF},
    {"AROMA_ICON_VOICE_CHAT", AROMA_ICON_VOICE_CHAT},
    {"AROMA_ICON_VOICEMAIL", AROMA_ICON_VOICEMAIL},
    {"AROMA_ICON_VOLUME_DOWN", AROMA_ICON_VOLUME_DOWN},
    {"AROMA_ICON_VOLUME_MUTE", AROMA_ICON_VOLUME_MUTE},
    {"AROMA_ICON_VOLUME_OFF", AROMA_ICON_VOLUME_OFF},
    {"AROMA_ICON_VOLUME_UP", AROMA_ICON_VOLUME_UP},
    {"AROMA_ICON_VPN_KEY", AROMA_ICON_VPN_KEY},
    {"AROMA_ICON_VPN_LOCK", AROMA_ICON_VPN_LOCK},
    {"AROMA_ICON_WALLPAPER", AROMA_ICON_WALLPAPER},
    {"AROMA_ICON_WARNING", AROMA_ICON_WARNING},
    {"AROMA_ICON_WATCH", AROMA_ICON_WATCH},
    {"AROMA_ICON_WATCH_LATER", AROMA_ICON_WATCH_LATER},
    {"AROMA_ICON_WB_AUTO", AROMA_ICON_WB_AUTO},
    {"AROMA_ICON_WB_CLOUDY", AROMA_ICON_WB_CLOUDY},
    {"AROMA_ICON_WB_INCANDESCENT", AROMA_ICON_WB_INCANDESCENT},
    {"AROMA_ICON_WB_IRIDESCENT", AROMA_ICON_WB_IRIDESCENT},
    {"AROMA_ICON_WB_SUNNY", AROMA_ICON_WB_SUNNY},
    {"AROMA_ICON_WC", AROMA_ICON_WC},
    {"AROMA_ICON_WEB", AROMA_ICON_WEB},
    {"AROMA_ICON_WEB_ASSET", AROMA_ICON_WEB_ASSET},
    {"AROMA_ICON_WEEKEND", AROMA_ICON_WEEKEND},
    {"AROMA_ICON_WHATSHOT", AROMA_ICON_WHATSHOT},
    {"AROMA_ICON_WIDGETS", AROMA_ICON_WIDGETS},
    {"AROMA_ICON_WIFI", AROMA_ICON_WIFI},
    {"AROMA_ICON_WIFI_LOCK", AROMA_ICON_WIFI_LOCK},
    {"AROMA_ICON_WIFI_TETHERING", AROMA_ICON_WIFI_TETHERING},
    {"AROMA_ICON_WORK", AROMA_ICON_WORK},
    {"AROMA_ICON_WRAP_TEXT", AROMA_ICON_WRAP_TEXT},
    {"AROMA_ICON_YOUTUBE_SEARCHED_FOR", AROMA_ICON_YOUTUBE_SEARCHED_FOR},
    {"AROMA_ICON_ZOOM_IN", AROMA_ICON_ZOOM_IN},
    {"AROMA_ICON_ZOOM_OUT", AROMA_ICON_ZOOM_OUT},
    {"AROMA_ICON_ZOOM_OUT_MAP", AROMA_ICON_ZOOM_OUT_MAP},
    {NULL, NULL}};

#define ICON_MAP_COUNT (sizeof(ICON_MAP) / sizeof(ICON_MAP[0]) - 1)

typedef struct
{
    int next;
    int idx;
} IconBucket;
static IconBucket s_icon_entries[ICON_MAP_COUNT > 0 ? ICON_MAP_COUNT : 1];
static int s_icon_chain[ICON_HASH_SIZE];
static bool s_icon_init = false;

static void icon_build_table(void)
{
    memset(s_icon_chain, -1, sizeof(s_icon_chain));
    for (int i = 0; i < (int)ICON_MAP_COUNT; i++)
    {
        if (!ICON_MAP[i].name)
            continue;
        uint32_t slot = fnv1a(ICON_MAP[i].name) & (ICON_HASH_SIZE - 1);
        s_icon_entries[i].idx = i;
        s_icon_entries[i].next = s_icon_chain[slot];
        s_icon_chain[slot] = i;
    }
    s_icon_init = true;
}

static const char *resolve_icon(const char *name)
{
    if (!name)
        return NULL;
    if (!s_icon_init)
        icon_build_table();
    size_t len = strlen(name);
    char tmp[128];
    const char *clean = name;
    if (len >= 2 && name[0] == '"' && name[len - 1] == '"' && (len - 2) < sizeof(tmp))
    {
        memcpy(tmp, name + 1, len - 2);
        tmp[len - 2] = '\0';
        clean = tmp;
    }
    if (!clean[0])
        return name;
    uint32_t slot = fnv1a(clean) & (ICON_HASH_SIZE - 1);
    for (int i = s_icon_chain[slot]; i >= 0 && i < (int)ICON_MAP_COUNT; i = s_icon_entries[i].next)
    {
        int idx = s_icon_entries[i].idx;
        if (idx < 0 || idx >= (int)ICON_MAP_COUNT)
            break;
        if (ICON_MAP[idx].name && strcmp(ICON_MAP[idx].name, clean) == 0)
            return ICON_MAP[idx].codepoint;
    }
    LOG_WARNING("Unknown icon name '%s', using literal text as fallback", clean);
    return name;
}

static bool eval_condition(const char *cond)
{
    if (!cond || !cond[0])
        return false;
    while (*cond == ' ' || *cond == '\t')
        cond++;

    if (strncmp(cond, "state.", 6) == 0)
    {
        const char *key_start = cond + 6;
        while (*key_start == ' ' || *key_start == '\t')
            key_start++;

        const char *eq = strstr(key_start, "==");
        const char *neq = strstr(key_start, "!=");
        const char *gte = strstr(key_start, ">=");
        const char *lte = strstr(key_start, "<=");
        const char *gt = strchr(key_start, '>');
        const char *lt = strchr(key_start, '<');

        char key[64];
        memset(key, 0, sizeof(key));

        const char *ops[] = {neq, gte, lte, eq, gt, lt};
        const char *op_names[] = {"!=", ">=", "<=", "==", ">", "<"};
        const char *selected_op = NULL;
        const char *selected_name = NULL;

        for (int i = 0; i < 6; i++)
        {
            if (ops[i])
            {
                bool is_first = true;
                for (int j = 0; j < i; j++)
                {
                    if (ops[j] && ops[j] < ops[i])
                    {
                        is_first = false;
                        break;
                    }
                }
                if (is_first)
                {
                    selected_op = ops[i];
                    selected_name = op_names[i];
                    break;
                }
            }
        }

        if (!selected_op)
        {
            size_t klen = strlen(key_start);
            if (klen >= sizeof(key))
                return false;
            memcpy(key, key_start, klen);
            key[klen] = '\0';
            char *end = key + strlen(key) - 1;
            while (end > key && (*end == ' ' || *end == '\t'))
                *end-- = '\0';
            bool bv = false;
            IncenseStateGetBool(key, &bv);
            return bv;
        }

        size_t klen = (size_t)(selected_op - key_start);
        while (klen > 0 && (key_start[klen - 1] == ' ' || key_start[klen - 1] == '\t'))
            klen--;
        if (klen >= sizeof(key))
            return false;
        memcpy(key, key_start, klen);
        key[klen] = '\0';

        const char *rhs = selected_op + strlen(selected_name);
        while (*rhs == ' ' || *rhs == '\t')
            rhs++;

        IncenseStateEntry *e = state_find(key);
        if (!e)
            return false;

        if (strcmp(selected_name, "!=") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v != atoi(rhs);
            }
            if (e->type == INCENSE_STATE_BOOL)
            {
                bool v;
                IncenseStateGetBool(key, &v);
                return v != (strcmp(rhs, "true") == 0 || strcmp(rhs, "1") == 0);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v != strtof(rhs, NULL);
            }
            if (e->type == INCENSE_STATE_STRING)
            {
                char sv[256];
                IncenseStateGetString(key, sv, sizeof(sv));
                return strcmp(sv, rhs) != 0;
            }
            return false;
        }
        if (strcmp(selected_name, ">=") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v >= atoi(rhs);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v >= strtof(rhs, NULL);
            }
            return false;
        }
        if (strcmp(selected_name, "<=") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v <= atoi(rhs);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v <= strtof(rhs, NULL);
            }
            return false;
        }
        if (strcmp(selected_name, "==") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v == atoi(rhs);
            }
            if (e->type == INCENSE_STATE_BOOL)
            {
                bool v;
                IncenseStateGetBool(key, &v);
                return v == (strcmp(rhs, "true") == 0 || strcmp(rhs, "1") == 0);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v == strtof(rhs, NULL);
            }
            if (e->type == INCENSE_STATE_STRING)
            {
                char sv[256];
                IncenseStateGetString(key, sv, sizeof(sv));
                return strcmp(sv, rhs) == 0;
            }
            return false;
        }
        if (strcmp(selected_name, ">") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v > atoi(rhs);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v > strtof(rhs, NULL);
            }
            return false;
        }
        if (strcmp(selected_name, "<") == 0)
        {
            if (e->type == INCENSE_STATE_INT)
            {
                int v;
                IncenseStateGetInt(key, &v);
                return v < atoi(rhs);
            }
            if (e->type == INCENSE_STATE_FLOAT)
            {
                float v;
                IncenseStateGetFloat(key, &v);
                return v < strtof(rhs, NULL);
            }
            return false;
        }
        return false;
    }

    if (strcmp(cond, "true") == 0)
        return true;
    if (strcmp(cond, "false") == 0)
        return false;
    return false;
}

static void props_collect(IncenseNode *node, PropBag *bag)
{
    if (!bag)
        return;
    bag->count = 0;
    bag->node = node;
    if (!node)
        return;
    for (IncenseNode *cur = node->first_child; cur && bag->count < MAX_PROPS; cur = cur->next_sibling)
    {
        if (!cur || cur->type != INCENSE_PROPERTY)
            continue;
        if (!cur->name)
            continue;
        const char *v = cur->value;
        char *owned = NULL;
        if (v)
        {
            const char *embed_val = NULL;
            if (strncmp(v, "embed_", 6) == 0)
            {
                embed_val = embed_props_get(v + 6);
            }
            const char *src = embed_val ? embed_val : v;
            size_t len = strlen(src);
            if (len >= 2 && src[0] == '"' && src[len - 1] == '"')
            {
                owned = malloc(len - 1);
                if (owned)
                {
                    memcpy(owned, src + 1, len - 2);
                    owned[len - 2] = '\0';
                }
                else
                {
                    LOG_ERROR("Out of memory while parsing property value");
                    continue;
                }
            }
            else
            {
                owned = strdup(src);
                if (!owned)
                {
                    LOG_ERROR("Out of memory while parsing property value");
                    continue;
                }
            }
        }
        bag->items[bag->count].key = cur->name;
        bag->items[bag->count].value = owned;
        bag->count++;
    }
}

static inline const char *props_get(const PropBag *bag, const char *key)
{
    if (!bag || !key)
        return NULL;
    for (int i = 0; i < bag->count; i++)
    {
        if (bag->items[i].key && strcmp(bag->items[i].key, key) == 0)
        {
            const char *val = bag->items[i].value;
            if (val && strncmp(val, "embed_", 6) == 0)
            {
                const char *resolved = embed_props_get(val + 6);
                return resolved ? resolved : val;
            }
            return val;
        }
    }
    return NULL;
}

static void props_free(PropBag *bag)
{
    if (!bag)
        return;
    for (int i = 0; i < bag->count; i++)
    {
        free((void *)bag->items[i].value);
        bag->items[i].value = NULL;
    }
    bag->count = 0;
}

static int props_int(const PropBag *bag, const char *key, int def)
{
    const char *v = props_get(bag, key);
    if (!v || !v[0])
        return def;
    const char *check = v;
    if (strncmp(check, "state.", 6) == 0)
    {
        int out = def;
        IncenseStateGetInt(check + 6, &out);
        return out;
    }
    if (strncmp(check, "embed_", 6) == 0)
    {
        const char *resolved = embed_props_get(check + 6);
        if (resolved)
            check = resolved;
    }
    errno = 0;
    char *end;
    long val = strtol(check, &end, 10);
    if (end == check || *end != '\0')
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not a valid integer", key, check);
        return def;
    }
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN)
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is out of integer range", key, check);
        return def;
    }
    return (int)val;
}

static float props_float(const PropBag *bag, const char *key, float def)
{
    const char *v = props_get(bag, key);
    if (!v || !v[0])
        return def;
    const char *check = v;
    if (strncmp(check, "state.", 6) == 0)
    {
        float out = def;
        IncenseStateGetFloat(check + 6, &out);
        return out;
    }
    if (strncmp(check, "embed_", 6) == 0)
    {
        const char *resolved = embed_props_get(check + 6);
        if (resolved)
            check = resolved;
    }
    errno = 0;
    char *end;
    float val = strtof(check, &end);
    if (end == check || *end != '\0')
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not a valid float", key, check);
        return def;
    }
    if (!isfinite(val))
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not finite", key, check);
        return def;
    }
    return val;
}

static bool props_bool(const PropBag *bag, const char *key, bool def)
{
    const char *v = props_get(bag, key);
    if (!v || !v[0])
        return def;
    const char *check = v;
    if (strncmp(check, "state.", 6) == 0)
    {
        bool out = def;
        IncenseStateGetBool(check + 6, &out);
        return out;
    }
    if (strncmp(check, "embed_", 6) == 0)
    {
        const char *resolved = embed_props_get(check + 6);
        if (resolved)
            check = resolved;
    }
    if (check[0] == 't' || check[0] == 'T' || check[0] == '1')
        return true;
    if (check[0] == 'f' || check[0] == 'F' || check[0] == '0')
        return false;
    ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not a valid boolean", key, check);
    return def;
}

static uint32_t props_color(const PropBag *bag, const char *key, uint32_t def)
{
    const char *v = props_get(bag, key);
    if (!v || !v[0])
        return def;
    const char *check = v;
    if (strncmp(check, "embed_", 6) == 0)
    {
        const char *resolved = embed_props_get(check + 6);
        if (resolved)
            check = resolved;
    }
    if (check[0] != '#')
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not a valid color", key, check);
        ERR_SUGGEST("Use #RRGGBB or #AARRGGBB format");
        return def;
    }
    errno = 0;
    char *end;
    unsigned long val = strtoul(check + 1, &end, 16);
    if (end == check + 1 || *end != '\0')
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is not a valid hex color", key, check);
        return def;
    }
    if (errno == ERANGE)
    {
        ERR_SYNTAX_N_CTX(bag ? bag->node : NULL, key, "Property '%s' value '%s' is out of range for a color", key, check);
        return def;
    }
    return (uint32_t)val;
}

static char *props_str_dup(const PropBag *bag, const char *key, const char *def)
{
    const char *v = props_get(bag, key);
    if (v && strncmp(v, "state.", 6) == 0)
    {
        char sbuf[256];
        if (IncenseStateGetString(v + 6, sbuf, sizeof(sbuf)))
        {
            char *out = strdup(sbuf);
            if (!out)
                LOG_ERROR("Out of memory duplicating state property '%s'", safe_str(key));
            return out;
        }
    }
    if (v && strncmp(v, "embed_", 6) == 0)
    {
        const char *resolved = embed_props_get(v + 6);
        if (resolved)
        {
            char *out = strdup(resolved);
            if (!out)
                LOG_ERROR("Out of memory duplicating embed property '%s'", safe_str(key));
            return out;
        }
    }
    const char *src = v ? v : (def ? def : "");
    char *out = strdup(src);
    if (!out)
    {
        LOG_ERROR("Out of memory duplicating property '%s'", safe_str(key));
    }
    return out;
}

static int match_enum(IncenseNode *node, const PropBag *bag, const char *key, int def,
                      const char *const *names, const int *values, int count,
                      const char *what, const char *valid_list)
{
    if (!names || !values || count <= 0)
        return def;
    const char *v = props_get(bag, key);
    if (!v || !v[0])
        return def;
    for (int i = 0; i < count; i++)
    {
        if (names[i] && strcmp(v, names[i]) == 0)
            return values[i];
    }
    ERR_WARN_N_CTX(node, key, "Unknown %s '%s'", safe_str(what), v);
    ERR_SUGGEST("Valid values: %s", safe_str(valid_list));
    return def;
}
static void validate_properties(IncenseNode *node, const PropBag *bag)
{
    if (!bag)
        return;
  static const char *const valid[] = {
    "action", "animation", "animation_duration", "animation_easing", "animation_end_val", "animation_start_val",
    "attribution", "autoplay", "color", "columns", "condition", "direction", "duration", "font", "group",
    "header", "height", "hidden", "icon", "id", "label", "lat", "layout", "length", "lon", "max", "message",
    "min", "on_change", "on_click", "on_select", "on_submit", "orientation", "parent", "placeholder",
    "position", "radius", "selected", "show", "size", "src", "style", "text", "thickness", "title", "type", "value",
    "variant", "visible", "width", "x", "y", "zoom", "z_index", NULL};
    for (int i = 0; i < bag->count; i++)
    {
        if (!bag->items[i].key)
            continue;
        bool found = false;
        for (int j = 0; valid[j] && !found; j++)
            found = strcmp(bag->items[i].key, valid[j]) == 0;
        if (!found)
        {
            ERR_WARN_N_CTX(node, bag->items[i].key, "Unknown property '%s' in widget '%s'", bag->items[i].key, node ? safe_str(node->name) : "?");
            ERR_SUGGEST("Check spelling or refer to documentation for valid properties");
        }
    }
}

static inline CallbackEntry *resolve_callback(IncenseNode *node, const PropBag *bag, const char *prop)
{
    const char *name = props_get(bag, prop);
    if (!name || !name[0])
        return NULL;
    CallbackEntry *entry = callback_find(name);
    if (!entry)
    {
        ERR_SEMANTIC_N(node, "Callback '%s' referenced by '%s' is not registered", name, safe_str(prop));
        ERR_SUGGEST("Call IncenseRegisterCallback() before loading UI");
    }
    return entry;
}

static inline AromaNode *resolve_parent(IncenseNode *node, AromaNode *sp, const BuildCtx *ctx)
{
    if (!node || !ctx)
        return sp;
    for (IncenseNode *cur = node->first_child; cur; cur = cur->next_sibling)
    {
        if (!cur || cur->type != INCENSE_PROPERTY || !cur->name || strcmp(cur->name, "parent") != 0)
            continue;
        if (!cur->value || !cur->value[0])
            return sp;
        AromaNode *override = registry_find(ctx->registry, cur->value);
        if (override)
            return override;
        ERR_SEMANTIC_N(node, "Parent '%s' not found in registry", cur->value);
        return sp;
    }
    return sp;
}

static AromaFont *resolve_font(const BuildCtx *ctx, const char *font_name)
{
    if (!ctx)
        return NULL;
    if (!font_name || !font_name[0])
        return ctx->default_font;
    AromaFont *f;
    if (ctx->font_registry && (f = font_registry_find(ctx->font_registry, font_name)))
        return f;
    if (s_global_font_registry && (f = font_registry_find(s_global_font_registry, font_name)))
        return f;
    LOG_WARNING("Font '%s' not found in registry, using default", font_name);
    return ctx->default_font;
}

static void apply_widget_animations(AromaNode *built, const PropBag *bag, IncenseNode *node)
{
    if (!built || !bag)
        return;
    const char *anim = props_get(bag, "animation");
    if (!anim || !anim[0])
    {
        if (props_bool(bag, "hidden", false))
            aroma_animation_start(built, AROMA_ANIM_FADE, 1.0f, 0.0f, 0);
        return;
    }
    static const char *const anim_names[] = {"fade", "slide_x", "slide_y", "scale_x", "scale_y"};
    static const int anim_values[] = {AROMA_ANIM_FADE, AROMA_ANIM_SLIDE_X, AROMA_ANIM_SLIDE_Y, AROMA_ANIM_SCALE_X, AROMA_ANIM_SCALE_Y};
    int type = match_enum(node, bag, "animation", -1, anim_names, anim_values, 5, "animation type", "fade, slide_x, slide_y, scale_x, scale_y");
    if (type == -1)
    {
        if (props_bool(bag, "hidden", false))
            aroma_animation_start(built, AROMA_ANIM_FADE, 1.0f, 0.0f, 0);
        return;
    }
    float duration = props_float(bag, "animation_duration", 300);
    if (duration < 0)
        duration = 0;
    AromaAnimation *anim_obj = aroma_animation_start(built, (AromaAnimationType)type,
                                                     props_float(bag, "animation_start_val", 0), props_float(bag, "animation_end_val", 0), duration);
    if (anim_obj)
    {
        const char *ease = props_get(bag, "animation_easing");
        if (ease && ease[0])
        {
            static const struct
            {
                const char *name;
                AromaEasingType e;
            } ease_map[] = {
                {"linear", AROMA_EASE_LINEAR}, {"ease_in", AROMA_EASE_IN_QUAD}, {"ease_out", AROMA_EASE_OUT_QUAD}, {"ease_in_out", AROMA_EASE_IN_OUT_QUAD}, {"ease_out_cubic", AROMA_EASE_OUT_CUBIC}, {"ease_out_back", AROMA_EASE_OUT_BACK}, {"ease_out_elastic", AROMA_EASE_OUT_ELASTIC}, {NULL, 0}};
            for (int i = 0; ease_map[i].name; i++)
            {
                if (strcmp(ease, ease_map[i].name) == 0)
                {
                    aroma_animation_set_easing(anim_obj, ease_map[i].e);
                    break;
                }
            }
        }
    }
    if (props_bool(bag, "hidden", false))
        aroma_animation_start(built, AROMA_ANIM_FADE, 1.0f, 0.0f, 0);
}

static inline void maybe_register(const PropBag *bag, AromaNode *built, BuildCtx *ctx)
{
    if (!built || !ctx || !ctx->registry || !bag)
        return;
    const char *id = props_get(bag, "id");
    if (id && id[0])
    {
        uint32_t slot = fnv1a(id) & (WR_HASH_SIZE - 1);
        int *prev_next = &ctx->registry->buckets[slot];
        int idx = *prev_next;
        while (idx >= 0 && idx < ctx->registry->count)
        {
            if (strcmp(ctx->registry->items[idx].id, id) == 0)
            {
                *prev_next = ctx->registry->items[idx].next;
                break;
            }
            prev_next = &ctx->registry->items[idx].next;
            idx = *prev_next;
        }
        registry_register(ctx->registry, id, built);
    }
}

static int collect_item_nodes(IncenseNode *node, const char *item_name, IncenseNode *out[], int max_out)
{
    int count = 0;
    if (!node || !item_name || !out || max_out <= 0)
        return 0;
    for (IncenseNode *cur = node->first_child; cur && count < max_out; cur = cur->next_sibling)
    {
        if (cur && cur->type == INCENSE_OBJECT && cur->name && strcmp(cur->name, item_name) == 0)
            out[count++] = cur;
    }
    if (!count)
        ERR_WARN_N(node, "No '%s' items found in widget '%s'", item_name, safe_str(node->name));
    return count;
}

static AromaNode *build_widget(IncenseNode *node, AromaNode *sp, BuildCtx *ctx);
typedef struct
{
    IncenseNode *node;
    AromaNode *parent;
    BuildCtx ctx;
    char condition[256];
    bool last_result;
} ConditionalState;

#define MAX_CONDITIONAL_STATES 64
static ConditionalState s_conditional_states[MAX_CONDITIONAL_STATES];
static int s_conditional_count = 0;

static void rebuild_conditional(ConditionalState *cs)
{
    if (!cs || !cs->parent || !cs->node)
        return;

    bool passes = eval_condition(cs->condition);
    if (passes == cs->last_result)
        return;
    cs->last_result = passes;

    AromaNode *parent = cs->parent;
    IncenseNode *node = cs->node;
    BuildCtx *ctx = &cs->ctx;

    for (IncenseNode *child = node->first_child; child; child = child->next_sibling)
    {
        if (!child || child->type != INCENSE_OBJECT || !child->name)
            continue;
        if (strcmp(child->name, "Then") == 0 && passes)
        {
            build_children(child, parent, ctx);
            aroma_node_invalidate_tree(parent);
            return;
        }
        if (strcmp(child->name, "Else") == 0 && !passes)
        {
            build_children(child, parent, ctx);
            aroma_node_invalidate_tree(parent);
            return;
        }
    }
}

static void on_conditional_state_change(const char *key, const IncenseStateEntry *entry, void *userdata)
{
    (void)key;
    (void)entry;
    ConditionalState *cs = (ConditionalState *)userdata;
    if (cs)
        rebuild_conditional(cs);
}

static void build_children(IncenseNode *node, AromaNode *parent, BuildCtx *ctx)
{
    if (!node || !ctx)
        return;
    for (IncenseNode *c = node->first_child; c; c = c->next_sibling)
    {
        if (!c)
            continue;
        if (c->type == INCENSE_OBJECT)
        {
            if (c->name && strcmp(c->name, "If") == 0)
            {
                PropBag cb;
                props_collect(c, &cb);
                const char *cond_str = props_get(&cb, "condition");
                bool passes = cond_str ? eval_condition(cond_str) : false;

                if (cond_str && s_conditional_count < MAX_CONDITIONAL_STATES)
                {
                    ConditionalState *cs = &s_conditional_states[s_conditional_count++];
                    memset(cs, 0, sizeof(*cs));
                    cs->node = c;
                    cs->parent = parent;
                    cs->ctx = *ctx;
                    strncpy(cs->condition, cond_str, sizeof(cs->condition) - 1);
                    cs->last_result = passes;

                    if (strncmp(cond_str, "state.", 6) == 0)
                    {
                        char key_copy[256];
                        strncpy(key_copy, cond_str, sizeof(key_copy) - 1);
                        key_copy[sizeof(key_copy) - 1] = '\0';
                        char *key_start = key_copy + 6;
                        char *op = strpbrk(key_start, "=!><");
                        if (op)
                            *op = '\0';
                        char *end = key_start + strlen(key_start) - 1;
                        while (end > key_start && (*end == ' ' || *end == '\t'))
                            *end-- = '\0';
                        IncenseStateAddObserver(key_start, on_conditional_state_change, cs);
                    }
                }

                props_free(&cb);
                if (passes)
                {
                    for (IncenseNode *child = c->first_child; child; child = child->next_sibling)
                        if (child && child->type == INCENSE_OBJECT && child->name && strcmp(child->name, "Then") == 0)
                            build_children(child, parent, ctx);
                }
                else
                {
                    for (IncenseNode *child = c->first_child; child; child = child->next_sibling)
                        if (child && child->type == INCENSE_OBJECT && child->name && strcmp(child->name, "Else") == 0)
                            build_children(child, parent, ctx);
                }
            }
            else
            {
                build_widget(c, parent, ctx);
            }
        }
    }
}

static bool bridge_bool_ptr(AromaNode *node, void *ud)
{
    CallbackEntry *e = ud;
    return (e && e->fn && e->type == INCENSE_CALLBACK_BOOL_PTR) ? ((bool (*)(AromaNode *, void *))e->fn)(node, e->userdata) : false;
}

static void bridge_void_ptr(void *ud)
{
    CallbackEntry *e = ud;
    if (e && e->fn && e->type == INCENSE_CALLBACK_VOID_PTR)
        ((void (*)(void *))e->fn)(e->userdata);
}

static void bridge_dropdown_change(int index, const char *option, void *ud)
{
    CallbackEntry *e = ud;
    if (e && e->fn && e->type == INCENSE_CALLBACK_INT_STRING_PTR)
        ((void (*)(int, const char *, void *))e->fn)(index, option, e->userdata);
}

static void bridge_checkbox_change(bool checked, void *ud)
{
    CallbackEntry *e = ud;
    if (e && e->fn && e->type == INCENSE_CALLBACK_BOOL_BOOL_PTR)
        ((void (*)(bool, void *))e->fn)(checked, e->userdata);
}

static bool bridge_textbox_change(AromaNode *node, const char *text, void *ud)
{
    CallbackEntry *e = ud;
    return (e && e->fn && e->type == INCENSE_CALLBACK_NODE_STRING_PTR) ? ((bool (*)(AromaNode *, const char *, void *))e->fn)(node, text, e->userdata) : false;
}

static void bridge_listview_select(int index, void *ud)
{
    CallbackEntry *e = ud;
    if (e && e->fn && e->type == INCENSE_CALLBACK_INT_PTR)
        ((void (*)(int, void *))e->fn)(index, e->userdata);
}

static void bridge_node_int(AromaNode *node, int index, void *ud)
{
    CallbackEntry *e = ud;
    if (e && e->fn && e->type == INCENSE_CALLBACK_NODE_INT_PTR)
        ((void (*)(AromaNode *, int, void *))e->fn)(node, index, e->userdata);
}

#define WIDGET_PREAMBLE(node, sp, ctx)                       \
    PropBag bag;                                             \
    props_collect((node), &bag);                             \
    validate_properties((node), &bag);                       \
    AromaNode *parent = resolve_parent((node), (sp), (ctx)); \
    const char *_font_name = props_get(&bag, "font");        \
    AromaFont *_widget_font = _font_name ? resolve_font((ctx), _font_name) : ((ctx) ? (ctx)->default_font : NULL)

#define WIDGET_POSTAMBLE(built, bag, node, ctx)              \
    do                                                       \
    {                                                        \
        if (built)                                           \
        {                                                    \
            if (node)                                        \
                (node)->id = (built)->node_id;               \
            int _zi = props_int(&(bag), "z_index", 0);       \
            if (_zi)                                         \
                aroma_node_set_z_index((built), _zi);        \
        }                                                    \
        else                                                 \
            ERR_SYNTAX_N((node), "Failed to create widget"); \
        apply_widget_animations((built), &(bag), (node));    \
        maybe_register(&(bag), (built), (ctx));              \
        build_children((node), (built), (ctx));              \
        props_free(&(bag));                                  \
    } while (0);                                             \
    return (built)

static AromaNode *build_button(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_click = resolve_callback(node, &bag, "on_click");
    char *text = props_str_dup(&bag, "text", "Button");
    AromaNode *built = NULL;
    if (text)
    {
        built = aroma_ui_button(parent, text, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                props_int(&bag, "width", 120), props_int(&bag, "height", 40), on_click ? bridge_bool_ptr : NULL, on_click, _widget_font);
    }
    free(text);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_label(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    char *text = props_str_dup(&bag, "text", "");
    static const char *const style_names[] = {"large", "medium", "small"};
    static const int style_values[] = {LABEL_STYLE_LABEL_LARGE, LABEL_STYLE_LABEL_MEDIUM, LABEL_STYLE_LABEL_SMALL};
    AromaLabelStyle style = (AromaLabelStyle)match_enum(node, &bag, "style", LABEL_STYLE_LABEL_LARGE, style_names, style_values, 3, "label style", "large, medium, small");

    AromaNode *built = NULL;
    if (text)
        built = aroma_ui_label(parent, text, props_int(&bag, "x", 0), props_int(&bag, "y", 0), style, _widget_font);
      const char *cs = props_get(&bag, "color");
        if (cs)
            aroma_label_set_color(built, props_color(&bag, "color", 0x000000FF));
        free(text);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_container(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    static const char *const layout_names[] = {"flex"};
    static const int layout_values[] = {AROMA_LAYOUT_MODE_FLEX};
    AromaLayoutMode layout = (AromaLayoutMode)match_enum(node, &bag, "layout", AROMA_LAYOUT_MODE_NONE, layout_names, layout_values, 1, "layout", "flex");
    static const char *const dir_names[] = {"row", "column"};
    static const int dir_values[] = {AROMA_FLEX_ROW, AROMA_FLEX_COLUMN};
    AromaFlexDirection dir = (AromaFlexDirection)match_enum(node, &bag, "direction", AROMA_FLEX_COLUMN, dir_names, dir_values, 2, "direction", "row, column");
    AromaNode *built = aroma_ui_container(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                          props_int(&bag, "width", 200), props_int(&bag, "height", 200), layout, dir, AROMA_JUSTIFY_START, AROMA_ALIGN_START);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_scrollview(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    static const char *const dir_names[] = {"vertical", "horizontal", "both"};
    static const int dir_values[] = {AROMA_SCROLL_VERTICAL, AROMA_SCROLL_HORIZONTAL, AROMA_SCROLL_BOTH};
    AromaScrollDirection dir = (AromaScrollDirection)match_enum(node, &bag, "direction", AROMA_SCROLL_VERTICAL, dir_names, dir_values, 3, "scroll direction", "vertical, horizontal, both");
    AromaNode *built = aroma_container_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                              props_int(&bag, "width", 200), props_int(&bag, "height", 200));
    if (!built)
    {
        ERR_SYNTAX_N(node, "Failed to create ScrollView");
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    aroma_container_set_scrollable(built, true);
    aroma_container_set_scroll_direction(built, dir);
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    aroma_container_update_auto_content_size(built);
    props_free(&bag);
    return built;
}

static AromaNode *build_checkbox(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_change = resolve_callback(node, &bag, "on_change");
    char *label = props_str_dup(&bag, "label", "");
    AromaNode *built = NULL;
    if (label)
    {
        built = aroma_ui_checkbox(parent, label, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                  props_int(&bag, "width", 160), props_int(&bag, "height", 32), on_change ? bridge_checkbox_change : NULL, on_change, _widget_font);
    }
    free(label);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_switch(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_change = resolve_callback(node, &bag, "on_change");
    AromaNode *built = aroma_ui_switch(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                       props_int(&bag, "width", 56), props_int(&bag, "height", 28), props_bool(&bag, "value", false),
                                       on_change ? bridge_bool_ptr : NULL, on_change);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_slider(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_change = resolve_callback(node, &bag, "on_change");
    int mn = props_int(&bag, "min", 0), mx = props_int(&bag, "max", 100), val = props_int(&bag, "value", 0);
    if (mn >= mx)
    {
        ERR_SYNTAX_N(node, "Slider min (%d) >= max (%d)", mn, mx);
        mn = 0;
        mx = 100;
    }
    if (val < mn)
        val = mn;
    else if (val > mx)
        val = mx;
    AromaNode *built = aroma_ui_slider(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                       props_int(&bag, "width", 200), props_int(&bag, "height", 24), mn, mx, val, on_change ? bridge_bool_ptr : NULL, on_change);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_textbox(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *cb = resolve_callback(node, &bag, "on_change");
    if (!cb)
        cb = resolve_callback(node, &bag, "on_submit");
    char *ph = props_str_dup(&bag, "placeholder", "");
    AromaNode *built = NULL;
    if (ph)
    {
        built = aroma_ui_textbox(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                 props_int(&bag, "width", 200), props_int(&bag, "height", 36), ph, cb ? bridge_textbox_change : NULL, cb, _widget_font);
    }
    free(ph);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_progressbar(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    float prog = props_float(&bag, "value", 0.0f);
    if (prog < 0.0f)
        prog = 0.0f;
    else if (prog > 1.0f)
        prog = 1.0f;
    static const char *const type_names[] = {"determinate", "indeterminate"};
    static const int type_values[] = {PROGRESS_TYPE_DETERMINATE, PROGRESS_TYPE_INDETERMINATE};
    AromaProgressType type = (AromaProgressType)match_enum(node, &bag, "type", PROGRESS_TYPE_DETERMINATE, type_names, type_values, 2, "progress type", "determinate, indeterminate");
    AromaNode *built = aroma_ui_progressbar(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                            props_int(&bag, "width", 200), props_int(&bag, "height", 8), type, prog);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_divider(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    int length = props_int(&bag, "length", 100);
    if (length <= 0)
        length = 100;
    static const char *const orient_names[] = {"horizontal", "vertical"};
    static const int orient_values[] = {DIVIDER_ORIENTATION_HORIZONTAL, DIVIDER_ORIENTATION_VERTICAL};
    AromaDividerOrientation orient = (AromaDividerOrientation)match_enum(node, &bag, "orientation", DIVIDER_ORIENTATION_HORIZONTAL, orient_names, orient_values, 2, "divider orientation", "horizontal, vertical");
    AromaNode *built = aroma_ui_divider(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0), length, orient);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_card(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    static const char *const type_names[] = {"elevated", "outlined", "filled"};
    static const int type_values[] = {CARD_TYPE_ELEVATED, CARD_TYPE_OUTLINED, CARD_TYPE_FILLED};
    AromaCardType type = (AromaCardType)match_enum(node, &bag, "type", CARD_TYPE_ELEVATED, type_names, type_values, 3, "card type", "elevated, outlined, filled");
    AromaNode *built = aroma_ui_card(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                     props_int(&bag, "width", 200), props_int(&bag, "height", 120), type);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}


static AromaNode *build_iconbutton(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_click = resolve_callback(node, &bag, "on_click");
    const char *ir = props_get(&bag, "icon");
    const char *resolved = ir ? resolve_icon(ir) : "";
    char *icon = strdup(resolved ? resolved : "");
    static const char *const variant_names[] = {"standard", "filled", "tonal", "outlined"};
    static const int variant_values[] = {ICON_BUTTON_STANDARD, ICON_BUTTON_FILLED, ICON_BUTTON_TONAL, ICON_BUTTON_OUTLINED};
    AromaIconButtonVariant variant = (AromaIconButtonVariant)match_enum(node, &bag, "variant", ICON_BUTTON_STANDARD, variant_names, variant_values, 4, "icon button variant", "standard, filled, tonal, outlined");
    AromaNode *built = NULL;
    if (icon)
    {
        built = aroma_ui_iconbutton(parent, icon, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                    props_int(&bag, "size", 40), variant, on_click ? bridge_void_ptr : NULL, on_click,
                                    ctx->icon_font ? ctx->icon_font : _widget_font);
    }
    free(icon);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_icon(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    AromaNode *built = aroma_icon_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0), props_int(&bag, "size", 24));
    if (built)
    {
        if (node)
            node->id = built->node_id;
        const char *tr = props_get(&bag, "text"), *sr = props_get(&bag, "src");
        if (tr)
        {
            const char *resolved = resolve_icon(tr);
            char *td = strdup(resolved ? resolved : tr);
            if (td)
            {
                aroma_icon_set_text(built, td, ctx->icon_font ? ctx->icon_font : _widget_font);
                free(td);
            }
            else
            {
                LOG_ERROR("Out of memory resolving icon text");
            }
        }
        else if (sr && sr[0])
        {
            aroma_icon_set_image(built, (char *)sr);
        }
        else
        {
            ERR_WARN_N(node, "Icon has neither 'text' nor 'src'");
            ERR_SUGGEST("Add either 'text' (icon name) or 'src' (image path)");
        }
        const char *cs = props_get(&bag, "color");
        if (cs)
            aroma_icon_set_color(built, props_color(&bag, "color", 0x000000FF));
        int zi = props_int(&bag, "z_index", 0);
        if (zi)
            aroma_node_set_z_index(built, zi);
    }
    else
    {
        ERR_SYNTAX_N(node, "Failed to create Icon widget");
    }
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}static AromaNode *build_snackbar(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;

    WIDGET_PREAMBLE(node, sp, ctx);

    int dur = props_int(&bag, "duration", 4000);
    if (dur <= 0)
        dur = 4000;

    char *msg = props_str_dup(&bag, "message", "");
    AromaNode *built = NULL;

    if (msg && msg[0])
    {
        built = aroma_snackbar_create(parent, msg, dur);

        if (built)
        {
            if (node)
                node->id = built->node_id;

            aroma_snackbar_set_font(built, _widget_font);

            const char *action_text = props_get(&bag, "action");
            if (action_text && action_text[0])
            {
                CallbackEntry *on_action = resolve_callback(node, &bag, "on_click");
                aroma_snackbar_set_action(built, action_text,
                                          on_action ? bridge_void_ptr : NULL,
                                          on_action);
            }

    
        int zi = props_int(&bag, "z_index", 0);
        if (zi)
            aroma_node_set_z_index(built, zi);

        maybe_register(&bag, built, ctx);
        build_children(node, built, ctx);

        apply_widget_animations(built, &bag, node);

        bool should_show = props_bool(&bag, "show", true);
        if (should_show)
        {
            aroma_snackbar_show(built);
        }
        }
        else
        {
            ERR_SYNTAX_N(node, "Failed to create Snackbar widget");
        }
    }
    else
    {
        ERR_WARN_N(node, "Snackbar requires a 'message' property");
        ERR_SUGGEST("Add 'message' property with snackbar text");
    }

    free(msg);
    props_free(&bag);
    return built;
}
static AromaNode *build_listview(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_select = resolve_callback(node, &bag, "on_select");
    AromaNode *built = aroma_ui_listview(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                         props_int(&bag, "width", 200), props_int(&bag, "height", 300), on_select ? bridge_listview_select : NULL, on_select, _widget_font);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_dialog(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    char *title = props_str_dup(&bag, "title", "Dialog");
    char *msg = props_str_dup(&bag, "message", "");
    static const char *const type_names[] = {"basic", "fullscreen"};
    static const int type_values[] = {DIALOG_TYPE_BASIC, DIALOG_TYPE_FULL_SCREEN};
    AromaDialogType type = (AromaDialogType)match_enum(node, &bag, "type", DIALOG_TYPE_BASIC, type_names, type_values, 2, "dialog type", "basic, fullscreen");
    AromaNode *built = NULL;
    if (title && msg)
    {
        built = aroma_ui_dialog(parent, title, msg, props_int(&bag, "width", 320), props_int(&bag, "height", 200), type, _widget_font);
    }
    free(title);
    free(msg);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_image(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    char *path = props_str_dup(&bag, "src", "");
    AromaNode *built = NULL;
    if (path)
    {
        built = aroma_ui_image(parent, path, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                               props_int(&bag, "width", 100), props_int(&bag, "height", 100));
    }
    free(path);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_canvas(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    AromaNode *built = aroma_canvas_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                           props_int(&bag, "width", 200), props_int(&bag, "height", 200));
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_debugoverlay(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    AromaNode *built = aroma_debug_overlay_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0), props_int(&bag, "width", 200));
    if (built)
    {
        if (node)
            node->id = built->node_id;
        aroma_debug_overlay_set_font(built, _widget_font);
        aroma_debug_overlay_set_visible(built, props_bool(&bag, "visible", true));
        int zi = props_int(&bag, "z_index", 0);
        if (zi)
            aroma_node_set_z_index(built, zi);
    }
    else
    {
        ERR_SYNTAX_N(node, "Failed to create DebugOverlay");
    }
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_dropdown(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_change = resolve_callback(node, &bag, "on_change");
    AromaNode *built = aroma_dropdown_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                             props_int(&bag, "width", 200), props_int(&bag, "height", 36));
    if (!built)
    {
        ERR_SYNTAX_N(node, "Failed to create Dropdown");
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    IncenseNode *items[MAX_ITEM_NODES];
    int n = collect_item_nodes(node, "Option", items, MAX_ITEM_NODES);
    for (int i = 0; i < n; i++)
    {
        PropBag ob;
        props_collect(items[i], &ob);
        char *text = props_str_dup(&ob, "text", "");
        if (text)
            aroma_dropdown_add_option(built, text);
        free(text);
        props_free(&ob);
    }
    if (on_change)
        aroma_dropdown_set_on_change(built, bridge_dropdown_change, on_change);
    aroma_dropdown_setup_events(built, NULL, NULL);
    aroma_dropdown_set_font(built, _widget_font);
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_gif(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    char *path = props_str_dup(&bag, "src", "");
    AromaNode *built = NULL;
    if (path)
    {
        built = aroma_gif_create(parent, path, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                 props_int(&bag, "width", 100), props_int(&bag, "height", 100));
        if (built && props_bool(&bag, "autoplay", true))
            aroma_gif_play(built);
    }
    free(path);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_loading(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    int radius = props_int(&bag, "radius", 16);
    if (radius <= 0)
        radius = 16;
    int thickness = props_int(&bag, "thickness", 3);
    if (thickness <= 0)
        thickness = 3;
    AromaNode *built = aroma_loading_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                            radius, thickness, props_color(&bag, "color", 0x000000FF));
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}

static AromaNode *build_map(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    AromaNode *built = aroma_map_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                        props_int(&bag, "width", 300), props_int(&bag, "height", 300));
    if (!built)
    {
        ERR_SYNTAX_N(node, "Failed to create Map");
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    int zoom = props_int(&bag, "zoom", 4);
    if (zoom < 0)
        zoom = 4;
    aroma_map_set_center(built, (double)props_float(&bag, "lat", 0.0f), (double)props_float(&bag, "lon", 0.0f));
    aroma_map_set_zoom(built, zoom);
    aroma_map_set_show_attribution(built, props_bool(&bag, "attribution", true));
    IncenseNode *items[MAX_ITEM_NODES];
    int n = collect_item_nodes(node, "Marker", items, MAX_ITEM_NODES);
    for (int i = 0; i < n; i++)
    {
        PropBag mb;
        props_collect(items[i], &mb);
        double mlat = (double)props_float(&mb, "lat", 0.0f), mlon = (double)props_float(&mb, "lon", 0.0f);
        uint32_t mc = props_color(&mb, "color", 0xFF0000FF);
        const char *popup = props_get(&mb, "popup"), *icon = props_get(&mb, "icon");
        if (popup)
        {
            char *p = props_str_dup(&mb, "popup", "");
            if (p)
                aroma_map_add_popup_marker(built, mlat, mlon, mc, p);
            free(p);
        }
        else if (icon)
        {
            char *ic = props_str_dup(&mb, "icon", "");
            if (ic)
                aroma_map_add_icon_marker_with_font(built, mlat, mlon, mc, ic, ctx->icon_font ? ctx->icon_font : _widget_font);
            free(ic);
        }
        else
        {
            aroma_map_add_marker(built, mlat, mlon, mc);
        }
        props_free(&mb);
    }
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_menu(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    AromaNode *built = aroma_menu_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0));
    if (!built)
    {
        ERR_SYNTAX_N(node, "Failed to create Menu");
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    aroma_menu_set_font(built, _widget_font);
    aroma_menu_set_icon_font(built, ctx->icon_font ? ctx->icon_font : _widget_font);
    int item_count = 0;
    if (node)
    {
        for (IncenseNode *cur = node->first_child; cur; cur = cur->next_sibling)
        {
            if (!cur || cur->type != INCENSE_OBJECT || !cur->name)
                continue;
            if (strcmp(cur->name, "MenuItem") == 0)
            {
                PropBag ib;
                props_collect(cur, &ib);
                char *text = props_str_dup(&ib, "text", "");
                const char *ir = props_get(&ib, "icon");
                CallbackEntry *oc = resolve_callback(node, &ib, "on_click");
                if (text)
                {
                    if (ir)
                    {
                        const char *resolved = resolve_icon(ir);
                        char *ic = strdup(resolved ? resolved : ir);
                        if (ic)
                        {
                            aroma_menu_add_item_with_icon(built, text, ic, oc ? bridge_void_ptr : NULL, oc);
                            free(ic);
                        }
                    }
                    else
                    {
                        aroma_menu_add_item(built, text, oc ? bridge_void_ptr : NULL, oc);
                    }
                }
                free(text);
                props_free(&ib);
                item_count++;
            }
            else if (strcmp(cur->name, "Separator") == 0)
            {
                aroma_menu_add_separator(built);
            }
        }
    }
    if (!item_count)
        ERR_WARN_N(node, "Menu has no items");
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_radiobutton(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_click = resolve_callback(node, &bag, "on_click");
    char *label = props_str_dup(&bag, "label", "");
    AromaNode *built = NULL;
    if (label)
    {
        built = aroma_radiobutton_create(parent, label, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                         props_int(&bag, "width", 160), props_int(&bag, "height", 32), props_int(&bag, "group", 0));
    }
    if (built)
    {
        if (node)
            node->id = built->node_id;
        aroma_radiobutton_set_font(built, _widget_font);
        if (props_bool(&bag, "selected", false))
            aroma_radiobutton_set_selected(built, true);
        if (on_click)
            aroma_radiobutton_set_callback(built, bridge_void_ptr, on_click);
        int zi = props_int(&bag, "z_index", 0);
        if (zi)
            aroma_node_set_z_index(built, zi);
    }
    else if (label)
    {
        ERR_SYNTAX_N(node, "Failed to create RadioButton");
    }
    free(label);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_sidebar(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_select = resolve_callback(node, &bag, "on_select");
    IncenseNode *items[AROMA_SIDEBAR_MAX_ITEMS];
    int n = collect_item_nodes(node, "Item", items, AROMA_SIDEBAR_MAX_ITEMS);
    char *label_bufs[AROMA_SIDEBAR_MAX_ITEMS];
    const char *labels[AROMA_SIDEBAR_MAX_ITEMS];
    memset(label_bufs, 0, sizeof(label_bufs));
    memset(labels, 0, sizeof(labels));
    bool alloc_ok = true;
    for (int i = 0; i < n; i++)
    {
        PropBag ib;
        props_collect(items[i], &ib);
        label_bufs[i] = props_str_dup(&ib, "text", "");
        if (!label_bufs[i])
        {
            alloc_ok = false;
            label_bufs[i] = strdup("");
        }
        labels[i] = label_bufs[i] ? label_bufs[i] : "";
        props_free(&ib);
    }
    AromaNode *built = NULL;
    if (alloc_ok || n == 0)
    {
        built = aroma_sidebar_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                     props_int(&bag, "width", 200), props_int(&bag, "height", 400), labels, n);
    }
    else
    {
        ERR_SYNTAX_N(node, "Out of memory building Sidebar labels");
    }
    for (int i = 0; i < n; i++)
        free(label_bufs[i]);
    if (!built)
    {
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    aroma_sidebar_set_font(built, _widget_font);
    for (int i = 0; i < n; i++)
    {
        PropBag ib;
        props_collect(items[i], &ib);
        const char *ir = props_get(&ib, "icon");
        if (ir)
        {
            const char *resolved = resolve_icon(ir);
            char *ic = strdup(resolved ? resolved : ir);
            if (ic)
            {
                aroma_sidebar_set_icon(built, i, ic, ctx->icon_font ? ctx->icon_font : _widget_font);
                free(ic);
            }
        }
        props_free(&ib);
    }
    for (int i = 0; i < n; i++)
    {
        AromaNode *cc[MAX_CHILDREN];
        int cc_n = 0;
        for (IncenseNode *ch = items[i]->first_child; ch && cc_n < MAX_CHILDREN; ch = ch->next_sibling)
        {
            if (ch && ch->type == INCENSE_OBJECT)
            {
                AromaNode *bw = build_widget(ch, sp, ctx);
                if (bw)
                    cc[cc_n++] = bw;
            }
        }
        if (cc_n)
            aroma_sidebar_set_content(built, i, cc, cc_n);
    }
    if (on_select)
        aroma_sidebar_set_on_select(built, bridge_node_int, on_select);
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_table(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    IncenseNode *columns[MAX_ITEM_NODES];
    int num_cols = collect_item_nodes(node, "Column", columns, MAX_ITEM_NODES);
    if (!num_cols)
    {
        num_cols = props_int(&bag, "columns", 1);
        if (num_cols <= 0)
            num_cols = 1;
    }
    AromaNode *built = aroma_table_create(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                                          props_int(&bag, "width", 400), props_int(&bag, "height", 300), num_cols);
    if (!built)
    {
        ERR_SYNTAX_N(node, "Failed to create Table");
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    aroma_table_set_font(built, _widget_font);
    for (int i = 0; i < num_cols && i < MAX_ITEM_NODES; i++)
    {
        if (!columns[i])
            continue;
        PropBag cb;
        props_collect(columns[i], &cb);
        char *hdr = props_str_dup(&cb, "header", "");
        if (hdr)
        {
            aroma_table_set_header(built, i, hdr);
            free(hdr);
        }
        int cw = props_int(&cb, "width", 0);
        if (cw > 0)
            aroma_table_set_col_width(built, i, cw);
        props_free(&cb);
    }
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    build_children(node, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_tabs(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    CallbackEntry *on_change = resolve_callback(node, &bag, "on_change");
    IncenseNode *items[AROMA_TABS_MAX];
    int n = collect_item_nodes(node, "Tab", items, AROMA_TABS_MAX);
    char *label_bufs[AROMA_TABS_MAX];
    const char *labels[AROMA_TABS_MAX];
    memset(label_bufs, 0, sizeof(label_bufs));
    memset(labels, 0, sizeof(labels));
    bool alloc_ok = true;
    for (int i = 0; i < n; i++)
    {
        PropBag ib;
        props_collect(items[i], &ib);
        label_bufs[i] = props_str_dup(&ib, "text", "");
        if (!label_bufs[i])
        {
            alloc_ok = false;
            label_bufs[i] = strdup("");
        }
        labels[i] = label_bufs[i] ? label_bufs[i] : "";
        props_free(&ib);
    }
    AromaNode *built = NULL;
    if (alloc_ok || n == 0)
    {
        built = aroma_ui_tabs(parent, props_int(&bag, "x", 0), props_int(&bag, "y", 0),
                              props_int(&bag, "width", 400), props_int(&bag, "height", 48), labels, n, on_change ? bridge_node_int : NULL, on_change, _widget_font);
    }
    else
    {
        ERR_SYNTAX_N(node, "Out of memory building Tabs labels");
    }
    for (int i = 0; i < n; i++)
        free(label_bufs[i]);
    if (!built)
    {
        props_free(&bag);
        return NULL;
    }
    if (node)
        node->id = built->node_id;
    aroma_tabs_set_font(built, _widget_font);
    for (int i = 0; i < n; i++)
    {
        PropBag ib;
        props_collect(items[i], &ib);
        const char *ir = props_get(&ib, "icon");
        if (ir)
        {
            const char *resolved = resolve_icon(ir);
            char *ic = strdup(resolved ? resolved : ir);
            if (ic)
            {
                aroma_tabs_set_icon(built, i, ic, ctx->icon_font ? ctx->icon_font : _widget_font);
                free(ic);
            }
        }
        props_free(&ib);
    }
    for (int i = 0; i < n; i++)
    {
        AromaNode *cc[MAX_CHILDREN];
        int cc_n = 0;
        for (IncenseNode *ch = items[i]->first_child; ch && cc_n < MAX_CHILDREN; ch = ch->next_sibling)
        {
            if (ch && ch->type == INCENSE_OBJECT)
            {
                AromaNode *bw = build_widget(ch, sp, ctx);
                if (bw)
                    cc[cc_n++] = bw;
            }
        }
        if (cc_n)
            aroma_tabs_set_content(built, i, cc, cc_n);
    }
    int zi = props_int(&bag, "z_index", 0);
    if (zi)
        aroma_node_set_z_index(built, zi);
    int selected = props_int(&bag, "selected", 0);
    if (selected)
        aroma_tabs_set_selected(built, selected);
    apply_widget_animations(built, &bag, node);
    maybe_register(&bag, built, ctx);
    props_free(&bag);
    return built;
}

static AromaNode *build_tooltip(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    char *text = props_str_dup(&bag, "text", "");
    static const char *const pos_names[] = {"top", "bottom", "left", "right"};
    static const int pos_values[] = {TOOLTIP_POSITION_TOP, TOOLTIP_POSITION_BOTTOM, TOOLTIP_POSITION_LEFT, TOOLTIP_POSITION_RIGHT};
    AromaTooltipPosition pos = (AromaTooltipPosition)match_enum(node, &bag, "position", TOOLTIP_POSITION_TOP, pos_names, pos_values, 4, "tooltip position", "top, bottom, left, right");
    AromaNode *built = NULL;
    if (text)
    {
        built = aroma_tooltip_create(parent, text, props_int(&bag, "x", 0), props_int(&bag, "y", 0), pos);
        if (built)
            aroma_tooltip_set_font(built, _widget_font);
    }
    free(text);
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}
static AromaNode *build_chip(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!ctx)
        return NULL;
    WIDGET_PREAMBLE(node, sp, ctx);
    
    char *label = props_str_dup(&bag, "label", "");
    const char *icon_raw = props_get(&bag, "icon");
    char *icon = NULL;
    
    if (icon_raw && icon_raw[0])
    {
        const char *resolved = resolve_icon(icon_raw);
        icon = strdup(resolved ? resolved : icon_raw);
        if (!icon)
        {
            LOG_ERROR("Out of memory duplicating icon name");
            free(label);
            props_free(&bag);
            return NULL;
        }
    }
    
    static const char *const type_names[] = {"assist", "filter", "input", "suggestion"};
    static const int type_values[] = {CHIP_TYPE_ASSIST, CHIP_TYPE_FILTER, CHIP_TYPE_INPUT, CHIP_TYPE_SUGGESTION};
    AromaChipType type = (AromaChipType)match_enum(node, &bag, "type", CHIP_TYPE_ASSIST, 
                                                     type_names, type_values, 4, 
                                                     "chip type", 
                                                     "assist, filter, input, suggestion");
    
    AromaNode *built = NULL;
    if (label && label[0])
    {
        built = aroma_chip_create(parent, 
                                  props_int(&bag, "x", 0), 
                                  props_int(&bag, "y", 0), 
                                  label, 
                                  type);
        
        if (built)
        {
            aroma_chip_set_font(built, _widget_font);
            
            if (type == CHIP_TYPE_FILTER && props_bool(&bag, "selected", false))
            {
                aroma_chip_set_selected(built, true);
            }
            
            if (icon && icon[0])
            {
                aroma_chip_set_icon(built, icon, ctx->icon_font ? ctx->icon_font : _widget_font);
            }
            
            int zi = props_int(&bag, "z_index", 0);
            if (zi)
                aroma_node_set_z_index(built, zi);
        }
        else
        {
            ERR_SYNTAX_N(node, "Failed to create Chip widget");
        }
    }
    else
    {
        ERR_WARN_N(node, "Chip requires a 'label' property");
        ERR_SUGGEST("Add 'label' property with chip text");
    }
    
    free(label);
    free(icon);
    
    WIDGET_POSTAMBLE(built, bag, node, ctx);
}
static const WidgetEntry WIDGET_TABLE[] = {
    {"Button", build_button},
    {"Canvas", build_canvas},
    {"Card", build_card},
    {"Checkbox", build_checkbox},
    {"Chip", build_chip},
    {"Container", build_container},
    {"DebugOverlay", build_debugoverlay},
    {"Dialog", build_dialog},
    {"Divider", build_divider},
    {"Dropdown", build_dropdown},
    {"GIF", build_gif},
    {"Icon", build_icon},
    {"IconButton", build_iconbutton},
    {"Image", build_image},
    {"Label", build_label},
    {"ListView", build_listview},
    {"Loading", build_loading},
    {"Map", build_map},
    {"Menu", build_menu},
    {"ProgressBar", build_progressbar},
    {"RadioButton", build_radiobutton},
    {"ScrollView", build_scrollview},
    {"Sidebar", build_sidebar},
    {"Slider", build_slider},
    {"Snackbar", build_snackbar},
    {"Switch", build_switch},
    {"Tab", NULL},
    {"Table", build_table},
    {"Tabs", build_tabs},
    {"Textbox", build_textbox},
    {"Tooltip", build_tooltip},
    {NULL, NULL}};

#define WIDGET_TABLE_COUNT (sizeof(WIDGET_TABLE) / sizeof(WIDGET_TABLE[0]) - 1)

static int widget_cmp(const void *a, const void *b)
{
    if (!a || !b)
        return 0;
    const char *key = (const char *)a;
    const WidgetEntry *entry = (const WidgetEntry *)b;
    if (!entry->name)
        return 1;
    return strcmp(key, entry->name);
}

static bool s_widget_table_checked = false;

static bool verify_widget_table_sorted(void)
{
    for (size_t i = 1; i < WIDGET_TABLE_COUNT; i++)
    {
        if (!WIDGET_TABLE[i - 1].name || !WIDGET_TABLE[i].name ||
            strcmp(WIDGET_TABLE[i - 1].name, WIDGET_TABLE[i].name) >= 0)
        {
            LOG_ERROR("FATAL: WIDGET_TABLE is not sorted: entry %zu ('%s') must come before entry %zu ('%s') for bsearch to work correctly",
                      i - 1, WIDGET_TABLE[i - 1].name ? WIDGET_TABLE[i - 1].name : "(null)",
                      i, WIDGET_TABLE[i].name ? WIDGET_TABLE[i].name : "(null)");
            s_widget_table_disabled = true;
            return false;
        }
    }
    s_widget_table_checked = true;
    return true;
}

static AromaNode *build_widget(IncenseNode *node, AromaNode *sp, BuildCtx *ctx)
{
    if (!node || node->type != INCENSE_OBJECT || !node->name || !ctx)
        return NULL;

    if (strcmp(node->name, "If") == 0 ||
        strcmp(node->name, "Then") == 0 ||
        strcmp(node->name, "Else") == 0)
    {
        return NULL;
    }

    if (!s_widget_table_checked && !s_widget_table_disabled)
        verify_widget_table_sorted();
    if (s_widget_table_disabled)
    {
        ERR_SYNTAX_N(node, "Widget table is corrupted, cannot build any widgets");
        return NULL;
    }
    const WidgetEntry *e = bsearch(node->name, WIDGET_TABLE, WIDGET_TABLE_COUNT, sizeof(WidgetEntry), widget_cmp);
    if (e && e->build)
        return e->build(node, sp, ctx);
    ERR_SYNTAX_N(node, "Unknown widget type '%s'", node->name);
    ERR_SUGGEST("Check widget name spelling or add to WIDGET_TABLE");
    return NULL;
}

static bool embed_buf_append(char **result, size_t *pos, size_t *cap, const char *data, size_t len)
{
    if (!result || !pos || !cap || !data)
        return false;
    if (len == 0)
        return true;
    if (*pos + len + 1 > *cap)
    {
        size_t new_cap = (*pos + len + 1) * 2;
        if (new_cap < *cap)
            new_cap = *cap + len + 4096;
        char *nr = realloc(*result, new_cap);
        if (!nr)
            return false;
        *result = nr;
        *cap = new_cap;
    }
    memcpy(*result + *pos, data, len);
    *pos += len;
    (*result)[*pos] = '\0';
    return true;
}

static bool embed_buf_append_str(char **result, size_t *pos, size_t *cap, const char *s)
{
    if (!s)
        return true;
    return embed_buf_append(result, pos, cap, s, strlen(s));
}
static char *incense_resolve_embed_refs(char *content, EmbedPropStore *props)
{
    if (!content || !props || props->count == 0)
        return content;

    // Calculate maximum possible expansion
    size_t content_len = strlen(content);
    size_t max_expanded = content_len * 2 + 4096;
    char *result = malloc(max_expanded);
    if (!result)
    {
        LOG_ERROR("Out of memory resolving embed references");
        return content;
    }

    size_t out_pos = 0;
    const char *p = content;

    while (*p)
    {
        // Check for embed_ pattern anywhere
        const char *embed_start = strstr(p, "embed_");
        if (embed_start)
        {
            // Copy everything before this match
            size_t before_len = embed_start - p;
            memcpy(result + out_pos, p, before_len);
            out_pos += before_len;

            // Extract the variable name
            const char *var_start = embed_start + 6; // Skip "embed_"
            const char *var_end = var_start;
            while (*var_end && (isalnum((unsigned char)*var_end) || *var_end == '_'))
            {
                var_end++;
            }

            size_t var_len = var_end - var_start;
            if (var_len > 0 && var_len < 64)
            {
                char var_name[64];
                memcpy(var_name, var_start, var_len);
                var_name[var_len] = '\0';

                // Look up the prop
                const char *replacement = NULL;
                for (int i = 0; i < props->count; i++)
                {
                    if (strcmp(props->props[i].key, var_name) == 0)
                    {
                        replacement = props->props[i].value;
                        break;
                    }
                }

                if (replacement)
                {
                    // Copy replacement value
                    size_t repl_len = strlen(replacement);
                    memcpy(result + out_pos, replacement, repl_len);
                    out_pos += repl_len;
                }
                else
                {
                    // Keep original text
                    memcpy(result + out_pos, embed_start, var_end - embed_start);
                    out_pos += var_end - embed_start;
                }

                p = var_end;
            }
            else
            {
                // Invalid variable name, copy embed_ literally
                result[out_pos++] = *p;
                p++;
            }
        }
        else
        {
            // No more embed_ patterns, copy rest
            size_t remaining = strlen(p);
            memcpy(result + out_pos, p, remaining);
            out_pos += remaining;
            break;
        }
    }

    result[out_pos] = '\0';
    free(content);
    return result;
}
static char *incense_resolve_includes_r(const char *source, const char *base_path, EmbedStack *stack, EmbedPropStore *props)
{
    if (!source || !stack)
        return NULL;
    size_t cap = strlen(source) * 2 + 4096;
    char *result = malloc(cap);
    if (!result)
    {
        LOG_ERROR("Out of memory resolving includes");
        return NULL;
    }
    result[0] = '\0';
    size_t pos = 0;
    const char *p = source;

    while (*p)
    {
        if (*p == '@')
        {
            const char *kw = p + 1;
            while (*kw == ' ' || *kw == '\t')
                kw++;
            if (strncmp(kw, "embed", 5) == 0)
            {
                p = kw + 5;
                while (*p == ' ' || *p == '\t')
                    p++;

                if (*p != '"')
                {
                    while (*p && *p != '\n')
                        p++;
                    if (!embed_buf_append_str(&result, &pos, &cap, "// ERROR: Invalid embed syntax\n"))
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }
                p++;
                const char *ps = p;
                while (*p && *p != '"' && *p != '\n')
                    p++;
                if (*p != '"')
                {
                    while (*p && *p != '\n')
                        p++;
                    if (!embed_buf_append_str(&result, &pos, &cap, "// ERROR: Unterminated embed path\n"))
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }
                size_t plen = (size_t)(p - ps);
                if (plen == 0 || plen >= MAX_EMBED_PATH_LEN)
                {
                    p++;
                    if (!embed_buf_append_str(&result, &pos, &cap, "// ERROR: Invalid embed path length\n"))
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }
                char *inc_path = malloc(plen + 1);
                if (!inc_path)
                {
                    LOG_ERROR("Out of memory resolving embed path");
                    free(result);
                    return NULL;
                }
                memcpy(inc_path, ps, plen);
                inc_path[plen] = '\0';
                p++;

                // Parse inline props
                EmbedPropStore inline_props;
                memset(&inline_props, 0, sizeof(inline_props));

                while (*p == ' ' || *p == '\t' || *p == '\n')
                    p++;
                if (*p == '{')
                {
                    p++;
                    while (*p && *p != '}')
                    {
                        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',')
                            p++;
                        if (*p == '}')
                            break;

                        const char *key_start = p;
                        while (*p && *p != ':' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '}')
                            p++;
                        size_t key_len = p - key_start;

                        while (*p == ' ' || *p == '\t' || *p == '\n')
                            p++;
                        if (*p == ':')
                        {
                            p++;
                            while (*p == ' ' || *p == '\t' || *p == '\n')
                                p++;

                            const char *val_start = p;
                            size_t val_len = 0;
                            if (*p == '"')
                            {
                                p++;
                                val_start = p;
                                while (*p && *p != '"')
                                {
                                    if (*p == '\\' && *(p + 1))
                                        p++;
                                    p++;
                                }
                                if (*p == '"')
                                {
                                    val_len = p - val_start;
                                    p++;
                                }
                            }
                            else
                            {
                                while (*p && *p != ',' && *p != '}' && *p != ' ' && *p != '\t' && *p != '\n')
                                    p++;
                                val_len = p - val_start;
                            }

                            if (key_len > 0 && key_len < 64 && val_len > 0 && val_len < 256)
                            {
                                char key[64], value[256];
                                memcpy(key, key_start, key_len);
                                key[key_len] = '\0';
                                memcpy(value, val_start, val_len);
                                value[val_len] = '\0';

                                // Add to inline props
                                if (inline_props.count < MAX_EMBED_PROPS)
                                {
                                    strncpy(inline_props.props[inline_props.count].key, key, sizeof(inline_props.props[0].key) - 1);
                                    strncpy(inline_props.props[inline_props.count].value, value, sizeof(inline_props.props[0].value) - 1);
                                    inline_props.count++;
                                }

                                // Also add to global embed props
                                embed_props_add(key, value);
                            }
                        }
                        while (*p && *p != ',' && *p != '}')
                            p++;
                        if (*p == ',')
                            p++;
                    }
                    if (*p == '}')
                        p++;
                }

                char *full_path = malloc(MAX_EMBED_PATH_LEN);
                if (!full_path)
                {
                    LOG_ERROR("Out of memory resolving embed path");
                    free(inc_path);
                    free(result);
                    return NULL;
                }
                full_path[0] = '\0';
                if (base_path)
                {
                    const char *sep = strrchr(base_path, '/');
                    const char *sep2 = strrchr(base_path, '\\');
                    if (!sep || (sep2 && sep2 > sep))
                        sep = sep2;
                    if (sep)
                    {
                        size_t dl = (size_t)(sep - base_path) + 1;
                        if (dl < (size_t)MAX_EMBED_PATH_LEN)
                        {
                            memcpy(full_path, base_path, dl);
                            snprintf(full_path + dl, (size_t)MAX_EMBED_PATH_LEN - dl, "%s", inc_path);
                        }
                        else
                        {
                            snprintf(full_path, MAX_EMBED_PATH_LEN, "%s", inc_path);
                        }
                    }
                    else
                    {
                        snprintf(full_path, MAX_EMBED_PATH_LEN, "%s", inc_path);
                    }
                }
                else
                {
                    snprintf(full_path, MAX_EMBED_PATH_LEN, "%s", inc_path);
                }
                free(inc_path);
                full_path[MAX_EMBED_PATH_LEN - 1] = '\0';

                if (stack->depth >= MAX_EMBED_DEPTH)
                {
                    free(full_path);
                    if (!embed_buf_append_str(&result, &pos, &cap, "// ERROR: Maximum embed depth exceeded\n"))
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }

                bool circular = false;
                for (size_t i = 0; i < stack->depth; i++)
                {
                    if (strcmp(stack->paths[i], full_path) == 0)
                    {
                        circular = true;
                        break;
                    }
                }
                if (circular)
                {
                    char err_buf[64 + MAX_EMBED_PATH_LEN];
                    snprintf(err_buf, sizeof(err_buf), "// ERROR: Circular embed detected: %s\n", full_path);
                    bool ok = embed_buf_append_str(&result, &pos, &cap, err_buf);
                    free(full_path);
                    if (!ok)
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }

                FILE *fp = fopen(full_path, "rb");
                char *file_content = NULL;
                if (fp)
                {
                    if (fseek(fp, 0, SEEK_END) == 0)
                    {
                        long fsz = ftell(fp);
                        if (fsz > 0 && fsz <= MAX_EMBED_SIZE && fseek(fp, 0, SEEK_SET) == 0)
                        {
                            file_content = malloc((size_t)fsz + 1);
                            if (file_content)
                            {
                                size_t rd = fread(file_content, 1, (size_t)fsz, fp);
                                file_content[rd] = '\0';
                            }
                        }
                    }
                    fclose(fp);
                }
                if (!file_content)
                {
                    char err_buf[64 + MAX_EMBED_PATH_LEN];
                    snprintf(err_buf, sizeof(err_buf), "// ERROR: Failed to embed file: %s\n", full_path);
                    bool ok = embed_buf_append_str(&result, &pos, &cap, err_buf);
                    free(full_path);
                    if (!ok)
                    {
                        free(result);
                        return NULL;
                    }
                    continue;
                }

                // MERGE PROPS: Global props + inline props (inline override global)
                EmbedPropStore merged_props = s_embed_props; // Copy global props

                // Add/override with inline props
                for (int i = 0; i < inline_props.count; i++)
                {
                    bool found = false;
                    for (int j = 0; j < merged_props.count; j++)
                    {
                        if (strcmp(merged_props.props[j].key, inline_props.props[i].key) == 0)
                        {
                            strncpy(merged_props.props[j].value, inline_props.props[i].value,
                                    sizeof(merged_props.props[j].value) - 1);
                            found = true;
                            break;
                        }
                    }
                    if (!found && merged_props.count < MAX_EMBED_PROPS)
                    {
                        strncpy(merged_props.props[merged_props.count].key, inline_props.props[i].key,
                                sizeof(merged_props.props[0].key) - 1);
                        strncpy(merged_props.props[merged_props.count].value, inline_props.props[i].value,
                                sizeof(merged_props.props[0].value) - 1);
                        merged_props.count++;
                    }
                }

                // RESOLVE EMBED REFERENCES IN FILE CONTENT
                file_content = incense_resolve_embed_refs(file_content, &merged_props);

                strncpy(stack->paths[stack->depth], full_path, MAX_EMBED_PATH_LEN - 1);
                stack->paths[stack->depth][MAX_EMBED_PATH_LEN - 1] = '\0';
                stack->depth++;
                char *processed = incense_resolve_includes_r(file_content, full_path, stack, props);
                stack->depth--;
                free(file_content);
                free(full_path);

                if (processed)
                {
                    bool ok = embed_buf_append_str(&result, &pos, &cap, processed);
                    free(processed);
                    if (!ok)
                    {
                        free(result);
                        return NULL;
                    }
                }
                else
                {
                    if (!embed_buf_append_str(&result, &pos, &cap, "// ERROR: Failed to process embedded file\n"))
                    {
                        free(result);
                        return NULL;
                    }
                }
                continue;
            }
        }
        if (!embed_buf_append(&result, &pos, &cap, p, 1))
        {
            free(result);
            return NULL;
        }
        p++;
    }
    return result;
}
static char *incense_resolve_includes(const char *source, const char *base_path)
{
    if (!source)
        return NULL;
    embed_props_clear();
    embed_props_parse_from_source(source);
    EmbedStack *stack = calloc(1, sizeof(EmbedStack));
    if (!stack)
    {
        LOG_ERROR("Out of memory resolving includes");
        return NULL;
    }
    stack->depth = 0;
    char *result = incense_resolve_includes_r(source, base_path, stack, &s_embed_props);
    free(stack);
    return result;
}

static time_t get_file_modified_time(const char *path)
{
    if (!path || !path[0])
        return 0;
    struct stat s;
    return (stat(path, &s) == 0) ? s.st_mtime : 0;
}

static bool read_entire_file(const char *path, char **out_data, size_t *out_size)
{
    if (!path || !path[0] || !out_data || !out_size)
        return false;
    *out_data = NULL;
    *out_size = 0;
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return false;
    }
    long size = ftell(fp);
    if (size < 0)
    {
        fclose(fp);
        return false;
    }
    if ((size_t)size > MAX_LOAD_FILE_SIZE)
    {
        LOG_ERROR("File '%s' exceeds maximum allowed size", path);
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return false;
    }
    char *raw = malloc((size_t)size + 1);
    if (!raw)
    {
        LOG_ERROR("Out of memory reading file '%s'", path);
        fclose(fp);
        return false;
    }
    size_t rd = 0;
    if (size > 0)
    {
        rd = fread(raw, 1, (size_t)size, fp);
        if (rd != (size_t)size && ferror(fp))
        {
            LOG_ERROR("Error reading file '%s'", path);
            fclose(fp);
            free(raw);
            return false;
        }
    }
    fclose(fp);
    raw[rd] = '\0';
    *out_data = raw;
    *out_size = rd;
    return true;
}

static int find_watcher_index(const char *path)
{
    if (!path)
        return -1;
    for (int i = 0; i < s_hot_watcher_count; i++)
        if (strcmp(s_hot_watchers[i].file_path, path) == 0)
            return i;
    return -1;
}
static bool reload_hot_window(HotReloadWatcher *watcher)
{
    if (!watcher || !watcher->active || !watcher->window)
        return false;

    struct stat buffer;
    if (stat(watcher->file_path, &buffer) != 0)
    {
        if (watcher->on_error)
        {
            char e[1024];
            snprintf(e, sizeof(e), "UI file not found: %s", watcher->file_path);
            watcher->on_error(e);
        }
        return false;
    }
    char *raw = NULL;
    size_t raw_size = 0;
    if (!read_entire_file(watcher->file_path, &raw, &raw_size))
    {
        if (watcher->on_error)
        {
            char e[1024];
            snprintf(e, sizeof(e), "Cannot read UI file: %s", watcher->file_path);
            watcher->on_error(e);
        }
        return false;
    }

    embed_props_clear();
    embed_props_parse_from_source(raw);
    char *processed = incense_resolve_includes(raw, watcher->file_path);
    free(raw);
    if (!processed)
    {
        if (watcher->on_error)
            watcher->on_error("Failed to resolve embeds in UI file");
        return false;
    }
    IncenseDocument *doc = IncenseParseString(processed);
    free(processed);
    if (!doc)
    {
        if (watcher->on_error)
            watcher->on_error("Failed to parse UI file");
        return false;
    }

    IncenseStateClearObservers();
    s_conditional_count = 0;
    memset(s_conditional_states, 0, sizeof(s_conditional_states));

    err_clear();
    aroma_animation_manager_init();
    if (!s_icon_init)
        icon_build_table();
    if (!s_cb_init)
        cb_init_buckets();

    if (!doc->root || !doc->root->name || strcmp(doc->root->name, "Window") != 0)
    {
        if (watcher->on_error)
            watcher->on_error("Root object must be 'Window'");
        IncenseDestroy(doc);
        return false;
    }
    if (watcher->out_registry)
    {
        if (*watcher->out_registry)
        {
            registry_init(&(*watcher->out_registry)->reg);
        }
        else
        {
            *watcher->out_registry = calloc(1, sizeof(IncenseRegistry));
            if (*watcher->out_registry)
                registry_init(&(*watcher->out_registry)->reg);
        }
    }

    FontRegistry *freg = calloc(1, sizeof(FontRegistry));
    if (freg)
    {
        font_registry_init(freg);
        if (s_global_font_registry)
        {
            for (int i = 0; i < s_global_font_registry->count && freg->count < MAX_FONTS; i++)
                font_registry_register(freg, s_global_font_registry->items[i].name, s_global_font_registry->items[i].font);
        }
    }
    else
    {
        LOG_ERROR("Out of memory allocating font registry during hot reload");
    }

    AromaNode *root_node = (AromaNode *)watcher->window;
    if (!root_node)
    {
        IncenseDestroy(doc);
        free(freg);
        return false;
    }

    aroma_animation_cleanup_all();

    uint64_t child_count = root_node->child_count;
    if (child_count > AROMA_MAX_CHILD_NODES)
    {
        LOG_ERROR("Window child_count (%llu) exceeds maximum, clamping", (unsigned long long)child_count);
        child_count = AROMA_MAX_CHILD_NODES;
    }
    AromaNode *child_nodes[AROMA_MAX_CHILD_NODES];
    for (uint64_t i = 0; i < child_count; i++)
        child_nodes[i] = root_node->child_nodes[i];
    for (uint64_t i = 0; i < child_count; i++)
    {
        if (child_nodes[i])
        {
            __destroy_node_tree(child_nodes[i]);
        }
    }
    root_node->child_count = 0;
    memset(root_node->child_nodes, 0, sizeof(root_node->child_nodes));

    BuildCtx ctx = {
        .registry = (watcher->out_registry && *watcher->out_registry) ? &(*watcher->out_registry)->reg : NULL,
        .font_registry = freg,
        .default_font = watcher->font,
        .icon_font = watcher->icon_font};
    build_children(doc->root, root_node, &ctx);
    IncenseDestroy(doc);
    free(freg);

    watcher->last_modified = get_file_modified_time(watcher->file_path);
    if (watcher->on_reload)
        watcher->on_reload(watcher->window);
    aroma_node_invalidate_tree(root_node);
    LOG_INFO("Hot reload: UI updated in same window from '%s'", watcher->file_path);
    return true;
}

static AromaWindow *IncenseLoadCore(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    err_clear();
    aroma_animation_manager_init();
    s_conditional_count = 0;
    memset(s_conditional_states, 0, sizeof(s_conditional_states));
    if (!s_icon_init)
        icon_build_table();
    if (!s_cb_init)
        cb_init_buckets();
    if (out_registry)
        *out_registry = NULL;
    if (!doc || !doc->root)
    {
        err_add(INCENSE_ERROR_SYNTAX, 0, 0, "Invalid document or missing root node");
        return NULL;
    }
    IncenseNode *root = doc->root;
    if (!root->name || strcmp(root->name, "Window") != 0)
    {
        ERR_SYNTAX_N(root, "Root object must be 'Window', got '%s'", root->name ? root->name : "(null)");
        ERR_SUGGEST("Wrap your UI in a Window {} block");
        return NULL;
    }
    PropBag bag;
    props_collect(root, &bag);
    validate_properties(root, &bag);
    int w = props_int(&bag, "width", 800), h = props_int(&bag, "height", 600);
    if (w <= 0 || h <= 0)
    {
        w = 800;
        h = 600;
    }
    char *title = props_str_dup(&bag, "title", "Incense App");
    AromaWindow *window = title ? aroma_ui_create_window(title, w, h) : NULL;
    free(title);
    props_free(&bag);
    if (!window)
    {
        ERR_SYNTAX_N(root, "Failed to create window");
        return NULL;
    }

    IncenseRegistry *ireg = calloc(1, sizeof(IncenseRegistry));
    if (ireg)
        registry_init(&ireg->reg);
    else
        LOG_ERROR("Out of memory allocating widget registry");

    FontRegistry *freg = calloc(1, sizeof(FontRegistry));
    if (freg)
    {
        font_registry_init(freg);
        if (s_global_font_registry)
        {
            for (int i = 0; i < s_global_font_registry->count && freg->count < MAX_FONTS; i++)
                font_registry_register(freg, s_global_font_registry->items[i].name, s_global_font_registry->items[i].font);
        }
    }
    else
    {
        LOG_ERROR("Out of memory allocating font registry");
    }

    AromaNode *root_node = (AromaNode *)window;
    BuildCtx ctx = {.registry = ireg ? &ireg->reg : NULL, .font_registry = freg, .default_font = font, .icon_font = icon_font};
    build_children(root, root_node, &ctx);

    free(freg);
    if (out_registry)
        *out_registry = ireg;
    else if (ireg)
        free(ireg);
    return window;
}

static AromaWindow *load_file_core(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (out_registry)
        *out_registry = NULL;
    if (!path || !path[0])
    {
        LOG_ERROR("IncenseLoadFile called with invalid path");
        return NULL;
    }
    char *raw = NULL;
    size_t raw_size = 0;
    if (!read_entire_file(path, &raw, &raw_size))
    {
        LOG_ERROR("Failed to read UI file '%s'", path);
        return NULL;
    }
    embed_props_clear();
    embed_props_parse_from_source(raw);
    char *processed = incense_resolve_includes(raw, path);
    free(raw);
    if (!processed)
    {
        LOG_ERROR("Failed to resolve embeds in '%s'", path);
        return NULL;
    }
    IncenseDocument *doc = IncenseParseString(processed);
    free(processed);
    if (!doc)
    {
        LOG_ERROR("Failed to parse UI file '%s'", path);
        return NULL;
    }
    AromaWindow *win = IncenseLoadCore(doc, font, icon_font, out_registry);
    IncenseDestroy(doc);
    return win;
}

static AromaWindow *load_string_core(const char *source, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (out_registry)
        *out_registry = NULL;
    if (!source)
    {
        LOG_ERROR("IncenseLoadString called with NULL source");
        return NULL;
    }
    embed_props_clear();
    embed_props_parse_from_source(source);
    char *processed = incense_resolve_includes(source, NULL);
    IncenseDocument *doc = IncenseParseString(processed ? processed : source);
    free(processed);
    if (!doc)
    {
        LOG_ERROR("Failed to parse UI source string");
        return NULL;
    }
    AromaWindow *win = IncenseLoadCore(doc, font, icon_font, out_registry);
    IncenseDestroy(doc);
    return win;
}

AromaWindow *IncenseLoad(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font)
{
    return IncenseLoadCore(doc, font, icon_font, NULL);
}

AromaWindow *IncenseLoadFile(const char *path, AromaFont *font, AromaFont *icon_font)
{
    return load_file_core(path, font, icon_font, NULL);
}

AromaWindow *IncenseLoadString(const char *source, AromaFont *font, AromaFont *icon_font)
{
    return load_string_core(source, font, icon_font, NULL);
}

AromaWindow *IncenseLoadEx(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (out_registry)
        *out_registry = NULL;
    return IncenseLoadCore(doc, font, icon_font, out_registry);
}

AromaWindow *IncenseLoadFileEx(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (out_registry)
        *out_registry = NULL;
    return load_file_core(path, font, icon_font, out_registry);
}

AromaWindow *IncenseLoadStringEx(const char *source, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (out_registry)
        *out_registry = NULL;
    return load_string_core(source, font, icon_font, out_registry);
}

int IncenseHotReloadStart(const char *path, AromaFont *font, AromaFont *icon_font, IncenseRegistry **out_registry)
{
    if (!path || !path[0] || !font)
    {
        LOG_ERROR("Invalid parameters for hot reload");
        return -1;
    }
    if (strlen(path) >= sizeof(s_hot_watchers[0].file_path))
    {
        LOG_ERROR("Hot reload path too long, maximum %zu characters", sizeof(s_hot_watchers[0].file_path) - 1);
        return -1;
    }
    int existing = find_watcher_index(path);
    if (existing >= 0)
    {
        LOG_WARNING("Already watching file: %s", path);
        return existing;
    }
    if (s_hot_watcher_count >= MAX_HOT_RELOAD_WATCHERS)
    {
        LOG_ERROR("Maximum hot reload watchers reached (%d)", MAX_HOT_RELOAD_WATCHERS);
        return -1;
    }
    HotReloadWatcher *watcher = &s_hot_watchers[s_hot_watcher_count];
    memset(watcher, 0, sizeof(HotReloadWatcher));
    strncpy(watcher->file_path, path, sizeof(watcher->file_path) - 1);
    watcher->file_path[sizeof(watcher->file_path) - 1] = '\0';
    watcher->font = font;
    watcher->icon_font = icon_font;
    watcher->out_registry = out_registry;
    watcher->active = true;
    AromaWindow *window = IncenseLoadFileEx(path, font, icon_font, out_registry);
    if (!window)
    {
        LOG_ERROR("Failed to perform initial load for hot reload: %s", path);
        memset(watcher, 0, sizeof(HotReloadWatcher));
        return -1;
    }
    watcher->window = window;
    watcher->last_modified = get_file_modified_time(path);
    int index = s_hot_watcher_count++;
    LOG_INFO("Hot reload started for: %s (watcher #%d)", path, index);
    return index;
}

int IncenseHotReloadCheck(void)
{
    int reloaded = 0;
    for (int i = 0; i < s_hot_watcher_count; i++)
    {
        if (!s_hot_watchers[i].active)
            continue;
        time_t mtime = get_file_modified_time(s_hot_watchers[i].file_path);
        if (mtime > s_hot_watchers[i].last_modified)
        {
            LOG_INFO("Change detected in: %s", s_hot_watchers[i].file_path);
            if (reload_hot_window(&s_hot_watchers[i]))
                reloaded++;
        }
    }
    return reloaded;
}

bool IncenseHotReloadForce(int watcher_index)
{
    if (watcher_index < 0 || watcher_index >= s_hot_watcher_count)
    {
        LOG_ERROR("Invalid watcher index: %d", watcher_index);
        return false;
    }
    if (!s_hot_watchers[watcher_index].active)
    {
        LOG_WARNING("Watcher #%d is not active", watcher_index);
        return false;
    }
    LOG_INFO("Forcing reload of: %s", s_hot_watchers[watcher_index].file_path);
    return reload_hot_window(&s_hot_watchers[watcher_index]);
}

int IncenseHotReloadForceAll(void)
{
    int reloaded = 0;
    for (int i = 0; i < s_hot_watcher_count; i++)
        if (s_hot_watchers[i].active && reload_hot_window(&s_hot_watchers[i]))
            reloaded++;
    return reloaded;
}

AromaWindow *IncenseHotReloadGetWindow(int watcher_index)
{
    if (watcher_index < 0 || watcher_index >= s_hot_watcher_count)
        return NULL;
    return s_hot_watchers[watcher_index].window;
}

void IncenseHotReloadSetCallback(int watcher_index, void (*on_reload)(AromaWindow *))
{
    if (watcher_index >= 0 && watcher_index < s_hot_watcher_count)
        s_hot_watchers[watcher_index].on_reload = on_reload;
}

void IncenseHotReloadSetErrorCallback(int watcher_index, void (*on_error)(const char *))
{
    if (watcher_index >= 0 && watcher_index < s_hot_watcher_count)
        s_hot_watchers[watcher_index].on_error = on_error;
}

void IncenseHotReloadStop(int watcher_index)
{
    if (watcher_index >= 0 && watcher_index < s_hot_watcher_count)
    {
        s_hot_watchers[watcher_index].active = false;
        LOG_INFO("Hot reload stopped for watcher #%d", watcher_index);
    }
}

void IncenseHotReloadStopAll(void)
{
    for (int i = 0; i < s_hot_watcher_count; i++)
        s_hot_watchers[i].active = false;
    LOG_INFO("All hot reload watchers stopped");
}