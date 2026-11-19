---
status: accepted
---

# Files are JSON5 in one envelope, read and written by an engine-owned parser, and migrated by per-type upgraders

Scene files, data assets and dev-time data share one text format that people hand-edit, review in diffs and merge three ways, while the engine rewrites it byte-for-byte stably and reads files written for older types. The user named TOML, JSON, JSON5 and YAML as acceptable and asked for an investigation. Measured against fifteen settled needs with the same sword, scene and settings files in each format (the `research/format-fit` branch), merges turned out to be decided by layout and stable ids, not syntax; no C++ library keeps comments across a rewrite, and every library path still needs an engine-owned writer for declaration order, float text and infinities; a JSON5 reader and writer is 1,250 to 2,000 lines against submodules of 16k to 40k; YAML's ` #` ending an unquoted scalar and a dedent silently moving a component to the entity, and TOML's tables re-parenting when a block moves, are hazards a tolerant loader cannot catch. JSON5 fit thirteen needs and failed none; YAML ten with one fail, JSON nine with three, TOML five with one.

We decided that **files are JSON5 in one envelope, read and written by code the engine owns, and that a type's version and its upgraders travel with the type**:

1. **JSON5, engine-owned.** One reader and one writer in the engine, no submodule. The reader accepts the JSON5 grammar and keeps comments as trivia on the tree, so a tool rewrite re-emits them. The writer emits a fixed subset: bare identifier keys, double-quoted strings, two-space indent, trailing commas, flow style only for arrays of scalars and single-field objects, shortest round-trip floats (`std::to_chars`), and `Infinity` and `NaN` as literals. Fields are written in the type's declaration order, defaults omitted (ADR-0061), so the same data writes the same bytes.
2. **One envelope.** A file is one object: `format` (the file format's version, 1), `type` (the canonical type name, ADR-0060), `version` (the type's version), `data` (the asset's fields) and `sub` (embedded sub-assets by name, each again `type`, `version` and `data`). A scene file is a data asset of type `Fenrir::Scene` whose `data` holds `resources` and `entities`; dev-time settings files use the same envelope. One top-level asset per file.
3. **A layout rule for merges.** The writer emits one entity per block with its id first, children directly after their parent depth-first, never reorders or renumbers, and mints an entity's readable random id once, keeping it for the entity's life; sub-assets likewise by name. Two people adding under different parents merge cleanly; two appending at the end conflict, in this format as in any.
4. **Extensions name the role and the format:** `sword.asset.json5`, `level1.scene.json5`, `project.settings.json5`. Editors key on the format, people and the engine on the role.
5. **C++ is the reader of record.** C# never parses a file; it sees loaded data through the asset store and the type registry.
6. **Versioning travels with the type.** `format` is bumped only for the envelope itself. A type's `version` comes from its description's `@version`; `@formerly` reads renamed fields; an unknown field warns and is dropped; a value of an unknown type is kept opaque and written back unchanged. A type registers upgraders, `(fromVersion, tree) -> tree`, with the type registry (C++ beside the type, C# as an attribute on a static method), applied in order before the tree is read into the struct. There is no downgrade.
7. **Tolerant loads with located diagnostics.** A syntax or type error is reported through the logger with file, line and column; a partly broken file loads the parts that read, each failure one diagnostic, and the load result counts what was skipped.
8. **No binary in text files.** Bulk data (images, meshes, baked tables) lives in its own file and is referenced by asset path (ADR-0062); a text file never carries an encoded blob.

## Considered options

- **YAML 1.2 through rapidyaml.** Smallest files (23 and 46 lines against 30 and 65) and the runner-up by score, but comments are lost on rewrite, the writer is engine-owned anyway, and a stray ` #`, an unquoted `#sparks` or a dedented line change meaning silently, which a tolerant loader then accepts.
- **Strict JSON through nlohmann or RapidJSON.** Readers everywhere, but no comments at all, `null` for infinity, and offsets rather than line and column; the writer is still ours.
- **TOML through toml++.** Nests poorly, sorts keys on write, a moved block re-parents into another table, and readers split across 1.0 and 1.1.
- **A library parser plus an engine-owned writer.** Saves the smaller half of the work, keeps a submodule with its own history, and cannot carry comments from parse to write.
- **A Godot-like INI or a custom grammar.** No highlighting or tooling anywhere, for no fit JSON5 lacks.

## Consequences

- The engine owns about 1,250 to 2,000 lines of parser and writer whose correctness rests on its own tests, including round-trip and byte-identity tests over the emit subset.
- Which package holds the reader and writer, and how loading and saving are exposed, is the load/save ticket's; ADR-0010 still applies to any library that package uses privately.
- A semantic merge driver keyed by entity id, component type and field is not built; the layout rule is what makes plain line merges work, and the driver waits in the serialisation map's fog until a team meets the end-of-file conflict often.
- What an entity's id is, where it lives and how components name entities is the scene-file ticket's; this ADR fixes only that the id exists, is minted once and is written first.
- The cooked form reads the same tree, so it can map paths to pack entries and skip the text (the cooked-form ticket).
