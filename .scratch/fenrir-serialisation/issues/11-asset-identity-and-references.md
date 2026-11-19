# How are assets identified, and how do files reference them?

Type: grilling
Status: resolved
Blocked by: 01, 02, 10

## Question

How does a file name an asset or data asset so the reference survives editing, moving files and cooking? **ADR.**

- **Identity:** the path relative to the asset root (as `Assets` loads today, with `#label` for sub-assets), a stable id assigned at import (Unity's GUID, Godot's UID), or both (a path for people, an id for the engine). Where an id would live (a sidecar, the file's own header, an index), who assigns it, and what happens to an id on copy, rename or move.
- **Reference kinds:** a component's `AssetHandle<Mesh>` written to a scene file; a data asset's field naming another data asset; a scene naming a prefab source; a reference into a sub-asset. Whether references are typed in the file, and how a reference to a missing or wrong-typed target is reported and shown.
- **Resolution:** when a referenced asset is loaded (on scene load, lazily on first use, by the game), with which owner (ADR-0021), and how a stale handle is told apart from an unloaded one.
- **Cooking:** what the identity must guarantee so a cooked form can replace paths with offsets without touching game code.

Zoom into [How do engines type, inherit and reference data assets?](01-how-engines-type-data-assets.md) and [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md); the data asset's shape is [What is a data asset?](10-what-a-data-asset-is.md)'s.

## Answer

ADR: `docs/adr/0062-an-assets-identity-in-files-is-its-root-relative-path-and-a-tool-rewrites-references-when-a-file-moves.md`. Glossary: Asset path. One grilling round, every recommendation accepted; the hybrid (ids in Fenrir-authored files' headers, paths for imported files) was weighed and declined.

- **Identity:** the asset path, as the store already keys it: relative to the asset root, forward slashes, extension included, `#label` for a sub-asset, `builtin:` for embedded assets; `..` and absolute paths refused; case-sensitive comparison with a development warning on a case-insensitive match. No GUIDs, no sidecars, no id index.
- **Reference kinds:** one spelling for all, the path string; the expected kind comes from the field's declared type, and a data asset target's type must be assignable to the field's base; a missing or wrong-typed target leaves the handle empty with one diagnostic naming file, field, path, expected and found. A prefab source is named by the same path.
- **Hard only:** an `AssetHandle<T>` in a file loads with the referencing file, synchronously, with the referencing load's owner, before the field is filled; a reference to a file already loading gets its handle (cycles resolve). Soft references go to the fog until streaming asks.
- **Writing:** a public path-from-handle query; `Add` with a name makes a code-made asset writable, unnamed ones write empty with a diagnostic; model sub-assets keep numbered labels, embedded data asset sub-assets are author-named and unique within the file.
- **Moves:** a command-line move tool (and a function a future editor calls) renames or moves a file under the root and rewrites every reference across the root's text files; it is part of the spec, not fog. The engine's run-time part is the diagnostic.
- **Left to other tickets:** how the path string is spelled inside a file (12), what a cooker does with it (20), how a prefab instance names its source (14).
