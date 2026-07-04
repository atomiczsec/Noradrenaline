#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#define OUT_SIZE 98304
#define MAX_ARTIFACTS 64
#define DEFAULT_TAIL_LINES 25
#define MAX_TAIL_LINES 200
#define DEFAULT_TAIL_BYTES 4096
#define MAX_TAIL_BYTES 16384
#define DEFAULT_READ_BYTES 8192
#define MAX_READ_BYTES 65536

static char output[OUT_SIZE];

typedef struct {
    char path[PATH_MAX];
    char source[64];
} artifact_t;

typedef struct {
    artifact_t items[MAX_ARTIFACTS];
    size_t count;
    int capped;
} artifact_list_t;

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

static const char *home_dir(void) {
    const char *home = getenv("HOME");
    return home != NULL && home[0] != '\0' ? home : ".";
}

static int str_equals_i(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
    }
    return *a == '\0' && *b == '\0';
}

static int starts_with(const char *s, const char *prefix) {
    if (s == NULL || prefix == NULL) {
        return 0;
    }
    size_t prefix_len = strlen(prefix);
    return strncmp(s, prefix, prefix_len) == 0;
}

static int build_path(char *out, size_t out_size, const char *base, const char *suffix) {
    if (out == NULL || out_size == 0 || base == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(out, out_size, "%s%s", base, suffix);
    return written > 0 && (size_t)written < out_size;
}

static int expand_user_path(char *out, size_t out_size, const char *input, const char *home) {
    if (out == NULL || out_size == 0 || input == NULL || input[0] == '\0') {
        return 0;
    }
    if (input[0] == '~' && (input[1] == '/' || input[1] == '\0')) {
        return build_path(out, out_size, home, input + 1);
    }
    int written = snprintf(out, out_size, "%s", input);
    return written > 0 && (size_t)written < out_size;
}

static int is_regular_file(const char *path, struct stat *out_st) {
    struct stat st;
    if (path == NULL || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (out_st != NULL) {
        *out_st = st;
    }
    return 1;
}

static int is_dir(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_under_home(const char *path, const char *home) {
    size_t home_len = strlen(home);
    if (!starts_with(path, home)) {
        return 0;
    }
    return path[home_len] == '/' || path[home_len] == '\0';
}

static int artifact_seen(const artifact_list_t *list, const char *path) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i].path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_artifact(artifact_list_t *list, const char *source, const char *path) {
    struct stat st;
    if (list == NULL || source == NULL || path == NULL || !is_regular_file(path, &st)) {
        return;
    }
    if (artifact_seen(list, path)) {
        return;
    }
    if (list->count >= MAX_ARTIFACTS) {
        list->capped = 1;
        return;
    }

    snprintf(list->items[list->count].path, sizeof(list->items[list->count].path), "%s", path);
    snprintf(list->items[list->count].source, sizeof(list->items[list->count].source), "%s", source);
    list->count++;
}

static int looks_like_script_log(const char *name) {
    size_t len = name != NULL ? strlen(name) : 0;
    if (len == 0) {
        return 0;
    }
    if (len >= 5 && strcmp(name + len - 5, ".cast") == 0) {
        return 1;
    }
    if (strcmp(name, "typescript") == 0) {
        return 1;
    }
    if (starts_with(name, "script") && strstr(name, ".log") != NULL) {
        return 1;
    }
    return 0;
}

static void scan_dir_for_recorders(artifact_list_t *list, const char *root, int depth) {
    if (list == NULL || root == NULL || depth < 0 || list->count >= MAX_ARTIFACTS || !is_dir(root)) {
        return;
    }

    DIR *dir = opendir(root);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && list->count < MAX_ARTIFACTS) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child[PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(child)) {
            continue;
        }

        if (looks_like_script_log(entry->d_name)) {
            add_artifact(list, "terminal recorder", child);
        }
        if (depth > 0 && is_dir(child)) {
            scan_dir_for_recorders(list, child, depth - 1);
        }
    }

    if (list->count >= MAX_ARTIFACTS) {
        list->capped = 1;
    }
    closedir(dir);
}

