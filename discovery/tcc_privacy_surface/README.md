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
[+] Indicator: User TCC database
    Status: present
    Size: 114688 bytes
    Owner UID: 501
    Mode: 0600
    Meaning: Stores current-user privacy decisions; presence does not reveal which apps are allowed.

[+] Indicator: System Integrity Protection
    Value: enabled
    Active exceptions: none

[+] Posture Verdict
    Privacy posture: Mixed
    Score: 5/10
```

macOS only. Export: `tcc_privacy_surface`.
