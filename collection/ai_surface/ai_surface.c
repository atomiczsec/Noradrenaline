#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#define OUT_SIZE 32768
#define PREVIEW_MAX 1024
#define MAX_PROJECT_HITS 16
#define MAX_EXT_HITS 16

static char output[OUT_SIZE];

typedef struct {
    int ai_paths;
    int agent_profiles;
    int mcp_files;
    int previews;
    int preview_errors;
    int project_hits;
    int extension_hits;
} scan_results_t;

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

static long file_size(const char *path) {
    struct stat st;
    if (path == NULL || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }
    return (long)st.st_size;
}

static int contains_ci(const char *hay, const char *needle) {
    if (hay == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    size_t nlen = strlen(needle);
    for (size_t i = 0; hay[i] != '\0'; i++) {
        size_t j = 0;
        while (hay[i + j] != '\0' && needle[j] != '\0' &&
               tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static int interesting_extension_name(const char *name) {
    return contains_ci(name, "mcp") ||
           contains_ci(name, "copilot") ||
           contains_ci(name, "claude") ||
           contains_ci(name, "cline") ||
           contains_ci(name, "continue") ||
           contains_ci(name, "cursor") ||
           contains_ci(name, "roo");
}

static void report_path_if_exists(size_t *used, scan_results_t *results, const char *label, const char *path) {
    if (path_exists(path)) {
        appendf(used, "[+] %s: %s\n", label, path);
        if (results != NULL) {
            results->ai_paths++;
        }
    }
}

static void sanitize_preview(char *buf) {
    int last_space = 0;
    size_t write_idx = 0;
    for (size_t read_idx = 0; buf[read_idx] != '\0'; read_idx++) {
        unsigned char c = (unsigned char)buf[read_idx];
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
        if (!isprint(c)) {
            c = '.';
        }
        if (c == ' ') {
            if (last_space) {
                continue;
            }
            last_space = 1;
        } else {
            last_space = 0;
        }
        buf[write_idx++] = (char)c;
    }
    buf[write_idx] = '\0';
}

static int preview_config_file(size_t *used, scan_results_t *results, const char *label, const char *path) {
    if (!path_exists(path)) {
        return 0;
    }

    appendf(used, "[+] %s: %s\n", label, path);
    if (results != NULL) {
        results->mcp_files++;
    }

    long size = file_size(path);
    if (size >= 0) {
        appendf(used, "[i]   Size: %ld bytes\n", size);
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        appendf(used, "[!]   Preview error: %s\n", strerror(errno));
        if (results != NULL) {
            results->preview_errors++;
        }
        return 1;
    }

    char preview[PREVIEW_MAX + 1];
    size_t n = fread(preview, 1, PREVIEW_MAX, f);
    fclose(f);
    preview[n] = '\0';
    sanitize_preview(preview);

    if (preview[0] != '\0') {
        appendf(used, "[i]   Preview:\n");
        appendf(used, "[i]     %s\n", preview);
        if (results != NULL) {
            results->previews++;
        }
    }
    return 1;
}

static void check_relative_paths(size_t *used, scan_results_t *results,
                                 const char *section, const char *base,
                                 const char *const items[][2], size_t count) {
    char path[PATH_MAX];
    int printed = 0;
    for (size_t i = 0; i < count; i++) {
        if (!build_path(path, sizeof(path), base, items[i][1])) {
            continue;
        }
        if (path_exists(path)) {
            if (!printed) {
                appendf(used, "\n[i] %s:\n", section);
                printed = 1;
            }
            report_path_if_exists(used, results, items[i][0], path);
        }
    }
    if (!printed) {
        appendf(used, "\n[i] %s: no default paths found\n", section);
    }
}

static void scan_agent_profiles(size_t *used, scan_results_t *results, const char *home) {
    char path[PATH_MAX];
    int codex_found = 0;
    int claude_found = 0;

    appendf(used, "\n[i] AI Agent Artifacts:\n");

    if (build_path(path, sizeof(path), home, "/.codex") && is_dir(path)) {
        codex_found = 1;
        results->agent_profiles++;
        appendf(used, "[+] Codex profile: %s\n", path);
        const char *const codex_items[][2] = {
            {"sessions", "/.codex/sessions"},
            {"archived sessions", "/.codex/archived_sessions"},
            {"auth/config", "/.codex/auth.json"},
            {"config", "/.codex/config.toml"},
            {"history", "/.codex/history.jsonl"},
            {"logs", "/.codex/log"},
            {"state db", "/.codex/state_5.sqlite"},
            {"sandbox/cache/tmp", "/.codex/.tmp"},
            {"memories", "/.codex/memories"},
            {"skills", "/.codex/skills"},
        };
        for (size_t i = 0; i < sizeof(codex_items) / sizeof(codex_items[0]); i++) {
            char item[PATH_MAX];
            if (build_path(item, sizeof(item), home, codex_items[i][1]) && path_exists(item)) {
                appendf(used, "[i]   %s\n", codex_items[i][0]);
            }
        }
    }

    if (build_path(path, sizeof(path), home, "/.claude") && is_dir(path)) {
        claude_found = 1;
        results->agent_profiles++;
        appendf(used, "[+] Claude Code profile: %s\n", path);
        const char *const claude_items[][2] = {
            {"sessions/projects", "/.claude/projects"},
            {"sessions", "/.claude/sessions"},
            {"paste-cache", "/.claude/paste-cache"},
            {"history", "/.claude/history.jsonl"},
            {"settings", "/.claude/settings.json"},
            {"credentials", "/.claude/.credentials.json"},
            {"plans/tasks/todos", "/.claude/plans"},
            {"shell/IDE/debug", "/.claude/ide"},
        };
        for (size_t i = 0; i < sizeof(claude_items) / sizeof(claude_items[0]); i++) {
            char item[PATH_MAX];
            if (build_path(item, sizeof(item), home, claude_items[i][1]) && path_exists(item)) {
                appendf(used, "[i]   %s\n", claude_items[i][0]);
            }
        }
    }

    appendf(used, "[i] AI agent artifact summary: Codex=%s Claude Code=%s\n",
            codex_found ? "yes" : "no",
            claude_found ? "yes" : "no");
}

static void scan_extension_storage(size_t *used, scan_results_t *results,
                                   const char *label, const char *storage_root) {
    DIR *dir = opendir(storage_root);
    if (dir == NULL) {
        return;
    }

    int printed = 0;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && results->extension_hits < MAX_EXT_HITS) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!interesting_extension_name(entry->d_name)) {
            continue;
        }
        char child[PATH_MAX];
        if (!build_path(child, sizeof(child), storage_root, "/")) {
            continue;
        }
        size_t len = strlen(child);
        if (len + strlen(entry->d_name) + 1 >= sizeof(child)) {
            continue;
        }
        strcat(child, entry->d_name);
        if (!path_exists(child)) {
            continue;
        }
        if (!printed) {
            appendf(used, "[i] %s potential AI/MCP extension storage:\n", label);
            printed = 1;
        }
        appendf(used, "[+]   %s\n", child);
        results->extension_hits++;
    }

    closedir(dir);
}

static void inspect_project_candidate(size_t *used, scan_results_t *results, const char *project_path) {
    if (results->project_hits >= MAX_PROJECT_HITS || !is_dir(project_path)) {
        return;
    }

    char candidate[PATH_MAX];
    if (build_path(candidate, sizeof(candidate), project_path, "/.mcp.json") &&
        preview_config_file(used, results, "Project MCP Config", candidate)) {
        results->project_hits++;
    }

    if (results->project_hits >= MAX_PROJECT_HITS) {
        return;
    }

    if (build_path(candidate, sizeof(candidate), project_path, "/.cursor/rules/mcp.json") &&
        preview_config_file(used, results, "Project Cursor MCP", candidate)) {
        results->project_hits++;
    }
}

static void scan_project_root(size_t *used, scan_results_t *results, const char *root) {
    if (!is_dir(root) || results->project_hits >= MAX_PROJECT_HITS) {
        return;
    }

    inspect_project_candidate(used, results, root);

    DIR *dir = opendir(root);
    if (dir == NULL) {
        return;
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL && results->project_hits < MAX_PROJECT_HITS) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char child[PATH_MAX];
        int written = snprintf(child, sizeof(child), "%s/%s", root, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(child)) {
            continue;
        }
        inspect_project_candidate(used, results, child);
    }
    closedir(dir);
}

