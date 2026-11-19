# Which text format do files use, and how are they versioned and migrated?

Type: grilling
Status: resolved
Blocked by: 02, 03, 10

## Question

Which one text format do scene files, data assets and dev-time data share, and how does a file written for an older type still read? **ADR.**

- **Format:** JSON (plain or with comments), TOML, YAML, a Godot-like INI, KDL, or a small custom grammar; chosen for hand editing, review diffs, three-way merges, float round-tripping, stable ordering (the same data writes the same bytes), and how it writes a typed reference, a polymorphic value and an embedded sub-object as [What is a data asset?](10-what-a-data-asset-is.md) decided them.
- **Library:** which C++ library reads and writes it, pinned as a `PRIVATE` submodule (ADR-0010's pattern), and whether C# ever reads the files itself or only sees loaded data.
- **File conventions:** extensions, a header naming the format version and the type, one object per file or several, and whether binary blobs sit inline or in sidecars.
- **Versioning:** a per-file format version and a per-type version or none; renamed fields (a `formerly` list), removed fields (kept as unknown data, dropped with a warning), changed kinds, and custom upgrade hooks in C++ and C#; what happens when a file names a type the build does not know (kept opaque for round-tripping, or an error).
- **Diagnostics:** errors with file, line and column; whether a partly broken file loads partly.

Zoom into [Which text formats suit scene files and data assets?](03-text-formats-for-game-data.md) and the merge-friendliness findings of [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md).

## Answer

ADR: `docs/adr/0077-files-are-json5-in-one-envelope-read-and-written-by-an-engine-owned-parser-and-migrated-by-per-type-upgraders.md`. Glossary: Envelope, Format version, Type version, Upgrader. Two grilling rounds; the format question was answered by a targeted investigation, `.scratch/fenrir-serialisation/research/format-fit.md` on branch `research/format-fit` (a78ddc75): the same sword, scene and settings files in TOML, JSON, JSON5 and YAML, measured for size, diffs, three-way merges, parser cost and hazards, and scored against fifteen settled needs (JSON5 13 fits, 0 fails; YAML 10, 1; JSON 9, 3; TOML 5, 1). Every recommendation accepted.

- **Format:** JSON5, read and written by an engine-owned reader and writer with no submodule. The reader keeps comments as trivia so rewrites keep them; the writer emits a fixed subset (bare identifier keys, double-quoted strings, two-space indent, trailing commas, flow style only for scalar arrays and single-field objects, shortest floats, `Infinity` and `NaN` literals) in declaration order with defaults omitted, so the same data writes the same bytes. Merges were found to depend on layout and stable ids, not syntax; YAML via rapidyaml was the runner-up and TOML and strict JSON were ruled out.
- **Envelope:** one object per file with `format`, `type`, `version`, `data` and `sub`; a scene is a `Fenrir::Scene` data asset whose `data` holds `resources` and `entities`; settings files use the same envelope.
- **Layout rule:** one entity per block, id first, children directly after their parent depth-first, never reordered or renumbered, ids readable random strings minted once; sub-assets by name. A semantic merge driver goes to the fog.
- **Extensions:** `.asset.json5`, `.scene.json5`, `.settings.json5`: role then format.
- **Reader of record:** C++; C# never parses files.
- **Versioning:** `format` for the envelope, `@version` per type, `@formerly` for renames; unknown fields warn and drop, unknown types are kept opaque and written back; upgraders `(fromVersion, tree) -> tree` registered with the type registry (C++ beside the type, C# as an attribute on a static method), no downgrade.
- **Diagnostics:** file, line and column through the logger; a partly broken file loads what reads, one diagnostic per failure, and the load result counts the skips.
- **Binary:** never inline; bulk data is its own file named by asset path.
- **Left to other tickets:** which package holds the reader and writer (15); what an entity id is and where it lives (13); what the cooker does with the tree (20).
