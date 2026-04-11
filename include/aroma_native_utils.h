#ifndef AROMA_NATIVE_UTILS_H
#define AROMA_NATIVE_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

void* aroma_native_get_window_ptr(size_t window_id);
void* aroma_native_get_display_ptr(void);

#ifdef __cplusplus
}
#endif

#endif