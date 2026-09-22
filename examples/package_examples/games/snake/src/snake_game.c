/* Snake (com.aroma.game.snake): classic arcade snake for the car host.
 *
 * Host-chrome plugin: the package manager owns the window chrome and the
 * close button. This plugin only builds game UI into app_root and drives
 * the game clock from update().
 *
 * Board cells are plain buttons recolored per tick (buttons have no public
 * set-text API, so color is the display channel). Controls are normal
 * buttons wired with on_click + setup_events.
 */

/* Clock needs POSIX.1b: define before any system header. */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 199309L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif

#include "aroma.h"
#include "aroma_package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SNAKE_COLS 12
#define SNAKE_ROWS 12
#define SNAKE_CELLS (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_MAX_LEN SNAKE_CELLS
#define SNAKE_CELL 30
#define SNAKE_ORIGIN_X 40
#define SNAKE_ORIGIN_Y 130
#define SNAKE_TICK_MS 220

#define COL_EMPTY 0xFF2B2B2B
#define COL_HEAD 0xFF4CAF50
#define COL_BODY 0xFF2E7D32
#define COL_FOOD 0xFFF44336
#define COL_TEXT 0xFFFFFFFF

/* dir: 0=up 1=right 2=down 3=left */
static AromaFont *s_font;
static AromaNode *s_cells[SNAKE_CELLS];
static AromaNode *s_score_label;
static AromaNode *s_best_label;
static AromaNode *s_status_label;
static AromaNode *s_start_btn;

static int s_snake[SNAKE_MAX_LEN];
static int s_len;
static int s_dir;
static int s_pending_dir;
static int s_food;
static int s_score;
static int s_best;
static bool s_running;
static bool s_game_over;
static bool s_built;
static long s_last_tick_ms;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static int row_of(int idx) { return idx / SNAKE_COLS; }
static int col_of(int idx) { return idx % SNAKE_COLS; }

static bool is_snake_cell(int idx)
{
    for (int i = 0; i < s_len; i++)
    {
        if (s_snake[i] == idx)
            return true;
    }
    return false;
}

static void spawn_food(void)
{
    if (s_len >= SNAKE_CELLS)
    {
        s_food = -1;
        return;
    }
    for (int tries = 0; tries < 200; tries++)
    {
        int idx = rand() % SNAKE_CELLS;
        if (!is_snake_cell(idx))
        {
            s_food = idx;
            return;
        }
    }
    for (int i = 0; i < SNAKE_CELLS; i++)
    {
        if (!is_snake_cell(i))
        {
            s_food = i;
            return;
        }
    }
    s_food = -1;
}

static void repaint(void)
{
    for (int i = 0; i < SNAKE_CELLS; i++)
    {
        if (!s_cells[i])
            continue;
        uint32_t c = COL_EMPTY;
        if (i == s_food)
            c = COL_FOOD;
        else if (s_len > 0 && i == s_snake[0])
            c = COL_HEAD;
        else if (is_snake_cell(i))
            c = COL_BODY;
        aroma_button_set_colors(s_cells[i], c, c, c, COL_TEXT);
    }
    if (s_score_label)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Score: %d", s_score);
        aroma_label_set_text(s_score_label, buf);
    }
    if (s_best_label)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Best: %d", s_best);
        aroma_label_set_text(s_best_label, buf);
    }
    if (s_status_label)
    {
        if (s_game_over)
            aroma_label_set_text(s_status_label, "Game over - press Restart");
        else if (s_running)
            aroma_label_set_text(s_status_label, "Playing... eat the red pellet!");
        else
            aroma_label_set_text(s_status_label, "Paused - press Start");
    }
    aroma_ui_request_redraw(NULL);
}

