#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <aroma.h>
#include <aroma_animation.h>
#include <aroma_native_utils.h>
#include <aroma_ui.h>
#include <widgets/aroma_textbox.h>
#include <widgets/aroma_button.h>
#include <aroma_font.h>
#include <aroma_ubuntu_font.h>
#include <aroma_logger.h>

#ifdef ANDROID
#include <aroma_android.h>
#endif

static int dp(int dp_val) {
#ifdef ANDROID
    return aroma_android_dp_to_px(dp_val);
#else
    return dp_val;
#endif
}

static int sp(int sp_val) {
#ifdef ANDROID
    return aroma_android_sp_to_px(sp_val);
#else
    return sp_val;
#endif
}

#define WIN_W_DP         1000
#define WIN_H_DP          600
#define TOPBAR_H_DP        56
#define PADDING_DP         20
#define GAP_DP             12
#define CARD_RADIUS_DP      8

#define SETUP_CARD_W_DP   540
#define SETUP_CARD_H_DP   480
#define SETUP_TB_W_DP     320
#define SETUP_TB_H_DP      52
#define SETUP_STEP_BTN_W_DP 48
#define SETUP_STEP_BTN_H_DP 48
#define SETUP_BTN_W_DP    220
#define SETUP_BTN_H_DP     56

#define BUZZ_BTN_W_DP     180
#define Q_CARD_H_DP       130

#define ANS_CARD_H_DP      88
#define VOTE_BTN_W_DP     220
#define VOTE_BTN_H_DP      64

#define FONT_TITLE_SP      36
#define FONT_LARGE_SP      28
#define FONT_MED_SP        20
#define FONT_SMALL_SP      14

typedef enum {
    STATE_SETUP,
    STATE_BUZZ,
    STATE_ANSWER_WAIT,
    STATE_ANSWER_VOTE,
    STATE_END
} GameState;

typedef struct  __attribute__((packed, aligned(1))) {
    char q[256];
    char a[128];
    char cat[64];
} Question;

static Question question_bank[] = {
    {"What is the most abundant gas in the Earth's atmosphere?",  "Nitrogen",                "Science"},
    {"What is the chemical symbol for gold?",                     "Au",                      "Science"},
    {"What planet is known as the Red Planet?",                   "Mars",                    "Science"},
    {"What is the hardest natural substance on Earth?",           "Diamond",                 "Science"},
    {"How many bones are in the adult human body?",               "206",                     "Science"},
    {"What part of the plant conducts photosynthesis?",           "Leaf",                    "Science"},
    {"In what year did the Titanic sink?",                        "1912",                    "History"},
    {"Who was the first President of the United States?",         "George Washington",       "History"},
    {"Which empire was ruled by Julius Caesar?",                  "Roman Empire",            "History"},
    {"Who painted the Mona Lisa?",                                "Leonardo da Vinci",       "History"},
    {"In what year did World War II end?",                        "1945",                    "History"},
    {"Who was the first person to walk on the moon?",             "Neil Armstrong",          "History"},
    {"What is the capital of Japan?",                             "Tokyo",                   "Geography"},
    {"What is the longest river in the world?",                   "The Nile",                "Geography"},
    {"In what country is the Taj Mahal located?",                 "India",                   "Geography"},
    {"What is the smallest continent by land area?",              "Australia",               "Geography"},
    {"Which ocean is the largest?",                               "Pacific Ocean",           "Geography"},
    {"What country has the most natural lakes?",                  "Canada",                  "Geography"},
    {"Who is known as the King of Pop?",                          "Michael Jackson",         "Pop Culture"},
    {"What is the name of the fictional city where Batman lives?","Gotham City",             "Pop Culture"},
    {"Which movie features the quote 'May the Force be with you'?","Star Wars",              "Pop Culture"},
    {"Who is the author of the Harry Potter series?",             "J.K. Rowling",            "Pop Culture"},
    {"What is the highest-grossing film of all time?",            "Avatar",                  "Pop Culture"},
    {"Which singer is known as the 'Material Girl'?",             "Madonna",                 "Pop Culture"},
    {"What is the main ingredient in guacamole?",                 "Avocado",                 "Food & Drink"},
    {"Which country is the origin of the cocktail Mojito?",       "Cuba",                    "Food & Drink"},
    {"What type of pasta translates to 'little worms'?",          "Vermicelli",              "Food & Drink"},
    {"What is the main ingredient in hummus?",                    "Chickpeas",               "Food & Drink"},
    {"What is sushi traditionally wrapped in?",                   "Seaweed (Nori)",          "Food & Drink"},
    {"What kind of alcohol is used in a Margarita?",              "Tequila",                 "Food & Drink"},
    {"In which sport is the Lombardi Trophy awarded?",            "American Football (NFL)", "Sports"},
    {"How many players are on a standard soccer team on the field?","11",                    "Sports"},
    {"Which country won the first FIFA World Cup in 1930?",       "Uruguay",                 "Sports"},
    {"In tennis, what is a score of zero called?",                "Love",                    "Sports"},
    {"What is the national sport of Canada?",                     "Ice Hockey / Lacrosse",   "Sports"},
    {"Who wrote the song 'Bohemian Rhapsody'?",                   "Queen",                   "Music"},
    {"How many keys are on a standard piano?",                    "88",                      "Music"},
    {"What brass instrument has a slide?",                        "Trombone",                "Music"},
    {"Who is the lead singer of the band U2?",                    "Bono",                    "Music"},
    {"What decade did rock and roll begin?",                      "1950s",                   "Music"},
    {"What is the name of the Hobbit played by Elijah Wood?",     "Frodo Baggins",           "Movies & TV"},
    {"In 'The Matrix', what pill does Neo take?",                 "The Red Pill",            "Movies & TV"},
    {"Who directed 'Jurassic Park'?",                             "Steven Spielberg",        "Movies & TV"},
    {"What is the name of the coffee shop in 'Friends'?",         "Central Perk",            "Movies & TV"},
    {"Which actor played Forrest Gump?",                          "Tom Hanks",               "Movies & TV"},
    {"What is the square root of 144?",                           "12",                      "Math"},
    {"What is the value of Pi to two decimal places?",            "3.14",                    "Math"},
    {"What is 15 percent of 200?",                                "30",                      "Math"},
    {"How many degrees are in a full circle?",                    "360",                     "Math"},
    {"What is the prime number closest to 100?",                  "97",                      "Math"},
    {"What is a group of flamingos called?",                      "A flamboyance",           "Random"},
    {"How many hearts does an octopus have?",                     "3",                       "Random"},
    {"What color is the blood of a horseshoe crab?",              "Blue",                    "Random"},
    {"What do you call a baby kangaroo?",                         "A joey",                  "Random"},
    {"What is the only mammal capable of true flight?",           "Bat",                     "Random"},
    {"What is the rarest blood type?",                            "AB Negative",             "Random"},
    {"What is the name of the longest bone in the human body?",   "Femur",                   "Random"},
};
static const int TOTAL_QUESTIONS = (int)(sizeof(question_bank) / sizeof(Question));

