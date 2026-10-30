# 10: bone attachments place entities at bones under any parent, and models spawn their attachment points

**What to build:** a `BoneAttachment` on an entity places it at a bone every frame under any parent, attachments chain in dependency order, failures keep the last transform and report an `AttachmentState`, detaching keeps the local or world transform, and `SpawnSkeletons` attaches a model's ordinary nodes below bones, shown in the first part of AnimationDemo's scene 5 (spec: "Bone attachments", "Spawning" step 3; ADR-0037).

- **`BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`**, plain and blittable, placing its entity at `GetBoneWorldMatrix(skeleton, bone) × offset`. `BoneName` is a fixed 256-byte UTF-8 buffer holding at most 255 bytes, `Set` rejecting a longer name with a log and never truncating, an unterminated buffer diagnosed and never resolved.
- **Resolution** cached privately per attached entity (bone index plus the skeleton entity, name and rebuild it was resolved against), re-resolving by engine name on any change before the next placement; a missing bone retrying only when the name, skeleton or rebuild changes.
- **Placement** in the pipeline's Attach step: the target world matrix through the inverse parent world matrix decomposed to TRS with shear dropped; the fast path `M_model[bone] × offset` with the skeleton entity as parent; a cached topological order of dependencies (skeleton entities, parents and their ancestry), ties by entity index; cycles `Unplaceable`, logged once. Attach makes no structural change. While attached FenrirSkeleton owns the local `Transform`, and gameplay moves the object through `offset`.
- **`Skeletons::AttachmentState(entity)`** returning `Attached`, `SkeletonMissing`, `SkeletonUnavailable`, `BoneNotFound`, `TransformMissing` or `Unplaceable`, never a component field; failures keeping the last `Transform`, logged once per cause, recovering automatically.
- **`Skeletons::Detach(entity, DetachMode, newParent = {})`:** `KeepLocal` removing the component only; `KeepWorld` reparenting under `newParent` (the scene root when null) keeping the evaluated world transform, transactional with a failure result; structural, never inside the pipeline.
- **Spawning:** `SpawnSkeletons` step 3 giving every ordinary node below a bone a `BoneAttachment` to its nearest ancestor bone with its rest transform relative to that bone as the offset, so a named point is an ordinary node already attached and there is no socket concept.
- **Physics note** in the docs: a dynamic body with a `BoneAttachment` is unsupported; a kinematic one on a frame-posed skeleton lags a frame (ticket 11's tick-posed skeletons remove it).
- **Scene 5, Bone attachments (first part)**, in AnimationDemo with its README row and hand checks: attachments on the humanoid (a prop in a hand, a hat), an attachment on an attachment, and the failure cases each labelled with its `AttachmentState`; detach with both modes on a key. The tick-posed shield is ticket 11's addition to this scene.

**Blocked by:** 05

**Touches:** `packages/FenrirSkeleton/`, `examples/AnimationDemo/src/`, `examples/AnimationDemo/README.md`, `GLOSSARY.md`

**Status:** resolved

- [x] Seam 8 attachment cases: placement under the skeleton and under other parents; topological order with an attachment on an attachment and a skeleton under an attached entity; cycles `Unplaceable`; every `AttachmentState`; keep-last-transform and recovery; name changes and rebuilds re-resolving; over-long and unterminated names; transactional `Detach` in both modes.
- [x] Seam 8 spawning: on a fixture with ordinary nodes below bones, `SpawnSkeletons` attaches each to its nearest ancestor bone with the rest offset, and the attached node's world transform matches its rest world transform.
- [x] Scene 5 is in the scene list and README with keys and hand checks; smoke runs still pass with the failure cases as warnings.
- [ ] Hand check (left to the user as a `Hand check:` line on resolve): the prop follows the hand through every clip, the scope follows the prop, each failure label names its state, and a `KeepWorld` detach leaves the prop where it was.
Hand check: in scene Bone attachments (`FENRIR_DEMO_SCENE="Bone attachments"`, on OpenGL), the rifle stays in the human's right hand through every clip C steps to, the scope stays on the rifle and the hat on the head, each labelled `Attached`; the five failure labels name `SkeletonMissing`, `SkeletonUnavailable`, `BoneNotFound`, `TransformMissing` and `Unplaceable`; W leaves the rifle exactly where it was with the scope on it, K leaves the hat where the head was, and R puts both back.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

## Comments

Built 2026-10-30 on `claude/animation-09-pose-override` (ticket 09, PR #245, not merged yet), as a stacked PR. Decisions to review:

- **Names:** the state read is `Skeletons::GetAttachmentState(scene, entity)`, as `GetPoseState` is, since a member function named `AttachmentState` would hide the enum of that name inside `Skeletons`. `Detach(scene, entity, mode, newParent)` is static too, taking the scene like the reads, and returns a `DetachResult` (`Detached`, `NotAttached`, `InsidePipeline`, `InvalidParent`, `ParentNotInvertible`, `NotRepresentable`).
- **`AttachmentState::NotAttached`**, a seventh value, for an entity with no `BoneAttachment` or one no Attach has seen yet: the spec's six describe a placement, and a read needs an answer before the first.
- **`BoneName::Set(name, logger)`** takes the logger it reports a refusal to, since a plain component has no other; it also refuses a name holding a zero byte, which would otherwise end early. `Get()` is `std::optional<std::string_view>`, empty for an unterminated buffer.
- **The cache** lives in a private `Detail::Attachments` inside `SkeletonPoses`, so it goes with the scene's instances: per attached entity the bone index and the skeleton entity, name and instance build (`SkeletonInstance::built`, the generation at its last rebuild) it was resolved against. Records of dead entities or removed components are swept each Attach.
- **The order's cache key** is the dependency signature itself: each attachment's id with its ancestors', its skeleton entity's and that one's ancestors'. Comparing it each frame costs a walk up each chain, which is what detecting a hierarchy change needs without hooks; the topological sort runs only when it changes.
- **Cycles:** Kahn's order with the lowest entity index first; when it stalls, an attachment that reaches itself is in a cycle, `Unplaceable`, and released so what depends on it is still placed (with the cycle's last transforms).
- **Logging:** a warning when an attachment's cause changes, once per cause until it is next placed, so a held failure never floods the log; a malformed name is a cause of its own (`BoneNotFound`). A missing inverse is logged the same way rather than every 300 frames.
- **Spawning attaches only nodes whose parent node is a bone.** A node below such a node is its child entity already and follows it, so it gets no attachment; the test checks both. A node whose skeleton did not load is left where `SpawnNodes` put it.
- **Scene 5's props are procedural** (cubes and spheres from the asset root), not downloaded: ActionForge's `props/*.glb` state no licence in its manifest that could be checked. The rifle is a skeleton entity with a two-bone skeleton made in code, so the scope is a true attachment on an attachment. The scene is fifth, before Going limp.
