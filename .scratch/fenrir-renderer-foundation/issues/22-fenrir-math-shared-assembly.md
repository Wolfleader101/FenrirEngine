# 22: The Fenrir.Math shared assembly

**What to build:** C# extensions share one pure managed maths contract laid out as FenrirMath's types, so every package's contract speaks the same vectors and colours (spec: "Scripting: the FenrirScripting prerequisite" (`Fenrir.Math`), "Testing Decisions" seam 7 (`Fenrir.Math` layouts); ADR-0029).

- **`packages/FenrirMath/Managed/Fenrir.Math`** (namespace `Fenrir.Math`): `Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`, `Colour`, `Ray` and `Bounds`, each blittable and laid out as its FenrirMath counterpart (`Colour` as ticket 01's `Fenrir::Colour`, `Bounds` as `Math::AABB`).
- `Vector3` and `Quaternion` move there from `Fenrir.Scripting`; `Transform` stays in `Fenrir.Scripting`.
- The built-in extension shares it first; physics's contract (`Fenrir.Physics`) moves onto it. Scripts in tests and the legacy DemoApp follow the namespace change.
- Layout checks for each type, in the way existing component mirrors are checked.

**Blocked by:** 01 (Engine prerequisites).

**Touches:** `packages/FenrirMath/Managed/`, `packages/FenrirScripting/Managed/`, `packages/FenrirScripting/Tests/`, `packages/FenrirPhysics/Managed/`, `packages/FenrirPhysics/Scripting/`, `examples/DemoApp/scripts/`, `FenrirScripting.sln`

**Status:** resolved

- [x] Scripting harness tests check each `Fenrir.Math` type's size and field offsets against its C++ counterpart.
- [x] `Transform`'s C# mirror uses `Fenrir.Math.Vector3` and `Quaternion` and still matches the C++ layout.
- [x] Physics scripting tests pass on the moved contract.
- [x] The legacy DemoApp's scripts still build with `FENRIR_BUILD_LEGACY_DEMOAPP=ON` and scripting on.
