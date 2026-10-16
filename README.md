# Boat Crew first-person mod

Walk the deck of your own boat in [Boat Crew](https://store.steampowered.com/app/1633370/). Press a key and you are looking out of the eyes of the crewmate you have selected, with mouse look and WASD, while the rest of the crew carry on with their jobs. Press it again and the game is back to normal.

It is one native Windows DLL, built from the source here, and it is **experimental**: the view and the walking work, the animation and cursor polish do not yet, and nothing beyond moving around is done. It is parked at the state described below; the log it writes is detailed enough to pick the work up again.

> Unofficial fan project, not affiliated with the developer. No game files are included. **Single-player only**, see [Safety](#safety).

## What works, what doesn't

Tested in the game: Steam version on Windows, game build on Unity 6000.3.15, October 2026.

- **First-person view** from the selected crewmate, with mouse look, looking straight out of the face.
- **Walking with WASD**, relative to where you look, along the deck. You stop at the deck edge instead of walking into the sea.
- **The crewmate's own AI is held off** while you have them. The other crew keep working, and the AI gets the crewmate back when you leave.
- **The boat ignores WASD** while you are in first person, and steers again when you leave.
- **Esc** opens the game menu with a usable mouse, and you are still in the crewmate afterwards. **Space** (the game's targeting mode) works in and out of first person. Clicking another crewmate switches the view to them.

Known problems:

- **Walking in place.** The walk animation keeps playing while you stand still. The game's AI "pawn" for the crewmate is most likely still trying to go somewhere; the current source blocks that pawn too, but **this build has not been tested**.
- **The mouse cursor flickers** at the centre of the screen while looking around. The current source adds Unity's own cursor lock and a Windows-level hide; also **untested**.
- The game turns the crewmate's body back on about one frame in five; the mod re-turns it every frame, which may show as a small jitter.
- The crewmate's **head** is still aimed by the game's own look-at, so seen from outside the model may look elsewhere than you do.
- **Walls** do not stop you where the deck continues behind them.
- **Not implemented:** manning a gun, loading, repairing or anything else by hand; multiplayer (see Safety).

## Build and install

No binaries are distributed; you build the DLL yourself. It needs Windows, Boat Crew on Steam, and the Visual Studio C++ build tools ("Build Tools for Visual Studio", 2019 or 2022, with the "Desktop development with C++" workload). `build.bat` finds a standard install; if yours is elsewhere, edit the paths at the top of it.

```
.\build.bat        builds out\version.dll (about 190 KB; it depends only on kernel32 and user32)
.\install.ps1      copies it next to Boat Crew.exe, finding the game through Steam (or pass -GamePath "...")
```

Run `install.ps1` from PowerShell with the game closed; it confirms the copy matches the build. Then start the game, load a boat, click a crewmate and press **Insert**.

`.\uninstall.ps1` removes it. Steam's "verify integrity of game files" does **not** remove `version.dll`, because it is not one of the game's files, so use the script or delete the file by hand. The mod writes only `BoatCrewMod\config.ini` and `BoatCrewMod\mod.log` inside the game folder.

## Controls and settings

| Key | Does |
| --- | --- |
| Insert | enter or leave first person for the selected crewmate |
| mouse | look; pitch is limited to 85° up and down |
| W A S D | walk; hold Ctrl to run |
| Esc | frees the mouse for the game menu; Esc again, or closing the menu, captures it again |
| Space | the game's own targeting mode, as usual |

`BoatCrewMod\config.ini` is created on first run. [config.example.ini](config.example.ini) documents the keys:

| Key | Default | Meaning |
| --- | --- | --- |
| `hotkey` | `Insert` | Insert, F9, Home, End, PageUp, PageDown, Pause, ScrollLock, NumpadMultiply, or a hex virtual-key code such as `0x75`. Avoid F8: it opens the game's debug console. |
| `sensitivity` | `0.12` | degrees of turn per mouse count |
| `runkey` | `Ctrl` | Shift is the game's magnify key, so running is Ctrl |
| `walkspeed`, `runspeed` | `1.5`, `3.0` | metres per second |
| `eyeforward` | `0.18` | metres the camera sits ahead of the head bone, so the face is not in view |
| `camera` | `head` | `head` (the head bone) or `bounds` (the top of the visible body mesh) |

## The log

`BoatCrewMod\mod.log` is appended on every run and is the main debugging tool. On start it lists what it found: the classes and methods it looked up, the console's toggle key, the game's character constants. On taking control it logs the deck layer mask, the crewmate's attributes, which transform carries the facing, and then the first few walk steps ("moved" or "refused", with the deck height), whether anything else moved the camera or turned the body, and every two seconds what the animation sees. If something behaves oddly, that file says which part did what.

## How it works

### Getting into the game

Boat Crew is a Unity game built with IL2CPP, so its C# is compiled to native code inside `GameAssembly.dll`. The mod is built as `version.dll`, a stand-in for the Windows `version.dll` that `UnityPlayer.dll` imports. Windows looks for that DLL next to the game's executable first, so the game loads the mod at start. The mod forwards all sixteen exports of the real `version.dll` to the copy in System32 (the game uses six of them) and otherwise does nothing in `DllMain` except start one thread. The crash handler in the game folder loads `version.dll` too, so the thread only runs when the process is `Boat Crew.exe`.

### Talking to the game

`GameAssembly.dll` exports the IL2CPP C API: `il2cpp_class_from_name`, `il2cpp_class_get_method_from_name`, `il2cpp_field_get_offset`, `il2cpp_runtime_invoke` and 240 others. The thread waits for the runtime to be ready, attaches to it, and looks everything up by name: classes such as `Core.CrewMember` and `Core.Player.PlayerCameraControlManager`, their fields (`headEnd`, `blockReasons`, `visual`), and Unity's own `Transform`, `Camera`, `Cursor`, `Physics` and the input system's `InputAction`. No address is hard-coded, so a game update only breaks the mod if the code itself is renamed. A dump of the game's class layout (made with [Il2CppInspectorRedux](https://github.com/LukeFZ/Il2CppInspectorRedux), whose Legacy CLI reads this game's IL2CPP metadata version 39) was the map for writing this; the first build checked it against the live game and every one of 318 field offsets and 288 method addresses matched.

### Running every frame

Unity calls a component's `LateUpdate` through the method's `MethodInfo`, whose first field is the native function pointer, read at call time. The mod overwrites that one pointer for `PlayerCameraControlManager.LateUpdate` with its own function, which calls the original and then the mod's `OnFrame`. That is the whole hook: no detour library, no code patching, and it runs on the game's main thread after the game's own camera update. Everything inside is wrapped in a structured exception handler; a fault turns first person off and is logged rather than crashing the game.

### The camera

Each frame in first person the mod reads the position of the crewmate's `headEnd` transform, moves it `eyeforward` metres along the horizontal look direction so the face mesh is behind the camera, and writes position and rotation to the main camera with `Transform.SetPositionAndRotation`. The rotation is built from the mod's own yaw and pitch. Before each game camera update it also checks whether the camera still has the rotation it wrote last frame; in testing nothing else ever moved it.

### The mouse

Mouse movement comes from Windows raw input (`WM_INPUT`), read by subclassing the game window and passing every message on. Two things were learned the hard way: measuring the cursor's distance from the screen centre breaks when the game locks the cursor itself (its targeting mode does, and the view drifted); and calling `RegisterRawInputDevices` is wrong, because Windows allows one registration per process and it replaced Unity's, which left the game's targeting mode unable to turn. The cursor is hidden by answering `WM_SETCURSOR` with no cursor, plus (in the current untested build) Unity's `Cursor.lockState` and `ShowCursor` on the game's thread. Esc latches the mouse free until Esc again or until the game un-pauses.

### Taking a crewmate from the AI

Crew in Boat Crew do not walk freely: an AI task system moves them along a deck navigation graph. Taking one over means:

- adding a block reason to `CrewMember.blockReasons`, the game's own switch for "do not move this crewmate", and (current build) to the AI pawn's `moveBlockReasons`;
- cancelling the current path with `CrewMember.pathCancel`;
- setting `PlayerCrewPather.block` so the game's click-to-move does not fire or draw its green path preview;
- disabling the two steering `InputAction`s on `PlayerShipController` so WASD stops driving the boat, and re-enabling them on leaving;
- turning the crewmate's **root** transform to the look direction each frame. The model's `visual` child carries a fixed 270° rotation that lines the imported model up with its facing; writing that child's rotation, as an earlier build did, turned the model sideways and put the camera over a shoulder.

Leaving first person undoes all of it, and the AI carries on.

### Walking

There is no nav mesh involved. Each frame while a key is held, the mod computes a step along the look direction at walk or run speed, casts a ray straight down from one metre above the target using the game's own deck layer mask (read from `PlayerCrewPather.deck` at the moment of taking control, because that object does not exist on the main menu), and only takes the step if deck is found within 0.6 m of the current height. The step is applied with the game's `CrewMember.setPositionSelfAndPawn`, which moves both the visible crewmate and its AI pawn. That is why you stop at the deck edge and why stairs work up to a point.

### Why not BepInEx or MelonLoader

Both rely on Il2CppInterop, which as of October 2026 fails on games built with Unity 6000.3 (IL2CPP metadata v39): the process dies shortly after start ([Il2CppInterop issue 283](https://github.com/BepInEx/Il2CppInterop/issues/283), [MelonLoader issue 1159](https://github.com/LavaGang/MelonLoader/issues/1159)). The IL2CPP C API is all this mod needs, so it does without them. If they catch up, a C# plugin would be a much more comfortable place for the next steps.

## Safety

The mod runs inside the game process and changes what it does. Use it in single-player only. Boat Crew's multiplayer is built on FishNet with each player on their own boat; nothing here is networked, a possessed crewmate would not be synchronised, and running a modified client in other people's lobbies is not fair to them. The game has no anti-cheat and the mod touches nothing outside the game folder, but it is still an injected DLL built from source you should read before using. Remove it with `uninstall.ps1` or by deleting `version.dll`.

## Research: what the crew stats do

Along the way the game's crew attributes were traced through its code: what hand-eye coordination, grit, adaptability and the rest change, the two different things "gunnery" means, and the game's real per-level numbers. See [research/crew-stats.md](research/crew-stats.md), the one-picture version [research/crew-stats-poster.png](research/crew-stats-poster.png), and [research/callers.py](research/callers.py), the script that finds which methods call a given function in `GameAssembly.dll`.

## Related

[boat-crew-map-tools](https://github.com/davidnendza/boat-crew-map-tools): a random level generator and a map viewer for the same game, including notes on its custom level format.

## Credits and licence

MIT, see [LICENSE](LICENSE). Boat Crew is by Tabbing Tabby. Class names, field offsets and the game's constants were read from the game with Il2CppInspectorRedux and the mod's own log; no game code or assets are reproduced here.
