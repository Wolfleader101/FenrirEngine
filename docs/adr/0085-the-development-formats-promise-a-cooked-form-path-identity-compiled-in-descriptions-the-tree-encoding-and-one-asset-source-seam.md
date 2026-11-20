---
status: accepted
---

# The development formats promise a cooked form path identity, compiled-in descriptions, the tree encoding and one asset source seam

A shipped game needs a binary or cooked form of its scenes, data assets and settings, which the serialisation map designs for but does not specify. The research found no engine ships its editor's text files as-is, but Godot, Unreal and Flax keep the shipped form a mechanical conversion of them (binary twins, packs, LZ4 chunks), with identities unchanged, embedded packs (Godot appends to the executable, Bevy includes bytes), and development-only data (sidecars' options, local overrides, caches, editor content) left out. The earlier decisions already fixed what the conversion needs: references are typed root-relative path strings (ADR-0076); the registry is filled from compiled-in registration and C# admission, so no description file exists at run time (ADR-0074); a value tree has a binary encoding in memory (ADR-0083); the watcher is off when cooked (ADR-0081); editor-only and transient types are marked (ADR-0078, ADR-0082); the engine never writes under the asset root at run time (ADR-0078).

We decided that **the development formats make four promises to a cooked form, that one private seam is built now for it, and that the cooker itself is a later effort with its own map**:

1. **Identity is the path, and nothing scans.** Every reference is a root-relative path string typed by its field; there are no absolute paths, no ids, no sidecar identities and no index built by scanning the asset root at run time. A cooker maps each distinct path to a pack entry; game code holding handles and calling `Load<T>(path)` changes nothing. The writer refuses a path that is absolute or escapes the root.
2. **Descriptions are compiled in; a cooked file is its tree encoding.** A cooked build carries no description file. A cooked data asset, scene or project file is its value tree in `FenrirSerialisation`'s binary tree encoding, read through the same bridge as text: `ReadTree` takes text or binary from a byte span, sniffing the first bytes, so loaders above it do not know which they got. Upgraders run on cooked trees too. A byte-image fast path for fixed-layout components is deferred.
3. **One door, one seam.** Game code loads only through the store and facade doors (`Load<T>`, `LoadScene`, `LoadSceneInto`, `Instantiate`, `Project::Load`). Beneath them, a private `AssetSource` seam answers "the bytes at this path" and "does this path exist", with the directory source as the only implementation now; a cooked build swaps in a pack source whose index maps paths to entries, with imported assets pre-baked as `MeshData`, `ImageData` and the rest in the tree encoding so importers need not ship. The asset root's layout is no contract beyond paths being relative to it; a read-only pack loses nothing since the engine never writes under the root at run time.
4. **Embedding goes through the same seam.** `builtin:` stays the engine's namespace for embedded shaders and defaults. A pack source reads from a byte span, so a game's pack is a file beside the executable or bytes embedded in it with no second code path.
5. **Cooking is its own effort.** The pack layout and index, compression, chunking, encryption, per-platform shader compilation, baked imports, stripping `@transient` and `@editor-only` types, pre-expanding or keeping instances, the cooked twin of the project file (platform layer folded in, local layer never cooked) and the pipeline that runs it are one effort, "Fenrir cooking", mapped when a game first needs a shipped build or first-run import costs bite. The promises above are its starting contract.

## Considered options

- **Specifying the cooked format on this map.** Nothing ships yet, no game has a shipped build, and every cooked format read was shaped by its pipeline (chunking, platform shaders, encryption); deciding the shape before the pipeline exists would decide it twice.
- **A description file shipped with the game** (parsed `api.json`). Rejected by ADR-0074; compiled-in registration leaves nothing to ship.
- **A separate cooked reader per type** (hand-written binary layouts). Fast, but a second serialiser to keep in step with the description; the tree encoding reuses the bridge and the upgraders, and a fixed-layout fast path can be added where it pays.
- **Paths replaced by ids at cook time inside game code's view.** Game code sees handles, never pack entries; the mapping lives in the pack source, so no API changes between development and shipped builds.
- **No seam until the cooker exists.** The directory source is the whole implementation today, so the seam costs an interface and a test; adding it later means touching every load path at once.

## Consequences

- `FenrirAssets` gains the private `AssetSource` seam with a directory implementation and the writer's path check; `FenrirSerialisation`'s `ReadTree` sniffs text or binary.
- A future pack source, cooker and build pipeline are a separate map; this map's fog entry on the cooked form points to it and lists the four promises.
- `@transient`, `@editor-only`, the never-cooked local project layer and the baked imports give the cooker its stripping rules without new marks.
