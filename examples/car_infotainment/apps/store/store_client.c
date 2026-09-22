#include "store_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#include "cJSON.h"
#endif

bool store_client_join_url(char *out, size_t out_len, const char *base,
                           const char *rel)
{
    if (!out || out_len == 0 || !base || !rel)
        return false;
    size_t blen = strlen(base);
    while (blen > 0 && base[blen - 1] == '/')
        blen--;
    while (*rel == '/')
        rel++;
    int n = snprintf(out, out_len, "%.*s/%s", (int)blen, base, rel);
    return n > 0 && (size_t)n < out_len;
}

#ifdef __EMSCRIPTEN__

StoreEntry *store_client_fetch(const char *base_url, int *out_count,
                               char *err_buf, size_t err_buf_len)
{
    (void)base_url;
    (void)out_count;
    if (err_buf && err_buf_len > 0)
        snprintf(err_buf, err_buf_len, "online store not supported on web");
    return NULL;
}

StoreEntry *store_client_fetch_ex(const char *base_url, const char *extra_query,
                                  int *out_count, char *err_buf,
                                  size_t err_buf_len)
{
    (void)base_url;
    (void)extra_query;
    (void)out_count;
    if (err_buf && err_buf_len > 0)
        snprintf(err_buf, err_buf_len, "online store not supported on web");
    return NULL;
}

void store_client_free(StoreEntry *entries)
{
    (void)entries;
}

bool store_client_download(const char *base_url, const StoreEntry *entry,
                           const char *dest_path, store_progress_cb cb,
                           void *user_data, char *err_buf, size_t err_buf_len)
{
    (void)base_url;
    (void)entry;
    (void)dest_path;
    (void)cb;
    (void)user_data;
    if (err_buf && err_buf_len > 0)
        snprintf(err_buf, err_buf_len, "online store not supported on web");
    return false;
}

#else /* native: libcurl implementation */

typedef struct
{
    char *data;
    size_t size;
} MemBuf;

static size_t write_mem_cb(void *contents, size_t size, size_t nmemb,
                           void *userp)
{
    size_t realsize = size * nmemb;
    MemBuf *mem = (MemBuf *)userp;
    if (!mem)
        return 0;
    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr)
        return 0;
    mem->data = ptr;
    memcpy(mem->data + mem->size, contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';
    return realsize;
}

typedef struct
{
    store_progress_cb cb;
    const char *id;
    void *user_data;
} ProgressCtx;

static int progress_cb(void *userp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    ProgressCtx *ctx = (ProgressCtx *)userp;
    if (ctx && ctx->cb)
        return ctx->cb(ctx->id, (long)dlnow, (long)dltotal, ctx->user_data);
    return 0;
}

static void set_common_opts(CURL *curl, const char *url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaInfotainment/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
}

static const char *json_str(const cJSON *obj, const char *key)
{
    if (!obj || !key)
        return NULL;
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || !cJSON_IsString(item) || !item->valuestring)
        return NULL;
    return item->valuestring;
}

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0)
        return;
    if (!src)
        src = "";
    snprintf(dst, dst_len, "%s", src);
}

StoreEntry *store_client_fetch(const char *base_url, int *out_count,
                               char *err_buf, size_t err_buf_len)
{
    return store_client_fetch_ex(base_url, NULL, out_count, err_buf,
                                 err_buf_len);
}