static const uint32_t PLAYER_COLORS[4] = {
    0xFF7F77DD,
    0xFF1D9E75,
    0xFFD85A30,
    0xFFD4537E,
};

static const char *DEFAULT_NAMES[4] = {
    "Player 1", "Player 2", "Player 3", "Player 4"
};

typedef struct  __attribute__((packed, aligned(1))) {
    char     name[64];
    uint32_t color;
    int      score;
} Player;

typedef struct  __attribute__((packed, aligned(1))) {
    AromaWindow *window;
    AromaTheme   theme;
    AromaFont   *font_title;
    AromaFont   *font_large;
    AromaFont   *font_med;
    AromaFont   *font_small;
    int win_w, win_h, topbar_h;

    int num_players;
    int questions_per_game;
    int points_to_win;

    GameState state;
    Player    players[4];
    int       active_questions[60];
    int       qs_played;
    int       buzzing_player;

    AromaNode *setup_layer;
    AromaNode *setup_card;
    AromaNode *tb_names[4];
    AromaNode *tb_name_rows[4];
    AromaNode *lbl_num_players;
    AromaNode *lbl_questions;
    AromaNode *lbl_points;

    AromaNode *topbar_layer;
    AromaNode *score_labels[4];
    AromaNode *q_count_label;

    AromaNode *game_layer;
    AromaNode *q_card;
    AromaNode *q_text;
    AromaNode *cat_text;
    AromaNode *buzz_left[2];
    AromaNode *buzz_right[2];
    AromaNode *skip_btn;

    AromaNode *answer_layer;
    AromaNode *buzzed_info_card;
    AromaNode *buzzed_info_text;
    AromaNode *ans_card;
    AromaNode *ans_text;
    AromaNode *btn_reveal;
    AromaNode *btn_correct;
    AromaNode *btn_wrong;

    AromaNode *end_layer;
    AromaNode *end_card;
    AromaNode *winner_text;
    AromaNode *score_summary_labels[4];

    int current_animation_id;
} AppState;

static AppState app;

void transition_to(GameState next);
void check_winner_or_next(void);
void update_scoreboard(void);
static void refresh_setup_ui(void);
static void rebuild_buzz_buttons(void);

static void start_animation(AromaNode *node, int anim_type, float from, float to, int dur_ms, int easing) {
    if (node == NULL) return;
    
    if (app.current_animation_id > 0) {
     //   aroma_animation_cancel(app.current_animation_id);
    }
    
    int anim_id = aroma_animation_start(node, anim_type, from, to, dur_ms);
    if (anim_id > 0) {
        aroma_animation_set_easing(anim_id, easing);
        app.current_animation_id = anim_id;
    }
}

