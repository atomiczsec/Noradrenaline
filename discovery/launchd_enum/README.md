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
[+] User LaunchAgents inventory
    Plist files: 1

    [1] com.example.agent.plist
        Label: com.example.agent
        ProgramArguments[0]: /usr/local/bin/example-agent
```
