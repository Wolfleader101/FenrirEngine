# 06: Iris's binding generator

**What to build:** stage 2 of ADR-0016 (spec: "Stage 2: the C# binding"): the Iris tool that reads API descriptions and a binding's configuration and writes the C# mirrors, the Iris mappings, the extension's registration lists and the defaults test. It knows nothing of Fenrir: its tests run on a fixture description.

- **The tool.** `packages/Iris/Tools/<Name>` (a .NET console project; name it for what it does, e.g. `Iris.Bindings`), built and tested with Iris. Inputs: one or more `*.api.json` (the schema of `tools/describe-api/SCHEMA.md`, copied into Iris's docs as the input format it accepts, since Iris may leave this repository) and one binding configuration (`*.binding.toml`, format a detail): the C# namespace and contract assembly, the output directories, the C# type that stands for each bound type, and the exceptions to the PascalCase rule. Output is written only when the content changes.
- **C# mirrors**, one `partial struct` per described component, event and shared struct, into the configured `Generated/` directory: fields in order, PascalCase names (rule: first letter upper; configured exceptions), field initialisers from the recorded defaults (`= 1;`, `= 0.05f;`, `= float.PositiveInfinity;`, `= new(0.5f, 0.5f, 0.5f);`, `= MotionType.Dynamic;`, `= new();` for a nested struct) with an explicit `public T() {}`, `<summary>` from the docs, the component or event attribute with the derived name (`Namespace.Type`), `IComponent`/the event interface, and for a tagged union `[TaggedUnion]`, `[StructLayout(LayoutKind.Explicit)]`, `[FieldOffset(0)] private Tag _type;` and one `[FieldOffset(n)] private Kind _kind;` per kind, exactly what `TaggedUnionGenerator` consumes today. Enums are emitted with their underlying type and values. A pointer field becomes `ulong` (or the configured type) at its offset. A zero-field struct is an empty struct. Bound types are not emitted.
- **Iris mappings**, one C++ source into the configured directory: `template <> struct Iris::StructMapping<T>` per type with `typeName` the derived C# name and `fields = Iris::StructFields(Iris::Field<Kind>("Name", offset), ...)` from ticket 01, a `static_assert(sizeof(T) == N && alignof(T) == A, "...")` per type, and `Iris::EnumMapping` per enum. It includes the package headers the configuration names.
- **Registration lists**: a C++ source with one function returning the `components`, `events` and `sharedStructs` vectors (`MakeComponent<T>("Namespace.Type")`, `MakeReadOnlyComponent` where the selection marks a type read-only, `MakeEvent`, `MakeSharedStruct` from ticket 02).
- **The defaults test**: a C++ Catch2 source and a C# fixture class, in the configured test directories, with one case per type: C# `new T()` written into a buffer the test reads, compared with C++ `T{}` field by field through the mapping (bytes of each field at its offset; padding never compared), naming the field on failure.
- **Hand-written fields are an error**: the tool reads the hand-written parts of the contract directory and fails naming any instance field declared in a `partial` whose other part it generates.
- **Build.** `iris_generate_binding(<name> <config> <descriptions...>)` CMake function adding to a root target `fenrir-generate-bindings`, not in `ALL`; CI runs it and fails on a dirty tree, in the job ticket 04 added.
- **Tests.** The tool's tests run the fixture description (plain, nested, array, enum, tagged union, empty struct, pointer field, bound type, every default form) and compare the four outputs with committed expected files; the generated C# compiles against a stub `IComponent`; a fixture with a hand-written field fails naming it.
- **Docs.** The tool's README; Iris's README "Changes" entry; the input schema copied into `packages/Iris/docs/`.

**Blocked by:** 01 (`Field<T>`), 02 (`MakeSharedStruct`), 04 (the schema).

**Touches:** `packages/Iris/Tools/`, `packages/Iris/CMakeLists.txt`, `packages/Iris/README.md`, `packages/Iris/docs/`, `cmake/`, `.github/workflows/`.

**Status:** ready-for-agent

- [ ] The fixture description produces the four committed expected outputs byte for byte; a changed default in the fixture changes exactly the matching initialiser.
- [ ] The generated tagged-union storage is accepted by `TaggedUnionGenerator` with no diagnostic (compiled once against the SDK in the tool's tests).
- [ ] A hand-written field beside a generated mirror fails with a diagnostic naming the struct and field.
- [ ] `fenrir-generate-bindings` is not in the default build; CI's clean-tree check covers it.
