#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 24576
#define PREVIEW_SIZE 4096

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

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int contains_ci(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    size_t needle_len = strlen(needle);
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (j < needle_len && haystack[i + j] != '\0' &&
               tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int read_prefix(const char *path, char *buf, size_t buf_size) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    size_t n = fread(buf, 1, buf_size - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\0') {
            buf[i] = ' ';
        }
    }
    return 1;
}

static void append_env_proxy(size_t *used, int *hits) {
    const char *names[] = {
        "http_proxy", "https_proxy", "all_proxy", "no_proxy",
        "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY",
        NULL
    };
    appendf(used, "\n[i] Querying proxy environment variables...\n");
    for (int i = 0; names[i] != NULL; i++) {
        const char *value = getenv(names[i]);
        if (value != NULL && value[0] != '\0') {
            (*hits)++;
            appendf(used, "  - %s=%s\n", names[i], value);
        }
    }
}

static void append_file_proxy(size_t *used, int *hits, const char *label, const char *path) {
    char buf[PREVIEW_SIZE];
    if (!read_prefix(path, buf, sizeof(buf))) {
        return;
    }
    if (contains_ci(buf, "proxy") || contains_ci(buf, "wpad") || contains_ci(buf, "autoconfig") || contains_ci(buf, "pac")) {
        (*hits)++;
        appendf(used, "  - %s: proxy indicators present\n", label);
        appendf(used, "    Provenance: %s\n", path);
    } else {
        appendf(used, "  - %s: readable, no obvious proxy indicators\n", label);
        appendf(used, "    Provenance: %s\n", path);
    }
}

static void append_user_path_file(size_t *used, int *hits, const char *label, const char *suffix) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return;
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", home, suffix);
    append_file_proxy(used, hits, label, path);
}

EXPORT char *proxy_enum(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t used = 0;
    int hits = 0;
    output[0] = '\0';
    appendf(&used, "[i] Starting proxy enumeration...\n");

    append_env_proxy(&used, &hits);

#if defined(__APPLE__)
    appendf(&used, "\n[i] Querying macOS SystemConfiguration proxy stores...\n");
    append_file_proxy(&used, &hits, "Network preferences", "/Library/Preferences/SystemConfiguration/preferences.plist");
    append_file_proxy(&used, &hits, "Global preferences", "/Library/Preferences/.GlobalPreferences.plist");
    append_user_path_file(&used, &hits, "User global preferences", "Library/Preferences/.GlobalPreferences.plist");

    appendf(&used, "\n[i] Checking browser policy/profile proxy settings...\n");
    append_file_proxy(&used, &hits, "Chrome managed policy", "/Library/Managed Preferences/com.google.Chrome.plist");
    append_file_proxy(&used, &hits, "Chrome policy", "/Library/Google/Chrome/Managed Preferences/com.google.Chrome.plist");
    append_user_path_file(&used, &hits, "Chrome profile preferences", "Library/Application Support/Google/Chrome/Default/Preferences");
    append_user_path_file(&used, &hits, "Firefox prefs", "Library/Application Support/Firefox/Profiles/profiles.ini");
#elif defined(__linux__)
    appendf(&used, "\n[i] Querying Linux desktop and package proxy settings...\n");
    append_file_proxy(&used, &hits, "APT proxy config", "/etc/apt/apt.conf");
    append_file_proxy(&used, &hits, "APT proxy snippets", "/etc/apt/apt.conf.d/01proxy");
    append_file_proxy(&used, &hits, "DNF config", "/etc/dnf/dnf.conf");
    append_file_proxy(&used, &hits, "YUM config", "/etc/yum.conf");
    append_file_proxy(&used, &hits, "Wget config", "/etc/wgetrc");
    append_file_proxy(&used, &hits, "System environment", "/etc/environment");
    append_user_path_file(&used, &hits, "User wget config", ".wgetrc");
    append_user_path_file(&used, &hits, "User curl config", ".curlrc");

    appendf(&used, "\n[i] Checking browser policy/profile proxy settings...\n");
    append_file_proxy(&used, &hits, "Chrome managed policy", "/etc/opt/chrome/policies/managed/proxy.json");
    append_file_proxy(&used, &hits, "Chromium managed policy", "/etc/chromium/policies/managed/proxy.json");
    append_user_path_file(&used, &hits, "Chrome profile preferences", ".config/google-chrome/Default/Preferences");
    append_user_path_file(&used, &hits, "Chromium profile preferences", ".config/chromium/Default/Preferences");
#else
    appendf(&used, "unsupported platform\n");
#endif

    if (path_exists("/etc/proxychains.conf")) {
        hits++;
        appendf(&used, "\n[i] Proxy tooling artifact present: /etc/proxychains.conf\n");
    }
    appendf(&used, "\n[+] Proxy enumeration complete\n");
    appendf(&used, "    Proxy indicator hits: %d\n", hits);
    return output;
}
