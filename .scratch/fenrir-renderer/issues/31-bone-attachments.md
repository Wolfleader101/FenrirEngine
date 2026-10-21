# How do bone attachments behave?

Type: grilling
Status: claimed
Blocked by: 27

## Question

A `BoneAttachment` names a skeleton entity, a bone by name and an offset, and finalisation owns the attached entity's resulting `Transform` (ADR-0028). How exactly?

- The component's fields and how C# edits them; the offset as the user-editable transform.
- The attached entity's ECS parent: required to be the skeleton entity, or any entity, with finalisation converting the bone's model-space transform into the parent's local space.
- Ordering: attachments placed in finalisation, after modifiers; attachments on attachments.
- Failure cases: a bone name that does not resolve, the skeleton entity despawned, the attached entity despawned, the skeleton asset reloaded with different bones; logging once, as stale handles are.
- Sockets: whether named attachment points beyond bones (non-bone nodes kept by `SpawnNodes`) need anything more.

## Comments

- From ticket 26 (ADR-0031): `SpawnSkeletons` gives every ordinary node below a bone a `BoneAttachment` to its nearest ancestor bone, offset its rest transform relative to it, after `Ensure`; while its skeleton is `Unavailable` an attachment keeps its last transform but must expose that it is unresolved, and re-resolves on recovery before its next update.
- From ticket 27 (ADR-0033): attachments are placed in the pipeline's internal Attach step, after Finalise and before the public `Late` stage, every frame (a skeleton entity move changes no model matrix but does move attachments).
