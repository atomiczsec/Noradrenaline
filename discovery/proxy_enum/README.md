# proxy_enum

Enumerates macOS and Linux proxy configuration evidence from environment variables, system configuration files, package-manager settings, and browser policy/profile files. Reports configuration state, not whether a proxy is reachable or enforced.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `proxy_enum` | none | Reports local proxy-related configuration evidence. |

## Example Output

```text
[i] Starting proxy enumeration...

[i] Querying proxy environment variables...
  - HTTPS_PROXY=http://proxy.corp.local:8080

[i] Checking browser policy/profile proxy settings...
  - Chrome managed policy: proxy indicators present
    Provenance: /etc/opt/chrome/policies/managed/proxy.json

[+] Proxy enumeration complete
    Proxy indicator hits: 2
```
