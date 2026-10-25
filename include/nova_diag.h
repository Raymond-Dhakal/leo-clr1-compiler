/*
 * nova_diag.h
 * Part of the Leo compiler extension.
 *
 * Diagnostics shared by every compiler phase. A phase never prints errors
 * directly: it adds them to a DiagList, and the driver prints the list
 * (with the offending source line and a caret) and decides whether the
 * pipeline may continue.
 */
#ifndef NOVA_DIAG_H
#define NOVA_DIAG_H

#include <stdio.h>

typedef enum { DIAG_ERROR, DIAG_WARNING } DiagLevel;

typedef struct {
    DiagLevel level;
    const char *phase;   /* "lexical", "syntax", "semantic" (static string) */
    int line, col;       /* 1-based; line 0 means "no location"             */
    char *message;
} Diag;

typedef struct {
    const char *filename;
    const char *source;  /* not owned; used to print the offending line */
    Diag *items;
    int count, capacity;
    int error_count, warning_count;
} DiagList;

void diag_init(DiagList *d, const char *filename, const char *source);
void diag_free(DiagList *d);
void diag_add(DiagList *d, DiagLevel level, const char *phase,
              int line, int col, const char *fmt, ...);
/* Prints every diagnostic: "file:line:col: <phase> error: message", the
   source line, and a caret under the column. */
void diag_print(const DiagList *d, FILE *out);

#endif /* NOVA_DIAG_H */
