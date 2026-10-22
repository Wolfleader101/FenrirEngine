---
status: accepted
---

# C# reaches skeletons and animation through mirrors, per-bone functions and extension stages

ADR-0029 settled what the renderer's packages give C#: plain-data components and resources are mirrored, behaviour is native functions, assets are named by `AssetHandle<T>`, and variable-length data crosses as spans the caller fills. FenrirSkeleton and FenrirAnimation follow those rules, and renderer map ticket 33 applies them. One thing those rules do not cover. The pose pipeline (ADR-0033) is one sequential system that runs its stages, `Animate`, `Modify` and `Late`, inside `PostUpdate`. C# systems can pick only a FenrirApp phase and an order, so a C# system cannot run inside a pose stage. A C# modifier, a C# base-pose writer, or a C# camera that follows a bone in the same frame would each be impossible. FenrirScripting knows no engine package (ADR-0009), so it cannot learn FenrirSkeleton's stages by name. It therefore gains **extension stages**, a general way for any extension to host C# systems inside its own C++ work. Pose data crosses by value, never as spans over FenrirSkeleton's arrays.

## Extension stages

- **An extension declares named stages.** A `ScriptingExtension` lists the stages it hosts by full name (`Fenrir.Skeleton.Animate`, `Fenrir.Skeleton.Modify`, `Fenrir.Skeleton.Late`). Like its native functions, they need no installed package.
- **C# joins a stage with `[StageSystem(name, Order = …)]`** on an ordinary `ISystem`. The contract gives the names as constants (`PoseStages.Modify`). The system generator registers it with its stage, as it registers phase systems with their phase. A script naming a stage no extension declares fails its load, with a diagnostic naming the system and the stage, as a native function mismatch does.
- **The extension runs the stage's systems.** When a script assembly loads, FenrirScripting tells each extension which `(stage, order)` groups the script has, and the extension places one C++ item per group in its own work. When the script is released, the extension removes the items. Each item calls into the script once and runs that group's systems in name order. As with phases, a group costs one transition per frame however many systems it has.
- **Orders interleave with C++.** FenrirSkeletonScripting adds each group to its pose stage at the group's order, through FenrirSkeleton's public stage registration. A C# modifier at `ModifierOrder.Override + 10` therefore runs after the pose override and before a C++ modifier at 2000. Equal orders keep registration order, so a C# group runs after C++ work of the same order.
- **Structural changes wait for the host.** Changes a stage system records play back when the extension's hosting system returns: for pose stages, after the whole pose pipeline. They do not play back after each group, so the pipeline never sees entities or `SkeletonPose`s appear or vanish halfway through. A throw is handled as in a phase system: the system's changes are dropped, and it is disabled until the next reload.
- **A stage that never runs is not an error.** Without FenrirSkeleton installed, or in an inactive scene, pose stage systems simply do not run, like the C++ work beside them.

## FenrirSkeleton (`Fenrir.Skeleton`)

- **Mirrors.** `SkeletonPose { AssetHandle<SkeletonData> }`, `BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`. `BoneName` is a fixed 256-byte UTF-8 buffer, and its C# `Set` rejects names over 255 bytes as C++'s does (ADR-0037). The enums `PoseState`, `AttachmentState`, `DetachMode`, `PoseSpace`, `Channels` and `Scope` are mirrored, and so is the blittable `Target { space, trs, channels, weight }`.
- **Bones are `int` indices**, −1 for none. `Skeleton.FindBone(entity, name)` looks a bone up by engine name. `SkeletonData` queries take the asset handle: `BoneCount`, `Parent`, `IsDescendant`, a bone's engine or source name filled into a caller's `Span<char>` with its length returned, and a subtree filled into a `Span<int>`.
- **Reads are per bone, or copies.**
  - Per bone: `GetLocal` (final pose), `GetBaseLocal`, `GetModel`, `GetBaseModel` and `GetBoneWorldMatrix(entity, bone, out Matrix4x4)`. Each returns `false` for a skeleton that is not `Ready` or a bone out of range.
  - Whole poses: `ReadLocals(entity, PoseKind.Base | Final, Span<Transform>)` and `ReadModel(entity, Span<Matrix4x4>)` copy into the caller's span and return how many bones they wrote.
  - `PoseState(entity)` and `PoseGeneration(entity)`, the `u64` (ADR-0035), so C# caches can key on the pose.
  - The palette is not exposed. It is the renderer's.
