# What world transform does the engine define?

Type: grilling
Status: resolved
Blocked by: none

## Question

`Transform` is local to its parent through `Relationship`, but the engine has no correct world transform: `GLRenderer::DrawModel` (`examples/DemoApp/src/GLRenderer.cpp`) adds the parent's position without rotating the child's offset, and looks only one level up. Physics simulates in world space and needs one definition ("How is physics modelled in the ECS?").

- Where does it live: FenrirECS (beside `Transform` and `Relationship`), or FenrirCore?
- Computed on demand (walk the parent chain each call), or cached in a component (Bevy's `GlobalTransform`, Godot's global transform) and propagated once per frame or tick? If cached, which system updates it, and when relative to `Tick` and rendering?
- How does it handle scale through rotated parents (shear cannot be represented by position, rotation and scale)?
- Setting a world transform: the inverse operation physics needs for write-back (`local = parentWorld⁻¹ × world`).
- Moving the DemoApp renderer onto it.

## Answer

- **Computed on demand**, never cached in a component: each call walks the parent chain through `Relationship`. Always current (a C# system reads its own edits at once), no propagation system to order against `Tick`, physics write-back, C# edits and rendering. Callers use functions only, so a private cache can be added behind them later if profiling asks, with no caller changing. Rejected: a cached `WorldTransform` component with a propagation system (Bevy's `GlobalTransform`), stale between an edit and the next propagation.
- **Two forms:**
  - `GetWorldMatrix(entity)`: the exact product of the local matrices up the chain, shear included; for rendering.
  - `GetWorldTransform(entity)`: position, rotation, scale composed as `pos = parentPos + parentRot·(parentScale ⊙ localPos)`, `rot = parentRot·localRot`, `scale = parentScale ⊙ localScale` (component-wise, lossy under rotation, as Unity's lossy scale); for physics, gameplay and C#.
  They agree except under shear (non-uniform parent scale with a rotated child), where a collider can differ from its rendered mesh; documented.
- **`SetWorldTransform(entity, world)`** sets the local transform from a desired world one, the inverse of the composition: `local.pos = parentRot⁻¹·(world.pos − parentPos) ⊘ parentScale`, `local.rot = parentRot⁻¹·world.rot`, `local.scale = world.scale ⊘ parentScale`; with no parent it sets `Transform`. Physics write-back uses it.
- **Lives in FenrirECS** beside `Transform` and `Relationship`, as free functions, tested in `FenrirECSTests` (deep chains, rotated parents, non-uniform scale).
- **DemoApp, in the foundation spec beside the FenrirApp fixes:** the renderer draws with `GetWorldMatrix` (fixing today's one-level, unrotated composition); the editor gizmo shows and edits in world space through `GetWorldTransform` / `SetWorldTransform`.
- **C#:** `entity.WorldTransform` and `entity.SetWorldTransform(t)` as native functions in scripting's built-in extension.
