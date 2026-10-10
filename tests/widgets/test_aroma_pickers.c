#include "test_aroma_pickers.h"
#include "widgets/aroma_calendar.h"
#include "widgets/aroma_datepicker.h"
#include "widgets/aroma_timepicker.h"
#include "widgets/aroma_stepper.h"
#include "widgets/aroma_carousel.h"
#include "widgets/aroma_container.h"
#include "aroma_node.h"
#include "aroma_slab_alloc.h"

#include <stdio.h>

static int s_passed = 0;
static int s_failed = 0;

#define CHECK(cond, name)                                            \
    do {                                                             \
        if (cond) {                                                  \
            s_passed++;                                              \
        } else {                                                     \
            s_failed++;                                              \
            printf("[FAIL] %s (line %d)\n", name, __LINE__);         \
        }                                                            \
    } while (0)

static AromaNode *make_root(void)
{
    void *w = aroma_widget_alloc(32);
    return __create_node(NODE_TYPE_ROOT, NULL, w);
}

static void test_date_math(void)
{
    CHECK(aroma_calendar_is_leap(2024), "2024 is leap");
    CHECK(!aroma_calendar_is_leap(2025), "2025 not leap");
    CHECK(!aroma_calendar_is_leap(1900), "1900 not leap");
    CHECK(aroma_calendar_is_leap(2000), "2000 is leap");
    CHECK(aroma_calendar_days_in_month(2024, 2) == 29, "feb leap 29");
    CHECK(aroma_calendar_days_in_month(2025, 2) == 28, "feb 28");
    CHECK(aroma_calendar_days_in_month(2026, 13) == 30, "bad month safe");
    int wd = aroma_calendar_first_weekday(2026, 10);
    CHECK(wd >= 0 && wd <= 6, "weekday in range");
}

