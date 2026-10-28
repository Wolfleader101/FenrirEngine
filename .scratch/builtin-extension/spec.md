Status: ready-for-agent

# Built-in extension: the built-in extension is an extension like the six

Origin: the second architecture review of `dotnet-scripting` (2026-10-27), candidates 5 and 6 of seven, rolled up by the user and grilled to nine decisions the same day. Candidate 1 of the same review became ADR-0046 (`.scratch/extension-shape`, built in PRs 176 to 180).

The vocabulary is the repo's: *scripting extension*, *built-in extension*, *extension state*, *native function*, *call context*, *native guard*, *binding*, *API description*, *bound type*, *shared struct*, *wrapper* from `GLOSSARY.md`; *module*, *interface*, *implementation*, *deep*, *shallow*, *seam*, *locality*, *leverage* from the codebase-design skill.

## Problem statement

Six packages carry a scripting extension on one seam (`ScriptingExtension`), and since ADR-0046 each is spelled by one owner. The built-in extension, FenrirScripting's own, sits on the same seam and is spelled differently in three places.

**Four coats on one concept.** "A C++ type mirrored by a C# struct, checked against its mapping when a script assembly loads" is written four times in the public `include/FenrirScripting/ComponentRegistry.hpp` (337 lines): `ComponentRegistration`, `EventRegistration`, `SharedStructRegistration` and `ResourceRegistration` each carry a `name`, a `typeName`, a size and alignment (a component's inside its `ComponentType`) and a `validate`, each documented afresh, and the five makers (`MakeComponent`, `MakeReadOnlyComponent`, `MakeEvent`, `MakeSharedStruct`, the four `Make...Resource` over `Detail::MakeResource`) each repeat the same three `static_assert`s and five assignments. `Scripting::InstallImpl` then runs its `addAll` lambda four times per extension, once per list, with the event list alone taking a `before` hook that names the queue. The private `src/ComponentRegistry.hpp` is already generic (`RegistryTable<Registration>`) and asks nothing of a registration but its `name`.

**The built-in binding cannot be used.** `FenrirScripting.binding.toml` names no `descriptions`, so no binding can `uses` it (Iris.Bindings refuses a `uses` entry that names no descriptions). The one entity value every binding names (`Fenrir::EntityValue`, ADR-0044) is therefore described three times: `FenrirPhysicsScripting.api.toml`, `FenrirDebugDrawScripting.api.toml` and `FenrirRendererScripting.api.toml` each reach into `FenrirScripting/include/FenrirScripting/EntityValue.hpp` and select it as a bound type, the renderer's selection existing for nothing else; and `FenrirPhysics.binding.toml` names `Fenrir::Transform` and `Fenrir::Entity` by hand in its `[bound-types]`, each with the built-in mappings header's path, where every other cross-binding type is a `uses` line. The review found the second half of this; the grilling found why `uses` alone would not fix it: `Binding.Types` is a binding's own described types, and a native's parameter is resolved against it, so a used binding's described bound type is not yet a type the using binding may take.

**One native file, two conventions.** `src/Bindings.cpp`, the file the Iris.Bindings README sends a newcomer to, is the one natives source not on ADR-0045's contract. `EntityWorldTransform` null-checks `world` and writes `*world = Transform{}` while the generated registration already wraps it in `ZeroingOutputs<..., 3>`; `EntitySetWorldTransform` null-checks an `in` pointer; `Log`, the three `*IdOf` and the two `RegisterScript*` take `const char*` plus `std::int32_t` where every package native takes `std::string_view` (so `FenrirScripting.natives.json` carries nine `"pointeeKind": "i8"` text pairs, the target-dependent kind the generator-hygiene spec is chasing). ADR-0045 excluded these natives because "they report failures as codes C# throws on, not warnings, and keep their own checks". The grilling checked the claim: nine natives return a code (six a `ChangeError`, three a `RegistryError` as `int32`), and none of the nine checks a pointer. Their codes are `ChangeBuffer`'s and `RegistryTable`'s rules. The exclusion protects nothing that exists, and covers eleven natives whose declarations say exactly what a package native's does.

## Solution

### One mirror registration

A `MirrorRegistration` struct in the public `ComponentRegistry.hpp` holds what the four share: `name`, `typeName`, `size`, `alignment`, `scriptDefined` and `validate`, documented once. The four registration structs derive from it and keep only their extras:

- `ComponentRegistration`: `type` (its `ComponentType`), `readOnly`, `required`;
- `EventRegistration`: `bind`;
- `SharedStructRegistration`: nothing;
- `ResourceRegistration`: `scope`, `readOnly`, `find`, `insert`, `remove`.

Inheritance, not a `mirror` member, so `registration.name` and the rest stay spelled as they are at every site (`RegistryTable`, `ScriptData.cpp`, `Scripting::FindComponent`, the physics drift tests). One `MakeMirror<T, Mapping>(std::string name)` holds the three `static_assert`s (the mapping's size and alignment, `Iris::StructValue<Mapping>`, trivial copyability where a kind needs it is the kind's) and the five shared assignments; `MakeComponent`, `MakeEvent`, `MakeSharedStruct` and `Detail::MakeResource` call it and add their extras. The five maker names and the four struct names stay public, since the generated registration, the SDK defaults file and the glossary name them. `MakeComponent`'s designated initialiser and `ScriptData.cpp`'s become assignments (a designated initialiser cannot name a base's members).

Install's second stage becomes one call per extension: `ComponentRegistry::AddExtension(ScriptingExtension& extension, App& app)` in the private `src/ComponentRegistry.hpp`, returning `std::optional<std::string>`, the failure text Install wraps in a `StartupFailure`. It names each event's queue before adding it (today's `bindQueue`), adds the four lists under the one-name rule, and builds the "a component in extension 'x' has an empty or duplicate name" message from `RegistryTable::Noun` as the lambda does today. It does not roll back: a failure midway leaves the earlier registrations in the registry, as today, because Install then returns a Scripting without scripts and the registry goes with it; the method's comment says so. `RegistryTableTests.cpp` pins it: the one-name rule across kinds, the queue-name failure, the message's noun and article.

### The built-in binding as a `uses` target

Iris.Bindings gains one rule: **a type a used binding describes is described in the binding that uses it**, with the used binding's `[bound-types]` entry (for a bound type) or mirror (for a described struct or enum) in force, so a native here may take, return or hold it and a shared struct here may hold it, without this binding's descriptions describing it again. Today that holds only for a type both bindings describe. The rule is `Binding.cs`'s (`Types`, `IsUsed`, `BoundTypeOf`) and `Natives.cs`'s resolution of a parameter's type, with a `tests/Fixture/Uses` case; the README's "Other bindings' types" paragraph says it.

With the rule, `FenrirScripting.binding.toml` names `descriptions = ["../../FenrirECS/api/FenrirECS.api.json", "FenrirScripting.api.json"]` (the two its CMake command passes, which the tool requires to match), and `FenrirScripting.api.toml` selects `Fenrir::EntityValue` as a bound type from `../include/FenrirScripting/EntityValue.hpp` beside its two enums, so the built-in binding describes the entity value as the SDK defaults file already names it. Then:

- `FenrirPhysics.binding.toml` adds the built-in binding to `uses`, deletes its `Fenrir::Transform` and `Fenrir::Entity` `[bound-types]` entries, and `FenrirPhysicsScripting.api.toml` drops the `EntityValue.hpp` header and the `Fenrir::EntityValue` line (its seven `ScriptQueries` structs stay);
- `FenrirDebugDraw.binding.toml` adds it to `uses`, and `FenrirDebugDrawScripting.api.toml` drops the same header and line (its option structs stay);
- `FenrirRenderer.binding.toml` adds it to `uses`, and `FenrirRendererScripting.api.toml` and `.api.json`, which held nothing else, are deleted; the renderer's `fenrir_scripting_extension` call passes `NO_SELECTION` as Window's does.

Physics's deleted `Fenrir::Entity` entry carried `default = "EntityLink.None"`, because a default `Fenrir::Entity` (its id every bit set) is not C#'s all-zero `new EntityLink()`. The built-in binding's generated `EntityLink` initialises its `value` field to that id, so an `EntityLink` field of a physics mirror should default the same way through the used mirror; the ticket's check is that every regenerated C# mirror and mapping is byte-identical to the committed one. If one is not, the ticket keeps that entry and says so in its PR.

The order of `uses` does not matter to the tool; the comment above each `uses` list says what is named from the built-in binding (the entity value; for physics, the transform and the entity link too).

### The built-in natives on the contract

Every native in `src/Bindings.cpp` moves onto ADR-0045's contract and ADR-0044's text kind:

- no comparison of an `in` or `out` pointer parameter against null (`EntityWorldTransform`, `EntitySetWorldTransform`, `EventReadAfter`'s `seen` and `view`), and no `*out = T{}` (`EntityWorldTransform`; the registration zeroes it);
- `const char*` plus `std::int32_t` length becomes `std::string_view` (`Log`'s `text`; `ComponentIdOf`, `EventIdOf` and `ResourceIdOf`'s two pairs; `RegisterScriptComponent` and `RegisterScriptEvent`'s `name`), so their C# declarations read `ReadOnlySpan<char>`, which the contract's `string` arguments convert to without a change at any call site (`Log.cs`, `RegistryIds.cs`, `ScriptComponents.cs`, `Events.cs`), and `FenrirScripting.natives.json` loses its nine `i8` text pairs;
- a pointer with a count becomes a span through `Fenrir::SpanOf` (`QueryCount` and `QueryFill`'s `ids` and `rows`).

Three rules stay, as rules on values rather than on pointers: `RegisterScript*` refuse an empty name or a non-positive size or alignment; `QueryCount` and `QueryFill` return -1 for an empty id list (so a C# query of nothing still throws, as today); `EventReadAfter` refuses a queue whose event size is not the registration's. The nine code-returning natives change nothing: they already fit. The file's header comment stops saying a string arrives as UTF-8 and its length and says what every natives source says: the declaration is the contract (ADR-0045).

No `NativeGuard` and no `WarnOnceRecord` on `EngineContext`. The built-in's failure channel is a code or a false that C# turns into an `InvalidOperationException` (`Entity.WorldTransform`, `QueryNative`, `EventReader.Read`); a warn-once beside it would report each failure twice. The guard stays what it is: a package native's way to say a call did nothing where C# is not told.

`EventReadAfter`'s `view`, five slots behind a bare `nuint*`, keeps its shape: describing a five-field `EventView` shared struct so it became `out EventView` would change the contract's `EventReader` for a shape nothing else reads. Out of scope, named here so the next person does not re-find it.

### What stays as it is

- `BuiltInExtension` in `Bindings.cpp` stays hand-written, with no maker (ADR-0046's exception, reaffirmed): a maker would name the extension `FenrirScripting` where Install says `built-in`, share only `Fenrir.Scripting.dll` where the built-in shares `Fenrir.Math.dll` first, and leave the three engine components' `required` flags unset, so fitting it needs two new binding keys to delete twelve lines. The spec states it as the one place the built-in extension differs from the six.
- ADR-0045's `in`/`out` rule, `ZeroingOutputs` and the registration's indices: unchanged; the built-in's registration already names them.
- The six package natives sources' dead `Fenrir::Generated::Register<Pkg>` declarations (left behind when the maker made the register function file-local): not this spec's; listed as a follow-up for whoever next touches those files.
- The NativeCallHarness library, the `ScriptingTests.cpp` split and the SystemHost seam: they wait for this spec because they touch `Scripting.cpp` and the tests it feeds.

## Decisions (from the grilling)

1. One spec for both candidates, slug `builtin-extension`, three tickets.
2. No maker for the built-in extension: ADR-0046's exception stands, and the spec says why.
3. `MirrorRegistration` is a base struct the four derive from, with `MakeMirror<T, Mapping>` under the five makers; inheritance so no access site changes; the struct and maker names stay public.
4. Install's four `addAll` calls become `ComponentRegistry::AddExtension(ScriptingExtension&, App&)`, pinned by the rules tests; no rollback on a failure midway, stated in its comment.
5. The `uses` half is built: Iris.Bindings learns that a used binding's described types are described here, the built-in binding names its descriptions and selects `EntityValue`, and Physics, DebugDraw and Renderer switch to `uses` it. The ticket waits, in text, for the generator-hygiene spec's Iris.Bindings tickets to merge.
6. Every built-in native moves onto the contract and `std::string_view`; the three value rules stay; no native guard and no warn-once record in the built-in.
7. `EventReadAfter` keeps its raw pointers, minus the null checks; an `EventView` shared struct is out of scope.
8. ADR-0045's last consequence is replaced by one line; ADR-0044's `EntityValue` consequence gains one sentence; no new ADR. The glossary's *scripting extension* entry gains one sentence on the built-in extension.
9. Tickets `01-mirror-registration`, `02-natives-on-the-contract`, `03-uses-the-built-in-binding`; 01 and 02 side by side off the base, 03 on 02.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | `MirrorRegistration` and `MakeMirror` in the public header; `ComponentRegistry::AddExtension`; Install's stage one call; `ScriptData.cpp`'s initialisers; rules tests | none |
| 02 | `Bindings.cpp` on the contract and `std::string_view`; natives description, natives header and `Native.cs` regenerated | none |
| 03 | Iris.Bindings' used-types rule and fixture; the built-in binding's `descriptions` and `EntityValue` selection; Physics, DebugDraw and Renderer onto `uses`; the renderer's selection deleted | 02 |

01 touches `include/FenrirScripting/ComponentRegistry.hpp`, `src/ComponentRegistry.hpp`, `src/Scripting.cpp`, `src/ScriptData.cpp` and `Tests/Rules/`; 02 touches `src/Bindings.cpp`, `api/FenrirScripting.natives.json`, `src/Generated/FenrirScriptingNatives.hpp` and `Managed/Fenrir.Scripting/Generated/Native.cs`. No file in common, so both are cut from `dotnet-scripting` and target it, and may run side by side; whichever merges second rebases. 03 touches `api/FenrirScripting.binding.toml` and `api/FenrirScripting.api.toml`, whose regenerated outputs 02 also regenerates, so it is cut from 02's branch and targets it. 03 also touches `packages/Iris/Tools/Iris.Bindings/src/`, where the generator-hygiene spec's tickets are working: it starts after those merge. Branches `builtin-extension-01-mirror-registration`, `-02-natives-on-the-contract`, `-03-uses-the-built-in-binding`.

## Done looks like

- `include/FenrirScripting/ComponentRegistry.hpp` declares `MirrorRegistration` once and `size`, `alignment`, `typeName`, `validate` nowhere else; `grep -c static_assert` on it finds the asserts in `MakeMirror` and the kinds' own trivial-copyability asserts only.
- `grep -n addAll packages/FenrirScripting/src/Scripting.cpp` finds nothing; the stage is one `AddExtension` call per extension.
- `grep -n "nullptr" packages/FenrirScripting/src/Bindings.cpp` finds no comparison of a parameter; `grep -n "const char\*" packages/FenrirScripting/src/Bindings.cpp` and `grep -c '"i8"' packages/FenrirScripting/api/FenrirScripting.natives.json` find nothing; `Native.cs` declares `Log(int level, ReadOnlySpan<char> text)`.
- `FenrirScripting.binding.toml` names `descriptions`; `grep -rln "EntityValue.hpp" packages/*/Scripting/api/` finds nothing; `packages/FenrirRenderer/Scripting/api/` holds only `FenrirRenderer.binding.toml`; `FenrirPhysics.binding.toml` has no `[bound-types]`.
- `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after each committed regeneration; every regenerated C# mirror is byte-identical to the committed one, or the PR names the one that is not and keeps its entry.
- ADR-0044 and ADR-0045 carry their amendment lines; `GLOSSARY.md`'s *scripting extension* entry says how the built-in extension differs.
- The msvc-x64-debug preset builds and tests locally; the other presets are CI's.
