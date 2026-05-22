#ifndef VOICE_HANDLER_H
#define VOICE_HANDLER_H

#include <stdbool.h>

// Voice control functions
void set_voice_status(const char *status);
void queue_voice_navigation(const char *dest);
void queue_voice_partial(const char *partial_text);
void queue_voice_theme(int dark_mode);
void queue_voice_ac_action(int temp_delta);
void queue_voice_info_request(int info_type);
void queue_voice_action(int tab_index, bool call, bool end_call, const char *status);

// UI callbacks
void voice_button_callback(void *user_data);

// Processing
void process_voice_commands(void);
void build_voice_status_ui(void);

#endif // VOICE_HANDLER_H