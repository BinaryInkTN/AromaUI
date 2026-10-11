#ifndef AROMA_TEST_H
#define AROMA_TEST_H

#include "aroma_common.h"
#include "aroma_node.h"
#include "aroma_event.h"
#include "aroma_time.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AROMA_TEST_BUTTON_LEFT 0u
#define AROMA_TEST_BUTTON_MIDDLE 1u
#define AROMA_TEST_BUTTON_RIGHT 2u

#define AROMA_TEST_KEY_BACKSPACE 8u
#define AROMA_TEST_KEY_TAB 9u
#define AROMA_TEST_KEY_ENTER 10u
#define AROMA_TEST_KEY_ESCAPE 27u
#define AROMA_TEST_KEY_SPACE 32u
#define AROMA_TEST_KEY_DELETE 127u
#define AROMA_TEST_KEY_LEFT 0xFF51u
#define AROMA_TEST_KEY_UP 0xFF52u
#define AROMA_TEST_KEY_RIGHT 0xFF53u
#define AROMA_TEST_KEY_DOWN 0xFF54u
#define AROMA_TEST_KEY_HOME 0xFF50u
#define AROMA_TEST_KEY_END 0xFF57u

typedef enum AromaTestMouseAction {
    AROMA_MOUSE_PRESS,
    AROMA_MOUSE_RELEASE,
    AROMA_MOUSE_CLICK,
    AROMA_MOUSE_DCLICK,
    AROMA_MOUSE_MOVE
} AromaTestMouseAction;

typedef enum AromaTestKeyAction {
    AROMA_KEY_PRESS,
    AROMA_KEY_RELEASE,
    AROMA_KEY_CLICK,
    AROMA_KEY_SHORTCUT
} AromaTestKeyAction;

typedef enum AromaBenchMetric {
    AROMA_BENCH_WALLTIME_MS,
    AROMA_BENCH_WALLTIME_NS,
    AROMA_BENCH_EVENTS,
    AROMA_BENCH_CPU_TICKS
} AromaBenchMetric;

typedef enum AromaExpectMode {
    AROMA_EXPECT_ABORT,
    AROMA_EXPECT_CONTINUE
} AromaExpectMode;

typedef struct AromaTestCtx {
    AromaNode *root;
    int last_mouse_x;
    int last_mouse_y;
    bool mouse_down;
    int touch_id;
} AromaTestCtx;

bool aroma_test_init(AromaTestCtx *ctx);
void aroma_test_shutdown(AromaTestCtx *ctx);
void aroma_test_pump(void);
void aroma_test_wait_ms(int ms);
void aroma_test_sleep_ms(int ms);
bool aroma_test_try_until(bool (*pred)(void *), void *user_data, int timeout_ms);

void aroma_test_set_default_delay(int ms);
int aroma_test_get_default_delay(void);
void aroma_test_set_default_timeout(int ms);
int aroma_test_default_timeout(void);

bool aroma_test_rect(AromaNode *node, AromaRect *out);
bool aroma_test_center(AromaNode *node, int *x, int *y);
AromaNode *aroma_test_hit(AromaNode *root, int x, int y);
AromaNode *aroma_test_root_of(AromaNode *node);

AromaNode *aroma_test_inject_mouse(AromaNode *root, AromaEventType type,
                                   int x, int y, uint8_t button);
AromaNode *aroma_test_inject_scroll(AromaNode *root, int x, int y,
                                    float dx, float dy);
AromaNode *aroma_test_inject_key(AromaNode *target, AromaEventType type,
                                 uint32_t key_code, uint16_t modifiers);

bool aroma_test_mouse_move(AromaNode *root, int x, int y);
bool aroma_test_mouse_press(AromaNode *root, int x, int y, uint8_t button);
bool aroma_test_mouse_release(AromaNode *root, int x, int y, uint8_t button);
bool aroma_test_mouse_click(AromaNode *root, int x, int y, uint8_t button);
bool aroma_test_mouse_dclick(AromaNode *root, int x, int y, uint8_t button);
bool aroma_test_click(AromaNode *widget, uint8_t button);
bool aroma_test_dclick(AromaNode *widget, uint8_t button);
bool aroma_test_mouse_drag(AromaNode *root,
                           int x0, int y0, int x1, int y1, int steps);
bool aroma_test_drag(AromaNode *widget, int dx, int dy, int steps);
bool aroma_test_scroll(AromaNode *root, int x, int y, float dx, float dy);

