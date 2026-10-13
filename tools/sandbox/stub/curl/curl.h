/* Minimal libcurl API stub for WebAssembly builds.
 *
 * Emscripten has no libcurl port, and the sandbox has no use for live
 * HTTP (map/image widgets fall back to their offline paths). This header
 * satisfies `#include <curl/curl.h>` so the unmodified widget sources
 * compile; every transfer reports failure. Add -Itools/sandbox/stub
 * and tools/sandbox/stub_curl.c to the emcc command line (see
 * tools/sandbox/build_wasm.sh). Never used on native builds.
 */
#ifndef AROMA_STUB_CURL_H
#define AROMA_STUB_CURL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void CURL;
typedef int CURLcode;
typedef int CURLINFO;
typedef int CURLoption;
typedef int CURLglob;

#define CURLE_OK 0
#define CURLE_FAILED_INIT 2
#define CURLE_URL_MALFORMAT 3
#define CURLE_COULDNT_CONNECT 7
#define CURLE_SEND_FAIL 55

#define CURLOPT_URL 10002
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001
#define CURLOPT_USERAGENT 10018
#define CURLOPT_TIMEOUT 13
#define CURLOPT_CONNECTTIMEOUT 78
#define CURLOPT_NOSIGNAL 99
#define CURLOPT_FOLLOWLOCATION 52
#define CURLOPT_FAILONERROR 45

#define CURLINFO_RESPONSE_CODE 0x200002
#define CURL_GLOBAL_ALL 3

typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb,
                                      void *userdata);

CURLcode curl_global_init(long flags);
void curl_global_cleanup(void);
CURL *curl_easy_init(void);
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...);
CURLcode curl_easy_perform(CURL *curl);
void curl_easy_cleanup(CURL *curl);
CURLcode curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
char *curl_easy_escape(CURL *curl, const char *string, int length);
void curl_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
