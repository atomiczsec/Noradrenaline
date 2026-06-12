#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char *(*native_entry_t)(int argc, char **argv);

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [--agent-path PATH] module function [args...]\n"
            "\n"
            "Run a Noradrenaline shared library export locally.\n"
            "\n"
            "arguments:\n"
            "  module          Path to a .dylib or .so\n"
            "  function        Exported symbol to call\n"
            "  args            String args passed to the export beginning at argv[1]\n"
            "\n"
            "options:\n"
            "  --agent-path    Simulated Poseidon process path passed as argv[0]\n",
            prog);
}

int main(int argc, char **argv) {
    const char *agent_path = "/poseidon/native_runner";
    int argi = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[argi], "--agent-path") == 0) {
        if (argc <= argi + 1) {
            usage(argv[0]);
            return 2;
        }
        agent_path = argv[argi + 1];
        argi += 2;
    }

    if (argc - argi < 2) {
        usage(argv[0]);
        return 2;
    }

    const char *module_path = argv[argi++];
    const char *function_name = argv[argi++];

    void *handle = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed for %s: %s\n", module_path, dlerror());
        return 1;
    }

    dlerror();
    native_entry_t fn = (native_entry_t)dlsym(handle, function_name);
    const char *err = dlerror();
    if (err != NULL) {
        fprintf(stderr, "dlsym failed for %s: %s\n", function_name, err);
        dlclose(handle);
        return 1;
    }

    int user_argc = argc - argi;
    int native_argc = user_argc + 1;
    char **native_argv = calloc((size_t)native_argc + 1, sizeof(char *));
    if (native_argv == NULL) {
        fprintf(stderr, "calloc failed\n");
        dlclose(handle);
        return 1;
    }

    native_argv[0] = (char *)agent_path;
    for (int i = 0; i < user_argc; i++) {
        native_argv[i + 1] = argv[argi + i];
    }

    char *result = fn(native_argc, native_argv);
    if (result != NULL) {
        puts(result);
    }

    free(native_argv);
    dlclose(handle);
    return 0;
}
