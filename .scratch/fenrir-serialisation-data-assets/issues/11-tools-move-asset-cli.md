# 11: `tools/move-asset` CLI with a CTest smoke run

**What to build:** a thin command-line tool over `Assets::Move`, so a developer can move an asset from a shell (ADR-0079).

- **`tools/move-asset <root> <from> <to> [--dry-run]`:**
  - It is a C++ executable that makes a `TypeRegistry`, registers the engine's packages as `App` would, and calls `Assets::Move`.
  - It prints `MoveResult`: each changed file, then each unscanned one.
  - It exits non-zero on a refused move.
  - A game builds its own copy by linking its registration, and the README says how.
- **Build:** a CMake target `fenrir-move-asset`, built with the tests. A CTest smoke run copies a fixture root into the build tree, runs a dry run and then a real move, and checks the output and the moved file.

**Blocked by:** 10.

**Touches:** `tools/move-asset/` (new: `CMakeLists.txt`, `src/main.cpp`, `README.md`), the root `CMakeLists.txt`, `README.md` (the tool's one line), `packages/FenrirAssets/Tests/` for the smoke run's fixtures if shared

**Status:** ready-for-agent

- [ ] `fenrir-move-asset` builds on msvc-x64-debug.
- [ ] The CTest smoke run passes: a dry run lists the changes and writes nothing, then a real move rewrites them.
- [ ] A refused move exits non-zero with its reason.
- [ ] The tool's README names its arguments and how a game links its own registration.
