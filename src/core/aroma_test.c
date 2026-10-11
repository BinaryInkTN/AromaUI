#include "aroma_test.h"
#include "aroma_slab_alloc.h"
#include "aroma_time.h"
#include "aroma_timer.h"
#include "aroma_logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

extern AromaNode *g_focused_node;

static void str_copy(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int s_default_delay_ms = 0;
static int s_default_timeout_ms = 5000;

static void pre_delay(int explicit_ms)
{
    if (explicit_ms > 0)
        aroma_test_wait_ms(explicit_ms);
    else if (s_default_delay_ms > 0)
        aroma_test_wait_ms(s_default_delay_ms);
}

static void sleep_ms(int ms)
{
    if (ms <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

bool aroma_test_init(AromaTestCtx *ctx)
{
    if (!ctx)
        return false;
    __node_system_init();
    if (!aroma_event_system_init())
        return false;
    aroma_timer_init();
    void *w = aroma_widget_alloc(32);
    if (!w)
        return false;
    AromaNode *root = __create_node(NODE_TYPE_ROOT, NULL, w);
    if (!root)
        return false;
    aroma_event_set_root(root);
    ctx->root = root;
    ctx->last_mouse_x = -1;
    ctx->last_mouse_y = -1;
    ctx->mouse_down = false;
    ctx->touch_id = 0;
    return true;
}

void aroma_test_shutdown(AromaTestCtx *ctx)
{
    if (!ctx)
        return;
    aroma_event_set_root(NULL);
    g_focused_node = NULL;
    if (ctx->root) {
        __destroy_node(ctx->root);
        ctx->root = NULL;
    }
    aroma_event_system_shutdown();
    aroma_timer_shutdown();
    __node_system_destroy();
}

void aroma_test_pump(void)
{
    aroma_event_process_queue();
    aroma_timer_tick(aroma_time_now_ms());
}

void aroma_test_wait_ms(int ms)
{
    sleep_ms(ms);
    aroma_test_pump();
}

void aroma_test_sleep_ms(int ms)
{
    sleep_ms(ms);
}

void aroma_test_set_default_delay(int ms)
{
    s_default_delay_ms = ms < 0 ? 0 : ms;
}

int aroma_test_get_default_delay(void)
{
    return s_default_delay_ms;
}

void aroma_test_set_default_timeout(int ms)
{
    s_default_timeout_ms = ms < 0 ? 0 : ms;
}

int aroma_test_default_timeout(void)
{
    return s_default_timeout_ms;
}

bool aroma_test_try_until(bool (*pred)(void *), void *user_data, int timeout_ms)
{
    if (!pred)
        return false;
    if (timeout_ms < 0)
        timeout_ms = 0;
    uint64_t start = aroma_time_now_ms();
    for (;;) {
        aroma_test_pump();
        if (pred(user_data))
            return true;
        if ((int)(aroma_time_now_ms() - start) >= timeout_ms)
            return false;
        sleep_ms(5);
    }
}

bool aroma_test_rect(AromaNode *node, AromaRect *out)
{
    if (!node || !out)
        return false;
    AromaRect *r = aroma_node_get_rect(node);
    if (!r)
        return false;
    *out = *r;
    return true;
}

bool aroma_test_center(AromaNode *node, int *x, int *y)
{
    AromaRect r;
    if (!aroma_test_rect(node, &r))
        return false;
    if (r.width <= 0 || r.height <= 0)
        return false;
    if (x)
        *x = r.x + r.width / 2;
    if (y)
        *y = r.y + r.height / 2;
    return true;
}

AromaNode *aroma_test_hit(AromaNode *root, int x, int y)
{
    if (!root)
        return NULL;
    return aroma_event_hit_test(root, x, y);
}

AromaNode *aroma_test_root_of(AromaNode *node)
{
    AromaNode *cur = node;
    while (cur && cur->parent_node)
        cur = cur->parent_node;
    return cur;
}

static AromaNode *dispatch_mouse(AromaNode *root, AromaEventType type,
                                 int x, int y, uint8_t button)
{
    if (!root)
        return NULL;
    pre_delay(0);
    AromaNode *target = aroma_event_hit_test(root, x, y);
    uint64_t id = target ? target->node_id : root->node_id;
    AromaNode *resolved = target ? target : root;
    AromaEvent *ev = aroma_event_create_mouse(type, id, x, y, button);
    if (!ev)
        return NULL;
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);
    aroma_test_pump();
    return resolved;
}

AromaNode *aroma_test_inject_mouse(AromaNode *root, AromaEventType type,
                                   int x, int y, uint8_t button)
{
    if (!root)
        return NULL;
    if (type == EVENT_TYPE_MOUSE_SCROLL)
        return aroma_test_inject_scroll(root, x, y, 0.0f, 0.0f);
    AromaNode *t = dispatch_mouse(root, type, x, y, button);
    if (type == EVENT_TYPE_MOUSE_MOVE)
        aroma_event_handle_pointer_move(x, y, false);
    return t;
}

AromaNode *aroma_test_inject_scroll(AromaNode *root, int x, int y,
                                    float dx, float dy)
{
    if (!root)
        return NULL;
    pre_delay(0);
    AromaNode *target = aroma_event_hit_test(root, x, y);
    uint64_t id = target ? target->node_id : root->node_id;
    AromaEvent *ev = aroma_event_create_scroll(id, x, y, dx, dy);
    if (!ev)
        return NULL;
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);
    aroma_test_pump();
    return target ? target : root;
}

static AromaNode *resolve_key_target(AromaNode *target)
{
    if (target)
        return target;
    if (g_focused_node)
        return g_focused_node;
    return aroma_event_get_root();
}

AromaNode *aroma_test_inject_key(AromaNode *target, AromaEventType type,
                                 uint32_t key_code, uint16_t modifiers)
{
    AromaNode *t = resolve_key_target(target);
    if (!t)
        return NULL;
    pre_delay(0);
    AromaEvent *ev = aroma_event_create_key(type, t->node_id, key_code, modifiers);
    if (!ev)
        return NULL;
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);
    aroma_test_pump();
    return t;
}

