#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 16384

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

static int read_first_match(const char *path, const char *needle, char *line, size_t line_size) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    char buf[512];
    int found = 0;
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (needle == NULL || strstr(buf, needle) != NULL) {
            buf[strcspn(buf, "\r\n")] = '\0';
            snprintf(line, line_size, "%s", buf);
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

static void append_file_signal(size_t *used, int *signals, const char *label, const char *path, const char *needle) {
    char line[512] = {0};
    if (read_first_match(path, needle, line, sizeof(line))) {
        (*signals)++;
        appendf(used, "    %s: %s\n", label, line);
        appendf(used, "    Provenance: %s\n", path);
    } else if (path_exists(path)) {
        (*signals)++;
        appendf(used, "    %s: present\n", label);
        appendf(used, "    Provenance: %s\n", path);
    }
}

EXPORT char *netjoin_query(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t used = 0;
    int signals = 0;
    char hostname[256] = {0};
    struct utsname uts;
    output[0] = '\0';

    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
        snprintf(hostname, sizeof(hostname), "(unknown)");
    }

    appendf(&used, "[*] Querying join information...\n");

#if defined(__APPLE__)
    appendf(&used, "Join Type: macOS directory binding evidence\n");
    appendf(&used, "Computer Name: %s\n", hostname);
    append_file_signal(&used, &signals, "Active Directory config", "/Library/Preferences/DirectoryService/ActiveDirectory.plist", NULL);
    append_file_signal(&used, &signals, "OpenDirectory Active Directory node", "/Library/Preferences/OpenDirectory/Configurations/Active Directory", NULL);
    append_file_signal(&used, &signals, "Kerberos realm", "/etc/krb5.conf", "default_realm");
    append_file_signal(&used, &signals, "DirectoryService preference", "/Library/Preferences/com.apple.DirectoryService.plist", NULL);
#elif defined(__linux__)
    appendf(&used, "Join Type: Linux domain/realm evidence\n");
    appendf(&used, "Computer Name: %s\n", hostname);
    append_file_signal(&used, &signals, "SSSD domain", "/etc/sssd/sssd.conf", "domains");
    append_file_signal(&used, &signals, "Realmd config", "/etc/realmd.conf", NULL);
    append_file_signal(&used, &signals, "Kerberos realm", "/etc/krb5.conf", "default_realm");
    append_file_signal(&used, &signals, "Samba workgroup/realm", "/etc/samba/smb.conf", "workgroup");
    append_file_signal(&used, &signals, "Machine account", "/etc/krb5.keytab", NULL);
#else
    appendf(&used, "unsupported platform\n");
#endif

    if (uname(&uts) == 0) {
        appendf(&used, "OS Version: %s %s %s\n", uts.sysname, uts.release, uts.machine);
    }
    if (signals == 0) {
        appendf(&used, "Join Name: Workgroup/standalone or not locally observable\n");
    } else {
        appendf(&used, "Join Evidence Count: %d\n", signals);
    }
    appendf(&used, "[*] query completed\n");
    return output;
}
