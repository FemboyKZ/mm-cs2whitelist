# cs2whitelist Commands

All commands are console commands (server console or client console). There are no chat aliases.
The server console always has full access.

## Permissions

**Permission check order:**

1. Group overrides (`admin_groups.cfg`).
2. Global overrides (`cfg/cs2admin/admin_overrides.cfg`).
3. Default flag (below).

**If mm-cs2admin is not loaded:**

- Every command is restricted to the server console.

## Commands

| Console                    | Default flag | Description                                   |
| -------------------------- | ------------ | --------------------------------------------- |
| `mm_whitelist_status`      | `b` generic  | Show whitelist status (enabled, counts, file) |
| `mm_whitelist_list`        | `b` generic  | List all loaded whitelist entries             |
| `mm_whitelist_exist`       | `b` generic  | Check if a SteamID/IP is whitelisted          |
| `mm_whitelist_add`         | `e` unban    | Add a SteamID/IP to the whitelist             |
| `mm_whitelist_remove`      | `e` unban    | Remove a SteamID/IP from the whitelist        |
| `mm_whitelist_reload`      | `h` convars  | Reload the whitelist file from disk           |
| `mm_whitelist_cache_clear` | `h` convars  | Clear the per-map whitelist/rejection cache   |

## admin_overrides.cfg examples

```json
"Overrides"
{
    "whitelist_add"        "d"      // require ban flag to edit the whitelist
    "mm_whitelist_remove"  "d"
    "@whitelist"           "b"      // gate every whitelist command at the group level
}
```

## Flag reference

- `a` reservation
- `b` generic
- `c` kick
- `d` ban
- `e` unban
- `f` slay
- `g` changemap
- `h` convars
- `i` config
- `j` chat
- `k` vote
- `l` password
- `m` rcon
- `n` cheats
- `o`-`t` custom 1–6
- `z` root (all access)