StoreEntry *store_client_fetch_ex(const char *base_url, const char *extra_query,
                                  int *out_count, char *err_buf,
                                  size_t err_buf_len)
{
    if (out_count)
        *out_count = 0;
    if (!base_url || !base_url[0])
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "no server URL set");
        return NULL;
    }
    char route[STORE_URL_MAX + 128];
    if (extra_query && extra_query[0])
        snprintf(route, sizeof(route), "/api/packages?%s", extra_query);
    else
        snprintf(route, sizeof(route), "/api/packages");
    char url[STORE_URL_MAX * 2];
    if (!store_client_join_url(url, sizeof(url), base_url, route))
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "bad server URL");
        return NULL;
    }
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "cannot init network");
        return NULL;
    }
    MemBuf chunk;
    chunk.data = malloc(1);
    chunk.size = 0;
    StoreEntry *entries = NULL;
    int count = 0;
    if (!chunk.data)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "out of memory");
        curl_easy_cleanup(curl);
        return NULL;
    }
    chunk.data[0] = '\0';
    set_common_opts(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "cannot reach store (%s)",
                     curl_easy_strerror(res));
        free(chunk.data);
        return NULL;
    }
    if (http_code != 200 || chunk.size == 0)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "store replied HTTP %ld", http_code);
        free(chunk.data);
        return NULL;
    }
    cJSON *root = cJSON_Parse(chunk.data);
    free(chunk.data);
    if (!root)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "store sent bad data");
        return NULL;
    }
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "packages");
    if (!arr || !cJSON_IsArray(arr))
    {
        cJSON_Delete(root);
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "store sent bad data");
        return NULL;
    }
    int n = cJSON_GetArraySize(arr);
    if (n > STORE_MAX_ENTRIES)
        n = STORE_MAX_ENTRIES;
    if (n > 0)
    {
        entries = calloc((size_t)n, sizeof(*entries));
        if (!entries)
        {
            cJSON_Delete(root);
            if (err_buf && err_buf_len > 0)
                snprintf(err_buf, err_buf_len, "out of memory");
            return NULL;
        }
    }
    for (int i = 0; i < n; i++)
    {
        cJSON *o = cJSON_GetArrayItem(arr, i);
        if (!o)
            continue;
        copy_field(entries[count].id, sizeof(entries[count].id),
                   json_str(o, "id"));
        if (!entries[count].id[0])
            continue;
        copy_field(entries[count].name, sizeof(entries[count].name),
                   json_str(o, "name"));
        if (!entries[count].name[0])
            copy_field(entries[count].name, sizeof(entries[count].name),
                       entries[count].id);
        copy_field(entries[count].version, sizeof(entries[count].version),
                   json_str(o, "version"));
        cJSON *vc = cJSON_GetObjectItemCaseSensitive(o, "version_code");
        entries[count].version_code = (vc && cJSON_IsNumber(vc)) ? vc->valueint : 0;
        copy_field(entries[count].author, sizeof(entries[count].author),
                   json_str(o, "author"));
        copy_field(entries[count].description, sizeof(entries[count].description),
                   json_str(o, "description"));
        copy_field(entries[count].icon, sizeof(entries[count].icon),
                   json_str(o, "icon"));
        copy_field(entries[count].category, sizeof(entries[count].category),
                   json_str(o, "category"));
        if (!entries[count].category[0])
            copy_field(entries[count].category, sizeof(entries[count].category),
                       "Apps");
        cJSON *rt = cJSON_GetObjectItemCaseSensitive(o, "rating");
        entries[count].rating = (rt && cJSON_IsNumber(rt)) ?
            (float)rt->valuedouble : 0.0f;
        cJSON *rc = cJSON_GetObjectItemCaseSensitive(o, "rating_count");
        entries[count].rating_count = (rc && cJSON_IsNumber(rc)) ?
            rc->valueint : 0;
        cJSON *dl = cJSON_GetObjectItemCaseSensitive(o, "downloads");
        entries[count].downloads = (dl && cJSON_IsNumber(dl)) ?
            (long)dl->valuedouble : 0;
        cJSON *ft = cJSON_GetObjectItemCaseSensitive(o, "featured");
        entries[count].featured = (ft && ((cJSON_IsBool(ft) && cJSON_IsTrue(ft)) ||
            (cJSON_IsNumber(ft) && ft->valueint != 0)));
        cJSON *sz = cJSON_GetObjectItemCaseSensitive(o, "size");
        entries[count].size = (sz && cJSON_IsNumber(sz)) ? (long)sz->valuedouble : 0;
        copy_field(entries[count].sha256, sizeof(entries[count].sha256),
                   json_str(o, "sha256"));
        copy_field(entries[count].url, sizeof(entries[count].url),
                   json_str(o, "url"));
        count++;
    }
    cJSON_Delete(root);
    if (out_count)
        *out_count = count;
    return entries;
}

void store_client_free(StoreEntry *entries)
{
    free(entries);
}

bool store_client_download(const char *base_url, const StoreEntry *entry,
                           const char *dest_path, store_progress_cb cb,
                           void *user_data, char *err_buf, size_t err_buf_len)
{
    if (!base_url || !entry || !entry->id[0] || !dest_path)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "bad download request");
        return false;
    }
    char url[STORE_URL_MAX * 2];
    if (entry->url[0])
    {
        if (!store_client_join_url(url, sizeof(url), base_url, entry->url))
        {
            if (err_buf && err_buf_len > 0)
                snprintf(err_buf, err_buf_len, "bad download URL");
            return false;
        }
    }
    else
    {
        /* Servers may omit per-entry URLs; fall back to the standard route. */
        char route[STORE_ID_MAX + 32];
        snprintf(route, sizeof(route), "/api/download/%s", entry->id);
        if (!store_client_join_url(url, sizeof(url), base_url, route))
        {
            if (err_buf && err_buf_len > 0)
                snprintf(err_buf, err_buf_len, "bad download URL");
            return false;
        }
    }
    FILE *fp = fopen(dest_path, "wb");
    if (!fp)
    {
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "cannot write download file");
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        fclose(fp);
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "cannot init network");
        return false;
    }
    ProgressCtx pctx;
    pctx.cb = cb;
    pctx.id = entry->id;
    pctx.user_data = user_data;
    set_common_opts(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, (void *)&pctx);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    fclose(fp);
    if (res != CURLE_OK)
    {
        remove(dest_path);
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "download failed (%s)",
                     curl_easy_strerror(res));
        return false;
    }
    if (http_code != 200)
    {
        remove(dest_path);
        if (err_buf && err_buf_len > 0)
            snprintf(err_buf, err_buf_len, "server replied HTTP %ld", http_code);
        return false;
    }
    return true;
}

#endif /* __EMSCRIPTEN__ */
