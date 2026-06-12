#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 8192
#define MAX_SEEN 2048

static char output[OUT_SIZE];

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

static unsigned long hash_ci(const char *s) {
    unsigned long h = 5381;
    for (size_t i = 0; s != NULL && s[i] != '\0' && i < 512; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') {
            c = (unsigned char)(c + ('a' - 'A'));
        }
        h = ((h << 5) + h) + c;
    }
    return h;
}

static int has_suffix(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    return text_len >= suffix_len && strcmp(text + text_len - suffix_len, suffix) == 0;
}

static int remember(unsigned long *seen, int *seen_count, const char *name) {
    unsigned long h = hash_ci(name);
    for (int i = 0; i < *seen_count; i++) {
        if (seen[i] == h) {
            return 0;
        }
    }
    if (*seen_count < MAX_SEEN) {
        seen[*seen_count] = h;
        (*seen_count)++;
    }
    return 1;
}

#if defined(__APPLE__)
static int count_app_bundle(unsigned long *seen, int *seen_count, int *scanned, const char *name) {
    (*scanned)++;
    return remember(seen, seen_count, name);
}

static int count_macos_apps_in_dir(const char *path, int nested, unsigned long *seen, int *seen_count, int *scanned) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    int added = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }

        if (has_suffix(entry->d_name, ".app")) {
            added += count_app_bundle(seen, seen_count, scanned, entry->d_name);
            continue;
        }

        if (!nested) {
            continue;
        }

        DIR *subdir = opendir(full);
        if (subdir == NULL) {
            continue;
        }

        struct dirent *subentry = NULL;
        while ((subentry = readdir(subdir)) != NULL) {
            if (subentry->d_name[0] == '.' || !has_suffix(subentry->d_name, ".app")) {
                continue;
            }

            char subfull[PATH_MAX];
            snprintf(subfull, sizeof(subfull), "%s/%s", full, subentry->d_name);
            struct stat subst;
            if (stat(subfull, &subst) != 0 || !S_ISDIR(subst.st_mode)) {
                continue;
            }

            added += count_app_bundle(seen, seen_count, scanned, subentry->d_name);
        }

        closedir(subdir);
    }

    closedir(dir);
    return added;
}
#endif

#if defined(__linux__)
static int count_entries(const char *path, const char *suffix, int require_dir, unsigned long *seen, int *seen_count, int *scanned) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }

    int added = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!has_suffix(entry->d_name, suffix)) {
            continue;
        }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }
        if (require_dir && !S_ISDIR(st.st_mode)) {
            continue;
        }
        if (!require_dir && !S_ISREG(st.st_mode)) {
            continue;
        }
        (*scanned)++;
        added += remember(seen, seen_count, entry->d_name);
    }

    closedir(dir);
    return added;
}
#endif

EXPORT char *app_count(int argc, char **argv) {
    (void)argc;
    (void)argv;

    unsigned long seen[MAX_SEEN] = {0};
    int seen_count = 0;
    int scanned = 0;
    int readable = 0;
    size_t used = 0;
    output[0] = '\0';

#if defined(__APPLE__)
    const char *paths[] = {
        "/Applications",
        "/System/Applications",
        "/System/Library/CoreServices/Applications",
        NULL
    };
    const char *home = getenv("HOME");
    appendf(&used, "[+] app_count\n");
    appendf(&used, "    Source: macOS .app bundles\n");
    for (int i = 0; paths[i] != NULL; i++) {
        int before = seen_count;
        int nested = strcmp(paths[i], "/Applications") == 0;
        int result = count_macos_apps_in_dir(paths[i], nested, seen, &seen_count, &scanned);
        if (result >= 0) {
            readable++;
            appendf(&used, "    Path: %s unique_added=%d\n", paths[i], seen_count - before);
        }
    }
    if (home != NULL && home[0] != '\0') {
        char user_apps[PATH_MAX];
        snprintf(user_apps, sizeof(user_apps), "%s/Applications", home);
        int before = seen_count;
        int result = count_macos_apps_in_dir(user_apps, 0, seen, &seen_count, &scanned);
        if (result >= 0) {
            readable++;
            appendf(&used, "    Path: %s unique_added=%d\n", user_apps, seen_count - before);
        }
    }
#elif defined(__linux__)
    const char *paths[] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL
    };
    const char *home = getenv("HOME");
    appendf(&used, "[+] app_count\n");
    appendf(&used, "    Source: Linux .desktop launchers\n");
    for (int i = 0; paths[i] != NULL; i++) {
        int before = seen_count;
        int result = count_entries(paths[i], ".desktop", 0, seen, &seen_count, &scanned);
        if (result >= 0) {
            readable++;
            appendf(&used, "    Path: %s unique_added=%d\n", paths[i], seen_count - before);
        }
    }
    if (home != NULL && home[0] != '\0') {
        char user_apps[PATH_MAX];
        snprintf(user_apps, sizeof(user_apps), "%s/.local/share/applications", home);
        int before = seen_count;
        int result = count_entries(user_apps, ".desktop", 0, seen, &seen_count, &scanned);
        if (result >= 0) {
            readable++;
            appendf(&used, "    Path: %s unique_added=%d\n", user_apps, seen_count - before);
        }
    }
#else
    appendf(&used, "unsupported platform\n");
#endif

    appendf(&used, "    Readable locations: %d\n", readable);
    appendf(&used, "    Entries scanned: %d\n", scanned);
    appendf(&used, "    Unique applications: %d\n", seen_count);
    if (seen_count >= MAX_SEEN) {
        appendf(&used, "    Note: unique tracking cap reached at %d\n", MAX_SEEN);
    }
    return output;
}
