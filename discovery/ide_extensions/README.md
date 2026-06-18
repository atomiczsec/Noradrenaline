# ide_extensions

Enumerates installed VS Code-family IDE extensions from the current user's profile on macOS and Linux.

## Supported Artifacts

```text
~/.vscode/extensions
~/.vscode-insiders/extensions
~/.cursor/extensions
~/.windsurf/extensions
~/.vscode-oss/extensions
~/.vscode-server/extensions
~/.cursor-server/extensions
~/.vscode-remote/extensions
```

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `ide_extensions` | none | Lists installed extensions from VS Code, Cursor, Windsurf, VSCodium, and remote-server extension roots. |

## Build

```bash
make -C discovery/ide_extensions all
```

## Example Output

```text
[i] Enumerating VS Code-family IDE extensions
[i] home: /Users/operator

[+] Cursor (11 extensions)
  - ms-python.python v2025.6.1
  - anysphere.remote-ssh v1.1.4
  - anthropic.claude-code v2.1.181
  - bierner.markdown-mermaid v1.32.1

[i] ide_extensions summary: hosts=1 extensions=11 truncated=no
[+] ide_extensions complete
```
