# How do bone-following kinematic bodies get this frame's pose before the physics step?

Type: grilling
Status: open
Blocked by: 31, 32

## Question

A kinematic body on an entity placed by a `BoneAttachment` (a hit box, a held shield) reads its `Transform` at physics's fixed ticks, but Attach places it in `PostUpdate`, after the ticks, so the body lags a frame (ADR-0037). That lag is the first version's known limitation, not the intended behaviour. What synchronisation replaces it, with FenrirSkeleton and FenrirPhysics still unaware of each other?

- Where the pose a tick needs comes from: evaluating attachments (and the poses under them) before the ticks, a pose evaluated per tick, or interpolating between the last two evaluated poses at each tick's time.
- Who writes the kinematic target: glue reading `GetBoneWorldMatrix` and calling physics's kinematic move, rather than the attached `Transform`, and in which phase relative to `Tick` and `AfterTicks`.
- How this sits with ADR-0033's later fixed-tick animation (a simulation pose apart from the render pose) and with ticket 32's ragdoll glue, which reads bodies after the ticks.
- What it means for several ticks in one frame, and for none.
- Whether a dynamic body following a bone (through a joint or a spring, not a `BoneAttachment`) needs anything from this side.
- The physics map's share: hit boxes keyed by bone name belong with its ragdoll spec; what it is handed.

## Comments

- From ticket 31 (ADR-0037): the first version documents the one-frame lag of kinematic bodies on attached entities and the unsupported case of a dynamic body with a `BoneAttachment` (Attach in `PostUpdate` overwrites it). The synchronisation this ticket settles goes through glue, adding no dependency between FenrirSkeleton and FenrirPhysics.
