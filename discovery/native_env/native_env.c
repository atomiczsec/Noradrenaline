#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 16384

static char output[OUT_SIZE];

static int parse_limit(const char *value, int fallback, int min, int max) {
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || parsed < min) {
        return fallback;
    }
    if (parsed > max) {
        return max;
    }
    return (int)parsed;
}

static int appendf(size_t *used, const char *fmt, ...) {
    if (*used >= sizeof(output)) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(output + *used, sizeof(output) - *used, fmt, args);
    va_end(args);
    if (written < 0) {
        return 0;
    }
    if ((size_t)written >= sizeof(output) - *used) {
        *used = sizeof(output) - 1;
        output[*used] = '\0';
        return 0;
    }
    *used += (size_t)written;
    return 1;
}

EXPORT char *list_env(int argc, char **argv) {
    const char *prefix = argc > 1 ? argv[1] : "";
    int limit = parse_limit(argc > 2 ? argv[2] : NULL, 50, 1, 200);
    size_t used = 0;
    int shown = 0;

    output[0] = '\0';
    appendf(&used, "Environment variables");
    if (prefix != NULL && prefix[0] != '\0') {
        appendf(&used, " matching prefix '%s'", prefix);
    }
    appendf(&used, " (limit %d):\n", limit);

    for (char **env = environ; env != NULL && *env != NULL && shown < limit; env++) {
        if (prefix == NULL || prefix[0] == '\0' || strncmp(*env, prefix, strlen(prefix)) == 0) {
            appendf(&used, "%s\n", *env);
            shown++;
        }
    }
    appendf(&used, "shown=%d\n", shown);
    return output;
}

EXPORT char *get_env(int argc, char **argv) {
    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        snprintf(output, sizeof(output), "usage: get_env NAME");
        return output;
    }
    const char *value = getenv(argv[1]);
    if (value == NULL) {
        snprintf(output, sizeof(output), "%s is not set", argv[1]);
    } else {
        snprintf(output, sizeof(output), "%s=%s", argv[1], value);
    }
    return output;
}
