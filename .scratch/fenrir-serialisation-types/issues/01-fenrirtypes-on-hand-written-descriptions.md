# 01: FenrirTypes: kinds, descriptions, the visitor and the registry, on hand-written descriptions

**What to build:** a new package, `FenrirTypes`, that holds a run-time description of a type (its canonical name, role, version, former names, fields with kinds, defaults, docs and metadata), one visitor interface that walks an instance's fields without the C++ type in hand, and a registry that finds a type by its canonical or former name and answers is-a over base chains, as ADR-0074 decides. Nothing generates descriptions yet (tickets 05 and 06): this ticket's tests write them by hand, so the package's shape is settled before a generator targets it.

- **The package:** `packages/FenrirTypes/`, below FenrirMath and FenrirECS, linking nothing but the standard library (ADR-0074 "FenrirTypes knows no file format and no language"); `FENRIR_ENABLE_*` has no switch for it, it always builds. Namespace `Fenrir`.
- **Kinds:** an enum of the description's kinds, the plain ones (`i8` to `u64`, `f32`, `f64`, `bool`, `char16`, `enum`, `struct`, `array`, `pointer`) plus `string`, `list`, `optional`, `reference`, `entity`, `fixed-string` and `path` (the types spec, "Kinds"); a kind is fixed-layout or owning, and the `data-asset` and `settings` roles alone admit owning kinds.
- **`TypeDescription`** and **`FieldInfo`**: static, immutable data a package will hold once per described type: canonical name, role, `version` (1 by default), former names, `transient` and `editorOnly`, size and alignment, `Construct(void*)` and `Destroy(void*)`, the base type's name for a `data-asset`, doc text, enumerators for an enum, and per field the name, kind and its element or target type, offset for fixed-layout types, former names, doc and the metadata vocabulary (`range`, `step`, `display`, `group`, `readonly`, `hidden`, `multiline`, `units`).
- **The visitor:** `TypeDescription::Visit(void* instance, FieldVisitor&) const` pushes each field once as `(const FieldInfo&, ValueRef)`; `ValueRef` gives typed access per kind (scalars and enums, text, a list's size, resize and element, an optional's presence and emplace, a reference's two fields, an entity) in const and mutable forms, and a nested struct recurses through its own description. In this ticket the table-driven form over offsets is the one that exists, built from a `FieldInfo` list; the generated owning form arrives with ticket 06 behind the same interface, and a caller cannot tell which it has.
- **`TypeRegistry`:** register a description (a second type under one name fails with a diagnostic naming both packages' types), find by canonical or former name reporting the current name, iterate by role, `IsA(type, base)` over the base chain, and attachments: `Attach<Tag>(name, value)` and `Attachment<Tag>(name)` hang a value keyed by a tag type on a registered type, so packages above FenrirTypes (the ECS, the scene, the serialiser) record their facts about a type without FenrirTypes naming them. The upgrader slot is one such attachment, left empty here.
- **`Describe<T>`:** the compile-time trait a package's generated header will specialise to yield the same `TypeDescription` its registration adds; declared here, specialised by hand in the tests.
- **Diagnostics** go through a function the caller gives (a `std::function<void(std::string_view)>` or the logger interface FenrirCore already has, whichever keeps FenrirTypes free of FenrirLogger); the registry never throws.

**Blocked by:** none.

**Touches:** `packages/FenrirTypes/` (new: `CMakeLists.txt`, `include/FenrirTypes/`, `src/`, `Tests/`), the root `CMakeLists.txt` (adding the package), `README.md` (the package list)

**Status:** resolved

- [x] `FenrirTypes` builds on msvc-x64-debug and links into a Catch2 test executable through `fenrir_add_catch2()` with no other engine package.
- [x] A hand-written description of a fixed-layout struct (a `Transform`-shaped test type) registers, is found by canonical and by a former name with the current name reported, and a recording `FieldVisitor` sees every field once with the kind, offset and value the struct holds; a mutable visit writes a field back.
- [x] A two-step base chain (`Sword` extends `WeaponData` extends nothing) answers `IsA` both ways correctly; an unregistered base is a registration diagnostic.
- [x] Registering two types under one canonical name fails with a diagnostic naming both, and the first stays.
- [x] An attachment keyed by a test tag type is hung on a type and read back; a missing attachment reads as absent, not as an error.
- [x] `Describe<T>` for a test type returns the description the test registered, by identity.
- [x] `PublicHeaders` passes: every header under `packages/FenrirTypes/include` compiles alone.
