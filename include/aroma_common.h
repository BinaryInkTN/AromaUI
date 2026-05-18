#ifndef AROMA_COMMON_H
#define AROMA_COMMON_H

#include <stdint.h>

/**
 * @file aroma_common.h
 * @brief Common definitions and data structures for AromaUI.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct AromaRect
 * @brief Represents a rectangle with integer coordinates.
 */
typedef struct  AromaRect {
    /** @brief X coordinate of the top-left corner. */
    int x;
    /** @brief Y coordinate of the top-left corner. */
    int y;
    /** @brief Width of the rectangle. */
    int width;
    /** @brief Height of the rectangle. */
    int height;
} AromaRect;

#ifdef __cplusplus
}
#endif
#endif