static void scan_mcp_configs(size_t *used, scan_results_t *results, const char *home) {
    char path[PATH_MAX];
    appendf(used, "\n[i] MCP Configuration Discovery:\n");

#if defined(__APPLE__)
    if (build_path(path, sizeof(path), home, "/Library/Application Support/Claude/claude_desktop_config.json")) {
        preview_config_file(used, results, "Claude Desktop MCP Config", path);
    }
#elif defined(__linux__)
    if (build_path(path, sizeof(path), home, "/.config/Claude/claude_desktop_config.json")) {
        preview_config_file(used, results, "Claude Desktop MCP Config", path);
    }
#endif

    const char *const global_configs[][2] = {
        {"Claude Code MCP Config", "/.claude.json"},
        {"Cursor Global MCP Config", "/.cursor/mcp.json"},
        {"Windsurf MCP Config", "/.codeium/windsurf/mcp_config.json"},
        {"Continue Config", "/.continue/config.json"},
    };
    for (size_t i = 0; i < sizeof(global_configs) / sizeof(global_configs[0]); i++) {
        if (build_path(path, sizeof(path), home, global_configs[i][1])) {
            preview_config_file(used, results, global_configs[i][0], path);
        }
    }

    const char *const project_roots[] = {
        "/source", "/Source", "/src", "/dev", "/Dev", "/code", "/Code",
        "/repos", "/Repos", "/projects", "/Projects", "/Documents/GitHub",
        "/Documents/Repos", "/Documents/Projects", "/Desktop",
    };
    for (size_t i = 0; i < sizeof(project_roots) / sizeof(project_roots[0]); i++) {
        if (results->project_hits >= MAX_PROJECT_HITS) {
            appendf(used, "[!] Project MCP hit cap reached (%d)\n", MAX_PROJECT_HITS);
            break;
        }
        if (build_path(path, sizeof(path), home, project_roots[i])) {
            scan_project_root(used, results, path);
        }
    }

    if (results->mcp_files == 0) {
        appendf(used, "[i] No MCP configs discovered in default global or project roots\n");
    }
    appendf(used, "[i] MCP summary: %d artifacts, %d previews, %d preview errors\n",
            results->mcp_files, results->previews, results->preview_errors);
}

