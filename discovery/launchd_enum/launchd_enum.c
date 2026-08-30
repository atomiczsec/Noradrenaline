#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 16384
#define MAX_LAUNCH_ITEMS 48
#define PLIST_PREVIEW_SIZE 8192

static char output[OUT_SIZE];

struct launch_item {
    char name[NAME_MAX + 1];
    char path[PATH_MAX];
    struct stat st;
};

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

static int stat_path(const char *path, struct stat *st_out) {
    if (path == NULL || st_out == NULL) {
        return 0;
    }
    return stat(path, st_out) == 0;
}

static int build_home_path(char *dest, size_t dest_size, const char *home, const char *suffix) {
    if (home == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(dest, dest_size, "%s%s", home, suffix);
    return written > 0 && (size_t)written < dest_size;
}

static int has_plist_suffix(const char *name) {
    size_t length = strlen(name);
    return length > 6 && strcmp(name + length - 6, ".plist") == 0;
}

static int compare_launch_items(const void *left, const void *right) {
    const struct launch_item *a = left;
    const struct launch_item *b = right;
    return strcmp(a->name, b->name);
}

static void format_mtime(char *dest, size_t dest_size, time_t timestamp) {
    struct tm tm;
    if (localtime_r(&timestamp, &tm) != NULL &&
        strftime(dest, dest_size, "%Y-%m-%d %H:%M:%S %Z", &tm) > 0) {
        return;
    }
    snprintf(dest, dest_size, "epoch %lld", (long long)timestamp);
}

static int read_file_preview(const char *path, char *dest, size_t dest_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t read_count = fread(dest, 1, dest_size - 1, file);
    fclose(file);
    dest[read_count] = '\0';
    return 1;
}

static int extract_plist_string(const char *contents, const char *key, char *dest, size_t dest_size) {
    char key_tag[160];
    int key_length = snprintf(key_tag, sizeof(key_tag), "<key>%s</key>", key);
    if (key_length < 0 || (size_t)key_length >= sizeof(key_tag)) {
        return 0;
    }
    const char *start = strstr(contents, key_tag);
    if (start == NULL || (start = strstr(start + key_length, "<string>")) == NULL) {
        return 0;
    }
    start += strlen("<string>");
    const char *end = strstr(start, "</string>");
    if (end == NULL || end <= start) {
        return 0;
    }
    size_t length = (size_t)(end - start);
    if (length >= dest_size) {
        length = dest_size - 1;
    }
    memcpy(dest, start, length);
    dest[length] = '\0';
    return 1;
}

static void report_launch_items(size_t *used, const char *directory, const char *name) {
    DIR *dir = opendir(directory);
    if (dir == NULL) {
        return;
    }
    struct launch_item items[MAX_LAUNCH_ITEMS];
    size_t item_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && item_count < MAX_LAUNCH_ITEMS) {
        if (entry->d_name[0] == '.' || !has_plist_suffix(entry->d_name)) {
            continue;
        }
        struct launch_item *item = &items[item_count];
        int path_length = snprintf(item->path, sizeof(item->path), "%s/%s", directory, entry->d_name);
        if (path_length < 0 || (size_t)path_length >= sizeof(item->path) ||
            stat(item->path, &item->st) != 0 || !S_ISREG(item->st.st_mode)) {
            continue;
        }
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        item_count++;
    }
    closedir(dir);

    qsort(items, item_count, sizeof(items[0]), compare_launch_items);
    appendf(used, "\n[+] %s inventory\n", name);
    appendf(used, "    Directory: %s\n", directory);
    appendf(used, "    Plist files: %zu%s\n", item_count,
            item_count == MAX_LAUNCH_ITEMS ? " (output limit reached)" : "");
    if (item_count == 0) {
        appendf(used, "    No .plist files found.\n");
        return;
    }

    for (size_t i = 0; i < item_count; i++) {
        char modified[64];
        char preview[PLIST_PREVIEW_SIZE];
        char label[512];
        char program[PATH_MAX];
        format_mtime(modified, sizeof(modified), items[i].st.st_mtime);
        appendf(used, "\n    [%zu] %s\n", i + 1, items[i].name);
        appendf(used, "        Path: %s\n", items[i].path);
        appendf(used, "        Modified: %s (epoch %lld)\n", modified, (long long)items[i].st.st_mtime);
        if (!read_file_preview(items[i].path, preview, sizeof(preview))) {
            appendf(used, "        Metadata: unreadable\n");
            continue;
        }
        if (extract_plist_string(preview, "Label", label, sizeof(label))) {
            appendf(used, "        Label: %s\n", label);
        } else {
            appendf(used, "        Label: unavailable (binary or nonstandard plist)\n");
        }
        if (extract_plist_string(preview, "Program", program, sizeof(program))) {
            appendf(used, "        Program: %s\n", program);
        } else if (extract_plist_string(preview, "ProgramArguments", program, sizeof(program))) {
            appendf(used, "        ProgramArguments[0]: %s\n", program);
        }
    }
}

