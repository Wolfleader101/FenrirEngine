Status: ready-for-agent

# Facade pass-throughs: a native's `@facade` mark generates its public method

Origin: the user asked whether the facades over the generated `<Package>Native` classes can be generated too, once the binding-defaults stack (`.scratch/binding-defaults/`, PRs #155, #156, #158, #160) and span declarations (`.scratch/span-declarations/`, PR #161) made the generated declarations typed. Grilled to twelve decisions in one session (2026-10-26), from the counts below, which were taken on the `span-declarations` tip, on which this is stacked.

The vocabulary is the repo's: *facade*, *native function*, *binding*, *natives description*, *mirror*, *wrapper* from `GLOSSARY.md`; *module*, *interface*, *seam*, *leverage* from the codebase-design skill. ADR-0043 is amended (below), not replaced: the boundary it drew does not move, one more thing has become describable.

## Problem statement

ADR-0043 settled that a facade (`Window`, `Keyboard`, `Mouse`, `Gamepads`, `PhysicsWorld`, `DebugDraw`, `Renderer`, `Assets`, `Materials`, `Shaders`: the public static classes over the natives) stays hand-written, holding only what a description cannot say, and rejected generating facades "from a public flag and nicer names" because the overloads, generics and conversions that make a facade worth having cannot be described without a second description of them. When it was written, a facade method cast, packed and pinned; since ADR-0044 and the two stacks above, the generated declaration already takes enums, wrappers, text and spans as C# does, so many facade methods are a one-line pass-through that only names the native publicly.

A script (`count_passthroughs.py` beside this spec, run over the eight facade files and six generated `<Package>Native` classes copied into one directory) counts, on the `span-declarations` tip, 127 public members over 128 natives:

| Facade | Public members | Exact pass-through | Drops `in` only | Defaults or reorder | Property or generic | Converts, packs or wraps |
|---|---|---|---|---|---|---|
| `Window` | 13 | 11 | 0 | 0 | 0 | 2 |
| `Keyboard`, `Mouse`, `Gamepads` | 18 | 15 | 0 | 0 | 3 | 0 |
| `PhysicsWorld` | 21 | 3 | 4 | 5 | 1 | 8 |
| `DebugDraw` | 31 | 5 | 0 | 0 | 0 | 26 |
| `Renderer` | 7 | 2 | 3 | 0 | 0 | 2 |
| `Materials`, `Assets`, `Shaders` | 37 | 0 | 0 | 0 | 2 | 35 |
| **Total** | **127** | **36** | **7** | **5** | **6** | **73** |

An *exact pass-through* is `=> XNative.F(a, b)` with the same parameters, modifiers, result and no default; *drops `in` only* is the same but the facade takes `T` where the declaration takes `in T` and passes `in`. Three facts from the count:

- **No public facade method takes `in`** (0 of 127): every facade takes by value what its native takes `in`. That is a uniform rule of the facades, not a per-method choice.
- **The rename overloads need no overload description.** `Keyboard.IsDown(Key)`, `IsDown(Scancode)`, `Mouse.IsDown(MouseButton)` and `Gamepads.IsDown(GamepadId, GamepadButton)` are four natives (`KeyIsDown`, `ScancodeIsDown`, `MouseIsDown`, `GamepadIsDown`) given one public name on three classes; C# overloads by parameter type. Fifteen of the 36 are this family.
- **The docs differ in 20 of the 43.** Twenty-three facade docs equal the C++ doc once `<paramref>` is read as backticks; five are DebugDraw's C++ docs appending "(DebugDraw.ShowGroup)"; four are physics `Add*` natives whose C++ doc has no `@return`; eleven are facade docs that say more.

The 73 converting members (DebugDraw's options conversion, the assets family's `AssetHandle<T>.Bits` packing, the `out`-to-return wrappers), the 5 with `QueryFilter filter = default` and the 6 properties and generics are what ADR-0043 meant, and stay hand-written. The 43 are a facade adding nothing but a public name, which one word beside the native can say.

## Solution

### The mark: `@facade Class.Method` in the native's doc comment

A native's C++ doc comment may carry one `@facade Class.Method` command (`\facade` as `\param`): the public static class, in the binding's namespace, and the method name it has there. The public name is the native's own fact, as what its `out` parameter means is, and the natives are already "named as C# calls them" in each source's header comment, so it is recorded once, beside the native, never in a table of the binding's. describe-api records it as a `facade` string on the function in `<Package>.natives.json`, beside `notRunning`; a mark that is not `Identifier.Identifier`, or a second mark on one native, fails the tool naming the function. The method name is always written: `Keyboard.IsDown` beside `KeyIsDown` says the public name in one form, and a reader never has to know a defaulting rule.

### The generated facade part

With `[natives]`, Iris.Bindings also writes, for each facade class the marks name, `<generated>/<Class>.cs`: `public static partial class <Class>` in the binding's namespace, with the same `using`s rule as the natives file, a `//` header (no `///`, so the hand-written part alone carries the class summary), and one `public static` method per mark, in the description's order: the declaration's result, its parameters by name and type, documented as the declaration is (the C++ doc verbatim, backticks as the generated mirrors' public docs already have them), and an expression body calling the native. One rule and no mark: a parameter the declaration takes `in T` the facade takes `T` and passes `in`, as all 127 hand-written members do; `out`, spans, text and values are as declared. The hand-written class becomes `partial` and keeps what a description cannot say.

