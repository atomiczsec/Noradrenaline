#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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
    return path != NULL && stat(path, &st) == 0;
}

#if defined(__APPLE__)
static int dir_has_entries(const char *path) {
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return 0;
    }
    int found = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}
#endif

#if defined(__linux__)
static int file_contains(const char *path, const char *needle) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    char buf[8193];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}
#endif

static void add_indicator(size_t *used, int *score, int points, const char *name, const char *provenance, const char *value) {
    *score += points;
    appendf(used, "[+] Indicator: %s\n", name);
    appendf(used, "    Provenance: %s\n", provenance);
    appendf(used, "    Value: %s\n", value != NULL ? value : "present");
    appendf(used, "    Score: +%d\n", points);
}

EXPORT char *mdm_policy_artifacts(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t used = 0;
    int score = 0;
    output[0] = '\0';

#if defined(__APPLE__)
    appendf(&used, "[+] mdm_policy_artifacts\n");
    appendf(&used, "    Platform model: macOS MDM/configuration profile artifacts\n");

    if (path_exists("/var/db/ConfigurationProfiles/Settings/.cloudConfigHasActivationRecord") ||
        path_exists("/var/db/ConfigurationProfiles/Settings/.cloudConfigRecordFound")) {
        add_indicator(&used, &score, 3, "Automated Device Enrollment evidence",
                      "/var/db/ConfigurationProfiles/Settings/.cloudConfig*", "present");
    }
    if (path_exists("/var/db/ConfigurationProfiles/Store/ConfigProfiles.binary") ||
        dir_has_entries("/var/db/ConfigurationProfiles/Store")) {
        add_indicator(&used, &score, 2, "Configuration profile store",
                      "/var/db/ConfigurationProfiles/Store", "present");
    }
    if (path_exists("/Library/Managed Preferences") && dir_has_entries("/Library/Managed Preferences")) {
        add_indicator(&used, &score, 2, "Managed preferences",
                      "/Library/Managed Preferences", "present");
    }
    if (path_exists("/Library/Application Support/JAMF") ||
        path_exists("/usr/local/jamf") ||
        path_exists("/Library/Intune/Microsoft Intune Agent.app") ||
        path_exists("/Library/Application Support/Microsoft/Intune")) {
        add_indicator(&used, &score, 2, "MDM/vendor management artifact",
                      "JAMF or Microsoft Intune local paths", "present");
    }
    if (path_exists("/var/db/MDM_ComputerPrefs.plist") ||
        path_exists("/Library/Preferences/com.apple.mdmclient.plist")) {
        add_indicator(&used, &score, 1, "MDM client preference artifact",
                      "MDM preference plist path", "present");
    }
#elif defined(__linux__)
    appendf(&used, "[+] mdm_policy_artifacts\n");
    appendf(&used, "    Platform model: Linux endpoint-management artifacts (best effort)\n");

    if (path_exists("/etc/opt/omi") || path_exists("/var/opt/microsoft/mdatp") ||
        path_exists("/etc/mdatp") || path_exists("/opt/microsoft/mdatp")) {
        add_indicator(&used, &score, 3, "Microsoft management/security agent evidence",
                      "OMI or Microsoft Defender for Endpoint paths", "present");
    }
    if (path_exists("/opt/puppetlabs") || path_exists("/etc/puppetlabs") ||
        path_exists("/etc/chef") || path_exists("/opt/chef")) {
        add_indicator(&used, &score, 2, "Configuration management agent evidence",
                      "Puppet or Chef paths", "present");
    }
    if (path_exists("/etc/landscape") || path_exists("/var/lib/landscape") ||
        path_exists("/etc/salt") || path_exists("/opt/saltstack")) {
        add_indicator(&used, &score, 2, "Fleet management agent evidence",
                      "Landscape or Salt paths", "present");
    }
    if (file_contains("/etc/sssd/sssd.conf", "id_provider") ||
        path_exists("/etc/realmd.conf")) {
        add_indicator(&used, &score, 1, "Directory enrollment configuration",
                      "/etc/sssd/sssd.conf or /etc/realmd.conf", "present");
    }
    if (path_exists("/etc/audit") || path_exists("/etc/auditd.conf")) {
        add_indicator(&used, &score, 1, "Managed audit policy evidence",
                      "/etc/audit or /etc/auditd.conf", "present");
    }
#else
    appendf(&used, "unsupported platform\n");
#endif

    appendf(&used, "\n[+] Posture Verdict\n");
    if (score >= 7) {
        appendf(&used, "    MDM posture: Enrolled or strongly managed\n");
    } else if (score >= 4) {
        appendf(&used, "    MDM posture: Partially enrolled or managed\n");
    } else {
        appendf(&used, "    MDM posture: Not enrolled or not locally observable\n");
    }
    appendf(&used, "    Score: %d/10\n", score > 10 ? 10 : score);
    appendf(&used, "    Note: reports only locally observable artifacts.\n");
    return output;
}
