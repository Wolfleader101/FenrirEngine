# Where do the type descriptions that read and write files come from?

Type: grilling
Status: resolved
Blocked by: 04, 08

## Question

What describes a type's fields when a scene file or data asset is read or written, and when an editor later shows it? **ADR.**

- Is it the build-time API description (ADR-0042) widened with the kinds data assets need (strings, variable-length lists, maps, nested lists of structs, optional values, typed references to assets and data assets, polymorphic values), a run-time type registry the engine fills from those descriptions (and from the C# script report for script types), both, or a new mechanism (macros, intrusive templates)? What does a game developer write per type and per field in C++, and in C#?
- Does a game project run `describe-api` on its own headers? If so, how it joins the game's build and where its output goes; if not, how C++ game types are described.
- Which types are describable: do plain-data components keep their fixed-layout rules, and data assets get richer rules, or does one rule cover both? What is refused with a diagnostic.
- How is a type named in a file so that C++ and C# types, and a future binding's, share one namespace, and how a renamed type is still read.
- Editor metadata on fields (ranges, display names, categories, read-only), from doc comments and from attributes, in both languages.
- Where the registry lives (FenrirCore, FenrirECS, a new package) and what depends on it.

Consult `codebase-design` for where the registry sits. Zoom into [How do C++ engines describe their types for serialisation and editors?](04-how-engines-reflect-types-for-files.md) and [What can the engine describe and name today?](08-what-the-engine-can-describe-today.md) for the facts.

## Answer

ADR: `docs/adr/0060-types-read-from-and-written-to-files-are-described-by-the-api-description-and-registered-with-fenrirtypes-from-generated-code.md`. Glossary: FenrirTypes, Type registry, Type registration, Canonical type name, Open slot. Two grilling rounds, every recommendation accepted.

- **Source:** the API description (ADR-0042) stays the one description of a C++ type, widened; no macros, templates or hand-filled registry. A game runs `describe-api` on its own headers through the same CMake function, committing its own description and registration; the .NET SDK is a development-time need, as for C# scripting, with prebuilt tool binaries a later convenience.
- **Run time:** a second generator, `tools/generate-type-registry`, reads the description and writes a committed C++ **type registration** per package, compiled in and registering each type with the **type registry** at `Install`; the engine parses no JSON and ships no description. It is configured by a `[registration]` table in the package's `.api.toml`, joins one `fenrir-generate-type-registry` target the ordinary build never runs, and CI's dirty-tree check gains it and runs with the renderer on.
- **Two rules, one description:** components, events and resources stay trivially copyable with fixed layout, read and written by offset (so C# script components stored as bytes work the same way); the new `data-asset` role may hold `std::string`, `std::vector<T>`, `std::optional<T>`, nested structs and typed references, recorded as kinds `string`, `list`, `optional` and `reference` with offsets unused, and read through a generated per-type visitor. A component holding a string is refused.
- **One visitor interface:** the registry's generic access is a push-model visit over an instance's fields (name, kind, metadata, value reference) used by the file writer, reader, a future inspector and C# bridging; fixed-layout types get a table-driven visitor from their offsets, owning types a generated one; nothing else reads offsets.
- **What the description can say:** an **open slot** (a value or reference of any registered type assignable to a named base, written as its type name beside the value; what "assignable" means is ticket 10's), a per-type `@version` (default 1, written beside the type name; what a reader does with an older one is ticket 12's), `@formerly` per type and field, and a fixed metadata vocabulary (`@range`, `@step`, `@display`, `@group`, `@readonly`, `@hidden`, `@multiline`, `@units`) as doc commands in C++, as the natives' commands already are, and as attributes in C#; C++26 annotations replace the commands later with the description unchanged. Doc text is compiled into the registration; stripping it is the cooked-form ticket's.
- **Naming:** files name types by the description's canonical name (`Fenrir::Transform`, `MyGame.WeaponData`), one namespace across languages, no GUIDs; renames read through the `formerly` lists and are rewritten on save.
- **C#:** script components are registered at admission with fields, kinds and offsets from Iris and a `new T()` default, so a scene file writes and reads them without C++ seeing the definition; C# data asset types arrive the same way through a description generated on the C# side (ticket 16 decides how their strings and lists cross).
- **Roles:** component (name and `ComponentType`, native or dynamic), scene-resource and app-resource (type-erased add, get, remove), data-asset (construct, destroy, fields, visitor), shared-struct and enum (nested values). Behaviour-carrying resources (`PhysicsWorld`, `SkeletonPoses`, `DebugDraw`) are not described; how a scene file carries them is ticket 13's.
- **Package:** a new **FenrirTypes** below FenrirECS (FenrirCore is an empty header), holding the kinds and the registry and nothing else; FenrirECS depends on it for names, FenrirScene for resources, FenrirAssets for data assets, FenrirScripting fills it at admission. File reading and scene loading are placed by ticket 15.
