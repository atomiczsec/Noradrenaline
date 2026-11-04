/*
 * wireless_situational_awareness — Poseidon execute_library module (macOS)
 *
 * Rootless Wi-Fi + Bluetooth situational awareness.
 *
 * Sources:
 *   system_profiler SPAirPortDataType
 *   ipconfig getsummary en0
 *   networksetup -listpreferredwirelessnetworks en0
 *   system_profiler SPBluetoothDataType
 *
 * Args: none (shaped summary) | raw
 *
 * Export: char *wireless_situational_awareness(int argc, char **argv);
 */

#include <errno.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))
#define OUT_SIZE 65536
#define CAP_SIZE 131072
#define READ_CHUNK 4096
#define DEFAULT_IFACE "en0"
#define MAX_NEARBY 24
#define MAX_PREFERRED 64
#define MAX_BT_DEVICES 48
#define SYSTEM_PROFILER_PATH "/usr/sbin/system_profiler"
#define IPCONFIG_PATH "/usr/sbin/ipconfig"
#define NETWORKSETUP_PATH "/usr/sbin/networksetup"

static char output[OUT_SIZE];
extern char **environ;

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

static int str_equals(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

static void trim_inplace(char *s) {
    if (!s) {
        return;
    }
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

static void print_usage(size_t *used) {
    appendf(used, "[i] wireless_situational_awareness [raw]\n");
    appendf(used, "    (default) shaped Wi-Fi + Bluetooth summary\n");
    appendf(used, "    raw       unfiltered source dumps\n");
}

static int run_capture_to(char *dest, size_t dest_size, char *const argv[], int *exit_code) {
    if (!dest || dest_size == 0 || !argv || !argv[0]) {
        return -1;
    }
    dest[0] = '\0';
    if (exit_code) {
        *exit_code = -1;
    }

    struct stat st;
    if (stat(argv[0], &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(dest, dest_size, "[-] missing binary: %s\n", argv[0]);
        return -1;
    }

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(dest, dest_size, "[-] pipe failed: %s\n", strerror(errno));
        return -1;
    }

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        snprintf(dest, dest_size, "[-] posix_spawn_file_actions_init failed\n");
        return -1;
    }
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);

    pid_t pid = 0;
    int spawn_rc = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);
    if (spawn_rc != 0) {
        close(pipefd[0]);
        snprintf(dest, dest_size, "[-] posix_spawn(%s) failed: %s\n", argv[0], strerror(spawn_rc));
        return -1;
    }

    size_t used = 0;
    char chunk[READ_CHUNK];
    ssize_t nread;
    while ((nread = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
        size_t room = dest_size - 1 - used;
        size_t copy = (size_t)nread < room ? (size_t)nread : room;
        memcpy(dest + used, chunk, copy);
        used += copy;
        dest[used] = '\0';
        if (copy < (size_t)nread) {
            while (read(pipefd[0], chunk, sizeof(chunk)) > 0) {
            }
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return -1;
    }
    if (exit_code) {
        *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return 0;
}

static int line_indent(const char *line) {
    int n = 0;
    while (line[n] == ' ') {
        n++;
    }
    return n;
}

static int extract_field(const char *block, const char *key, char *out, size_t out_size) {
    if (!block || !key || !out || out_size == 0) {
        return 0;
    }
    char needle[128];
    int nlen = snprintf(needle, sizeof(needle), "%s:", key);
    if (nlen < 0 || (size_t)nlen >= sizeof(needle)) {
        return 0;
    }
    const char *p = block;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        char line[1024];
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, p, len);
        line[len] = '\0';
        char *s = line;
        while (*s == ' ' || *s == '\t') {
            s++;
        }
        if (strncmp(s, needle, (size_t)nlen) == 0) {
            s += nlen;
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            snprintf(out, out_size, "%s", s);
            trim_inplace(out);
            return out[0] != '\0';
        }
        if (!eol) {
            break;
        }
        p = eol + 1;
    }
    return 0;
}