static void test_calendar(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    CHECK(aroma_calendar_create(NULL, 0, 0, 300, 280) == NULL,
          "calendar rejects NULL parent");
    AromaNode *c = aroma_calendar_create(root, 0, 0, 320, 300);
    CHECK(c != NULL, "calendar creates");
    aroma_calendar_set_date(c, 2026, 10, 6);
    int y = 0, m = 0, d = 0;
    aroma_calendar_get_date(c, &y, &m, &d);
    CHECK(y == 2026 && m == 10 && d == 6, "calendar set/get roundtrip");
    aroma_calendar_set_date(c, 9999, 99, 99);
    aroma_calendar_get_date(c, &y, &m, &d);
    CHECK(y == 2026 && m == 10, "calendar ignores bad date");
    aroma_calendar_set_date(NULL, 2026, 1, 1);
    aroma_calendar_get_date(NULL, NULL, NULL, NULL);
    aroma_calendar_set_on_select(c, NULL, NULL);
    aroma_calendar_set_on_select(NULL, NULL, NULL);
    aroma_calendar_set_font(c, NULL);
    CHECK(aroma_calendar_setup_events(c, NULL, NULL), "calendar events ok");
    CHECK(!aroma_calendar_setup_events(NULL, NULL, NULL),
          "calendar events NULL-safe");
    aroma_calendar_draw(c, 9999);
    aroma_calendar_draw(NULL, 0);
    aroma_calendar_destroy(NULL);
    aroma_calendar_destroy(c);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_datepicker(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    CHECK(aroma_datepicker_create(NULL, 0, 0, 300, 300, 2026, 1, 1) == NULL,
          "datepicker rejects NULL parent");
    AromaNode *dp = aroma_datepicker_create(root, 0, 0, 340, 340, 2026, 6, 15);
    CHECK(dp != NULL, "datepicker creates");
    int y = 0, m = 0, d = 0;
    aroma_datepicker_get_date(dp, &y, &m, &d);
    CHECK(y == 2026 && m == 6 && d == 15, "datepicker init date");
    aroma_datepicker_set_date(dp, 2025, 12, 25);
    aroma_datepicker_get_date(dp, &y, &m, &d);
    CHECK(y == 2025 && m == 12 && d == 25, "datepicker set/get");
    aroma_datepicker_set_date(NULL, 2026, 1, 1);
    aroma_datepicker_get_date(NULL, NULL, NULL, NULL);
    aroma_datepicker_set_on_change(dp, NULL, NULL);
    aroma_datepicker_set_font(dp, NULL);
    aroma_datepicker_set_title(dp, "Pick a day");
    aroma_datepicker_set_title(NULL, NULL);
    aroma_datepicker_set_title(dp, NULL);
    CHECK(aroma_datepicker_setup_events(dp, NULL, NULL),
          "datepicker events ok");
    aroma_datepicker_draw(dp, 9999);
    aroma_datepicker_destroy(NULL);
    aroma_datepicker_destroy(dp);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_popup_mode(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *c = aroma_calendar_create(root, 0, 0, 320, 56);
    AromaNode *dp = aroma_datepicker_create(root, 0, 64, 320, 56, 2026, 6,
                                            15);
    AromaNode *tp = aroma_timepicker_create(root, 0, 128, 320, 56, 14, 30);
    CHECK(!aroma_calendar_is_popup_open(c), "cal popup closed by default");
    CHECK(!aroma_calendar_is_popup_open(NULL), "cal popup NULL-safe");
    CHECK(!aroma_datepicker_is_popup_open(dp), "dp popup closed by default");
    CHECK(!aroma_datepicker_is_popup_open(NULL), "dp popup NULL-safe");
    CHECK(!aroma_timepicker_is_popup_open(tp), "tp popup closed by default");
    CHECK(!aroma_timepicker_is_popup_open(NULL), "tp popup NULL-safe");


    aroma_calendar_set_popup(c, true);
    aroma_datepicker_set_popup(dp, true);
    aroma_calendar_open_popup(c);
    CHECK(aroma_calendar_is_popup_open(c), "cal popup opens");

    aroma_datepicker_open_popup(dp);
    CHECK(!aroma_calendar_is_popup_open(c), "cal popup exclusive");
    CHECK(aroma_datepicker_is_popup_open(dp), "dp popup opens");
    aroma_timepicker_open_popup(tp);
    CHECK(!aroma_datepicker_is_popup_open(dp), "dp popup exclusive");
    CHECK(aroma_timepicker_is_popup_open(tp), "tp popup opens");

    AromaNode *hit = NULL;
    CHECK(aroma_timepicker_overlay_hit_test(5, 5, &hit) && hit == tp,
          "tp overlay claims taps while open");
    CHECK(!aroma_calendar_overlay_hit_test(5, 5, NULL),
          "closed cal claims nothing");
    aroma_timepicker_close_popup(tp);
    CHECK(!aroma_timepicker_is_popup_open(tp), "tp popup closes");
    CHECK(!aroma_timepicker_overlay_hit_test(5, 5, NULL),
          "closed tp claims nothing");

    aroma_calendar_open_popup(c);
    aroma_datepicker_open_popup(dp);
    aroma_timepicker_open_popup(tp);
    aroma_calendar_close_popups();
    aroma_datepicker_close_popups();
    aroma_timepicker_close_popups();
    CHECK(!aroma_calendar_is_popup_open(c), "cal close-all works");
    CHECK(!aroma_datepicker_is_popup_open(dp), "dp close-all works");
    CHECK(!aroma_timepicker_is_popup_open(tp), "tp close-all works");


    aroma_calendar_draw(c, 9999);
    aroma_datepicker_draw(dp, 9999);
    aroma_timepicker_draw(tp, 9999);
    aroma_calendar_set_popup(NULL, true);
    aroma_datepicker_set_popup(NULL, true);
    aroma_calendar_open_popup(NULL);
    aroma_datepicker_open_popup(NULL);
    aroma_timepicker_open_popup(NULL);
    aroma_calendar_close_popup(NULL);
    aroma_datepicker_close_popup(NULL);
    aroma_timepicker_close_popup(NULL);
    aroma_calendar_destroy(c);
    aroma_datepicker_destroy(dp);
    aroma_timepicker_destroy(tp);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_calendar_year_view(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *c = aroma_calendar_create(root, 0, 0, 320, 300);
    CHECK(c != NULL, "calendar creates for year view");
    CHECK(!aroma_calendar_get_year_view(c), "day view by default");
    CHECK(!aroma_calendar_get_year_view(NULL), "year view NULL-safe");
    aroma_calendar_set_year_view(c, true);
    CHECK(aroma_calendar_get_year_view(c), "year view enabled");
    aroma_calendar_draw(c, 9999);
    aroma_calendar_set_year_view(c, false);
    CHECK(!aroma_calendar_get_year_view(c), "back to day view");
    aroma_calendar_set_year_view(NULL, true);
    aroma_calendar_set_date(c, 2024, 2, 29);
    int y = 0, m = 0, d = 0;
    aroma_calendar_get_date(c, &y, &m, &d);
    CHECK(y == 2024 && m == 2 && d == 29, "leap day sticks");
    aroma_calendar_destroy(c);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_datepicker_confirm(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *dp = aroma_datepicker_create(root, 0, 0, 340, 360, 2026, 6,
                                            15);
    CHECK(dp != NULL, "datepicker creates for confirm tests");

    aroma_datepicker_set_date(dp, 2025, 12, 25);
    int y = 0, m = 0, d = 0;
    aroma_datepicker_get_date(dp, &y, &m, &d);
    CHECK(y == 2025 && m == 12 && d == 25, "datepicker confirm roundtrip");
    aroma_datepicker_confirm(dp);
    aroma_datepicker_get_date(dp, &y, &m, &d);
    CHECK(y == 2025 && m == 12 && d == 25, "confirm keeps date");
    aroma_datepicker_cancel(NULL);
    aroma_datepicker_confirm(NULL);
    aroma_datepicker_cancel(dp);
    aroma_datepicker_get_date(dp, &y, &m, &d);
    CHECK(y == 2025 && m == 12 && d == 25, "cancel keeps confirmed");
    aroma_datepicker_destroy(dp);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_datepicker_year_view(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *dp = aroma_datepicker_create(root, 0, 0, 340, 360, 2026, 6,
                                            15);
    CHECK(dp != NULL, "datepicker creates for year view");
    CHECK(!aroma_datepicker_get_year_view(dp), "day view by default");
    CHECK(!aroma_datepicker_get_year_view(NULL), "year view NULL-safe");
    aroma_datepicker_set_year_view(dp, true);
    CHECK(aroma_datepicker_get_year_view(dp), "year view enabled");
    aroma_datepicker_draw(dp, 9999);
    aroma_datepicker_set_year_view(dp, false);
    CHECK(!aroma_datepicker_get_year_view(dp), "back to day view");
    aroma_datepicker_set_year_view(NULL, true);
    aroma_datepicker_destroy(dp);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_timepicker(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    CHECK(aroma_timepicker_create(NULL, 0, 0, 300, 120, 12, 0) == NULL,
          "timepicker rejects NULL parent");
    AromaNode *tp = aroma_timepicker_create(root, 0, 0, 320, 120, 14, 30);
    CHECK(tp != NULL, "timepicker creates");
    int h = 0, mi = 0;
    aroma_timepicker_get_time(tp, &h, &mi);
    CHECK(h == 14 && mi == 30, "timepicker init time");
    aroma_timepicker_set_time(tp, 9, 45);
    aroma_timepicker_get_time(tp, &h, &mi);
    CHECK(h == 9 && mi == 45, "timepicker set/get");
    aroma_timepicker_set_time(tp, 99, 99);
    aroma_timepicker_get_time(tp, &h, &mi);
    CHECK(h == 9 && mi == 45, "timepicker ignores bad time");
    aroma_timepicker_set_24h(tp, true);
    aroma_timepicker_set_24h(NULL, true);
    aroma_timepicker_set_on_change(tp, NULL, NULL);
    aroma_timepicker_set_font(tp, NULL);
    aroma_timepicker_set_title(tp, "Pick a time");
    aroma_timepicker_set_title(NULL, NULL);
    aroma_timepicker_set_title(tp, NULL);
    aroma_timepicker_confirm(tp);
    aroma_timepicker_cancel(tp);
    aroma_timepicker_confirm(NULL);
    aroma_timepicker_cancel(NULL);
    CHECK(aroma_timepicker_setup_events(tp, NULL, NULL),
          "timepicker events ok");
    aroma_timepicker_draw(tp, 9999);
    aroma_timepicker_destroy(NULL);
    aroma_timepicker_destroy(tp);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_stepper(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    AromaNode *s = aroma_stepper_create_numeric(root, 0, 0, 220, 44, 0, 10,
                                                5, 1);
    CHECK(s != NULL, "stepper numeric creates");
    CHECK(aroma_stepper_get_value(s) == 5, "stepper init value");
    aroma_stepper_set_value(s, 8);
    CHECK(aroma_stepper_get_value(s) == 8, "stepper set value");
    aroma_stepper_set_value(s, 99);
    CHECK(aroma_stepper_get_value(s) == 10, "stepper clamps high");
    aroma_stepper_set_value(s, -99);
    CHECK(aroma_stepper_get_value(s) == 0, "stepper clamps low");
    CHECK(aroma_stepper_get_value(NULL) == 0, "stepper get NULL-safe");
    aroma_stepper_set_value(NULL, 5);
    aroma_stepper_set_wrap(s, false);
    aroma_stepper_set_wrap(s, true);
    aroma_stepper_set_wrap(NULL, true);
    CHECK(aroma_stepper_owns_touch(s), "numeric owns touch");
    CHECK(!aroma_stepper_owns_touch(NULL), "owns touch NULL-safe");
    static const char *labels[] = {"Cart", "Ship", "Pay"};
    AromaNode *w = aroma_stepper_create_steps(root, 0, 60, 340, 72, labels,
                                              3);
    CHECK(w != NULL, "stepper steps creates");
    CHECK(aroma_stepper_get_step_index(w) == 0, "steps init index 0");
    aroma_stepper_set_step_index(w, 2);
    CHECK(aroma_stepper_get_step_index(w) == 2, "steps set index");
    aroma_stepper_set_step_index(w, 9);
    CHECK(aroma_stepper_get_step_index(w) == 2, "steps ignores bad index");
    CHECK(aroma_stepper_get_step_index(NULL) == -1,
          "steps get NULL-safe");
    CHECK(!aroma_stepper_owns_touch(w), "steps does not own touch");
    aroma_stepper_set_on_change(s, NULL, NULL);
    aroma_stepper_set_font(s, NULL);
    CHECK(aroma_stepper_setup_events(s, NULL, NULL), "stepper events ok");
    aroma_stepper_draw(s, 9999);
    aroma_stepper_draw(w, 9999);
    aroma_stepper_destroy(NULL);
    aroma_stepper_destroy(s);
    aroma_stepper_destroy(w);
    __destroy_node(root);
    __node_system_destroy();
}

static void test_carousel(void)
{
    __node_system_init();
    AromaNode *root = make_root();
    CHECK(aroma_carousel_create(NULL, 0, 0, 300, 200) == NULL,
          "carousel rejects NULL parent");
    AromaNode *c = aroma_carousel_create(root, 0, 0, 340, 220);
    CHECK(c != NULL, "carousel creates");
    CHECK(aroma_carousel_get_count(c) == 0, "carousel starts empty");
    CHECK(aroma_carousel_get_page(c) == 0, "carousel page 0");
    AromaNode *p0 = aroma_carousel_add_page(c);
    AromaNode *p1 = aroma_carousel_add_page(c);
    AromaNode *p2 = aroma_carousel_add_page(c);
    CHECK(p0 && p1 && p2, "carousel pages created");
    CHECK(aroma_carousel_get_count(c) == 3, "carousel counts pages");
    aroma_carousel_set_page(c, 2);
    CHECK(aroma_carousel_get_page(c) == 2, "carousel set page");
    aroma_carousel_set_page(c, 99);
    CHECK(aroma_carousel_get_page(c) == 2, "carousel clamps high");
    aroma_carousel_next(c);
    CHECK(aroma_carousel_get_page(c) == 2, "carousel next at end stays");
    aroma_carousel_set_page(c, 0);
    aroma_carousel_next(c);
    CHECK(aroma_carousel_get_page(c) == 1, "carousel next advances");
    aroma_carousel_prev(c);
    CHECK(aroma_carousel_get_page(c) == 0, "carousel prev goes back");
    aroma_carousel_prev(c);
    CHECK(aroma_carousel_get_page(c) == 0, "carousel prev at 0 stays");
    CHECK(aroma_carousel_add_page(NULL) == NULL,
          "carousel add page NULL-safe");
    CHECK(aroma_carousel_get_count(NULL) == 0, "carousel count NULL-safe");
    CHECK(aroma_carousel_get_page(NULL) == -1, "carousel page NULL-safe");
    aroma_carousel_set_page(NULL, 1);
    aroma_carousel_next(NULL);
    aroma_carousel_prev(NULL);
    aroma_carousel_set_on_change(c, NULL, NULL);
    aroma_carousel_set_font(c, NULL);
    CHECK(aroma_carousel_setup_events(c, NULL, NULL),
          "carousel events ok");
    aroma_carousel_draw(c, 9999);
    aroma_carousel_destroy(NULL);
    aroma_carousel_destroy(c);
    __destroy_node(root);
    __node_system_destroy();
}

void run_pickers_tests(int *passed, int *failed)
{
    s_passed = 0;
    s_failed = 0;
    printf("=== Aroma Pickers Tests ===\n");
    test_date_math();
    test_calendar();
    test_calendar_year_view();
    test_popup_mode();
    test_datepicker();
    test_datepicker_confirm();
    test_datepicker_year_view();
    test_timepicker();
    test_stepper();
    test_carousel();
    printf("Aroma Pickers: %d passed, %d failed\n", s_passed, s_failed);
    *passed = s_passed;
    *failed = s_failed;
}
