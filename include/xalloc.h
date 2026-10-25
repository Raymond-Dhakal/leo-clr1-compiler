/*
 * xalloc.h
 * Part of the Leo compiler extension.
 *
 * Allocation helpers used by all NEW compiler modules (lexer, AST,
 * semantic analysis, IR, optimizer, code generator, driver). Every helper
 * checks the result of the allocation and terminates with a clear message
 * on failure, so the compiler never dereferences a NULL pointer after
 * running out of memory. (The original CLR(1) generator modules keep their
 * own allocation code unchanged.)
 */
#ifndef NOVA_XALLOC_H
#define NOVA_XALLOC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void nova_oom(void) {
    fprintf(stderr, "leoc: fatal: out of memory\n");
    exit(70);
}

static inline void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) nova_oom();
    return p;
}

static inline void *xcalloc(size_t n, size_t sz) {
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) nova_oom();
    return p;
}

static inline void *xrealloc(void *old, size_t n) {
    void *p = realloc(old, n ? n : 1);
    if (!p) nova_oom();
    return p;
}

static inline char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static inline char *xstrndup(const char *s, size_t len) {
    char *p = (char *)xmalloc(len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

/* Grow-on-demand array helper: ensures room for one more element. */
#define NOVA_GROW(ptr, count, cap)                                            \
    do {                                                                       \
        if ((count) >= (cap)) {                                                \
            (cap) = (cap) ? (cap) * 2 : 8;                                     \
            (ptr) = xrealloc((ptr), sizeof(*(ptr)) * (size_t)(cap));           \
        }                                                                      \
    } while (0)

#endif /* NOVA_XALLOC_H */
