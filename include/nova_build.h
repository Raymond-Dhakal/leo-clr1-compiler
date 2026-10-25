/*
 * nova_build.h
 * Part of the Leo compiler extension: file output, GCC invocation and
 * program execution. Child processes are started with an argument vector
 * (fork + execvp / _spawnvp); no shell command line is ever assembled, so
 * file names containing spaces or shell characters are safe.
 */
#ifndef NOVA_BUILD_H
#define NOVA_BUILD_H

#include <stddef.h>

/* Creates `path` and its missing parents. Returns 0 on success. */
int nova_mkdirs(const char *path);
int nova_write_file(const char *path, const char *data, size_t len);
/* 1 if both paths exist and name the same file (guards the source file). */
int nova_same_file(const char *a, const char *b);
/* Runs argv[0] with arguments; returns its exit status, 128+signal if it
   was killed, or -1 if it could not be started. */
int nova_spawn(char *const argv[]);
/* "dir/name" or "dir/name.exe" on Windows. */
void nova_exe_path(char *buf, size_t n, const char *dir, const char *base);

#endif /* NOVA_BUILD_H */
