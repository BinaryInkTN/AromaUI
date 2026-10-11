#include "test_aroma_testfw.h"
#include "aroma_test.h"
#include "aroma_logger.h"
#include "widgets/aroma_button.h"
#include "widgets/aroma_checkbox.h"
#include "widgets/aroma_slider.h"
#include "widgets/aroma_textbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_passed = 0;
static int s_failed = 0;
static int s_clicks = 0;

#define CHECK(cond, name) \
    do { \
        if (cond) { s_passed++; } \
        else { s_failed++; printf("[FAIL] %s (line %d)\n", name, __LINE__); } \
    } while (0)

static bool on_click(AromaNode *node, void *ud)
{
    (void)node;
    (void)ud;
    s_clicks++;
    return true;
}

static bool pred_true(void *ud)
{
    (void)ud;
    return true;
}

static bool pred_false(void *ud)
{
    (void)ud;
    return false;
}

static void test_fixture(void)
{
    AromaTestCtx ctx;
    CHECK(aroma_test_init(&ctx), "fixture init");
    CHECK(ctx.root != NULL, "fixture root");
    CHECK(aroma_event_get_root() == ctx.root, "event root set");
    aroma_test_shutdown(&ctx);
    CHECK(aroma_event_get_root() == NULL, "event root cleared");
}

static void test_mouse_click_button(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for mouse click");
        return;
    }
    AromaNode *btn = aroma_button_create(ctx.root, "OK", 40, 100, 140, 40);
    CHECK(btn != NULL, "button created");
    aroma_button_set_on_click(btn, on_click, NULL);
    aroma_button_setup_events(btn, NULL, NULL);

    s_clicks = 0;
    CHECK(aroma_test_mouse_click(ctx.root, 100, 120, AROMA_TEST_BUTTON_LEFT),
        "mouse_click hits");
    CHECK(s_clicks == 1, "mouse_click fires on_click");

    s_clicks = 0;
    CHECK(aroma_test_click(btn, AROMA_TEST_BUTTON_LEFT), "click widget");
    CHECK(s_clicks == 1, "click widget fires");

    s_clicks = 0;
    aroma_test_mouse_click(ctx.root, 500, 500, AROMA_TEST_BUTTON_LEFT);
    CHECK(s_clicks == 0, "click outside does not fire");

    aroma_test_shutdown(&ctx);
}

static void test_touch_tap_checkbox(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for tap");
        return;
    }
    AromaNode *cb = aroma_checkbox_create(ctx.root, "Accept", 10, 10, 200, 32);
    CHECK(cb != NULL, "checkbox created");
    aroma_checkbox_setup_events(cb, NULL, NULL);

    CHECK(!aroma_checkbox_is_checked(cb), "starts unchecked");
    CHECK(aroma_test_tap_widget(cb), "tap widget");
    CHECK(aroma_checkbox_is_checked(cb), "tap checks");
    CHECK(aroma_test_tap_widget(cb), "tap again");
    CHECK(!aroma_checkbox_is_checked(cb), "second tap unchecks");

    aroma_test_shutdown(&ctx);
}

static void test_type_text(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for typing");
        return;
    }
    AromaNode *tb = aroma_textbox_create(ctx.root, 10, 60, 200, 36);
    CHECK(tb != NULL, "textbox created");
    aroma_textbox_setup_events(tb, NULL, NULL, NULL);

    CHECK(aroma_test_click(tb, AROMA_TEST_BUTTON_LEFT), "focus textbox");
    CHECK(aroma_textbox_is_focused(tb), "textbox focused");
    CHECK(aroma_test_type_text(tb, "hi"), "type text");
    CHECK(strcmp(aroma_textbox_get_text(tb), "hi") == 0, "text matches");

    CHECK(aroma_test_key_click(tb, AROMA_TEST_KEY_BACKSPACE, 0), "backspace");
    CHECK(strcmp(aroma_textbox_get_text(tb), "h") == 0, "backspace erases");

    aroma_test_shutdown(&ctx);
}

static void test_spy_and_drag(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for spy");
        return;
    }
    AromaNode *s = aroma_slider_create(ctx.root, 10, 10, 200, 40, 0, 100, 50);
    CHECK(s != NULL, "slider created");
    aroma_slider_setup_events(s, NULL, NULL);

    AromaTestSpy spy;
    CHECK(aroma_test_spy_start(&spy, s->node_id, EVENT_TYPE_MOUSE_CLICK),
        "spy attaches");
    int cx, cy;
    CHECK(aroma_test_center(s, &cx, &cy), "slider center");
    aroma_test_mouse_press(ctx.root, cx, cy, AROMA_TEST_BUTTON_LEFT);
    CHECK(aroma_test_spy_count(&spy) >= 1, "spy saw press");
    aroma_test_mouse_release(ctx.root, cx, cy, AROMA_TEST_BUTTON_LEFT);
    aroma_test_spy_stop(&spy);

    int v0 = aroma_slider_get_value(s);
    CHECK(aroma_test_mouse_drag(ctx.root, cx, cy, cx + 60, cy, 5), "drag");
    CHECK(aroma_slider_get_value(s) != v0, "drag changes value");

    CHECK(aroma_test_try_until(pred_true, NULL, 50), "try_until true");
    CHECK(!aroma_test_try_until(pred_false, NULL, 20), "try_until false times out");

    aroma_test_shutdown(&ctx);
}

