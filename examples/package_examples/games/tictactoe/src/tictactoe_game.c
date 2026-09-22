/* Tic Tac Toe (com.aroma.game.tictactoe): you (X) vs the computer (O).
 *
 * Host-chrome plugin: board cells are buttons (click targets) with a label
 * child each for the X/O face (buttons have no public set-text API).
 */

#include "aroma.h"
#include "aroma_package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TTT_CELL 100
#define TTT_ORIGIN_X 80
#define TTT_ORIGIN_Y 140

#define COL_X 0xFF1A73E8
#define COL_O 0xFFF44336

static const int WINS[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
    {0, 4, 8}, {2, 4, 6},
};

static AromaFont *s_font;
static AromaNode *s_cells[9];
static AromaNode *s_faces[9];
static AromaNode *s_status_label;
static AromaNode *s_score_label;
static char s_board[9];
static bool s_over;
static int s_wins_you;
static int s_wins_cpu;
static int s_draws;
static bool s_built;

static void refresh_faces(void)
{
    for (int i = 0; i < 9; i++)
    {
        if (!s_faces[i])
            continue;
        char face[2] = {s_board[i] == ' ' ? '\0' : s_board[i], '\0'};
        aroma_label_set_text(s_faces[i], face);
        if (s_board[i] == 'X')
            aroma_label_set_color(s_faces[i], COL_X);
        else if (s_board[i] == 'O')
            aroma_label_set_color(s_faces[i], COL_O);
    }
}

static void refresh_status(const char *text)
{
    if (s_status_label)
        aroma_label_set_text(s_status_label, text);
    if (s_score_label)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "You %d  -  CPU %d  -  Draws %d",
                 s_wins_you, s_wins_cpu, s_draws);
        aroma_label_set_text(s_score_label, buf);
    }
    aroma_ui_request_redraw(NULL);
}

static char check_winner(void)
{
    for (int i = 0; i < 8; i++)
    {
        char a = s_board[WINS[i][0]];
        if (a != ' ' && a == s_board[WINS[i][1]] && a == s_board[WINS[i][2]])
            return a;
    }
    for (int i = 0; i < 9; i++)
    {
        if (s_board[i] == ' ')
            return ' ';
    }
    return 'D';
}

static void cpu_move(void)
{
    int empty[9];
    int n = 0;
    for (int i = 0; i < 9; i++)
    {
        if (s_board[i] == ' ')
            empty[n++] = i;
    }
    if (n == 0)
        return;
    /* Win if possible, block if needed, else random. */
    for (int k = 0; k < n; k++)
    {
        s_board[empty[k]] = 'O';
        if (check_winner() == 'O')
        {
            s_board[empty[k]] = ' ';
            s_board[empty[k]] = 'O';
            return;
        }
        s_board[empty[k]] = ' ';
    }
    for (int k = 0; k < n; k++)
    {
        s_board[empty[k]] = 'X';
        if (check_winner() == 'X')
        {
            s_board[empty[k]] = 'O';
            return;
        }
        s_board[empty[k]] = ' ';
    }
    s_board[empty[rand() % n]] = 'O';
}

static void finish_round(char result)
{
    s_over = true;
    if (result == 'X')
    {
        s_wins_you++;
        refresh_status("You win! Tap New Round.");
    }
    else if (result == 'O')
    {
        s_wins_cpu++;
        refresh_status("CPU wins. Tap New Round.");
    }
    else
    {
        s_draws++;
        refresh_status("Draw! Tap New Round.");
    }
    refresh_faces();
}

static bool on_cell_click(AromaNode *node, void *user_data)
{
    (void)node;
    int idx = (int)(intptr_t)user_data;
    if (s_over || s_board[idx] != ' ')
        return true;
    s_board[idx] = 'X';
    char r = check_winner();
    if (r != ' ')
    {
        finish_round(r);
        return true;
    }
    cpu_move();
    r = check_winner();
    if (r != ' ')
        finish_round(r);
    else
        refresh_status("Your turn (X).");
    refresh_faces();
    return true;
}

static void new_round(void)
{
    for (int i = 0; i < 9; i++)
        s_board[i] = ' ';
    s_over = false;
    refresh_faces();
    refresh_status("Your turn (X).");
}

static bool on_new_round_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    new_round();
    return true;
}

