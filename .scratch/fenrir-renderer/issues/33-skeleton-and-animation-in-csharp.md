# What does C# get from FenrirSkeleton and FenrirAnimation?

Type: grilling
Status: resolved
Blocked by: 22, 28, 29

## Question

Each package extends scripting through its own extension, built only when scripting is on (ADR-0009). What do FenrirSkeleton's and FenrirAnimation's extensions give C#?

- Reading bone poses (base, final, model space, world matrices) and bone lookup by name, without copying whole arrays each call.
- Writing modifier targets and weights (ADR-0028), and bone attachments.
- Playing, crossfading and layering clips; reading playback state; receiving clip events.
- How variable-length pose arrays cross the boundary (spans over native memory, as other extensions do or not).
- Which of this follows the renderer's scripting extension's choices (ticket 22).

## Answer

ADR: `docs/adr/0039-csharp-reaches-skeletons-and-animation-through-mirrors-per-bone-functions-and-extension-stages.md` (amends ADR-0009 and ADR-0029, refines ADR-0033). Glossary: **Extension stage** added, **Scripting extension** extended. All of it goes into the animation spec. Settled with the user: C# joins pose stages through extension stages, pose arrays cross as per-bone calls and caller-filled copies, and blending gets no C# demo twin of its own.

- **Extension stages (a FenrirScripting prerequisite).** An extension declares named stages; C# joins one with `[StageSystem(PoseStages.Modify, Order = …)]` on an ordinary `ISystem`; an undeclared stage name fails the load. On script load the extension places one C++ item per `(stage, order)` group in its own work, through FenrirSkeleton's public stage registration, so C# orders interleave with C++ modifiers (equal orders: C++ first). One transition per group per frame. Structural changes play back when the hosting system returns, i.e. after the whole pose pipeline. A throw disables the system until reload. FenrirSkeleton's stage registration returns a removable handle, so reloads replace their groups.
- **`Fenrir.Skeleton`:** mirrors `SkeletonPose`, `BoneAttachment`, `BoneName` (rejecting `Set`), `Target`, and the enums `PoseState`, `AttachmentState`, `DetachMode`, `PoseSpace`, `Channels`, `Scope`. Bones are `int` (−1 none); `FindBone`, `SkeletonData` queries (count, parent, `IsDescendant`, names into `Span<char>`, subtree into `Span<int>`).
  - Reads: per bone (`GetLocal`, `GetBaseLocal`, `GetModel`, `GetBaseModel`, `GetBoneWorldMatrix`, each `false` when not `Ready`), whole-pose copies (`ReadLocals(entity, PoseKind, Span<Transform>)`, `ReadModel(entity, Span<Matrix4x4>)`), `PoseState`, `PoseGeneration`. No palette; no spans over native memory.
  - Writes: `SetBaseLocal`/`SetBasePose` (C#'s `EditBasePose`; refused with a log in `Modify` and `Late`); in `Modify`, `Skeleton.Edit(entity)` returns a `ref struct PoseEdit` with an edit token checked by every native call (`Base`, `Local`, `Model`, `Write`); `Late` read-only for poses.
  - Overrides and override weights as C++ (C# can be ragdoll glue); `AttachmentState`; `Detach` and headless `SpawnModel` through the change buffer; no `Ensure`.
- **`Fenrir.Animation`:** mirrors `Animator` and `AnimationLayer` with `ClipLoop`, `LayerMode`, `AdditiveReference`, `FadeCurve`, `MaskScope` (so blending needs nothing more); `Seek`, `Playback(entity, out PlaybackInfo)` with fade fields; `AnimationEvent`, `ClipFinished`, `AnimationEventsDropped` through `EventReader<T>`; `ClipEventId.Of` in C# pinned to C++'s FNV-1a; `Clips` (duration, event reads, names into spans, `FindEvents`, `AddEvent`/`RemoveEvent` bumping the revision); `BoneMasks.Create/Include/Clear` as materials are edited.
- **Packaging:** `FenrirSkeletonScripting` and `FenrirAnimationScripting`, contracts under each package's `Managed/`; `SkinnedMesh` mirrored in `Fenrir.Renderer`. Physics's no-op rules.
- **Demo (ticket 34):** C# twins *Playback (C#)*, which also crossfades and adds one masked layer, and *Bone attachments (C#)*; no blending twin.
- **Tests for the spec:** layouts and `new` equals `{}` for each mirror; a C# `Modify` system interleaved between C++ modifiers by order; a stale `PoseEdit` writes nothing; an undeclared stage fails the load; structural changes from a stage system appear after the pipeline; a C# `Late` system reads this frame's bone; reads agree with C++; `ClipEventId.Of` matches C++; events read in C#; missing packages no-op.
- **Later** (*Not yet specified*): zero-copy pose spans if C# crowds show copying in a profile. No new tickets.

## Comments

- From ticket 26 (ADR-0031): `SkeletonPose` is a blittable `{ AssetHandle<SkeletonData> }` C# can mirror; the arrays live in `SkeletonPoses` and cross by span-filling functions, valid only within the current system.
- From ticket 27 (ADR-0033): the pose pipeline's public stages are `Animate`, `Modify` and `Late`, ordered by an `int` within each; how a C# system or modifier joins a stage, and how `PoseEdit` (valid only during one modifier call) and `SetOverride` cross to C#, are this ticket's.
- From ticket 28 (ADR-0034): C# mirrors the blittable `Animator`; `Animation::Seek` and `Animation::Playback` cross as native functions; `AnimationEvent`, `ClipFinished` and `AnimationEventsDropped` are plain C++ events read through event readers.

- From ticket 30 (ADR-0035): `SkinnedMesh { Entity skeleton; }` is a blittable component beside `MeshRenderer` that C# mirrors; its documentation says prominently that a skinned mesh is drawn with the skeleton entity's world matrix and its own `Transform` is ignored. `PoseGeneration` may be worth exposing for C# caches.

- From ticket 29 (ADR-0036): C# mirrors the blittable `AnimationLayer` and `Animator`'s new `fadeSeconds` and `fadeCurve`, with the `LayerMode`, `AdditiveReference` and `FadeCurve` enums; `BoneMask` is a FenrirAssets asset built by name and subtree, which C# creates and edits as it does materials; `Seek` and `Playback` take a layer entity too, `Playback` gains `fading` and `fadeProgress`, and `AnimationEvent` and `ClipFinished` gain `layer`.
- From ticket 31 (ADR-0037): C# mirrors the blittable `BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`; `BoneName` is a fixed 256-byte UTF-8 buffer whose C# `Set` rejects names over 255 bytes, as C++'s does. `Skeleton::AttachmentState(entity)` and the transactional `Skeleton::Detach(entity, DetachMode, newParent)` cross as native functions; `Detach` is structural, never inside the pose pipeline.

- From ticket 34: AnimationDemo has two C# twins on this extension: *Playback (C#)* (clip control, seeking, playback-state queries, clip events) and *Bone attachments (C#)* (creating and editing attachment components, querying bone transforms, observing attachment resolution states). Blending gets a C# twin only if this ticket exposes blending.
