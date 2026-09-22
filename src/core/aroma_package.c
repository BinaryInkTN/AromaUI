#include "aroma_package.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <dlfcn.h>
#endif

static void set_err(char *buf, size_t len, const char *msg)
{
    if (buf && len > 0)
    {
        snprintf(buf, len, "%s", msg ? msg : "unknown error");
    }
}

static void copy_str_field(char *dst, size_t dst_len, const char *src)
{
    if (!src)
    {
        if (dst_len > 0)
            dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static const char *json_str(const cJSON *obj, const char *key)
{
    if (!obj || !key)
        return NULL;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring)
        return NULL;
    return item->valuestring;
}

/* Reverse-dns id: 2+ dot-separated parts, each starting with alnum and
 * containing only [a-z0-9_]. Lowercase enforced by convention. */
static bool id_part_ok(const char *start, size_t len)
{
    if (len == 0 || len >= 64)
        return false;
    if (!isalnum((unsigned char)start[0]))
        return false;
    for (size_t i = 0; i < len; i++)
    {
        char c = start[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) ||
              c == '_'))
            return false;
    }
    return true;
}

static bool id_format_ok(const char *id)
{
    if (!id || !id[0])
        return false;
    int parts = 0;
    const char *p = id;
    while (*p)
    {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (!id_part_ok(p, len))
            return false;
        parts++;
        if (!dot)
            break;
        p = dot + 1;
        if (!*p)
            return false;
    }
    return parts >= 2;
}

bool aroma_package_validate_manifest(const AromaPackageManifest *m,
                                     char *err_buf, size_t err_buf_len)
{
    if (!m)
    {
        set_err(err_buf, err_buf_len, "manifest is NULL");
        return false;
    }
    if (!id_format_ok(m->id))
    {
        set_err(err_buf, err_buf_len,
                "invalid id: use reverse-dns lowercase, e.g. \"com.example.calc\"");
        return false;
    }
    if (!m->name[0])
    {
        set_err(err_buf, err_buf_len, "missing required field: name");
        return false;
    }
    if (!m->version[0])
    {
        set_err(err_buf, err_buf_len, "missing required field: version");
        return false;
    }
    if (m->version_code < 0)
    {
        set_err(err_buf, err_buf_len, "version_code must be >= 0");
        return false;
    }
    if (m->min_abi < 0 || m->min_abi > AROMA_PACKAGE_ABI_VERSION)
    {
        set_err(err_buf, err_buf_len, "min_abi is newer than this host supports");
        return false;
    }
    if (m->entry[0] && (strstr(m->entry, "..") || m->entry[0] == '/'))
    {
        set_err(err_buf, err_buf_len, "entry must be a relative path without '..'");
        return false;
    }
    if (m->plugin[0] && (strstr(m->plugin, "..") || m->plugin[0] == '/' ||
                          strchr(m->plugin, '/')))
    {
        set_err(err_buf, err_buf_len,
                "plugin must be a bare filename inside the package");
        return false;
    }
    if (strcmp(m->chrome, "host") != 0 && strcmp(m->chrome, "self") != 0)
    {
        set_err(err_buf, err_buf_len, "chrome must be \"host\" or \"self\"");
        return false;
    }
    if (m->rating < 0.0f || m->rating > 5.0f)
    {
        set_err(err_buf, err_buf_len, "rating must be between 0 and 5");
        return false;
    }
    if (m->rating_count < 0)
    {
        set_err(err_buf, err_buf_len, "rating_count must be >= 0");
        return false;
    }
    if (m->downloads < 0)
    {
        set_err(err_buf, err_buf_len, "downloads must be >= 0");
        return false;
    }
    return true;
}

