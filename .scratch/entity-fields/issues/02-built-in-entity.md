# 02: The built-in binding shows `Fenrir::Entity` as `Entity`

**What to build:** the built-in binding's projection, the `EntityLink` semantics the spec pins, and the scripting tests that prove them (spec: "`EntityLink` stays, as storage" and "The built-in binding, physics, scripts", first and third bullets).

- **`packages/FenrirScripting/api/FenrirScripting.binding.toml`**: `project-as = { "Fenrir::Entity" = { type = "Entity", get = "Target" } }` beside `immutable-structs`, with a comment (a script holds an entity; the link is storage). Regenerate: `Relationship.cs` shows four `public Entity` getters over four `private readonly EntityLink` backing fields, and nothing else in `packages/FenrirScripting/Managed/Fenrir.Scripting/Generated` changes.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting/EntityLink.cs`**: the implicit conversion from `Entity` returns `None` for `Entity.None` (no list stamped); the type's doc comment says it is the storage of an entity field, which a generated mirror shows as `Entity`, and that hand-written components may hold one directly.
- **`packages/FenrirScripting/Tests/Fixture/Links.cs`**: `Tether` in the projected form (`public Entity Target { get => target.Target; set => target = value; } private EntityLink target;`); `Unlink` assigns `Entity.None`; a new case pins that converting `Entity.None` stores `EntityLink.None` (owner 0), checked from C++ in `packages/FenrirScripting/Tests/` beside the existing link tests; `ReadableMembers` unchanged. **`Registry.cs`**: the hierarchy reads go through `Entity` (`parent == Entity.None ? uint.MaxValue : parent.Value`).
- **`GLOSSARY.md`** "Entity link": a script sees an entity named in a component or event as an `Entity`; `EntityLink` is how a mirror stores one, and the empty link reads as `Entity.None`.
- Physics still compiles: its holders regenerate in ticket 03, and until then they hold `EntityLink` fields as today (the projection is inherited through `uses` only when physics is regenerated).

**Blocked by:** 01

**Touches:** `packages/FenrirScripting/api/FenrirScripting.binding.toml`, `packages/FenrirScripting/Managed/Fenrir.Scripting/EntityLink.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Relationship.cs`, `packages/FenrirScripting/Tests/`, `GLOSSARY.md`

**Status:** ready-for-agent

- [ ] `grep -n "EntityLink" packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Relationship.cs` finds only the four private backing fields.
- [ ] `ctest -R "Scripting"` passes under the msvc-x64-debug preset, the new `None` case included; `fenrir-generate-bindings` is a no-op afterwards.
- [ ] clang-format and `check_tickets.py` pass.

## Comments

Filed 2026-10-28 from the spec. Cut from ticket 01's pushed branch. The test-support stack's ticket 03 (PR #198) edited `packages/FenrirScripting/Tests/`: cut from a base that has it, or rebase.
