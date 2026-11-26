# 04: Typed references through the store's resolvers: kind and assignability checks, diagnostics, hard loads with the owner, cycles

**What to build:** an `AssetHandle<T>` field in a data asset reads a path, and the store loads its target first, then fills the handle (ADR-0076). A missing target, or one of the wrong type, leaves the handle empty and says why, naming the file, field, path, expected and found.

- **`Resolvers::readReference`**, supplied by the store (the spec, "Hard references"):
  - It takes the path and the field. `type.target` names `T`.
  - It loads the target synchronously with the referencing load's owner, and returns its `ReferenceValue`.
  - A missing target, a target of the wrong kind, or a data asset whose type is not `IsA` `T` returns nothing with one diagnostic, counted in the load's `diagnostics`.
  - A bare `#name` is the enclosing file's sub-asset.
- **Cycles:** a file's slot is reserved under its path before its references load, as `ReserveBuiltin` does for `builtin:` names. A reference to a file already loading gets its handle at once, so a pair that reference each other both load.
- **`Resolvers::writeReference`** turns a `ReferenceValue` back into a path through `PathOf`, for ticket 06. Sub-asset references write in full as `path#name`.
- **Entities:** the store passes no entity callbacks. Ticket 12 makes the tool refuse an `entity` in a data asset.

**Blocked by:** 02, 03.

**Touches:** `packages/FenrirAssets/` (`src/AssetStore.cpp`, `src/Envelope.cpp`, `src/AssetStoreState.hpp`, `Tests/` (a new `References.cpp`, and fixtures: a pair referencing each other, a reference to a missing file, a `.png` referenced from an `AssetHandle<WeaponData>` field, an `AssetHandle<Image>` to a real image))

**Status:** resolved

- [x] A sword's `AssetHandle<Image>` loads its image with the sword's owner, and the image frees with that owner.
- [x] Two files referencing each other both load, each handle resolving to the other.
- [x] A missing target and a `.png` in an `AssetHandle<WeaponData>` field each leave the handle empty, with one located diagnostic naming the file, field, path, expected and found, and the load `Partial`.
- [x] An `AssetHandle<WeaponData>` pointing at a `Sword` file resolves, while an `AssetHandle<Sword>` pointing at a plain `WeaponData` file does not.