AromaNode *aroma_test_mouse_event(AromaTestMouseAction action, AromaNode *root,
                                  int x, int y, uint8_t button,
                                  uint16_t mods, int delay_ms);
bool aroma_test_wheel(AromaNode *root, int x, int y,
                      int pixel_dx, int pixel_dy,
                      int angle_dx, int angle_dy);

bool aroma_test_touch_down(AromaNode *root, int id, int x, int y);
bool aroma_test_touch_move(AromaNode *root, int id, int x, int y);
bool aroma_test_touch_up(AromaNode *root, int id, int x, int y);
bool aroma_test_tap(AromaNode *root, int x, int y);
bool aroma_test_tap_widget(AromaNode *widget);
bool aroma_test_swipe(AromaNode *root,
                      int x0, int y0, int x1, int y1, int steps);

typedef struct AromaTouchSeq {
    AromaNode *root;
    int id;
    int xs[16];
    int ys[16];
    int kinds[16];
    int count;
    bool failed;
} AromaTouchSeq;

AromaTouchSeq aroma_touch_begin(AromaNode *root, int id);
AromaTouchSeq *aroma_touch_press(AromaTouchSeq *seq, int x, int y);
AromaTouchSeq *aroma_touch_move(AromaTouchSeq *seq, int x, int y);
AromaTouchSeq *aroma_touch_stationary(AromaTouchSeq *seq, int x, int y);
AromaTouchSeq *aroma_touch_release(AromaTouchSeq *seq, int x, int y);
bool aroma_touch_commit(AromaTouchSeq *seq);

bool aroma_test_key_press(AromaNode *target, uint32_t key, uint16_t mods);
bool aroma_test_key_release(AromaNode *target, uint32_t key, uint16_t mods);
bool aroma_test_key_click(AromaNode *target, uint32_t key, uint16_t mods);
bool aroma_test_type_text(AromaNode *target, const char *text);
bool aroma_test_key_sequence(AromaNode *target, const uint32_t *keys, size_t n);
AromaNode *aroma_test_key_event(AromaTestKeyAction action, AromaNode *target,
                                uint32_t key, uint16_t mods, int delay_ms);

bool aroma_test_wait_visible(AromaNode *node, int timeout_ms);
bool aroma_test_wait_hidden(AromaNode *node, int timeout_ms);
bool aroma_test_wait_focused(AromaNode *node, int timeout_ms);

typedef struct AromaTestSpy {
    uint64_t node_id;
    AromaEventType type;
    int count;
    AromaEvent last;
    bool has_last;
    bool attached;
} AromaTestSpy;

bool aroma_test_spy_start(AromaTestSpy *spy, uint64_t node_id, AromaEventType type);
void aroma_test_spy_stop(AromaTestSpy *spy);
void aroma_test_spy_clear(AromaTestSpy *spy);
int aroma_test_spy_count(const AromaTestSpy *spy);
bool aroma_test_spy_wait(AromaTestSpy *spy, int expected, int timeout_ms);

void aroma_test_begin(const char *name);
void aroma_test_end(void);
const char *aroma_test_current_func(void);
bool aroma_test_current_failed(void);
bool aroma_test_current_resolved(void);
void aroma_test_stats(int *passed, int *failed, int *skipped);
void aroma_test_reset_stats(void);
void aroma_test_expect_fail(const char *comment, AromaExpectMode mode);
void aroma_test_skip_at(const char *msg, const char *file, int line);
bool aroma_test_check_at(bool cond, const char *expr,
                         const char *file, int line, const char *msg);
bool aroma_test_check_cmp_ll(const char *expr_a, const char *expr_b,
                             long long a, long long b, const char *op,
                             const char *file, int line);
bool aroma_test_check_cmp_dbl(const char *expr_a, const char *expr_b,
                              double a, double b, const char *op,
                              const char *file, int line);
bool aroma_test_check_cmp_str(const char *expr_a, const char *expr_b,
                              const char *a, const char *b, const char *op,
                              const char *file, int line);
bool aroma_test_check_cmp_ptr(const char *expr_a, const char *expr_b,
                              const void *a, const void *b, const char *op,
                              const char *file, int line);

#define AROMA_TCHECK(passed_var, failed_var, cond, name) \
    do { \
        if (cond) { (passed_var)++; } \
        else { (failed_var)++; printf("[FAIL] %s (%s:%d)\n", name, __FILE__, __LINE__); } \
    } while (0)