EXPORT char *ai_surface(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const char *home = home_dir();
    scan_results_t results;
    memset(&results, 0, sizeof(results));
    size_t used = 0;
    output[0] = '\0';

    appendf(&used, "[i] Mapping AI developer surface:\n");
    appendf(&used, "[i] home: %s\n", home);

#if defined(__APPLE__)
    const char *const copilot_items[][2] = {
        {"VS Code Copilot", "/Library/Application Support/Code/User/globalStorage/github.copilot"},
        {"VS Code Copilot Chat", "/Library/Application Support/Code/User/globalStorage/github.copilot-chat"},
        {"VS Code workspaceStorage", "/Library/Application Support/Code/User/workspaceStorage"},
        {"VS Code Insiders Copilot", "/Library/Application Support/Code - Insiders/User/globalStorage/github.copilot"},
        {"Cursor Copilot", "/Library/Application Support/Cursor/User/globalStorage/github.copilot"},
        {"Cursor workspaceStorage", "/Library/Application Support/Cursor/User/workspaceStorage"},
    };
    const char *const third_party_items[][2] = {
        {"ChatGPT", "/Library/Application Support/ChatGPT"},
        {"ChatGPT Container", "/Library/Containers/com.openai.chat"},
        {"Claude", "/Library/Application Support/Claude"},
        {"Cursor", "/Library/Application Support/Cursor"},
        {"LM Studio", "/Library/Application Support/LM Studio"},
        {"LM Studio profile", "/.lmstudio"},
        {"Ollama", "/.ollama"},
        {"Windsurf", "/Library/Application Support/Windsurf"},
        {"Windsurf profile", "/.codeium/windsurf"},
    };
    check_relative_paths(&used, &results, "GitHub Copilot", home, copilot_items, sizeof(copilot_items) / sizeof(copilot_items[0]));
    check_relative_paths(&used, &results, "Third-party AI", home, third_party_items, sizeof(third_party_items) / sizeof(third_party_items[0]));

    char storage[PATH_MAX];
    if (build_path(storage, sizeof(storage), home, "/Library/Application Support/Code/User/globalStorage")) {
        scan_extension_storage(&used, &results, "VS Code", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/Library/Application Support/Code - Insiders/User/globalStorage")) {
        scan_extension_storage(&used, &results, "VS Code Insiders", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/Library/Application Support/Cursor/User/globalStorage")) {
        scan_extension_storage(&used, &results, "Cursor", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/Library/Application Support/Windsurf/User/globalStorage")) {
        scan_extension_storage(&used, &results, "Windsurf", storage);
    }
#elif defined(__linux__)
    const char *const copilot_items[][2] = {
        {"VS Code Copilot", "/.config/Code/User/globalStorage/github.copilot"},
        {"VS Code Copilot Chat", "/.config/Code/User/globalStorage/github.copilot-chat"},
        {"VS Code workspaceStorage", "/.config/Code/User/workspaceStorage"},
        {"VS Code Insiders Copilot", "/.config/Code - Insiders/User/globalStorage/github.copilot"},
        {"Cursor Copilot", "/.config/Cursor/User/globalStorage/github.copilot"},
        {"Cursor workspaceStorage", "/.config/Cursor/User/workspaceStorage"},
    };
    const char *const third_party_items[][2] = {
        {"ChatGPT", "/.config/ChatGPT"},
        {"Claude", "/.config/Claude"},
        {"Cursor", "/.config/Cursor"},
        {"LM Studio", "/.config/LM Studio"},
        {"LM Studio profile", "/.lmstudio"},
        {"Ollama", "/.ollama"},
        {"Windsurf", "/.config/Windsurf"},
        {"Windsurf profile", "/.codeium/windsurf"},
    };
    check_relative_paths(&used, &results, "GitHub Copilot", home, copilot_items, sizeof(copilot_items) / sizeof(copilot_items[0]));
    check_relative_paths(&used, &results, "Third-party AI", home, third_party_items, sizeof(third_party_items) / sizeof(third_party_items[0]));

    char storage[PATH_MAX];
    if (build_path(storage, sizeof(storage), home, "/.config/Code/User/globalStorage")) {
        scan_extension_storage(&used, &results, "VS Code", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/.config/Code - Insiders/User/globalStorage")) {
        scan_extension_storage(&used, &results, "VS Code Insiders", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/.config/Cursor/User/globalStorage")) {
        scan_extension_storage(&used, &results, "Cursor", storage);
    }
    if (build_path(storage, sizeof(storage), home, "/.config/Windsurf/User/globalStorage")) {
        scan_extension_storage(&used, &results, "Windsurf", storage);
    }
#else
    appendf(&used, "[!] unsupported platform\n");
#endif

    scan_agent_profiles(&used, &results, home);
    scan_mcp_configs(&used, &results, home);
    appendf(&used, "\n[i] AI surface summary: paths=%d agent_profiles=%d mcp_configs=%d extension_hits=%d project_hits=%d\n",
            results.ai_paths, results.agent_profiles, results.mcp_files,
            results.extension_hits, results.project_hits);
    appendf(&used, "[+] ai_surface complete\n");

    return output;
}