bool aroma_package_parse_manifest(const char *json,
                                  AromaPackageManifest *out,
                                  char *err_buf, size_t err_buf_len)
{
    if (!json || !out)
    {
        set_err(err_buf, err_buf_len, "invalid arguments");
        return false;
    }
    memset(out, 0, sizeof(*out));
    /* Sensible defaults before parsing. */
    copy_str_field(out->icon, sizeof(out->icon), "AROMA_ICON_WIDGETS");
    copy_str_field(out->entry, sizeof(out->entry), "ui.aroma");
    copy_str_field(out->chrome, sizeof(out->chrome), "host");
    copy_str_field(out->category, sizeof(out->category), "Apps");
    out->min_abi = 1;
    out->rating = 0.0f;
    out->rating_count = 0;
    out->downloads = 0;
    out->featured = false;

    cJSON *root = cJSON_Parse(json);
    if (!root)
    {
        set_err(err_buf, err_buf_len, "manifest.json is not valid JSON");
        return false;
    }

    const char *id = json_str(root, "id");
    const char *name = json_str(root, "name");
    const char *version = json_str(root, "version");
    const cJSON *vc = cJSON_GetObjectItemCaseSensitive(root, "version_code");
    const char *icon = json_str(root, "icon");
    const char *author = json_str(root, "author");
    const char *desc = json_str(root, "description");
    const char *entry = json_str(root, "entry");
    const char *plugin = json_str(root, "plugin");
    const char *chrome = json_str(root, "chrome");
    const cJSON *abi = cJSON_GetObjectItemCaseSensitive(root, "min_abi");
    const char *category = json_str(root, "category");
    const cJSON *rating_j = cJSON_GetObjectItemCaseSensitive(root, "rating");
    const cJSON *rating_count_j = cJSON_GetObjectItemCaseSensitive(root, "rating_count");
    const cJSON *downloads_j = cJSON_GetObjectItemCaseSensitive(root, "downloads");
    const cJSON *featured_j = cJSON_GetObjectItemCaseSensitive(root, "featured");

    /* Reject over-long fields instead of silently truncating them into
     * a different (but valid-looking) manifest. */
    struct
    {
        const char *key;
        const char *val;
        size_t cap;
    } str_fields[] = {
        {"id", id, sizeof(out->id)},
        {"name", name, sizeof(out->name)},
        {"version", version, sizeof(out->version)},
        {"icon", icon, sizeof(out->icon)},
        {"author", author, sizeof(out->author)},
        {"description", desc, sizeof(out->description)},
        {"entry", entry, sizeof(out->entry)},
        {"plugin", plugin, sizeof(out->plugin)},
        {"chrome", chrome, sizeof(out->chrome)},
        {"category", category, sizeof(out->category)},
    };
    for (size_t i = 0; i < sizeof(str_fields) / sizeof(str_fields[0]); i++)
    {
        if (str_fields[i].val && strlen(str_fields[i].val) >= str_fields[i].cap)
        {
            cJSON_Delete(root);
            if (err_buf && err_buf_len > 0)
                snprintf(err_buf, err_buf_len, "field too long: %s",
                         str_fields[i].key);
            return false;
        }
    }
    /* version_code / min_abi must be plain integers (booleans are not
     * numbers for this purpose; floats are truncated by cJSON). */
    if (vc && !cJSON_IsNumber(vc))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "version_code must be an integer");
        return false;
    }
    if (vc && cJSON_IsNumber(vc) &&
        vc->valuedouble != (double)vc->valueint)
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "version_code must be an integer");
        return false;
    }
    if (abi && !cJSON_IsNumber(abi))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "min_abi must be an integer");
        return false;
    }
    if (abi && cJSON_IsNumber(abi) &&
        abi->valuedouble != (double)abi->valueint)
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "min_abi must be an integer");
        return false;
    }
    if (rating_j && !cJSON_IsNumber(rating_j))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "rating must be a number 0-5");
        return false;
    }
    if (rating_count_j && (!cJSON_IsNumber(rating_count_j) ||
        rating_count_j->valuedouble != (double)rating_count_j->valueint))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "rating_count must be an integer");
        return false;
    }
    if (downloads_j && (!cJSON_IsNumber(downloads_j) ||
        downloads_j->valuedouble != (double)downloads_j->valueint))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "downloads must be an integer");
        return false;
    }
    if (featured_j && !cJSON_IsBool(featured_j) && !cJSON_IsNumber(featured_j))
    {
        cJSON_Delete(root);
        set_err(err_buf, err_buf_len, "featured must be a boolean");
        return false;
    }

    if (id)
        copy_str_field(out->id, sizeof(out->id), id);
    if (name)
        copy_str_field(out->name, sizeof(out->name), name);
    if (version)
        copy_str_field(out->version, sizeof(out->version), version);
    if (vc && cJSON_IsNumber(vc))
        out->version_code = vc->valueint;
    if (icon)
        copy_str_field(out->icon, sizeof(out->icon), icon);
    if (author)
        copy_str_field(out->author, sizeof(out->author), author);
    if (desc)
        copy_str_field(out->description, sizeof(out->description), desc);
    if (entry)
        copy_str_field(out->entry, sizeof(out->entry), entry);
    if (plugin)
        copy_str_field(out->plugin, sizeof(out->plugin), plugin);
    if (chrome)
        copy_str_field(out->chrome, sizeof(out->chrome), chrome);
    if (abi && cJSON_IsNumber(abi))
        out->min_abi = abi->valueint;
    if (category)
        copy_str_field(out->category, sizeof(out->category), category);
    if (rating_j && cJSON_IsNumber(rating_j))
        out->rating = (float)rating_j->valuedouble;
    if (rating_count_j && cJSON_IsNumber(rating_count_j))
        out->rating_count = rating_count_j->valueint;
    if (downloads_j && cJSON_IsNumber(downloads_j))
        out->downloads = (long)downloads_j->valuedouble;
    if (featured_j)
        out->featured = cJSON_IsTrue(featured_j) ||
            (cJSON_IsNumber(featured_j) && featured_j->valueint != 0);
    if (!out->category[0])
        copy_str_field(out->category, sizeof(out->category), "Apps");

    cJSON_Delete(root);

    if (!aroma_package_validate_manifest(out, err_buf, err_buf_len))
        return false;
    return true;
}

