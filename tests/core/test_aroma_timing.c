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

/* Slide end positions: Incense values are parent-relative (parent offset
 * added each tick) while the programmatic API stays absolute. Duration-0
 * animations finish on the first engine tick, so a few wall-clock ticks
 * settle them deterministically on every platform. */
static void tick_engine_settle(void)
{
    for (int i = 0; i < 6; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
#ifdef _WIN32
        Sleep(25);
#else
        usleep(25000);
#endif
    }
}

static void test_animation_coordinates(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *wp = aroma_widget_alloc(32);
    AromaNode *parent = __add_child_node(NODE_TYPE_CONTAINER, root, wp);
    AromaRect *pr = (AromaRect *)wp;
    pr->x = 0;
    pr->y = 0;
    pr->width = 392;
    pr->height = 724;
    void *wc = aroma_widget_alloc(32);
    AromaNode *child = __add_child_node(NODE_TYPE_WIDGET, parent, wc);
    AromaRect *cr = (AromaRect *)wc;
    cr->x = 24;
    cr->y = 276;
    cr->width = 245;
    cr->height = 44;

    /* Top-level container child: parent offset is zero, so the Incense
     * end value lands exactly (Linux behavior unchanged). */
    AromaAnimation *a = aroma_animation_start(child, AROMA_ANIM_SLIDE_Y,
                                              300.0f, 276.0f, 0);
    CHECK(a != NULL, "relative slide starts");
    a->parent_relative = true;
    tick_engine_settle();
    CHECK(cr->y == 276, "relative slide lands on parent+end");

    /* Nested parent at (8,8): end must include the live parent offset. */
    pr->x = 8;
    pr->y = 8;
    AromaAnimation *b = aroma_animation_start(child, AROMA_ANIM_SLIDE_Y,
                                              20.0f, 10.0f, 0);
    CHECK(b != NULL, "nested relative slide starts");
    b->parent_relative = true;
    tick_engine_settle();
    CHECK(cr->y == 18, "nested slide adds parent offset");

    /* Programmatic absolute animations ignore the parent offset. */
    AromaAnimation *c = aroma_animation_start(child, AROMA_ANIM_SLIDE_Y,
                                              100.0f, 200.0f, 0);
    CHECK(c != NULL && !c->parent_relative, "absolute is default");
    tick_engine_settle();
    CHECK(cr->y == 200, "absolute slide writes end as-is");

    aroma_animation_cleanup_all();
    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

/* Pause/resume: while paused, ticks advance nothing (Android background /
 * lost surface); resume continues from the frozen position instead of
 * teleporting to the end. */
static int s_pause_steps = 0;
static int s_pause_done = 0;

static void on_pause_step(AromaNode *target, float current_val, void *user_data)
{
    (void)target;
    (void)current_val;
    (void)user_data;
    s_pause_steps++;
}

static void on_pause_done(AromaNode *target, void *user_data)
{
    (void)target;
    (void)user_data;
    s_pause_done++;
}

static void test_animation_pause_resume(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);

    CHECK(!aroma_animation_is_paused(), "not paused by default");
    aroma_animation_resume_all(); /* redundant resume is safe */
    CHECK(!aroma_animation_is_paused(), "redundant resume safe");

    s_pause_steps = 0;
    s_pause_done = 0;
    AromaAnimation *a = aroma_animation_start_custom(target, 0.0f, 100.0f,
                                                     400, on_pause_step,
                                                     NULL);
    CHECK(a != NULL, "pausable animation started");
    aroma_animation_set_easing(a, AROMA_EASE_LINEAR);
    aroma_animation_set_on_complete(a, on_pause_done);
    for (int i = 0; i < 6; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        usleep(5000);
    }
    float frozen = a->current_val;
    CHECK(frozen > 0.0f && frozen < 100.0f, "animation mid-flight before pause");
    int steps_before = s_pause_steps;

    aroma_animation_pause_all();
    CHECK(aroma_animation_is_paused(), "pause takes effect");
    aroma_animation_pause_all(); /* redundant pause is safe */
    usleep(150000);
    for (int i = 0; i < 6; i++)
        aroma_timer_tick(aroma_time_now_ms());
    CHECK(a->current_val == frozen, "paused tick advances nothing");
    CHECK(s_pause_done == 0, "paused animation never completes");

    aroma_animation_resume_all();
    CHECK(!aroma_animation_is_paused(), "resume takes effect");
    for (int i = 0; i < 120 && s_pause_done == 0; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        usleep(5000);
    }
    CHECK(s_pause_done == 1, "resumed animation completes exactly once");
    CHECK(s_pause_steps > steps_before, "resumed animation keeps ticking");

    aroma_animation_cleanup_all();
    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

/* Stall clamp: a long gap between ticks (frame hitch, debugger,
 * backgrounding without pause) must not teleport a mid-flight animation.
 * Progress advances at most AROMA_ANIM_MAX_TICK_STEP_MS per tick. */
static void test_animation_stall_clamp(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);

    AromaAnimation *a = aroma_animation_start_custom(target, 0.0f, 100.0f,
                                                     2000, on_pause_step,
                                                     NULL);
    CHECK(a != NULL, "long animation started");
    aroma_animation_set_easing(a, AROMA_EASE_LINEAR);
    aroma_timer_tick(aroma_time_now_ms());
    /* Simulate a ~400ms stall: wall-clock progress would be ~0.2, the
     * clamp caps one tick at 64/2000 = 0.032. */
    usleep(400000);
    aroma_timer_tick(aroma_time_now_ms());
    CHECK(a->last_progress < 0.15f, "stalled tick does not teleport");
    CHECK(a->current_val < 15.0f, "stalled value eases forward");

    aroma_animation_cleanup_all();
    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

/* Destroying a target from inside its own tick callback (__destroy_node,
 * e.g. list refresh racing a running animation) must not crash: the
 * animation is detached without touching the dead node again. */
static int s_suicide_fired = 0;

static void on_suicide_step(AromaNode *target, float current_val, void *user_data)
{
    (void)current_val;
    (void)user_data;
    if (!s_suicide_fired)
    {
        s_suicide_fired = 1;
        __destroy_node(target);
    }
}

static void test_animation_destroy_in_callback(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);
    (void)target;

    s_suicide_fired = 0;
    AromaAnimation *a = aroma_animation_start_custom(target, 0.0f, 100.0f,
                                                     400, on_suicide_step,
                                                     NULL);
    CHECK(a != NULL, "suicide animation started");
    for (int i = 0; i < 10; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        usleep(5000);
    }
    CHECK(s_suicide_fired == 1, "callback destroyed its target");

    /* Engine still healthy: a fresh animation on a live node runs. */
    void *w3 = aroma_widget_alloc(32);
    AromaNode *target2 = __add_child_node(NODE_TYPE_WIDGET, root, w3);
    s_pause_steps = 0;
    AromaAnimation *b = aroma_animation_start_custom(target2, 0.0f, 1.0f,
                                                      40, on_pause_step,
                                                      NULL);
    CHECK(b != NULL, "engine usable after mid-tick destroy");
    for (int i = 0; i < 10; i++)
    {
        aroma_timer_tick(aroma_time_now_ms());
        usleep(5000);
    }
    CHECK(s_pause_steps > 0, "fresh animation ticks");

    aroma_animation_cleanup_all();
    __destroy_node(root);
    __node_system_destroy();
    aroma_animation_manager_shutdown();
    aroma_timer_shutdown();
}

