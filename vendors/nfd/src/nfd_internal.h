#ifndef NFD_INTERNAL_H
#define NFD_INTERNAL_H

#include "nfd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Backend error reporting. Implemented by nfd_common.c. */
void nfd_set_error(const char *format, ...);
void nfd_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif /* NFD_INTERNAL_H */
