# 01: Generated natives take spans, pinned by the SDK's generator, and the facades stop pinning

**What to build:** Iris.Bindings declares a pointer-plus-count pair as a span, the Roslyn generator pins it, and the four facades keep only docs, overloads and conversions (spec: all of it).

- **`packages/Iris/Tools/Iris.Bindings/src/Natives.cs`**: in `Bind`, the branch that today declares `{pointee}* {name}` for a pointer with a count after it (not `[in]`, not `[in,out]`, not `void*`) declares `ReadOnlySpan<{pointee}>` when the pointer is const and `Span<{pointee}>` when not, keeps the two wire entries (`void*`, `int`), and skips the count (`index++`), recording its name in a new `BoundNative.Folded` list so `CSharpWriter.Natives` writes no `<param>` for it (the `const char*` text case joins the same list). A `const char16_t*` with a count is refused (`ReadOnlySpan<char>` is text). `BoundNative.TakesText` becomes `NeedsSystem` (text or a span), so the class gets `using System;`.
- **`packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`**: a parameter whose type is `System.Span<T>` or `System.ReadOnlySpan<T>` with an unmanaged `T` other than the text case adds field types `T*` and `int`, the statement `fixed (T* __p = p)` and the arguments `__p, p.Length`. Anything else generic stays refused by the existing diagnostic.
- **Docs**: the README's natives paragraph says "a span" where it said "`T*` (a span C# fixes)" and names the two spellings; `packages/Iris/docs/natives-description.md` is unchanged (the description still records a pointer and an `i32`).
- **Regenerate**: the fixture's `FixtureNative.cs` (its span natives) and every `<Package>Native.cs`; the `tests/Checks/Sdk` compile proves the generator accepts them.
- **Facades**: `PhysicsWorld.cs`, `Assets.cs`, `DebugDraw.cs`, `Window.cs`: each `fixed` method becomes `=> Native.F(..., span)`, `unsafe` dropped where only the pinning needed it.
- **Tests** (`tests/BindingTests.cs`): the fixture's natives class declares `ReadOnlySpan<Vector3>` for its const span, `Span<T>` for its output span, no count parameter and no `<param>` for the count; a `const char16_t*` with a count is refused naming the parameter.

**Blocked by:** none

**Touches:** `packages/Iris/Tools/Iris.Bindings/`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/NativeFunctionGenerator.cs`, `packages/*/Managed/*/Generated/*Native.cs`, `packages/FenrirScripting/Managed/Fenrir.Scripting/Generated/Native.cs`, `packages/FenrirPhysics/Managed/Fenrir.Physics/PhysicsWorld.cs`, `packages/FenrirAssets/Managed/Fenrir.Assets/Assets.cs`, `packages/FenrirDebugDraw/Managed/Fenrir.DebugDraw/DebugDraw.cs`, `packages/FenrirWindow/Managed/Fenrir.Window/Window.cs`

**Status:** resolved

- [x] `dotnet test` for Iris.Bindings passes with the new cases; `fenrir-generate-bindings` is a no-op after the commit.
- [x] No `fixed (` remains in the four facades.
- [x] msvc-x64-debug builds and `ctest -LE gl` passes.

## Comments

Filed from the span-declarations grilling. Cut from `binding-defaults-03-bound-types`; its PR targets that branch until the stack merges.
