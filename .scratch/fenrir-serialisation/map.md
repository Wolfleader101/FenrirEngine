# Fenrir serialisation: data assets, scene files and dev-time data

Label: wayfinder:map

## Destination

A set of specs, one feature directory each (`.scratch/fenrir-serialisation-<name>/spec.md`), each ready for `/to-tickets`, plus ADRs for the hard-to-reverse calls, covering: **data assets** (game-defined typed data in files, what Godot calls a Resource, Unity a ScriptableObject and Unreal a DataAsset; defined from C++ and from C#), **scene files** (a FenrirScene's entities, components, hierarchy and scene resources, and prefabs), the **one text format** and how files are versioned and migrated, **where type descriptions come from** for reading and writing files, **asset identity and references**, **load, save and hot reload**, **dev-time data** (project settings, import sidecars, per-developer overrides), and **what the data layer must offer a level editor**, informed by research into what makes a good one. The development formats are designed so a cooked or binary shipping form can follow, but that form and the pipeline that writes it are not specified here. Editor UI is out of scope.

## Notes

- **The engine's one word is *scene*** (`GLOSSARY.md`, FenrirScene). *Map* and *level* are the game's words for a scene file. The field says *level editor*, so the editor research does too.
- **"Resource" is taken.** A scene resource or app resource is one-per-type package state (`GLOSSARY.md`), and *Asset* says *avoid: resource*. The game-defined typed data in a file is provisionally a **data asset** until [What is a data asset?](issues/10-what-a-data-asset-is.md) names it; *blueprint* is Unreal's and *resource* is ours, so neither.
- **Nothing is serialised today.** Demo scenes are C++ functions; materials are edited in code; the files the engine reads are models, images, shader containers and C# assemblies.
- **Reflection exists at build time, for plain data only.** `tools/describe-api` writes each package's committed `<Package>.api.json` (ADR-0042): every shared type's fields, offsets, kinds, defaults and doc comments. Its kinds are integers, floats, bools, enums, nested structs, fixed arrays and opaque pointers: no strings (names are fixed 256-byte UTF-8 arrays, `BoneName`), no variable-length lists, no polymorphism. Data assets were agreed to need richer types than components (strings, lists, nested lists, handles to other data assets); how the engine describes them is a ticket.
- **Godot's inheritance is class inheritance**, and C++ structs with members on both base and derived are not standard-layout, so describe-api cannot describe them. The type model (inheritance, composition or another) is chosen, not copied.
- **Language-agnostic.** C++ and C# both define data asset types and component data; a file names a type neutrally, as the API description does. C# reaches the data through the existing scripting pipeline (ADR-0042, ADR-0043, ADR-0046).
- **Editor-ready, no editor.** The data layer and its API are designed so an inspector, a scene view and a level editor can sit on top; none is decided here. Prototypes may use throwaway ImGui panels to react to the data.
- **Both dev-time and baked-in data are in scope as concerns**: this map decides the development formats and what they must guarantee so a cooked form is possible later (identities that survive moving files, no absolute paths, type descriptions available without headers); the cooked form itself is fog.
- **The engine/game line holds** (ADR-0057's spirit): the engine offers building blocks (load, save, reflect, reference), not opinions about what a world is. Streaming, partitioning and brush-to-mesh baking are expected to be the game's or a tool's; brush geometry in the scene file stays open until the level editor research lands.
- **Libraries stay hidden** (ADR-0010): a text-format or reflection library is `PRIVATE` to the package that uses it and appears in no public header.
- **Conventions:** in this map *scene*, *asset*, *asset handle*, *asset owner*, *sub-asset*, *scene resource*, *app resource*, *component*, *API description*, *bound type* and *native resource* carry their meanings from `GLOSSARY.md`.
- **Skills:** grilling tickets call `grilling` and `domain-modeling`; package and seam questions also use `codebase-design`. Research tickets call `research` and put their findings in `.scratch/fenrir-serialisation/research/<name>.md` on a `research/<name>` branch, pushed, never merged.
- **Plan, don't do:** tickets produce decisions, not code (spikes are throwaway, on `prototype/…` branches).
- **Built specs are removed**, as on the physics and renderer maps: the commit that records a spec built deletes its directory.

## Decisions so far

<!-- one line per resolved ticket: [title](issues/NN-slug.md): gist -->

## Not yet specified

- **The cooked or binary shipping form and the pipeline that writes it.** A shipped game needs one (the user's words). Whether it is a later spec on this map or its own effort is decided once [What must the development formats guarantee a cooked form?](issues/20-what-the-formats-guarantee-a-cooked-form.md) closes.
- **Spec boundaries:** how many specs, which tickets each gathers (data assets and reflection; scene files and prefabs; dev-time data; the level-editor-facing additions), and their order. Settled once the grilling tickets close.
- **Which existing engine assets become files:** materials (`MaterialData`, edited in code today), instance lists, bone masks, render-target images. Likely the first engine-provided data assets; waits on the data asset model and the format.
- **Editing support in the data layer:** what a running scene or asset store offers an editor beyond load and save (change tracking, dirty state, undo-friendly edits, validation diagnostics for broken files). Waits on load/save and hot reload.
- **Streaming building blocks:** what a game needs from scene files to stream an open world (additive load and unload, scene composition, cells) given that partitioning is the game's. Waits on the scene file and prefab tickets and the level editor research.
- **Text in data assets:** localisation and string tables, if data assets hold player-facing text. Waits on the type model.

## Out of scope

- Editor UI: an inspector, a scene view, a level editor's own tools and windows, editor layouts and window state.
- The demo apps' scene lists stay code.
