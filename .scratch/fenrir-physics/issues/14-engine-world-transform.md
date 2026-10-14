# What world transform does the engine define?

Type: grilling
Status: open
Blocked by: none

## Question

`Transform` is local to its parent through `Relationship`, but the engine has no correct world transform: `GLRenderer::DrawModel` (`examples/DemoApp/src/GLRenderer.cpp`) adds the parent's position without rotating the child's offset, and looks only one level up. Physics simulates in world space and needs one definition ("How is physics modelled in the ECS?").

- Where does it live: FenrirECS (beside `Transform` and `Relationship`), or FenrirCore?
- Computed on demand (walk the parent chain each call), or cached in a component (Bevy's `GlobalTransform`, Godot's global transform) and propagated once per frame or tick? If cached, which system updates it, and when relative to `Tick` and rendering?
- How does it handle scale through rotated parents (shear cannot be represented by position, rotation and scale)?
- Setting a world transform: the inverse operation physics needs for write-back (`local = parentWorld⁻¹ × world`).
- Moving the DemoApp renderer onto it.
