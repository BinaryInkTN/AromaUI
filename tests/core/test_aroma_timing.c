/*
 Copyright (c) 2026 BinaryInkTN

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in
 the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test_aroma_timing.h"
#include "aroma_timer.h"
#include "aroma_time.h"
#include "aroma_animation.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

static int s_passed = 0;
static int s_failed = 0;
static int s_fires = 0;
static int s_loop_steps = 0;
static int s_loop_done = 0;

#define CHECK(cond, name)                                              \
    do {                                                               \
        if (cond) {                                                    \
            s_passed++;                                                \
        } else {                                                       \
            s_failed++;                                                \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);           \
        }                                                              \
    } while (0)

static void on_timer(void *user_data)
{
    (void)user_data;
    s_fires++;
}

static void on_loop_step(AromaNode *target, float current_val, void *user_data)
{
    (void)target;
    (void)current_val;
    (void)user_data;
    s_loop_steps++;
}

static void on_loop_done(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    s_loop_done++;
}

static void test_timer(void)
{
    aroma_timer_init();

    CHECK(aroma_timer_create(0, false, on_timer, NULL) == NULL,
          "timer rejects zero period");
    CHECK(aroma_timer_create(100, false, NULL, NULL) == NULL,
          "timer rejects NULL callback");

    s_fires = 0;
    AromaTimer *once = aroma_timer_create(100, false, on_timer, NULL);
    CHECK(once != NULL, "one-shot timer created");
    aroma_timer_tick(0);     /* arms the timer */
    CHECK(s_fires == 0, "timer does not fire early");
    aroma_timer_tick(50);
    CHECK(s_fires == 0, "timer does not fire at half period");
    aroma_timer_tick(100);
    CHECK(s_fires == 1, "timer fires at period");
    aroma_timer_tick(1000);
    CHECK(s_fires == 1, "one-shot does not refire");

    s_fires = 0;
    AromaTimer *rep = aroma_timer_create(100, true, on_timer, NULL);
    CHECK(rep != NULL, "repeating timer created");
    aroma_timer_tick(1000);
    aroma_timer_tick(1100);
    aroma_timer_tick(1200);
    aroma_timer_tick(1300);
    CHECK(s_fires == 3, "repeating timer refires");

    aroma_timer_cancel(rep);
    aroma_timer_tick(10000);
    CHECK(s_fires == 3, "cancelled timer stays silent");
    aroma_timer_cancel(NULL); /* no crash */

    aroma_timer_shutdown();
}

static void test_animation_lifecycle(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);

    CHECK(aroma_animation_start(NULL, 0, 0.0f, 1.0f, 100) == NULL,
          "animation rejects NULL target");

    AromaAnimation *a = aroma_animation_start(target, 0, 0.0f, 1.0f, 300);
    CHECK(a != NULL, "animation started");
    aroma_animation_set_easing(a, 1);
    aroma_animation_set_easing(NULL, 1);
    aroma_animation_set_on_complete(a, NULL);
    aroma_animation_set_on_complete(NULL, NULL);

    /* Restarting the same target replaces the running animation. */
    AromaAnimation *b = aroma_animation_start(target, 0, 0.0f, 2.0f, 300);
    CHECK(b != NULL && b != a, "restart replaces animation");

    aroma_animation_stop(target);
    aroma_animation_stop(NULL); /* no crash */
    aroma_animation_cleanup_node(target);
    aroma_animation_cleanup_node(NULL);
    CHECK(1, "animation stop/cleanup safe");

    AromaAnimation *c = aroma_animation_start_custom(target, 0.0f, 1.0f, 100,
                                                     NULL, NULL);
    CHECK(c != NULL, "custom animation started");
    aroma_animation_cleanup_all();

    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

