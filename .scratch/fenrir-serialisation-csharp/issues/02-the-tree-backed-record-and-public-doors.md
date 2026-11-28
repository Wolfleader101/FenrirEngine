# 02: FenrirAssets: the tree-backed record, its materialised C++ base, and public by-name doors

**What to build:** the store holds a data asset of a tree-backed type as its value tree, and exposes untyped by-name doors that scripting and tools can call. A C++ base keeps working from C++. The user made both decisions on 2026-11-28 (the spec's "The registry and the store").

- **The tree-backed record.** A record of a tree-backed type holds its value tree, not a `DataValue`.
  - Load reads the envelope's `data` into the tree, upgraded and checked against the description: kinds, references resolved, and unknown fields dropped under the rule.
  - Save writes the tree back, keeping the file's comments.
  - The asset watcher's reload replaces the tree under the same handle and bumps the revision.
- **The materialised base.** When the type extends a C++ data asset type, the record also keeps a struct of that base, filled from the tree.
  - It is refreshed on every replace and reload.
  - C++ `TryGet<Base>` and `Edit<Base>` read it and edit it, and an `Edit` is folded back into the tree.
  - `TryGet<Base>` on a tree-backed type with no C++ base returns null, logged once.
- **Public by-name doors.** `AssetStore` gains public untyped doors over the private ones. They take and give trees in the tree encoding, and work for both C++ and tree-backed types: a C++ type converts through the bridge, and a tree-backed one is kept as given. The doors are:
  - load by type name, with the is-a check;
  - the tree of a record, with its revision;
  - replace from a tree;
  - add a named record from a tree;
  - save, to its own path or another.
- **Guarding `Unregister`.** The store refuses, with a reason, to let a type be unregistered while live records hold it. Give the check a seam that ticket 05's `Forget` calls.

**Blocked by:** 01.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/AssetStore.cpp`, `src/AssetStoreState.hpp`, `src/Envelope.*`, `Tests/` (a new `TreeBacked.cpp`))

**Status:** resolved

- [x] A hand-registered tree-backed type extending `AssetsTest::WeaponData` loads from a fixture envelope, saves back byte-identical, and reloads in place with its revision bumped.
- [x] `TryGet<WeaponData>` reads its base fields, and an `Edit<WeaponData>` shows in the record's tree.
- [x] Each by-name door round-trips a C++ type and a tree-backed type through tree bytes.
- [x] Unregistering a type with live records is refused.