static bool on_reset_score_click(AromaNode *node, void *user_data)
{
    (void)node;
    (void)user_data;
    s_wins_you = s_wins_cpu = s_draws = 0;
    new_round();
    return true;
}

static bool ttt_init(const AromaPackageManifest *manifest,
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
    memset(s_cells, 0, sizeof(s_cells));
    memset(s_faces, 0, sizeof(s_faces));
    s_status_label = s_score_label = NULL;
    s_wins_you = s_wins_cpu = s_draws = 0;
    return true;
}

static bool ttt_build_ui(struct AromaNode *app_root)
{
    if (!app_root || s_built)
        return s_built;
    AromaNode *title = aroma_label_create(app_root, "Tic Tac Toe",
                                          80, 24, LABEL_STYLE_LABEL_LARGE);
    if (title && s_font)
        aroma_label_set_font(title, s_font);

    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            int idx = r * 3 + c;
            AromaNode *b = aroma_button_create(
                app_root, "",
                TTT_ORIGIN_X + c * (TTT_CELL + 10),
                TTT_ORIGIN_Y + r * (TTT_CELL + 10),
                TTT_CELL, TTT_CELL);
            if (!b)
                return false;
            aroma_button_set_on_click(b, on_cell_click, (void *)(intptr_t)idx);
            if (s_font)
                aroma_button_set_font(b, s_font);
            aroma_button_setup_events(b, aroma_ui_request_redraw, NULL);
            s_cells[idx] = b;
            /* Face label centered inside the button. */
            AromaNode *face = aroma_label_create(b, "", 38, 28,
                                                 LABEL_STYLE_LABEL_LARGE);
            if (face && s_font)
                aroma_label_set_font(face, s_font);
            s_faces[idx] = face;
            s_board[idx] = ' ';
        }
    }

    int panel_x = TTT_ORIGIN_X + 3 * (TTT_CELL + 10) + 60;
    s_status_label = aroma_label_create(app_root, "Your turn (X).",
                                        panel_x, 140, LABEL_STYLE_LABEL_MEDIUM);
    s_score_label = aroma_label_create(app_root, "You 0  -  CPU 0  -  Draws 0",
                                       panel_x, 175, LABEL_STYLE_LABEL_SMALL);
    if (s_font)
    {
        if (s_status_label)
            aroma_label_set_font(s_status_label, s_font);
        if (s_score_label)
            aroma_label_set_font(s_score_label, s_font);
    }

    AromaNode *nb = aroma_button_create(app_root, "New Round", panel_x, 230,
                                        170, 48);
    if (nb)
    {
        aroma_button_set_on_click(nb, on_new_round_click, NULL);
        if (s_font)
            aroma_button_set_font(nb, s_font);
        aroma_button_setup_events(nb, aroma_ui_request_redraw, NULL);
    }
    AromaNode *rb = aroma_button_create(app_root, "Reset Score", panel_x, 290,
                                        170, 48);
    if (rb)
    {
        aroma_button_set_on_click(rb, on_reset_score_click, NULL);
        if (s_font)
            aroma_button_set_font(rb, s_font);
        aroma_button_setup_events(rb, aroma_ui_request_redraw, NULL);
    }

    AromaNode *hint = aroma_label_create(app_root,
                                         "You are X - three in a row wins.",
                                         80, TTT_ORIGIN_Y + 3 * (TTT_CELL + 10) + 16,
                                         LABEL_STYLE_LABEL_SMALL);
    if (hint && s_font)
        aroma_label_set_font(hint, s_font);

    s_built = true;
    s_over = false;
    refresh_faces();
    refresh_status("Your turn (X).");
    return true;
}

static void ttt_destroy(void)
{
    s_built = false;
    memset(s_cells, 0, sizeof(s_cells));
    memset(s_faces, 0, sizeof(s_faces));
    s_status_label = s_score_label = NULL;
    s_font = NULL;
}

static const AromaPackageHooks s_ttt_hooks = {
    .init = ttt_init,
    .build_ui = ttt_build_ui,
    .show = NULL,
    .hide = NULL,
    .update = NULL,
    .destroy = ttt_destroy,
};

const AromaPackageHooks *aroma_package_entry(void)
{
    return &s_ttt_hooks;
}
