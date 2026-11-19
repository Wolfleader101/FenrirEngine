---
status: accepted
---

# An asset's identity in files is its root-relative path, and a tool rewrites references when a file moves

Scene files and data assets must name the assets they use so the reference survives editing, moving files and cooking. The store already keys every asset by its path relative to the asset root plus an optional `#label` (ADR-0021). The engines that add a stable id (Unity, Godot, Flax, Stride, O3DE) need a sidecar for files that cannot carry one, committed and moved with the file, minting a fresh id when lost; Godot's live pains are id churn and duplicate ids from copied files, and its editor still rewrites dependents' paths on every move.

We decided that **an asset's identity in files is its path, with no ids**, and that a move is a tool's job:

1. **The path is the identity.** Relative to the asset root, forward slashes, extension included, `#label` for a sub-asset, `builtin:` for the engine's embedded assets. `..` and absolute paths are refused with a diagnostic. Paths compare case-sensitively; a development build warns when a file is found only by a case-insensitive match, so a project authored on Windows loads on Linux. No GUIDs, no sidecars, no id index to scan or ship.
2. **References are typed by the field, checked at load.** A reference in a file is the path string alone; the expected kind comes from the field's declared type (`AssetHandle<Mesh>`, `AssetHandle<WeaponData>`). On load the target's kind, and for a data asset its type's assignability to the field's base (ADR-0075), is checked; a missing file or a wrong type leaves the handle empty and reports one diagnostic naming the referencing file, field, path, expected and found.
3. **Handles are hard references.** An `AssetHandle<T>` in a file loads with the referencing file, synchronously on the main thread, with the same owner as the referencing load, before the field that names it is filled. A reference to a file already loading gets that file's handle at once, so cycles between data assets resolve. Soft references (a path loaded later) are not offered.
4. **Writing needs a name.** The store gains a public path-from-handle query. An asset added from code is writable only if `Add` gave it a name, keyed like a loaded one; an unnamed code-made asset writes as an empty handle with one diagnostic. Model sub-assets keep their numbered labels; a data asset's embedded sub-assets are named by the author, unique within the file.
5. **A move tool is part of the feature.** A command-line tool (and a function a future editor calls) moves or renames a file under the asset root and rewrites every reference to it across the root's text files, reporting what it changed. The engine's own part at run time is the diagnostic for a path that no longer resolves.

## Considered options

- **A stable id beside the path for Fenrir-authored files only** (data assets and scenes carry a random id in their header; imported files stay path-only). Moves of those files would need no rewrite, at the cost of an id index built by scanning the asset root in development and baked when cooking, duplicate ids on copied files, and two identities where imported assets have one. The move tool covers the same workflow with no second identity.
- **Ids for every asset through sidecars** (Unity's `.meta`, Godot's `.import`). Robust to moves made outside any tool, but a sidecar per PNG and glTF, committed and moved by hand, and references no human can read. The research found ids were retrofitted where formats cannot carry them and kept the path as a fallback; Fenrir starts from the path.
- **Soft references now.** Every engine distinguishes hard from soft, but nothing in the engine streams yet; a string field and a load call cover the rare case until streaming asks for more.

## Consequences

- A text reference is readable and greppable, and a file's dependencies are its path strings; cooking maps those strings to pack entries without touching game code (the cooked-form ticket of the serialisation map).
- Renaming or moving a file by hand, outside the tool, breaks references until they are rewritten; the diagnostic names each one.
- `FenrirAssets` gains the path-from-handle query, named `Add`, author-named sub-asset labels inside data asset files, and the case-match warning.
- A prefab instance names its source scene file by the same path; the format ticket decides how the string is spelled inside a file.
