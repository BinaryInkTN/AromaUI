#ifndef AROMA_DP_H
#define AROMA_DP_H













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
