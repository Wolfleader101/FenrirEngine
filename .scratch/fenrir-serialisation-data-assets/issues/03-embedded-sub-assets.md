# 03: Embedded sub-assets: `sub`, `file#name`, the shared parsed file and owners

**What to build:** an envelope's `sub` holds named data assets embedded in the same file, loaded the way a model's labelled sub-assets are (ADR-0076, ADR-0079).

- **`sub`** (the spec, "Sub-assets"): it maps author-chosen names, unique within the file, to `type`, `version` and `data`. Sub-assets have no `sub` of their own, and a nested one is refused with a diagnostic.
- **Loading:**
  - `file#name` loads one sub-asset by label, sharing the parsed file.
  - Loading the top-level asset loads every sub-asset for the same owner.
  - A later load of any of them gives the same handle.
  - A missing label fails with `LabelNotFound`, as for models.
  - Model sub-assets keep their numbered labels.
- **Reading:** each entry goes through the same `Find`, `IsA`, `Upgrade` and `FromTree` steps as `data`, with its diagnostics in the load's `diagnostics`. A broken sub-asset leaves the others loaded and the load `Partial`.

**Blocked by:** 01.

**Touches:** `packages/FenrirAssets/` (`src/AssetStore.cpp`, `src/Envelope.cpp`, `Tests/` (`DataAssets.cpp` or a new `SubAssets.cpp`, `Tests/fixtures/data/` with a sword carrying two sub-assets))

**Status:** resolved

- [x] `Load` of `sword.asset.json5#sparks` and of the top-level sword share one parse: the file is read once.
- [x] Loading the top level loads every sub-asset for that owner, and each frees with that owner.
- [x] An unknown label fails with `LabelNotFound`. A sub-asset holding its own `sub` is refused with a located diagnostic.
- [x] A broken sub-asset leaves its siblings loaded and the load `Partial`.
