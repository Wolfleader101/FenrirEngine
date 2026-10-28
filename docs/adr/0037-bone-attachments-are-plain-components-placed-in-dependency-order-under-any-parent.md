---
status: accepted
---

# Bone attachments are plain components, placed in dependency order under any parent

ADR-0028 makes a `BoneAttachment` a component naming a skeleton entity, a bone by name and an offset, with finalisation owning the attached entity's `Transform`. ADR-0031 has `SpawnSkeletons` attach every ordinary node below a bone, and has attachments report themselves unresolved while their skeleton is `Unavailable`. ADR-0033 places them in the pose pipeline's internal Attach step, after Finalise and before `Late`, every frame. This ADR settles the rest (renderer map ticket 31). FenrirScene computes world transforms when they are read and never stores them, so an attachment placed in Attach carries its ECS descendants with it at no extra cost.

## The component

- **`BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`**, plain and blittable, mirrored by C#. `offset` is the user-editable attachment transform: the entity is placed at `GetBoneWorldMatrix(skeleton, bone) × offset`. The full bone transform is inherited (translation, rotation and scale); there are no channel masks or weights yet.
- **`BoneName`** is a fixed 256-byte buffer, like `Name`, holding at most 255 UTF-8 bytes and a terminator. `Set`, in C++ and C#, rejects a longer name with a log and leaves the field unchanged; it never truncates, since a truncated name could match another bone. A buffer written raw without a terminator is malformed: it is diagnosed and never resolved as a partial name.
- **Resolution lives outside the component.** FenrirSkeleton keeps a private cache per attached entity: the resolved bone index and what it was resolved against (skeleton entity, bone name, the instance's rebuild). The index is re-resolved by engine name whenever any of these changes, before the attachment is next placed. Code, including C#, may edit any field at any time without invalidating anything.

## State

`Skeleton::AttachmentState(entity)` reports, and C# reads it through a native function:

- `Attached`: placed this frame.
- `SkeletonMissing`: the skeleton entity is dead or has no `SkeletonPose`.
- `SkeletonUnavailable`: its instance is `Unavailable` (ADR-0031).
- `BoneNotFound`: no bone has that engine name, or the name is malformed.
- `TransformMissing`: the attached entity has no `Transform`. A resolved bone is not `Attached` unless the entity can be updated.
- `Unplaceable`: a needed inverse does not exist (a degenerate parent or skeleton world matrix), or the attachment is in a dependency cycle.

The state is never a field on the component, which stays the user's data.

## Placement

- **Any ECS parent.** Attach computes the target world matrix, takes it into the parent's space by the inverse of the parent's world matrix, and decomposes the result into the entity's local TRS `Transform`. When the parent is the skeleton entity, the default `SpawnSkeletons` creates, this reduces to `M_model[bone] × offset` with no inverse.
- **Shear is dropped.** Decomposing to TRS discards shear, so under non-uniform scale with rotation in the parent chain or the bone, the placed entity's world matrix can differ from the requested one. This is the rule ADR-0033 applies to targets.
- **Dependency order.** Model matrices never depend on world placement, so one Attach pass suffices if it runs in dependency order. Attachment B depends on attachment A when A's entity is B's skeleton entity or B's parent, or an ancestor of either; dependencies through ordinary ECS ancestry count as much as explicit references. Attachments run in a topological order of that graph, ties broken by entity index, which is deterministic for one application configuration and spawn order. The order is cached and rebuilt when an attachment's skeleton entity changes, an attachment is added or removed, or the hierarchy changes. Plain descendants of an attached entity need no place in the order.
- **Cycles** (a sword attached to a bone of a skeleton parented under the sword), direct or through ancestry, make every attachment in the cycle `Unplaceable`, logged once.

## Failure

One rule: **keep the last `Transform`, report the state, log once per cause, recover automatically.**

- An unresolved or unplaceable attachment leaves its entity's `Transform` exactly as it was.
- A bone name that does not resolve is retried when the name, the skeleton entity or the instance's rebuild changes, not every frame.
- A despawned skeleton entity, or a removed `SkeletonPose`, leaves the `BoneAttachment` and its entity in place, `SkeletonMissing`. A despawned attached entity has its cache entry swept.
- A skeleton rebuilt from a reloaded asset with different bones invalidates every cached index against it; each re-resolves by name before it is next placed, or becomes `BoneNotFound`.
- A non-invertible matrix gives `Unplaceable` for that frame, with a rate-limited warning as in ADR-0033.
- Attach makes no structural change: it never adds a missing `Transform` or removes a component.

## Ownership of the Transform

While an entity has a `BoneAttachment`, FenrirSkeleton owns its local `Transform`. Gameplay repositions the object relative to its bone through `offset`. A direct write to the `Transform` (in `Update`, say) is overwritten at the next Attach. There is no wider transform-authority system.

## Detaching

- **Removing the component** detaches and keeps the local `Transform` and parent, as any component removal does; the entity then follows its parent but no longer the bone. No removal hooks are involved.
- **`Skeleton::Detach(entity, DetachMode, Entity newParent = {})`:**
  - `KeepLocal` removes the component and changes nothing else.
  - `KeepWorld` reparents the entity under `newParent`, or the scene root when it is null, with a local `Transform` that preserves its current evaluated world transform: the one in the scene now, not a pose this frame has not yet evaluated. Detaching in `Update` therefore keeps where the entity was placed by the last Attach.
  - `KeepWorld` is transactional. It validates the new parent (alive, and not the entity or one of its descendants), computes the new local transform (the new parent's world matrix must be invertible and the result representable as TRS within tolerance) and only then removes the component and reparents. On any failure it changes nothing, the parent and the component included, and returns a failure result.
  - It is a structural change, allowed outside the pose pipeline and never inside it.

## Named attachment points

There is no socket concept. A named attachment point is an ordinary model node that `SpawnNodes` keeps and `SpawnSkeletons` attaches to its bone. To hang a sword on `hand_r_grip`, parent the sword under that node's entity with ordinary ECS parenting; the sword needs no `BoneAttachment`.

## Physics

FenrirSkeleton and FenrirPhysics stay unaware of each other.

- A dynamic body and a `BoneAttachment` on one entity are unsupported: Attach runs in `PostUpdate` and overwrites what the physics step wrote.
- A kinematic body on an attached entity reads, at the next tick, a `Transform` placed after the previous frame's ticks, so it lags a frame. That is a known limitation of the first version, not the intended behaviour: bone-following kinematic bodies need explicit synchronisation before the step that uses them, through glue depending on neither package's internals. Renderer map ticket 62 settles how.

## Considered options

- **The resolved bone index cached in the component.** Saves a lookup, but C# or game code renaming the bone would leave it stale. Rejected.
- **The parent required to be the skeleton entity.** Simpler placement with no inverse, but forbids re-parenting attached objects into other hierarchies. Rejected; the skeleton parent is a fast path instead.
- **Attachments placed in hierarchy-depth or entity order.** Wrong when a skeleton entity sits in another branch under an attached entity. Rejected for a dependency graph.
- **Truncating long bone names.** A truncated name could silently resolve to a different bone. Rejected.
- **Channel rules on attachments** (ignore the bone's scale, as Unreal's attachment rules can). Not now.
- **A `KeepWorld` detach without reparenting.** Removing the component already keeps the world transform at that moment; keeping it afterwards needs a new parent. Folded into `Detach`.

## Consequences

- *Refines ADR-0028's bone attachment line* (any parent, resolution outside the component, placement in dependency order) and *ADR-0031's engine names*: a source name over 255 UTF-8 bytes gets an engine name shortened at a UTF-8 character boundary and suffixed to stay unique, with a diagnostic, while the source name and identity are kept unchanged, so every bone remains attachable by engine name.
- Ticket 33 mirrors `BoneAttachment` and `BoneName` (with its rejecting `Set`) in C#, and crosses `AttachmentState` and `Detach` as native functions.
- Ticket 62 (kinematic bodies following bones) builds on this. *Resolved by ADR-0040* for tick-posed skeletons: glue writes a bone-following kinematic body's `Transform` in `Tick` from the simulation pose, with no `BoneAttachment` on the body. Frame-posed skeletons keep the lag recorded above.