bool aroma_package_read_file(const char *path, char **out_buf, size_t *out_len)
{
    if (!path || !out_buf)
        return false;
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        return false;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf)
    {
        fclose(f);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size)
    {
        free(buf);
        return false;
    }
    buf[got] = '\0';
    *out_buf = buf;
    if (out_len)
        *out_len = got;
    return true;
}

bool aroma_package_load_manifest_file(const char *path,
                                      AromaPackageManifest *out,
                                      char *err_buf, size_t err_buf_len)
{
    char *json = NULL;
    if (!aroma_package_read_file(path, &json, NULL))
    {
        set_err(err_buf, err_buf_len, "cannot read manifest.json");
        return false;
    }
    bool ok = aroma_package_parse_manifest(json, out, err_buf, err_buf_len);
    free(json);
    return ok;
}

bool aroma_package_join_path(char *out, size_t out_len,
                             const char *dir, const char *name)
{
    if (!out || out_len == 0 || !dir || !name)
        return false;
    size_t dir_len = strlen(dir);
    while (dir_len > 0 && dir[dir_len - 1] == '/')
        dir_len--;
    while (*name == '/')
        name++;
    int n = snprintf(out, out_len, "%.*s/%s", (int)dir_len, dir, name);
    return n > 0 && (size_t)n < out_len;
}

int aroma_package_compare_versions(const char *a, const char *b)
{
    if (!a)
        a = "";
    if (!b)
        b = "";
    while (*a || *b)
    {
        char *end_a = NULL;
        char *end_b = NULL;
        long na = strtol(a, &end_a, 10);
        long nb = strtol(b, &end_b, 10);
        if (na < nb)
            return -1;
        if (na > nb)
            return 1;
        /* Skip one separator run (".", "-", ...). */
        a = (end_a == a) ? a : end_a;
        b = (end_b == b) ? b : end_b;
        while (*a && !isdigit((unsigned char)*a))
            a++;
        while (*b && !isdigit((unsigned char)*b))
            b++;
        if (!*a && !*b)
            return 0;
    }
    return 0;
}

bool aroma_package_native_load(const char *so_path,
                               const AromaPackageHooks **out_hooks,
                               void **out_handle,
                               char *err_buf, size_t err_buf_len)
{
#ifdef __EMSCRIPTEN__
    (void)so_path;
    (void)out_hooks;
    (void)out_handle;
    set_err(err_buf, err_buf_len,
            "native plugins are not supported on this platform");
    return false;
#else
    if (!so_path || !out_hooks || !out_handle)
    {
        set_err(err_buf, err_buf_len, "invalid arguments");
        return false;
    }
    *out_hooks = NULL;
    *out_handle = NULL;
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char *detail = dlerror();
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "dlopen failed: %s",
                     detail ? detail : "unknown error");
        return false;
    }
    dlerror();
    AromaPackageEntryFn entry =
        (AromaPackageEntryFn)dlsym(handle, AROMA_PACKAGE_ENTRY_SYMBOL);
    const char *sym_err = dlerror();
    if (!entry || sym_err)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len,
                     "plugin is missing the '" AROMA_PACKAGE_ENTRY_SYMBOL "' symbol");
        dlclose(handle);
        return false;
    }
    const AromaPackageHooks *hooks = entry();
    if (!hooks)
    {
        set_err(err_buf, err_buf_len, "plugin entry returned NULL hooks");
        dlclose(handle);
        return false;
    }
    *out_hooks = hooks;
    *out_handle = handle;
    return true;
#endif
}

void aroma_package_native_unload(void *handle)
{
#ifndef __EMSCRIPTEN__
    if (handle)
        dlclose(handle);
#else
    (void)handle;
#endif
}
