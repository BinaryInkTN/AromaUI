#ifndef AROMA_HTTP_H
#define AROMA_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool aroma_http_fetch_to_file(const char *url, const char *dest_path);
bool aroma_http_fetch_to_file_cookies(const char *url, const char *dest_path,
                                      const char *cookie_header,
                                      char *set_cookie_out, size_t set_cookie_len);
void aroma_http_cache_path_for_url(const char *url, char *out, size_t out_size);
bool aroma_http_cache_ensure_dir(char *out_dir, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