static int extract_kv_colon(const char *block, const char *key, char *out, size_t out_size) {
    if (!block || !key || !out || out_size == 0) {
        return 0;
    }
    char needle[128];
    int nlen = snprintf(needle, sizeof(needle), "%s :", key);
    if (nlen < 0 || (size_t)nlen >= sizeof(needle)) {
        return 0;
    }
    const char *p = strstr(block, needle);
    if (!p) {
        nlen = snprintf(needle, sizeof(needle), "%s:", key);
        p = strstr(block, needle);
        if (!p) {
            return 0;
        }
    }
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    size_t i = 0;
    while (*p && *p != '\n' && *p != '{' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    trim_inplace(out);
    return out[0] != '\0';
}

static void shape_wifi(size_t *used, const char *iface, const char *sp, const char *ipc,
                       const char *prefs) {
    appendf(used, "[+] Wi-Fi\n");
    appendf(used, "    iface: %s\n\n", iface);

    char status[64] = "";
    char mac[64] = "";
    char locale[32] = "";
    char country[32] = "";
    char phy[64] = "";
    char channel[64] = "";
    char security[64] = "";
    char snr[64] = "";
    char txrate[32] = "";
    char mcs[16] = "";
    char card[96] = "";

    char iface_tag[64];
    snprintf(iface_tag, sizeof(iface_tag), "%s:", iface);
    const char *iface_sec = strstr(sp, iface_tag);
    if (!iface_sec) {
        iface_sec = sp;
    }

    char iface_block[CAP_SIZE];
    {
        const char *awdl = strstr(iface_sec, "\n        awdl0:");
        size_t len = awdl ? (size_t)(awdl - iface_sec) : strlen(iface_sec);
        if (len >= sizeof(iface_block)) {
            len = sizeof(iface_block) - 1;
        }
        memcpy(iface_block, iface_sec, len);
        iface_block[len] = '\0';
    }

    extract_field(iface_block, "Status", status, sizeof(status));
    extract_field(iface_block, "MAC Address", mac, sizeof(mac));
    extract_field(iface_block, "Locale", locale, sizeof(locale));
    extract_field(iface_block, "Country Code", country, sizeof(country));
    extract_field(iface_block, "Card Type", card, sizeof(card));

    const char *cur = strstr(iface_block, "Current Network Information:");
    if (cur) {
        extract_field(cur, "PHY Mode", phy, sizeof(phy));
        extract_field(cur, "Channel", channel, sizeof(channel));
        extract_field(cur, "Security", security, sizeof(security));
        extract_field(cur, "Signal / Noise", snr, sizeof(snr));
        extract_field(cur, "Transmit Rate", txrate, sizeof(txrate));
        extract_field(cur, "MCS Index", mcs, sizeof(mcs));
    }

    char ssid[128] = "";
    char bssid[128] = "";
    char sec2[64] = "";
    char ipv4[64] = "";
    char mask[64] = "";
    char router[64] = "";
    char dns[256] = "";
    char domain[128] = "";
    char dhcp_state[64] = "";
    char lease_start[64] = "";
    char lease_end[64] = "";
    char ipv6[128] = "";

    extract_kv_colon(ipc, "SSID", ssid, sizeof(ssid));
    extract_kv_colon(ipc, "BSSID", bssid, sizeof(bssid));
    extract_kv_colon(ipc, "Security", sec2, sizeof(sec2));
    if (security[0] == '\0' && sec2[0]) {
        snprintf(security, sizeof(security), "%s", sec2);
    }

    {
        const char *addr = strstr(ipc, "Addresses :");
        if (addr) {
            const char *ip = strstr(addr, "0 : ");
            if (ip) {
                ip += 4;
                size_t i = 0;
                while (*ip && *ip != '\n' && *ip != ' ' && i + 1 < sizeof(ipv4)) {
                    ipv4[i++] = *ip++;
                }
                ipv4[i] = '\0';
            }
        }
        if (ipv4[0] == '\0') {
            const char *yi = strstr(ipc, "yiaddr = ");
            if (yi) {
                yi += 9;
                size_t i = 0;
                while (*yi && *yi != '\n' && *yi != ' ' && i + 1 < sizeof(ipv4)) {
                    ipv4[i++] = *yi++;
                }
                ipv4[i] = '\0';
            }
        }
    }
    extract_kv_colon(ipc, "Router", router, sizeof(router));
    {
        const char *sm = strstr(ipc, "subnet_mask (ip): ");
        if (sm) {
            sm += strlen("subnet_mask (ip): ");
            size_t i = 0;
            while (*sm && *sm != '\n' && i + 1 < sizeof(mask)) {
                mask[i++] = *sm++;
            }
            mask[i] = '\0';
            trim_inplace(mask);
        } else {
            const char *arr = strstr(ipc, "SubnetMasks :");
            if (arr) {
                const char *v = strstr(arr, "0 : ");
                if (v) {
                    v += 4;
                    size_t i = 0;
                    while (*v && *v != '\n' && i + 1 < sizeof(mask)) {
                        mask[i++] = *v++;
                    }
                    mask[i] = '\0';
                    trim_inplace(mask);
                }
            }
        }
    }
    {
        const char *d = strstr(ipc, "domain_name (string): ");
        if (d) {
            d += strlen("domain_name (string): ");
            size_t i = 0;
            while (*d && *d != '\n' && i + 1 < sizeof(domain)) {
                domain[i++] = *d++;
            }
            domain[i] = '\0';
            trim_inplace(domain);
        }
    }
    {
        const char *d = strstr(ipc, "domain_name_server (ip_mult): ");
        if (d) {
            d += strlen("domain_name_server (ip_mult): ");
            size_t i = 0;
            while (*d && *d != '\n' && i + 1 < sizeof(dns)) {
                dns[i++] = *d++;
            }
            dns[i] = '\0';
            trim_inplace(dns);
        }
    }
    extract_kv_colon(ipc, "State", dhcp_state, sizeof(dhcp_state));
    extract_kv_colon(ipc, "LeaseStartTime", lease_start, sizeof(lease_start));
    extract_kv_colon(ipc, "LeaseExpirationTime", lease_end, sizeof(lease_end));
    {
        const char *sa = strstr(ipc, "StatefulAddress : ");
        if (sa) {
            sa += strlen("StatefulAddress : ");
            size_t i = 0;
            while (*sa && *sa != '\n' && i + 1 < sizeof(ipv6)) {
                ipv6[i++] = *sa++;
            }
            ipv6[i] = '\0';
            trim_inplace(ipv6);
        }
    }

    appendf(used, "  [+] Association\n");
    appendf(used, "      Status: %s\n", status[0] ? status : "unknown");
    appendf(used, "      SSID: %s\n", ssid[0] ? ssid : "<unavailable>");
    appendf(used, "      BSSID: %s\n", bssid[0] ? bssid : "<unavailable>");
    appendf(used, "      Security: %s\n", security[0] ? security : "<unavailable>");
    appendf(used, "      PHY: %s\n", phy[0] ? phy : "<unavailable>");
    appendf(used, "      Channel: %s\n", channel[0] ? channel : "<unavailable>");
    appendf(used, "      Signal/Noise: %s\n", snr[0] ? snr : "<unavailable>");
    if (txrate[0]) {
        appendf(used, "      TxRate: %s Mbps\n", txrate);
    }
    if (mcs[0]) {
        appendf(used, "      MCS: %s\n", mcs);
    }
    appendf(used, "      Client MAC: %s\n", mac[0] ? mac : "<unavailable>");
    if (country[0] || locale[0]) {
        appendf(used, "      Country/Locale: %s%s%s\n", country[0] ? country : "-",
                (country[0] && locale[0]) ? "/" : "", locale[0] ? locale : "");
    }
    if (card[0]) {
        appendf(used, "      Adapter: %s\n", card);
    }

    appendf(used, "\n  [+] Addressing\n");
    appendf(used, "      IPv4: %s%s%s\n", ipv4[0] ? ipv4 : "<none>", mask[0] ? "/" : "",
            mask[0] ? mask : "");
    appendf(used, "      Router: %s\n", router[0] ? router : "<none>");
    appendf(used, "      DNS: %s\n", dns[0] ? dns : "<none>");
    appendf(used, "      Domain: %s\n", domain[0] ? domain : "<none>");
    appendf(used, "      DHCP: %s\n", dhcp_state[0] ? dhcp_state : "<unknown>");
    if (lease_start[0] || lease_end[0]) {
        appendf(used, "      Lease: %s -> %s\n", lease_start[0] ? lease_start : "?",
                lease_end[0] ? lease_end : "?");
    }
    if (ipv6[0]) {
        appendf(used, "      IPv6: %s\n", ipv6);
    }

    appendf(used, "\n  [+] Nearby (security/channel; SSIDs often redacted)\n");
    const char *near = strstr(iface_block, "Other Local Wi-Fi Networks:");
    int nearby_count = 0;
    int open_count = 0;
    int ent_count = 0;
    if (near) {
        const char *p = near;
        const char *eol = strchr(p, '\n');
        if (eol) {
            p = eol + 1;
        }
        char nphy[64], nch[64], nsec[64], nsn[64];
        nphy[0] = nch[0] = nsec[0] = nsn[0] = '\0';
        int in_entry = 0;
        while (*p && nearby_count < MAX_NEARBY) {
            eol = strchr(p, '\n');
            size_t len = eol ? (size_t)(eol - p) : strlen(p);
            char line[512];
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            memcpy(line, p, len);
            line[len] = '\0';
            int ind = line_indent(line);
            char *s = line + ind;

            if (ind <= 8 && s[0] && strstr(s, "awdl0:") == s) {
                break;
            }
            if (ind >= 12 && s[0] && s[strlen(s) - 1] == ':' && strchr(s, ' ') == NULL) {
                if (in_entry && (nsec[0] || nch[0])) {
                    nearby_count++;
                    if (strstr(nsec, "None")) {
                        open_count++;
                    }
                    if (strstr(nsec, "Enterprise")) {
                        ent_count++;
                    }
                    appendf(used, "      [%d] %s | %s%s%s%s%s\n", nearby_count,
                            nsec[0] ? nsec : "?", nch[0] ? nch : "?",
                            nphy[0] ? " | " : "", nphy[0] ? nphy : "",
                            nsn[0] ? " | " : "", nsn[0] ? nsn : "");
                }
                in_entry = 1;
                nphy[0] = nch[0] = nsec[0] = nsn[0] = '\0';
            } else if (in_entry) {
                if (strncmp(s, "PHY Mode:", 9) == 0) {
                    snprintf(nphy, sizeof(nphy), "%s", s + 9);
                    trim_inplace(nphy);
                } else if (strncmp(s, "Channel:", 8) == 0) {
                    snprintf(nch, sizeof(nch), "%s", s + 8);
                    trim_inplace(nch);
                } else if (strncmp(s, "Security:", 9) == 0) {
                    snprintf(nsec, sizeof(nsec), "%s", s + 9);
                    trim_inplace(nsec);
                } else if (strncmp(s, "Signal / Noise:", 15) == 0) {
                    snprintf(nsn, sizeof(nsn), "%s", s + 15);
                    trim_inplace(nsn);
                }
            }
            if (!eol) {
                break;
            }
            p = eol + 1;
        }
        if (in_entry && (nsec[0] || nch[0]) && nearby_count < MAX_NEARBY) {
            nearby_count++;
            if (strstr(nsec, "None")) {
                open_count++;
            }
            if (strstr(nsec, "Enterprise")) {
                ent_count++;
            }
            appendf(used, "      [%d] %s | %s%s%s%s%s\n", nearby_count, nsec[0] ? nsec : "?",
                    nch[0] ? nch : "?", nphy[0] ? " | " : "", nphy[0] ? nphy : "",
                    nsn[0] ? " | " : "", nsn[0] ? nsn : "");
        }
    }
    if (nearby_count == 0) {
        appendf(used, "      (none parsed)\n");
    } else {
        appendf(used, "      Summary: %d visible | %d open | %d enterprise\n", nearby_count,
                open_count, ent_count);
    }

    appendf(used, "\n  [+] Preferred networks (saved SSIDs)\n");
    int pref_count = 0;
    if (prefs && *prefs) {
        const char *p = prefs;
        while (*p && pref_count < MAX_PREFERRED) {
            const char *eol = strchr(p, '\n');
            size_t len = eol ? (size_t)(eol - p) : strlen(p);
            char line[256];
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            memcpy(line, p, len);
            line[len] = '\0';
            trim_inplace(line);
            if (line[0] && strncmp(line, "Preferred networks", 18) != 0 &&
                strncmp(line, "[+]", 3) != 0 && strncmp(line, "[i]", 3) != 0 &&
                strncmp(line, "[-]", 3) != 0) {
                pref_count++;
                appendf(used, "      %d. %s\n", pref_count, line);
            }
            if (!eol) {
                break;
            }
            p = eol + 1;
        }
    }
    if (pref_count == 0) {
        appendf(used, "      (none)\n");
    } else {
        appendf(used, "      Count: %d\n", pref_count);
    }
}

static int looks_like_device_name(const char *s) {
    if (!s || !*s) {
        return 0;
    }
    size_t n = strlen(s);
    if (s[n - 1] != ':') {
        return 0;
    }
    if (strchr(s, ' ') == NULL && strchr(s, ':') == s + n - 1) {
        /* single token ending in colon — could be name */
        if (strcmp(s, "Address:") == 0 || strcmp(s, "State:") == 0) {
            return 0;
        }
    }
    /* device names are keys ending with ':' and not known field labels */
    static const char *fields[] = {
        "Address:", "Vendor ID:", "Product ID:", "Case Battery Level:", "Left Battery Level:",
        "Right Battery Level:", "Case Version:", "Firmware Version:", "Minor Type:",
        "Serial Number:", "Serial Number (Left):", "Serial Number (Right):", "RSSI:",
        "Chipset:", "Discoverable:", "Product ID:", "Supported services:", "Transport:",
        "Bluetooth Controller:", "Not Connected:", "Connected:", "Services:", NULL};
    for (int i = 0; fields[i]; i++) {
        if (strcmp(s, fields[i]) == 0) {
            return 0;
        }
    }
    return 1;
}

static void shape_bt(size_t *used, const char *bt) {
    appendf(used, "[+] Bluetooth\n\n");

    char addr[64] = "";
    char state[32] = "";
    char chipset[64] = "";
    char disc[32] = "";
    char vendor[64] = "";
    char product[32] = "";
    char transport[32] = "";
    char services[256] = "";

    const char *ctrl = strstr(bt, "Bluetooth Controller:");
    if (ctrl) {
        extract_field(ctrl, "Address", addr, sizeof(addr));
        extract_field(ctrl, "State", state, sizeof(state));
        extract_field(ctrl, "Chipset", chipset, sizeof(chipset));
        extract_field(ctrl, "Discoverable", disc, sizeof(disc));
        extract_field(ctrl, "Vendor ID", vendor, sizeof(vendor));
        extract_field(ctrl, "Product ID", product, sizeof(product));
        extract_field(ctrl, "Transport", transport, sizeof(transport));
        extract_field(ctrl, "Supported services", services, sizeof(services));
    }

    appendf(used, "  [+] Controller\n");
    appendf(used, "      State: %s\n", state[0] ? state : "unknown");
    appendf(used, "      Address: %s\n", addr[0] ? addr : "<unavailable>");
    appendf(used, "      Discoverable: %s\n", disc[0] ? disc : "?");
    if (chipset[0]) {
        appendf(used, "      Chipset: %s\n", chipset);
    }
    if (vendor[0] || product[0]) {
        appendf(used, "      Vendor/Product: %s / %s\n", vendor[0] ? vendor : "?",
                product[0] ? product : "?");
    }
    if (transport[0]) {
        appendf(used, "      Transport: %s\n", transport);
    }
    if (services[0]) {
        appendf(used, "      Services: %s\n", services);
    }

    appendf(used, "\n  [+] Devices (paired / known)\n");

    /* Walk Connected / Not Connected sections */
    const char *sections[] = {"Connected:", "Not Connected:", NULL};
    int total = 0;
    for (int si = 0; sections[si]; si++) {
        const char *sec = strstr(bt, sections[si]);
        if (!sec) {
            continue;
        }
        const char *p = sec;
        const char *eol = strchr(p, '\n');
        if (eol) {
            p = eol + 1;
        }

        char name[128] = "";
        char daddr[64] = "";
        char dtype[64] = "";
        char rssi[32] = "";
        char vend[64] = "";
        char bat_case[32] = "";
        char bat_l[32] = "";
        char bat_r[32] = "";
        char serial[96] = "";
        int in_dev = 0;
        const char *section_label = sections[si];

        while (*p && total < MAX_BT_DEVICES) {
            eol = strchr(p, '\n');
            size_t len = eol ? (size_t)(eol - p) : strlen(p);
            char line[512];
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            memcpy(line, p, len);
            line[len] = '\0';
            int ind = line_indent(line);
            char *s = line + ind;

            /* next top-level section */
            if (ind <= 6 && (strcmp(s, "Connected:") == 0 || strcmp(s, "Not Connected:") == 0 ||
                             strcmp(s, "Bluetooth Controller:") == 0)) {
                if (strcmp(s, section_label) != 0) {
                    break;
                }
            }

            if (ind >= 10 && looks_like_device_name(s)) {
                if (in_dev && name[0]) {
                    total++;
                    const char *conn =
                        (strstr(section_label, "Not") == section_label) ? "paired" : "connected";
                    appendf(used, "      [%d] %s (%s)\n", total, name, conn);
                    if (daddr[0]) {
                        appendf(used, "          Address: %s\n", daddr);
                    }
                    if (dtype[0]) {
                        appendf(used, "          Type: %s\n", dtype);
                    }
                    if (rssi[0]) {
                        appendf(used, "          RSSI: %s\n", rssi);
                    }
                    if (vend[0]) {
                        appendf(used, "          Vendor: %s\n", vend);
                    }
                    if (bat_case[0] || bat_l[0] || bat_r[0]) {
                        appendf(used, "          Battery:");
                        if (bat_case[0]) {
                            appendf(used, " case %s", bat_case);
                        }
                        if (bat_l[0]) {
                            appendf(used, " L %s", bat_l);
                        }
                        if (bat_r[0]) {
                            appendf(used, " R %s", bat_r);
                        }
                        appendf(used, "\n");
                    }
                    if (serial[0]) {
                        appendf(used, "          Serial: %s\n", serial);
                    }
                }
                in_dev = 1;
                snprintf(name, sizeof(name), "%s", s);
                if (name[0] && name[strlen(name) - 1] == ':') {
                    name[strlen(name) - 1] = '\0';
                }
                daddr[0] = dtype[0] = rssi[0] = vend[0] = bat_case[0] = bat_l[0] = bat_r[0] =
                    serial[0] = '\0';
            } else if (in_dev) {
                if (strncmp(s, "Address:", 8) == 0) {
                    snprintf(daddr, sizeof(daddr), "%s", s + 8);
                    trim_inplace(daddr);
                } else if (strncmp(s, "Minor Type:", 11) == 0) {
                    snprintf(dtype, sizeof(dtype), "%s", s + 11);
                    trim_inplace(dtype);
                } else if (strncmp(s, "RSSI:", 5) == 0) {
                    snprintf(rssi, sizeof(rssi), "%s", s + 5);
                    trim_inplace(rssi);
                } else if (strncmp(s, "Vendor ID:", 10) == 0) {
                    snprintf(vend, sizeof(vend), "%s", s + 10);
                    trim_inplace(vend);
                } else if (strncmp(s, "Case Battery Level:", 19) == 0) {
                    snprintf(bat_case, sizeof(bat_case), "%s", s + 19);
                    trim_inplace(bat_case);
                } else if (strncmp(s, "Left Battery Level:", 19) == 0) {
                    snprintf(bat_l, sizeof(bat_l), "%s", s + 19);
                    trim_inplace(bat_l);
                } else if (strncmp(s, "Right Battery Level:", 20) == 0) {
                    snprintf(bat_r, sizeof(bat_r), "%s", s + 20);
                    trim_inplace(bat_r);
                } else if (strncmp(s, "Serial Number:", 14) == 0 &&
                           strncmp(s, "Serial Number (Left):", 21) != 0 &&
                           strncmp(s, "Serial Number (Right):", 22) != 0) {
                    snprintf(serial, sizeof(serial), "%s", s + 14);
                    trim_inplace(serial);
                }
            }
            if (!eol) {
                break;
            }
            p = eol + 1;
        }
        if (in_dev && name[0] && total < MAX_BT_DEVICES) {
            total++;
            const char *conn =
                (strstr(section_label, "Not") == section_label) ? "paired" : "connected";
            appendf(used, "      [%d] %s (%s)\n", total, name, conn);
            if (daddr[0]) {
                appendf(used, "          Address: %s\n", daddr);
            }
            if (dtype[0]) {
                appendf(used, "          Type: %s\n", dtype);
            }
            if (rssi[0]) {
                appendf(used, "          RSSI: %s\n", rssi);
            }
            if (vend[0]) {
                appendf(used, "          Vendor: %s\n", vend);
            }
            if (bat_case[0] || bat_l[0] || bat_r[0]) {
                appendf(used, "          Battery:");
                if (bat_case[0]) {
                    appendf(used, " case %s", bat_case);
                }
                if (bat_l[0]) {
                    appendf(used, " L %s", bat_l);
                }
                if (bat_r[0]) {
                    appendf(used, " R %s", bat_r);
                }
                appendf(used, "\n");
            }
            if (serial[0]) {
                appendf(used, "          Serial: %s\n", serial);
            }
        }
    }
    if (total == 0) {
        appendf(used, "      (none parsed)\n");
    } else {
        appendf(used, "      Count: %d\n", total);
    }
}


static char *run_info(int raw_mode) {
    size_t used = 0;
    output[0] = '\0';
    const char *iface = DEFAULT_IFACE;

    static char sp[CAP_SIZE];
    static char ipc[CAP_SIZE];
    static char prefs[CAP_SIZE];
    static char bt[CAP_SIZE];
    int ec = 0;

    appendf(&used, "[+] wireless_situational_awareness\n");
    appendf(&used, "    euid: %d\n\n", (int)geteuid());

    char *sp_argv[] = {(char *)SYSTEM_PROFILER_PATH, (char *)"SPAirPortDataType", NULL};
    char *ipc_argv[] = {(char *)IPCONFIG_PATH, (char *)"getsummary", (char *)iface, NULL};
    char *pref_argv[] = {(char *)NETWORKSETUP_PATH, (char *)"-listpreferredwirelessnetworks",
                         (char *)iface, NULL};
    char *bt_argv[] = {(char *)SYSTEM_PROFILER_PATH, (char *)"SPBluetoothDataType", NULL};
    (void)run_capture_to(sp, sizeof(sp), sp_argv, &ec);
    (void)run_capture_to(ipc, sizeof(ipc), ipc_argv, &ec);
    (void)run_capture_to(prefs, sizeof(prefs), pref_argv, &ec);
    (void)run_capture_to(bt, sizeof(bt), bt_argv, &ec);

    if (raw_mode) {
        appendf(&used, "--- system_profiler SPAirPortDataType ---\n%s\n", sp);
        appendf(&used, "--- ipconfig getsummary %s ---\n%s\n", iface, ipc);
        appendf(&used, "--- preferred ---\n%s\n", prefs);
        appendf(&used, "--- system_profiler SPBluetoothDataType ---\n%s\n", bt);
        return output;
    }

    shape_wifi(&used, iface, sp, ipc, prefs);
    appendf(&used, "\n");
    shape_bt(&used, bt);
    appendf(&used, "\n");
    return output;
}

EXPORT char *wireless_situational_awareness(int argc, char **argv) {
    output[0] = '\0';
#if !defined(__APPLE__)
    (void)argc;
    (void)argv;
    snprintf(output, sizeof(output), "unsupported platform\n");
    return output;
#else
    int user_argc = argc > 0 ? argc - 1 : 0;
    char **user_argv = argc > 1 ? argv + 1 : NULL;
    int raw_mode = 0;

    for (int i = 0; i < user_argc; i++) {
        const char *a = user_argv[i];
        if (!a || !*a) {
            continue;
        }
        if (str_equals(a, "help") || str_equals(a, "-h") || str_equals(a, "--help")) {
            size_t used = 0;
            print_usage(&used);
            return output;
        }
        if (str_equals(a, "raw")) {
            raw_mode = 1;
            continue;
        }
        {
            size_t used = 0;
            appendf(&used, "[-] Unsupported argument: %s\n", a);
            print_usage(&used);
            return output;
        }
    }

    return run_info(raw_mode);
#endif
}
