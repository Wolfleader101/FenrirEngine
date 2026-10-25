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

**Status:** ready-for-agent

- [ ] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; `Fenrir.Math/Generated/` holds `Colour.cs`, `Ray.cs`, `Bounds.cs`; `Fenrir.Physics/Generated/Aabb.cs` is gone.
- [ ] The generated math defaults test runs in the FenrirScripting test target: `new Colour()` is `Colour{}` (1, 1, 1, 1), `new Ray()` and `new Bounds()` are their C++ defaults.
- [ ] Physics scripting tests pass with `Bounds`; the demo scripts build; nothing else a script wrote changes name.
- [ ] `grep` finds no `Mat4View`, `BoundsView` or `Aabb` outside history.
