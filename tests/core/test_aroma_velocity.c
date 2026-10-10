#include "test_aroma_velocity.h"
#include "aroma_velocity.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                  \
            s_passed++;                                                  \
        } else {                                                         \
            s_failed++;                                                  \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static bool feq(float a, float b, float eps)
{
    float d = a > b ? a - b : b - a;
    return d <= eps;
}

static void test_empty_and_single(void)
{
    AromaVelocityTracker vt;
    aroma_velocity_reset(&vt);
    float vx = 1.0f, vy = 1.0f;
    CHECK(!aroma_velocity_get(&vt, 1000, &vx, &vy),
          "empty tracker has no velocity");
    CHECK(vx == 0.0f && vy == 0.0f, "empty tracker zeroes output");
    aroma_velocity_add(&vt, 0, 0, 1000);
    CHECK(!aroma_velocity_get(&vt, 1010, &vx, &vy),
          "single sample has no velocity");
    CHECK(!aroma_velocity_get(NULL, 1010, &vx, &vy),
          "NULL tracker safe");
    aroma_velocity_add(NULL, 0, 0, 1000);
    aroma_velocity_reset(NULL);
    CHECK(1, "NULL add/reset safe");
}

static void test_constant_velocity(void)
{
    AromaVelocityTracker vt;
    aroma_velocity_reset(&vt);

    for (int i = 0; i <= 5; i++)
        aroma_velocity_add(&vt, i * 100, i * 50, 1000 + i * 100);
    float vx = 0.0f, vy = 0.0f;
    CHECK(aroma_velocity_get(&vt, 1500, &vx, &vy), "velocity available");
    CHECK(feq(vx, 1000.0f, 1.0f), "vx matches motion");
    CHECK(feq(vy, 500.0f, 1.0f), "vy matches motion");
}

static void test_horizon_expiry(void)
{
    AromaVelocityTracker vt;
    aroma_velocity_reset(&vt);
    aroma_velocity_add(&vt, 0, 0, 1000);
    aroma_velocity_add(&vt, 100, 0, 1100);
    float vx = 0.0f, vy = 0.0f;

    CHECK(!aroma_velocity_get(&vt, 2000, &vx, &vy),
          "stale samples expire");

    CHECK(!aroma_velocity_get(&vt, 1200, &vx, &vy),
          "one in-horizon sample is not enough");
}

static void test_direction_and_stop(void)
{
    AromaVelocityTracker vt;
    aroma_velocity_reset(&vt);

    for (int i = 0; i <= 4; i++)
        aroma_velocity_add(&vt, 400 - i * 100, 0, 1000 + i * 50);
    float vx = 0.0f, vy = 0.0f;
    CHECK(aroma_velocity_get(&vt, 1200, &vx, &vy), "flick velocity");
    CHECK(vx < -1500.0f, "flick direction and magnitude");
    CHECK(feq(vy, 0.0f, 0.01f), "no vertical component");

    for (int i = 1; i <= 8; i++)
        aroma_velocity_add(&vt, 0, 0, 1200 + i * 20);
    CHECK(aroma_velocity_get(&vt, 1400, &vx, &vy), "held velocity");
    CHECK(vx > -500.0f && vx < 500.0f, "held finger decays velocity");
}

static void test_same_timestamp_ignored(void)
{
    AromaVelocityTracker vt;
    aroma_velocity_reset(&vt);
    aroma_velocity_add(&vt, 0, 0, 1000);
    aroma_velocity_add(&vt, 50, 0, 1000);
    aroma_velocity_add(&vt, 100, 0, 1100);
    float vx = 0.0f, vy = 0.0f;
    CHECK(aroma_velocity_get(&vt, 1100, &vx, &vy), "dup timestamp skipped");
    CHECK(feq(vx, 1000.0f, 1.0f), "dup timestamp does not corrupt");
}

void run_velocity_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Velocity Tests ===\n");
    test_empty_and_single();
    test_constant_velocity();
    test_horizon_expiry();
    test_direction_and_stop();
    test_same_timestamp_ignored();
    printf("Aroma Velocity: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
