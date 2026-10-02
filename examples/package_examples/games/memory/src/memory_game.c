






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

#define MEM_ROWS 4
#define MEM_COLS 4
#define MEM_CELLS (MEM_ROWS * MEM_COLS)
#define MEM_CELL 76
#define MEM_ORIGIN_X 60
#define MEM_ORIGIN_Y 130
#define MEM_FLIP_BACK_MS 750

#define COL_MATCHED 0xFF2E7D32

static const char *FACES[8] = {"A", "B", "C", "D", "E", "F", "G", "H"};

static AromaFont *s_font;
static AromaNode *s_tiles[MEM_CELLS];
static AromaNode *s_labels[MEM_CELLS];
static AromaNode *s_status_label;
static AromaNode *s_moves_label;
static int s_values[MEM_CELLS];
static bool s_shown[MEM_CELLS];
static bool s_matched[MEM_CELLS];
static int s_first;
static int s_second;
static long s_lock_until_ms;
static int s_moves;
static int s_pairs;
static bool s_won;
static bool s_built;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)(ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}

static void refresh_tile(int i)
{
    if (!s_labels[i])
        return;
    if (s_matched[i])
    {
        aroma_label_set_text(s_labels[i], FACES[s_values[i]]);
        aroma_label_set_color(s_labels[i], COL_MATCHED);
    }
    else if (s_shown[i])
    {
        aroma_label_set_text(s_labels[i], FACES[s_values[i]]);
        aroma_label_set_color(s_labels[i], 0xFFFFFFFF);
    }
    else
    {
        aroma_label_set_text(s_labels[i], "?");
        aroma_label_set_color(s_labels[i], 0xFF9E9E9E);
    }
}

static void refresh_all(void)
{
    for (int i = 0; i < MEM_CELLS; i++)
        refresh_tile(i);
    if (s_moves_label)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "Moves: %d   Pairs: %d/8", s_moves, s_pairs);
        aroma_label_set_text(s_moves_label, buf);
    }
    if (s_status_label)
    {
        if (s_won)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "You win in %d moves! Tap Restart.", s_moves);
            aroma_label_set_text(s_status_label, buf);
        }
        else if (s_lock_until_ms > now_ms())
            aroma_label_set_text(s_status_label, "No match - watch closely...");
        else if (s_first >= 0)
            aroma_label_set_text(s_status_label, "Pick a second tile...");
        else
            aroma_label_set_text(s_status_label, "Find all 8 pairs!");
    }
    aroma_ui_request_redraw(NULL);
}

static void shuffle_deal(void)
{
    int deck[MEM_CELLS];
    for (int i = 0; i < 8; i++)
    {
        deck[2 * i] = i;
        deck[2 * i + 1] = i;
    }
    for (int i = MEM_CELLS - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        int t = deck[i];
        deck[i] = deck[j];
        deck[j] = t;
    }
    for (int i = 0; i < MEM_CELLS; i++)
    {
        s_values[i] = deck[i];
        s_shown[i] = false;
        s_matched[i] = false;
    }
    s_first = -1;
    s_second = -1;
    s_lock_until_ms = 0;
    s_moves = 0;
    s_pairs = 0;
    s_won = false;
    refresh_all();
}

static bool on_tile_click(AromaNode *node, void *user_data)
{
    (void)node;
    int idx = (int)(intptr_t)user_data;
    if (s_won || s_matched[idx] || s_shown[idx])
        return true;
    if (s_lock_until_ms > now_ms())
        return true;
    s_shown[idx] = true;
    if (s_first < 0)
    {
        s_first = idx;
    }
    else
    {
        s_second = idx;
        s_moves++;
        if (s_values[s_first] == s_values[s_second])
        {
            s_matched[s_first] = true;
            s_matched[s_second] = true;
            s_first = s_second = -1;
            s_pairs++;
            if (s_pairs >= 8)
                s_won = true;
        }
        else
        {

            s_lock_until_ms = now_ms() + MEM_FLIP_BACK_MS;
        }
    }
    refresh_all();
    return true;
}

static bool on_restart_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    shuffle_deal();
    return true;
}