static void slide_in_card(AromaNode *node, int from_y_offset, int dur_ms) {
    if (node == NULL) return;
    start_animation(node, AROMA_ANIM_SLIDE_Y, (float)from_y_offset, 0.0f, dur_ms, AROMA_EASE_IN_QUAD);
}

static void slide_in_from_left(AromaNode *node, int dur_ms) {
    if (node == NULL) return;
    start_animation(node, AROMA_ANIM_SLIDE_X, -300.0f, 0.0f, dur_ms, AROMA_EASE_IN_QUAD);
}

static void slide_in_from_right(AromaNode *node, int dur_ms) {
    if (node == NULL) return;
    start_animation(node, AROMA_ANIM_SLIDE_X, 300.0f, 0.0f, dur_ms, AROMA_EASE_IN_QUAD);
}

static void pop_in(AromaNode *node, int dur_ms) {
    if (node == NULL) return;
    start_animation(node, AROMA_ANIM_SCALE_X, 0.5f, 1.0f, dur_ms, AROMA_EASE_OUT_ELASTIC);
    start_animation(node, AROMA_ANIM_SCALE_Y, 0.5f, 1.0f, dur_ms, AROMA_EASE_OUT_ELASTIC);
}

static void shuffle_questions(void) {
    for (int i = 0; i < TOTAL_QUESTIONS; i++)
        app.active_questions[i] = i;
    for (int i = TOTAL_QUESTIONS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = app.active_questions[i];
        app.active_questions[i] = app.active_questions[j];
        app.active_questions[j] = tmp;
    }
    app.qs_played = 0;
}

void update_scoreboard(void) {
    for (int i = 0; i < 4; i++) {
        char buf[128];
        if (i < app.num_players)
            snprintf(buf, sizeof(buf), "%s: %d", app.players[i].name, app.players[i].score);
        else
            buf[0] = '\0';
        aroma_label_set_text(app.score_labels[i], buf);
        aroma_node_set_hidden(app.score_labels[i], i >= app.num_players);
    }
    char qb[64];
    snprintf(qb, sizeof(qb), "Q %d / %d  \xE2\x80\x94  First to %d",
             app.qs_played + 1, app.questions_per_game, app.points_to_win);
    aroma_label_set_text(app.q_count_label, qb);
}

static void clamp_setup(void) {
    if (app.num_players       < 2)  app.num_players       = 2;
    if (app.num_players       > 4)  app.num_players       = 4;
    if (app.questions_per_game < 5)  app.questions_per_game = 5;
    if (app.questions_per_game > 30) app.questions_per_game = 30;
    if (app.points_to_win      < 3)  app.points_to_win      = 3;
    if (app.points_to_win      > 15) app.points_to_win      = 15;
}

static void refresh_setup_ui(void) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", app.num_players);
    aroma_label_set_text(app.lbl_num_players, buf);
    snprintf(buf, sizeof(buf), "%d", app.questions_per_game);
    aroma_label_set_text(app.lbl_questions, buf);
    snprintf(buf, sizeof(buf), "%d", app.points_to_win);
    aroma_label_set_text(app.lbl_points, buf);

    for (int i = 0; i < 4; i++)
        aroma_node_set_hidden(app.tb_name_rows[i], i >= app.num_players);
}

