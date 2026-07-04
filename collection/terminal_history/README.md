# terminal_history

Collects current-user terminal history evidence on macOS and Linux. The default mode inventories discovered shell and terminal-recorder artifacts, reports metadata, and returns bounded exact tail previews so large histories do not flood task output.

## Supported Artifacts

- `HISTFILE` when it resolves under the current user's home directory.
- `~/.bash_history`, `~/.zsh_history`, and `~/.zhistory`.
- Fish history from `~/.local/share/fish/fish_history` and `XDG_DATA_HOME/fish/fish_history`.
- Terminal recorder artifacts from common asciinema paths and shallow scans for `.cast`, `typescript`, and `script*.log` files.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `terminal_history` | none | Inventories discovered artifacts and returns bounded exact tail previews. |
| `terminal_history` | `inventory` | Reports discovered artifact metadata without content previews. |
| `terminal_history` | `tail [lines] [bytes]` | Returns bounded tail previews for discovered artifacts. Defaults to 25 lines and 4096 bytes per file; hard-capped at 200 lines and 16384 bytes. |
| `terminal_history` | `read <path> [offset] [bytes]` | Reads an explicit chunk from a regular file under the current user's home directory. Defaults to offset 0 and 8192 bytes; hard-capped at 65536 bytes. |

## Usage

```text
terminal_history
terminal_history inventory
terminal_history tail 10 2048
terminal_history read ~/.zsh_history 0 4096
```

## Example Output

```text
[i] Terminal history tail collection:
[i] artifacts=2 cap=64 capped=no lines=25 bytes_per_file=4096
[+] zsh history: /Users/user/.zsh_history
[i]   size=91234 bytes mtime=2026-07-04 13:37:18 -0400 readable=yes
[i]   tail preview begin offset=87210 bytes=4024 max_lines=25 truncated_before=yes
git status
make build
[i]   tail preview end
[+] terminal recorder: /Users/user/.local/share/asciinema/demo.cast
[i]   size=4821 bytes mtime=2026-07-02 09:14:01 -0400 readable=yes
[i]   tail preview begin offset=725 bytes=4096 max_lines=25 truncated_before=yes
{"version":2,"width":120,"height":32}
[i]   tail preview end
[+] terminal_history complete
```
