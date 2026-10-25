/*
 * nova_codegen.h
 * Part of the Leo compiler extension - PHASE 6: target code generation.
 *
 * Translates the (optimized) IR into a standalone C99 program. Our compiler
 * stops at C source; GCC then performs the real machine-code generation.
 */
#ifndef NOVA_CODEGEN_H
#define NOVA_CODEGEN_H

#include "nova_ir.h"
#include "nova_util.h"

/* Appends the complete C translation unit to `out`. `source_name` is only
   used in the header comment. */
void codegen_c(const IRProgram *p, const char *source_name, StrBuf *out);

#endif /* NOVA_CODEGEN_H */
