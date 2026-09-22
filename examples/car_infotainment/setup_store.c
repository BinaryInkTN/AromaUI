#include "setup_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#define SETUP_MAX_ENTRIES 64
#define SETUP_MAX_KEY 64
#define SETUP_MAX_VALUE 512
#define SETUP_MAX_LINE 640

typedef struct
{
    char key[SETUP_MAX_KEY];
    char value[SETUP_MAX_VALUE];
} SetupEntry;

static SetupEntry s_entries[SETUP_MAX_ENTRIES];
static int s_entry_count = 0;
static char s_path[512] = "";

static void mkdir_p(const char *path)
{
    char tmp[480];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t n = strlen(tmp);
    for (size_t i = 1; i <= n; i++)
    {
        if (tmp[i] == '/' || tmp[i] == '\0')
        {
            char c = tmp[i];
            tmp[i] = '\0';
            mkdir(tmp, 0755);
            tmp[i] = c;
        }
    }
}

static void setup_store_resolve_path(void)
{
    if (s_path[0])
        return;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[480] = "";
    if (xdg && xdg[0])
    {
        snprintf(base, sizeof(base), "%s/aroma", xdg);
    }
    else
    {
        const char *home = getenv("HOME");
        if (home && home[0])
            snprintf(base, sizeof(base), "%s/.config/aroma", home);
    }
    if (base[0])
    {
        /* mkdir -p equivalent ($HOME/.config is assumed to exist). */
        mkdir_p(base);
        snprintf(s_path, sizeof(s_path), "%s/infotainment.conf", base);
    }
    else
    {
        snprintf(s_path, sizeof(s_path), "aroma_setup.conf");
    }
}

static SetupEntry *find_entry(const char *key)
{
    if (!key)
        return NULL;
    for (int i = 0; i < s_entry_count; i++)
    {
        if (strcmp(s_entries[i].key, key) == 0)
            return &s_entries[i];
    }
    return NULL;
}

static void trim(char *s)
{
    if (!s)
        return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
    size_t off = 0;
    while (s[off] == ' ' || s[off] == '\t')
        off++;
    if (off > 0)
        memmove(s, s + off, n - off + 1);
}

bool setup_store_load(void)
{
    setup_store_resolve_path();
    s_entry_count = 0;
    FILE *f = fopen(s_path, "r");
    if (!f)
        return true; /* First boot: no file yet, defaults apply. */
    char line[SETUP_MAX_LINE];
    while (fgets(line, sizeof(line), f))
    {
        trim(line);
        if (!line[0] || line[0] == '#')
            continue;
        char *eq = strchr(line, '=');
        if (!eq || s_entry_count >= SETUP_MAX_ENTRIES)
            continue;
        *eq = '\0';
        char *key = line;
        char *value = eq + 1;
        trim(key);
        trim(value);
        if (!key[0])
            continue;
        SetupEntry *e = find_entry(key);
        if (!e)
            e = &s_entries[s_entry_count++];
        snprintf(e->key, sizeof(e->key), "%.*s",
                 (int)sizeof(e->key) - 1, key);
        snprintf(e->value, sizeof(e->value), "%.*s",
                 (int)sizeof(e->value) - 1, value);
    }
    fclose(f);
    return true;
}

bool setup_store_save(void)
{
    setup_store_resolve_path();
    FILE *f = fopen(s_path, "w");
    if (!f)
        return false;
    fprintf(f, "# Aroma infotainment settings (written automatically)\n");
    for (int i = 0; i < s_entry_count; i++)
        fprintf(f, "%s=%s\n", s_entries[i].key, s_entries[i].value);
    fclose(f);
    return true;
}

const char *setup_store_get(const char *key, const char *dflt)
{
    SetupEntry *e = find_entry(key);
    return e ? e->value : dflt;
}

void setup_store_set(const char *key, const char *value)
{
    if (!key || !key[0] || !value)
        return;
    SetupEntry *e = find_entry(key);
    if (!e)
    {
        if (s_entry_count >= SETUP_MAX_ENTRIES)
            return;
        e = &s_entries[s_entry_count++];
        snprintf(e->key, sizeof(e->key), "%s", key);
    }
    snprintf(e->value, sizeof(e->value), "%s", value);
}

int setup_store_get_int(const char *key, int dflt)
{
    const char *v = setup_store_get(key, NULL);
    if (!v || !v[0])
        return dflt;
    return atoi(v);
}

void setup_store_set_int(const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    setup_store_set(key, buf);
}

bool setup_is_complete(void)
{
    return setup_store_get_int("setup_complete", 0) == 1;
}

void setup_mark_complete(void)
{
    setup_store_set_int("setup_complete", 1);
    setup_store_save();
}

const char *setup_store_path(void)
{
    setup_store_resolve_path();
    return s_path;
}
