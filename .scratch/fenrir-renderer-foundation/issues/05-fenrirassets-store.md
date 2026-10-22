# 05: FenrirAssets: the store, handles, owners, meshes and images

**What to build:** FenrirAssets exists as a headless package holding plain CPU-side assets behind generational handles owned by the app or a scene, with meshes built in code and images loaded from PNG, JPEG and HDR (spec: "FenrirAssets" (package, handles, owners and lifetime, loading, revisions, data shapes for `MeshData`, `ImageData` and `ShaderData`), "Build" (stb row, `FENRIR_ENABLE_ASSETS`), "Room left for animation" (FenrirAssets), "Testing Decisions" seam 4; ADR-0021). Materials are ticket 07, models and their importers ticket 08.

- **Package** `packages/FenrirAssets`, depending on FenrirScene and FenrirApp only. `Assets::Install(App&, AssetsConfig)` returns the owner of the store, which is an app resource (it cannot fail).
- **Root option** `FENRIR_ENABLE_ASSETS` (`ON`) builds FenrirAssets and stb; renderer on with assets off is a configure `FATAL_ERROR`. README options table and CI's `FENRIR_CMAKE_OPTIONS` follow.
- **Handles:** `AssetHandle<T>`, a plain trivially copyable `{ index, generation }`, with the short names `Mesh`, `Image`, `Material`, `Shader` and `Model`, resolving to `MeshData`, `ImageData`, `MaterialData`, `ShaderData` and `Model` (the last three declared, filled by tickets 06–08). `Skeleton` and `Clip` are reserved. ADR-0024 and ADR-0027 are amended to the short spelling.
- **Owners and lifetime:** each `Load` or `Add` names an owner, the app or a scene; an asset is freed when its last owner ends or on `Unload`, which bumps the slot's generation. Loading a path already loaded returns the same handle and adds the owner. Resolving a stale or invalid handle is detectable, and the "logged once per handle" helper the renderer uses lives here.
- **Loading:** synchronous and main-thread only. `Load<T>(path, owner)` returns a result with the handle or a diagnostic (path, reason, importer message), and parses `file#Label` sub-asset paths (the importers in ticket 08 use them). Paths resolve against `AssetsConfig`'s root (default `assets/` beside the executable); absolute paths are taken as given.
- **Revisions:** `Add<T>(data, owner)` gives a handle with no path and no deduplication; `Replace(handle, data)` swaps the data and bumps a revision every reader sees.
- **`MeshData`:** optional streams (positions, normals, tangents, UV0, UV1, colours), 32-bit indices, submeshes (index range plus material slot), local bounds computed on `Add` and `Replace`. Room left for a skin section, not built.
- **`ImageData`:** width, height, format (`RGBA8`, `RGBA16F`, `RGBA32F`) and a colour space; optional pixels (none makes a render target, RGBA8 sRGB; ADR-0026); a cubemap as six faces of one size. No mips.
- **`ShaderData`:** a shader container's bytes, opaque here (ticket 06 adds its parameter table).
- **stb_image** (`packages/FenrirAssets/libs/stb`, a pinned commit, `PRIVATE`, behind a private importer seam) reads PNG, JPEG and HDR; the colour space is set by the caller or by first use (ticket 08 for materials).
- **`PublicHeaders.cpp`** fails the build if stb reaches a public header.

**Blocked by:** 01 (Engine prerequisites).

**Touches:** `packages/FenrirAssets/`, `.gitmodules`, `CMakeLists.txt`, `README.md`, `.github/workflows/build-and-test.yml`, `docs/adr/0024-fenrirrenderer-materials-are-shared-assets-bound-through-a-reflected-material-set.md`, `docs/adr/0027-ui-draws-through-fenrirdraw2d-and-takes-input-through-input-routers.md`

**Status:** ready-for-agent

- [ ] `FenrirAssetsTests` (headless): handles are trivially copyable; an app-owned asset outlives scenes; a scene-owned asset is freed with its scene; an asset owned by two scenes lives until both end; `Unload` frees it and the old handle resolves as stale.
- [ ] Loading the same path twice returns the same handle and adds the owner; `Add` never deduplicates.
- [ ] `Replace` bumps the revision; `MeshData`'s bounds are computed on `Add` and `Replace`.
- [ ] Load diagnostics name the path, reason and importer message for a missing file and a corrupt PNG; relative paths resolve against the asset root, absolute ones as given.
- [ ] A PNG, a JPEG and an HDR fixture load with the right size and format; a pixel-less image and a six-face cubemap are accepted.
- [ ] `FENRIR_ENABLE_ASSETS=OFF` builds the engine without FenrirAssets or stb; renderer on with assets off fails at configure; Clang and MSVC x64 presets build with no warnings from stb.
