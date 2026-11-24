# 02: The `AssetSource` seam, the directory source, path refusal, the case warning and `PathOf`

**What to build:** every envelope read asks one private seam for its bytes, so that a pack source can later serve the same doors (ADR-0085). An asset's identity is its root-relative path, refused when it escapes the root, and a handle can be asked its path (ADR-0076).

- **`AssetSource`:** a private interface with two calls: "the bytes at this asset path" and "does this path exist". The only implementation is a directory source over `AssetsConfig::root`, and every envelope read from ticket 01 goes through it. Importers keep opening their own files, since a cooked build pre-bakes them (the spec, "Gaps settled here").
- **Path rules:**
  - An asset path is root-relative, uses forward slashes and includes its extension; `#label` and `builtin:` names work as today.
  - A path in a file that is absolute or contains `..` is refused with a diagnostic.
  - `Load` of an absolute path under the root is keyed by its root-relative form. One outside the root fails with the new `LoadFailure::OutsideRoot`, which amends ADR-0021's "taken as given" and its test in `Loading.cpp`.
  - Comparison is case-sensitive. In a development build, the directory source warns once per path when a file is found only by a case-insensitive match.
- **`PathOf<T>(handle) -> std::optional<std::string>`:** gives the asset path of a loaded or named asset, and is empty for an unnamed or stale one. It works for every kind.

**Blocked by:** 01.

**Touches:** `packages/FenrirAssets/` (`include/FenrirAssets/AssetStore.hpp`, `src/AssetStore.cpp`, a new private `src/AssetSource.hpp` and `src/DirectorySource.cpp`, `src/Envelope.cpp`, `Tests/` (`Loading.cpp`, `DataAssets.cpp`, a new `AssetPaths.cpp`))

**Status:** ready-for-agent

- [ ] A data asset loads through the directory source, and a test double source serves the same bytes from memory with no file on disk.
- [ ] An absolute path under the root and its relative form give one handle. An absolute path outside the root fails with `OutsideRoot`, and the old "taken as given" test is changed to match.
- [ ] A `..` path in a file is refused with a located diagnostic.
- [ ] A path matching a file only case-insensitively warns once in a development build.
- [ ] `PathOf` answers for a loaded data asset, a loaded image and a sub-asset label. It is empty for an unnamed `Add` and for a stale handle.