/* Explicit timestamps: aroma_animation_tick() advances to the given clock,
 * which is how Android feeds vsync time and how tests drive it
 * deterministically. */
static void test_animation_explicit_tick(void)
{
    __node_system_init();
    aroma_timer_init();
    aroma_animation_manager_init();

    void *w = aroma_widget_alloc(32);
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    void *w2 = aroma_widget_alloc(32);
    AromaNode *target = __add_child_node(NODE_TYPE_WIDGET, root, w2);

    AromaAnimation *a = aroma_animation_start_custom(target, 0.0f, 100.0f,
                                                     100, on_pause_step,
                                                     NULL);
    CHECK(a != NULL, "explicit-tick animation started");
    aroma_animation_set_easing(a, AROMA_EASE_LINEAR);
    uint64_t t0 = a->start_time;
    aroma_animation_tick(t0 + 25);
    float v25 = a->current_val;
    aroma_animation_tick(t0 + 50);
    float v50 = a->current_val;
    CHECK(v25 > 20.0f && v25 < 30.0f, "tick(25ms) lands at quarter");
    CHECK(v50 > 45.0f && v50 < 55.0f, "tick(50ms) lands at half");
    CHECK(v50 > v25, "explicit ticks advance monotonically");
    aroma_animation_tick(t0 + 200);
    CHECK(a->is_running == false, "explicit tick past duration finishes");

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
    test_animation_coordinates();
    test_animation_pause_resume();
    test_animation_stall_clamp();
    test_animation_destroy_in_callback();
    test_animation_explicit_tick();
    printf("Aroma Timing: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
