# 03: `Fenrir.Math` through a math binding, and physics's `Aabb` becomes `Bounds`

**What to build:** `Colour`, `Ray` and `Bounds` are generated mirrors of FenrirMath's description; the glm five stay bound types; `Math::AABB` has one C# type (spec: "`Fenrir.Math`"; ADR-0043).

- **Selection:** `packages/FenrirMath/api/FenrirMath.api.toml` adds `Fenrir::Colour`, `Fenrir::Math::Ray`, `Fenrir::Math::AABB` as `shared-struct` (headers `Colour.hpp` and `Math.hpp`) and `Fenrir::Math::Vec2`, `Vec4`, `Mat4` as `bound-type`. Regenerate `FenrirMath.api.json`.
- **Math binding:** `packages/FenrirScripting/api/FenrirMath.binding.toml`: namespace `Fenrir.Math`; contract `../../FenrirMath/Managed/Fenrir.Math`, generated `…/Generated`; `[cpp] mappings` into `packages/FenrirScripting/include/FenrirScripting/Generated/FenrirMathMappings.hpp` including `FenrirScripting/BoundMappings.hpp`; `[bound-types]` the glm five (`Vector2`, `Vector3`, `Vector4`, `Quaternion`, `Matrix4x4`); `[names] "Fenrir::Math::AABB" = "Bounds"`, `"Fenrir::Math::Ray::dir" = "Direction"`; no `[registration]` or `[natives]`; `[tests]` through the built-in binding's fixture (`FixtureApp`), class `Fenrir.ScriptFixture.MathDefaults`. `iris_generate_binding(FenrirMath ...)` in FenrirScripting's CMake.
- **Hand-written parts:** `Colour.cs`, `Ray.cs`, `Bounds.cs` lose their fields and keep operators, constructors and conversions as `partial` parts; the generator checks no field remains. `Vector2/3/4`, `Quaternion`, `Matrix4x4` are untouched.
- **`BoundMappings.hpp`:** keeps the glm five; `Mat4View` is replaced by an `Iris::StructMapping<Fenrir::Math::Mat4>` giving `Column0..3` as `Iris::Field<glm::vec4>` at offsets 0, 16, 32, 48 (a `static_assert` on size); `Colour`, `Ray` and `BoundsView` mappings go. `FenrirScriptingMappings.hpp` includes `FenrirMathMappings.hpp`.
- **Bindings use it:** `FenrirScripting.binding.toml` and `FenrirPhysics.binding.toml` add `[uses]` naming the math binding. Physics drops `[names] "Fenrir::Math::AABB" = "Aabb"`; regenerating deletes `Fenrir.Physics/Generated/Aabb.cs`; `Aabb` becomes `Bounds` in `Fenrir.Physics` (`PhysicsWorld`, docs), its fixture, compile-fail files and the demo scripts (`examples/`).
- **Docs:** `FenrirScripting/README.md`'s math section; `GLOSSARY.md`'s *Math contract* is already updated by ADR-0043.

**Blocked by:** 02 (describe-api and Iris.Bindings learn text, resources, internal mirrors and shared bindings)

**Touches:** `packages/FenrirMath/api/`, `packages/FenrirMath/Managed/Fenrir.Math/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/include/FenrirScripting/BoundMappings.hpp`, `packages/FenrirScripting/include/FenrirScripting/Generated/`, `packages/FenrirScripting/CMakeLists.txt`, `packages/FenrirScripting/Tests/`, `packages/FenrirScripting/README.md`, `packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml`, `packages/FenrirPhysics/Managed/Fenrir.Physics/`, `packages/FenrirPhysics/Scripting/Tests/`, `examples/`

**Status:** resolved

- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; `Fenrir.Math/Generated/` holds `Colour.cs`, `Ray.cs`, `Bounds.cs`; `Fenrir.Physics/Generated/Aabb.cs` is gone.
- [x] The generated math defaults test runs in the FenrirScripting test target: `new Colour()` is `Colour{}` (1, 1, 1, 1), `new Ray()` and `new Bounds()` are their C++ defaults.
- [x] Physics scripting tests pass with `Bounds`; the demo scripts build; nothing else a script wrote changes name.
- [x] `grep` finds no `Mat4View`, `BoundsView` or `Aabb` outside history.

## Comments

**`Math::Ray`'s defaults moved into member initialisers** (`packages/FenrirMath/include/FenrirMath/Math.hpp`, outside the
Touches line). Its default constructor set `dir` in its initialiser list, which describe-api cannot read, so the
description recorded a zero direction and the generated defaults test (rightly) would have failed against C++'s
(0, 0, 1). It now has `origin{}` and `dir = Vec3(0.0f, 0.0f, 1.0f)` and `Ray() = default;`: the same value, and a
`Ray{}` now value-initialises `origin` (before, a default-initialised ray left it indeterminate). Its two member comments
became leading `/** */` comments, since describe-api keeps a trailing `///<`'s `<`.

**The built-in binding no longer binds FenrirMath's description.** It held FenrirMath only for the vector and
quaternion, which FenrirECS's description already records as bound types; with FenrirMath now selecting `Colour`, `Ray`
and `AABB`, they are the math binding's, which the built-in names through `uses`. Its `iris_generate_binding` passes
FenrirECS's description alone.

**A used type may be selected with the same role (Iris.Bindings, outside the Touches line).** Ticket 02 refused any
selected role on a type a used binding mirrors. FenrirPhysics selects `Math::AABB` as a shared struct, because a native
fills one through a pointer and the natives binding needs it described, and FenrirMath's description selects it the
same way. The check now fails only when the two descriptions give it different roles (the fixture's case is a
component against a shared struct). Physics no longer registers `AABB` (the math binding has no registration, and a used
type is never registered here), so its layout is checked by the math defaults test and the mapping's `static_assert`
rather than at script load.

**`Mat4View` became a mapping of `Math::Mat4` itself**, its columns as `Iris::Field<glm::vec4>` at 0, 16, 32 and 48,
with a `static_assert` that it is four packed `vec4`s. `BoundMappings.hpp` no longer includes `FenrirMath/Colour.hpp`;
the generated `FenrirMathMappings.hpp` does, and every extension reaches it through `FenrirScriptingMappings.hpp`.

**FenrirRenderer's `Shapes.cpp` test** (outside the Touches line) named `Detail::BoundsView`; it checks `Math::AABB`
against `Bounds` now, the one change needed there.
