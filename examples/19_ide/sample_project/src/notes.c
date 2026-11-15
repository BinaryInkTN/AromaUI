#include "notes.h"

#include <stdio.h>
#include <string.h>

static Note s_notes[NOTES_MAX];
static int s_note_count;
static unsigned int s_revision;

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (!destination || capacity == 0)
        return;
    snprintf(destination, capacity, "%s", source ? source : "");
}

void notes_init(void)
{
    memset(s_notes, 0, sizeof(s_notes));
    s_note_count = 0;
    s_revision = 0;

    notes_add("Release checklist", "Verify markup preview and save the workspace.");
    notes_add("Language services", "Check hover, completion and go-to-definition.");
    notes_add("Design review", "Keep the warm accent against the dark surface.");
}

void notes_clear(void)
{
    memset(s_notes, 0, sizeof(s_notes));
    s_note_count = 0;
    s_revision++;
}

int notes_count(void)
{
    return s_note_count;
}

const Note *notes_get(int index)
{
    if (index < 0 || index >= s_note_count)
        return NULL;
    return &s_notes[index];
}

int notes_add(const char *title, const char *body)
{
    if (s_note_count >= NOTES_MAX)
        return -1;

    Note *note = &s_notes[s_note_count];
    copy_text(note->title, sizeof(note->title), title);
    copy_text(note->body, sizeof(note->body), body);
    note->revision = ++s_revision;
    return s_note_count++;
}
