# How do the new libraries join the build?

Type: grilling
Status: resolved
Blocked by: 09, 12, 15

## Question

How do the windowing library, the GL loader, the asset importer(s), the shader toolchain and any animation library join the build, following "How does Jolt join the build?" on the physics map?

- Submodules or another route, pinned versions, static linking, `PRIVATE` links, options turned off.
- Engine warning flags kept off third-party code; MSVC runtime; Debug and Release.
- CMake options (`FENRIR_ENABLE_RENDERER`?) and defaults; what a build with rendering off looks like.
- What happens to `examples/DemoApp/libs` (not submodules today).

## Answer

No ADR: as with Jolt, every line here is cheap to change, and the hard-to-reverse calls (which libraries, ADR-0018, ADR-0020, ADR-0021; hiding them, ADR-0010) were made elsewhere.

- **Arrival: git submodules under the package that links them**, pinned, built static, linked `PRIVATE`, as Jolt, spdlog and EnTT are. No FetchContent, vcpkg or Conan, so CI's `submodules: recursive` checkout stays the only route.

  | Library | Submodule | Pin | Linked by |
  |---|---|---|---|
  | SDL3 | `packages/FenrirWindow/libs/SDL` | a 3.4.x release tag | FenrirWindow |
  | assimp | `packages/FenrirAssets/libs/assimp` | a release tag | FenrirAssets |
  | stb (`stb_image.h`) | `packages/FenrirAssets/libs/stb` | a commit (stb has no tags) | FenrirAssets |
  | glslang | `packages/FenrirRenderer/Tools/ShaderCompiler/libs/glslang` | a release tag | `FenrirShaderCompiler` only |
  | SPIRV-Reflect | `packages/FenrirRenderer/Tools/ShaderCompiler/libs/SPIRV-Reflect` | a release tag | `FenrirShaderCompiler` only |

  The exact tags are picked by the implementation ticket that adds each submodule.
- **The GL loader is generated once and checked in**, not fetched: glad 2's generator (GL 4.6 core, `--mx` multi-context) is run by hand and its `.c`/`.h` committed under `packages/FenrirRenderer/libs/glad/`, with a README holding the exact regeneration command. No Python or Jinja2 in the build. It is its own static target named so it cannot clash with the old DemoApp's `glad` (for example `FenrirGlad`), linked `PRIVATE` by FenrirRenderer's OpenGL backend only.
- **Flags, runtime and configurations follow Jolt's `libs/CMakeLists.txt`**, each package's own, with every override local to that directory:
  - clear the inherited directory `COMPILE_OPTIONS`, so the root's `-Weverything` / `/W4` reach neither C++ nor C sources (SDL3, glad and stb are C); turn off each library's warnings-as-errors; mark every target `SYSTEM`;
  - `BUILD_SHARED_LIBS OFF` set locally; the dynamic MSVC runtime kept (`SDL_FORCE_STATIC_VCRT OFF`, assimp's `USE_STATIC_CRT OFF`); no LTO;
  - Debug and Release supported; `RelWithDebInfo` and `MinSizeRel` documented, not mapped;
  - **SDL3:** the subsystems ADR-0020 leaves off (audio, GPU, 2D render, camera, dialogs, tray, process, `SDL_main`), plus its tests, examples and install;
  - **assimp:** only the glTF, FBX and OBJ importers; no exporters (`ASSIMP_NO_EXPORT`), tests, tools, install or Draco; its bundled zlib kept;
  - **glslang:** no HLSL front end, no optimizer (`ENABLE_OPT OFF`, so no SPIRV-Tools), no binaries, tests or install;
  - **SPIRV-Reflect:** its static library only, no executable.

  The exact option list is the implementation ticket's; a break is a line in that file, not a design decision. "Spike: a lit mesh through the seam" (ticket 25) confirms every preset builds with these settings.
- **Two new root options, both default `ON`** (nothing outside the submodules is needed, like physics):
  - `FENRIR_ENABLE_ASSETS` builds FenrirAssets, assimp and stb. Kept apart from the renderer so a headless app or server can load skeletons and clips with no SDL3 or GL.
  - `FENRIR_ENABLE_RENDERER` builds FenrirWindow, FenrirRenderer, `FenrirShaderCompiler`, SDL3, glad and glslang. On with `FENRIR_ENABLE_ASSETS` off is a configure `FATAL_ERROR`, not a silent fix.
  - FenrirInput links no library and is always built, like FenrirTime. The renderer's scripting extension builds only when both it and `FENRIR_ENABLE_SCRIPTING` are on, as physics's does. Package tests follow their package's option.
  - **With rendering off**, none of those targets exist, nor the demo apps that need them, nor their tests: the build is the engine core plus physics and scripting, as today. An animation option is ticket 24's call.
- **The old DemoApp until it is deleted:**
  - `FENRIR_BUILD_EXAMPLES` (`ON`) now means the new demo apps under `examples/`. Their libraries all arrive as submodules, so CI turns it on.
  - The old DemoApp moves behind `FENRIR_BUILD_LEGACY_DEMOAPP` (`OFF`). `examples/DemoApp/libs` is left as it is (hand-copied, not submodules), with one guard: it skips its own `add_subdirectory(assimp)` when an `assimp` target already exists. No submodules are added for libraries that are about to be deleted; both go when the demo apps replace it.
- **Libraries not yet chosen join by the same rule** (a submodule under the package that links it, overrides local to its `libs/`, `SYSTEM` and `PRIVATE`), each named by its own ticket: an animation library if ticket 24 picks one, a font rasteriser for debug text (ticket 18), SPIRV-Cross once a second backend exists.
- **Consequences for the spec:** the README's options table gains the two options and the legacy one; CI's `FENRIR_CMAKE_OPTIONS` turns on assets, renderer and examples; Linux CI installs the X11 and Wayland development packages SDL3's video needs.

## Comments

- Amended by ADR-0031 (ticket 26): cgltf and ufbx replace assimp as FenrirAssets' model importers, each a pinned submodule under `packages/FenrirAssets/libs/` by this ticket's rule.
