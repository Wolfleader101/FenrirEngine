# What are the pose stages, and how do pose modifiers plug in?

Type: grilling
Status: claimed
Blocked by: 26

## Question

ADR-0028 orders each frame's pose work as base pose (animation), then modifiers, then finalisation, all in `PostUpdate` for now, and lets modifiers aim in local, model or world space. How is that built so later stages need no redesign?

- How the stages are named and ordered against other packages' systems (FenrirScheduler's ordering, or sub-phases of `PostUpdate`), and what game code may write in each.
- The modifier interface: how a modifier supplies a target in local, model or world space with a weight, and how finalisation resolves the targets into final local transforms in order.
- The first implementation's single weighted per-bone override: its shape, and how it is documented as initial rather than the contract.
- Finalisation's steps and their cost: model-space matrices parents-first, the palette, attachments; skipping the palette for skeletons nothing skins.
- What would move for fixed-tick animation, IK between animation and skinning, and physics interpolation (listed, not built).
