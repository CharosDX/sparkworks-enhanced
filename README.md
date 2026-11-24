# Sparkworks Enhanced

An Enshrouded mod: new logic nodes and smarter trigger areas for your Sparkworks builds.

**Download:** [Nexus Mods](https://www.nexusmods.com/enshrouded/mods/360)
How to install, the trigger area tags, settings, co-op and troubleshooting are explained in the [mod README](mod/README.md).

## What it does

- **5 new nodes** in their own logic editor category: Environment (time of day and weather), Random, Random Timer, Counter, Alert (center-screen messages and game sounds).
- **Smart trigger areas:** a tag in the area's name lets it count who is inside `(2)`, react only to some players `(@Name)` or everyone but them `(!Name)`, only to villagers `(NPC)` or only to players `(!NPC)`, or watch chests `(Chest open)`, `(Chest 80%)`.
- **Bigger trigger areas:** up to 16 m instead of 8 m (up to 64 m in the settings).
- Solo, co-op and dedicated servers. English and German.

## How it works

This mod goes deeper into the game than most, so here is what it changes.

`sparkworks-enhanced.dll` patches the game code in memory while it runs. Nothing on disk is changed, and the game's files stay untouched.

- **New node types:** the logic editor, the graph compiler and the logic VM learn five new node types and their operations. The editor parts (menu, category, names, settings panel) are skipped on a dedicated server, where there is no editor.
- **Save safety:** in the save, every new node is stored as a vanilla Timed Trigger, with its settings encoded in the trigger's times. A game without the mod loads the world normally and sees plain Timed Triggers. With the mod they turn back into the new nodes.
- **Edits from players without the mod:** they are ignored for the new nodes, so they cannot overwrite the settings.
- **Trigger areas:** the mod reads the area's name and changes only the result of tagged areas. Untagged areas run unchanged.
- **Messages:** sent as the game's own puzzle-completed event, which shows the "Door opened" / "Alarm" banners.
- **Sounds:** sent as the game's own chest-deposit event with no chests in it, which makes every player's game play the chosen sound at the given object.
- **Weather:** the game's own weather lookup (the one that makes players wet in the rain) is asked for the weather at the graph's first object.

Every place the mod changes is found by a byte pattern and checked before patching. If the game code doesn't match (for example after a game update), the mod changes nothing and says so in its log (`debug.log`).

`dinput8.dll` is a small loader. The game loads it at start; it passes the game's DirectInput calls on to the real Windows `dinput8.dll` and loads every DLL in `mods\<mod>\native\`.

## Repository

| Path | Content |
|---|---|
| `src/sparkworks_enhanced.c` | the mod |
| `src/dinput8_proxy.c` | the loader |
| `src/*.rc` | version details of the DLL files |
| `mod/` | `mod.json` and the README that ship with the mod |
| `build.bat` | builds both DLLs into `build\` |

## Building

Needs Visual Studio 2022 or newer with "Desktop development with C++". Run `build.bat`; the DLLs end up in `build\`.
For a working install, copy `build\dinput8.dll` next to `enshrouded.exe` (or `enshrouded_server.exe`), and `mod\mod.json`, `mod\README.md` and `build\sparkworks-enhanced.dll` into `Enshrouded\mods\sparkworks-enhanced\` (the DLL into its `native` subfolder).

## About AI use

This mod was made in collaboration with Claude (Anthropic). Claude did the reverse engineering of the game, wrote the code and the translations, and ran the automated tests. I came up with the ideas and the design, decided how each feature should work, and tested everything in game, reporting problems until it worked the way I wanted.

## License

[MIT](LICENSE)
