# tcc_privacy_surface

Assesses macOS current-user privacy and hardening posture using query-only path existence checks and in-process SIP queries. Reports observable indicators for the user TCC database path, System Integrity Protection, FileVault recovery escrow preferences, and Screen Time preferences, then scores posture as Hardened, Mixed, or Permissive.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `tcc_privacy_surface` | none | Scores current-user privacy/TCC posture indicators. |

## Example Output

```text
[+] tcc_privacy_surface
    Platform model: macOS current-user privacy/TCC posture
    Home: /Users/operator

[+] Indicator: User TCC database
    Path: /Users/operator/Library/Application Support/com.apple.TCC/TCC.db
    Status: present
    Score: +2

[+] Indicator: System Integrity Protection
    Provenance: csr_get_active_config / sysctl
    Value: enabled
    Score: +3

[i] Indicator: FileVault FDE escrow preferences
    Path: /Users/operator/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist
    Status: absent

[+] Indicator: Screen Time preferences
    Path: /Users/operator/Library/Preferences/com.apple.ScreenTimeAgent.plist
    Status: present
    Score: +2

[+] Posture Verdict
    Privacy posture: Mixed
    Score: 7/10
    Note: query-only path checks for the current user; no TCC.db reads.
```

## OPSEC Notes

- Query-only: uses `stat` for user-home paths and never opens or reads `TCC.db`.
- No process spawn: does not invoke `fdesetup`, `csrutil`, `launchctl`, `sqlite3`, or similar tools.
- Current user only: checks `~/Library` paths and skips system TCC stores, `/Library` management databases, Gatekeeper databases, and FDA lists.
- SIP is queried in-process via `csr_get_active_config` and/or `sysctl`; if SIP state cannot be determined, the module reports `[!]` and does not treat unknown as a zero score.

## Build

```bash
make -C discovery/tcc_privacy_surface macos
```

On Linux hosts, `make -C discovery/tcc_privacy_surface linux` is a no-op skip so catalog builds continue.

## Test

The module includes a `native_runner` fixture test that drives `HOME` to a fake profile so path present/absent scoring is deterministic. Run on macOS after building the module and runner:

```bash
make -C discovery/tcc_privacy_surface macos
make -C tests/native_runner
make -C discovery/tcc_privacy_surface test
```

Or run the script directly:

```bash
./discovery/tcc_privacy_surface/test_native.sh
```