static bool on_players_inc(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.num_players++;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_players_dec(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.num_players--;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_questions_inc(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.questions_per_game += 5;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_questions_dec(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.questions_per_game -= 5;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_points_inc(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.points_to_win++;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_points_dec(AromaNode *n, void *d) {
    (void)n; (void)d;
    app.points_to_win--;
    clamp_setup();
    refresh_setup_ui();
    return true;
}

static bool on_start_game(AromaNode *n, void *d) {
    (void)n; (void)d;
    for (int i = 0; i < app.num_players; i++) {
        const char *t = aroma_ui_textbox_get_text(app.tb_names[i]);
        if (t && strlen(t) > 0)
            strncpy(app.players[i].name, t, 63);
        else
            strncpy(app.players[i].name, DEFAULT_NAMES[i], 63);
        app.players[i].name[63] = '\0';
        app.players[i].score = 0;
    }
    shuffle_questions();
    update_scoreboard();
    rebuild_buzz_buttons();
    transition_to(STATE_BUZZ);
    return true;
}

static bool on_buzz(AromaNode *n, void *d) {
    (void)n;
    if (app.state != STATE_BUZZ) return true;
    app.buzzing_player = (int)(intptr_t)d;
    transition_to(STATE_ANSWER_WAIT);
    return true;
}

static bool on_skip(AromaNode *n, void *d) {
    (void)n; (void)d;
    if (app.state != STATE_BUZZ) return true;
    app.qs_played++;
    check_winner_or_next();
    return true;
}

static bool on_reveal(AromaNode *n, void *d) {
    (void)n; (void)d;
    if (app.state != STATE_ANSWER_WAIT) return true;
    transition_to(STATE_ANSWER_VOTE);
    return true;
}

static bool on_vote_correct(AromaNode *n, void *d) {
    (void)n; (void)d;
    if (app.state != STATE_ANSWER_VOTE) return true;
    app.players[app.buzzing_player].score++;
    update_scoreboard();
    app.qs_played++;
    check_winner_or_next();
    return true;
}

static bool on_vote_wrong(AromaNode *n, void *d) {
    (void)n; (void)d;
    if (app.state != STATE_ANSWER_VOTE) return true;
    app.qs_played++;
    check_winner_or_next();
    return true;
}

static bool on_play_again(AromaNode *n, void *d) {
    (void)n; (void)d;
    transition_to(STATE_SETUP);
    return true;
}

static void rebuild_buzz_buttons(void) {
    const int left_idx[2]  = {0, 2};
    const int right_idx[2] = {1, 3};

    for (int slot = 0; slot < 2; slot++) {
        int li = left_idx[slot];
        int ri = right_idx[slot];
        bool l_active = (li < app.num_players);
        bool r_active = (ri < app.num_players);

        if (app.buzz_left[slot]) aroma_node_set_hidden(app.buzz_left[slot], !l_active);
        if (app.buzz_right[slot]) aroma_node_set_hidden(app.buzz_right[slot], !r_active);

        if (l_active && app.buzz_left[slot]) {
            char label[80];
            snprintf(label, sizeof(label), "BUZZ\n%s", app.players[li].name);
            aroma_button_set_colors(app.buzz_left[slot],
                app.players[li].color, app.players[li].color,
                app.players[li].color, 0xFFFFFFFF);
        }
        if (r_active && app.buzz_right[slot]) {
            char label[80];
            snprintf(label, sizeof(label), "BUZZ\n%s", app.players[ri].name);
            aroma_button_set_colors(app.buzz_right[slot],
                app.players[ri].color, app.players[ri].color,
                app.players[ri].color, 0xFFFFFFFF);
        }
    }
}

void check_winner_or_next(void) {
    int winner = -1;
    for (int i = 0; i < app.num_players; i++) {
        if (app.players[i].score >= app.points_to_win) {
            winner = i;
            break;
        }
    }

    bool game_over = (winner >= 0) || (app.qs_played >= app.questions_per_game);

    if (game_over) {
        if (winner < 0) {
            int max_s = -1;
            for (int i = 0; i < app.num_players; i++) {
                if (app.players[i].score > max_s) {
                    max_s = app.players[i].score;
                    winner = i;
                }
            }
        }
        char wbuf[128];
        snprintf(wbuf, sizeof(wbuf), "\xF0\x9F\x8F\x86  %s wins!", app.players[winner].name);
        aroma_label_set_text(app.winner_text, wbuf);

        for (int i = 0; i < 4; i++) {
            char sbuf[128];
            if (i < app.num_players)
                snprintf(sbuf, sizeof(sbuf), "%s — %d pts", app.players[i].name, app.players[i].score);
            else
                sbuf[0] = '\0';
            aroma_label_set_text(app.score_summary_labels[i], sbuf);
            aroma_node_set_hidden(app.score_summary_labels[i], i >= app.num_players);
        }
        transition_to(STATE_END);
    } else {
        transition_to(STATE_BUZZ);
    }
}

void transition_to(GameState next) {
    app.current_animation_id = 0;
    app.state = next;

    if (app.setup_layer) aroma_node_set_hidden(app.setup_layer, true);
    if (app.game_layer) aroma_node_set_hidden(app.game_layer, true);
    if (app.answer_layer) aroma_node_set_hidden(app.answer_layer, true);
    if (app.end_layer) aroma_node_set_hidden(app.end_layer, true);
    if (app.topbar_layer) aroma_node_set_hidden(app.topbar_layer, true);

    switch (next) {
    case STATE_SETUP:
        if (app.setup_layer) aroma_node_set_hidden(app.setup_layer, false);
        refresh_setup_ui();
        if (app.setup_card) slide_in_card(app.setup_card, 80, 500);
        break;

    case STATE_BUZZ:
        if (app.game_layer) aroma_node_set_hidden(app.game_layer, false);
        if (app.topbar_layer) aroma_node_set_hidden(app.topbar_layer, false);
        update_scoreboard();

        int q_idx = app.active_questions[app.qs_played % TOTAL_QUESTIONS];
        if (app.q_text) aroma_label_set_text(app.q_text, question_bank[q_idx].q);
        if (app.cat_text) aroma_label_set_text(app.cat_text, question_bank[q_idx].cat);

        if (app.q_card) slide_in_card(app.q_card, -120, 450);

        for (int slot = 0; slot < 2; slot++) {
            if (app.buzz_left[slot] && !aroma_node_is_hidden(app.buzz_left[slot]))
                slide_in_from_left(app.buzz_left[slot], 350 + slot * 60);
            if (app.buzz_right[slot] && !aroma_node_is_hidden(app.buzz_right[slot]))
                slide_in_from_right(app.buzz_right[slot], 350 + slot * 60);
        }
        break;

    case STATE_ANSWER_WAIT:
    case STATE_ANSWER_VOTE:
        if (app.answer_layer) aroma_node_set_hidden(app.answer_layer, false);
        if (app.topbar_layer) aroma_node_set_hidden(app.topbar_layer, false);

        char buf[128];
        snprintf(buf, sizeof(buf), "%s buzzed in!",
                 app.players[app.buzzing_player].name);
        if (app.buzzed_info_text) aroma_label_set_text(app.buzzed_info_text, buf);

        if (app.buzzed_info_text) {
            aroma_label_set_color(app.buzzed_info_text,
                                  app.players[app.buzzing_player].color);
        }

        if (app.buzzed_info_card) pop_in(app.buzzed_info_card, 400);

        if (next == STATE_ANSWER_WAIT) {
            if (app.ans_card) aroma_node_set_hidden(app.ans_card, true);
            if (app.btn_reveal) aroma_node_set_hidden(app.btn_reveal, false);
            if (app.btn_correct) aroma_node_set_hidden(app.btn_correct, true);
            if (app.btn_wrong) aroma_node_set_hidden(app.btn_wrong, true);
            if (app.btn_reveal) slide_in_card(app.btn_reveal, 60, 320);
        } else {
            int q_idx = app.active_questions[app.qs_played % TOTAL_QUESTIONS];
            char ans_buf[200];
            snprintf(ans_buf, sizeof(ans_buf), "Answer: %s", question_bank[q_idx].a);
            if (app.ans_text) aroma_label_set_text(app.ans_text, ans_buf);

            if (app.ans_card) aroma_node_set_hidden(app.ans_card, false);
            if (app.btn_reveal) aroma_node_set_hidden(app.btn_reveal, true);
            if (app.btn_correct) aroma_node_set_hidden(app.btn_correct, false);
            if (app.btn_wrong) aroma_node_set_hidden(app.btn_wrong, false);

            if (app.ans_card) slide_in_from_left(app.ans_card, 420);
            if (app.btn_correct) slide_in_from_left(app.btn_correct, 360);
            if (app.btn_wrong) slide_in_from_right(app.btn_wrong, 360);
        }
        break;

    case STATE_END:
        if (app.end_layer) aroma_node_set_hidden(app.end_layer, false);
        if (app.end_card) slide_in_card(app.end_card, 100, 500);
        if (app.winner_text) pop_in(app.winner_text, 600);
        break;
    }
}

static AromaNode *make_stepper(
    AromaNode   *parent,
    const char  *title,
    int          x, int y, int row_w,
    bool (*on_inc)(AromaNode *, void *),
    bool (*on_dec)(AromaNode *, void *),
    AromaFont   *font_label,
    AromaFont   *font_btn)
{
    int btn_w = dp(SETUP_STEP_BTN_W_DP);
    int btn_h = dp(SETUP_STEP_BTN_H_DP);
    int P     = dp(PADDING_DP);

    aroma_ui_label(parent, title, x, y, LABEL_STYLE_LABEL_MEDIUM, font_label);

    int val_y = y + dp(28);

    aroma_ui_button(parent, "-",
                    x, val_y, btn_w, btn_h,
                    on_dec, NULL, font_btn);

    int val_x = x + btn_w + P;
    AromaNode *val_lbl = aroma_ui_label(parent, "0",
                                        val_x, val_y + (btn_h - sp(FONT_MED_SP)) / 2,
                                        LABEL_STYLE_LABEL_LARGE, font_label);

    int inc_x = x + row_w - btn_w;
    aroma_ui_button(parent, "+",
                    inc_x, val_y, btn_w, btn_h,
                    on_inc, NULL, font_btn);

    return val_lbl;
}

static void build_ui(void) {
    const int W  = app.win_w;
    const int H  = app.win_h;
    const int TH = app.topbar_h;
    const int P  = dp(PADDING_DP);
    const int G  = dp(GAP_DP);

    {
        app.setup_layer = aroma_ui_container(
            (AromaNode *)app.window,
            0, 0, W, H,
            AROMA_LAYOUT_MODE_NONE,
            AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);

        int card_w = W - P * 2;
        int card_h = dp(440);
        int card_x = P;
        int card_y = (H - card_h) / 2;

        app.setup_card = aroma_ui_card(
            app.setup_layer,
            card_x, card_y, card_w, card_h,
            CARD_TYPE_ELEVATED);

        int col_gap   = dp(32);
        int left_w    = (card_w - dp(48) - col_gap) / 2;
        int right_w   = card_w - dp(48) - col_gap - left_w;
        int left_x    = dp(24);
        int right_x   = left_x + left_w + col_gap;
        int top_y     = dp(24);

        int cy = top_y;

        aroma_ui_label(app.setup_card, "Buzzer!",
                       left_x, cy,
                       LABEL_STYLE_LABEL_LARGE, app.font_title);
        cy += dp(52);

        aroma_ui_divider(app.setup_card,
                         left_x, cy, left_w,
                         DIVIDER_ORIENTATION_HORIZONTAL);
        cy += dp(14);

        app.lbl_num_players = make_stepper(
            app.setup_card,
            "Number of Players",
            left_x, cy, left_w,
            on_players_inc, on_players_dec,
            app.font_med, app.font_large);
        cy += dp(90);

        app.lbl_questions = make_stepper(
            app.setup_card,
            "Questions per Game",
            left_x, cy, left_w,
            on_questions_inc, on_questions_dec,
            app.font_med, app.font_large);
        cy += dp(90);

        app.lbl_points = make_stepper(
            app.setup_card,
            "Points to Win",
            left_x, cy, left_w,
            on_points_inc, on_points_dec,
            app.font_med, app.font_large);

        aroma_ui_divider(app.setup_card,
                         left_x + left_w + col_gap / 2, top_y,
                         card_h - top_y * 2,
                         DIVIDER_ORIENTATION_VERTICAL);

        int rcy = top_y;

        aroma_ui_label(app.setup_card, "Player Names",
                       right_x, rcy,
                       LABEL_STYLE_LABEL_LARGE, app.font_med);
        rcy += dp(52);

        aroma_ui_divider(app.setup_card,
                         right_x, rcy, right_w,
                         DIVIDER_ORIENTATION_HORIZONTAL);
        rcy += dp(14);

        int tb_h     = dp(SETUP_TB_H_DP);
        int tb_gap   = dp(10);
        int swatch_w = dp(10);

        for (int i = 0; i < 4; i++) {
            AromaNode *swatch = aroma_ui_container(
                app.setup_card,
                right_x, rcy + (tb_h - dp(32)) / 2,
                swatch_w, dp(32),
                AROMA_LAYOUT_MODE_NONE,
                AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_START);

            app.tb_name_rows[i] = aroma_ui_container(
                app.setup_card,
                right_x + swatch_w + dp(8), rcy,
                right_w - swatch_w - dp(8), tb_h,
                AROMA_LAYOUT_MODE_NONE,
                AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);

            app.tb_names[i] = aroma_ui_textbox(
                app.tb_name_rows[i],
                0, 0,
                right_w - swatch_w - dp(8), tb_h,
                DEFAULT_NAMES[i],
                NULL, NULL, app.font_small);
            aroma_ui_textbox_set_text(app.tb_names[i], DEFAULT_NAMES[i]);

            rcy += tb_h + tb_gap;
        }

        int btn_w = dp(SETUP_BTN_W_DP);
        int btn_h = dp(SETUP_BTN_H_DP);
        AromaNode* btn = aroma_ui_button(app.window,
                        "Start Game",
                        (card_w - btn_w) / 2,
                        card_h - btn_h - dp(20),
                        btn_w, btn_h,
                        on_start_game, NULL, app.font_med);
        aroma_node_set_z_index(btn, 100);
    }

    {
        app.topbar_layer = aroma_ui_container(
            (AromaNode *)app.window,
            0, 0, W, TH,
            AROMA_LAYOUT_MODE_NONE,
            AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_START);

        int label_y = (TH - sp(FONT_SMALL_SP)) / 2;
        int col_w   = (W - dp(160)) / 4;

        for (int i = 0; i < 4; i++) {
            app.score_labels[i] = aroma_ui_label(
                app.topbar_layer,
                "",
                P + i * col_w, label_y,
                LABEL_STYLE_LABEL_MEDIUM, app.font_small);
        }

        app.q_count_label = aroma_ui_label(
            app.topbar_layer,
            "Q 1 / ?",
            W - dp(280), label_y,
            LABEL_STYLE_LABEL_MEDIUM, app.font_small);
    }

    {
        int layer_y = TH;
        int layer_h = H - TH;

        app.game_layer = aroma_ui_container(
            (AromaNode *)app.window,
            0, layer_y, W, layer_h,
            AROMA_LAYOUT_MODE_NONE,
            AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_START);

        int buzz_col_w = dp(BUZZ_BTN_W_DP);
        int q_card_x   = buzz_col_w + G;
        int q_card_w   = W - buzz_col_w * 2 - G * 2;
        int q_card_h   = dp(Q_CARD_H_DP);
        int q_card_y   = dp(12);

        app.q_card = aroma_ui_card(
            app.game_layer,
            q_card_x, q_card_y,
            q_card_w, q_card_h,
            CARD_TYPE_ELEVATED);

        app.cat_text = aroma_ui_label(
            app.q_card,
            "Category",
            dp(16), dp(10),
            LABEL_STYLE_LABEL_MEDIUM, app.font_small);

        app.q_text = aroma_ui_label(
            app.q_card,
            "Question?",
            dp(16), dp(38),
            LABEL_STYLE_LABEL_LARGE, app.font_med);

        int buzz_h      = (layer_h - dp(Q_CARD_H_DP) - q_card_y * 2 - G) / 2;
        int buzz_top_y  = q_card_y;
        int buzz_bot_y  = q_card_y + buzz_h + G;

        int avail_buzz_h = layer_h - dp(12) * 2;
        if (buzz_h < dp(80)) {
            buzz_h     = (avail_buzz_h - G) / 2;
            buzz_top_y = dp(12);
            buzz_bot_y = dp(12) + buzz_h + G;
        }

        app.buzz_left[0] = aroma_ui_button(
            app.game_layer,
            "BUZZ\nPlayer 1",
            0, buzz_top_y, buzz_col_w, buzz_h,
            on_buzz, (void *)(intptr_t)0, app.font_large);

        app.buzz_left[1] = aroma_ui_button(
            app.game_layer,
            "BUZZ\nPlayer 3",
            0, buzz_bot_y, buzz_col_w, buzz_h,
            on_buzz, (void *)(intptr_t)2, app.font_large);

        int right_x = W - buzz_col_w;
        app.buzz_right[0] = aroma_ui_button(
            app.game_layer,
            "BUZZ\nPlayer 2",
            right_x, buzz_top_y, buzz_col_w, buzz_h,
            on_buzz, (void *)(intptr_t)1, app.font_large);

        app.buzz_right[1] = aroma_ui_button(
            app.game_layer,
            "BUZZ\nPlayer 4",
            right_x, buzz_bot_y, buzz_col_w, buzz_h,
            on_buzz, (void *)(intptr_t)3, app.font_large);

        int skip_w = dp(120);
        int skip_h = dp(40);
        int skip_x = (W - skip_w) / 2;
        int skip_y = layer_h - skip_h - dp(12);
        app.skip_btn = aroma_ui_button(
            app.game_layer,
            "Skip",
            skip_x, skip_y, skip_w, skip_h,
            on_skip, NULL, app.font_small);
    }

    {
        int layer_y = TH;
        int layer_h = H - TH;

        app.answer_layer = aroma_ui_container(
            (AromaNode *)app.window,
            0, layer_y, W, layer_h,
            AROMA_LAYOUT_MODE_NONE,
            AROMA_FLEX_ROW, AROMA_JUSTIFY_START, AROMA_ALIGN_START);

        int cx = (W - dp(600)) / 2;
        int cy = dp(24);

        int bi_card_w = dp(600);
        int bi_card_h = dp(72);
        app.buzzed_info_card = aroma_ui_card(
            app.answer_layer,
            cx, cy, bi_card_w, bi_card_h,
            CARD_TYPE_ELEVATED);

        app.buzzed_info_text = aroma_ui_label(
            app.buzzed_info_card,
            "Player buzzed!",
            dp(20), (bi_card_h - sp(FONT_LARGE_SP)) / 2,
            LABEL_STYLE_LABEL_LARGE, app.font_large);

        cy += bi_card_h + G;

        int ans_card_w = dp(600);
        int ans_card_h = dp(ANS_CARD_H_DP);
        app.ans_card = aroma_ui_card(
            app.answer_layer,
            cx, cy, ans_card_w, ans_card_h,
            CARD_TYPE_ELEVATED);

        app.ans_text = aroma_ui_label(
            app.ans_card,
            "Answer: ???",
            dp(20), (ans_card_h - sp(FONT_LARGE_SP)) / 2,
            LABEL_STYLE_LABEL_LARGE, app.font_large);

        cy += ans_card_h + dp(20);

        int rv_w = dp(240);
        int rv_h = dp(VOTE_BTN_H_DP);
        app.btn_reveal = aroma_ui_button(
            app.answer_layer,
            "Reveal Answer",
            cx + (dp(600) - rv_w) / 2, cy,
            rv_w, rv_h,
            on_reveal, NULL, app.font_med);

        int vb_w = dp(VOTE_BTN_W_DP);
        int vb_h = dp(VOTE_BTN_H_DP);
        int total_vb_w = vb_w * 2 + G;
        int vb_x1 = cx + (dp(600) - total_vb_w) / 2;
        int vb_x2 = vb_x1 + vb_w + G;

        app.btn_correct = aroma_ui_button(
            app.answer_layer,
            "Correct  +1",
            vb_x1, cy, vb_w, vb_h,
            on_vote_correct, NULL, app.font_med);
        aroma_button_set_colors(app.btn_correct,
            0xFF1D9E75, 0xFF1D9E75, 0xFF1D9E75, 0xFFFFFFFF);

        app.btn_wrong = aroma_ui_button(
            app.answer_layer,
            "Wrong",
            vb_x2, cy, vb_w, vb_h,
            on_vote_wrong, NULL, app.font_med);
        aroma_button_set_colors(app.btn_wrong,
            0xFFD4537E, 0xFFD4537E, 0xFFD4537E, 0xFFFFFFFF);
    }

    {
        app.end_layer = aroma_ui_container(
            (AromaNode *)app.window,
            0, 0, W, H,
            AROMA_LAYOUT_MODE_NONE,
            AROMA_FLEX_COLUMN, AROMA_JUSTIFY_START, AROMA_ALIGN_CENTER);

        int card_w = dp(520);
        int card_h = dp(380);
        int card_x = (W - card_w) / 2;
        int card_y = (H - card_h) / 2;

        app.end_card = aroma_ui_card(
            app.end_layer,
            card_x, card_y, card_w, card_h,
            CARD_TYPE_ELEVATED);

        int cc = dp(24);
        int cy = dp(24);

        aroma_ui_label(app.end_card, "Game Over!",
                       cc, cy,
                       LABEL_STYLE_LABEL_LARGE, app.font_title);
        cy += dp(52);

        aroma_ui_divider(app.end_card,
                         cc, cy, card_w - dp(48),
                         DIVIDER_ORIENTATION_HORIZONTAL);
        cy += dp(16);

        app.winner_text = aroma_ui_label(
            app.end_card, "Winner: ---",
            cc, cy,
            LABEL_STYLE_LABEL_LARGE, app.font_large);
        cy += dp(52);

        for (int i = 0; i < 4; i++) {
            app.score_summary_labels[i] = aroma_ui_label(
                app.end_card, "",
                cc, cy + i * dp(36),
                LABEL_STYLE_LABEL_MEDIUM, app.font_med);
        }
        cy += 4 * dp(36) + dp(12);

        int pa_w = dp(200);
        int pa_h = dp(52);
        aroma_ui_button(app.end_card,
                        "Play Again",
                        (card_w - pa_w) / 2, card_h - pa_h - dp(24),
                        pa_w, pa_h,
                        on_play_again, NULL, app.font_med);
    }
}

#ifdef ANDROID
#include <android_native_app_glue.h>
void android_main(struct android_app *state) {
    aroma_android_set_app(state);
    aroma_android_set_orientation_landscape();
#else
int main(int argc, char **argv) {
    (void)argc; (void)argv;
#endif
    srand((unsigned)time(NULL));
    aroma_animation_manager_init();
    aroma_ui_init();

    app.theme = aroma_theme_create_material_preset_dark(AROMA_THEME_MATERIAL_PURPLE);
    app.theme.colors.primary = 0xFF7F77DD;
    app.theme.enable_shadows = true;
    aroma_ui_set_theme(&app.theme);

#ifdef ANDROID
    int avail_w = 0, avail_h = 0;
    aroma_android_get_available_size_dp(&avail_w, &avail_h);
    app.win_w = aroma_android_dp_to_px(avail_w > 0 ? avail_w : WIN_W_DP);
    app.win_h = aroma_android_dp_to_px(avail_h > 0 ? avail_h : WIN_H_DP);
#else
    app.win_w = dp(WIN_W_DP);
    app.win_h = dp(WIN_H_DP);
#endif
    app.topbar_h = dp(TOPBAR_H_DP);
    app.current_animation_id = 0;

    app.font_title = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, sp(FONT_TITLE_SP));
    app.font_large = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, sp(FONT_LARGE_SP));
    app.font_med   = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, sp(FONT_MED_SP));
    app.font_small = aroma_font_create_from_memory(
        aroma_ubuntu_ttf, aroma_ubuntu_ttf_len, sp(FONT_SMALL_SP));

    app.window = aroma_ui_create_window("Buzzer", app.win_w, app.win_h);
    aroma_event_set_root((AromaNode *)app.window);
    aroma_ui_prepare_font_for_window(0, app.font_title);
    aroma_ui_prepare_font_for_window(0, app.font_large);
    aroma_ui_prepare_font_for_window(0, app.font_med);
    aroma_ui_prepare_font_for_window(0, app.font_small);

    for (int i = 0; i < 4; i++) {
        strncpy(app.players[i].name, DEFAULT_NAMES[i], 63);
        app.players[i].color = PLAYER_COLORS[i];
        app.players[i].score = 0;
    }

    app.num_players        = 4;
    app.questions_per_game = 10;
    app.points_to_win      = 5;

    build_ui();

    shuffle_questions();
    transition_to(STATE_SETUP);

    while (aroma_ui_is_running()) {
        aroma_ui_process_events();
        aroma_ui_render(app.window);
    }

    aroma_ui_destroy_window(app.window);
    aroma_ui_shutdown();

#ifndef ANDROID
    return 0;
#endif
}