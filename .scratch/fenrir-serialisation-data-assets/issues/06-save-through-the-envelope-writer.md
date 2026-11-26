# 06: `Save<T>` through the envelope writer, atomic, byte-identical against the golden file, with `SaveResult`

**What to build:** a data asset writes back to its file in the engine's fixed JSON5 form, through one envelope writer. The same data always writes the same bytes, and the writer keeps the file's comments (ADR-0077, ADR-0079).

- **`Save<T>(handle) -> SaveResult`:**
  - It writes the asset and its sub-assets to the asset's own path, through `ToTree` with the store's `Resolvers` and then `WriteText`.
  - It passes the file's last-read tree as `prior` when the store still holds it, so comments survive.
  - The envelope keys come first, in order: `format`, `type`, `version` (the type's), `data`, then `sub`.
- **`Save<T>(handle, path)`:**
  - For an unnamed asset, it writes to `path` and gives the asset that path.
  - A named asset is written to `path` too, but its own path does not change.
  - On a sub-asset handle it is refused. A plain `Save` of a sub-asset handle writes its whole file.
- **Writing:** the write is atomic: to a file beside the target, then a rename. The writer refuses a path that is absolute or escapes the root (ADR-0085), and writes to the directory root alone; it is not part of the `AssetSource` seam.
- **`SaveResult`** (ADR-0079): `status`, the located diagnostics, and the count of references written empty, such as a handle to an unnamed asset.
- **The golden:** the format research's `sword.asset.json5`, already committed under FenrirSerialisation's fixtures, loaded through the test package's `Sword` and saved, gives the same bytes. Copy it into FenrirAssets' fixtures if the test package's types need to match it field for field; if they cannot, stop and ask.

**Blocked by:** 04, 05.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/AssetStore.cpp`, `src/Envelope.cpp`, `Tests/` (a new `Saving.cpp`, the golden fixture), `Tests/Package/` if the test types need the golden's fields)

**Status:** resolved

- [x] Loading then saving the golden sword reproduces its bytes, and so does a second save.
- [x] A comment in a fixture survives a load, an `Edit` of another field, and a save.
- [x] Saving an unnamed asset to a path gives it that path. Saving a named asset to a new path leaves its own path unchanged.
- [x] A reference to an unnamed asset writes `null`, is counted in `SaveResult`, and has one diagnostic.
- [x] A save to an absolute or escaping path is refused. A save interrupted before the rename leaves the old file intact.
