/*
 * nova_util.c - number formatting and a growable string buffer.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "nova_util.h"
#include "xalloc.h"

static int looks_integral(const char *s) {
    for (; *s; s++) if (*s == '.' || *s == 'e' || *s == 'E' || *s == 'n' || *s == 'i') return 0;
    return 1;
}

void nova_fmt_double_display(double v, char *buf, size_t n) {
    snprintf(buf, n, "%g", v);
    if (looks_integral(buf) && strlen(buf) + 3 < n) strcat(buf, ".0");
}

void nova_fmt_double_c(double v, char *buf, size_t n) {
    snprintf(buf, n, "%.17g", v);
    if (looks_integral(buf) && strlen(buf) + 3 < n) strcat(buf, ".0");
}

void strbuf_init(StrBuf *b) {
    b->cap = 1024;
    b->len = 0;
    b->data = (char *)xmalloc(b->cap);
    b->data[0] = '\0';
}

void strbuf_free(StrBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

static void strbuf_reserve(StrBuf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        while (b->len + extra + 1 > b->cap) b->cap *= 2;
        b->data = (char *)xrealloc(b->data, b->cap);
    }
}

void strbuf_append(StrBuf *b, const char *s) {
    size_t n = strlen(s);
    strbuf_reserve(b, n);
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
}

void strbuf_printf(StrBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    strbuf_reserve(b, (size_t)n);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
}

void strbuf_append_quoted(StrBuf *b, const char *s, int c_style) {
    strbuf_append(b, "\"");
    unsigned char prev = 0;
    for (const unsigned char *q = (const unsigned char *)s; *q; prev = *q, q++) {
        switch (*q) {
            case '\n': strbuf_append(b, "\\n"); break;
            case '\t': strbuf_append(b, "\\t"); break;
            case '"':  strbuf_append(b, "\\\""); break;
            case '\\': strbuf_append(b, "\\\\"); break;
            default:
                if (c_style && (*q < 32 || *q >= 127 || (*q == '?' && prev == '?')))   /* "??x" could be a trigraph */
                    strbuf_printf(b, "\\%03o", *q);
                else
                    strbuf_printf(b, "%c", *q);
        }
    }
    strbuf_append(b, "\"");
}
