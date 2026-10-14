# What does Jolt require of a host engine?

Type: research
Status: open
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
