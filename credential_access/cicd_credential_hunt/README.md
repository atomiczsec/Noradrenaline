# cicd_credential_hunt

Finds common CI/CD and developer credential artifacts in the current user's home directory. Reports path and size only; it does not read file contents.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `cicd_credential_hunt` | *(none)* | Presence mode: paths and sizes only; no file-content reads. Extra arguments are ignored. |

## Checks

- GitHub CLI: `~/.config/gh/hosts.yml` and, on macOS only, `~/Library/Application Support/GitHub CLI/hosts.yml`
- Package and container credentials: `~/.npmrc`, `~/.pypirc`, and `~/.docker/config.json`
- Git: `~/.git-credentials` and `~/.gitconfig`
- SSH: `~/.ssh/config`, `id_rsa`, `id_ed25519`, `id_ecdsa`, `identity`, and up to eight top-level `*.pem` files in `~/.ssh`

Missing paths are silent. Discovery is read-only, limited to the current user's home, and does not recurse beyond the top level of `~/.ssh`.

## Example Output

```text
[i] Enumerating CI/CD credential artifacts on Linux developer paths
[i] Mode: presence (existence only)
[+] GitHub CLI auth: /home/alice/.config/gh/hosts.yml
[i]   Size: 191 bytes
[+] SSH private key: /home/alice/.ssh/id_ed25519
[i]   Size: 411 bytes
[i] Summary: fixed artifacts=1, ssh configs=0, ssh key candidates=1
```

Empty or unpopulated home:

```text
[i] Enumerating CI/CD credential artifacts on Linux developer paths
[i] Mode: presence (existence only)
[i] Summary: fixed artifacts=0, ssh configs=0, ssh key candidates=0
```

## Local Testing

Build the module and `native_runner`, then run the fake-home fixture test:

```bash
make -C credential_access/cicd_credential_hunt
make -C tests/native_runner
./credential_access/cicd_credential_hunt/test_native.sh
```

The script creates temporary empty and populated home directories, touches the listed artifact paths with empty files, and asserts hit counts via `native_runner`. No real secrets are placed in fixtures.

On macOS, the populated fixture also creates `~/Library/Application Support/GitHub CLI/hosts.yml`. On Linux, that macOS-only path is skipped by the module and omitted from fixture expectations.

## Operational Notes

- Use this module for presence-only discovery. It never reads credential file contents.
- Discovery is limited to the current user's `HOME` and does not spawn subprocesses or access keychain material.
