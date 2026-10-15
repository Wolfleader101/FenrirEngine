# How does Jolt join the build?

Type: grilling
Status: resolved
Blocked by: 01

## Question

How is Jolt brought into the engine's build without its defaults leaking into the rest of it? "What does Jolt require of a host engine?" found the options that bite:

- How Jolt arrives: a submodule under `packages/FenrirPhysics/libs/` like the other packages' libraries, pinned to v5.6.0, added with `add_subdirectory(<jolt>/Build)`.
- Which of its defaults we override: the static MSVC runtime, link-time optimisation, RTTI and exceptions off, the GPU compute backends (and their Vulkan/dxc dependency), `/WX`/`-Werror`.
- How the root's directory-wide warning flags (`-Weverything`, `/W4`) are kept off Jolt's sources while staying on FenrirPhysics's.
- How CMake's `RelWithDebInfo`/`MinSizeRel` map onto Jolt's configurations, so the debug renderer is present where we want it.
- Whether the root gets a `FENRIR_ENABLE_PHYSICS` option (as scripting has `FENRIR_ENABLE_SCRIPTING`), and whether FenrirPhysics links Jolt `PRIVATE` so no Jolt include path reaches its users (the compile-level hiding in the map's Notes).
- Whether "Jolt builds cleanly under our flags" needs a quick spike before deciding.

## Answer

- **Arrival:** a git submodule at `packages/FenrirPhysics/libs/JoltPhysics`, pinned to `v5.6.0`, added by `packages/FenrirPhysics/libs/CMakeLists.txt` with `add_subdirectory(JoltPhysics/Build)` (Jolt's CMake root is `Build/`), as spdlog and EnTT are. Built as a **static library** (`JPH_BUILD_SHARED_LIBS OFF`, set explicitly). FenrirPhysics links `Jolt` `PRIVATE`; its backend sources see Jolt's headers as `SYSTEM` includes.
- **Option overrides**, set in `libs/CMakeLists.txt` before the `add_subdirectory` so they stay local to Jolt:

  | Option | Jolt default | Ours | Why |
  |---|---|---|---|
  | `JPH_BUILD_SHARED_LIBS` | OFF | OFF (explicit) | Static, like every engine library |
  | `USE_STATIC_MSVC_RUNTIME_LIBRARY` | ON (`/MT`) | OFF | Match the engine's dynamic runtime |
  | `INTERPROCEDURAL_OPTIMIZATION` | ON | OFF | The engine has no LTO; a mismatch fails the link |
  | `CPP_RTTI_ENABLED` | OFF | ON | The backend subclasses Jolt interfaces; an RTTI mismatch gives `typeinfo` link errors |
  | `CPP_EXCEPTIONS_ENABLED` | OFF | OFF | Nothing throws across the seam |
  | `JPH_USE_DX12`, `JPH_USE_VK`, `JPH_USE_MTL`, `JPH_USE_CPU_COMPUTE` | ON | OFF | GPU backends for hair simulation; pull in Vulkan and dxc |
  | `DEBUG_RENDERER_IN_DEBUG_AND_RELEASE`, `PROFILER_IN_DEBUG_AND_RELEASE` | ON | OFF | Unused ("What does debug rendering data look like?"); removes the debug-define link hazard and most per-configuration defines |
  | `ENABLE_OBJECT_STREAM` | ON | OFF | Jolt's serialization is unused |
  | `CROSS_PLATFORM_DETERMINISTIC` | OFF | OFF | Only same-build determinism is required |
  | CPU features (`USE_AVX2`, ...) | ON | default | Requires an AVX2 CPU; revisit if the engine targets older CPUs |

- **Warnings:** `libs/CMakeLists.txt` clears the inherited directory `COMPILE_OPTIONS` (so the root's `-Weverything` / `/W4` do not reach Jolt's sources) and sets `ENABLE_ALL_WARNINGS OFF` (removing Jolt's `-Werror` / `/WX`). FenrirPhysics's own sources keep the engine's flags; backend `.cpp` files wrap Jolt includes in `JPH_SUPPRESS_WARNING_PUSH/POP`, as Jolt's HelloWorld does.
- **Configurations:** Debug and Release are supported and match Jolt's names. Other configurations (`RelWithDebInfo`, `MinSizeRel`) build Jolt without its debug-only checks; documented, not mapped.
- **`FENRIR_ENABLE_PHYSICS`**, a root option defaulting ON (physics has no external dependency, unlike scripting's .NET): ON builds FenrirPhysics, Jolt and the DemoApp's physics scene; OFF builds none of them. The physics scripting extension builds only when both it and `FENRIR_ENABLE_SCRIPTING` are on.
- **Verification** belongs to "Spike: a box dropping onto a plane through the proposed seam": every preset builds with these settings. A break is a line in `libs/CMakeLists.txt`, not a design decision.
