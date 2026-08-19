#ifndef AROMA_NATIVE_UTILS_H
#define AROMA_NATIVE_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

void* aroma_native_get_window_ptr(size_t window_id);
void* aroma_native_get_display_ptr(void);
const char* aroma_resolve_asset_path(const char* path, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif