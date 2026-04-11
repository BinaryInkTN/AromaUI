#include "aroma_native_utils.h"
#include "aroma_abi.h"
#include "backends/platforms/aroma_platform_interface.h"

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