- **No spans over native memory.** A copy cannot go stale when reconcile, `Ensure` or a sweep moves the arrays. Nor can it be read during `Modify` while model matrices are invalid and recomputed only on demand. Crowds read through per-bone calls or one copy per skeleton per frame.
- **Base-pose writes.** `SetBaseLocal(entity, bone, Transform)` and `SetBasePose(entity, ReadOnlySpan<Transform>)` are C#'s `EditBasePose`, and mark the instance pose-dirty. They work before the pipeline, in `Animate`, and after the pipeline (seen next frame), as ADR-0033 says. In `Modify` and `Late` they write nothing and log once per call site.
- **Modifiers.** A `Modify` stage system opens `PoseEdit edit = Skeleton.Edit(entity)`. `PoseEdit` is a C# `ref struct` holding the entity and an edit token, with `Base(bone)`, `Local(bone)`, `Model(bone)` and `Write(bone, Target)` as native calls. The token is valid only during the group call that opened it. A call through a stale token, or `Edit` outside `Modify`, writes nothing and logs once. `Edit` on an instance that is not `Ready` returns an empty edit, as in C++.
- **`Late` is read-only for poses.** A `Late` stage system reads this frame's final pose and writes ordinary ECS state, such as a camera's `Transform`.
- **Overrides and weights** (ADR-0033, ADR-0038), callable outside the pipeline: `SetOverride`, `ClearOverride`, `ClearOverrides`, `SetOverrideWeight`, `GetOverrideWeight` and `ClearOverrideWeight`. So C# can be ragdoll glue.
- **Attachments and spawning.** `Skeleton.AttachmentState(entity)` is a native function. `Skeleton.Detach(entity, DetachMode, newParent)` and the headless `Skeleton.SpawnModel(model, parent)` record through the change buffer (ADR-0029), so `Detach` stays structural and never runs inside the pipeline. `Ensure` is not exposed: `SkeletonPose`s added from C# play back after the system, and reconcile picks them up at the next pipeline.

## FenrirAnimation (`Fenrir.Animation`)

- **Mirrors.** `Animator` (with `fadeSeconds` and `fadeCurve`) and `AnimationLayer`, with the enums `ClipLoop`, `LayerMode`, `AdditiveReference`, `FadeCurve` and `MaskScope`. Blending is entirely these components, so C# has crossfades, layers and masks with no further API.
- **Playback.** `Animation.Seek(entity, seconds)` takes the skeleton entity or a layer entity. `Animation.Playback(entity, out PlaybackInfo)` returns `{ state, time (double), completedLoops (ulong), finished, fading, fadeProgress }`.
- **Events.** `AnimationEvent`, `ClipFinished` and `AnimationEventsDropped` (with `layer`) are native events read through `EventReader<T>`. `ClipEventId.Of(name)` is computed in C# by the same FNV-1a, and a test pins it to C++'s.
- **Clips.** `Clips.Duration(clip)`, `EventCount`, `GetEvent(clip, index, out time)`, an event's name filled into a `Span<char>`, and `FindEvents(clip, name, Span<uint>)`. `AddEvent(clip, time, name)` and `RemoveEvent(clip, index)` edit `ClipData` and bump its revision, as C++'s authoring does (ADR-0034).
- **Bone masks.** `BoneMask` is an asset handle type. `BoneMasks.Create(owner)`, `Include(mask, name, weight, MaskScope)` and `Clear(mask)` each bump the revision, as materials are made and edited (ADR-0029).

## Packaging and failure

- **One extension per package.** `Skeleton::ScriptingExtension()` and `Animation::ScriptingExtension()` live in static libraries `FenrirSkeletonScripting` and `FenrirAnimationScripting`, with contracts in `packages/FenrirSkeleton/Managed/Fenrir.Skeleton/` and `packages/FenrirAnimation/Managed/Fenrir.Animation/`. Each is built when scripting and its package are on. `SkinnedMesh { Entity skeleton; }` is mirrored in `Fenrir.Renderer`, which owns it (ADR-0035).
- **Physics's no-op rules** (ADR-0029). With the package absent, a stale handle or a skeleton that is not `Ready`, functions do nothing and return `false`, 0, −1 or an empty result, a debug build warns once per call site, and nothing throws.

## Considered options

- **C# outside the pipeline only.** C# would write overrides and base poses in `Update` and read final poses a frame late. Rejected: no C# modifiers, and a C# camera following a bone would lag a frame.
- **Runtime delegates** (`Skeleton.AddModifier(name, order, callback)` from C#). Mirrors C++, but every script reload must drop and re-add them, and each modifier costs its own transition. Rejected in favour of declared stages, which the load checks and which reload with the script.
- **New FenrirApp phases for pose stages.** Rejected by ADR-0033: animation's words would join the core phase enum.
- **`ReadOnlySpan<T>` over `SkeletonPoses`**, valid until the end of the current system, like `Ref<T>`. Zero-copy, but stale across `Ensure` and reconcile, and wrong during `Modify`, where model matrices are lazy. Rejected: copies and per-bone calls are safe, and their cost has not shown in a profile.
- **Playing back a stage system's changes after each group.** Entities and skeletons could appear halfway through the pipeline. Rejected.

## Consequences

- *Amends ADR-0009 and ADR-0029*: an extension may also declare stages that C# systems join and that it runs inside its own C++ work. FenrirScripting gains a prerequisite in the animation spec: stage declarations on `ScriptingExtension`, `[StageSystem]` in the generator, the load-time stage check, per-group registration and removal callbacks, and playback when the host returns.
- *Refines ADR-0033*: FenrirSkeleton's stage registration (`AddModifier` and its `Animate` and `Late` counterparts) returns a handle that removes the work, outside the pipeline, so script reloads can replace their groups.
- AnimationDemo's C# twins are *Playback (C#)* and *Bone attachments (C#)* (ticket 34). The Playback twin also crossfades and adds one masked layer, so blending gets no twin of its own.
- Later: zero-copy pose spans if C# crowds show copying in a profile, and C# stages for other packages' work as the mechanism allows.
