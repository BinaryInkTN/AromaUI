#ifndef AROMA_DP_H
#define AROMA_DP_H

/**
 * @file aroma_dp.h
 * @brief Density-independent pixel helpers.
 *
 * On Android, UI geometry authored by callers is expressed in dp (layout)
 * and sp (text). The platform density converts those units to physical
 * pixels. On desktop/embedded builds density is 1 so these are identity.
 *
 * Use AROMA_DP(x)/AROMA_SP(x) for float metrics (radii, hairlines,
 * padding) and AROMA_DP_I(x) for integer geometry.
 */

#ifdef __ANDROID__
#include "aroma_android.h"
#define AROMA_DP(x) ((float)aroma_android_dp_to_px_f((float)(x)))
#define AROMA_SP(x) ((float)aroma_android_sp_to_px_f((float)(x)))
#define AROMA_DP_I(x) (aroma_android_dp_to_px((int)(x)))
#define AROMA_SP_I(x) (aroma_android_sp_to_px((int)(x)))
#else
#define AROMA_DP(x) ((float)(x))
#define AROMA_SP(x) ((float)(x))
#define AROMA_DP_I(x) ((int)(x))
#define AROMA_SP_I(x) ((int)(x))
#endif

#endif
