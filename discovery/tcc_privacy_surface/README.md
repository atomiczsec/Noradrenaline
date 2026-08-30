# tcc_privacy_surface

Build and run a current-user macOS privacy check:

```bash
make -C discovery/tcc_privacy_surface macos
make -C tests/native_runner
tests/native_runner/build/native_runner \
  discovery/tcc_privacy_surface/build/tcc_privacy_surface-macos-arm64.dylib \
  tcc_privacy_surface
```

## What you get

- TCC database path, status, size, owner, mode, and modified time
- SIP state plus active protection exceptions
- FileVault escrow and Screen Time preference evidence
- Plain-English meaning for every result

The module does not read TCC database contents or spawn system commands.

## Example

```text
[+] tcc_privacy_surface
    Platform model: macOS current-user privacy/TCC posture
    Home: /Users/operator

[+] Indicator: User TCC database
    Path: /Users/operator/Library/Application Support/com.apple.TCC/TCC.db
    Status: present
    Modified: 2026-08-24 19:46:16 EDT (epoch 1787615176)
    Type: regular file
    Size: 118784 bytes
    Owner UID: 501
    Mode: 0600
    Score: +2
    Meaning: Stores current-user privacy decisions; presence does not reveal which apps are allowed.

[+] Indicator: System Integrity Protection
    Provenance: csr_get_active_config / sysctl
    Value: relaxed (config=0x20)
    Active exceptions:
      - unrestricted DTrace (0x020)
    Meaning: SIP protects system files and runtime operations; listed exceptions show relaxed controls.
    Score: +1

[+] Indicator: FileVault FDE escrow preferences
    Path: /Users/operator/Library/Preferences/com.apple.security.FDERecoveryKeyEscrow.plist
    Status: present
    Modified: 2026-08-18 11:14:30 EDT (epoch 1787066070)
    Type: regular file
    Size: 842 bytes
    Owner UID: 501
    Mode: 0600
    Score: +2
    Meaning: An escrow preference artifact exists; this does not prove FileVault is enabled.

[i] Indicator: Screen Time preferences
    Path: /Users/operator/Library/Preferences/com.apple.ScreenTimeAgent.plist
    Status: absent
    Meaning: No current-user Screen Time preference artifact was found.

[+] Posture Verdict
    Privacy posture: Mixed
    Score: 5/10
    Evidence collected: file metadata and in-process SIP state; TCC.db contents were not read.
```

macOS only. Export: `tcc_privacy_surface`.
