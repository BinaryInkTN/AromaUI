#include "aroma_native_utils.h"
#include "aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"

#include <string.h>
#include <stdio.h>

void* aroma_native_get_window_ptr(size_t window_id) {
    AromaPlatformInterface* pi = aroma_backend_abi.get_platform_interface();
    if (pi && pi->get_native_window_ptr) {
        return pi->get_native_window_ptr(window_id);
    }
    return NULL;
}

void* aroma_native_get_display_ptr(void) {
    AromaPlatformInterface* pi = aroma_backend_abi.get_platform_interface();
    if (pi && pi->get_native_display_ptr) {
        return pi->get_native_display_ptr();
    }
    return NULL;
}

const char* aroma_resolve_asset_path(const char* path, char* buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0) {
        return path;
    }

#ifdef __EMSCRIPTEN__
    const char* cursor = path;

    while (strncmp(cursor, "../", 3) == 0) {
        cursor += 3;
    }

    while (strncmp(cursor, "./", 2) == 0) {
        cursor += 2;
    }

    if (strncmp(cursor, "assets/", 7) == 0) {
        snprintf(buffer, buffer_size, "/%s", cursor);
        return buffer;
    }

    if (strncmp(path, "/assets/", 8) == 0) {
        return path;
    }
#endif

    strncpy(buffer, path, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return buffer;
}