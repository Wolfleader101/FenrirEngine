# No scene or prefab files, and no way to save data

Status: ready-for-agent
Blocked by: none

## What is missing

The engine has no scene, level or prefab file format, no entity or component serialisation, and no API to write and read a game's own data (settings, records). A level is code today. The nearest authored path is a glTF spawned one entity per node by `SpawnModel`, and whether a node's `extras` survive import is unverified.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): its one level must be data, so that a second level is new data and not new code, and it places cages, coins, power-ups, enemies and an exit in it. It keeps its best score and best time between runs, but writes that file itself with .NET ([What is the game loop?](../../flightless-frenzy/issues/06-the-game-loop.md)), so it does not wait on an engine save API.

## Does anything plan it?

Yes, on an unmerged planning branch: the `fenrir-serialisation` map (`origin/claude/wayfinder-serializable-resources-246b15`, last commit 2026-11-10), with 20 tickets covering data assets, scene files, prefabs, the load and save API, C# data and hot reload. Nothing is specified or built. The inventory missed it because it is not on `main`.

## Does it block the game?

No. A format that describes this game's own things is the game's to define, and it can read a small level file or markers in a model until the engine's scene files exist. "How are levels authored and loaded?" decides the stopgap. This game is a first real client for that map's decisions.

## Comments

- 2026-11-19, charting [Engine gaps for Flightless Frenzy](../map.md): a pointer to the `fenrir-serialisation` map (`claude/wayfinder-serializable-resources-246b15`); no decision ticket here.
- 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md): **a pointer** to the `fenrir-serialisation` map, with nothing added there. The one open fact, that the glTF importer drops node `extras`, went to the levels and scenes design through the sessions' shared notes.
- 2026-11-20: the `fenrir-serialisation` map is now on `main` (#316, `.scratch/fenrir-serialisation/map.md`), with ADR-0074 to ADR-0085 and its specs under `.scratch/fenrir-serialisation-*/`; the scene file is ADR-0078.