bool aroma_test_mouse_move(AromaNode *root, int x, int y)
{
    return aroma_test_inject_mouse(root, EVENT_TYPE_MOUSE_MOVE, x, y, 0) != NULL;
}

bool aroma_test_mouse_press(AromaNode *root, int x, int y, uint8_t button)
{
    return aroma_test_inject_mouse(root, EVENT_TYPE_MOUSE_CLICK, x, y, button) != NULL;
}

bool aroma_test_mouse_release(AromaNode *root, int x, int y, uint8_t button)
{
    AromaNode *t = dispatch_mouse(root, EVENT_TYPE_MOUSE_RELEASE, x, y, button);
    aroma_event_handle_pointer_move(x, y, false);
    return t != NULL;
}

bool aroma_test_mouse_click(AromaNode *root, int x, int y, uint8_t button)
{
    if (!aroma_test_mouse_press(root, x, y, button))
        return false;
    return aroma_test_mouse_release(root, x, y, button);
}

bool aroma_test_mouse_dclick(AromaNode *root, int x, int y, uint8_t button)
{
    if (!aroma_test_mouse_click(root, x, y, button))
        return false;
    if (!aroma_test_mouse_click(root, x, y, button))
        return false;
    AromaNode *target = aroma_event_hit_test(root, x, y);
    uint64_t id = target ? target->node_id : (root ? root->node_id : 0);
    if (!id)
        return false;
    AromaEvent *ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_DOUBLE_CLICK, id, x, y, button);
    if (!ev)
        return false;
    aroma_event_dispatch(ev);
    aroma_event_destroy(ev);
    aroma_test_pump();
    return true;
}

bool aroma_test_click(AromaNode *widget, uint8_t button)
{
    int x, y;
    if (!aroma_test_center(widget, &x, &y))
        return false;
    AromaNode *root = aroma_test_root_of(widget);
    if (!root)
        return false;
    return aroma_test_mouse_click(root, x, y, button);
}

bool aroma_test_dclick(AromaNode *widget, uint8_t button)
{
    int x, y;
    if (!aroma_test_center(widget, &x, &y))
        return false;
    AromaNode *root = aroma_test_root_of(widget);
    if (!root)
        return false;
    return aroma_test_mouse_dclick(root, x, y, button);
}

bool aroma_test_mouse_drag(AromaNode *root, int x0, int y0, int x1, int y1, int steps)
{
    if (!root || steps < 1)
        return false;
    if (!aroma_test_mouse_move(root, x0, y0))
        return false;
    if (!aroma_test_mouse_press(root, x0, y0, AROMA_TEST_BUTTON_LEFT))
        return false;
    for (int i = 1; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        AromaNode *target = aroma_event_hit_test(root, x, y);
        uint64_t id = target ? target->node_id : root->node_id;
        AromaEvent *ev = aroma_event_create_mouse(EVENT_TYPE_MOUSE_MOVE, id, x, y, 0);
        if (ev) {
            aroma_event_dispatch(ev);
            aroma_event_destroy(ev);
        }
        aroma_test_pump();
    }
    return aroma_test_mouse_release(root, x1, y1, AROMA_TEST_BUTTON_LEFT);
}

bool aroma_test_drag(AromaNode *widget, int dx, int dy, int steps)
{
    int x, y;
    if (!aroma_test_center(widget, &x, &y))
        return false;
    AromaNode *root = aroma_test_root_of(widget);
    if (!root)
        return false;
    return aroma_test_mouse_drag(root, x, y, x + dx, y + dy, steps);
}

bool aroma_test_scroll(AromaNode *root, int x, int y, float dx, float dy)
{
    return aroma_test_inject_scroll(root, x, y, dx, dy) != NULL;
}

