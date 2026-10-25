/*
 * nova_util.h
 * Part of the Leo compiler extension: tiny shared helpers.
 */
#ifndef NOVA_UTIL_H
#define NOVA_UTIL_H

#include <stddef.h>

/* Shortest readable form for IR dumps / AST dumps: 2.5, 3.0, 1e+20 ... */
void nova_fmt_double_display(double v, char *buf, size_t n);
/* Exact round-trip form usable as a C double literal (always has '.' or 'e'). */
void nova_fmt_double_c(double v, char *buf, size_t n);

/* Appends `s` to `b` as a quoted literal. With c_style the result is a valid C
   string literal (octal escapes for control and non ASCII bytes, so the C
   compiler never sees trigraphs or multi byte surprises); otherwise a Leo
   style literal using only the escapes \n \t \\ \" (for dumps). */
/* Growable text buffer used by the C code generator. */
typedef struct {
    char *data;
    size_t len, cap;
} StrBuf;

void strbuf_init(StrBuf *b);
void strbuf_free(StrBuf *b);
void strbuf_append(StrBuf *b, const char *s);
void strbuf_printf(StrBuf *b, const char *fmt, ...);
void strbuf_append_quoted(StrBuf *b, const char *s, int c_style);

#endif /* NOVA_UTIL_H */
