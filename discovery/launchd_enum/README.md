# launchd_enum

Assesses macOS current-user launchd persistence surface using query-only directory existence checks. Reports whether `~/Library/LaunchAgents` and `~/Library/LaunchDaemons` exist, then scores user persistence posture as present or absent.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `launchd_enum` | none | Scores current-user LaunchAgents/LaunchDaemons directory presence. |

## Scoring

| Indicator | Points |
|-----------|--------|
| `~/Library/LaunchAgents` exists | +4 |
| `~/Library/LaunchDaemons` exists | +4 |

Maximum score is 8/10 when both directories exist. The Posture Verdict reports **present** if either directory exists, otherwise **absent**.

## Example Output

```text
[+] launchd_enum
    Platform model: macOS current-user launchd persistence surface
    Home: /Users/operator

[+] Indicator: User LaunchAgents directory
    Path: /Users/operator/Library/LaunchAgents
    Status: present
    Score: +4
    Modified: 2026-08-30 14:30:00 PDT (epoch 1756594200)

[i] Indicator: User LaunchDaemons directory
    Path: /Users/operator/Library/LaunchDaemons
    Status: absent

[+] Posture Verdict
    User persistence surface: present
    Score: 4/10
    Note: directory existence only; no launchctl, listings, or plist reads.
```

## OPSEC Notes

- Query-only: uses in-process `stat` on two user-home directories; no process spawn.
- When a directory is present, output includes `st_mtime` from the same `stat` call (human-readable time plus Unix epoch).
- No `launchctl`, directory listings, label enumeration, or plist parsing.
- Current user only: checks `~/Library/LaunchAgents` and `~/Library/LaunchDaemons`; skips `/Library`, `/System`, and `/System/Library/LaunchDaemons`.

## Build

```bash
make -C discovery/launchd_enum macos
```

On Linux hosts, `make -C discovery/launchd_enum linux` is a no-op skip so catalog builds continue.

## Test

The module includes a `native_runner` fixture test that drives `HOME` to a fake profile so present vs absent scoring is deterministic. Run on macOS after building the module and runner:

```bash
make -C discovery/launchd_enum macos
make -C tests/native_runner
make -C discovery/launchd_enum test
```

Or run the script directly:

```bash
./discovery/launchd_enum/test_native.sh
```
