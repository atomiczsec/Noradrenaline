#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 32768
#define MAX_RULE_LINES 200

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

static int str_equals_ci(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int ruleish_line(const char *line) {
    return contains_ci(line, "pass ") || contains_ci(line, "block ") ||
           contains_ci(line, "anchor ") || contains_ci(line, "table ") ||
           contains_ci(line, "iptables") || contains_ci(line, "-A ") ||
           contains_ci(line, "nft ") || contains_ci(line, "chain ") ||
           contains_ci(line, "rule ") || contains_ci(line, "allow ") ||
           contains_ci(line, "deny ") || contains_ci(line, "reject ") ||
           contains_ci(line, "drop ");
}

static int scan_file(size_t *used, const char *path, const char *query, int list_mode, int *shown, int *matches) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (path_exists(path)) {
            appendf(used, "[!] Present but unreadable: %s\n", path);
        }
        return 0;
    }

    char line[1024];
    int local_matches = 0;
    int line_no = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_no++;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        int is_match = list_mode ? ruleish_line(line) : contains_ci(line, query);
        if (!is_match) {
            continue;
        }
        if (*shown < MAX_RULE_LINES) {
            appendf(used, "[+] %s:%d: %s\n", path, line_no, line);
            (*shown)++;
        }
        local_matches++;
        (*matches)++;
    }
    fclose(fp);
    if (local_matches > 0) {
        appendf(used, "[i] Source matches: %s (%d)\n", path, local_matches);
    }
    return local_matches;
}

static void print_usage(size_t *used) {
    appendf(used, "[i] firewall_rule <list|query|add|remove> <args...>\n");
    appendf(used, "[i] list: query readable firewall configuration sources, capped at %d rows\n", MAX_RULE_LINES);
    appendf(used, "[i] query: <name-or-pattern>\n");
    appendf(used, "[i] add/remove: not supported in v1 query-only build\n");
}

EXPORT char *firewall_rule(int argc, char **argv) {
    size_t used = 0;
    int shown = 0;
    int matches = 0;
    output[0] = '\0';

    if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
        print_usage(&used);
        return output;
    }

    const char *subcmd = argv[1];
    if (str_equals_ci(subcmd, "add") || str_equals_ci(subcmd, "remove")) {
        appendf(&used, "[-] firewall_rule %s is not supported in v1 query-only build.\n", subcmd);
        appendf(&used, "    Use list/query for local firewall posture. Add/remove requires a backend-specific implementation and privileges.\n");
        return output;
    }
    if (!str_equals_ci(subcmd, "list") && !str_equals_ci(subcmd, "query")) {
        print_usage(&used);
        return output;
    }

    int list_mode = str_equals_ci(subcmd, "list");
    const char *query = argc > 2 && argv[2] != NULL ? argv[2] : "";
    if (!list_mode && query[0] == '\0') {
        appendf(&used, "usage: firewall_rule query <name-or-pattern>\n");
        return output;
    }

    appendf(&used, "[i] Enumerating readable firewall configuration sources (query-only v1)\n");
#if defined(__APPLE__)
    appendf(&used, "[i] Backend hints: PF and Application Firewall files; live pfctl state is not queried because this module does not spawn tools.\n");
    const char *paths[] = {
        "/etc/pf.conf",
        "/etc/pf.anchors/com.apple",
        "/Library/Preferences/com.apple.alf.plist",
        "/Library/Preferences/com.apple.alf.plist.lockfile",
        NULL
    };
#elif defined(__linux__)
    appendf(&used, "[i] Backend hints: nftables, iptables-persistent, ufw, and firewalld config files.\n");
    const char *paths[] = {
        "/etc/nftables.conf",
        "/etc/iptables/rules.v4",
        "/etc/iptables/rules.v6",
        "/etc/ufw/user.rules",
        "/etc/ufw/user6.rules",
        "/etc/firewalld/firewalld.conf",
        NULL
    };
#else
    const char *paths[] = {NULL};
    appendf(&used, "unsupported platform\n");
#endif

    for (int i = 0; paths[i] != NULL; i++) {
        scan_file(&used, paths[i], query, list_mode, &shown, &matches);
    }

    appendf(&used, "[i] Matches: %d\n", matches);
    if (shown >= MAX_RULE_LINES) {
        appendf(&used, "[i] Output capped at %d displayed rows\n", MAX_RULE_LINES);
    }
    if (matches == 0) {
        appendf(&used, "[-] No readable firewall rule matches found.\n");
    }
    appendf(&used, "[i] Note: elevated rights may be required to inspect complete live firewall state.\n");
    return output;
}