static void report_path_indicator(size_t *used, int *score, int points_if_present,
                                  const char *name, const char *path) {
    struct stat st;
    int present = stat_path(path, &st);

    if (present) {
        *score += points_if_present;
        appendf(used, "[+] Indicator: %s\n", name);
        appendf(used, "    Path: %s\n", path);
        appendf(used, "    Status: present\n");
        appendf(used, "    Score: +%d\n", points_if_present);

        char time_buf[64];
        format_mtime(time_buf, sizeof(time_buf), st.st_mtime);
        appendf(used, "    Modified: %s (epoch %lld)\n", time_buf, (long long)st.st_mtime);
        return;
    }

    appendf(used, "[i] Indicator: %s\n", name);
    appendf(used, "    Path: %s\n", path);
    appendf(used, "    Status: absent\n");
}

#if defined(__APPLE__)
static char *run_assessment(void) {
    size_t used = 0;
    int score = 0;
    output[0] = '\0';

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        appendf(&used, "[!] HOME is not set; current-user paths cannot be resolved\n");
        return output;
    }

    appendf(&used, "[+] launchd_enum\n");
    appendf(&used, "    Platform model: macOS current-user launchd persistence surface\n");
    appendf(&used, "    Home: %s\n", home);

    char launch_agents[PATH_MAX];
    build_home_path(launch_agents, sizeof(launch_agents), home, "/Library/LaunchAgents");
    report_path_indicator(&used, &score, 4, "User LaunchAgents directory", launch_agents);
    struct stat launch_agents_st;
    if (stat_path(launch_agents, &launch_agents_st)) {
        report_launch_items(&used, launch_agents, "User LaunchAgents");
    }

    char launch_daemons[PATH_MAX];
    build_home_path(launch_daemons, sizeof(launch_daemons), home, "/Library/LaunchDaemons");
    report_path_indicator(&used, &score, 4, "User LaunchDaemons directory", launch_daemons);
    struct stat launch_daemons_st;
    if (stat_path(launch_daemons, &launch_daemons_st)) {
        report_launch_items(&used, launch_daemons, "User LaunchDaemons");
    }

    appendf(&used, "\n[+] Posture Verdict\n");
    if (score > 0) {
        appendf(&used, "    User persistence surface: present\n");
    } else {
        appendf(&used, "    User persistence surface: absent\n");
    }
    appendf(&used, "    Score: %d/10\n", score > 10 ? 10 : score);
    appendf(&used, "    Note: inventories up to %d current-user plist files per directory; no launchctl invocation.\n", MAX_LAUNCH_ITEMS);
    return output;
}
#endif

EXPORT char *launchd_enum(int argc, char **argv) {
    (void)argc;
    (void)argv;

#if defined(__APPLE__)
    return run_assessment();
#else
    snprintf(output, sizeof(output), "unsupported platform\n");
    return output;
#endif
}
