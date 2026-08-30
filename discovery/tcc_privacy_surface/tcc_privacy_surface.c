#include <dlfcn.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <time.h>
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

static int stat_path(const char *path, struct stat *st) {
    return path != NULL && st != NULL && stat(path, st) == 0;
}

static int path_exists(const char *path) {
    struct stat st;
    return stat_path(path, &st);
}

static int build_home_path(char *dest, size_t dest_size, const char *home, const char *suffix) {
    if (home == NULL || suffix == NULL) {
        return 0;
    }
    int written = snprintf(dest, dest_size, "%s%s", home, suffix);
    return written > 0 && (size_t)written < dest_size;
}

static void report_file_details(size_t *used, const struct stat *st) {
    char modified[64];
    struct tm local_time;
    if (localtime_r(&st->st_mtime, &local_time) != NULL &&
        strftime(modified, sizeof(modified), "%Y-%m-%d %H:%M:%S %Z", &local_time) > 0) {
        appendf(used, "    Modified: %s (epoch %lld)\n", modified, (long long)st->st_mtime);
    } else {
        appendf(used, "    Modified: epoch %lld\n", (long long)st->st_mtime);
    }
    appendf(used, "    Type: %s\n", S_ISREG(st->st_mode) ? "regular file" :
                                      S_ISDIR(st->st_mode) ? "directory" : "other");
    appendf(used, "    Size: %lld bytes\n", (long long)st->st_size);
    appendf(used, "    Owner UID: %u\n", st->st_uid);
    appendf(used, "    Mode: %04o\n", st->st_mode & 07777);
}

static void report_path_indicator(size_t *used, int *score, int points_if_present,
                                  const char *name, const char *path, const char *meaning) {
    struct stat st;
    int present = stat_path(path, &st);
    if (present) {
        *score += points_if_present;
        appendf(used, "[+] Indicator: %s\n", name);
        appendf(used, "    Path: %s\n", path);
        appendf(used, "    Status: present\n");
        report_file_details(used, &st);
        appendf(used, "    Score: +%d\n", points_if_present);
    } else {
        appendf(used, "[i] Indicator: %s\n", name);
        appendf(used, "    Path: %s\n", path);
        appendf(used, "    Status: absent\n");
    }
    appendf(used, "    Meaning: %s\n", meaning);
}

static void report_sip_exceptions(size_t *used, uint32_t config) {
    static const struct {
        uint32_t flag;
        const char *name;
    } exceptions[] = {
        {0x001U, "unrestricted filesystem"},
        {0x002U, "unrestricted kernel extensions"},
        {0x004U, "task_for_pid allowed"},
        {0x008U, "kernel debugger allowed"},
        {0x010U, "Apple-internal mode"},
        {0x020U, "unrestricted DTrace"},
        {0x040U, "unrestricted NVRAM"},
        {0x080U, "device configuration allowed"},
        {0x800U, "unrestricted authenticated root"},
    };

    appendf(used, "    Active exceptions:");
    if (config == 0) {
        appendf(used, " none\n");
        return;
    }
    appendf(used, "\n");
    for (size_t i = 0; i < sizeof(exceptions) / sizeof(exceptions[0]); i++) {
        if ((config & exceptions[i].flag) != 0) {
            appendf(used, "      - %s (0x%03x)\n", exceptions[i].name, exceptions[i].flag);
        }
    }
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
    report_sip_exceptions(used, config);
    appendf(used, "    Meaning: SIP protects system files and runtime operations; listed exceptions show relaxed controls.\n");
    appendf(used, "    Score: +%d\n", points);
}

static int fde_escrow_prefs_present(const char *home, char *report_path, size_t report_path_size) {
    static const char *candidates[] = {
        "/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist",
        "/Library/Preferences/com.apple.MCX.FileVault2.plist",
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
                    "/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist");
    return 0;
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
    report_path_indicator(&used, &score, 2, "User TCC database", tcc_db,
                          "Stores current-user privacy decisions; presence does not reveal which apps are allowed.");

    report_sip_indicator(&used, &score);

    char fde_escrow[PATH_MAX];
    int fde_escrow_present = fde_escrow_prefs_present(home, fde_escrow, sizeof(fde_escrow));
    report_path_indicator(&used, &score, 2, "FileVault FDE escrow preferences",
                          fde_escrow,
                          fde_escrow_present ? "An escrow preference artifact exists; this does not prove FileVault is enabled."
                                             : "No current-user escrow preference artifact was found; FileVault may still be enabled.");

    char screen_time[PATH_MAX];
    int screen_time_present = screen_time_prefs_present(home, screen_time, sizeof(screen_time));
    report_path_indicator(&used, &score, 2, "Screen Time preferences", screen_time,
                          screen_time_present ? "A current-user Screen Time preference artifact exists."
                                              : "No current-user Screen Time preference artifact was found.");

    appendf(&used, "\n[+] Posture Verdict\n");
    if (score >= 7) {
        appendf(&used, "    Privacy posture: Hardened\n");
    } else if (score >= 4) {
        appendf(&used, "    Privacy posture: Mixed\n");
    } else {
        appendf(&used, "    Privacy posture: Permissive\n");
    }
    appendf(&used, "    Score: %d/10\n", score > 10 ? 10 : score);
    appendf(&used, "    Evidence collected: file metadata and in-process SIP state; TCC.db contents were not read.\n");
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
