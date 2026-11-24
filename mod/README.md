# Sparkworks Enhanced

New logic nodes and smarter trigger areas for your Sparkworks builds.
The logic editor gets a new gold category, **Sparkworks Enhanced**, at the end of the category list.

## The new nodes

- **Environment:** on at *Night*, during the *Day*, in your own *Time span* (in-game time), or while it is *Rain*, *Snow* or *Clear* where the graph's objects are. No input; combine it with AND.
- **Random:** rolls the dice whenever a signal comes in (5 to 100 %). *Pass through* passes the signal on if the roll works, *Hold* keeps the result until the next signal.
- **Random Timer:** *Delay* switches on after a random time between From and To. With *Signal* set to *On/Off* (default) it also switches off after a random time, with *Only on* right away, with a time for exactly that long. *Clock* switches on and off at random intervals while a signal comes in.
- **Counter:** counts signals at the upper input. From the chosen count on (1 to 50) it passes a signal on, until the lower input sets it back to 0. Counter (1) keeps even a short signal on.
- **Alert:** *Message* shows "Door opened", "Alarm raised" or "Alarm resolved" in the middle of the screen to every player within 200 m. *Sound* plays one of 28 game sounds (signals, animals, enemies, elements, objects) at the objects after the node. Every player hears it, even without the mod.

## Smart trigger areas

Put a tag in brackets anywhere in the area's name, for example `Gate (2)`. The area must be wired into a graph.

- `(2)` at least 2 inside, `(=2)` exactly 2, `(<3)` at least 1 but fewer than 3, `(>1)` more than 1. Who counts is up to the area's own *Triggered by* setting.
- `(@Anna, @Ben)` only reacts to these players, `(!Anna)` to everyone except them. Don't mix @ and !.
- `(NPC)` only reacts to villagers, `(!NPC)` only to players. *Triggered by: Player* also counts villagers; use it together with these tags.
- `(Chest open)` a short signal when a chest is opened. `(Chest 80%)`, `(Chest <20%)`, `(Chest >50%)`, `(Chest =0%)` react to how full the chests are (occupied slots). An area around several chests adds them up; a small area on top of a chest reads only that chest. German: `(Kiste offen)`, `(Kiste 80%)`.

Trigger areas can be up to 16 m big (up to 64 m in the settings).

## Installation

1. Extract the archive into your Enshrouded folder (next to enshrouded.exe). You get `dinput8.dll` (small loader) and the `mods\sparkworks-enhanced` folder.
2. Start the game.

My mods Cross-Base Storage and Craft From Stations (Queue Fix) use the very same `dinput8.dll`; overwriting it is fine. Don't overwrite the dinput8.dll of other authors' mods; use the Multi DLL Installer instead.

## Co-op and dedicated servers

**The mod must be installed on the host, or on the dedicated server** (extract next to enshrouded_server.exe). Guests need it to see and edit the new nodes; without it their changes to them are ignored. Messages and sounds reach every guest, mod or not. Everyone should use the same version. Linux servers are untested (try `WINEDLLOVERRIDES="dinput8=n,b"`).

## Settings

On the first start the mod creates `mods\sparkworks-enhanced\native\config.ini`. Restart the game after changing it.

```
colour=#A87D12  ; colour of the category and the nodes
area_size=16    ; largest trigger area in metres (8 to 64)
debug=0         ; 1 = write more details to debug.log
```

## Good to know

- Right after loading a world, tagged areas need a few seconds before they react. A renamed tag works after about 2 seconds.
- Counters start at 0 every time a world is loaded.
- Weather is checked at the first object of the graph, about once a second. *Snow* is untested so far; please report if it does not react.
- Without the mod your world still loads: the new nodes become plain Timed Triggers and stop working, tagged areas work like normal ones.

## Uninstall

1. Remove the `sparkworks-enhanced` folder from `Enshrouded\mods\`.
2. Delete `dinput8.dll` from the Enshrouded folder, unless another mod uses it.

## Troubleshooting

`native-loader.log` and `debug.log` are in `mods\sparkworks-enhanced\native\`. debug.log should say `nodes active`; `NOT active` means the game was updated and the mod needs an update too. Set `debug=1`, reproduce the problem and attach both logs to a bug report on Nexus.
