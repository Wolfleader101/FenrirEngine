# 12: Remove glslang, SPIRV-Reflect, the GLSL front end and the GLSL includes, and finish the docs

**What to build:** with every shader in the tree in Slang, `FenrirShaderCompiler` loses its GLSL front end and the two submodules that served it, the GLSL includes leave `Shaders/include/Fenrir/`, and the docs say what is now true (ADR-0073, "GLSL is accepted until every shader in the tree is ported, and no longer": this ticket follows ticket 11 directly, there being no releases to hold a grace period in, and the ADR names the last commit that compiled GLSL).

- **The tool:** delete `StageCompiler.cpp`/`.hpp` (glslang), `Reflection.cpp`/`.hpp` (SPIRV-Reflect) and `Pragmas.cpp`/`.hpp`; `CompileStage` is the Slang path alone, and a `.vert`, `.frag` or `.comp` file passed to `fenrir_add_shaders` is a configure error naming ADR-0073 and the last commit that accepted it. `Program.cpp` loses the GLSL-only branches (the preamble, the pragma application). The GLSL must-fail fixtures and `ShaderCompilerTest.hpp`'s GLSL helpers go; the Slang twins stay.
- **The submodules:** `packages/FenrirRenderer/Tools/ShaderCompiler/libs/glslang` and `libs/SPIRV-Reflect` are removed from `.gitmodules` and the tree, with `libs/CMakeLists.txt` reduced to the Slang fetch; the README's submodule list and the "New worktree submodules" notes follow.
- **The includes:** every `Shaders/include/Fenrir/*.glsl` is deleted; the `.slang` modules keep their header comments' references to the ADRs, with "twin of `X.glsl`" removed.
- **Docs:** ADR-0073 is already `accepted`; it gains one line naming the last commit that compiled GLSL (the merge of ticket 11), and ADR-0018's superseded line stays. `GLOSSARY.md`: *FenrirShaderCompiler* ("compiles Slang to SPIR-V"), *Shader variant* (unchanged: `#define` keys), *shader include* removed in favour of *shader module*. `packages/FenrirRenderer/README.md`: "User shaders" in Slang only, the `fenrir_add_shaders` paragraph, and the submodule list. `cmake/FenrirShaders.cmake`'s header comment. `docs/agents` and `README.md`'s "Building and testing" (Slang's fetch is the shader toolchain). `.scratch/fenrir-renderer/map.md`'s decision line for ticket 12 points at ADR-0073.
- **Closing the spec:** the commit that records the spec built deletes `.scratch/fenrir-renderer-slang/` (docs/agents/issue-tracker.md, "Built"), and the map's line names that commit.

**Blocked by:** 11 (Port the demos' and tests' shaders).

**Touches:** `packages/FenrirRenderer/Tools/ShaderCompiler/`, `.gitmodules`, `packages/FenrirRenderer/Shaders/include/Fenrir/*.glsl`, `cmake/FenrirShaders.cmake`, `docs/adr/0073-*.md`, `GLOSSARY.md`, `packages/FenrirRenderer/README.md`, `README.md`, `.scratch/fenrir-renderer/map.md`

**Status:** ready-for-agent

- [ ] `git grep -l glslang` and `git grep -l -i spirv-reflect` outside `docs/adr/` and git history return nothing; the two submodules are gone and a fresh clone with `--recursive` fetches neither.
- [ ] A `fenrir_add_shaders` call naming a `.frag` fails at configure with a message naming ADR-0073.
- [ ] Every test and every golden passes; the Windows CI job builds without the submodules.
- [ ] `FenrirShaderCompiler`'s source is smaller than before the move began (1,832 lines at `6b94df2f`), and no file in it names glslang or SPIRV-Reflect.