AromaNode *aroma_test_mouse_event(AromaTestMouseAction action, AromaNode *root,
                                  int x, int y, uint8_t button,
                                  uint16_t mods, int delay_ms)
{
    (void)mods;
    if (!root)
        return NULL;
    int saved = s_default_delay_ms;
    s_default_delay_ms = 0;
    pre_delay(delay_ms);
    AromaNode *out = NULL;
    switch (action) {
    case AROMA_MOUSE_MOVE:
        out = aroma_test_inject_mouse(root, EVENT_TYPE_MOUSE_MOVE, x, y, button);
        break;
    case AROMA_MOUSE_PRESS:
        out = aroma_test_inject_mouse(root, EVENT_TYPE_MOUSE_CLICK, x, y, button);
        break;
    case AROMA_MOUSE_RELEASE: {
        out = dispatch_mouse(root, EVENT_TYPE_MOUSE_RELEASE, x, y, button);
        aroma_event_handle_pointer_move(x, y, false);
        break;
    }
    case AROMA_MOUSE_CLICK:
        out = aroma_test_mouse_click(root, x, y, button) ? aroma_test_hit(root, x, y) : NULL;
        break;
    case AROMA_MOUSE_DCLICK:
        out = aroma_test_mouse_dclick(root, x, y, button) ? aroma_test_hit(root, x, y) : NULL;
        break;
    default:
        break;
    }
    s_default_delay_ms = saved;
    return out;
}

bool aroma_test_wheel(AromaNode *root, int x, int y,
                      int pixel_dx, int pixel_dy,
                      int angle_dx, int angle_dy)
{
    float dx = (float)pixel_dx + (float)angle_dx / 120.0f;
    float dy = (float)pixel_dy + (float)angle_dy / 120.0f;
    return aroma_test_inject_scroll(root, x, y, dx, dy) != NULL;
}

bool aroma_test_touch_down(AromaNode *root, int id, int x, int y)
{
    (void)root;
    if (id < 0 || id >= 10)
        return false;
    pre_delay(0);
    aroma_event_handle_touch(id, x, y, 1);
    aroma_test_pump();
    return true;
}

bool aroma_test_touch_move(AromaNode *root, int id, int x, int y)
{
    (void)root;
    if (id < 0 || id >= 10)
        return false;
    pre_delay(0);
    aroma_event_handle_touch(id, x, y, 2);
    aroma_test_pump();
    return true;
}

bool aroma_test_touch_up(AromaNode *root, int id, int x, int y)
{
    (void)root;
    if (id < 0 || id >= 10)
        return false;
    pre_delay(0);
    aroma_event_handle_touch(id, x, y, 0);
    aroma_test_pump();
    return true;
}

bool aroma_test_tap(AromaNode *root, int x, int y)
{
    if (!aroma_test_touch_down(root, 0, x, y))
        return false;
    return aroma_test_touch_up(root, 0, x, y);
}

bool aroma_test_tap_widget(AromaNode *widget)
{
    int x, y;
    if (!aroma_test_center(widget, &x, &y))
        return false;
    AromaNode *root = aroma_test_root_of(widget);
    if (!root)
        return false;
    return aroma_test_tap(root, x, y);
}

bool aroma_test_swipe(AromaNode *root, int x0, int y0, int x1, int y1, int steps)
{
    if (!root || steps < 1)
        return false;
    if (!aroma_test_touch_down(root, 0, x0, y0))
        return false;
    for (int i = 1; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        if (!aroma_test_touch_move(root, 0, x, y))
            return false;
    }
    return aroma_test_touch_up(root, 0, x1, y1);
}

AromaTouchSeq aroma_touch_begin(AromaNode *root, int id)
{
    AromaTouchSeq seq;
    memset(&seq, 0, sizeof(seq));
    seq.root = root;
    seq.id = id;
    if (!root || id < 0 || id >= 10)
        seq.failed = true;
    return seq;
}

static AromaTouchSeq *touch_push(AromaTouchSeq *seq, int kind, int x, int y)
{
    if (!seq || seq->failed)
        return seq;
    if (seq->count >= 16) {
        seq->failed = true;
        return seq;
    }
    seq->kinds[seq->count] = kind;
    seq->xs[seq->count] = x;
    seq->ys[seq->count] = y;
    seq->count++;
    return seq;
}

AromaTouchSeq *aroma_touch_press(AromaTouchSeq *seq, int x, int y)
{
    return touch_push(seq, 1, x, y);
}

AromaTouchSeq *aroma_touch_move(AromaTouchSeq *seq, int x, int y)
{
    return touch_push(seq, 2, x, y);
}

AromaTouchSeq *aroma_touch_stationary(AromaTouchSeq *seq, int x, int y)
{
    return touch_push(seq, 3, x, y);
}

AromaTouchSeq *aroma_touch_release(AromaTouchSeq *seq, int x, int y)
{
    return touch_push(seq, 0, x, y);
}

