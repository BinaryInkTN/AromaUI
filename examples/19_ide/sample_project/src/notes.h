#ifndef AROMA_NOTES_H
#define AROMA_NOTES_H

#define NOTES_MAX 16
#define NOTE_TITLE_MAX 96
#define NOTE_BODY_MAX 192

/* FIXME: persist the note array before shipping the sample. */
typedef struct Note
{
    char title[NOTE_TITLE_MAX];
    char body[NOTE_BODY_MAX];
    unsigned int revision;
} Note;

void notes_init(void);
void notes_clear(void);
int notes_count(void);
const Note *notes_get(int index);
int notes_add(const char *title, const char *body);

#endif
