# 07: Ragdolls in C#

**What to build:** C# systems build, switch, power and read ragdolls as C++ does: a `Fenrir.Ragdoll` contract and FenrirRagdoll scripting extension with every ragdoll component a writable mirror, the states read-only, `HumanoidBones` a plain struct, and natives `DescribeHumanoid`, `Build`, `Destroy` and `ClearOverrides` (spec: "FenrirRagdollScripting and Fenrir.Ragdoll"; ADR-0009, ADR-0039, ADR-0042 to ADR-0046, physics map ticket "What does C# get for ragdolls?").

- **The extension:** a `FenrirRagdollScripting` library and `Fenrir.Ragdoll` contract in ADR-0046's generated shape, built only with physics, assets and scripting on, installed by the demo scaffold beside the physics extension; its API description regenerated.
- **Mirrors:** `Ragdoll` (mode, strength, followRoot, group, rootBody) writable with `RagdollState` read-only through `ref readonly`; `RagdollBody` writable, its `Bone` the `Fenrir.Skeleton` contract's `BoneName` (ADR-0044), with `RagdollBodyState` read-only; `RagdollMode` and `RagdollBodyMode`; `HumanoidBones` as sixteen `BoneName` slots. Each `new` equals C++'s `{}`.
- **Natives:** `Ragdoll.DescribeHumanoid(Entity skeleton, in HumanoidBones bones, float totalMass)` returning a `RagdollDescriptionHandle` native resource (ADR-0029, ADR-0043), or `None` with a warning; `Ragdoll.Build(Entity skeleton, RagdollDescriptionHandle)` spawning the entities, returning them through the caller's buffers (or a count-then-fill pair, within ADR-0044's rules) and dropping the handle; `Ragdoll.Destroy(Entity skeleton)`; `Ragdoll.ClearOverrides(Entity skeleton)`. A described-but-not-built handle is dropped at scene end. Editing a description before building is C++-only (documented).
- **No-op rules:** the foundation's silent no-ops without a world or body; ADR-0039's for a skeleton that is not ready (a warning once, nothing built).
- **Doc comments:** switching and weights (the glue never sets weights), partial ragdolls, strength, getting up, tick-posed required, swapping the clip before powering.

**Blocked by:** 05 (Edges, determinism and ragdoll debug lines).

**Touches:** `packages/FenrirRagdoll/Scripting/`, `packages/FenrirRagdoll/Managed/`, `packages/FenrirRagdoll/api/`, `examples/DemoScaffold/`

**Status:** ready-for-agent

- [ ] `RagdollScriptingTests` with a fixture: the mirrors validate against their native layouts; each `new` equals C++'s `{}`.
- [ ] A C# system fills `HumanoidBones`, calls `DescribeHumanoid` and `Build` on a code-built rig, gets the body and joint entities, sets `Mode = Simulating`, and after playback reads `State.AnyAwake` and a body's `State.Mode`; `Destroy` removes them; `ClearOverrides` clears.
- [ ] The natives are no-ops with one warning on an unready skeleton or an unknown bone map; a handle never built is dropped at scene end without a leak.
- [ ] Compile-fail cases: writing `Ragdoll.State` or `RagdollBody.State`, or a field of either, through `Ref<T>` does not compile.
- [ ] The extension is absent when physics, assets or scripting is off; the API description is regenerated.