static void test_checks_pass(void)
{
    int p0 = 0, f0 = 0, s0 = 0;
    aroma_test_stats(&p0, &f0, &s0);
    aroma_test_begin("checks_pass");

    int v = 41;
    AROMA_TEST_VERIFY(v == 41);
    AROMA_TEST_VERIFY_MSG(v > 0, "positive");
    AROMA_TEST_COMPARE_INT(v, 41);
    AROMA_TEST_COMPARE_EQ(v, 41);
    AROMA_TEST_COMPARE_NE(v, 42);
    AROMA_TEST_COMPARE_LT(v, 42);
    AROMA_TEST_COMPARE_LE(v, 41);
    AROMA_TEST_COMPARE_GT(v, 40);
    AROMA_TEST_COMPARE_GE(v, 41);
    AROMA_TEST_COMPARE_STR("abc", "abc");
    AROMA_TEST_COMPARE_PTR(&v, &v);
    double d = 1.5;
    AROMA_TEST_COMPARE_DBL(d, 1.5);
    AROMA_TEST_COMPARE_LT_DBL(d, 2.5);

    aroma_test_end();
    int p1 = 0, f1 = 0, s1 = 0;
    aroma_test_stats(&p1, &f1, &s1);
    CHECK(p1 - p0 == 13, "13 checks recorded as passed");
    CHECK(f1 == f0, "no failures recorded");
    CHECK(s1 == s0, "no skips recorded");
    CHECK(!aroma_test_current_failed(), "test not marked failed");
}

static int s_after_abort = 0;

static void inner_abort(void)
{
    AROMA_TEST_VERIFY(1 == 2);
    s_after_abort = 1;
}

static void inner_fail(void)
{
    AROMA_TEST_FAIL("demo failure");
}

static void test_abort_semantics(void)
{
    s_after_abort = 0;
    aroma_test_begin("abort");
    inner_abort();
    CHECK(s_after_abort == 0, "VERIFY failure aborts function");
    CHECK(aroma_test_current_failed(), "failure flag set");
    aroma_test_reset_stats();
    aroma_test_begin("fail");
    inner_fail();
    CHECK(aroma_test_current_failed(), "FAIL marks failed");
    aroma_test_reset_stats();
    aroma_test_end();
}

static int s_skip_reached = 0;

static void inner_skip(void)
{
    AROMA_TEST_SKIP("demo skip");
    s_skip_reached = 1;
}

static void test_expect_skip(void)
{
    aroma_test_begin("expect");
    aroma_test_expect_fail("known bug", AROMA_EXPECT_CONTINUE);
    AROMA_TEST_VERIFY(1 == 2);
    CHECK(!aroma_test_current_failed(), "XFAIL does not fail test");

    aroma_test_expect_fail("optimistic", AROMA_EXPECT_CONTINUE);
    AROMA_TEST_VERIFY(1 == 1);
    CHECK(aroma_test_current_failed(), "XPASS is a failure");
    aroma_test_reset_stats();
    aroma_test_end();

    s_skip_reached = 0;
    aroma_test_begin("skip");
    inner_skip();
    CHECK(s_skip_reached == 0, "SKIP aborts function");
    int sk0 = 0;
    aroma_test_stats(NULL, NULL, &sk0);
    CHECK(sk0 >= 1, "skip recorded");
    aroma_test_end();
}

static void test_try_macros(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for try macros");
        return;
    }
    AromaNode *btn = aroma_button_create(ctx.root, "Go", 10, 10, 120, 36);
    if (!btn) {
        CHECK(0, "button for try macros");
        aroma_test_shutdown(&ctx);
        return;
    }
    aroma_button_set_on_click(btn, on_click, NULL);
    aroma_button_setup_events(btn, NULL, NULL);

    aroma_test_begin("try");
    AROMA_TEST_TRY_VERIFY_WITH_TIMEOUT(btn != NULL, 50);
    AROMA_TEST_TRY_VERIFY_WITH_TIMEOUT(s_clicks == 0, 50);
    s_clicks = 0;
    aroma_test_click(btn, AROMA_TEST_BUTTON_LEFT);
    AROMA_TEST_TRY_COMPARE_INT(s_clicks, 1);
    AROMA_TEST_TRY_COMPARE_INT_WITH_TIMEOUT(s_clicks, 1, 100);
    AROMA_TEST_TRY_COMPARE_GE_WITH_TIMEOUT(s_clicks, 1, 100);
    AROMA_TEST_TRY_VERIFY_MSG(s_clicks == 1, "clicked once");
    aroma_test_end();

    aroma_test_shutdown(&ctx);
}