static void reset_game(void)
{
    s_len = 3;
    int mid = (SNAKE_ROWS / 2) * SNAKE_COLS + (SNAKE_COLS / 2);
    s_snake[0] = mid;
    s_snake[1] = mid - 1;
    s_snake[2] = mid - 2;
    s_dir = 1;
    s_pending_dir = 1;
    s_score = 0;
    s_game_over = false;
    s_last_tick_ms = now_ms();
    spawn_food();
    repaint();
}

static void step_game(void)
{
    if (!s_running || s_game_over)
        return;
    /* Reject 180-degree turns. */
    if (!((s_pending_dir == 0 && s_dir == 2) ||
          (s_pending_dir == 2 && s_dir == 0) ||
          (s_pending_dir == 1 && s_dir == 3) ||
          (s_pending_dir == 3 && s_dir == 1)))
        s_dir = s_pending_dir;
    int head = s_snake[0];
    int r = row_of(head);
    int c = col_of(head);
    if (s_dir == 0)
        r--;
    else if (s_dir == 1)
        c++;
    else if (s_dir == 2)
        r++;
    else
        c--;
    if (r < 0 || r >= SNAKE_ROWS || c < 0 || c >= SNAKE_COLS)
    {
        s_game_over = true;
        s_running = false;
        repaint();
        return;
    }
    int nh = r * SNAKE_COLS + c;
    bool eats = (nh == s_food);
    /* Self collision: the tail moves away unless we grow, so ignore it. */
    int check_len = eats ? s_len : s_len - 1;
    for (int i = 0; i < check_len; i++)
    {
        if (s_snake[i] == nh)
        {
            s_game_over = true;
            s_running = false;
            repaint();
            return;
        }
    }
    if (eats)
    {
        if (s_len < SNAKE_MAX_LEN)
        {
            for (int i = s_len; i > 0; i--)
                s_snake[i] = s_snake[i - 1];
            s_len++;
        }
        s_score += 10;
        if (s_score > s_best)
            s_best = s_score;
        spawn_food();
    }
    else
    {
        for (int i = s_len - 1; i > 0; i--)
            s_snake[i] = s_snake[i - 1];
    }
    s_snake[0] = nh;
    if (s_food < 0)
    {
        /* Board cleared: you win. */
        s_game_over = true;
        s_running = false;
    }
    repaint();
}

static bool on_dir_click(AromaNode *node, void *user_data)
{
    (void)node;
    int dir = (int)(intptr_t)user_data;
    s_pending_dir = dir;
    if (!s_running && !s_game_over)
    {
        s_running = true;
        s_last_tick_ms = now_ms();
        repaint();
    }
    return true;
}

static bool on_start_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    if (s_game_over)
    {
        reset_game();
        s_running = true;
    }
    else
    {
        s_running = !s_running;
        s_last_tick_ms = now_ms();
    }
    repaint();
    return true;
}

static bool on_restart_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    reset_game();
    s_running = true;
    repaint();
    return true;
}

static AromaNode *make_button(AromaNode *parent, const char *text,
                              int x, int y, int w, int h,
                              bool (*cb)(AromaNode *, void *), void *ud)
{
    AromaNode *b = aroma_button_create(parent, text, x, y, w, h);
    if (!b)
        return NULL;
    if (cb)
        aroma_button_set_on_click(b, cb, ud);
    if (s_font)
        aroma_button_set_font(b, s_font);
    aroma_button_setup_events(b, aroma_ui_request_redraw, NULL);
    return b;
}

static bool snake_init(const AromaPackageManifest *manifest,
                       const char *install_dir,
                       const AromaPackageHost *host,
                       struct AromaNode *app_root)
{
    (void)manifest;
    (void)install_dir;
    (void)app_root;
    s_font = host ? host->ui_font : NULL;
    srand((unsigned)time(NULL) ^ (unsigned)now_ms());
    s_best = 0;
    s_built = false;
    memset(s_cells, 0, sizeof(s_cells));
    s_score_label = s_best_label = s_status_label = s_start_btn = NULL;
    return true;
}

