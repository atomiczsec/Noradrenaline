# launchd_enum

Lists current-user launchd plist files in `~/Library/LaunchAgents` and `~/Library/LaunchDaemons`.

## Output

For each plist: filename, path, modified time, label, and `Program` or the first `ProgramArguments` value when available. The output is capped at 48 plists per directory.

## Build

```bash
make -C discovery/launchd_enum macos
```

## Run

```bash
make -C tests/native_runner
tests/native_runner/build/native_runner \
  discovery/launchd_enum/build/launchd_enum-macos-arm64.dylib launchd_enum
```

## Example

```text
[+] launchd_enum
    Platform model: macOS current-user launchd persistence surface
    Home: /Users/operator

[+] Indicator: User LaunchAgents directory
    Path: /Users/operator/Library/LaunchAgents
    Status: present
    Score: +4
    Modified: 2026-08-28 16:05:22 EDT (epoch 1787947522)

[+] User LaunchAgents inventory
    Directory: /Users/operator/Library/LaunchAgents
    Plist files: 2

    [1] com.google.GoogleUpdater.wake.plist
        Path: /Users/operator/Library/LaunchAgents/com.google.GoogleUpdater.wake.plist
        Modified: 2026-08-21 09:42:14 EDT (epoch 1787319734)
        Label: com.google.GoogleUpdater.wake
        ProgramArguments[0]: /Users/operator/Library/Application Support/Google/GoogleUpdater/Current/GoogleUpdater.app/Contents/MacOS/GoogleUpdater

    [2] com.vendor.helper.plist
        Path: /Users/operator/Library/LaunchAgents/com.vendor.helper.plist
        Modified: 2026-08-28 16:05:22 EDT (epoch 1787947522)
        Label: unavailable (binary or nonstandard plist)

[i] Indicator: User LaunchDaemons directory
    Path: /Users/operator/Library/LaunchDaemons
    Status: absent

[+] Posture Verdict
    User persistence surface: present
    Score: 4/10
```
