#ifndef STORE_CLIENT_H
#define STORE_CLIENT_H

/* HTTP client for the AromaUI package store server
 * (tools/aroma_store_server.py). Native builds use libcurl; on Emscripten
 * every call fails gracefully with "not supported" so callers compile
 * unchanged. All network calls are blocking - run them on a worker thread,
 * never on the UI thread.
 */

#include <stdbool.h>
#include <stddef.h>

#define STORE_ID_MAX 128
#define STORE_NAME_MAX 128
#define STORE_VERSION_MAX 32
#define STORE_AUTHOR_MAX 128
#define STORE_DESC_MAX 512
#define STORE_ICON_MAX 64
#define STORE_CAT_MAX 64
#define STORE_URL_MAX 512
#define STORE_SHA_MAX 65
#define STORE_MAX_ENTRIES 64

typedef struct
{
    char id[STORE_ID_MAX];
    char name[STORE_NAME_MAX];
    char version[STORE_VERSION_MAX];
    int version_code;
    char author[STORE_AUTHOR_MAX];
    char description[STORE_DESC_MAX];
    char icon[STORE_ICON_MAX];
    char category[STORE_CAT_MAX];
    float rating;
    int rating_count;
    long downloads;
    bool featured;
    long size;
    char sha256[STORE_SHA_MAX];
    char url[STORE_URL_MAX]; /* server-relative, e.g. /api/download/<id> */
} StoreEntry;

/* Progress callback during downloads; return non-zero to abort. */
typedef int (*store_progress_cb)(const char *id, long downloaded, long total,
                                 void *user_data);

/* Fetch the server index. Returns a heap array (*out_count entries) or NULL
 * on error (err_buf set). Free with store_client_free().
 * extra_query may be NULL or e.g. "category=Games&sort=downloads". */
StoreEntry *store_client_fetch(const char *base_url, int *out_count,
                               char *err_buf, size_t err_buf_len);
StoreEntry *store_client_fetch_ex(const char *base_url, const char *extra_query,
                                  int *out_count, char *err_buf,
                                  size_t err_buf_len);

void store_client_free(StoreEntry *entries);

/* Download one entry to dest_path (overwritten). Shows progress via cb. */
bool store_client_download(const char *base_url, const StoreEntry *entry,
                           const char *dest_path, store_progress_cb cb,
                           void *user_data, char *err_buf, size_t err_buf_len);

/* Join base_url + relative path into out (exactly one '/' between them). */
bool store_client_join_url(char *out, size_t out_len, const char *base,
                           const char *rel);

#endif
