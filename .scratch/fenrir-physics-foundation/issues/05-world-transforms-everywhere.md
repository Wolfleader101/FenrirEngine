# 05: World transforms everywhere

**What to build:** The engine has one correct world transform, and the DemoApp and C# use it (spec: "FenrirECS", "DemoApp", "FenrirScripting"; glossary: world transform). FenrirECS gains free functions computed on demand by walking `Relationship`: `GetWorldMatrix` (exact, shear included), `GetWorldTransform` (position, rotation, component-wise scale) and `SetWorldTransform` (the inverse). The DemoApp renderer draws children at their correct world transform (today it adds the parent's position without rotating the child's offset, one level up only), the editor gizmo works in world space, and C# gets `entity.WorldTransform` / `entity.SetWorldTransform(t)` as built-in native functions.

**Blocked by:** None (can start immediately).

**Status:** ready-for-agent

- [ ] `FenrirECSTests` pin: no parent returns the local transform; deep chains compose; a rotated parent rotates the child's offset; non-uniform scale composes component-wise; the matrix and the decomposed form agree except under shear (documented); `SetWorldTransform` then `GetWorldTransform` round-trips under rotated, scaled parents.
- [ ] Composition follows the spec: `pos = parentPos + parentRot·(parentScale ⊙ localPos)`, `rot = parentRot·localRot`, `scale = parentScale ⊙ localScale`.
- [ ] The DemoApp renderer draws with the world matrix; a child under a rotated parent renders in the right place (checked by hand).
- [ ] Dragging a child with the editor gizmo moves it where it is dragged (checked by hand).
- [ ] A scripting test reads and sets a child's world transform from C# and sees its own edit in the same system.
