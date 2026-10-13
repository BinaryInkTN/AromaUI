
AromaTest is a headless framework for simulating input on an `AromaNode` tree. No window, GPU, or platform backend is required: events go through the real event system (`aroma_event_hit_test` + `aroma_event_dispatch`), so widget handlers run exactly as in production.

## Fixture

```c
#include "aroma_test.h"

AromaTestCtx t;
aroma_test_init(&t);   // node system + event system + root
// ... build widgets under t.root, call aroma_*_setup_events ...
aroma_test_pump();     // drain queue + tick timers
aroma_test_shutdown(&t);
```

`aroma_test_pump()` drains the event queue and ticks timers. `aroma_test_wait_ms()` sleeps then pumps; `aroma_test_sleep_ms()` sleeps without pumping. `aroma_test_try_until(pred, ud, timeout_ms)` polls a predicate until it passes or the timeout expires.

`aroma_test_set_default_delay(ms)` makes every simulated event wait first. `aroma_test_set_default_timeout(ms)` sets the timeout used by all `TRY_*` macros without an explicit timeout (default 5000 ms).

## Simulated Input

```c
aroma_test_mouse_click(t.root, 100, 120, AROMA_TEST_BUTTON_LEFT);
aroma_test_click(button_node, AROMA_TEST_BUTTON_LEFT); // center of widget
aroma_test_mouse_drag(t.root, x0, y0, x1, y1, 5);
aroma_test_mouse_dclick(t.root, x, y, AROMA_TEST_BUTTON_LEFT);
aroma_test_scroll(t.root, x, y, 0.0f, -120.0f);
aroma_test_wheel(t.root, x, y, 0, 0, 0, 120); // pixel + angle (1/8 deg) deltas
```

`aroma_test_mouse_event(action, ...)` dispatches by `AromaTestMouseAction` (press/release/click/dclick/move) with an explicit delay. Mouse press = `MOUSE_CLICK`, release = `MOUSE_RELEASE`, matching what the GLPS/GLFW backends queue. Keyboard modifiers on mouse calls are accepted but ignored: `AromaMouseEventData` carries no modifier field.

Touch goes through `aroma_event_handle_touch`, including scroll capture:

```c
aroma_test_tap_widget(checkbox_node);
aroma_test_swipe(t.root, x0, y0, x1, y1, 5);

AromaTouchSeq seq = aroma_touch_begin(t.root, 0); // buffered touch sequence
aroma_touch_press(&seq, x, y);
aroma_touch_stationary(&seq, x, y);
aroma_touch_release(&seq, x, y);
aroma_touch_commit(&seq);
```

Keyboard targets an explicit node, or the focused node when passed `NULL`:

```c
aroma_test_click(textbox_node, AROMA_TEST_BUTTON_LEFT); // focus
aroma_test_type_text(textbox_node, "hi");
aroma_test_key_click(textbox_node, AROMA_TEST_KEY_BACKSPACE, 0);
aroma_test_key_event(AROMA_KEY_PRESS, tb, key, mods, delay_ms);
```

`AROMA_KEY_SHORTCUT` behaves as click (Aroma has no shortcut system). Modifiers reuse `AROMA_KEY_MOD_*`.

Wait helpers for async UI state: `aroma_test_wait_visible()`, `aroma_test_wait_hidden()`, `aroma_test_wait_focused()`.

## Verification Checks

Abort-style macros for `void` test functions (a failure records, prints `file:line`, and returns):

```c
AROMA_TEST_VERIFY(cond);
AROMA_TEST_VERIFY_MSG(cond, "message");
AROMA_TEST_COMPARE_INT(a, b);   // integers (also _EQ/_NE/_LT/_LE/_GT/_GE)
AROMA_TEST_COMPARE_DBL(a, b);   // doubles (+ _NE/_LT/_LE/_GT/_GE/_EQ_DBL)
AROMA_TEST_COMPARE_STR(a, b);   // strcmp content compare, NULL-safe
AROMA_TEST_COMPARE_PTR(a, b);   // address compare
AROMA_TEST_FAIL("message");
AROMA_TEST_SKIP("reason");
AROMA_TEST_EXPECT_FAIL("known bug", AROMA_EXPECT_CONTINUE); // or _ABORT
```

C has no overloading, so comparison needs a typed macro: `_INT` for integers, `_DBL` for floating point, `_STR` for `char *` (always use `_STR` for strings, including literals), `_PTR` for addresses.

Polling variants pump the event loop while waiting, then check once:

```c
AROMA_TEST_TRY_VERIFY(cond);
AROMA_TEST_TRY_VERIFY_WITH_TIMEOUT(cond, ms);
AROMA_TEST_TRY_COMPARE_INT(a, b);                         // + _STR/_DBL/_PTR
AROMA_TEST_TRY_COMPARE_INT_WITH_TIMEOUT(a, b, ms);        // + relational _NE/_LT/...
```

`aroma_test_begin(name)` / `aroma_test_end()` track the current test for messages; `aroma_test_current_func()`, `aroma_test_current_failed()`, `aroma_test_current_resolved()`, and `aroma_test_stats(&p, &f, &s)` expose results. `AROMA_TCHECK()` remains for the older continue-style suites.

## Data-Driven Tests

```c
aroma_test_data_clear();
AROMA_TEST_ADD_COLUMN(int, "input");
AROMA_TEST_NEW_ROW("case1");
AROMA_TEST_PUSH(42);
int v = AROMA_TEST_FETCH(int, "input"); // + aroma_test_fetch_int/str
aroma_test_data_tag();
```

## Benchmarks

```c
AROMA_TEST_BENCHMARK(acc++);       // adaptive iterations, reports avg ns/iter
AROMA_TEST_BENCHMARK_ONCE(expensive());
aroma_test_bench_result(&metric);
aroma_bench_metric_name(m);
```

## Test Runner

```c
AROMA_TEST_CASE(name) { /* test */ }   // auto-registers the case
AROMA_TEST_DATA(name) { /* rows */ }   // data function for the case
aroma_test_set_hooks(initCase, cleanupCase, init, cleanup);
AROMA_TEST_MAIN()                       // main() calling aroma_test_run()
```

`aroma_test_run(argc, argv)` supports `--list` and `--filter=SUBSTR`, prints `PASS:`/`FAIL:`/`SKIP:` per case (`name:tag` per data row) plus totals, and returns the failure count.

## Test Spies and Diagnostics

`AromaTestSpy` observes events before widget handlers consume them (priority 2000, never consumes): `aroma_test_spy_start/stop/clear/count/wait`.

```c
aroma_test_ignore_message("substring");
aroma_test_fail_on_warning("pattern");    // NULL = any warning
aroma_test_check_ignored();               // verify at test end (runner does this)
aroma_test_find_data("file.txt");
```

No resource-extraction equivalent exists (Aroma has no bundled resource system); point `AROMA_TESTDATA` at a directory to resolve files.

## Additional Notes

- Coordinates are in the same space as widget rects. `aroma_test_center()` / `aroma_test_rect()` / `aroma_test_hit()` help resolve them.
- Mouse injection hit-tests at press and release separately, like the real backends. Pressing inside and releasing outside does not activate buttons.
- Touch id must be 0-9; touch sequences hold up to 16 points.
- `AROMA_TEST_PUSH` uses `__typeof__` (GCC/Clang).
- The framework lives in `include/aroma_test.h` / `src/core/aroma_test.c` and is linked into `libaroma`. See `tests/core/test_aroma_testfw.c` for full examples.
