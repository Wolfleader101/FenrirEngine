# 01: Slang is fetched at configure time as a pinned prebuilt release, and only FenrirShaderCompiler links it

**What to build:** the build fetches one Slang release archive per host at configure time, checks its hash, and gives `FenrirShaderCompiler` an imported target to link, as ADR-0073 decides ("Slang is a pinned prebuilt release, fetched at configure time, never a submodule built from source"). Nothing else in the tree sees it. No shader is compiled with it yet: that is ticket 02.

- **The fetch:** a CMake module beside the asset download ADR-0052 uses (`cmake/`), `fenrir_fetch_slang()`, called from `packages/FenrirRenderer/Tools/ShaderCompiler/libs/CMakeLists.txt` when the renderer is on. It downloads `slang-<version>-windows-x86_64.zip` or `slang-<version>-linux-x86_64.tar.gz` from the GitHub release for the pinned version (v2026.20 at the time of writing; one variable holds the version, one the hash per archive), into the build directory, once per machine (a marker file keyed by version and hash), and extracts it. A download that fails is a configure error naming the URL and the proxy-free workaround (place the archive by hand). A hash that does not match deletes the archive and fails.
- **The target:** `Fenrir::Slang`, an `IMPORTED` shared library with its import library, include directory (`slang.h`, `slang-com-ptr.h`) and runtime DLL or `.so`; `FenrirShaderCompiler` links it `PRIVATE` and gets a post-build copy of the runtime library beside its executable, as the tool runs from the build tree in `fenrir_add_shaders`. The headers are `SYSTEM`. No `install` rule: the tool is never installed.
- **Hidden as ADR-0010 asks:** no public header names Slang, no engine, test or demo target links it, and `FenrirShaderCompiler`'s `PublicHeaders.cpp` test keeps passing. A grep in the tool's tests (as `PublicHeaders.cpp` does for glslang) asserts that no header under `packages/*/include` mentions `slang`.
- **Vulkan SDK as an override:** `FENRIR_SLANG_ROOT` points the build at an existing Slang (the SDK's, for a developer) instead of fetching, with a configure message saying which was used and its version; the pinned version is still the one CI uses.
- **CI:** `.github/workflows/build-and-test.yml` fetches it like any configure step, with the archive cached by version (`actions/cache` on the download directory), and the Linux job's commented block gets the same.
- **Docs:** `README.md`'s "Building and testing" names the download, the override and the one-time cost; `packages/FenrirRenderer/Tools/ShaderCompiler/libs/CMakeLists.txt`'s header comment says why Slang is fetched and glslang is a submodule.

**Blocked by:** none.

**Touches:** `cmake/FenrirSlang.cmake` (new), `packages/FenrirRenderer/Tools/ShaderCompiler/libs/CMakeLists.txt`, `packages/FenrirRenderer/Tools/ShaderCompiler/CMakeLists.txt`, `packages/FenrirRenderer/Tools/ShaderCompiler/Tests/PublicHeaders.cpp`, `.github/workflows/build-and-test.yml`, `README.md`

**Status:** ready-for-agent

- [ ] A clean configure of msvc-x64-debug downloads the archive once, and a second configure does not download again; `FENRIR_SLANG_ROOT=C:\VulkanSDK\<version>` skips the download and says so.
- [ ] A wrong hash in the CMake variable fails configure with a message naming the archive.
- [ ] `FenrirShaderCompiler` builds and links `slang`, and its executable runs (a `--version`-style smoke call that creates and drops a Slang global session) with the runtime library found beside it.
- [ ] `PublicHeaders` asserts no public header names Slang; `ldd`/`dumpbin` on `FenrirRendererCore` and the demos shows no Slang library.
- [ ] CI's Windows job configures and builds with the fetch, cached on the second run.
