# 04: `tools/describe-api`: headers to an API description

**What to build:** stage 1 of ADR-0016 (spec: "Stage 1: the API description"): the engine tool that reads a package's selected C++ headers and writes its committed, language-neutral API description. It knows nothing of Iris, C# or FenrirScripting.

- **The tool.** `tools/describe-api/`: a .NET console project using ClangSharp's libclang bindings (the `ClangSharp` and `libclang` NuGet packages; no LLVM install) as a header reader. Input: a selection file and the compile arguments it needs (include directories, defines, `-std=c++20`), which CMake passes from the package target. Output: `<Package>.api.json` beside the selection, written only when the content changes.
- **The selection** (`packages/<Package>/api/<Package>.api.toml`): the headers to read; the types to share, each with a role: `component`, `event`, `shared-struct` (plain data natives take or return by pointer), `bound-type` (described by layout only; every binding has its own); and nothing binding-specific.
- **The description** (`<Package>.api.json`, a documented schema in `tools/describe-api/SCHEMA.md`): `pointerSize: 8` and the alignment model; per type its C++ qualified name, size, alignment, role and doc comment; per field name, byte offset, kind (`i8..u64`, `f32`, `f64`, `bool`, `char16`, `enum`, `struct`, `array` with element kind and length, `pointer` as an opaque 8 bytes), the referenced type's name for enums and structs, its default value and doc comment; per enum its underlying kind, values and docs; per tagged union its tag field and, at the union's offset, each kind's name and type. Types are written in a deterministic order (dependencies first, then by name).
- **Defaults** are read from default member initialisers and recorded as values for: literals (including `1.0f`, `0.05f`, `true`), enum values, `std::numeric_limits<T>::infinity()`/`max()`/`lowest()`, a bound type's constructor with literal arguments (`Math::Vec3(0.5f)` → three equal components; `Math::Vec3(x, y, z)`; `Math::Quat(1, 0, 0, 0)` or the identity), and a nested struct's own defaults (`T{}` or no initialiser). Anything else fails the tool naming the type and field.
- **Tagged unions** are recognised by shape: a class whose non-static data members are one enum tag followed by one anonymous union, with no other data members. Anything else that is not plain data (another private member, a virtual function, a base class with members, a non-trivially-copyable member, a reference, a type the kinds cannot express) fails the tool with a diagnostic naming the type and member.
- **Build.** A CMake function `fenrir_describe_api(<package-target> <selection>)` adds the package to a root target `fenrir-describe-api`, not in `ALL`, that rebuilds the tool and runs it with the target's compile arguments. CI: a job that runs `fenrir-describe-api` and fails on a dirty tree (`git diff --exit-code`).
- **Tests.** The tool's own tests (xunit) run a fixture header through it: plain structs, nested structs, arrays, enums, a tagged union, bound types, every supported default form, and one failing case per diagnostic (an unknown default expression, a private member outside the union shape, a reference member). The expected JSON is committed and compared.
- **Docs.** `tools/describe-api/README.md` (purpose, selection format, schema pointer, how to run); `README.md`'s building section names the target; `docs/agents/`'s domain doc points at ADR-0016 for the vocabulary.

**Blocked by:** none.

**Touches:** `tools/describe-api/`, `CMakeLists.txt` (root), `cmake/`, `.github/workflows/`, `README.md`.

**Status:** ready-for-agent

- [ ] The fixture header produces the committed expected description byte for byte on Windows and Linux CI; each failing fixture fails with the documented diagnostic.
- [ ] Running the tool twice on unchanged headers rewrites nothing; `fenrir-describe-api` is not built by the default build.
- [ ] CI fails on a tree where a description is stale (proved once in the PR by a deliberately stale commit, then fixed).
- [ ] Nothing under `tools/describe-api` references Iris, C#, FenrirScripting or any Fenrir type by name beyond the fixture.
