/* No-op libcurl for WebAssembly builds. See stub/curl/curl.h. */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

CURLcode curl_global_init(long flags)
{
    (void)flags;
    return CURLE_OK;
}

void curl_global_cleanup(void)
{
}

CURL *curl_easy_init(void)
{
    static int token;
    return (CURL *)&token;
}

CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
{
    (void)curl;
    (void)option;
    return CURLE_OK;
}

CURLcode curl_easy_perform(CURL *curl)
{
    (void)curl;
    return CURLE_COULDNT_CONNECT;
}

void curl_easy_cleanup(CURL *curl)
{
    (void)curl;
}

CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...)
{
    (void)curl;
    if (info == CURLINFO_RESPONSE_CODE) {
        va_list ap;
        va_start(ap, info);
        long *code = va_arg(ap, long *);
        if (code)
            *code = 0;
        va_end(ap);
    }
    return CURLE_OK;
}

char *curl_easy_escape(CURL *curl, const char *string, int length)
{
    (void)curl;
    if (!string)
        return NULL;
    size_t n = (length >= 0) ? (size_t)length : strlen(string);
    char *out = (char *)malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, string, n);
    out[n] = '\0';
    return out;
}

void curl_free(void *ptr)
{
    free(ptr);
}