bool aroma_touch_commit(AromaTouchSeq *seq)
{
    if (!seq || seq->failed || seq->count == 0)
        return false;
    pre_delay(0);
    for (int i = 0; i < seq->count; i++) {
        int state = seq->kinds[i] == 3 ? 2 : seq->kinds[i];
        aroma_event_handle_touch(seq->id, seq->xs[i], seq->ys[i], state);
        aroma_test_pump();
    }
    seq->count = 0;
    return true;
}

bool aroma_test_key_press(AromaNode *target, uint32_t key, uint16_t mods)
{
    return aroma_test_inject_key(target, EVENT_TYPE_KEY_PRESS, key, mods) != NULL;
}

bool aroma_test_key_release(AromaNode *target, uint32_t key, uint16_t mods)
{
    return aroma_test_inject_key(target, EVENT_TYPE_KEY_RELEASE, key, mods) != NULL;
}

bool aroma_test_key_click(AromaNode *target, uint32_t key, uint16_t mods)
{
    if (!aroma_test_key_press(target, key, mods))
        return false;
    return aroma_test_key_release(target, key, mods);
}

bool aroma_test_type_text(AromaNode *target, const char *text)
{
    if (!text)
        return false;
    for (const char *p = text; *p; p++) {
        uint32_t k = (uint32_t)(unsigned char)*p;
        if (!aroma_test_key_press(target, k, 0))
            return false;
        if (!aroma_test_key_release(target, k, 0))
            return false;
    }
    return true;
}

bool aroma_test_key_sequence(AromaNode *target, const uint32_t *keys, size_t n)
{
    if (n > 0 && !keys)
        return false;
    for (size_t i = 0; i < n; i++) {
        if (!aroma_test_key_click(target, keys[i], 0))
            return false;
    }
    return true;
}

AromaNode *aroma_test_key_event(AromaTestKeyAction action, AromaNode *target,
                                uint32_t key, uint16_t mods, int delay_ms)
{
    int saved = s_default_delay_ms;
    s_default_delay_ms = 0;
    pre_delay(delay_ms);
    AromaNode *out = NULL;
    switch (action) {
    case AROMA_KEY_PRESS:
        out = aroma_test_inject_key(target, EVENT_TYPE_KEY_PRESS, key, mods);
        break;
    case AROMA_KEY_RELEASE:
        out = aroma_test_inject_key(target, EVENT_TYPE_KEY_RELEASE, key, mods);
        break;
    case AROMA_KEY_CLICK:
    case AROMA_KEY_SHORTCUT:
        out = aroma_test_inject_key(target, EVENT_TYPE_KEY_PRESS, key, mods);
        if (out)
            out = aroma_test_inject_key(target, EVENT_TYPE_KEY_RELEASE, key, mods);
        break;
    default:
        break;
    }
    s_default_delay_ms = saved;
    return out;
}

static bool pred_visible(void *ud)
{
    AromaNode *n = (AromaNode *)ud;
    return n && !n->is_hidden;
}

static bool pred_hidden(void *ud)
{
    AromaNode *n = (AromaNode *)ud;
    return n && n->is_hidden;
}

static bool pred_focused(void *ud)
{
    AromaNode *n = (AromaNode *)ud;
    return n && g_focused_node == n;
}

bool aroma_test_wait_visible(AromaNode *node, int timeout_ms)
{
    if (!node)
        return false;
    return aroma_test_try_until(pred_visible, node, timeout_ms);
}

bool aroma_test_wait_hidden(AromaNode *node, int timeout_ms)
{
    if (!node)
        return false;
    return aroma_test_try_until(pred_hidden, node, timeout_ms);
}

bool aroma_test_wait_focused(AromaNode *node, int timeout_ms)
{
    if (!node)
        return false;
    return aroma_test_try_until(pred_focused, node, timeout_ms);
}

static bool spy_handler(AromaEvent *ev, void *user_data)
{
    AromaTestSpy *spy = (AromaTestSpy *)user_data;
    if (!spy || !ev)
        return false;
    spy->count++;
    spy->last = *ev;
    spy->has_last = true;
    return false;
}

bool aroma_test_spy_start(AromaTestSpy *spy, uint64_t node_id, AromaEventType type)
{
    if (!spy || node_id == 0 || type >= EVENT_TYPE_COUNT)
        return false;
    memset(spy, 0, sizeof(*spy));
    spy->node_id = node_id;
    spy->type = type;
    if (!aroma_event_subscribe(node_id, type, spy_handler, spy, 2000))
        return false;
    spy->attached = true;
    return true;
}

void aroma_test_spy_stop(AromaTestSpy *spy)
{
    if (!spy || !spy->attached)
        return;
    aroma_event_unsubscribe(spy->node_id, spy->type, spy_handler);
    spy->attached = false;
}

void aroma_test_spy_clear(AromaTestSpy *spy)
{
    if (!spy)
        return;
    spy->count = 0;
    spy->has_last = false;
    memset(&spy->last, 0, sizeof(spy->last));
}

int aroma_test_spy_count(const AromaTestSpy *spy)
{
    return spy ? spy->count : 0;
}

