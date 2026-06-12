# native_runner

Generic local runner for Noradrenaline shared library modules.

Use this to load any `.dylib` or `.so`, resolve an exported function, and call it with Poseidon-like arguments:

```c
char *function_name(int argc, char **argv)
```

The runner passes a simulated agent path as `argv[0]`. User arguments begin at `argv[1]`.

## Build

```bash
make -C tests/native_runner
```

## Usage

```bash
tests/native_runner/build/native_runner module function [args...]
```

Optional simulated agent path:

```bash
tests/native_runner/build/native_runner \
  --agent-path /tmp/poseidon \
  module function [args...]
```

## Examples

```bash
tests/native_runner/build/native_runner \
  discovery/native_env/build/native_env-macos-arm64.dylib \
  get_env HOME
```

```bash
tests/native_runner/build/native_runner \
  credential_access/cloud_metadata_check/build/cloud_metadata_check-macos-arm64.dylib \
  cloud_metadata_check presence
```

## Naming

Use the platform-specific terms for artifacts:

- `.dylib` on macOS
- `.so` on Linux

Avoid calling these **objects** in Noradrenaline docs; that term fits Windows BOFs better than dynamic libraries.
