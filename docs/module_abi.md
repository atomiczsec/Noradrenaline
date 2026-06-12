# Noradrenaline Module ABI

Noradrenaline modules are Linux `.so` and macOS `.dylib` shared libraries with exported C functions.

On Poseidon, macOS `.dylib` artifacts run through `execute_library`. Linux `.so` execution is not yet available on public Poseidon agents, though other agents may already support it. Linux support for public agents is actively in development.

## Poseidon Workflow (macOS)

1. Build a module for the callback OS and CPU architecture.
2. Upload the `.dylib` to Mythic.
3. Run `execute_library` with the uploaded file, exported function name, and string arguments.

## Export ABI

Every callable function must export a plain C symbol with this signature:

```c
char *function_name(int argc, char **argv);
```

Use `extern "C"` for C++ modules.

Poseidon passes the agent process path as `argv[0]`. User-supplied arguments begin at `argv[1]`. The returned string is copied into task output, so return a stable pointer such as a static buffer or allocated string that remains valid after the function returns.

## Platform Notes

- macOS modules use `.dylib` and must match the callback architecture: `arm64`, `x86_64`, or universal.
- Linux modules use ELF `.so` and must match the callback architecture.
- Keep module output bounded. Large strings can create noisy task output and stress transport handling.
- A crash in module code can crash the Poseidon callback because execution is in-process.
