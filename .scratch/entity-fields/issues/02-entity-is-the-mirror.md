# 02: `Entity` is the mirror of `Fenrir::Entity`; `EntityLink` goes

**What to build:** the one C# `Entity` (spec: "`Entity` is the mirror", "`EntityValue` is for natives only" second paragraph, "Scripts, tests, docs").

- **`packages/Iris/Tools/Iris.Bindings`**: two rules. (a) A `[bound-types]` entry with a `wrapper` may have the C# type of a mirror this binding or a used one generates (today that is refused as a facade class is: find the check that groups C# names and the one naming bound types; a bound type without a `wrapper` is still refused, since it would have a layout of its own). (b) A described struct holding a wrapper bound type as a field is refused naming the field: "`<path>`: `<Type>::<field>` holds `<C++ type>`, which is `<wrapper>` where C# `<T>` is a mirror: hold the mirrored type". README (bound types paragraph, diagnostics list) and `tests/BindingTests.cs` cases for both, with a fixture change if the fixture's `Ticket` wrapper or a new one is the natural case; fixture outputs regenerated.
- **`packages/FenrirScripting/api/FenrirScripting.binding.toml`**: `[names] "Fenrir::Entity" = "Entity"` (the `m_entityId`/`m_entityList` entries stay); `immutable-structs` and `private-fields` stay. `FenrirScripting.binding-defaults.toml`'s `EntityValue` entry stays as it is (`type = "Entity"`, `wrapper = "u32"`, `constructor = ["value"]`).
- **`packages/FenrirScripting/Managed/Fenrir.Scripting/Entity.cs`**: today's members on `public readonly partial struct Entity`, the generated half supplying `value` and `owner`. The constructor from a value sets `value` and stamps `owner` from `Native.EntityLinkOwner()` unless the value is `uint.MaxValue` (owner 0); `None` is `new(uint.MaxValue)`; `Value` reads `value`; equality, hash and `ToString` by value; the doc comment says the shape (value and owner, as C++'s), what `None` and `default` are, and that a native-returned entity names the active scene. **`EntityLink.cs`** is deleted. `Native.EntityLinkOwner`'s doc in `Bindings.cpp` says the constructor uses it.
- **Regenerate everything** (`fenrir-generate-bindings`): the built-in `Relationship`, physics's holders and query structs, DebugDraw's options show `Entity` fields; the mappings and defaults tests are byte-identical but for the name. Review: nothing but the type name and the initialiser spelling may change.
- **`packages/FenrirScripting/Tests/Fixture/Links.cs`**: `Tether.Target` is `Entity`; `Unlink` assigns `Entity.None`; `ReadableMembers` pins `Entity`'s public surface; a new C++ case in `packages/FenrirScripting/Tests/` pins that `Entity.None` written from C# reads as C++'s default `Entity` and that a native-returned entity's owner is the active scene's list. **`Registry.cs`**: the hierarchy reads compare with `Entity.None`. The physics scripting fixture and **the examples** (`examples/PhysicsDemo/scripts/Character.cs`, `Joints.cs`, `Vehicles.cs`, `examples/RendererDemo/scripts/MeshesAndMaterials.cs`) lose every `EntityLink` spelling and `NameOf(EntityLink)` helper; say which example files changed.
- **Docs**: `GLOSSARY.md` "Entity" (same shape in both languages; `None`; `default`) and the "Entity link" entry removed; ADR-0044's `EntityLink` sentence corrected; physics's hand-written docs (`Joints.cs`, `Vehicles.cs`) name `Entity.None`.

**Blocked by:** 01

**Touches:** `packages/Iris/Tools/Iris.Bindings/`, `packages/FenrirScripting/api/FenrirScripting.binding.toml`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `packages/FenrirPhysics/Managed/`, `packages/FenrirPhysics/Scripting/Tests/Fixture/`, `packages/FenrirDebugDraw/Managed/`, `examples/PhysicsDemo/scripts/`, `examples/RendererDemo/scripts/`, `GLOSSARY.md`, `docs/adr/0044-natives-pass-enums-text-and-wrappers-by-value-and-bindings-use-other-bindings-mirrors.md`

**Status:** ready-for-agent

- [ ] `grep -rn "EntityLink" packages examples` finds nothing; Iris.Bindings' tests pass with the two new cases.
- [ ] `ctest -R "Scripting|PhysicsCompile"` passes under the msvc-x64-debug preset, the new link case included; both generators are no-ops afterwards.
- [ ] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from ticket 01's pushed branch.
