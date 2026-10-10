#include "aroma_velocity.h"
#include <stddef.h>

void aroma_velocity_reset(AromaVelocityTracker *vt)
{
    if (!vt)
        return;
    vt->head = 0;
    vt->count = 0;
}

void aroma_velocity_add(AromaVelocityTracker *vt, int x, int y,
                        uint64_t t_ms)
{
    if (!vt)
        return;
    if (vt->count > 0) {
        int prev = (vt->head - 1 + AROMA_VELOCITY_MAX_SAMPLES) %
                   AROMA_VELOCITY_MAX_SAMPLES;
        if (vt->samples[prev].t_ms == t_ms)
            return;
    }
    vt->samples[vt->head].x = x;
    vt->samples[vt->head].y = y;
    vt->samples[vt->head].t_ms = t_ms;
    vt->head = (vt->head + 1) % AROMA_VELOCITY_MAX_SAMPLES;
    if (vt->count < AROMA_VELOCITY_MAX_SAMPLES)
        vt->count++;
}

bool aroma_velocity_get(const AromaVelocityTracker *vt, uint64_t now_ms,
                        float *out_vx_pps, float *out_vy_pps)
{
    if (out_vx_pps)
        *out_vx_pps = 0.0f;
    if (out_vy_pps)
        *out_vy_pps = 0.0f;
    if (!vt || vt->count < 2)
        return false;
    int first = -1, last = -1;
    for (int k = 0; k < vt->count; k++) {
        int idx = (vt->head - 1 - k + AROMA_VELOCITY_MAX_SAMPLES) %
                  AROMA_VELOCITY_MAX_SAMPLES;
        if (now_ms < vt->samples[idx].t_ms ||
            now_ms - vt->samples[idx].t_ms > AROMA_VELOCITY_HORIZON_MS)
            break;
        if (first < 0)
            first = idx;
        last = idx;
    }
    if (first < 0 || last < 0 || first == last)
        return false;
    float dt =
        (float)(vt->samples[first].t_ms - vt->samples[last].t_ms) / 1000.0f;
    if (dt < 0.001f)
        return false;
    if (out_vx_pps)
        *out_vx_pps = (float)(vt->samples[first].x - vt->samples[last].x) /
                      dt;
    if (out_vy_pps)
        *out_vy_pps = (float)(vt->samples[first].y - vt->samples[last].y) /
                      dt;
    return true;
}
