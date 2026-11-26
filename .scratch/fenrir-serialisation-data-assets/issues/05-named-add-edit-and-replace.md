# 05: Named `Add<T>`, `Edit<T>` generalising `EditMaterial`, and `Replace<T>` for data assets

**What to build:** code can make a data asset under a path, so it can be saved and found again. It can also edit any data asset in place through one door that hot reload and an inspector will share (ADR-0075, ADR-0076).

- **`Add<T>(value, owner, name)`:** keys the asset by `name`, an asset path under the root, deduplicated like a loaded asset. A name already in use, or one outside the root, is a logged error and gives an empty handle. The unnamed `Add<T>(value, owner)` stays for every kind.
- **`Edit<T>(handle, action) -> T*`:**
  - It gives a mutable pointer to the instance and bumps the revision. `action` is the word the log uses when the handle does not resolve.
  - It is public for data asset types.
  - The material, clip and bone-mask edits move onto its private core, with their behaviour and messages unchanged.
  - Kinds whose `Add` derives data (mesh, skeleton, model) keep `Replace` only.
- **`Replace<T>(handle, value)`:** works for a data asset, as it does today for other kinds. It requires the record's type to be exactly `T`, never a base, and logs otherwise.

**Blocked by:** 01.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/AssetStore.cpp`, `src/Materials.cpp`, `src/AnimationEdits.cpp`, `Tests/` (`DataAssets.cpp`, `Revisions.cpp`, `Materials.cpp`, `Clips.cpp`, `BoneMasks.cpp` where the edit core moves))

**Status:** resolved

- [x] A named `Add` gives a handle whose `PathOf` is the name, and a second named `Add` under that name is refused.
- [x] `Edit` changes a field in place and bumps the revision, as `Revisions.cpp` checks for materials. `Replace` does the same.
- [x] `Replace<WeaponData>` on a `Sword` record is refused and logged.
- [x] Every existing material, clip and bone-mask edit test passes unchanged.
