#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 8192

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

#if defined(__linux__)
static int read_value_after_key(const char *path, const char *key, char *value, size_t value_size) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    char line[512];
    int found = 0;
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, key, key_len) == 0 &&
            (line[key_len] == '=' || line[key_len] == ' ' ||
             line[key_len] == '\t' || line[key_len] == '\0')) {
            char *start = line + key_len;
            while (*start == ' ' || *start == '\t' || *start == '=' || *start == '"' || *start == '>') {
                start++;
            }
            char *end = start + strlen(start);
            while (end > start && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == '"' || end[-1] == '<' || end[-1] == '/' || end[-1] == ' ')) {
                end--;
            }
            *end = '\0';
            snprintf(value, value_size, "%s", start);
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}
#endif

#if defined(__APPLE__)
static int plist_string_value(const char *path, const char *key, char *value, size_t value_size) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    char prev[256] = {0};
    char line[512];
    int found = 0;
    char needle[256];
    snprintf(needle, sizeof(needle), "<key>%s</key>", key);
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(prev, needle) != NULL) {
            char *start = strstr(line, "<string>");
            char *end = strstr(line, "</string>");
            if (start != NULL && end != NULL && end > start) {
                start += strlen("<string>");
                *end = '\0';
                snprintf(value, value_size, "%s", start);
                found = 1;
                break;
            }
        }
        snprintf(prev, sizeof(prev), "%s", line);
    }
    fclose(fp);
    return found;
}
#endif

static void append_install_date(size_t *used, const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        appendf(used, "    InstallDate: %ld (Unix Timestamp, from %s)\n", (long)st.st_mtime, path);
    } else {
        appendf(used, "    InstallDate: N/A\n");
    }
}

EXPORT char *os_version(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t used = 0;
    struct utsname uts;
    output[0] = '\0';
    appendf(&used, "[+] os_version\n");

#if defined(__APPLE__)
    char product[256] = {0};
    char version[128] = {0};
    char build[128] = {0};
    const char *plist = "/System/Library/CoreServices/SystemVersion.plist";
    plist_string_value(plist, "ProductName", product, sizeof(product));
    plist_string_value(plist, "ProductVersion", version, sizeof(version));
    plist_string_value(plist, "ProductBuildVersion", build, sizeof(build));
    appendf(&used, "    Product:    %s (%s)\n", product[0] ? product : "macOS", version[0] ? version : "N/A");
    appendf(&used, "    Build:      %s\n", build[0] ? build : "N/A");
    appendf(&used, "    Edition:    macOS / Client\n");
#elif defined(__linux__)
    char pretty[256] = {0};
    char version_id[128] = {0};
    read_value_after_key("/etc/os-release", "PRETTY_NAME", pretty, sizeof(pretty));
    read_value_after_key("/etc/os-release", "VERSION_ID", version_id, sizeof(version_id));
    appendf(&used, "    Product:    %s (%s)\n", pretty[0] ? pretty : "Linux", version_id[0] ? version_id : "N/A");
    appendf(&used, "    Build:      N/A\n");
    appendf(&used, "    Edition:    Distribution-defined / Client or Server\n");
#else
    appendf(&used, "unsupported platform\n");
#endif

    if (uname(&uts) == 0) {
        appendf(&used, "    Kernel:     %s %s\n", uts.sysname, uts.release);
        appendf(&used, "    Arch:       %s\n", uts.machine);
    } else {
        appendf(&used, "    Arch:       N/A\n");
    }
#if defined(__APPLE__)
    append_install_date(&used, "/var/db/.AppleSetupDone");
#elif defined(__linux__)
    append_install_date(&used, "/");
#endif
    return output;
}
