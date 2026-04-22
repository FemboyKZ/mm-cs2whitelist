# CS2 Whitelist

Metamod: Source plugin for CS2 server whitelisting. Restricts server access to a list of SteamIDs and IP addresses defined in a text file.

## Features

- Allow/deny by **SteamID** (`STEAM_0:X:Y` or SteamID64) or **IPv4 address**
- **Admin immunity** - any player with a cs2admin flag can bypass the whitelist check (requires [mm-cs2admin](https://github.com/FemboyKZ/mm-cs2admin))
- Ingame **admin commands** with flag-based access control via mm-cs2admin
- Whitelist reloads automatically on every map change
- Thread-safe, synchronous - no database required

## Installation

1. Download the [latest release](https://github.com/FemboyKZ/mm-cs2whitelist/releases/latest) and extract it in your server's root folder (`~/game/csgo/`)
2. Create a new file or rename the .example to `whitelist.txt` and add your selection of people's IDs/IPs to the file.
3. (Optional) Install [mm-cs2admin](https://github.com/FemboyKZ/mm-cs2admin) to enable in-game admin commands and admin immunity.

## Configuration

`cfg/cs2whitelist/whitelist.txt` - one entry per line:

Supported entry types:

| Format | Example |
| --- | --- |
| Steam2 SteamID | `STEAM_0:1:12345678` |
| SteamID64 | `76561198012345678` |
| IPv4 address | `192.168.1.100` |

## ConVars

| ConVar | Default | Description |
| --- | --- | --- |
| `mm_whitelist_enable` | `1` | Enable (1) or disable (0) the whitelist. |
| `mm_whitelist_immunity` | `1` | Skip the check for players with any cs2admin flag. |
| `mm_whitelist_kickmessage` | `You are not whitelisted on this server.` | Console message sent to kicked players. |
| `mm_whitelist_filename` | `whitelist.txt` | File name inside `cfg/cs2whitelist/`. |

## Admin commands

All commands require [mm-cs2admin](https://github.com/FemboyKZ/mm-cs2admin) for in-game use.
Without it, commands can only be issued from the **server console**.

| Command | Required flag | Description |
| --- | --- | --- |
| `mm_whitelist_status` | `b` (Generic) | Show status, entry count, and file name. |
| `mm_whitelist_list` | `b` (Generic) | Print all whitelist entries. |
| `mm_whitelist_reload` | `h` (ConVars) | Reload whitelist from disk. |
| `mm_whitelist_add <id\|ip>` | `e` (Unban) | Add a SteamID or IP and save. |
| `mm_whitelist_remove <id\|ip>` | `e` (Unban) | Remove a SteamID or IP and save. |

## Build

### Prerequisites

- This repository is cloned recursively (ie. has submodules)
- [python3](https://www.python.org/)
- [ambuild](https://github.com/alliedmodders/ambuild), make sure `ambuild` is in your `PATH`
- MSVC (VS build tools) on Windows / Clang on Linux

### AMBuild

```bash
mkdir -p build && cd build
python3 ../configure.py --enable-optimize
ambuild
```

## Credits

- [ServerWhitelistAdvanced](https://forums.alliedmods.net/showthread.php?p=1830686)
- [SourceMod](https://github.com/alliedmodders/sourcemod)
- [zer0.k's MetaMod Sample plugin fork](https://github.com/zer0k-z/mm_misc_plugins)
- [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod)

## Contributing

Feel free to create PRs or issues regarding anything, but this is very much just for fkz and I will not implement bloat like funcommands.
