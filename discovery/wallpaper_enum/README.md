# wallpaper_enum

Enumerates desktop wallpaper paths on macOS and Linux. Centralized wallpapers can reveal internal SMB or NFS shares, imaging infrastructure, domain naming, or managed endpoint policy without touching the network.

## Supported OS

| OS | Architecture | Artifact | Status | Notes |
|----|--------------|----------|--------|-------|
| macOS | arm64 | `.dylib` | Supported | Uses AppKit to query each attached display in a GUI session. |
| macOS | x86_64 | `.dylib` | Supported | Built as part of the universal dylib. |
| Linux | x86_64 | `.so` | Supported | Uses file-only desktop environment configuration discovery. |
| Linux | arm64 | `.so` | Supported | Build with a Linux host, cross-compiler, or Docker. |

## Supported Artifacts

```text
build/wallpaper_enum-macos-arm64.dylib
build/wallpaper_enum-macos-x86_64.dylib
build/wallpaper_enum-macos-universal.dylib
build/wallpaper_enum-linux-x86_64.so
build/wallpaper_enum-linux-arm64.so
```

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `wallpaper_enum` | none | Reports local wallpaper path evidence from macOS displays or Linux desktop configuration files. |

## Build

Build for the current host platform:

```bash
make -C discovery/wallpaper_enum all
```

Build macOS artifacts on a macOS host:

```bash
make -C discovery/wallpaper_enum macos
```

Build Linux artifacts on a Linux host, with local cross-compilers, or with Docker:

```bash
make -C discovery/wallpaper_enum linux
make -C discovery/wallpaper_enum linux-docker
```

## Poseidon Usage

Load the module with the custom `native_import` workflow, call the exported symbol, then unload it when finished:

```text
native_import wallpaper_enum-macos-universal.dylib
native_call wallpaper_enum wallpaper_enum
native_unload wallpaper_enum
```

## Example Output

macOS:

```text
[i] Starting wallpaper enumeration...
[i] Found 2 display(s)
[+] Display 0 (NSScreen:69734662): /Users/operator/Pictures/wall.jpg
[+] Display 1 (NSScreen:69734663): /Volumes/corp-share/wallpapers/branded.jpg
[i] Enumeration complete
    Wallpaper path hits: 2
```

Linux:

```text
[i] Starting wallpaper enumeration...

[i] Querying Linux desktop environment wallpaper settings...
[+] GNOME dconf wallpaper: file:///mnt/nfs/corp/wallpaper.jpg
    Provenance: /etc/dconf/db/local.d/00-background
[+] KDE Image: /home/operator/Pictures/wall.jpg
    Provenance: /home/operator/.config/plasma-org.kde.plasma.desktop-appletsrc

[i] Enumeration complete
    Wallpaper path hits: 2
```

## Operator Notes

macOS enumeration uses `NSWorkspace` and `NSScreen`, so it is most reliable from an interactive GUI session. If AppKit cannot see displays, the module reports that condition instead of falling back to stale preference parsing.

Linux enumeration is intentionally file-only and does not spawn `gsettings`, `dconf`, DBus helpers, or shell commands. It checks enterprise GNOME dconf keyfiles, KDE Plasma, Xfce, feh, nitrogen, sway, and a bounded best-effort scan of the GNOME user dconf database for `file://` URI strings. Desktop environment coverage varies by distribution and version.

## Telemetry Notes

The module does not make network connections or set wallpapers. On macOS, defenders may observe AppKit/Foundation framework loading and native module image-load telemetry. On Linux, defenders may observe reads from user desktop configuration files, `/etc/dconf/db/`, and native `.so` loading through the Poseidon native module workflow.
