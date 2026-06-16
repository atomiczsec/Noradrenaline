#include <stddef.h>
#include <stdio.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 24576

static char output[OUT_SIZE];

#if defined(__APPLE__)
int wallpaper_enum_darwin(char *buf, size_t buf_size);
#endif

#if defined(__linux__)
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define READ_SIZE 65536
#define MAX_HITS 32
#define MAX_VALUE 1024

typedef struct wallpaper_report {
    size_t used;
    int hits;
    int stored;
    char seen[MAX_HITS][MAX_VALUE];
} wallpaper_report_t;

static int appendf(wallpaper_report_t *report, const char *fmt, ...) {
    if (report->used >= sizeof(output)) {
        return 0;
    }
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(output + report->used, sizeof(output) - report->used, fmt, args);
    va_end(args);
    if (written < 0) {
        return 0;
    }
    if ((size_t)written >= sizeof(output) - report->used) {
        report->used = sizeof(output) - 1;
        output[report->used] = '\0';
        return 0;
    }
    report->used += (size_t)written;
    return 1;
}

static int read_file_prefix(const char *path, char *buf, size_t buf_size, size_t *read_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, buf_size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    *read_len = n;
    return 1;
}

static int build_user_path(const char *suffix, char *path, size_t path_size) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
    int written = snprintf(path, path_size, "%s/%s", home, suffix);
    return written > 0 && (size_t)written < path_size;
}

static int value_seen(const wallpaper_report_t *report, const char *value) {
    for (int i = 0; i < report->stored; i++) {
        if (strcmp(report->seen[i], value) == 0) {
            return 1;
        }
    }
    return 0;
}

static void remember_value(wallpaper_report_t *report, const char *value) {
    if (report->stored >= MAX_HITS) {
        return;
    }
    snprintf(report->seen[report->stored], sizeof(report->seen[report->stored]), "%s", value);
    report->stored++;
}

