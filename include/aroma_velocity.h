#ifndef AROMA_VELOCITY_H
#define AROMA_VELOCITY_H

#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AROMA_VELOCITY_MAX_SAMPLES 8
#define AROMA_VELOCITY_HORIZON_MS 150

typedef struct {
    int x;
    int y;
    uint64_t t_ms;
} AromaVelocitySample;

typedef struct {
    AromaVelocitySample samples[AROMA_VELOCITY_MAX_SAMPLES];
    int head;
    int count;
} AromaVelocityTracker;

void aroma_velocity_reset(AromaVelocityTracker *vt);
void aroma_velocity_add(AromaVelocityTracker *vt, int x, int y,
                        uint64_t t_ms);
bool aroma_velocity_get(const AromaVelocityTracker *vt, uint64_t now_ms,
                        float *out_vx_pps, float *out_vy_pps);

#ifdef __cplusplus
}
#endif
#endif
