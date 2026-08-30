#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int build_home_path(char *dest, size_t dest_size, const char *home, const char *suffix) {
    if (home == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(dest, dest_size, "%s%s", home, suffix);
    return written > 0 && (size_t)written < dest_size;
}

static void report_path_indicator(size_t *used, int *score, int points_if_present,
                                  const char *name, const char *path, int present) {
    if (present) {
        *score += points_if_present;
        appendf(used, "[+] Indicator: %s\n", name);
        appendf(used, "    Path: %s\n", path);
        appendf(used, "    Status: present\n");
        appendf(used, "    Score: +%d\n", points_if_present);
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
    report_path_indicator(&used, &score, 4, "User LaunchAgents directory",
                          launch_agents, path_exists(launch_agents));

    char launch_daemons[PATH_MAX];
    build_home_path(launch_daemons, sizeof(launch_daemons), home, "/Library/LaunchDaemons");
    report_path_indicator(&used, &score, 4, "User LaunchDaemons directory",
                          launch_daemons, path_exists(launch_daemons));

    appendf(&used, "\n[+] Posture Verdict\n");
    if (score > 0) {
        appendf(&used, "    User persistence surface: present\n");
    } else {
        appendf(&used, "    User persistence surface: absent\n");
    }
    appendf(&used, "    Score: %d/10\n", score > 10 ? 10 : score);
    appendf(&used, "    Note: directory existence only; no launchctl, listings, or plist reads.\n");
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
