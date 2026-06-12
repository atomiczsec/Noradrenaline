# netjoin_query

Reports local hostname, OS version, and macOS/Linux directory, realm, or workgroup join evidence. Reports local configuration artifacts, not live directory-controller reachability.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `netjoin_query` | none | Reports hostname, OS version, and local domain or realm evidence. |

## Example Output

```text
[*] Querying join information...
Join Type: Linux domain/realm evidence
Computer Name: workstation01
    SSSD domain: domains = corp.example.com
    Provenance: /etc/sssd/sssd.conf
OS Version: Linux 6.8.0 x86_64
Join Evidence Count: 1
[*] query completed
```
