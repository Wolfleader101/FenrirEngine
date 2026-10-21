# How are poses blended?

Type: grilling
Status: open
Blocked by: 27, 28

## Question

How much blending does the first animation spec have, and how does it fit ADR-0028's base pose and ordered modifiers?

- Crossfades between clips: who starts one, over what time, and with what curve.
- Layers: weighted and additive, in order; masks by bone (by name, by subtree, per-bone weights).
- Whether layers and masks belong to FenrirAnimation's base pose or become pose modifiers (ADR-0028 lists layers and masks among modifiers to come).
- Whether a blend tree or graph (blend spaces, state machines) is in this spec or stays in *Richer animation*.
- Weights that sum below one: fall back to the rest pose, as ozz does, or normalise.

## Comments

- From ticket 27 (ADR-0033): a modifier is `Skeleton::AddModifier(name, order, fn)` writing `Target`s (local/model/world, channel mask, weight) through `PoseEdit`, composed sequentially in `Modify`; layers and masks may be base-pose blending inside `Animate` or modifiers on this interface.