static bool spy_reached(void *ud)
{
    struct { AromaTestSpy *s; int want; } *a = ud;
    return aroma_test_spy_count(a->s) >= a->want;
}

bool aroma_test_spy_wait(AromaTestSpy *spy, int expected, int timeout_ms)
{
    if (!spy)
        return false;
    struct { AromaTestSpy *s; int want; } arg = { spy, expected };
    return aroma_test_try_until(spy_reached, &arg, timeout_ms);
}

static int s_check_passed = 0;
static int s_check_failed = 0;
static int s_check_skipped = 0;
static const char *s_current_func = NULL;
static bool s_current_failed = false;
static bool s_current_skipped = false;
static const char *s_expect_comment = "";
static AromaExpectMode s_expect_mode = AROMA_EXPECT_CONTINUE;
static bool s_expect_armed = false;

void aroma_test_begin(const char *name)
{
    s_current_func = name;
    s_current_failed = false;
    s_current_skipped = false;
    s_expect_armed = false;
}

void aroma_test_end(void)
{
    s_current_func = NULL;
}

const char *aroma_test_current_func(void)
{
    return s_current_func;
}

bool aroma_test_current_failed(void)
{
    return s_current_failed;
}

bool aroma_test_current_resolved(void)
{
    return s_current_failed || s_current_skipped;
}

void aroma_test_stats(int *passed, int *failed, int *skipped)
{
    if (passed)
        *passed = s_check_passed;
    if (failed)
        *failed = s_check_failed;
    if (skipped)
        *skipped = s_check_skipped;
}

void aroma_test_reset_stats(void)
{
    s_check_passed = 0;
    s_check_failed = 0;
    s_check_skipped = 0;
    s_current_failed = false;
    s_current_skipped = false;
    s_expect_armed = false;
}

void aroma_test_expect_fail(const char *comment, AromaExpectMode mode)
{
    s_expect_comment = comment ? comment : "";
    s_expect_mode = mode;
    s_expect_armed = true;
}

void aroma_test_skip_at(const char *msg, const char *file, int line)
{
    s_check_skipped++;
    s_current_skipped = true;
    printf("SKIP %s (%s:%d): %s\n",
           s_current_func ? s_current_func : "?",
           file, line, msg ? msg : "");
}

bool aroma_test_check_at(bool cond, const char *expr,
                         const char *file, int line, const char *msg)
{
    const char *fn = s_current_func ? s_current_func : "?";
    if (s_expect_armed) {
        s_expect_armed = false;
        if (!cond) {
            s_check_passed++;
            printf("XFAIL %s (%s:%d): %s [expected: %s]\n",
                   fn, file, line, expr, s_expect_comment);
            return true;
        }
        s_check_failed++;
        s_current_failed = true;
        printf("XPASS %s (%s:%d): %s unexpectedly passed [expected: %s]\n",
               fn, file, line, expr, s_expect_comment);
        return s_expect_mode == AROMA_EXPECT_CONTINUE;
    }
    if (cond) {
        s_check_passed++;
        return true;
    }
    s_check_failed++;
    s_current_failed = true;
    if (msg && msg[0])
        printf("FAIL %s (%s:%d): %s: %s\n", fn, file, line, expr, msg);
    else
        printf("FAIL %s (%s:%d): %s\n", fn, file, line, expr);
    return false;
}

bool aroma_test_check_cmp_ll(const char *expr_a, const char *expr_b,
                             long long a, long long b, const char *op,
                             const char *file, int line)
{
    bool ok = false;
    if (strcmp(op, "==") == 0)
        ok = (a == b);
    else if (strcmp(op, "!=") == 0)
        ok = (a != b);
    else if (strcmp(op, "<") == 0)
        ok = (a < b);
    else if (strcmp(op, "<=") == 0)
        ok = (a <= b);
    else if (strcmp(op, ">") == 0)
        ok = (a > b);
    else if (strcmp(op, ">=") == 0)
        ok = (a >= b);
    char expr[192];
    char msg[192];
    snprintf(expr, sizeof(expr), "%s %s %s", expr_a, op, expr_b);
    snprintf(msg, sizeof(msg), "actual=%lld expected=%lld", a, b);
    return aroma_test_check_at(ok, expr, file, line, msg);
}

bool aroma_test_check_cmp_dbl(const char *expr_a, const char *expr_b,
                              double a, double b, const char *op,
                              const char *file, int line)
{
    bool ok = false;
    if (strcmp(op, "==") == 0)
        ok = (a == b);
    else if (strcmp(op, "!=") == 0)
        ok = (a != b);
    else if (strcmp(op, "<") == 0)
        ok = (a < b);
    else if (strcmp(op, "<=") == 0)
        ok = (a <= b);
    else if (strcmp(op, ">") == 0)
        ok = (a > b);
    else if (strcmp(op, ">=") == 0)
        ok = (a >= b);
    char expr[192];
    char msg[192];
    snprintf(expr, sizeof(expr), "%s %s %s", expr_a, op, expr_b);
    snprintf(msg, sizeof(msg), "actual=%g expected=%g", a, b);
    return aroma_test_check_at(ok, expr, file, line, msg);
}

