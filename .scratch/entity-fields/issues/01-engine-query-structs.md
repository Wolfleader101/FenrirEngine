# 01: Physics and DebugDraw hold the engine's `Entity`, not an `EntityValue`

**What to build:** the structs scripts receive from physics queries and pass to DebugDraw hold `Fenrir::Entity`, so that `EntityValue` is left to natives' parameters and results (spec: "`EntityValue` is for natives only"). After this ticket those fields show as `EntityLink` in C# through `uses`; ticket 02 renames that to `Entity`.

- **`packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml`**: delete the `exclude` list and its comment. The engine's `QueryFilter`, `RaycastHit`, `ShapeCastHit`, `MoveCastHit`, `OverlapHit`, `PenetrationHit` and `Contact` (`packages/FenrirPhysics/include/FenrirPhysics/Queries.hpp`, selected as shared structs in `packages/FenrirPhysics/api/FenrirPhysics.api.toml`) are mirrored, holding `Fenrir::Entity` as the built-in binding's `EntityLink` through `uses` (PR #199 made that work). `uses` gains nothing: the built-in binding is there already.
- **`packages/FenrirPhysics/Scripting/api/FenrirPhysicsScripting.api.toml`** and **`src/ScriptQueries.hpp`**: the shadow query types go; what else that description holds (check: the natives' own option structs) stays, and `"Fenrir::EntityValue" = "bound-type"` goes if nothing described holds one.
- **`packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp`** (and `PhysicsScriptingState.hpp` if it names them): the query natives take and fill the engine's structs, with the conversions to and from `ScriptQueries` removed; a native that took a `ScriptQueries::QueryFilter` takes `const Fenrir::QueryFilter*`. The natives' `[natives] includes` drop `ScriptQueries.hpp`; the `[cpp] includes` too. Regenerate the natives description (`fenrir-describe-api`) and the binding.
- **`packages/FenrirDebugDraw/Scripting/src/ScriptOptions.hpp`**: `Fenrir::Entity relativeTo` instead of `EntityValue`; `DebugDrawScriptingState.hpp`'s `TargetFor` takes the entity (or its value from it); `FenrirDebugDrawScripting.api.toml` selects nothing as `EntityValue` if nothing holds one; regenerate.
- **Scripts**: the physics scripting fixture (`packages/FenrirPhysics/Scripting/Tests/Fixture/Bodies.cs`: `hit.Body.Value` reads become `hit.Body.Target.Value`, or a helper, until ticket 02) and the examples that read a hit's body (`examples/PhysicsDemo/scripts/`); say which example files changed, the user checks them.
- **Docs**: the header comments that said why the shadow types existed go with them; the physics binding's header comment stops naming `FenrirPhysicsScripting.api.json` if that description is gone.

**Blocked by:** none

**Touches:** `packages/FenrirPhysics/Scripting/`, `packages/FenrirPhysics/Managed/Fenrir.Physics/Generated/`, `packages/FenrirDebugDraw/Scripting/`, `packages/FenrirDebugDraw/Managed/`, `examples/PhysicsDemo/scripts/`

**Status:** resolved

- [x] `grep -rn "EntityValue" --include=*.hpp packages/FenrirPhysics packages/FenrirDebugDraw` finds only natives' parameters; `ScriptQueries.hpp` is gone; `grep -n "exclude" packages/FenrirPhysics/Scripting/api/FenrirPhysics.binding.toml` finds nothing.
- [x] `ctest -R "Scripting|PhysicsCompile"` passes under the msvc-x64-debug preset; both generators are no-ops afterwards.
- [x] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from the spec's branch (`claude/entity-fields-spec`, on PR #199's). The test-support stack's ticket 03 (PR #198) edited physics test sources: cut from a base that has it, or rebase.

Built 2026-10-28 as PR #202, merged into the spec branch.