static void discover_artifacts(artifact_list_t *list, const char *home) {
    char path[PATH_MAX];
    memset(list, 0, sizeof(*list));

    const char *histfile = getenv("HISTFILE");
    if (histfile != NULL && expand_user_path(path, sizeof(path), histfile, home) && is_under_home(path, home)) {
        add_artifact(list, "HISTFILE", path);
    }

    const char *const shell_files[][2] = {
        {"bash history", "/.bash_history"},
        {"zsh history", "/.zsh_history"},
        {"zsh history", "/.zhistory"},
        {"fish history", "/.local/share/fish/fish_history"},
    };
    for (size_t i = 0; i < sizeof(shell_files) / sizeof(shell_files[0]); i++) {
        if (build_path(path, sizeof(path), home, shell_files[i][1])) {
            add_artifact(list, shell_files[i][0], path);
        }
    }

    const char *xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data != NULL && expand_user_path(path, sizeof(path), xdg_data, home) && is_under_home(path, home)) {
        char fish[PATH_MAX];
        if (build_path(fish, sizeof(fish), path, "/fish/fish_history")) {
            add_artifact(list, "fish history", fish);
        }
    }

    const char *const recorder_roots[] = {
        "/.local/share/asciinema",
        "/.asciinema",
        "/asciinema",
        "/Desktop",
        "/Documents",
        "/Downloads",
    };
    for (size_t i = 0; i < sizeof(recorder_roots) / sizeof(recorder_roots[0]) && list->count < MAX_ARTIFACTS; i++) {
        if (build_path(path, sizeof(path), home, recorder_roots[i])) {
            scan_dir_for_recorders(list, path, 1);
        }
    }
}

static void format_mtime(char *buf, size_t buf_size, time_t mtime) {
    struct tm tm_buf;
    struct tm *tm = localtime_r(&mtime, &tm_buf);
    if (tm == NULL || strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S %z", tm) == 0) {
        snprintf(buf, buf_size, "unknown");
    }
}

static void append_metadata(size_t *used, const artifact_t *artifact) {
    struct stat st;
    if (!is_regular_file(artifact->path, &st)) {
        appendf(used, "[!] %s: %s\n", artifact->source, artifact->path);
        appendf(used, "[!]   stat/readability: %s\n", strerror(errno));
        return;
    }

    char mtime[64];
    format_mtime(mtime, sizeof(mtime), st.st_mtime);
    appendf(used, "[+] %s: %s\n", artifact->source, artifact->path);
    appendf(used, "[i]   size=%lld bytes mtime=%s readable=%s\n",
            (long long)st.st_size, mtime, access(artifact->path, R_OK) == 0 ? "yes" : "no");
}

static void append_sanitized_bytes(size_t *used, const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len && *used + 1 < sizeof(output); i++) {
        unsigned char c = buf[i];
        if (c == '\n' || c == '\r') {
            output[(*used)++] = (char)c;
        } else if (isprint(c)) {
            output[(*used)++] = (char)c;
        } else {
            output[(*used)++] = '.';
        }
    }
    output[*used] = '\0';
}

static size_t tail_line_start(const unsigned char *buf, size_t len, int max_lines) {
    if (max_lines <= 0 || len == 0) {
        return 0;
    }

    int lines = 0;
    size_t i = len;
    while (i > 0) {
        i--;
        if (buf[i] == '\n') {
            if (i + 1 < len) {
                lines++;
            }
            if (lines >= max_lines) {
                return i + 1;
            }
        }
    }
    return 0;
}

static void append_tail(size_t *used, const artifact_t *artifact, int max_lines, long max_bytes) {
    struct stat st;
    if (!is_regular_file(artifact->path, &st)) {
        appendf(used, "[!]   preview error: %s\n", strerror(errno));
        return;
    }

    FILE *f = fopen(artifact->path, "rb");
    if (f == NULL) {
        appendf(used, "[!]   preview error: %s\n", strerror(errno));
        return;
    }

    long read_len = st.st_size < max_bytes ? (long)st.st_size : max_bytes;
    long offset = st.st_size > read_len ? (long)st.st_size - read_len : 0;
    if (fseek(f, offset, SEEK_SET) != 0) {
        appendf(used, "[!]   preview error: %s\n", strerror(errno));
        fclose(f);
        return;
    }

    unsigned char buf[MAX_TAIL_BYTES];
    size_t got = fread(buf, 1, (size_t)read_len, f);
    int read_error = ferror(f);
    fclose(f);

    if (read_error) {
        appendf(used, "[!]   preview error: read failed\n");
        return;
    }

    size_t start = tail_line_start(buf, got, max_lines);
    if (offset > 0 && start == 0) {
        while (start < got && buf[start] != '\n') {
            start++;
        }
        if (start < got && buf[start] == '\n') {
            start++;
        }
    }
    appendf(used, "[i]   tail preview begin offset=%lld bytes=%zu max_lines=%d truncated_before=%s\n",
            (long long)offset + (long long)start, got - start, max_lines,
            (offset > 0 || start > 0) ? "yes" : "no");
    append_sanitized_bytes(used, buf + start, got - start);
    if (*used > 0 && output[*used - 1] != '\n') {
        appendf(used, "\n");
    }
    appendf(used, "[i]   tail preview end\n");
}