static void test_animation_loop(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);

    /* Loop flag defaults to off and the setter is NULL-safe. */
    AromaAnimation *a = aroma_animation_start(target, AROMA_ANIM_FADE,
                                              0.0f, 1.0f, 50);
    CHECK(a != NULL && a->loop_mode == AROMA_LOOP_OFF, "loop defaults to off");
    aroma_animation_set_loop(a, true);
    CHECK(a->loop_mode == AROMA_LOOP_RESTART, "loop enabled");
    aroma_animation_set_loop(a, false);
    CHECK(a->loop_mode == AROMA_LOOP_OFF, "loop disabled");
    aroma_animation_set_loop(NULL, true); /* no crash */
    aroma_animation_set_loop_mode(a, AROMA_LOOP_PINGPONG);
    CHECK(a->loop_mode == AROMA_LOOP_PINGPONG, "ping-pong mode set");
    aroma_animation_set_loop_mode(NULL, AROMA_LOOP_PINGPONG); /* no crash */

    /* A looping animation restarts every cycle and never completes.
       Drive the 16ms engine timer with the wall clock across several
       40ms cycles (60 x 5ms sleeps ~= 300ms wall). */
    s_loop_steps = 0;
    s_loop_done = 0;
    AromaAnimation *l = aroma_animation_start_custom(target, 0.0f, 1.0f, 40,
                                                     on_loop_step, NULL);
    CHECK(l != NULL, "looping animation started");
    aroma_animation_set_loop(l, true);
    aroma_animation_set_on_complete(l, on_loop_done);
    for (int i = 0; i < 60; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(s_loop_done == 0, "looping animation never completes");
    CHECK(s_loop_steps > 3, "looping animation keeps ticking");

    /* One-shot control: completes exactly once, then goes quiet. */
    s_loop_steps = 0;
    s_loop_done = 0;
    AromaAnimation *o = aroma_animation_start_custom(target, 0.0f, 1.0f, 40,
                                                     on_loop_step, NULL);
    CHECK(o != NULL, "one-shot animation started");
    aroma_animation_set_on_complete(o, on_loop_done);
    for (int i = 0; i < 60; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(s_loop_done == 1, "one-shot completes exactly once");
    int frozen = s_loop_steps;
    CHECK(frozen > 0, "one-shot ticked before finishing");
    for (int i = 0; i < 20; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(s_loop_steps == frozen, "one-shot stops ticking");

    /* stop() also halts a looping animation without completing it. */
    s_loop_steps = 0;
    s_loop_done = 0;
    AromaAnimation *s = aroma_animation_start_custom(target, 0.0f, 1.0f, 40,
                                                     on_loop_step, NULL);
    aroma_animation_set_loop(s, true);
    aroma_animation_set_on_complete(s, on_loop_done);
    for (int i = 0; i < 10; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(s_loop_steps > 0, "looping animation ticked");
    aroma_animation_stop(target);
    frozen = s_loop_steps;
    for (int i = 0; i < 20; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(s_loop_steps == frozen && s_loop_done == 0,
          "stop halts looping animation");

    /* Ping-pong reverses direction each cycle: value climbs toward the
       end, then falls again, without ever completing. */
    s_loop_steps = 0;
    s_loop_done = 0;
    AromaAnimation *pp = aroma_animation_start_custom(target, 0.0f, 100.0f,
                                                      40, on_loop_step,
                                                      NULL);
    CHECK(pp != NULL, "ping-pong animation started");
    aroma_animation_set_loop_mode(pp, AROMA_LOOP_PINGPONG);
    aroma_animation_set_on_complete(pp, on_loop_done);
    float peak = -1.0f;
    for (int i = 0; i < 200 && peak < 99.0f; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        if (pp->current_val > peak)
            peak = pp->current_val;
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(peak >= 99.0f, "ping-pong reaches the end value");
    float valley = peak;
    for (int i = 0; i < 200 && valley > 50.0f; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        if (pp->current_val < valley)
            valley = pp->current_val;
#ifdef _WIN32
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    CHECK(valley < 50.0f, "ping-pong reverses back down");
    CHECK(s_loop_done == 0, "ping-pong never completes");

    aroma_animation_cleanup_all();
    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

void run_timing_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Timing Tests ===\n");
    test_timer();
    test_animation_lifecycle();
    test_animation_loop();
    printf("Aroma Timing: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
