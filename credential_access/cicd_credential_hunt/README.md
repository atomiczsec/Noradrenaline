# cicd_credential_hunt

Finds common CI/CD and developer credential artifacts in the current user's home directory. Reports path and size only; it does not read file contents.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `cicd_credential_hunt` | *(none)* | Presence mode: paths and sizes only; no file-content reads. Extra arguments are ignored. |

## Checks

- Cloud: AWS credentials/config/cache, Google Cloud ADC, Kubernetes config, Terraform credentials/config, and Vault token
- Source control: GitHub CLI, GitLab CLI, Git credential store, and `.netrc`
- Packages: npm, PyPI, Cargo, Maven, Gradle, and RubyGems
- Containers: Docker and `containers/auth.json`
- SSH: common private-key names and up to eight top-level `*.pem` files

Git and SSH configs are reported separately as context. Missing paths are silent. AWS cache and SSH PEM scans are bounded to eight files each.

## Example Output

```text
[i] Enumerating CI/CD credential artifacts on macOS developer paths
[i] Mode: presence (existence only)
[+] GitHub CLI auth: /Users/alice/.config/gh/hosts.yml
[i]   Size: 88 bytes
[+] AWS shared credentials: /Users/alice/.aws/credentials
[i]   Size: 312 bytes
[+] Kubernetes config: /Users/alice/.kube/config
[i]   Size: 2841 bytes
[+] Git config context: /Users/alice/.gitconfig
[i]   Size: 328 bytes
[i] Summary: credential artifacts=3, config context=1, ssh key candidates=0
```

Empty or unpopulated home:

```text
[i] Enumerating CI/CD credential artifacts on Linux developer paths
[i] Mode: presence (existence only)
[i] Summary: credential artifacts=0, config context=0, ssh key candidates=0
```

## Local Testing

Build the module and `native_runner`, then run the fake-home fixture test:

```bash
make -C credential_access/cicd_credential_hunt
make -C tests/native_runner
./credential_access/cicd_credential_hunt/test_native.sh
```

The script uses empty fake-home fixtures and asserts every path group. No real secrets are placed in fixtures.

## Operational Notes

- Presence-only: it never reads credential file contents.
- Current-user only: no subprocesses, recursion, or keychain access.