static void trim_copy(const char *start, size_t len, char *out, size_t out_size) {
    while (len > 0 && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len > 0 && isspace((unsigned char)start[len - 1])) {
        len--;
    }
    if (len >= 2 && ((start[0] == '\'' && start[len - 1] == '\'') ||
                     (start[0] == '"' && start[len - 1] == '"'))) {
        start++;
        len -= 2;
    }
    while (len > 0 && (start[len - 1] == ';' || isspace((unsigned char)start[len - 1]))) {
        len--;
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

static int looks_like_wallpaper_value(const char *value) {
    return value != NULL &&
           (strncmp(value, "file://", 7) == 0 ||
            value[0] == '/' ||
            value[0] == '~' ||
            strstr(value, ".jpg") != NULL ||
            strstr(value, ".jpeg") != NULL ||
            strstr(value, ".png") != NULL ||
            strstr(value, ".webp") != NULL);
}

static void report_value(wallpaper_report_t *report, const char *label, const char *value, const char *provenance) {
    char cleaned[MAX_VALUE];
    trim_copy(value, strlen(value), cleaned, sizeof(cleaned));
    if (cleaned[0] == '\0' || !looks_like_wallpaper_value(cleaned) || value_seen(report, cleaned)) {
        return;
    }
    if (report->stored >= MAX_HITS) {
        return;
    }
    remember_value(report, cleaned);
    report->hits++;
    appendf(report, "[+] %s: %s\n", label, cleaned);
    appendf(report, "    Provenance: %s\n", provenance);
}

static void parse_key_values(wallpaper_report_t *report, const char *label, const char *path,
                             const char *buf, const char *key) {
    const char *cursor = buf;
    size_t key_len = strlen(key);
    while ((cursor = strstr(cursor, key)) != NULL) {
        const char *p = cursor + key_len;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p != '=') {
            cursor += key_len;
            continue;
        }
        p++;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        const char *start = p;
        char quote = '\0';
        if (*p == '\'' || *p == '"') {
            quote = *p;
            p++;
            start = p;
        }
        while (*p != '\0') {
            if ((quote != '\0' && *p == quote) ||
                (quote == '\0' && (*p == '\n' || *p == '\r'))) {
                break;
            }
            p++;
        }
        char value[MAX_VALUE];
        trim_copy(start, (size_t)(p - start), value, sizeof(value));
        report_value(report, label, value, path);
        cursor = p;
    }
}

static void parse_xml_property_values(wallpaper_report_t *report, const char *label, const char *path,
                                      const char *buf, const char *property) {
    const char *cursor = buf;
    while ((cursor = strstr(cursor, property)) != NULL) {
        const char *value = strstr(cursor, "value=");
        if (value == NULL) {
            cursor += strlen(property);
            continue;
        }
        value += 6;
        if (*value != '\'' && *value != '"') {
            cursor = value;
            continue;
        }
        char quote = *value++;
        const char *end = strchr(value, quote);
        if (end == NULL) {
            break;
        }
        char cleaned[MAX_VALUE];
        trim_copy(value, (size_t)(end - value), cleaned, sizeof(cleaned));
        report_value(report, label, cleaned, path);
        cursor = end + 1;
    }
}

static void parse_quoted_paths(wallpaper_report_t *report, const char *label, const char *path, const char *buf) {
    const char *cursor = buf;
    while (*cursor != '\0') {
        if (*cursor != '\'' && *cursor != '"') {
            cursor++;
            continue;
        }
        char quote = *cursor++;
        const char *start = cursor;
        const char *end = strchr(start, quote);
        if (end == NULL) {
            break;
        }
        char value[MAX_VALUE];
        trim_copy(start, (size_t)(end - start), value, sizeof(value));
        report_value(report, label, value, path);
        cursor = end + 1;
    }
}

static void parse_sway_backgrounds(wallpaper_report_t *report, const char *path, const char *buf) {
    const char *cursor = buf;
    while (*cursor != '\0') {
        const char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end != NULL ? (size_t)(line_end - cursor) : strlen(cursor);
        if (line_len > 0 && cursor[0] != '#') {
            const char *bg = strstr(cursor, " background ");
            size_t token_len = 12;
            if (bg == NULL || bg >= cursor + line_len) {
                bg = strstr(cursor, " bg ");
                token_len = 4;
            }
            if (bg != NULL && bg < cursor + line_len) {
                const char *value = bg + token_len;
                while (value < cursor + line_len && isspace((unsigned char)*value)) {
                    value++;
                }
                const char *end = value;
                if (end < cursor + line_len && (*end == '\'' || *end == '"')) {
                    char quote = *end++;
                    value = end;
                    while (end < cursor + line_len && *end != quote) {
                        end++;
                    }
                } else {
                    while (end < cursor + line_len && !isspace((unsigned char)*end)) {
                        end++;
                    }
                }
                char cleaned[MAX_VALUE];
                trim_copy(value, (size_t)(end - value), cleaned, sizeof(cleaned));
                report_value(report, "sway background", cleaned, path);
            }
        }
        if (line_end == NULL) {
            break;
        }
        cursor = line_end + 1;
    }
}

static void parse_text_file_keys(wallpaper_report_t *report, const char *path, const char *label,
                                 const char *const *keys) {
    char buf[READ_SIZE];
    size_t read_len = 0;
    if (!read_file_prefix(path, buf, sizeof(buf), &read_len)) {
        return;
    }
    (void)read_len;
    for (int i = 0; keys[i] != NULL; i++) {
        parse_key_values(report, label, path, buf, keys[i]);
    }
}

static void append_user_text_keys(wallpaper_report_t *report, const char *suffix, const char *label,
                                  const char *const *keys) {
    char path[PATH_MAX];
    if (!build_user_path(suffix, path, sizeof(path))) {
        return;
    }
    parse_text_file_keys(report, path, label, keys);
}

static void append_user_xml_property(wallpaper_report_t *report, const char *suffix,
                                     const char *label, const char *property) {
    char path[PATH_MAX];
    char buf[READ_SIZE];
    size_t read_len = 0;
    if (!build_user_path(suffix, path, sizeof(path)) ||
        !read_file_prefix(path, buf, sizeof(buf), &read_len)) {
        return;
    }
    (void)read_len;
    parse_xml_property_values(report, label, path, buf, property);
}

static void append_user_quoted_paths(wallpaper_report_t *report, const char *suffix, const char *label) {
    char path[PATH_MAX];
    char buf[READ_SIZE];
    size_t read_len = 0;
    if (!build_user_path(suffix, path, sizeof(path)) ||
        !read_file_prefix(path, buf, sizeof(buf), &read_len)) {
        return;
    }
    (void)read_len;
    parse_quoted_paths(report, label, path, buf);
}

static void append_user_sway_backgrounds(wallpaper_report_t *report) {
    char path[PATH_MAX];
    char buf[READ_SIZE];
    size_t read_len = 0;
    if (!build_user_path(".config/sway/config", path, sizeof(path)) ||
        !read_file_prefix(path, buf, sizeof(buf), &read_len)) {
        return;
    }
    (void)read_len;
    parse_sway_backgrounds(report, path, buf);
}

static int name_ends_with(const char *name, const char *suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    return name_len >= suffix_len && strcmp(name + name_len - suffix_len, suffix) == 0;
}

static void append_dconf_keyfiles(wallpaper_report_t *report) {
    const char *keys[] = {"picture-uri", "picture-uri-dark", NULL};
    DIR *db_dir = opendir("/etc/dconf/db");
    if (db_dir == NULL) {
        return;
    }
    struct dirent *db_entry = NULL;
    int dirs_seen = 0;
    while ((db_entry = readdir(db_dir)) != NULL && dirs_seen < 16) {
        if (db_entry->d_name[0] == '.' || !name_ends_with(db_entry->d_name, ".d")) {
            continue;
        }
        char dir_path[PATH_MAX];
        int dir_written = snprintf(dir_path, sizeof(dir_path), "/etc/dconf/db/%s", db_entry->d_name);
        if (dir_written <= 0 || (size_t)dir_written >= sizeof(dir_path)) {
            continue;
        }
        DIR *key_dir = opendir(dir_path);
        if (key_dir == NULL) {
            continue;
        }
        dirs_seen++;
        struct dirent *key_entry = NULL;
        int files_seen = 0;
        while ((key_entry = readdir(key_dir)) != NULL && files_seen < 64) {
            if (key_entry->d_name[0] == '.') {
                continue;
            }
            char path[PATH_MAX];
            int written = snprintf(path, sizeof(path), "%s/%s", dir_path, key_entry->d_name);
            if (written <= 0 || (size_t)written >= sizeof(path)) {
                continue;
            }
            parse_text_file_keys(report, path, "GNOME dconf wallpaper", keys);
            files_seen++;
        }
        closedir(key_dir);
    }
    closedir(db_dir);
}

static void scan_file_uris(wallpaper_report_t *report, const char *path, const char *label,
                           const char *buf, size_t read_len) {
    const char needle[] = "file://";
    size_t needle_len = sizeof(needle) - 1;
    for (size_t i = 0; i + needle_len < read_len; i++) {
        if (memcmp(buf + i, needle, needle_len) != 0) {
            continue;
        }
        char value[MAX_VALUE];
        size_t used = 0;
        for (size_t j = i; j < read_len && used < sizeof(value) - 1; j++) {
            unsigned char ch = (unsigned char)buf[j];
            if (ch == '\0' || ch == '\'' || ch == '"' || ch == ',' || ch < 0x20 || ch > 0x7e) {
                break;
            }
            value[used++] = (char)ch;
        }
        value[used] = '\0';
        report_value(report, label, value, path);
        i += used > 0 ? used - 1 : needle_len;
    }
}

static void append_user_dconf_blob(wallpaper_report_t *report) {
    char path[PATH_MAX];
    char buf[READ_SIZE];
    size_t read_len = 0;
    if (!build_user_path(".config/dconf/user", path, sizeof(path)) ||
        !read_file_prefix(path, buf, sizeof(buf), &read_len)) {
        return;
    }
    scan_file_uris(report, path, "GNOME user dconf blob", buf, read_len);
}

static void append_linux_wallpapers(wallpaper_report_t *report) {
    const char *kde_keys[] = {"Image", NULL};
    const char *nitrogen_keys[] = {"file", NULL};

    appendf(report, "[i] Starting wallpaper enumeration...\n");
    appendf(report, "\n[i] Querying Linux desktop environment wallpaper settings...\n");

    append_dconf_keyfiles(report);
    append_user_text_keys(report, ".config/plasma-org.kde.plasma.desktop-appletsrc", "KDE Image", kde_keys);
    append_user_xml_property(report, ".config/xfce4/xfconf/xfce-perchannel-xml/xfce4-desktop.xml",
                             "Xfce last-image", "last-image");
    append_user_quoted_paths(report, ".fehbg", "feh background");
    append_user_text_keys(report, ".config/nitrogen/bg-saved.cfg", "nitrogen file", nitrogen_keys);
    append_user_sway_backgrounds(report);
    append_user_dconf_blob(report);

    appendf(report, "\n[i] Enumeration complete\n");
    appendf(report, "    Wallpaper path hits: %d\n", report->hits);
    if (report->hits == 0) {
        appendf(report, "    No readable wallpaper paths found in supported Linux file sources.\n");
    }
}
#endif

EXPORT char *wallpaper_enum(int argc, char **argv) {
    (void)argc;
    (void)argv;
    output[0] = '\0';

#if defined(__APPLE__)
    if (wallpaper_enum_darwin(output, sizeof(output)) < 0) {
        snprintf(output, sizeof(output), "[!] macOS wallpaper enumeration failed\n");
    }
#elif defined(__linux__)
    wallpaper_report_t report;
    memset(&report, 0, sizeof(report));
    append_linux_wallpapers(&report);
#else
    snprintf(output, sizeof(output), "unsupported platform\n");
#endif

    return output;
}
