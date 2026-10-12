# 03: Spike: Ref<T> and generated field accessors

**What to build:** Prove the component access syntax before anything depends on it. `Ref<T>` is a `ref struct` wrapping a `T*`. A Roslyn source generator (`Fenrir.Scripting.Generators`) emits, for each `IComponent` struct, C# 14 extension properties on `Ref<T>` that return `ref` to each field. With these, `var t = entity.Get<Transform>(); t.Position.X += 1;` writes through to the pointed-to memory, and no `ref` is needed at the call site. Use native memory owned by the test (no ECS yet).

**Blocked by:** 02

**Status:** resolved

- [x] Writes through generated properties, including nested struct fields (`t.Position.X += 1`), are visible in native memory.
- [x] `Ref<T>` cannot be stored in a field or captured in a lambda (it's a compile error, shown by a negative compile test or documented).
- [x] The generator runs on a user's script assembly that only references the SDK.
- [x] If extension properties can't return `ref` as needed, record why, and switch the design to the accepted fallback `t.Value.Position.X += 1`. Update the spec either way.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Access). The user accepted the `t.Value` fallback in advance.

### Resolution

- **The spike succeeded; no fallback.** A C# 14 extension property on a `ref struct` receiver can return `ref`, and compound assignment on a nested field (`t.Position.X += 1`) writes through. It was proved first in a throwaway project, then built for real. The spec's Access section is updated to say so.
- **`Ref<T>`** (`Fenrir.Scripting`): `readonly unsafe ref struct Ref<T> where T : unmanaged, IComponent`, with `Value` (a `ref T`, which throws a clear `InvalidOperationException` when null), `IsNull`, and a public constructor over `T*`. `IComponent` is the marker interface.
- **Generator** (`Fenrir.Scripting.Generators`, netstandard2.0, an incremental generator against Roslyn 4.8): for each struct implementing `IComponent` it emits one `ref` extension property per public or internal instance field, as wide as the field is declared (an internal field never gets a public accessor). It handles nested and internal components, keyword-named fields, pointer fields (the accessors class becomes `unsafe`) and partial structs, including one that names `IComponent` only on a later part (emitted once). It reports a field shadowed by a `Ref<T>` member (`FENRIR0001`), and refuses a generic component (`FENRIR0002`), one nested in a private or protected type (`FENRIR0003`), one that is not unmanaged (`FENRIR0004`, an error, instead of a confusing failure inside generated code) and a `readonly struct` (`FENRIR0005`). Diagnostics are cached as plain positions, not syntax trees, so an edit does not regenerate everything.
- **The SDK:** one folder built by CMake (`<build>/fenrir-scripting/contract/`): the contract, `analyzers/Fenrir.Scripting.Generators.dll` and `Fenrir.Scripting.props`. The fixture script assembly now imports only that props file, with no project reference to the contract or generator sources, which is how the generator is shown to run on a script that only references the SDK.
- **Writes reach native memory:** the C++ test owns a struct and passes its address to the script. After `Nudge`, `Position.X`, `Position.Y`, `Scale` and `Flags` all hold the new values, a native write is seen by the next read, and a nested component, an internal component, a `Value`/`IsNull` clash and `@class`/`@event` fields all work.
- **Compile tests** (`Tests/CompileFail`, eleven CTest cases, each one small C# file built against the SDK). Misuse of `Ref<T>`, each failing with its own diagnostic and no other error: a class field (`CS8345`), a lambda capture (`CS9108`), `List<Ref<T>>` (`CS9244`), an async parameter (`CS4012`) and a local held across an `await` (`CS4007`). Generator behaviour: an internal field, a pointer field and `IComponent` on a later part all compile; a managed component is `FENRIR0004`; a readonly component has no accessor (`CS1061`). A control case compiles, so none of the failures is a broken setup. The harness was checked to fail on a wrong expected code and on a case that compiles when it should not, and it rejects any extra error.
- **Not done here:** getting a `Ref<T>` from an entity (`entity.Get<T>()`) is ticket 06; this spike uses native memory the test owns. Packaging the SDK for use outside this build tree (a NuGet package) is future work.
