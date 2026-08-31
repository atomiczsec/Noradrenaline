#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#define OUT_SIZE 8192
#define MAX_PEM_HITS 8
#define MAX_CACHE_HITS 8

typedef struct {
    int credential_hits;
    int context_hits;
    int ssh_key_hits;
} scan_results_t;

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

static int join_home_path(const char *home, const char *suffix, char *out, size_t out_size) {
    if (home == NULL || home[0] == '\0' || suffix == NULL || out == NULL || out_size == 0) {
        return 0;
    }

    size_t home_len = strlen(home);
    const char *rest = suffix;
    if (suffix[0] == '/') {
        rest = suffix + 1;
    }

    int n = snprintf(out, out_size, "%s%s%s", home,
                     home_len > 0 && home[home_len - 1] == '/' ? "" : "/", rest);
    return n > 0 && (size_t)n < out_size;
}

static int ends_with_ci(const char *value, const char *suffix) {
    if (value == NULL || suffix == NULL || suffix[0] == '\0') {
        return 0;
    }

    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > value_len) {
        return 0;
    }

    const char *start = value + value_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        unsigned char a = (unsigned char)start[i];
        unsigned char b = (unsigned char)suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a + 32);
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b + 32);
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static int is_regular_file(const char *path, off_t *size_out) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    if (size_out != NULL) {
        *size_out = st.st_size;
    }
    return 1;
}

static void report_text_artifact(size_t *used, scan_results_t *results, const char *label,
                                 const char *path, int class_id) {
    off_t size = 0;
    if (!is_regular_file(path, &size)) {
        return;
    }

    appendf(used, "[+] %s: %s\n", label, path);
    appendf(used, "[i]   Size: %ld bytes\n", (long)size);

    if (class_id == 0) {
        results->credential_hits++;
    } else {
        results->context_hits++;
    }
}

static void report_private_key(size_t *used, scan_results_t *results, const char *label,
                               const char *path) {
    off_t size = 0;
    if (!is_regular_file(path, &size)) {
        return;
    }

    appendf(used, "[+] %s: %s\n", label, path);
    appendf(used, "[i]   Size: %ld bytes\n", (long)size);
    results->ssh_key_hits++;
}

static void inspect_fixed_patterns(size_t *used, scan_results_t *results, const char *home) {
    char path[PATH_MAX];

    static const struct {
        const char *suffix;
        const char *label;
        int class_id;
    } patterns[] = {
        {".config/gh/hosts.yml", "GitHub CLI auth", 0},
        {".config/glab-cli/config.yml", "GitLab CLI auth", 0},
        {".npmrc", "npm config", 0},
        {".pypirc", "PyPI config", 0},
        {".docker/config.json", "Docker config", 0},
        {".config/containers/auth.json", "Container registry auth", 0},
        {".git-credentials", "Git credential store", 0},
        {".aws/credentials", "AWS shared credentials", 0},
        {".aws/config", "AWS config", 0},
        {".config/gcloud/application_default_credentials.json", "Google Cloud ADC", 0},
        {".kube/config", "Kubernetes config", 0},
        {".terraform.d/credentials.tfrc.json", "Terraform CLI credentials", 0},
        {".terraformrc", "Terraform CLI config", 0},
        {".cargo/credentials.toml", "Cargo registry credentials", 0},
        {".cargo/credentials", "Cargo registry credentials (legacy)", 0},
        {".m2/settings.xml", "Maven settings", 0},
        {".gradle/gradle.properties", "Gradle properties", 0},
        {".gem/credentials", "RubyGems credentials", 0},
        {".netrc", "netrc credentials", 0},
        {".vault-token", "Vault token", 0},
        {".gitconfig", "Git config context", 1},
        {NULL, NULL, 0},
    };

    for (int i = 0; patterns[i].suffix != NULL; i++) {
        if (join_home_path(home, patterns[i].suffix, path, sizeof(path))) {
            report_text_artifact(used, results, patterns[i].label, path, patterns[i].class_id);
        }
    }

#if defined(__APPLE__)
    if (join_home_path(home, "Library/Application Support/GitHub CLI/hosts.yml", path, sizeof(path))) {
        report_text_artifact(used, results, "GitHub CLI auth", path, 0);
    }
#endif

}