bool aroma_test_check_cmp_str(const char *expr_a, const char *expr_b,
                              const char *a, const char *b, const char *op,
                              const char *file, int line)
{
    int cmp;
    if (!a && !b)
        cmp = 0;
    else if (!a || !b)
        cmp = 1;
    else
        cmp = strcmp(a, b);
    bool ok = (strcmp(op, "!=") == 0) ? (cmp != 0) : (cmp == 0);
    char expr[192];
    char msg[256];
    snprintf(expr, sizeof(expr), "%s %s %s", expr_a, op, expr_b);
    snprintf(msg, sizeof(msg), "actual=\"%s\" expected=\"%s\"",
             a ? a : "(null)", b ? b : "(null)");
    return aroma_test_check_at(ok, expr, file, line, msg);
}

bool aroma_test_check_cmp_ptr(const char *expr_a, const char *expr_b,
                              const void *a, const void *b, const char *op,
                              const char *file, int line)
{
    bool ok = (strcmp(op, "!=") == 0) ? (a != b) : (a == b);
    char expr[192];
    char msg[192];
    snprintf(expr, sizeof(expr), "%s %s %s", expr_a, op, expr_b);
    snprintf(msg, sizeof(msg), "actual=%p expected=%p", a, b);
    return aroma_test_check_at(ok, expr, file, line, msg);
}

#define AROMA_DATA_MAX_COLS 16
#define AROMA_DATA_MAX_ROWS 128
#define AROMA_DATA_CELL 64

static struct {
    char name[48];
    size_t size;
} s_data_cols[AROMA_DATA_MAX_COLS];
static int s_data_ncols = 0;

static struct {
    char tag[64];
    unsigned char cells[AROMA_DATA_MAX_COLS][AROMA_DATA_CELL];
    int filled;
} s_data_rows[AROMA_DATA_MAX_ROWS];
static int s_data_nrows = 0;
static int s_data_cur = -1;

void aroma_test_data_clear(void)
{
    s_data_ncols = 0;
    s_data_nrows = 0;
    s_data_cur = -1;
}

bool aroma_test_add_column(const char *name, size_t elem_size)
{
    if (!name || !name[0] || elem_size == 0 || elem_size > AROMA_DATA_CELL)
        return false;
    if (s_data_ncols >= AROMA_DATA_MAX_COLS)
        return false;
    for (int i = 0; i < s_data_ncols; i++) {
        if (strcmp(s_data_cols[i].name, name) == 0)
            return false;
    }
    str_copy(s_data_cols[s_data_ncols].name,
             sizeof(s_data_cols[s_data_ncols].name), name);
    s_data_cols[s_data_ncols].size = elem_size;
    s_data_ncols++;
    return true;
}

bool aroma_test_new_row(const char *tag)
{
    if (s_data_nrows >= AROMA_DATA_MAX_ROWS)
        return false;
    int r = s_data_nrows++;
    str_copy(s_data_rows[r].tag, sizeof(s_data_rows[r].tag), tag);
    memset(s_data_rows[r].cells, 0, sizeof(s_data_rows[r].cells));
    s_data_rows[r].filled = 0;
    s_data_cur = r;
    return true;
}

bool aroma_test_push(const void *value)
{
    if (!value || s_data_nrows == 0)
        return false;
    int r = s_data_cur;
    if (r < 0 || r >= s_data_nrows)
        return false;
    int c = s_data_rows[r].filled;
    if (c < 0 || c >= s_data_ncols)
        return false;
    memcpy(s_data_rows[r].cells[c], value, s_data_cols[c].size);
    s_data_rows[r].filled++;
    return true;
}

static int data_col_index(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < s_data_ncols; i++) {
        if (strcmp(s_data_cols[i].name, name) == 0)
            return i;
    }
    return -1;
}

const void *aroma_test_fetch(const char *name)
{
    static unsigned char zero[AROMA_DATA_CELL] = {0};
    int c = data_col_index(name);
    if (c < 0 || s_data_cur < 0 || s_data_cur >= s_data_nrows) {
        aroma_test_check_at(false, "FETCH", __FILE__, __LINE__, name ? name : "(null)");
        return zero;
    }
    return s_data_rows[s_data_cur].cells[c];
}

int aroma_test_fetch_int(const char *name)
{
    const void *p = aroma_test_fetch(name);
    int v = 0;
    memcpy(&v, p, sizeof(v) < AROMA_DATA_CELL ? sizeof(v) : AROMA_DATA_CELL);
    return v;
}

const char *aroma_test_fetch_str(const char *name)
{
    const void *p = aroma_test_fetch(name);
    const char *s = NULL;
    memcpy(&s, p, sizeof(s));
    return s ? s : "";
}

const char *aroma_test_data_tag(void)
{
    if (s_data_cur < 0 || s_data_cur >= s_data_nrows)
        return NULL;
    return s_data_rows[s_data_cur].tag;
}