static void test_data_table(void)
{
    aroma_test_data_clear();
    CHECK(aroma_test_add_column("input", sizeof(int)), "add int column");
    CHECK(aroma_test_add_column("label", sizeof(char *)), "add str column");
    CHECK(!aroma_test_add_column("input", sizeof(int)), "dup column rejected");
    CHECK(aroma_test_new_row("case1"), "new row");
    int v = 42;
    CHECK(aroma_test_push(&v), "push int");
    const char *s = "hi";
    CHECK(aroma_test_push(&s), "push str");
    CHECK(!aroma_test_push(&v), "over-push rejected");
    CHECK(aroma_test_fetch_int("input") == 42, "fetch int");
    CHECK(strcmp(aroma_test_fetch_str("label"), "hi") == 0, "fetch str");
    CHECK(AROMA_TEST_FETCH(int, "input") == 42, "FETCH");
    CHECK(aroma_test_data_tag() && strcmp(aroma_test_data_tag(), "case1") == 0,
          "data tag");
    CHECK(aroma_test_row_count() == 1, "row count");

    CHECK(aroma_test_new_row("case2"), "second row");
    CHECK(aroma_test_select_row(0), "select row 0");
    CHECK(strcmp(aroma_test_data_tag(), "case1") == 0, "row 0 tag");
    CHECK(aroma_test_select_row(1), "select row 1");
    CHECK(!aroma_test_select_row(7), "bad row rejected");

    AROMA_TEST_ADD_COLUMN(int, "n");
    CHECK(aroma_test_row_count() == 2, "cols do not add rows");
    CHECK(aroma_test_new_row("m"), "macro row");
    AROMA_TEST_NEW_ROW("m2");
    AROMA_TEST_PUSH(7);
    CHECK(aroma_test_fetch_int("input") == 7, "macro push/fetch");
    aroma_test_data_clear();
    CHECK(aroma_test_row_count() == 0, "table cleared");
}

static void test_benchmark(void)
{
    volatile int acc = 0;
    AROMA_TEST_BENCHMARK(acc++);
    AROMA_TEST_BENCHMARK_ONCE(acc++);
    AromaBenchMetric m = AROMA_BENCH_EVENTS;
    double val = aroma_test_bench_result(&m);
    CHECK(val > 0.0, "bench result recorded");
    CHECK(m == AROMA_BENCH_WALLTIME_NS, "bench metric");
    CHECK(aroma_bench_metric_name(AROMA_BENCH_WALLTIME_MS) != NULL, "metric name");
    CHECK(aroma_bench_metric_unit(AROMA_BENCH_WALLTIME_NS) != NULL, "metric unit");
    CHECK(acc > 0, "bench body ran");
}

static void test_touch_seq(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for touch seq");
        return;
    }
    AromaNode *cb = aroma_checkbox_create(ctx.root, "Seq", 10, 10, 200, 32);
    if (!cb) {
        CHECK(0, "checkbox for touch seq");
        aroma_test_shutdown(&ctx);
        return;
    }
    aroma_checkbox_setup_events(cb, NULL, NULL);
    int cx = 0, cy = 0;
    CHECK(aroma_test_center(cb, &cx, &cy), "seq center");

    AromaTouchSeq seq = aroma_touch_begin(ctx.root, 1);
    aroma_touch_press(&seq, cx, cy);
    aroma_touch_stationary(&seq, cx, cy);
    aroma_touch_release(&seq, cx, cy);
    CHECK(aroma_touch_commit(&seq), "seq tap commits");
    CHECK(aroma_checkbox_is_checked(cb), "seq tap checks");

    AromaTouchSeq empty = aroma_touch_begin(ctx.root, 2);
    CHECK(!aroma_touch_commit(&empty), "empty seq fails");
    AromaTouchSeq bad = aroma_touch_begin(ctx.root, 99);
    CHECK(!aroma_touch_commit(&bad), "bad id fails");
    AromaTouchSeq noroor = aroma_touch_begin(NULL, 0);
    CHECK(!aroma_touch_commit(&noroor), "null root fails");

    aroma_test_shutdown(&ctx);
}

