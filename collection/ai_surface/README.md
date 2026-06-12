# ai_surface

Maps AI tooling, AI agent profile artifacts, and MCP configuration files on macOS and Linux developer endpoints. Performs bounded filesystem enumeration from the current user's home directory and prints concise triage output with short previews for discovered MCP config files.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `ai_surface` | none | Enumerates common AI desktop, agent, VS Code/Cursor/Windsurf, and MCP artifact paths. |

## Example Output

```text
[i] Mapping AI developer surface:
[i] home: /Users/user

[i] GitHub Copilot:
[+] VS Code Copilot: /Users/user/Library/Application Support/Code/User/globalStorage/github.copilot
[+] VS Code workspaceStorage: /Users/user/Library/Application Support/Code/User/workspaceStorage

[i] Third-party AI:
[+] Claude: /Users/user/Library/Application Support/Claude
[+] Cursor: /Users/user/Library/Application Support/Cursor
[+] Ollama: /Users/user/.ollama

[i] AI Agent Artifacts:
[+] Codex profile: /Users/user/.codex
[i]   sessions
[i]   auth/config
[i]   history
[i]   skills
[+] Claude Code profile: /Users/user/.claude
[i]   sessions/projects
[i]   settings
[i] AI agent artifact summary: Codex=yes Claude Code=yes

[i] MCP Configuration Discovery:
[+] Claude Desktop MCP Config: /Users/user/Library/Application Support/Claude/claude_desktop_config.json
[i]   Size: 612 bytes
[i]   Preview:
[i]     { "mcpServers": { "filesystem": { "command": "npx", "args": [ "-y", "@modelcontextprotocol/server-filesystem" ] } } }
[i] MCP summary: 1 artifacts, 1 previews, 0 preview errors

[i] AI surface summary: paths=5 agent_profiles=2 mcp_configs=1 extension_hits=0 project_hits=0
[+] ai_surface complete
```