int aroma_test_row_count(void)
{
    return s_data_nrows;
}

bool aroma_test_select_row(int index)
{
    if (index < 0 || index >= s_data_nrows)
        return false;
    s_data_cur = index;
    return true;
}

uint64_t aroma_bench_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

const char *aroma_bench_metric_name(AromaBenchMetric metric)
{
    switch (metric) {
    case AROMA_BENCH_WALLTIME_MS:
        return "WalltimeMilliseconds";
    case AROMA_BENCH_WALLTIME_NS:
        return "WalltimeNanoseconds";
    case AROMA_BENCH_EVENTS:
        return "Events";
    case AROMA_BENCH_CPU_TICKS:
        return "CPUTicks";
    default:
        return "Unknown";
    }
}

const char *aroma_bench_metric_unit(AromaBenchMetric metric)
{
    switch (metric) {
    case AROMA_BENCH_WALLTIME_MS:
        return "ms";
    case AROMA_BENCH_WALLTIME_NS:
        return "ns";
    case AROMA_BENCH_EVENTS:
        return "events";
    case AROMA_BENCH_CPU_TICKS:
        return "ticks";
    default:
        return "?";
    }
}

static double s_bench_value = 0.0;
static AromaBenchMetric s_bench_metric = AROMA_BENCH_WALLTIME_NS;

void aroma_test_set_bench_result(double value, AromaBenchMetric metric)
{
    s_bench_value = value;
    s_bench_metric = metric;
}

double aroma_test_bench_result(AromaBenchMetric *metric_out)
{
    if (metric_out)
        *metric_out = s_bench_metric;
    return s_bench_value;
}

#define AROMA_RUN_MAX_CASES 128

static struct {
    char name[96];
    AromaTestFn data_fn;
    AromaTestFn test_fn;
} s_run_cases[AROMA_RUN_MAX_CASES];
static int s_run_ncases = 0;
static AromaTestFn s_hook_init_case = NULL;
static AromaTestFn s_hook_cleanup_case = NULL;
static AromaTestFn s_hook_init = NULL;
static AromaTestFn s_hook_cleanup = NULL;

static int run_case_index(const char *name)
{
    for (int i = 0; i < s_run_ncases; i++) {
        if (strcmp(s_run_cases[i].name, name) == 0)
            return i;
    }
    return -1;
}

bool aroma_test_register(const char *name, AromaTestFn test_fn)
{
    if (!name || !name[0] || !test_fn)
        return false;
    int i = run_case_index(name);
    if (i >= 0) {
        s_run_cases[i].test_fn = test_fn;
        return true;
    }
    if (s_run_ncases >= AROMA_RUN_MAX_CASES)
        return false;
    str_copy(s_run_cases[s_run_ncases].name,
             sizeof(s_run_cases[s_run_ncases].name), name);
    s_run_cases[s_run_ncases].data_fn = NULL;
    s_run_cases[s_run_ncases].test_fn = test_fn;
    s_run_ncases++;
    return true;
}

bool aroma_test_register_data(const char *name, AromaTestFn data_fn)
{
    if (!name || !name[0] || !data_fn)
        return false;
    int i = run_case_index(name);
    if (i >= 0) {
        s_run_cases[i].data_fn = data_fn;
        return true;
    }
    if (s_run_ncases >= AROMA_RUN_MAX_CASES)
        return false;
    str_copy(s_run_cases[s_run_ncases].name,
             sizeof(s_run_cases[s_run_ncases].name), name);
    s_run_cases[s_run_ncases].data_fn = data_fn;
    s_run_cases[s_run_ncases].test_fn = NULL;
    s_run_ncases++;
    return true;
}

void aroma_test_set_hooks(AromaTestFn init_case, AromaTestFn cleanup_case,
                          AromaTestFn init, AromaTestFn cleanup)
{
    s_hook_init_case = init_case;
    s_hook_cleanup_case = cleanup_case;
    s_hook_init = init;
    s_hook_cleanup = cleanup;
}

static void run_one(const char *label, AromaTestFn fn)
{
    aroma_test_begin(label);
    if (s_hook_init)
        s_hook_init();
    if (fn)
        fn();
    if (s_hook_cleanup)
        s_hook_cleanup();
    aroma_test_check_ignored();
    bool failed = aroma_test_current_failed();
    printf("%s: %s\n", failed ? "FAIL" : "PASS", label);
    aroma_test_end();
}