static bool mem_init(const AromaPackageManifest *manifest,
                     const char *install_dir,
                     const AromaPackageHost *host,
                     struct AromaNode *app_root)
{
    (void)manifest;
    (void)install_dir;
    (void)app_root;
    s_font = host ? host->ui_font : NULL;
    srand((unsigned)time(NULL));
    s_built = false;
    memset(s_tiles, 0, sizeof(s_tiles));
    memset(s_labels, 0, sizeof(s_labels));
    s_status_label = s_moves_label = NULL;
    return true;
}

static bool mem_build_ui(struct AromaNode *app_root)
{
    if (!app_root || s_built)
        return s_built;
    AromaNode *title = aroma_label_create(app_root, "Memory Match",
                                          60, 24, LABEL_STYLE_LABEL_LARGE);
    if (title && s_font)
        aroma_label_set_font(title, s_font);

    for (int r = 0; r < MEM_ROWS; r++)
    {
        for (int c = 0; c < MEM_COLS; c++)
        {
            int idx = r * MEM_COLS + c;
            AromaNode *b = aroma_button_create(
                app_root, "",
                MEM_ORIGIN_X + c * (MEM_CELL + 10),
                MEM_ORIGIN_Y + r * (MEM_CELL + 10),
                MEM_CELL, MEM_CELL);
            if (!b)
                return false;
            aroma_button_set_on_click(b, on_tile_click, (void *)(intptr_t)idx);
            if (s_font)
                aroma_button_set_font(b, s_font);
            aroma_button_setup_events(b, aroma_ui_request_redraw, NULL);
            s_tiles[idx] = b;
            AromaNode *lab = aroma_label_create(b, "?", 28, 22,
                                                LABEL_STYLE_LABEL_LARGE);
            if (lab && s_font)
                aroma_label_set_font(lab, s_font);
            s_labels[idx] = lab;
        }
    }

    int panel_x = MEM_ORIGIN_X + MEM_COLS * (MEM_CELL + 10) + 50;
    s_moves_label = aroma_label_create(app_root, "Moves: 0   Pairs: 0/8",
                                       panel_x, 130, LABEL_STYLE_LABEL_LARGE);
    s_status_label = aroma_label_create(app_root, "Find all 8 pairs!",
                                        panel_x, 165, LABEL_STYLE_LABEL_SMALL);
    if (s_font)
    {
        if (s_moves_label)
            aroma_label_set_font(s_moves_label, s_font);
        if (s_status_label)
            aroma_label_set_font(s_status_label, s_font);
    }
    AromaNode *rb = aroma_button_create(app_root, "Restart", panel_x, 220,
                                        150, 48);
    if (rb)
    {
        aroma_button_set_on_click(rb, on_restart_click, NULL);
        if (s_font)
            aroma_button_set_font(rb, s_font);
        aroma_button_setup_events(rb, aroma_ui_request_redraw, NULL);
    }
    AromaNode *hint = aroma_label_create(app_root,
                                         "Flip two tiles - match all pairs.",
                                         60, MEM_ORIGIN_Y + MEM_ROWS * (MEM_CELL + 10) + 14,
                                         LABEL_STYLE_LABEL_SMALL);
    if (hint && s_font)
        aroma_label_set_font(hint, s_font);

    s_built = true;
    shuffle_deal();
    return true;
}

static void mem_update(struct AromaNode *app_root)
{
    (void)app_root;
    if (!s_built || s_lock_until_ms == 0)
        return;
    if (now_ms() >= s_lock_until_ms)
    {
        if (s_first >= 0)
            s_shown[s_first] = false;
        if (s_second >= 0)
            s_shown[s_second] = false;
        s_first = s_second = -1;
        s_lock_until_ms = 0;
        refresh_all();
    }
    else
    {

        refresh_all();
    }
}

static void mem_destroy(void)
{
    s_built = false;
    memset(s_tiles, 0, sizeof(s_tiles));
    memset(s_labels, 0, sizeof(s_labels));
    s_status_label = s_moves_label = NULL;
    s_font = NULL;
}

static const AromaPackageHooks s_mem_hooks = {
    .init = mem_init,
    .build_ui = mem_build_ui,
    .show = NULL,
    .hide = NULL,
    .update = mem_update,
    .destroy = mem_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_mem_hooks;
}
