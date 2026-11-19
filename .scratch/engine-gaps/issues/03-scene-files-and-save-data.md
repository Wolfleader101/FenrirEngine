# No scene or prefab files, and no way to save data

Status: needs-triage
Blocked by: none

## What is missing

The engine has no scene, level or prefab file format, no entity or component serialisation, and no API to write and read a game's own data (settings, records). A level is code today. The nearest authored path is a glTF spawned one entity per node by `SpawnModel`, and whether a node's `extras` survive import is unverified.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): its one level must be data, so that a second level is new data and not new code, and it places cages, coins, power-ups, enemies and an exit in it. It may also keep best times and options between runs ("What is the game loop?" decides).

## Does anything plan it?

Yes, on an unmerged planning branch: the `fenrir-serialisation` map (`origin/claude/wayfinder-serializable-resources-246b15`, last commit 2026-11-10), with 20 tickets covering data assets, scene files, prefabs, the load and save API, C# data and hot reload. Nothing is specified or built. The inventory missed it because it is not on `main`.

## Does it block the game?

No. A format that describes this game's own things is the game's to define, and it can read a small level file or markers in a model until the engine's scene files exist. "How are levels authored and loaded?" decides the stopgap. This game is a first real client for that map's decisions.

## Comments
