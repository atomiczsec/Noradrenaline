# firewall_rule

Query-only macOS and Linux firewall posture module. Reads readable local firewall configuration sources for PF, Application Firewall, nftables, iptables, ufw, and firewalld evidence. `add` and `remove` return an unsupported message in this v1 build.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `firewall_rule` | `list` | Lists rule-like lines from readable firewall configuration sources, capped at 200 rows. |
| `firewall_rule` | `query <name-or-pattern>` | Searches readable firewall configuration sources for a pattern. |
| `firewall_rule` | `add ...` or `remove ...` | Returns a clear unsupported message in this v1 query-only build. |

## Example Output

```text
[i] Enumerating readable firewall configuration sources (query-only v1)
[i] Backend hints: nftables, iptables-persistent, ufw, and firewalld config files.
[+] /etc/ufw/user.rules:20: ### tuple ### allow tcp 4444 0.0.0.0/0 any 0.0.0.0/0 in
[i] Source matches: /etc/ufw/user.rules (1)
[i] Matches: 1
[i] Note: elevated rights may be required to inspect complete live firewall state.
```