Iris.Bindings refuses, at generation time with a diagnostic naming the native and its mark: a mark on a native whose declaration cannot be public, one taking a type in `internal-structs` (`NativeShapeOptions`, `NativeTextOptions`: why DebugDraw's 26 shape calls stay by hand) or a pointer (`void*`, an `[in,out]` `T*`); two marks giving one class and method with the same declaration parameter types; a facade class named as a mirror, a bound type or the `[natives] class`.

### What stays hand-written

Everything the count's last three columns hold, and the rule for it is unchanged: overloads that are not renames, generics (`Assets.Load<T>`), properties over outputs (`Mouse.Position`, `PhysicsWorld.Gravity`), constants (`Window.Main`, `DebugDraw.Forever`), a default argument (`QueryFilter filter = default`, with its reorder), a `void` native's one `out` returned as a value (`Window.Size`, `PixelSize`, `PhysicsWorld.GetJointState`), handle packing and the options conversion. Three cases do not pay for an `out`-to-return rule, and a property or a default is a second description.

### The port

The 43 members get their mark in the five sources (`WindowScripting.cpp` 11, `InputScripting.cpp` 15, `PhysicsScripting.cpp` 7, `DebugDrawScripting.cpp` 5, `RendererScripting.cpp` 5), the five hand-written classes become `partial` and lose them, and every binding is regenerated. The public doc is then the C++ doc, so the port moves into the native's comment what the facade's said more (`Window.GetClipboard`, `RequestClose`, `Gamepads.Connected`, `PhysicsWorld.ContactsOf`, `IsSleeping`, `Bounds`, and `Renderer`'s five), adds the `@return` the four physics `Add*` natives lack, and drops DebugDraw's five "(DebugDraw.X)" cross-references, which the facade name now says. Nothing changes for a script: every deleted member has a generated one of the same name, parameters and result, which the contracts' and examples' builds and the facades' existing .NET-driven tests (`packages/*/Scripting/Tests/Fixture/*.cs`) prove; no new test asserts a generated facade method, which calls the native the hand-written one did.

### ADR-0043 amended

Its facade bullet ("a facade stays hand-written C# over generated natives") and its rejected option ("Generated facades, from a public flag and nicer names") each gain an *Amended by* note (as ADR-0029 carries one): a facade is a hand-written part over a generated part; the generated part holds the natives whose public shape is the declaration's, each named by its `@facade`; the hand-written part holds what a description cannot say, as before. ADR-0029's amended facade bullet and `GLOSSARY.md`'s *Facade* entry say the same. No new ADR: the boundary does not move.

### Out of scope

- An `out`-to-return rule, properties, defaults, and any second doc command for public wording.
- `@facade` on anything but a native function; a facade in another binding's namespace.
- The assets family: nothing there is a pass-through, so no mark lands in `AssetsScripting.cpp`.
- `packages/FenrirScripting/src` and the Roslyn generator: a generated facade method is ordinary C# over the declaration.

## Decisions (from the grilling)

1. The candidate is built: it covers 43 of 127 facade members (36 exact, 7 dropping `in`), with the win in `Window` (11 of 13), the input facades (15 of 18) and `Renderer` (5 of 7).
2. The mark lives in the native's C++ doc comment, recorded by describe-api as `facade`, not in a `[natives]` table.
3. A generated facade method takes `T` where the declaration takes `in T`, as a fixed rule; everything else is as declared.
4. Its doc is the C++ doc verbatim, written as the declaration's is; no second doc command, no `<paramref>` conversion.
5. `out`-to-return, properties and defaults stay hand-written.
6. ADR-0043, ADR-0029 and the glossary are amended; no new ADR.
7. One file per facade class, `<generated>/<Class>.cs`, `public static partial class`.
8. The mark is `@facade Class.Method`, both parts always written; describe-api checks its form and that a native has at most one.
9. Iris.Bindings refuses a mark on a native taking an `internal-structs` type or a pointer, two marks of one class, method and parameter types, and a facade class named as a mirror, a bound type or the natives class.
10. The port moves the eleven richer facade docs into the C++ comments, adds the four missing `@return`s and drops the five cross-references; the ticket names them.
11. Two tickets, stacked: 01 the tools, cut from `span-declarations`; 02 the five ports, cut from 01.
12. The port is proved by the builds and the existing scripting tests; no new facade test.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | describe-api: `@facade` and the `facade` key, `SCHEMA.md`, a test; Iris.Bindings: the facade writer, the refusals, the fixture, `tests/Checks/Sdk`, README; `natives-description.md`; ADR-0043, ADR-0029 and glossary amendments | none |
| 02 | The 43 marks in five `*Scripting.cpp`, the doc merges, five hand-written classes made `partial` and trimmed, every binding regenerated | 01 |

Both touch generated outputs under `packages/*/Scripting/` and `packages/*/Managed/`, so they are a stack: 01's branch is cut from `span-declarations` (PR #161) and targets it until it merges; 02's from 01's.

## Done looks like

- `grep -c "@facade" packages/*/Scripting/src/*Scripting.cpp` finds 11, 15, 7, 5 and 5 in the window, input, physics, debug draw and renderer sources, and 0 in assets'.
- Each of `Fenrir.Window/Generated/Window.cs`, `Fenrir.Input/Generated/{Keyboard,Mouse,Gamepads}.cs`, `Fenrir.Physics/Generated/PhysicsWorld.cs`, `Fenrir.DebugDraw/Generated/DebugDraw.cs` and `Fenrir.Renderer/Generated/Renderer.cs` exists, and the hand-written file beside it is `partial` with no member that only forwards; `grep -c "=> [A-Za-z]*Native\." ` over the five hand-written files finds only members that pack, convert, reorder or default.
- `grep -c " in [A-Z]" ` over the generated facade parts finds none: no generated public method takes `in`.
- `fenrir-describe-api` and `fenrir-generate-bindings` are no-ops after each PR (CI's api-descriptions check).
- `dotnet test` for describe-api and Iris.Bindings pass with the new cases; the README documents `@facade`, the facade part, the `in` rule and the refusals.
- The msvc-x64-debug preset builds and `ctest -LE gl` passes locally; the other presets are CI's.
