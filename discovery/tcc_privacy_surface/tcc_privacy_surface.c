#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 16384

#define CSR_VALID_FLAGS 0x8ffU

static char output[OUT_SIZE];

typedef int (*csr_get_active_config_fn)(uint32_t *);

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

static int query_sip_config(uint32_t *config_out) {
    uint32_t config = 0;
    size_t size = sizeof(config);

    if (sysctlbyname("csr.active_config", &config, &size, NULL, 0) == 0) {
        *config_out = config & CSR_VALID_FLAGS;
        return 0;
    }

    void *lib = dlopen("/usr/lib/libSystem.dylib", RTLD_LAZY | RTLD_LOCAL);
    if (lib != NULL) {
        csr_get_active_config_fn fn = (csr_get_active_config_fn)dlsym(lib, "csr_get_active_config");
        if (fn != NULL) {
            config = 0;
            if (fn(&config) == 0) {
                *config_out = config & CSR_VALID_FLAGS;
                dlclose(lib);
                return 0;
            }
        }
        dlclose(lib);
    }

    return -1;
}

static int sip_score_for_config(uint32_t config) {
    if (config == 0) {
        return 3;
    }
    if ((config & CSR_VALID_FLAGS) == CSR_VALID_FLAGS) {
        return 0;
    }
    return 1;
}

static void report_sip_indicator(size_t *used, int *score) {
    uint32_t config = 0;
    if (query_sip_config(&config) != 0) {
        appendf(used, "[!] Indicator: System Integrity Protection\n");
        appendf(used, "    Provenance: csr_get_active_config / sysctl\n");
        appendf(used, "    Status: unknown\n");
        return;
    }

    int points = sip_score_for_config(config);
    *score += points;
    appendf(used, "[+] Indicator: System Integrity Protection\n");
    appendf(used, "    Provenance: csr_get_active_config / sysctl\n");
    if (config == 0) {
        appendf(used, "    Value: enabled\n");
    } else {
        appendf(used, "    Value: relaxed (config=0x%x)\n", config);
    }
    appendf(used, "    Score: +%d\n", points);
}

static int screen_time_prefs_present(const char *home, char *report_path, size_t report_path_size) {
    static const char *candidates[] = {
        "/Library/Preferences/com.apple.ScreenTimeAgent.plist",
        "/Library/Preferences/com.apple.ScreenTime.plist",
        NULL,
    };

    for (int i = 0; candidates[i] != NULL; i++) {
        char path[PATH_MAX];
        if (!build_home_path(path, sizeof(path), home, candidates[i])) {
            continue;
        }
        if (path_exists(path)) {
            snprintf(report_path, report_path_size, "%s", path);
            return 1;
        }
    }

    build_home_path(report_path, report_path_size, home,
                    "/Library/Preferences/com.apple.ScreenTimeAgent.plist");
    return 0;
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

    appendf(&used, "[+] tcc_privacy_surface\n");
    appendf(&used, "    Platform model: macOS current-user privacy/TCC posture\n");
    appendf(&used, "    Home: %s\n", home);

    char tcc_db[PATH_MAX];
    build_home_path(tcc_db, sizeof(tcc_db), home, "/Library/Application Support/com.apple.TCC/TCC.db");
    report_path_indicator(&used, &score, 2, "User TCC database", tcc_db, path_exists(tcc_db));

    report_sip_indicator(&used, &score);

    char fde_escrow[PATH_MAX];
    build_home_path(fde_escrow, sizeof(fde_escrow), home,
                    "/Library/Preferences/com.apple.preference.security.plist");
    report_path_indicator(&used, &score, 2, "FileVault user recovery escrow preferences",
                          fde_escrow, path_exists(fde_escrow));

    char screen_time[PATH_MAX];
    int screen_time_present = screen_time_prefs_present(home, screen_time, sizeof(screen_time));
    report_path_indicator(&used, &score, 2, "Screen Time preferences", screen_time, screen_time_present);

    appendf(&used, "\n[+] Posture Verdict\n");
    if (score >= 7) {
        appendf(&used, "    Privacy posture: Hardened\n");
    } else if (score >= 4) {
        appendf(&used, "    Privacy posture: Mixed\n");
    } else {
        appendf(&used, "    Privacy posture: Permissive\n");
    }
    appendf(&used, "    Score: %d/10\n", score > 10 ? 10 : score);
    appendf(&used, "    Note: query-only path checks for the current user; no TCC.db reads.\n");
    return output;
}
#endif

EXPORT char *tcc_privacy_surface(int argc, char **argv) {
    (void)argc;
    (void)argv;

#if defined(__APPLE__)
    return run_assessment();
#else
    snprintf(output, sizeof(output), "unsupported platform\n");
    return output;
#endif
}
