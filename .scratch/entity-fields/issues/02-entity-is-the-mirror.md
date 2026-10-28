# 02: `Entity` is the mirror of `Fenrir::Entity`; `EntityLink` and `EntityValue` go

**What to build:** the one C# `Entity`, and natives that take it (spec: "`Entity` is the mirror", "`EntityValue` goes: natives take the entity", "Scripts, tests, docs"). Two halves that must land together, since C# `Entity` becomes 16 bytes: the mirror, then the natives.

**The mirror**

- **`packages/FenrirScripting/api/FenrirScripting.binding.toml`**: `[names] "Fenrir::Entity" = "Entity"` (the `m_entityId`/`m_entityList` entries, `immutable-structs` and `private-fields` stay).
- **`packages/FenrirScripting/Managed/Fenrir.Scripting/Entity.cs`**: today's members on `public readonly partial struct Entity`, the generated half supplying `value` and `owner`. `None` is `new(uint.MaxValue)`; `Value` reads `value`; equality, hash and `ToString` by value; the constructor from a value is `internal`, gives owner 0, and is for tests that name an entity by id; the doc comment says the shape (value and owner, as C++'s), what `None` and `default` are (`default` names entity 0 with no scene and is never valid), and that every entity a script holds came from C++ with its scene set. **`EntityLink.cs`** is deleted; `IEquatable<Entity>` and the operators stay.
- **Regenerate everything** (`fenrir-generate-bindings`): the built-in `Relationship`, physics's holders and query structs, DebugDraw's options show `Entity` fields. Review: nothing but the type name and initialiser spelling may change in the mirrors.

**The natives**

- **`packages/FenrirScripting/src/Bindings.cpp`**: every native taking `std::uint32_t value` or `entity` takes `const Fenrir::Entity*` (C#: `in Entity`); `EntityCreate` fills a `@param[out] Fenrir::Entity*` and returns whether it could; `EntityLinkOwner` is deleted. `Entity.cs` passes `this` (`in`) and takes `Create`'s result from the out parameter.
- **`packages/FenrirScripting/include/FenrirScripting/NativeGuard.hpp`**: `Alive` takes `const Entity&`, finds it alive in the active scene's list, and treats an entity whose owner is not that list as not alive (one warning, as today's rule); its tests in `packages/FenrirScripting/Tests/Rules/NativeGuardTests.cpp` and `ScriptingTests.cpp` follow.
- **Extension natives**: `packages/FenrirPhysics/Scripting/src/PhysicsScripting.cpp` and `PhysicsScriptingState.hpp`, `packages/FenrirRenderer/Scripting/src/RendererScripting.cpp` and `RendererScriptingState.hpp`, `packages/FenrirDebugDraw/Scripting/src/DebugDrawScriptingState.hpp`: `EntityValue entity` parameters become `const Fenrir::Entity* entity` (or `const Entity&` behind the native, as the guard takes it); their tests (`FenrirPhysics/Scripting/Tests/Rules.cpp`, `FenrirRenderer/Scripting/Tests/NoOps.cpp`, `FenrirDebugDraw/Scripting/Tests/NoOps.cpp`) make entities instead of values. Their `[natives] includes` drop `EntityValue.hpp`.
- **Delete `EntityValue`**: `packages/FenrirScripting/include/FenrirScripting/EntityValue.hpp` and `EntityValueMapping.hpp`, the bound-type entry in `FenrirScripting.binding-defaults.toml`, the `"Fenrir::EntityValue" = "bound-type"` line and header in `packages/FenrirScripting/api/FenrirScripting.api.toml` and any extension selection still naming it, and `[cpp] includes` naming the mapping. Run `fenrir-describe-api` (natives descriptions change) then `fenrir-generate-bindings`; the generated natives declarations show `in Entity` and `out Entity`. The generator should need no change: if it refuses something, stop and report rather than widen.

**Scripts, tests, docs**

- **`packages/FenrirScripting/Tests/Fixture/Links.cs`**: `Tether.Target` is `Entity`; `Unlink` assigns `Entity.None`; `ReadableMembers` pins `Entity`'s public surface; a C++ case in `packages/FenrirScripting/Tests/` pins that `Entity.None` written from C# reads as C++'s default `Entity`, and that an entity from `Entity.Create` written into a component carries the active scene's list. **`Registry.cs`**: the hierarchy reads compare with `Entity.None`. The physics scripting fixture (`Bodies.cs`'s `.Target.Value` reads from ticket 01 become `.Value`) and **the examples** (`examples/PhysicsDemo/scripts/`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`) lose every `EntityLink` spelling and `NameOf(EntityLink)` helper; say which example files changed, the user checks them.
- **Docs**: `GLOSSARY.md` "Entity" (same shape in both languages; `None`; `default`) and the "Entity link" entry removed; ADR-0044's `EntityLink` and `EntityValue` sentences corrected in place; physics's hand-written docs (`Joints.cs`, `Vehicles.cs`) name `Entity.None`; the natives' README mention of `EntityValue`, if any.

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/api/`, `packages/FenrirScripting/include/FenrirScripting/`, `packages/FenrirScripting/src/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `packages/FenrirPhysics/Scripting/`, `packages/FenrirPhysics/Managed/`, `packages/FenrirRenderer/Scripting/`, `packages/FenrirRenderer/Managed/`, `packages/FenrirDebugDraw/Scripting/`, `packages/FenrirDebugDraw/Managed/`, `examples/PhysicsDemo/scripts/`, `examples/RendererDemo/scripts/`, `GLOSSARY.md`, `docs/adr/0044-natives-pass-enums-text-and-wrappers-by-value-and-bindings-use-other-bindings-mirrors.md`

**Status:** claimed

- [x] `grep -rn "EntityLink\|EntityValue" packages examples` finds nothing.
- [x] `ctest -R "Scripting|PhysicsCompile"` passes under the msvc-x64-debug preset, the new link cases included; both generators are no-ops afterwards; Iris.Bindings' tests pass unchanged.
- [x] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from the spec's branch after PR #202 merged into it. Widened the same day on the user's question: `EntityValue` goes too, natives taking the entity by pointer, so the generator needs no change.

Built 2026-10-28 on branch `claude/entity-fields-02`. Beyond the list above, and needed: `InternalsVisibleTo` for the five
fixture assemblies (they name entities by id through the internal constructor); the guard's owner rule looks an
owner-less entity up by value, so those fixtures' entities and a zeroed field still resolve, and an entity another scene
owns is not alive; one engine accessor, `Entity::HasEntityList()`, since `Entity(id, list)` is a creating constructor and
cannot probe for a list; query rows carry the whole `Fenrir::Entity`, since `Query<T>` built its entity from the bare
value and a query entity written into a native field would have had no scene; the Iris.Bindings fixture's two natives
take the entity alias by pointer, since the SDK check compiles against the real `Entity`, no longer a wrapper.