static void inspect_aws_cache(size_t *used, scan_results_t *results, const char *home) {
    char cache_root[PATH_MAX];
    if (!join_home_path(home, ".aws/cli/cache", cache_root, sizeof(cache_root))) {
        return;
    }
    DIR *dir = opendir(cache_root);
    if (dir == NULL) {
        return;
    }
    int hits = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && hits < MAX_CACHE_HITS) {
        if (entry->d_name[0] == '.' || !ends_with_ci(entry->d_name, ".json")) {
            continue;
        }
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "%s/%s", cache_root, entry->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            continue;
        }
        int before = results->credential_hits;
        report_text_artifact(used, results, "AWS CLI cached credential", path, 0);
        if (results->credential_hits > before) {
            hits++;
        }
    }
    closedir(dir);
}

static void inspect_ssh_artifacts(size_t *used, scan_results_t *results, const char *home) {
    char ssh_root[PATH_MAX];
    char candidate[PATH_MAX];
    struct stat st;
    int pem_hits = 0;

    if (!join_home_path(home, ".ssh", ssh_root, sizeof(ssh_root))) {
        return;
    }
    if (stat(ssh_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }

    if (join_home_path(home, ".ssh/config", candidate, sizeof(candidate))) {
        report_text_artifact(used, results, "SSH config context", candidate, 1);
    }

    static const char *key_names[] = {"id_rsa", "id_ed25519", "id_ecdsa", "identity", NULL};
    for (int i = 0; key_names[i] != NULL; i++) {
        char suffix[PATH_MAX];
        snprintf(suffix, sizeof(suffix), ".ssh/%s", key_names[i]);
        if (join_home_path(home, suffix, candidate, sizeof(candidate))) {
            report_private_key(used, results, "SSH private key", candidate);
        }
    }

    DIR *dir = opendir(ssh_root);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && pem_hits < MAX_PEM_HITS) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (ends_with_ci(entry->d_name, ".pub")) {
            continue;
        }
        if (!ends_with_ci(entry->d_name, ".pem")) {
            continue;
        }

        char suffix[PATH_MAX];
        snprintf(suffix, sizeof(suffix), ".ssh/%s", entry->d_name);
        if (!join_home_path(home, suffix, candidate, sizeof(candidate))) {
            continue;
        }

        struct stat entry_st;
        if (stat(candidate, &entry_st) != 0 || !S_ISREG(entry_st.st_mode)) {
            continue;
        }

        report_private_key(used, results, "SSH PEM candidate", candidate);
        pem_hits++;
    }

    closedir(dir);
}

static void print_hunt_banner(size_t *used) {
#if defined(__APPLE__)
    appendf(used, "[i] Enumerating CI/CD credential artifacts on macOS developer paths\n");
#elif defined(__linux__)
    appendf(used, "[i] Enumerating CI/CD credential artifacts on Linux developer paths\n");
#else
    appendf(used, "[i] Enumerating CI/CD credential artifacts on Unix developer paths\n");
#endif
    appendf(used, "[i] Mode: presence (existence only)\n");
}

EXPORT char *cicd_credential_hunt(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t used = 0;
    scan_results_t results;
    const char *home = getenv("HOME");

    output[0] = '\0';
    memset(&results, 0, sizeof(results));

    print_hunt_banner(&used);

    if (home == NULL || home[0] == '\0') {
        appendf(&used,
                "[i] Summary: credential artifacts=%d, config context=%d, ssh key candidates=%d\n",
                results.credential_hits, results.context_hits, results.ssh_key_hits);
        return output;
    }

    inspect_fixed_patterns(&used, &results, home);
    inspect_aws_cache(&used, &results, home);
    inspect_ssh_artifacts(&used, &results, home);

    appendf(&used,
            "[i] Summary: credential artifacts=%d, config context=%d, ssh key candidates=%d\n",
            results.credential_hits, results.context_hits, results.ssh_key_hits);
    return output;
}
