#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#define OUT_SIZE 32768
#define JSON_READ_MAX 262144
#define MAX_EXTENSIONS 128
#define MAX_PER_HOST 64
#define ID_MAX 128
#define VER_MAX 64
#define DISPLAY_MAX 128

static char output[OUT_SIZE];

typedef struct {
    int total;
    int hosts;
    int truncated;
} scan_stats_t;

typedef struct {
    char id[ID_MAX];
    char version[VER_MAX];
    char display[DISPLAY_MAX];
} extension_t;

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

static int build_path(char *out, size_t out_size, const char *base, const char *suffix) {
    if (out == NULL || out_size == 0 || base == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(out, out_size, "%s%s", base, suffix);
    return written > 0 && (size_t)written < out_size;
}

static int path_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

static int is_dir(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int looks_like_uuid(const char *text) {
    int dashes = 0;
    int hex = 0;
    for (size_t i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '-') {
            dashes++;
        } else if (isxdigit(c)) {
            hex++;
        } else {
            return 0;
        }
    }
    return dashes >= 4 && hex >= 32;
}

static int looks_like_extension_id(const char *text) {
    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    const char *dot = strchr(text, '.');
    if (dot == NULL || dot == text || dot[1] == '\0') {
        return 0;
    }
    if (looks_like_uuid(text)) {
        return 0;
    }
    for (const char *p = text; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int read_file_bounded(const char *path, char *buf, size_t buf_size) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, buf_size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return n > 0;
}

static int extract_json_string_after(const char *hay, const char *key, char *out, size_t out_size) {
    if (hay == NULL || key == NULL || out == NULL || out_size == 0) {
        return 0;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(hay, pattern);
    if (start == NULL) {
        return 0;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (end == NULL || (size_t)(end - start) >= out_size) {
        return 0;
    }
    memcpy(out, start, (size_t)(end - start));
    out[end - start] = '\0';
    return out[0] != '\0';
}

static int extension_seen(const extension_t *list, int count, const char *id) {
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i].id, id) == 0) {
            return 1;
        }
    }
    return 0;
}

static int add_extension(extension_t *list, int *count, scan_stats_t *stats,
                         const char *id, const char *version, const char *display) {
    if (*count >= MAX_EXTENSIONS) {
        stats->truncated = 1;
        return 0;
    }
    if (!looks_like_extension_id(id)) {
        return 0;
    }
    if (extension_seen(list, *count, id)) {
        return 0;
    }

    extension_t *ext = &list[*count];
    memset(ext, 0, sizeof(*ext));
    snprintf(ext->id, sizeof(ext->id), "%s", id);
    if (version != NULL && version[0] != '\0') {
        snprintf(ext->version, sizeof(ext->version), "%s", version);
    }
    if (display != NULL && display[0] != '\0') {
        snprintf(ext->display, sizeof(ext->display), "%s", display);
    }
    (*count)++;
    stats->total++;
    return 1;
}

static void parse_extensions_json(extension_t *list, int *count, scan_stats_t *stats,
                                  const char *json_path) {
    char *json = malloc(JSON_READ_MAX);
    if (json == NULL) {
        return;
    }
    if (!read_file_bounded(json_path, json, JSON_READ_MAX)) {
        free(json);
        return;
    }

    const char *cursor = json;
    while (*cursor != '\0' && *count < MAX_PER_HOST && stats->total < MAX_EXTENSIONS) {
        const char *id_key = strstr(cursor, "\"id\":\"");
        if (id_key == NULL) {
            break;
        }
        id_key += strlen("\"id\":\"");
        const char *id_end = strchr(id_key, '"');
        if (id_end == NULL) {
            break;
        }
        char id[ID_MAX];
        size_t id_len = (size_t)(id_end - id_key);
        if (id_len >= sizeof(id)) {
            cursor = id_end + 1;
            continue;
        }
        memcpy(id, id_key, id_len);
        id[id_len] = '\0';
        if (!looks_like_extension_id(id)) {
            cursor = id_end + 1;
            continue;
        }

        char version[VER_MAX];
        version[0] = '\0';
        const char *ver_key = strstr(id_end, "\"version\":\"");
        if (ver_key != NULL && (size_t)(ver_key - id_end) < 512) {
            ver_key += strlen("\"version\":\"");
            const char *ver_end = strchr(ver_key, '"');
            if (ver_end != NULL && (size_t)(ver_end - ver_key) < sizeof(version)) {
                memcpy(version, ver_key, (size_t)(ver_end - ver_key));
                version[ver_end - ver_key] = '\0';
            }
        }

        add_extension(list, count, stats, id, version, NULL);
        cursor = id_end + 1;
    }

    free(json);
}

