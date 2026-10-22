# How do bone attachments behave?

Type: grilling
Status: resolved
Blocked by: 27

## Question

A `BoneAttachment` names a skeleton entity, a bone by name and an offset, and finalisation owns the attached entity's resulting `Transform` (ADR-0028). How exactly?

- The component's fields and how C# edits them; the offset as the user-editable transform.
- The attached entity's ECS parent: required to be the skeleton entity, or any entity, with finalisation converting the bone's model-space transform into the parent's local space.
- Ordering: attachments placed in finalisation, after modifiers; attachments on attachments.
- Failure cases: a bone name that does not resolve, the skeleton entity despawned, the attached entity despawned, the skeleton asset reloaded with different bones; logging once, as stale handles are.
- Sockets: whether named attachment points beyond bones (non-bone nodes kept by `SpawnNodes`) need anything more.

## Answer

ADR: `docs/adr/0037-bone-attachments-are-plain-components-placed-in-dependency-order-under-any-parent.md` (refines ADR-0028's bone attachment line and ADR-0031's engine names). Glossary: Bone attachment revised; Attachment state added.

- **Facts found:** FenrirScene computes world transforms and matrices on read and never stores them (`WorldTransform.hpp`), so descendants of an attached entity follow it with no work; `Name` is a fixed `char[256]`, blittable for C#.
- **Component:** `BoneAttachment { Entity skeleton; BoneName bone; Transform offset; }`, plain and blittable; the full bone transform is inherited, no channel masks or weights. `BoneName` is 256 bytes, at most 255 UTF-8 bytes; `Set` (C++ and C#) rejects longer names with a log rather than truncating; unterminated buffers are diagnosed, never resolved partially. Imported source names over 255 bytes get a shortened, suffixed, unique engine name, the source name and identity kept.
- **Resolution:** a private FenrirSkeleton cache per attached entity holds the bone index and what it was resolved against (skeleton entity, name, instance rebuild); any change re-resolves by engine name before the next placement, so code may edit fields freely.
- **State:** `Skeleton::AttachmentState(entity)`: `Attached`, `SkeletonMissing`, `SkeletonUnavailable`, `BoneNotFound`, `TransformMissing`, `Unplaceable` (non-invertible matrix, or a dependency cycle); a native function for C#, never a component field.
- **Placement:** any ECS parent; Attach takes `skeletonWorld × M_model[bone] × offset` into the parent's space by its inverse world matrix and decomposes to TRS, shear dropped (documented); the skeleton entity as parent is the fast path `M_model[bone] × offset`.
- **Order:** one Attach pass in topological order of dependencies (an attachment depends on another whose entity is, or is an ancestor of, its skeleton entity or parent, explicit or through ordinary ancestry), ties by entity index, deterministic for one configuration and spawn order; cached and rebuilt on attachment or hierarchy changes; cycles, direct or indirect, make their attachments `Unplaceable`, logged once.
- **Failure:** keep the last `Transform`, report the state, log once per cause, recover automatically; missing bones retry only when name, skeleton or rebuild changes; despawned skeletons leave the component and entity in place; rebuilt skeletons invalidate every cached index; non-invertible matrices are skipped with rate-limited warnings; Attach makes no structural changes.
- **Ownership:** while attached, FenrirSkeleton owns the entity's local `Transform`; gameplay moves it through `offset`; direct writes are overwritten at the next Attach.
- **Detaching:** removing the component keeps the local `Transform` and parent. `Skeleton::Detach(entity, DetachMode, newParent = {})`: `KeepLocal` removes the component only; `KeepWorld` reparents under `newParent` (scene root when null), preserving the current evaluated world transform, not one this frame has yet to evaluate. It is transactional: the new parent (no hierarchy cycle), the inverse and a TRS-representable result are validated before anything changes, and a failure changes nothing and returns a failure result. Structural, so never inside the pose pipeline.
- **Named attachment points:** no socket concept; an ordinary node kept by `SpawnNodes` is already attached to its bone, and objects hang from it by ordinary ECS parenting.
- **Physics:** no dependency either way. A dynamic body with a `BoneAttachment` is unsupported (Attach in `PostUpdate` overwrites it); a kinematic one lags a frame, a known first-version limitation, not the design.
- **Graduated:** ticket 62, "How do bone-following kinematic bodies get this frame's pose before the physics step?" (blocked by 31 and 32). Note appended to ticket 33.

## Comments

- From ticket 26 (ADR-0031): `SpawnSkeletons` gives every ordinary node below a bone a `BoneAttachment` to its nearest ancestor bone, offset its rest transform relative to it, after `Ensure`; while its skeleton is `Unavailable` an attachment keeps its last transform but must expose that it is unresolved, and re-resolves on recovery before its next update.
- From ticket 27 (ADR-0033): attachments are placed in the pipeline's internal Attach step, after Finalise and before the public `Late` stage, every frame (a skeleton entity move changes no model matrix but does move attachments).
