/*
 * nova_build.c - file output and process control for the driver.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "nova_build.h"
#include "xalloc.h"

#ifdef _WIN32
  #include <direct.h>
  #include <process.h>
  #define MKDIR(p) _mkdir(p)
#elif defined(__wasi__)
  /* The browser build (WebAssembly): no processes, no GCC. Only --interp works. */
  #define MKDIR(p) mkdir((p), 0777)
#else
  #include <sys/wait.h>
  #include <unistd.h>
  #define MKDIR(p) mkdir((p), 0777)
#endif

int nova_mkdirs(const char *path) {
    char *tmp = xstrdup(path);
    size_t len = strlen(tmp);
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char c = *p;
            *p = '\0';
            if (MKDIR(tmp) != 0 && errno != EEXIST) { free(tmp); return -1; }
            *p = c;
        }
    }
    int rc = (MKDIR(tmp) != 0 && errno != EEXIST) ? -1 : 0;
    free(tmp);
    return rc;
}

int nova_write_file(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    int rc = (w == len) ? 0 : -1;
    if (fclose(f) != 0) rc = -1;
    return rc;
}

int nova_same_file(const char *a, const char *b) {
#ifdef _WIN32
    return _stricmp(a, b) == 0;
#else
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
#endif
}

void nova_exe_path(char *buf, size_t n, const char *dir, const char *base) {
#ifdef _WIN32
    snprintf(buf, n, "%s/%s.exe", dir, base);
#else
    snprintf(buf, n, "%s/%s", dir, base);
#endif
}

int nova_spawn(char *const argv[]) {
    fflush(stdout);
    fflush(stderr);
#ifdef _WIN32
    int rc = (int)_spawnvp(_P_WAIT, argv[0], (const char *const *)argv);
    return rc;
#elif defined(__wasi__)
    (void)argv;
    return -1;      /* cannot start programs inside WebAssembly */
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "leoc: cannot execute '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}