static void parse_package_json(extension_t *list, int *count, scan_stats_t *stats,
                               const char *package_path, const char *folder_name) {
    char json[8192];
    if (!read_file_bounded(package_path, json, sizeof(json))) {
        return;
    }

    char id[ID_MAX];
    char publisher[ID_MAX];
    char name[ID_MAX];
    char version[VER_MAX];
    char display[DISPLAY_MAX];
    id[0] = publisher[0] = name[0] = version[0] = display[0] = '\0';

    extract_json_string_after(json, "publisher", publisher, sizeof(publisher));
    extract_json_string_after(json, "name", name, sizeof(name));
    extract_json_string_after(json, "version", version, sizeof(version));
    extract_json_string_after(json, "displayName", display, sizeof(display));

    if (publisher[0] != '\0' && name[0] != '\0') {
        snprintf(id, sizeof(id), "%s.%s", publisher, name);
    } else {
        snprintf(id, sizeof(id), "%s", folder_name);
    }

    add_extension(list, count, stats, id, version, display);
}

static void scan_extension_root(extension_t *list, int *count, scan_stats_t *stats,
                                const char *root) {
    if (!is_dir(root)) {
        return;
    }

    int host_start = *count;
    char manifest[PATH_MAX];
    int has_manifest = build_path(manifest, sizeof(manifest), root, "/extensions.json") &&
                       path_exists(manifest);
    if (has_manifest) {
        parse_extensions_json(list, count, stats, manifest);
    }

    if (has_manifest && *count > host_start) {
        stats->hosts++;
        return;
    }

    DIR *dir = opendir(root);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' || strcmp(entry->d_name, "extensions.json") == 0) {
            continue;
        }
        if (*count - host_start >= MAX_PER_HOST || stats->total >= MAX_EXTENSIONS) {
            stats->truncated = 1;
            break;
        }

        char child[PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(child) || !is_dir(child)) {
            continue;
        }

        char package[PATH_MAX];
        snprintf(package, sizeof(package), "%s/package.json", child);
        if (path_exists(package)) {
            parse_package_json(list, count, stats, package, entry->d_name);
        }
    }
    closedir(dir);

    if (*count > host_start) {
        stats->hosts++;
    }
}

static void print_extensions(size_t *used, const char *host_label, const extension_t *list,
                             int start, int end) {
    if (start >= end) {
        return;
    }
    appendf(used, "\n[+] %s (%d extensions)\n", host_label, end - start);
    for (int i = start; i < end; i++) {
        const extension_t *ext = &list[i];
        if (ext->display[0] != '\0') {
            appendf(used, "  - %s (%s)", ext->id, ext->display);
        } else {
            appendf(used, "  - %s", ext->id);
        }
        if (ext->version[0] != '\0') {
            appendf(used, " v%s", ext->version);
        }
        appendf(used, "\n");
    }
}

static void scan_host(size_t *used, extension_t *list, int *count, scan_stats_t *stats,
                      const char *home, const char *label, const char *suffix) {
    char root[PATH_MAX];
    if (!build_path(root, sizeof(root), home, suffix) || !is_dir(root)) {
        return;
    }

    int start = *count;
    scan_extension_root(list, count, stats, root);
    print_extensions(used, label, list, start, *count);
}

EXPORT char *ide_extensions(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const char *home = home_dir();
    extension_t list[MAX_EXTENSIONS];
    int count = 0;
    scan_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    memset(list, 0, sizeof(list));

    size_t used = 0;
    output[0] = '\0';

    appendf(&used, "[i] Enumerating VS Code-family IDE extensions\n");
    appendf(&used, "[i] home: %s\n", home);

    static const char *const hosts[][2] = {
        {"VS Code", "/.vscode/extensions"},
        {"VS Code Insiders", "/.vscode-insiders/extensions"},
        {"Cursor", "/.cursor/extensions"},
        {"Windsurf", "/.windsurf/extensions"},
        {"VSCodium", "/.vscode-oss/extensions"},
        {"VS Code Server", "/.vscode-server/extensions"},
        {"Cursor Server", "/.cursor-server/extensions"},
        {"VS Code Remote", "/.vscode-remote/extensions"},
    };

    for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        if (stats.total >= MAX_EXTENSIONS) {
            stats.truncated = 1;
            break;
        }
        scan_host(&used, list, &count, &stats, home, hosts[i][0], hosts[i][1]);
    }

    if (count == 0) {
        appendf(&used, "\n[i] No IDE extension directories found under the current user profile\n");
    }

    appendf(&used, "\n[i] ide_extensions summary: hosts=%d extensions=%d truncated=%s\n",
            stats.hosts, stats.total, stats.truncated ? "yes" : "no");
    appendf(&used, "[+] ide_extensions complete\n");

    return output;
}
