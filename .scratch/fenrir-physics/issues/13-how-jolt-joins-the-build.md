# How does Jolt join the build?

Type: grilling
Status: open
Blocked by: 01

## Question

How is Jolt brought into the engine's build without its defaults leaking into the rest of it? "What does Jolt require of a host engine?" found the options that bite:

- How Jolt arrives: a submodule under `packages/FenrirPhysics/libs/` like the other packages' libraries, pinned to v5.6.0, added with `add_subdirectory(<jolt>/Build)`.
- Which of its defaults we override: the static MSVC runtime, link-time optimisation, RTTI and exceptions off, the GPU compute backends (and their Vulkan/dxc dependency), `/WX`/`-Werror`.
- How the root's directory-wide warning flags (`-Weverything`, `/W4`) are kept off Jolt's sources while staying on FenrirPhysics's.
- How CMake's `RelWithDebInfo`/`MinSizeRel` map onto Jolt's configurations, so the debug renderer is present where we want it.
- Whether the root gets a `FENRIR_ENABLE_PHYSICS` option (as scripting has `FENRIR_ENABLE_SCRIPTING`), and whether FenrirPhysics links Jolt `PRIVATE` so no Jolt include path reaches its users (the compile-level hiding in the map's Notes).
- Whether "Jolt builds cleanly under our flags" needs a quick spike before deciding.
