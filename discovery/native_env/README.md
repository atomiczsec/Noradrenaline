# native_env

Lists environment variables or returns a single environment variable from the current agent process.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `list_env` | `[prefix] [limit]` | Lists environment variables, optionally filtered by prefix. |
| `get_env` | `NAME` | Returns the value of one environment variable. |

## Example Output

```text
Environment variables matching prefix 'PATH' (limit 5):
PATH=/usr/local/bin:/usr/bin:/bin
shown=1
```
