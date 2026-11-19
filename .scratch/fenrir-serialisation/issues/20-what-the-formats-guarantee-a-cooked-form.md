# What must the development formats guarantee so a cooked shipping form can follow?

Type: grilling
Status: resolved
Blocked by: 11, 12, 13

## Question

A shipped game needs a binary or cooked form of its scenes and data assets, which this map does not specify. What must the development formats and the engine API promise now so that form can be added later without changing game code or the files?

- **Identity:** references that a cooker can replace with offsets or indices (as [How are assets identified?](11-asset-identity-and-references.md) decided), no absolute paths, and nothing resolved by scanning the asset root at runtime.
- **Descriptions without headers:** the type descriptions a cooked reader needs available without the C++ headers or the C# assemblies, or a cooked form laid out so none are needed.
- **One door:** whether game code loads through one API that a cooked build serves from a pack and a development build from text files, and whether the asset root's layout is part of the contract.
- **Embedded data:** which engine defaults are already embedded (built-in shaders) and whether the pattern extends to a game's cooked pack in the executable.
- **What is deferred:** the cooked format, the cooker, compression, platform packaging, and whether they are a later spec on this map or a new effort; this ticket's resolution updates the map's fog accordingly.

Zoom into [Which text format do files use?](12-which-text-format-and-versioning.md) and [What does a scene file hold?](13-what-a-scene-file-holds.md).

## Answer

ADR: `docs/adr/0085-the-development-formats-promise-a-cooked-form-path-identity-compiled-in-descriptions-the-tree-encoding-and-one-asset-source-seam.md`. Glossary: Asset source, Cooked form. One grilling round, every recommendation accepted; most promises had already fallen out of earlier tickets and are here named as a contract.

- **Identity:** every reference is a root-relative path string typed by its field (ADR-0076), no absolute paths, ids, sidecar identities or run-time scan of the asset root; a cooker maps paths to pack entries and game code holding handles changes nothing; the writer refuses a path that is absolute or escapes the root.
- **Descriptions without headers:** registration is compiled in and C# admits at run time (ADR-0074), so no description file ships; a cooked data asset, scene or project file is its value tree in the binary tree encoding (ADR-0083), read through the same bridge, `ReadTree` sniffing text or binary from a byte span; upgraders run on cooked trees; a fixed-layout byte-image fast path is deferred.
- **One door:** game code loads only through the store and facade doors; beneath them a private `AssetSource` seam ("bytes at this path", "does it exist") with a directory implementation now and a pack source later whose index maps paths to entries, imported assets pre-baked so importers need not ship; the root's layout is no contract beyond relative paths; the engine never writes under the root at run time.
- **Embedded data:** `builtin:` stays the engine's namespace; a pack source reads from a byte span, so a game's pack is a file beside the executable or bytes embedded in it through the same code.
- **Deferred:** the pack layout and index, compression, chunking, encryption, per-platform shader compilation, baked imports, stripping `@transient` and `@editor-only`, pre-expanding or keeping instances, the cooked project twin (platform folded in, local never cooked) and the pipeline: one new effort, "Fenrir cooking", with its own map when a game first needs a shipped build or first-run import costs bite; the map's fog entry is rewritten to list the promises as its starting contract.
