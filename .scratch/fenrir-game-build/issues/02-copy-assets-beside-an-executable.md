# 02: Copy a game's assets beside its executable

Spec: [Building a game against the engine](../spec.md).

**What to build:** A game copies one or more asset folders beside its executable with `fenrir_copy_assets(<target> DIRECTORIES <dirs>... DESTINATION <folder>)`, from a new engine CMake module, FenrirGame, that the engine's root includes so a parent project has it after `add_subdirectory` and that exists with the demo apps off. Directories are relative to the calling list file and copied after every build in order, so a later directory's file replaces an earlier one's, and an edited file is copied again on the next build. The demo app function copies the shared demo assets, the demo UI kit's files and the app's own through it, its documented behaviour unchanged.

**Blocked by:** None (can start immediately)
**Touches:** `cmake/FenrirGame.cmake` (new), `CMakeLists.txt`, `examples/DemoScaffold/cmake/FenrirDemoApp.cmake`, `cmake/Tests/`

**Status:** ready-for-agent

Stacked on 01 (both touch the root `CMakeLists.txt`).

- [ ] A new case in the CMake script tests, in the style of the fetch-demo-assets tests, configures and builds a small project that includes only the FenrirGame module with a dummy executable, and checks that each directory's files land in the destination folder, that a later directory overrides an earlier one, and that an edited file is copied again on the next build.
- [ ] That case runs in CTest and needs no engine build.
- [ ] The demo app function uses `fenrir_copy_assets`; every demo app's null-backend smoke run passes unchanged.
- [ ] The function exists in a project that adds the engine with the demo apps off.
