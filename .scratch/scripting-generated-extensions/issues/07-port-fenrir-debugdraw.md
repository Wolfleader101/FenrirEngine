# 07: Port `Fenrir.DebugDraw` onto the generated pipeline

**What to build:** DebugDraw's native declarations and registration are generated, its two wire option structs are described binding-side and mirrored `internal`, and `DebugDraw` is a hand-written facade over typed natives (spec: "Each port"; ADR-0043, ADR-0044). It carries the step that closes the spec, for whichever port lands last.

- **Binding-owned selection** (`packages/FenrirDebugDraw/Scripting/api/FenrirDebugDrawScripting.api.toml`, header a new `src/ScriptOptions.hpp` holding today's `ScriptShapeOptions` and `ScriptTextOptions`): both as `shared-struct`; `DepthMode` described through the shape options (its field becomes the enum, not the widened `std::uint32_t`: the padding worry that widened it is gone, since the description records the offsets). `RelativeTo` stays the entity's 32-bit value (a `Fenrir::ScriptQueries`-style `EntityValue` bound to C# `Entity`, or an `Entity` field under `[natives] types`; the ticket follows physics). FenrirDebugDraw's own headers select nothing: C# holds no component or resource of the package.
- **Binding** (`FenrirDebugDraw.binding.toml`): `[uses]` the math binding (`Colour`, `Vector3`, `Quaternion`, `Matrix4x4`, `Bounds`); `internal-structs` the two option structs, `[names]` them `NativeShapeOptions` and `NativeTextOptions`; `[natives] sources = ["../src/DebugDrawScripting.cpp"]`, class `DebugDrawNative`; `[registration]` (shared structs and natives); `[tests]` (natives only: the defaults test still covers the two internal structs).
- **Natives:** each takes `std::string_view` for the group and text (kept), `const Colour*`, `const Math::Vec3*`, `const Math::AABB*`, `const Math::Mat4*` as it does (by pointer, `in` from C#), and the options by pointer; `KeyedMessage`'s key stays `std::int64_t`. The warn-once record moves to a `DebugDrawScripting` state class.
- **Facade:** `DebugDraw` rewritten over the generated `DebugDrawNative`: every overload, `ShapeOptions`/`TextOptions` (with `string? Group`) and the `Shape(options, out group)` conversion to the internal mirror kept; `NativeShapeOptions`/`NativeTextOptions` constructors become partial parts of the generated internal mirrors.
- **Deleted:** the hand-written internal structs' fields, `DebugDrawNative`'s declarations, the `extension.functions` list, the `static_assert`s on sizes (the description holds them).
- **Closing the spec:** remove the "(the extensions of input, window, assets, renderer and debug draw still declare both by hand)" note under *Native function* in `GLOSSARY.md`; the commit that marks this ticket resolved deletes `.scratch/scripting-generated-extensions/`.

**Blocked by:** 03 (`Fenrir.Math` through a math binding, and physics's `Aabb` becomes `Bounds`)

**Touches:** `packages/FenrirDebugDraw/Managed/`, `packages/FenrirDebugDraw/Scripting/`, `GLOSSARY.md`

**Status:** resolved

- [x] `fenrir-describe-api` and `fenrir-generate-bindings` leave the tree clean; no `[NativeFunction]` or `NativeFunction::Make` remains under the package.
- [x] The existing debug-draw extension tests pass unchanged in behaviour (items, shapes, text, groups, no-op rules); the generated natives and defaults tests run in the test target. (Neither has a case for this binding, so it has no `[tests]`: see Comments.)
- [x] `DebugDraw.Line(a, b, Colour.Red)`, `DebugDraw.Box(bounds, colour, new ShapeOptions { Group = "nav", Depth = DepthMode.OnTop })` read exactly as before from a script.
- [x] Only the ticket's own Touches change. If 06 lands after this ticket, the glossary note and the directory deletion move to 06's closing commit instead.

## Comments

**The selection's names.** `src/ScriptOptions.hpp` holds `Fenrir::ScriptOptions::ShapeOptions` and `TextOptions` (today's
`ScriptShapeOptions` and `ScriptTextOptions`, renamed by their namespace) and, after physics's `ScriptQueries`, an
`EntityValue` for the relativeTo entity, bound to C# `Entity` in `[bound-types]` and mapped by hand in the extension's own
`src/BoundMappings.hpp`. Its default is written `EntityValue(0xFFFFFFFFu)`: describe-api records a bound type's
constructor with literal arguments only, not `Entity::Null`. The selection also names `Colour` and `Math::AABB`
(`shared-struct`, the math description's role) and the four glm types the natives take (`bound-type`), since a native's
pointee must be described; `DepthMode` is described through the shape options' field, now the enum, so the hand-written
`DepthMode` in `Options.cs` became the generated `Generated/DepthMode.cs` (same name, values and `byte`).

**The option structs are not registered (settles the ticket against ticket 02).** Ticket 02 said an `internal-structs`
mirror's layout "is still checked when the extension registers it". It is not: Iris checks a shared struct against a
public C# struct only (`Iris.Bridge/ComponentExports.cs`), so registering the two internal mirrors made every script load
fail with "No public struct with that name was found". Both are in `[registration] by-hand` and registered by nothing.
Their layout is checked this way instead: the C# mirror and the C++ mappings are generated from one description, the
mappings `static_assert` C++'s size and alignment against it, and the extension's tests set every field from C#
(duration, relativeTo, thickness, depth, segments; text duration, relativeTo, size) and read it back through what the
call draws against the same call made in C++. Nothing checks the C# mirror's layout against C++ at run time. A follow-up
for Iris or Iris.Bindings: either Iris accepts an internal shared struct, or `internal-structs` implies not registered
and the README's sentence changes. No other port's ticket mirrors a struct internal, so only this binding meets it
today.

**No `[tests]`.** Neither generated test has a case here: the internal mirrors get no defaults case, `DepthMode` (an enum)
gets none, and no native has an output, so the natives test has nothing to call. With only internal registered types,
Iris.Bindings' natives test also crashes (`CSharpWriter.NativesTest` takes `binding.Registered.First()` as the type whose
assembly it reflects on, and there is none); the empty defaults test generates, but only as unused helpers. So the
binding has no `[tests]`, and the test target and fixture are unchanged. The crash is a second Iris.Bindings follow-up.

**`Grid`'s native takes `cellSize` before `cells`.** Iris.Bindings reads any pointer followed by a `std::int32_t` as a
span, so `const Math::Quat* rotation, std::int32_t cells` declared `Quaternion* rotation`. The native's two parameters
swapped places (a comment there says why); `DebugDraw.Grid` keeps its order.

**The warn-once record** is the `DebugDrawScripting` state (`src/DebugDrawScriptingState.hpp`), with `TargetFor` as a
member (a free function taking the context first would be read as a native). Warnings stay keyed by native call site and
reason, now per extension rather than per process; each no-op test has its own app and log, so the tests are unchanged.
The facade's remarks and the public header say "once" instead of "once per process".

**The spec's closing step moves to 06.** Ticket 06 (renderer) lands after this one, so the `GLOSSARY.md` note under
*Native function* (which still lists debug draw) and this directory stay; 06's closing commit removes both.