#define AROMA_TEST_VERIFY(cond) \
    do { \
        if (!aroma_test_check_at((bool)(cond), #cond, __FILE__, __LINE__, NULL)) return; \
    } while (0)

#define AROMA_TEST_VERIFY_MSG(cond, message) \
    do { \
        if (!aroma_test_check_at((bool)(cond), #cond, __FILE__, __LINE__, message)) return; \
    } while (0)

#define AROMA_TEST_FAIL(message) \
    do { \
        aroma_test_check_at(false, "FAIL", __FILE__, __LINE__, message); \
        return; \
    } while (0)

#define AROMA_TEST_SKIP(message) \
    do { \
        aroma_test_skip_at(message, __FILE__, __LINE__); \
        return; \
    } while (0)

#define AROMA_TEST_EXPECT_FAIL(comment, mode) aroma_test_expect_fail(comment, mode)

#define AROMA_TEST_COMPARE_INT(actual, expected) \
    do { \
        if (!aroma_test_check_cmp_ll(#actual, #expected, (long long)(actual), (long long)(expected), "==", __FILE__, __LINE__)) return; \
    } while (0)

#define AROMA_TEST_COMPARE_DBL(actual, expected) \
    do { \
        if (!aroma_test_check_cmp_dbl(#actual, #expected, (double)(actual), (double)(expected), "==", __FILE__, __LINE__)) return; \
    } while (0)

#define AROMA_TEST_COMPARE_STR(actual, expected) \
    do { \
        if (!aroma_test_check_cmp_str(#actual, #expected, (actual), (expected), "==", __FILE__, __LINE__)) return; \
    } while (0)

#define AROMA_TEST_COMPARE_PTR(actual, expected) \
    do { \
        if (!aroma_test_check_cmp_ptr(#actual, #expected, (const void *)(actual), (const void *)(expected), "==", __FILE__, __LINE__)) return; \
    } while (0)

#define AROMA_TEST_COMPARE_EQ(a, e) AROMA_TEST_COMPARE_INT(a, e)
#define AROMA_TEST_COMPARE_EQ_DBL(a, e) AROMA_TEST_COMPARE_DBL(a, e)
#define AROMA_TEST_COMPARE_NE(a, e) \
    do { \
        if (!aroma_test_check_cmp_ll(#a, #e, (long long)(a), (long long)(e), "!=", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_LT(a, e) \
    do { \
        if (!aroma_test_check_cmp_ll(#a, #e, (long long)(a), (long long)(e), "<", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_LE(a, e) \
    do { \
        if (!aroma_test_check_cmp_ll(#a, #e, (long long)(a), (long long)(e), "<=", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_GT(a, e) \
    do { \
        if (!aroma_test_check_cmp_ll(#a, #e, (long long)(a), (long long)(e), ">", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_GE(a, e) \
    do { \
        if (!aroma_test_check_cmp_ll(#a, #e, (long long)(a), (long long)(e), ">=", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_NE_DBL(a, e) \
    do { \
        if (!aroma_test_check_cmp_dbl(#a, #e, (double)(a), (double)(e), "!=", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_LT_DBL(a, e) \
    do { \
        if (!aroma_test_check_cmp_dbl(#a, #e, (double)(a), (double)(e), "<", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_LE_DBL(a, e) \
    do { \
        if (!aroma_test_check_cmp_dbl(#a, #e, (double)(a), (double)(e), "<=", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_GT_DBL(a, e) \
    do { \
        if (!aroma_test_check_cmp_dbl(#a, #e, (double)(a), (double)(e), ">", __FILE__, __LINE__)) return; \
    } while (0)
#define AROMA_TEST_COMPARE_GE_DBL(a, e) \
    do { \
        if (!aroma_test_check_cmp_dbl(#a, #e, (double)(a), (double)(e), ">=", __FILE__, __LINE__)) return; \
    } while (0)

#define AROMA_TEST_TRY_POLL(cond, timeout_ms) \
    do { \
        uint64_t _at_t0 = aroma_time_now_ms(); \
        while (!(cond)) { \
            if ((int)(aroma_time_now_ms() - _at_t0) >= (int)(timeout_ms)) break; \
            aroma_test_wait_ms(5); \
        } \
    } while (0)

#define AROMA_TEST_TRY_VERIFY(cond) \
    do { AROMA_TEST_TRY_POLL((cond), aroma_test_default_timeout()); AROMA_TEST_VERIFY(cond); } while (0)
#define AROMA_TEST_TRY_VERIFY_WITH_TIMEOUT(cond, timeout_ms) \
    do { AROMA_TEST_TRY_POLL((cond), (timeout_ms)); AROMA_TEST_VERIFY(cond); } while (0)
#define AROMA_TEST_TRY_VERIFY_MSG(cond, message) \
    do { AROMA_TEST_TRY_POLL((cond), aroma_test_default_timeout()); AROMA_TEST_VERIFY_MSG(cond, message); } while (0)
#define AROMA_TEST_TRY_VERIFY_MSG_WITH_TIMEOUT(cond, message, timeout_ms) \
    do { AROMA_TEST_TRY_POLL((cond), (timeout_ms)); AROMA_TEST_VERIFY_MSG(cond, message); } while (0)

#define AROMA_TEST_TRY_COMPARE_INT(actual, expected) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), aroma_test_default_timeout()); AROMA_TEST_COMPARE_INT(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_INT_WITH_TIMEOUT(actual, expected, timeout_ms) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), (timeout_ms)); AROMA_TEST_COMPARE_INT(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_DBL(actual, expected) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), aroma_test_default_timeout()); AROMA_TEST_COMPARE_DBL(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_DBL_WITH_TIMEOUT(actual, expected, timeout_ms) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), (timeout_ms)); AROMA_TEST_COMPARE_DBL(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_PTR(actual, expected) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), aroma_test_default_timeout()); AROMA_TEST_COMPARE_PTR(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_PTR_WITH_TIMEOUT(actual, expected, timeout_ms) \
    do { AROMA_TEST_TRY_POLL(((actual) == (expected)), (timeout_ms)); AROMA_TEST_COMPARE_PTR(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_STR(actual, expected) \
    do { AROMA_TEST_TRY_POLL((strcmp((actual), (expected)) == 0), aroma_test_default_timeout()); AROMA_TEST_COMPARE_STR(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_STR_WITH_TIMEOUT(actual, expected, timeout_ms) \
    do { AROMA_TEST_TRY_POLL((strcmp((actual), (expected)) == 0), (timeout_ms)); AROMA_TEST_COMPARE_STR(actual, expected); } while (0)
#define AROMA_TEST_TRY_COMPARE_EQ(a, e) AROMA_TEST_TRY_COMPARE_INT(a, e)
#define AROMA_TEST_TRY_COMPARE_EQ_WITH_TIMEOUT(a, e, t) AROMA_TEST_TRY_COMPARE_INT_WITH_TIMEOUT(a, e, t)
#define AROMA_TEST_TRY_COMPARE_NE_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) != (e)), (t)); AROMA_TEST_COMPARE_NE(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_LT_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) < (e)), (t)); AROMA_TEST_COMPARE_LT(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_LE_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) <= (e)), (t)); AROMA_TEST_COMPARE_LE(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_GT_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) > (e)), (t)); AROMA_TEST_COMPARE_GT(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_GE_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) >= (e)), (t)); AROMA_TEST_COMPARE_GE(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_NE(a, e) AROMA_TEST_TRY_COMPARE_NE_WITH_TIMEOUT(a, e, aroma_test_default_timeout())
#define AROMA_TEST_TRY_COMPARE_LT(a, e) AROMA_TEST_TRY_COMPARE_LT_WITH_TIMEOUT(a, e, aroma_test_default_timeout())
#define AROMA_TEST_TRY_COMPARE_LE(a, e) AROMA_TEST_TRY_COMPARE_LE_WITH_TIMEOUT(a, e, aroma_test_default_timeout())
#define AROMA_TEST_TRY_COMPARE_GT(a, e) AROMA_TEST_TRY_COMPARE_GT_WITH_TIMEOUT(a, e, aroma_test_default_timeout())
#define AROMA_TEST_TRY_COMPARE_GE(a, e) AROMA_TEST_TRY_COMPARE_GE_WITH_TIMEOUT(a, e, aroma_test_default_timeout())
#define AROMA_TEST_TRY_COMPARE_NE_DBL_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) != (e)), (t)); AROMA_TEST_COMPARE_NE_DBL(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_LT_DBL_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) < (e)), (t)); AROMA_TEST_COMPARE_LT_DBL(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_LE_DBL_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) <= (e)), (t)); AROMA_TEST_COMPARE_LE_DBL(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_GT_DBL_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) > (e)), (t)); AROMA_TEST_COMPARE_GT_DBL(a, e); } while (0)
#define AROMA_TEST_TRY_COMPARE_GE_DBL_WITH_TIMEOUT(a, e, t) \
    do { AROMA_TEST_TRY_POLL(((a) >= (e)), (t)); AROMA_TEST_COMPARE_GE_DBL(a, e); } while (0)

#define AROMA_TEST_MAX_COLS 16
#define AROMA_TEST_MAX_ROWS 128
#define AROMA_TEST_CELL_SIZE 64

bool aroma_test_add_column(const char *name, size_t elem_size);
bool aroma_test_new_row(const char *tag);
bool aroma_test_push(const void *value);
const void *aroma_test_fetch(const char *name);
int aroma_test_fetch_int(const char *name);
const char *aroma_test_fetch_str(const char *name);
const char *aroma_test_data_tag(void);
int aroma_test_row_count(void);
bool aroma_test_select_row(int index);
void aroma_test_data_clear(void);

#define AROMA_TEST_ADD_COLUMN(type, name) aroma_test_add_column(name, sizeof(type))
#define AROMA_TEST_NEW_ROW(tag) aroma_test_new_row(tag)
#define AROMA_TEST_PUSH(value) \
    do { __typeof__(value) _at_v_ = (value); aroma_test_push(&_at_v_); } while (0)
#define AROMA_TEST_FETCH(type, name) (*(const type *)aroma_test_fetch(name))

uint64_t aroma_bench_now_ns(void);
const char *aroma_bench_metric_name(AromaBenchMetric metric);
const char *aroma_bench_metric_unit(AromaBenchMetric metric);
void aroma_test_set_bench_result(double value, AromaBenchMetric metric);
double aroma_test_bench_result(AromaBenchMetric *metric_out);

#define AROMA_TEST_BENCHMARK(code) \
    do { \
        int _at_n = 5; \
        uint64_t _at_dt = 0; \
        for (;;) { \
            uint64_t _at_t0 = aroma_bench_now_ns(); \
            for (int _at_i = 0; _at_i < _at_n; _at_i++) { code; } \
            _at_dt = aroma_bench_now_ns() - _at_t0; \
            if (_at_dt >= 100000000ULL || _at_n >= 200000) break; \
            _at_n *= 5; \
            if (_at_n > 200000) _at_n = 200000; \
        } \
        aroma_test_set_bench_result((double)_at_dt / (double)_at_n, AROMA_BENCH_WALLTIME_NS); \
        printf("BENCH %s:%d: %d iters, avg %.1f ns/iter\n", __FILE__, __LINE__, _at_n, (double)_at_dt / (double)_at_n); \
    } while (0)

#define AROMA_TEST_BENCHMARK_ONCE(code) \
    do { \
        uint64_t _at_t0 = aroma_bench_now_ns(); \
        { code; } \
        uint64_t _at_dt = aroma_bench_now_ns() - _at_t0; \
        aroma_test_set_bench_result((double)_at_dt, AROMA_BENCH_WALLTIME_NS); \
        printf("BENCH %s:%d: once, %.1f ns\n", __FILE__, __LINE__, (double)_at_dt); \
    } while (0)

typedef void (*AromaTestFn)(void);

bool aroma_test_register(const char *name, AromaTestFn test_fn);
bool aroma_test_register_data(const char *name, AromaTestFn data_fn);
void aroma_test_set_hooks(AromaTestFn init_case, AromaTestFn cleanup_case,
                          AromaTestFn init, AromaTestFn cleanup);
int aroma_test_run(int argc, char **argv);

#define AROMA_TEST_CASE(name) \
    static void aroma_case_fn_##name(void); \
    __attribute__((constructor)) static void aroma_case_reg_##name(void) { \
        aroma_test_register(#name, aroma_case_fn_##name); \
    } \
    static void aroma_case_fn_##name(void)

#define AROMA_TEST_DATA(name) \
    static void aroma_data_fn_##name(void); \
    __attribute__((constructor)) static void aroma_data_reg_##name(void) { \
        aroma_test_register_data(#name, aroma_data_fn_##name); \
    } \
    static void aroma_data_fn_##name(void)

#define AROMA_TEST_MAIN() \
    int main(int argc, char **argv) { return aroma_test_run(argc, argv); }

bool aroma_test_ignore_message(const char *substr);
void aroma_test_fail_on_warning(const char *pattern_or_null);
bool aroma_test_check_ignored(void);

const char *aroma_test_find_data(const char *filename);

#ifdef __cplusplus
}
#endif

#endif