static bool snake_build_ui(struct AromaNode *app_root)
{
    if (!app_root || s_built)
        return s_built;
    AromaNode *title = aroma_label_create(app_root, "Snake",
                                          40, 24, LABEL_STYLE_LABEL_LARGE);
    if (title && s_font)
        aroma_label_set_font(title, s_font);

    for (int r = 0; r < SNAKE_ROWS; r++)
    {
        for (int c = 0; c < SNAKE_COLS; c++)
        {
            int idx = r * SNAKE_COLS + c;
            AromaNode *cell = aroma_button_create(
                app_root, "",
                SNAKE_ORIGIN_X + c * SNAKE_CELL,
                SNAKE_ORIGIN_Y + r * SNAKE_CELL,
                SNAKE_CELL - 2, SNAKE_CELL - 2);
            if (!cell)
            {
                return false;
            }
            aroma_button_set_colors(cell, COL_EMPTY, COL_EMPTY, COL_EMPTY,
                                    COL_TEXT);
            s_cells[idx] = cell;
        }
    }

    int panel_x = SNAKE_ORIGIN_X + SNAKE_COLS * SNAKE_CELL + 40;
    s_score_label = aroma_label_create(app_root, "Score: 0", panel_x, 130,
                                       LABEL_STYLE_LABEL_LARGE);
    s_best_label = aroma_label_create(app_root, "Best: 0", panel_x, 165,
                                      LABEL_STYLE_LABEL_MEDIUM);
    s_status_label = aroma_label_create(app_root, "Press Start", panel_x, 200,
                                        LABEL_STYLE_LABEL_SMALL);
    if (s_font)
    {
        if (s_score_label)
            aroma_label_set_font(s_score_label, s_font);
        if (s_best_label)
            aroma_label_set_font(s_best_label, s_font);
        if (s_status_label)
            aroma_label_set_font(s_status_label, s_font);
    }

    /* D-pad. */
    int dx = panel_x + 60;
    int dy = 260;
    make_button(app_root, "Up", dx, dy, 90, 44, on_dir_click,
                (void *)(intptr_t)0);
    make_button(app_root, "Left", dx - 95, dy + 50, 90, 44, on_dir_click,
                (void *)(intptr_t)3);
    make_button(app_root, "Right", dx + 95, dy + 50, 90, 44, on_dir_click,
                (void *)(intptr_t)1);
    make_button(app_root, "Down", dx, dy + 50, 90, 44, on_dir_click,
                (void *)(intptr_t)2);

    s_start_btn = make_button(app_root, "Start", panel_x, dy + 120, 130, 46,
                              on_start_click, NULL);
    make_button(app_root, "Restart", panel_x + 140, dy + 120, 130, 46,
                on_restart_click, NULL);

    AromaNode *hint = aroma_label_create(
        app_root, "Arrows steer - red pellet = +10", 40,
        SNAKE_ORIGIN_Y + SNAKE_ROWS * SNAKE_CELL + 12,
        LABEL_STYLE_LABEL_SMALL);
    if (hint && s_font)
        aroma_label_set_font(hint, s_font);

    s_built = true;
    reset_game();
    s_running = false;
    repaint();
    return true;
}

static void snake_update(struct AromaNode *app_root)
{
    (void)app_root;
    if (!s_built || !s_running || s_game_over)
        return;
    long now = now_ms();
    if (now - s_last_tick_ms >= SNAKE_TICK_MS)
    {
        s_last_tick_ms = now;
        step_game();
    }
}

static void snake_destroy(void)
{
    s_built = false;
    memset(s_cells, 0, sizeof(s_cells));
    s_score_label = s_best_label = s_status_label = s_start_btn = NULL;
    s_font = NULL;
}

static const AromaPackageHooks s_snake_hooks = {
    .init = snake_init,
    .build_ui = snake_build_ui,
    .show = NULL,
    .hide = NULL,
    .update = snake_update,
    .destroy = snake_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_snake_hooks;
}