static long parse_long_arg(const char *s, long fallback, long min_value, long max_value) {
    if (s == NULL || s[0] == '\0') {
        return fallback;
    }
    char *end = NULL;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return fallback;
    }
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void run_inventory(size_t *used, const artifact_list_t *list) {
    appendf(used, "[i] Terminal history inventory:\n");
    appendf(used, "[i] artifacts=%zu cap=%d capped=%s\n",
            list->count, MAX_ARTIFACTS, list->capped ? "yes" : "no");
    for (size_t i = 0; i < list->count; i++) {
        append_metadata(used, &list->items[i]);
    }
    if (list->count == 0) {
        appendf(used, "[i] no terminal history artifacts discovered in default current-user paths\n");
    }
}

static void run_tail(size_t *used, const artifact_list_t *list, int lines, long bytes) {
    appendf(used, "[i] Terminal history tail collection:\n");
    appendf(used, "[i] artifacts=%zu cap=%d capped=%s lines=%d bytes_per_file=%ld\n",
            list->count, MAX_ARTIFACTS, list->capped ? "yes" : "no", lines, bytes);
    for (size_t i = 0; i < list->count; i++) {
        append_metadata(used, &list->items[i]);
        append_tail(used, &list->items[i], lines, bytes);
    }
    if (list->count == 0) {
        appendf(used, "[i] no terminal history artifacts discovered in default current-user paths\n");
    }
}

static void run_read(size_t *used, const char *raw_path, long offset, long bytes, const char *home) {
    char path[PATH_MAX];
    if (!expand_user_path(path, sizeof(path), raw_path, home)) {
        appendf(used, "[!] read requires a valid path\n");
        return;
    }
    if (!is_under_home(path, home)) {
        appendf(used, "[!] read refused: path is outside current user's HOME\n");
        appendf(used, "[i] path: %s\n", path);
        return;
    }

    struct stat st;
    if (!is_regular_file(path, &st)) {
        appendf(used, "[!] read failed: %s\n", strerror(errno));
        appendf(used, "[i] path: %s\n", path);
        return;
    }
    if (offset > st.st_size) {
        offset = (long)st.st_size;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        appendf(used, "[!] read failed: %s\n", strerror(errno));
        appendf(used, "[i] path: %s\n", path);
        return;
    }
    if (fseek(f, offset, SEEK_SET) != 0) {
        appendf(used, "[!] read failed: %s\n", strerror(errno));
        fclose(f);
        return;
    }

    unsigned char buf[MAX_READ_BYTES];
    size_t got = fread(buf, 1, (size_t)bytes, f);
    int read_error = ferror(f);
    fclose(f);
    if (read_error) {
        appendf(used, "[!] read failed while reading bytes\n");
        return;
    }

    char mtime[64];
    format_mtime(mtime, sizeof(mtime), st.st_mtime);
    appendf(used, "[i] Terminal history chunk read:\n");
    appendf(used, "[+] path: %s\n", path);
    appendf(used, "[i] size=%lld mtime=%s offset=%ld requested=%ld returned=%zu truncated_after=%s\n",
            (long long)st.st_size, mtime, offset, bytes, got,
            offset + (long)got < st.st_size ? "yes" : "no");
    appendf(used, "[i] chunk begin\n");
    append_sanitized_bytes(used, buf, got);
    if (*used > 0 && output[*used - 1] != '\n') {
        appendf(used, "\n");
    }
    appendf(used, "[i] chunk end\n");
}

EXPORT char *terminal_history(int argc, char **argv) {
    const char *home = home_dir();
    artifact_list_t artifacts;
    size_t used = 0;
    output[0] = '\0';

    const char *mode = argc > 1 ? argv[1] : "tail";
    if (str_equals_i(mode, "read")) {
        if (argc <= 2) {
            appendf(&used, "[!] usage: terminal_history read <path> [offset] [bytes]\n");
            return output;
        }
        long offset = parse_long_arg(argc > 3 ? argv[3] : NULL, 0, 0, LONG_MAX);
        long bytes = parse_long_arg(argc > 4 ? argv[4] : NULL, DEFAULT_READ_BYTES, 1, MAX_READ_BYTES);
        run_read(&used, argv[2], offset, bytes, home);
        return output;
    }

    discover_artifacts(&artifacts, home);
    if (str_equals_i(mode, "inventory")) {
        run_inventory(&used, &artifacts);
    } else if (str_equals_i(mode, "tail")) {
        int lines = (int)parse_long_arg(argc > 2 ? argv[2] : NULL, DEFAULT_TAIL_LINES, 1, MAX_TAIL_LINES);
        long bytes = parse_long_arg(argc > 3 ? argv[3] : NULL, DEFAULT_TAIL_BYTES, 1, MAX_TAIL_BYTES);
        run_tail(&used, &artifacts, lines, bytes);
    } else {
        appendf(&used, "[!] unknown mode: %s\n", mode);
        appendf(&used, "[i] usage: terminal_history [inventory|tail [lines] [bytes]|read <path> [offset] [bytes]]\n");
    }

    appendf(&used, "[+] terminal_history complete\n");
    return output;
}
