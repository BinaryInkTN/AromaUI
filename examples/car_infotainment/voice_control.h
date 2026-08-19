#ifndef VOICE_CONTROL_H
#define VOICE_CONTROL_H

void start_voice_control_thread(void);
void trigger_manual_wake(void);
void aroma_voice_speak(const char *message);

#endif