static void test_wheel_delay_waits(void)
{
    AromaTestCtx ctx;
    if (!aroma_test_init(&ctx)) {
        CHECK(0, "init for wheel/waits");
        return;
    }
    CHECK(aroma_test_wheel(ctx.root, 5, 5, 0, 0, 0, 120), "wheel injects");
    CHECK(aroma_test_get_default_delay() == 0, "default delay 0");
    aroma_test_set_default_delay(1);
    CHECK(aroma_test_get_default_delay() == 1, "default delay set");
    aroma_test_set_default_delay(0);

    AromaNode *tb = aroma_textbox_create(ctx.root, 10, 60, 200, 36);
    if (!tb) {
        CHECK(0, "textbox for waits");
        aroma_test_shutdown(&ctx);
        return;
    }
    aroma_textbox_setup_events(tb, NULL, NULL, NULL);
    CHECK(aroma_test_wait_visible(tb, 50), "visible waits pass");
    CHECK(!aroma_test_wait_hidden(tb, 20), "hidden wait times out");
    CHECK(!aroma_test_wait_focused(tb, 20), "unfocused wait times out");
    CHECK(aroma_test_click(tb, AROMA_TEST_BUTTON_LEFT), "focus for wait");
    CHECK(aroma_test_wait_focused(tb, 100), "focused wait passes");
    CHECK(aroma_test_key_event(AROMA_KEY_SHORTCUT, tb, (uint32_t)'Q', 0, 0) != NULL,
          "shortcut types");
    CHECK(strstr(aroma_textbox_get_text(tb), "Q") != NULL, "shortcut text");
    CHECK(aroma_test_mouse_event(AROMA_MOUSE_MOVE, ctx.root, 1, 1,
                                 AROMA_TEST_BUTTON_LEFT, 0, 0) != NULL,
          "mouse event move");
    aroma_node_set_hidden(tb, true);
    CHECK(aroma_test_wait_hidden(tb, 50), "hidden wait passes");
    CHECK(!aroma_test_wait_visible(tb, 20), "visible wait times out");
    aroma_test_sleep_ms(1);

    aroma_test_shutdown(&ctx);
}

static void test_warn_ignore(void)
{
    set_minimum_log_level(DEBUG_LEVEL_WARNING);
    CHECK(aroma_test_ignore_message("aroma-test-marker"), "ignore armed");
    LOG_WARNING("aroma-test-marker %d", 7);
    CHECK(aroma_test_check_ignored(), "ignored message verified");

    CHECK(aroma_test_ignore_message("never-emitted-marker"), "ignore armed 2");
    CHECK(!aroma_test_check_ignored(), "missing message fails");

    aroma_test_begin("fow");
    aroma_test_fail_on_warning("aroma-fow-marker");
    LOG_WARNING("aroma-fow-marker fired");
    CHECK(aroma_test_current_failed(), "fail-on-warning fires");
    aroma_test_check_ignored();
    aroma_test_reset_stats();
    aroma_test_end();
    set_minimum_log_level(DEBUG_LEVEL_CRITICAL);
}

static int s_runner_hits = 0;

static void dummy_runner_test(void)
{
    s_runner_hits++;
}

static void test_runner(void)
{
    CHECK(aroma_test_register("selftest_dummy", dummy_runner_test), "registered");
    CHECK(aroma_test_register("selftest_dummy", dummy_runner_test), "re-register ok");
    CHECK(!aroma_test_register(NULL, dummy_runner_test), "null name rejected");
    s_runner_hits = 0;
    char *args[] = {"t", "--filter=selftest_dummy"};
    CHECK(aroma_test_run(2, args) == 0, "runner passes");
    CHECK(s_runner_hits == 1, "runner ran once");
    char *listargs[] = {"t", "--list"};
    CHECK(aroma_test_run(2, listargs) == 0, "list works");
}

static void test_find_data(void)
{
    CHECK(aroma_test_find_data(NULL) == NULL, "null rejected");
    CHECK(aroma_test_find_data("") == NULL, "empty rejected");
    CHECK(aroma_test_find_data("no_such_file_xyz_123") == NULL, "missing NULL");
    setenv("AROMA_TESTDATA", "/bin", 1);
    CHECK(aroma_test_find_data("sh") != NULL, "testdata env found");
    unsetenv("AROMA_TESTDATA");
}

void run_testfw_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma TestFW Tests ===\n");
    test_fixture();
    test_mouse_click_button();
    test_touch_tap_checkbox();
    test_type_text();
    test_spy_and_drag();
    test_checks_pass();
    test_abort_semantics();
    test_expect_skip();
    test_try_macros();
    test_data_table();
    test_benchmark();
    test_touch_seq();
    test_wheel_delay_waits();
    test_warn_ignore();
    test_runner();
    test_find_data();
    printf("Aroma TestFW: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