int aroma_test_run(int argc, char **argv)
{
    const char *filter = NULL;
    bool list_only = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0)
            list_only = true;
        else if (strncmp(argv[i], "--filter=", 9) == 0)
            filter = argv[i] + 9;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: %s [--list] [--filter=SUBSTR]\n", argv[0]);
            return 0;
        }
    }
    if (list_only) {
        for (int i = 0; i < s_run_ncases; i++)
            printf("%s\n", s_run_cases[i].name);
        return 0;
    }
    int failed_before = 0;
    aroma_test_stats(NULL, &failed_before, NULL);
    if (s_hook_init_case)
        s_hook_init_case();
    for (int i = 0; i < s_run_ncases; i++) {
        if (filter && !strstr(s_run_cases[i].name, filter))
            continue;
        if (!s_run_cases[i].test_fn)
            continue;
        if (s_run_cases[i].data_fn) {
            char base[96];
            str_copy(base, sizeof(base), s_run_cases[i].name);
            aroma_test_data_clear();
            s_run_cases[i].data_fn();
            int n = aroma_test_row_count();
            if (n == 0) {
                run_one(base, s_run_cases[i].test_fn);
                continue;
            }
            for (int r = 0; r < n; r++) {
                aroma_test_select_row(r);
                char label[192];
                const char *tag = aroma_test_data_tag();
                str_copy(label, sizeof(label), base);
                {
                    size_t bl = strlen(label);
                    if (bl + 1 < sizeof(label)) {
                        label[bl] = ':';
                        str_copy(label + bl + 1, sizeof(label) - bl - 1, tag);
                    }
                }
                run_one(label, s_run_cases[i].test_fn);
            }
            aroma_test_data_clear();
        } else {
            run_one(s_run_cases[i].name, s_run_cases[i].test_fn);
        }
    }
    if (s_hook_cleanup_case)
        s_hook_cleanup_case();
    int p = 0, f = 0, s = 0;
    aroma_test_stats(&p, &f, &s);
    printf("Totals: %d passed, %d failed, %d skipped\n", p, f, s);
    int failed_after = 0;
    aroma_test_stats(NULL, &failed_after, NULL);
    return failed_after - failed_before;
}

#define AROMA_WARN_MAX 8

static struct {
    char substr[128];
    bool seen;
} s_warn_ignores[AROMA_WARN_MAX];
static int s_warn_nignore = 0;
static bool s_fow_armed = false;
static char s_fow_pattern[128] = {0};
static bool s_warn_hook_installed = false;

static bool warn_hook_fn(DebugLevel level, const char *message, void *user_data)
{
    (void)user_data;
    if (!message)
        return false;
    for (int i = 0; i < s_warn_nignore; i++) {
        if (s_warn_ignores[i].substr[0] && strstr(message, s_warn_ignores[i].substr)) {
            s_warn_ignores[i].seen = true;
            return true;
        }
    }
    if (s_fow_armed && level == DEBUG_LEVEL_WARNING &&
        (!s_fow_pattern[0] || strstr(message, s_fow_pattern))) {
        aroma_test_check_at(false, "failOnWarning", __FILE__, __LINE__, message);
    }
    return false;
}

static void warn_hook_ensure(void)
{
    if (!s_warn_hook_installed) {
        aroma_log_set_hook(warn_hook_fn, NULL);
        s_warn_hook_installed = true;
    }
}

bool aroma_test_ignore_message(const char *substr)
{
    if (!substr || !substr[0] || s_warn_nignore >= AROMA_WARN_MAX)
        return false;
    warn_hook_ensure();
    str_copy(s_warn_ignores[s_warn_nignore].substr,
             sizeof(s_warn_ignores[s_warn_nignore].substr), substr);
    s_warn_ignores[s_warn_nignore].seen = false;
    s_warn_nignore++;
    return true;
}

void aroma_test_fail_on_warning(const char *pattern_or_null)
{
    warn_hook_ensure();
    str_copy(s_fow_pattern, sizeof(s_fow_pattern), pattern_or_null);
    s_fow_armed = true;
}

bool aroma_test_check_ignored(void)
{
    bool ok = true;
    for (int i = 0; i < s_warn_nignore; i++) {
        if (!s_warn_ignores[i].seen) {
            ok = false;
            aroma_test_check_at(false, "ignoreMessage", __FILE__, __LINE__,
                                s_warn_ignores[i].substr);
        }
    }
    s_warn_nignore = 0;
    s_fow_armed = false;
    s_fow_pattern[0] = '\0';
    return ok;
}

const char *aroma_test_find_data(const char *filename)
{
    static char found[1024];
    if (!filename || !filename[0])
        return NULL;
    const char *env = getenv("AROMA_TESTDATA");
    char cand[1024];
    const char *bases[4];
    int n = 0;
    if (env && env[0])
        bases[n++] = env;
    bases[n++] = "tests/data";
    bases[n++] = "tests";
    bases[n++] = ".";
    for (int i = 0; i < n; i++) {
        str_copy(cand, sizeof(cand), bases[i]);
        {
            size_t cl = strlen(cand);
            if (cl + 1 < sizeof(cand)) {
                cand[cl] = '/';
                str_copy(cand + cl + 1, sizeof(cand) - cl - 1, filename);
            }
        }
        if (access(cand, F_OK) == 0) {
            str_copy(found, sizeof(found), cand);
            return found;
        }
    }
    return NULL;
}

#ifdef __cplusplus
}
#endif
