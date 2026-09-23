#ifndef LOCK_SCREEN_H
#define LOCK_SCREEN_H

#include "aroma_common.h"
#include "aroma_node.h"

#ifdef __cplusplus
extern "C" {
#endif

void unlock_screen(void);
void build_lock_screen(AromaNode *window);
bool lock_screen_is_active(void);

#ifdef __cplusplus
}
#endif
#endif
