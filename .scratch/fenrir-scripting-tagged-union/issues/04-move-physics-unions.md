# 04: `CollisionShape` and `JointKind` on the `[TaggedUnion]` generator

**What to build:** FenrirPhysics's two hand-written unions move onto the generator, so there is one way to write a union and the existing tests prove the surface is the same (ticket 01's Answer, decisions 1, 6 and 7; "What does C# get for vehicles?", decision 1).

- **`CollisionShape`** (`packages/FenrirPhysics/Managed/Fenrir.Physics/CollisionShape.cs`): `[TaggedUnion(Noun = "shape")] partial`, keeping its enum, `_type` at 0 and its kinds at 8; every member it writes by hand goes, including the constructor. **`_hull` is renamed `_convexHull`**, with the one native string in `PhysicsScripting.cpp` changed to `IRIS_FIELD_AS(Layout, hull, "_convexHull")`. The stand-in's C++ member stays `hull`. Its doc comment keeps saying what each view does and that, new, it is a unit box.
- **`JointKind`** (`Joints.cs`): `[TaggedUnion(Noun = "joint")] partial`, keeping its enum, `_type` at 0 and its kinds at 4; every hand-written member goes. No native change.
- No other C# or C++ caller changes.

**Blocked by:** 02 (`[TaggedUnion]` tracer bullet)

**Status:** resolved (built on branch `claude/tagged-union-physics`, on top of 02's). ADR-0011's 2026-10-16 amendment, which said the pattern is written by hand, gains a second amendment. The conversions' parameter is now `value` for every kind, `JointKind`'s `fixedJoint` included; only a call naming the argument could notice.

- [x] Every existing `PhysicsScriptingTests` case passes unchanged, including the layout validation of `Collider` and `Joint`, the exact message "The shape is a Box, not a Sphere.", and the joint test that `new JointKind()` equals C++'s default.
- [x] `CollisionShape.cs` and `JointKind` hold no hand-written view, conversion, `Type`, constructor or `Expect`.
