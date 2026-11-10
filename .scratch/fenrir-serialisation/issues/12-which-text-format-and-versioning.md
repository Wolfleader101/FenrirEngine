# Which text format do files use, and how are they versioned and migrated?

Type: grilling
Status: open
Blocked by: 02, 03, 10

## Question

Which one text format do scene files, data assets and dev-time data share, and how does a file written for an older type still read? **ADR.**

- **Format:** JSON (plain or with comments), TOML, YAML, a Godot-like INI, KDL, or a small custom grammar; chosen for hand editing, review diffs, three-way merges, float round-tripping, stable ordering (the same data writes the same bytes), and how it writes a typed reference, a polymorphic value and an embedded sub-object as [What is a data asset?](10-what-a-data-asset-is.md) decided them.
- **Library:** which C++ library reads and writes it, pinned as a `PRIVATE` submodule (ADR-0010's pattern), and whether C# ever reads the files itself or only sees loaded data.
- **File conventions:** extensions, a header naming the format version and the type, one object per file or several, and whether binary blobs sit inline or in sidecars.
- **Versioning:** a per-file format version and a per-type version or none; renamed fields (a `formerly` list), removed fields (kept as unknown data, dropped with a warning), changed kinds, and custom upgrade hooks in C++ and C#; what happens when a file names a type the build does not know (kept opaque for round-tripping, or an error).
- **Diagnostics:** errors with file, line and column; whether a partly broken file loads partly.

Zoom into [Which text formats suit scene files and data assets?](03-text-formats-for-game-data.md) and the merge-friendliness findings of [How do engines serialise scenes and prefabs?](02-how-engines-serialise-scenes-and-prefabs.md).
