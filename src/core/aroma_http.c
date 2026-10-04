#include "aroma_http.h"
#include "backends/platforms/aroma_platform_interface.h"
#include "core/aroma_logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef __ANDROID__
#include <curl/curl.h>
#else
#include "aroma_android.h"
#endif

static uint32_t http_fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (s && *s)
    {
        h ^= (uint8_t)(*s++);
        h *= 16777619u;
    }
    return h;
}

static void http_file_ext(const char *url, char *out, size_t cap)
{
    const char *q = strchr(url ? url : "", '?');
    size_t ulen = q ? (size_t)(q - url) : strlen(url ? url : "");
    const char *dot = NULL;
    size_t i = 0;
    for (i = 0; i < ulen; i++)
    {
        if (url[i] == '.')
            dot = url + i;
        if (url[i] == '/')
            dot = NULL;
    }
    if (dot && (size_t)(url + ulen - dot) <= 5)
    {
        size_t n = (size_t)(url + ulen - dot);
        if (n >= cap)
            n = cap - 1;
        memcpy(out, dot, n);
        out[n] = '\0';
        return;
    }
    strncpy(out, ".img", cap - 1);
    out[cap - 1] = '\0';
}

bool aroma_http_cache_ensure_dir(char *out_dir, size_t out_size)
{
    if (!out_dir || out_size == 0)
        return false;
    out_dir[0] = '\0';
#ifdef __ANDROID__
    AromaPlatformInterface *plat = aroma_get_platform_interface();
    const char *base = (plat && plat->android_get_internal_path) ? plat->android_get_internal_path() : NULL;
    if (!base || !base[0])
        return false;
    snprintf(out_dir, out_size, "%s/imgcache", base);
#else
    const char *home = getenv("HOME");
    if (home && home[0])
        snprintf(out_dir, out_size, "%s/.cache/aromaimg", home);
    else
        snprintf(out_dir, out_size, "/tmp/aromaimg");
#endif
    char tmp[1024];
    size_t pos = 0;
    size_t len = strlen(out_dir);
    if (len == 0 || len >= sizeof(tmp) - 1)
        return false;
    for (pos = 1; pos <= len; pos++)
    {
        if (pos == len || out_dir[pos] == '/')
        {
            memcpy(tmp, out_dir, pos);
            tmp[pos] = '\0';
#ifdef _WIN32
            mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
        }
    }
    return true;
}

void aroma_http_cache_path_for_url(const char *url, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!url || !url[0])
        return;
    char dir[768];
    if (!aroma_http_cache_ensure_dir(dir, sizeof(dir)))
        return;
    char ext[8];
    http_file_ext(url, ext, sizeof(ext));
    snprintf(out, out_size, "%s/%08x%s", dir, http_fnv1a(url), ext);
}

#ifndef __ANDROID__

typedef struct
{
    FILE *fp;
    long total;
    long limit;
} HttpSink;

static size_t http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpSink *sink = (HttpSink *)userdata;
    size_t n = size * nmemb;
    if (!sink || !sink->fp)
        return 0;
    if (sink->total + (long)n > sink->limit)
        return 0;
    if (fwrite(ptr, 1, n, sink->fp) != n)
        return 0;
    sink->total += (long)n;
    return n;
}

static bool http_fetch_curl(const char *url, const char *dest_path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.part", dest_path);
    FILE *fp = fopen(tmp, "wb");
    if (!fp)
        return false;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        fclose(fp);
        return false;
    }
    HttpSink sink;
    sink.fp = fp;
    sink.total = 0;
    sink.limit = 16777216;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&sink);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AromaUI/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);
    fclose(fp);
    if (res != CURLE_OK || code < 200 || code >= 300 || sink.total == 0)
    {
        remove(tmp);
        return false;
    }
    if (rename(tmp, dest_path) != 0)
    {
        remove(tmp);
        return false;
    }
    return true;
}

#endif

bool aroma_http_fetch_to_file(const char *url, const char *dest_path)
{
    if (!url || !url[0] || !dest_path || !dest_path[0])
        return false;
#ifdef __ANDROID__
    return aroma_android_http_fetch(url, dest_path);
#else
    return http_fetch_curl(url, dest_path);
#endif
}
