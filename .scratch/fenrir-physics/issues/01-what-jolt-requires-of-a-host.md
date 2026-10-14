# What does Jolt require of a host engine?

Type: research
Status: resolved
Blocked by: none

## Question

What does JoltPhysics require of, and offer to, an engine that embeds it, in the areas where those facts constrain FenrirPhysics's design? Answer from Jolt's own source, docs and samples (pin the version or commit read):

- **Threading:** the `JobSystem` interface. Can it run on a host's own thread pool (FenrirScheduler's `ThreadPool`), or does it want its own threads? What a minimal single-threaded setup looks like.
- **Lifetimes:** how bodies, `BodyID`s and shapes (ref-counted) are created, added, removed and destroyed; what is cheap to recreate and what is not.
- **Locking and access:** `BodyInterface` locking vs non-locking, and what may be called during a step or from a listener.
- **Layers:** object layers, broad-phase layers and the filter interfaces the host must implement.
- **Callbacks:** `ContactListener` and `BodyActivationListener` — which thread they run on, what they may do, and the contact lifecycle (added, persisted, removed) including sensors.
- **Stepping:** `PhysicsSystem::Update` collision steps, and what it needs (temp allocator, job system).
- **Determinism:** what Jolt guarantees, and which build flags and usage rules (e.g. `JPH_CROSS_PLATFORM_DETERMINISTIC`, add/remove ordering, multithreading) it depends on.
- **Precision:** `JPH_DOUBLE_PRECISION` and what changes at the API boundary.
- **Debug rendering:** `JPH_DEBUG_RENDERER`, the `DebugRenderer` interface, and its cost in release builds.
- **Build:** CMake integration (as a subdirectory/submodule), required options, compiler and C++ standard requirements, interaction with `/W4` and clang `-Weverything`, licence.
- **Scale:** how non-uniform scale is handled on shapes.

Write findings to a Markdown file on the `research/jolt-host-requirements` branch, citing each claim's source, and link it here.

## Answer

Findings: `.scratch/fenrir-physics/research/jolt-host-requirements.md` on branch `research/jolt-host-requirements` (commit `516dfa3`), against Jolt v5.6.0 (`e77f175`), each claim cited.

- **Threading:** `JobSystem` is an interface meant for the host's scheduler; `JobSystemWithBarrier` needs four functions, so FenrirScheduler's `ThreadPool` can back it (gap: it exposes no thread count for `GetMaxConcurrency`). `JobSystemSingleThreaded` is the fallback. `Update` blocks until done.
- **Locking:** all body and constraint locks are held during `Update`; no reads, writes, adds or removes during a step.
- **Callbacks:** contact and activation listeners run on job threads, unordered, read-only through NoLock interfaces; `OnContactRemoved` gets only IDs. Buffer under a mutex and sort (Jolt's own sensor sample does this). A body going to sleep ends its contacts.
- **Determinism:** same binary holds regardless of thread count, given the same API call order and reproducible `BodyID`s (`CreateBodyWithID`). `CROSS_PLATFORM_DETERMINISTIC` (~8% cost) only matters across builds.
- **Lifetimes:** capacities fixed at `Init`, overflow reported as error flags; batch add is the cheap path; shapes ref-counted, shareable, cooked once. Each body has a `uint64` user data (room for an entity).
- **Process-global state:** allocator, trace/assert hooks, factory, type registration, debug renderer instance are shared by every physics world.
- **Stepping:** constant dt suits warm starting; 60 Hz with one collision step is the sweet spot.
- **Precision:** double precision changes only position types and query base offsets.
- **Debug rendering:** opt-in draw calls; compiled in Debug/Release, out in Distribution, keyed to Jolt's own configuration names.
- **Build:** C++17, MIT, `add_subdirectory(<jolt>/Build)`. Defaults that bite: static MSVC runtime, LTO on, no RTTI/exceptions, 5.6 GPU compute backends on (pull in Vulkan/dxc), `/WX`/`-Werror` plus the engine's directory-wide warning flags.
- **Scale:** non-uniform scale only on some shapes; changing scale means a new shape (`SetShape`).
