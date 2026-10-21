# How do bone attachments behave?

Type: grilling
Status: open
Blocked by: 27

## Question

A `BoneAttachment` names a skeleton entity, a bone by name and an offset, and finalisation owns the attached entity's resulting `Transform` (ADR-0028). How exactly?

- The component's fields and how C# edits them; the offset as the user-editable transform.
- The attached entity's ECS parent: required to be the skeleton entity, or any entity, with finalisation converting the bone's model-space transform into the parent's local space.
- Ordering: attachments placed in finalisation, after modifiers; attachments on attachments.
- Failure cases: a bone name that does not resolve, the skeleton entity despawned, the attached entity despawned, the skeleton asset reloaded with different bones; logging once, as stale handles are.
- Sockets: whether named attachment points beyond bones (non-bone nodes kept by `SpawnNodes`) need anything more.
