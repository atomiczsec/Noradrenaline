# mdm_policy_artifacts

Assesses local MDM or endpoint-management posture using observable macOS and Linux artifacts. macOS scoring checks configuration profiles, managed preferences, ADE, and vendor artifacts. Linux output is best effort because management approaches vary by distribution and fleet tooling.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `mdm_policy_artifacts` | none | Scores observable MDM or endpoint-management indicators. |

## Example Output

```text
[+] Indicator: Configuration profile store
    Provenance: /var/db/ConfigurationProfiles/Store
    Value: present
    Score: +2

[+] Posture Verdict
    MDM posture: Partially enrolled or managed
    Score: 4/10
    Note: reports only locally observable artifacts.
```
