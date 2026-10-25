/*
 * nova_diag.c - diagnostic collection and printing.
 */
#include <stdarg.h>
#include <string.h>
#include "nova_diag.h"
#include "xalloc.h"

void diag_init(DiagList *d, const char *filename, const char *source) {
    memset(d, 0, sizeof(*d));
    d->filename = filename;
    d->source = source;
}

void diag_free(DiagList *d) {
    for (int i = 0; i < d->count; i++) free(d->items[i].message);
    free(d->items);
    memset(d, 0, sizeof(*d));
}

void diag_add(DiagList *d, DiagLevel level, const char *phase,
              int line, int col, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    NOVA_GROW(d->items, d->count, d->capacity);
    Diag *e = &d->items[d->count++];
    e->level = level;
    e->phase = phase;
    e->line = line;
    e->col = col;
    e->message = xstrdup(buf);
    if (level == DIAG_ERROR) d->error_count++; else d->warning_count++;
}

/* Prints source line number `line` (1-based) and a caret under `col`. */
static void print_source_context(const DiagList *d, FILE *out, int line, int col) {
    if (!d->source || line <= 0) return;
    const char *p = d->source;
    for (int l = 1; l < line && *p; l++) {
        const char *nl = strchr(p, '\n');
        if (!nl) return;
        p = nl + 1;
    }
    const char *end = p;
    while (*end && *end != '\n' && *end != '\r') end++;
    int len = (int)(end - p);
    if (len > 200) len = 200;

    fprintf(out, "  %4d | ", line);
    for (int i = 0; i < len; i++) fputc(p[i] == '\t' ? ' ' : p[i], out);
    fputc('\n', out);
    fprintf(out, "       | ");
    for (int i = 1; i < col && i <= len; i++) fputc(' ', out);
    fputs("^\n", out);
}

void diag_print(const DiagList *d, FILE *out) {
    for (int i = 0; i < d->count; i++) {
        const Diag *e = &d->items[i];
        const char *kind = (e->level == DIAG_ERROR) ? "error" : "warning";
        if (e->line > 0) {
            fprintf(out, "%s:%d:%d: ", d->filename, e->line, e->col);
        } else {
            fprintf(out, "%s: ", d->filename);
        }
        if (e->level == DIAG_ERROR) fprintf(out, "%s %s: %s\n", e->phase, kind, e->message);
        else fprintf(out, "%s: %s\n", kind, e->message);
        print_source_context(d, out, e->line, e->col);
    }
}
