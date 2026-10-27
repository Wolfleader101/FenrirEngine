Status: built (#161)

# Span declarations: a generated native takes a span where C++ takes a pointer and a count

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidate 6 (span-taking generated declarations), grilled to five decisions after the binding-defaults stack (`.scratch/binding-defaults/`), on which this is stacked: both regenerate every binding and both change Iris.Bindings' `Natives.cs`.

The vocabulary is the repo's: *native function*, *facade*, *binding* from `GLOSSARY.md`; *module*, *interface*, *leverage*, *locality* from the codebase-design skill. No ADR: ADR-0043 (a facade stays hand-written over generated natives) and ADR-0044 (text and the pointer-plus-count rule) stand; the generated declaration's spelling of one case changes.

## Problem statement

Iris.Bindings declares a C++ pointer followed by a `std::int32_t` count as `T* name, int count` ("a span C# fixes", its README says), and the SDK's Roslyn generator (`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`) passes both through. So every facade that takes a span pins it by hand before the one call it makes: 21 `fixed` blocks across `PhysicsWorld.cs` (8), `Assets.cs` (10), `DebugDraw.cs` (2) and `Window.cs` (1), each `fixed (T* first = span)` then `Native.F(..., first, span.Length)`, and 13 `unsafe` modifiers that exist for them. The generator already does exactly this for text: a `ReadOnlySpan<char>` argument is encoded into a buffer and pinned inside the body it writes. The pinning is a module waiting to exist in the one place that writes every native's body.

The two "text-out" natives (`WindowNative.GetClipboard`, `AssetsNative`'s one) are a `char16_t*` plus a capacity: the same pair, with `char16_t` as the element.

## Solution

### Iris.Bindings declares the pair as a span

In `Natives.cs`, a pointer followed by a `std::int32_t` count (not a `const char*`, which stays `string`; not a pointer the doc marks `[in]`, `[out]` or `[in,out]`; not `void*`) is declared as `ReadOnlySpan<T>` when the pointee is const and `Span<T>` when it is not, named as the pointer is; the count parameter is folded into the span and gets no `<param>` doc line. `T` is the pointee's C# type as today (`char16_t` is `char`, so a `char16_t*` plus a capacity is `Span<char>`). The wire types stay `T*, int`: nothing crosses differently. A `Span`-taking class gets `using System;`, as a text-taking one does. The zeroing registration is untouched: a span was never an output.

### The Roslyn generator pins the span

`NativeFunctionGenerator` accepts a `Span<T>` or `ReadOnlySpan<T>` parameter of an unmanaged `T`, beside the `in`/`out` and text cases: the field takes `T*, int`, the body writes `fixed (T* __p = p)` (C# pins a span through its `GetPinnableReference`) and passes `__p, p.Length`. An empty span passes null and 0, which FenrirScripting's `SpanOf` makes an empty `std::span`. A `ReadOnlySpan<char>` stays text (UTF-8 encoded), so a `Span<char>` is the UTF-16 output buffer and a `ReadOnlySpan<char>` is text: the two cannot be confused because a `const char16_t*` input span does not occur and would be declared `ReadOnlySpan<char>` only by a native nothing writes; the spec records that a `const char16_t*` plus count is refused by Iris.Bindings with a diagnostic, so the rule stays one-to-one.

### The facades keep what a description cannot say

Each of the 21 methods becomes a pass-through keeping its docs, overloads, conversions and handle packing, and loses `unsafe` (ADR-0043: the facade holds overloads, generics, properties over outputs, constants and conversions). No facade method is deleted and none is generated.

### Out of scope

- `ref T` for a pointer the doc marks `[in,out]`: the same family, not pointer-plus-count, nothing in a facade pins one; a later candidate.
- The natives description and describe-api: unchanged, the pair is still recorded as a pointer and an `i32`.
- A `const char16_t*` plus count as `ReadOnlySpan<char>` text-in: refused, to keep `ReadOnlySpan<char>` meaning text.

## Decisions (from the grilling)

1. `const T*` plus count is `ReadOnlySpan<T>`, `T*` plus count is `Span<T>`, the count folded in; `const char*` plus count stays `string`; `char16_t*` plus count is `Span<char>` by the same rule, with no text-out kind.
2. The Roslyn generator pins a span inside the body, as it pins text, passing the pointer and the length.
3. The facades become pass-throughs with their docs, overloads and conversions, and lose `unsafe`; none is deleted or generated.
4. `ref T` for `[in,out]` is out of scope.
5. One spec, one ticket, one PR cut from `binding-defaults-03-bound-types`; the spec rides in that PR.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | Iris.Bindings `Natives.cs`, `CSharpWriter.cs`, README; the Roslyn generator; `natives-description.md`; every binding regenerated; the four facades | none |

## Done looks like

- `grep -c "fixed (" packages/*/Managed/*/*.cs` finds none in the four facades, and `grep -c unsafe` finds none they needed only for pinning.
- Every generated `<Package>Native.cs` declares `Span<T>`/`ReadOnlySpan<T>` where it declared `T*, int`; `fenrir-generate-bindings` is a no-op after the commit.
- Iris.Bindings' tests pass with the fixture's span natives (`tests/Checks/Sdk` compiles the fixture's class against the real generator); the msvc-x64-debug preset builds and `ctest -LE gl` passes.
