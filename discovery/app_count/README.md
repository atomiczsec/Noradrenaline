# app_count

Counts installed application indicators on macOS and Linux with a bounded, de-duplicated local scan. macOS counts `.app` bundles; Linux counts `.desktop` launcher files.

## Exports

| Function | Arguments | Description |
|----------|-----------|-------------|
| `app_count` | none | Counts unique application indicators from standard local locations. |

## Example Output

```text
[+] app_count
    Source: macOS .app bundles
    Path: /Applications unique_added=54
    Path: /System/Applications unique_added=28
    Readable locations: 2
    Entries scanned: 82
    Unique applications: 82
```
