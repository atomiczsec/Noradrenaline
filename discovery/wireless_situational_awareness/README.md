# wireless_situational_awareness

Rootless macOS Wi-Fi and Bluetooth situational awareness for Poseidon `execute_library`. Default output is a shaped operator summary (association, addressing, nearby security mix, preferred SSIDs, Bluetooth controller and paired devices). Pass `raw` for unfiltered source dumps.

Export: `wireless_situational_awareness`

## Build

macOS host required (`clang` + `lipo`). Linux catalog builds skip this module.

```bash
make -C discovery/wireless_situational_awareness macos
```

## Run

```bash
make -C tests/native_runner
tests/native_runner/build/native_runner \
  discovery/wireless_situational_awareness/build/wireless_situational_awareness-macos-arm64.dylib \
  wireless_situational_awareness

tests/native_runner/build/native_runner \
  discovery/wireless_situational_awareness/build/wireless_situational_awareness-macos-arm64.dylib \
  wireless_situational_awareness raw
```

## Arguments

| Argument | Description |
|----------|-------------|
| _(none)_ | Shaped Wi-Fi + Bluetooth summary |
| `raw` | Unfiltered `system_profiler` / `ipconfig` / `networksetup` dumps |
| `help` / `-h` | Usage |

Sources (posix_spawn, no shell): `system_profiler SPAirPortDataType`, `ipconfig getsummary en0`, `networksetup -listpreferredwirelessnetworks en0`, `system_profiler SPBluetoothDataType`.
