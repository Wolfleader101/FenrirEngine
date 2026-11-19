Status: ready-for-agent

# FenrirTypes: a run-time type registry filled from generated registrations, the API description widened to say what files need, and the generator that writes them

Origin: the serialisation map (`.scratch/fenrir-serialisation/map.md`), gathering ticket 08 (what the engine can describe today) and ticket 09 (where type descriptions come from), with the kinds and doc commands decided in tickets 13, 18 and 19, settled in grilling sessions on 2026-11-19. It is the first of the map's six specs: the format spec (`.scratch/fenrir-serialisation-format/spec.md`) and the rest build on it. It extends the generated API pipeline of ADR-0042 to ADR-0046 and cites rather than restates it.

| ADR | Decision |
|---|---|
| 0074 | Types read from and written to files are described by the API description and registered with FenrirTypes from generated code |
| 0042 | Packages share plain data through a generated, language-neutral API description |
| 0043 | Native resources and facades move onto the generated API pipeline |
| 0044 | Natives pass enums, text and wrappers by value, and bindings use other bindings' mirrors |
| 0046 | A scripting extension's shape is generated and shared: a package writes its natives and its state |
| 0075 | A data asset is a game-defined plain struct in the asset store, with single inheritance and polymorphism through references |
| 0078 | A scene file is authoring data holding described components by entity id and described scene resources, and is read once |
| 0082 | Editor data rides on editor-only components, and brushes, wiring and world data stay outside the engine's scene file |
| 0084 | A project file layers each package's config by type name, and import sidecars hold options only |

ADR-0075 narrows ADR-0074's open slot to its reference form; ADR-0078, ADR-0082 and ADR-0084 each add a kind, a role or a doc command to the description, which this spec builds.

## Prerequisite

None of the other serialisation specs: this one goes first. The engine as it stands: `tools/describe-api` and Iris.Bindings on their committed descriptions, the .NET SDK at development time (as for C# scripting), and CI's `api-descriptions.yml` on its Windows runner, where configuring with the renderer on must succeed (SDL3 configures there without a display).

## Problem Statement

The engine describes its shared plain data once, at build time: `tools/describe-api` reads the headers each of eleven packages selects and writes a committed `<Package>.api.json` recording every field's name, offset, kind, default and doc comment, with roles `component`, `event`, `scene-resource`, `app-resource`, `shared-struct`, `bound-type` and `enum`, and kinds that are integers, floats, `bool`, `char16`, `enum`, nested `struct`, fixed `array` and opaque `pointer`. It refuses references, virtual functions, bases with data members and anything not trivially copyable, so `std::string`, `std::vector`, `std::optional` and `std::filesystem::path` cannot appear. Only generators read the description: the ordinary build never runs the tool, CI regenerates on pull requests and fails on a dirty tree, but configures with the renderer off, so FenrirWindow's and FenrirRenderer's descriptions are never checked.

At run time nothing knows a type's fields. `ComponentType` names a native or dynamic component by size and alignment and carries no name; the one name-to-component table is FenrirScripting's `MakeComponent<T>("Fenrir.Physics.RigidBody")`, present only when scripting is installed and spelled in the binding's C# namespace; scene resources are reached by `type_index` alone; `Name` and `BoneName` are `char[256]` described as 256 bytes of `u8`; `Fenrir::Entity` is a 16-byte nested struct of a `u32` and a `pointer`; an `AssetHandle<T>` is a bound type recorded by layout only, nine specialisations named one by one. The six `Install` configs (`WindowConfig`, `RendererConfig`, `PhysicsConfig`, `AnimationConfig`, `AssetsConfig`, `ScriptingConfig`) hold strings, paths and vectors and are described nowhere.

A scene file, a data asset or a project file needs the opposite: a type found by a name a file spells, its fields walked without the C++ type in hand, strings and lists in game-defined types, a version and former names to read old files, and metadata an inspector shows. ADR-0074 keeps the header as the one source and the description as the one record, and adds a generated registration per package that fills a run-time registry.

## Solution

A new package, **FenrirTypes**, below FenrirMath and FenrirECS, depending on nothing but the standard library; `tools/describe-api` widened; a new tool, `tools/generate-type-registry`, beside it; one CMake module and one CI step.

### FenrirTypes: kinds, descriptions and the registry

- **Kinds** (ADR-0074, 0064, 0070): the description's kinds today plus `string` (`std::string`), `list` (`std::vector<T>`, element of any kind the role allows, `list` included), `optional` (`std::optional<T>`, any kind but `optional`), `reference` (`AssetHandle<T>`, with the target type's canonical name), `entity` (`Fenrir::Entity`, 16 bytes), `fixed-string` (`char[N]`, written as text, read back cut at N-1 bytes on a UTF-8 boundary) and `path` (`std::filesystem::path`). `reference`, `entity` and `fixed-string` are fixed-layout and allowed in every role; `string`, `list`, `optional` and `path` are owning and allowed in the `data-asset` and `settings` roles only.
- **`TypeDescription`**: one static object per described type, generated: canonical name, role, `version` (default 1), `formerly` names, `transient` and `editorOnly` flags, size and alignment, `Construct(void*)` and `Destroy(void*)`, the base type's name for a `data-asset`, doc text, and its fields as `FieldInfo` (name, kind and its element or target type, offset for fixed-layout types, former names, doc, metadata: `range`, `step`, `display`, `group`, `readonly`, `hidden`, `multiline`, `units`). Enums carry their enumerators.
- **One visitor interface** (ADR-0074): `TypeDescription::Visit(void* instance, FieldVisitor&) const` pushes each field once as `(const FieldInfo&, ValueRef)`; a `ValueRef` gives typed access per kind (scalars, text, a list's size, resize and element, an optional's presence and emplace, a reference's two fields, an entity) in const and mutable forms, and a nested struct recurses through its own description. Fixed-layout types visit through a generated table of offsets; owning types through a generated function that walks the base's fields first (ADR-0075). A caller cannot tell which, and nothing else reads offsets.
- **`TypeRegistry`**: register a description, find by canonical or former name (reporting the current name), iterate by role, `IsA(type, base)` over the base chain, and **attachments**: a value keyed by a tag type hung on a registered type (`Attach<Tag>(name, value)`, `Attachment<Tag>(name)`), so packages above FenrirTypes record their own facts about a type without FenrirTypes naming them. Registering a second type under one name fails with a diagnostic naming both packages.
- **`Describe<T>`**: a compile-time trait whose specialisation, declared in each package's generated header, yields the same `TypeDescription` the registration adds, so `Project::Get<T>()` (the dev-time spec) fills a config through `T`'s visitor before any `Install` and without the registry (ADR-0084).
- **Upgrader slots** (ADR-0077): the registry holds no tree; an upgrader list is an attachment the format spec's `FenrirSerialisation` defines and hangs per type, keyed by its own tag, and the registration leaves that slot empty.
- **The registry's home**: `App` makes one `TypeRegistry` as an app resource in its constructor and registers the packages beneath `Install` (FenrirMath, FenrirECS, FenrirScene, FenrirApp); every `X::Install(App&, XConfig)` registers its package's types first. FenrirScripting fills C# types at admission (the C# spec).
- **Attachments the engine hangs**: FenrirECS attaches a `ComponentType` per `component`-role type and offers `ComponentType::ForName(const TypeRegistry&, std::string_view)`; FenrirScene attaches type-erased find, add and remove over a `Scene&` per `scene-resource`, FenrirApp the same over an `App&` per `app-resource`. The generated registration does the attaching, since it is compiled in the package that knows both the type and its role.

### describe-api widened

- **Roles**: `data-asset` (ADR-0075: may extend one other `data-asset` type, recorded as `base`; data members in bases allowed here only; no virtual functions) and `settings` (ADR-0084: an `Install` config or an importer's options type). Components, events, resources and shared structs keep every refusal they have; a `string`, `list`, `optional` or `path` in one is refused naming the type, member and role.
- **Kinds**: as above. The tool recognises `Fenrir::Entity`, `Fenrir::AssetHandle<T>`, `std::string`, `std::vector`, `std::optional` and `std::filesystem::path` by qualified name; `char[N]` becomes `fixed-string` (`std::uint8_t[N]` stays an `array` of `u8`). A `list` and an `optional` reuse the array's element keys. `Fenrir::Entity` stays a `shared-struct` in FenrirECS's selection for the binding's mirror, and the other selections drop it from `private-members`.
- **Open slot** (ADR-0074 as narrowed by ADR-0075): a `reference` whose target is a `data-asset` type accepts a file of any type extending it; the check is `IsA` at load (the data-assets spec). Nothing else is recorded.
- **Doc commands**, split out of the doc text as the natives' `@param` and `@facade` are: on a type, `@version N`, `@formerly Old::Name` (repeatable), `@transient` (ADR-0078), `@editor-only` (ADR-0082); on a field, `@formerly oldName`, `@range min max`, `@step s`, `@display text`, `@group text`, `@readonly`, `@hidden`, `@multiline`, `@units text`. A command on the wrong subject, a malformed one or `@version` below 1 fails the tool naming it. The description records `version`, `formerly`, `transient`, `editorOnly` and a per-field `metadata` object.
- **Bound types record their components**: the scalar fields the tool already reads to match constructor arguments, with offsets, so a visitor presents a `Vec3` as three `f32` and a `Mat4` as sixteen; how a file spells them is the format spec's.
- **Defaults**: a scalar's default may be any constant expression libclang evaluates (`DebugValidationByDefault`), as an empty enum's already may; a `string` or `path` default is its literal; a `list`, `optional` and `reference` default is empty; `fixed-string` and `entity` default to empty.
- **Selections**: `[registration]` is accepted in a `.api.toml` and left to the generator. Every package with an `Install` config adds it under `settings`: `WindowConfig`, `RendererConfig`, `PhysicsConfig` (its `collisionLayers` a `list`), `AnimationConfig`, `AssetsConfig` (its `root` a `path`) and `ScriptingConfig`, whose `extensions` is marked `@transient` and whose `.feproj` doc comment is corrected to the project file (ADR-0084). The fixture, the format document and Iris's copy of it gain every addition.
- **Iris.Bindings** keeps generating: it reads `entity` as the built-in binding's `Entity`, `fixed-string` as the fixed buffer it made from `array` of `u8`, `reference` as the bound handle it already names, skips `data-asset` and `settings` types and every owning kind (the C# spec takes them up), and fails naming an owning kind it meets elsewhere.

### generate-type-registry

- **A .NET tool** beside describe-api, reading a package's `.api.json` and the `[registration]` table of its `.api.toml`: `header` (a generated public header beside the package's headers, declaring the registration function and each `Describe<T>` specialisation), `source` (a generated source beside the package's sources, defining them), `function` (`Fenrir::Generated::Register<Package>Types(TypeRegistry&)`) and `includes` (the package headers the source needs). Written only when the content changes, the same bytes on every platform, `// Generated ... do not edit` first, as the binding's outputs are.
- **What it writes**: per type its `TypeDescription`, fields and metadata as static data; a table-driven visitor for every fixed-layout type (`component`, `event`, `scene-resource`, `app-resource`, `shared-struct`, `bound-type`, `nested`); a generated visitor function for every `data-asset` and `settings` type, base first; `Construct` and `Destroy`; doc text compiled in (stripping it is the cooking effort's, ADR-0085); the attachments by role; and the registration function adding each type. An `enum` registers its enumerators. The tool fails naming the type when the description holds a kind or role it does not know.
- **Build**: `fenrir_generate_type_registry(<target> api/<Package>.api.toml)` in a CMake module beside `FenrirDescribeApi.cmake` joins one `fenrir-generate-type-registry` target the ordinary build never runs; the package compiles the generated source and links `FenrirTypes`. A game runs the same function on its own `.api.toml` (ADR-0074).
- **CI**: `api-descriptions.yml` regenerates descriptions, bindings and registrations in that order, configured with `FENRIR_ENABLE_RENDERER=ON`, and fails on a dirty tree, so FenrirWindow's and FenrirRenderer's descriptions are checked for the first time; the README's description paragraph names the third target.

### Testing

- **describe-api** keeps its shape: the fixture header gains one of everything new (a `data-asset` extending another, a `settings` type with a `path` and a `list`, an `entity`, a `fixed-string`, a `reference`, every doc command) and its committed `Fixture.api.json` is compared byte for byte; `Failing.hpp` gains a string in a component, a data member in a component's base, a malformed command and a command on the wrong subject, each expecting its diagnostic. Iris.Bindings' fixture copy is regenerated and its tests pass unchanged where nothing it mirrors moved.
- **generate-type-registry** (xunit, as describe-api's): the describe-api fixture's description generates a committed header and source compared byte for byte, and a second run rewrites nothing; a description with an unknown kind fails naming the type.
- **FenrirTypes** (Catch2, as FenrirECS's `ComponentTypes.cpp` and `DynamicComponents.cpp`): hand-written descriptions first (ticket 01), then the fixture's generated registration compiled against the fixture header: register, find by canonical and former name, `IsA` over a two-step chain, attachments, duplicate names refused, and a recording `FieldVisitor` that sees every field once with the kind, offset or value the header says, for a fixed-layout type, a bound type's components, an owning type with a base, a `list` of nested structs resized and an `optional` emplaced. A test package with its own `.api.toml` and `[registration]` runs the real tools and is under CI's check, as the describe-api fixture is.
- **Per package**, a generated test that the registration's defaults equal `T{}` field by field, the shape of the binding's `<Package>DefaultsTests.cpp`, and in FenrirApp's tests that a headless `App` has every installed package's types by name, `ComponentType::ForName` finds `Fenrir::Transform`, and `Describe<RendererConfig>` is the registered description.

## Tickets

| # | Ticket | Blocked by |
|---|---|---|
| 01 | FenrirTypes: kinds, `TypeDescription`, `FieldInfo`, `FieldVisitor` and `ValueRef`, `TypeRegistry` with attachments, `Describe<T>`, on hand-written descriptions | — |
| 02 | describe-api: type and field doc commands, `[registration]` accepted, bound-type components, constant-expression defaults | — |
| 03 | describe-api: `entity`, `fixed-string` and `reference` kinds; Iris.Bindings reads them; every description and binding regenerated | 02 |
| 04 | describe-api: `data-asset` and `settings` roles, the owning kinds, the base type, the role refusals; Iris.Bindings skips them | 03 |
| 05 | generate-type-registry: the tool, the CMake module and target, table-driven visitors for fixed-layout types, against the fixture | 01, 04 |
| 06 | Generated visitors for owning types and base chains, attachments by role, the FenrirTypes test package through the real tools | 05 |
| 07 | Every package's registration committed and compiled in; `App` owns the registry; each `Install` registers | 06 |
| 08 | `ComponentType::ForName` and the resource doors as attachments in FenrirECS, FenrirScene and FenrirApp | 07 |
| 09 | The six configs join their selections under `settings`, `ScriptingConfig::extensions` `@transient`, `Describe<T>` for each | 07 |
| 10 | CI's check gains `fenrir-generate-type-registry` and runs with the renderer on; README and tool READMEs | 09 |

Tickets 02, 03 and 04 all edit `tools/describe-api/` and its fixture, so they stack in that order; 01 touches only the new `packages/FenrirTypes/` and runs beside them off the base. Tickets 05 and 06 both write `tools/generate-type-registry/` and stack after 04 and 01. Tickets 07, 08 and 09 each touch several packages' `CMakeLists.txt`, selections and generated files, so they stack after 06; 10 edits the workflow and READMEs last.

## Out of scope

The value tree, the JSON5 reader and writer, the bridge from a visited value to a tree, how defaults are omitted and bound types spelled, upgraders' function type and door, diagnostics and the binary tree encoding (the format spec); `AssetKind::Data`, the envelope, the `IsA` check at load, references and the store's doors (the data-assets spec); `EntityId`, `Transient`, `UnknownComponents`, `Instance` and the scene doors (the scenes spec); C# components and data asset types entering the registry at admission, the C# generator, codecs and `Iris.Bindings` emitting data asset classes (the C# spec); `Project::Load`, `Get<T>`, layers, `--project` and the two options types (the dev-time spec). A `map` kind (ADR-0075 left it to the format, which recorded none); a by-value open slot (ADR-0075); stable ids per type or field; a hand-filled registry or registration macros; stripping doc text, `@transient` and `@editor-only` types (the cooking effort); C++26 annotations replacing the doc commands; prebuilt tool binaries for games without the .NET SDK; `@input` and `@output` commands (the map's Entity wiring fog); describing behaviour-carrying resources (`PhysicsWorld`, `SkeletonPoses`, `Animators`, `DebugDraw`).

## Gaps settled here

- The registry's attachments (a value keyed by a tag type per registered type) are how `ComponentType`, the scene and app resource doors and the upgrader list reach the registry from packages above FenrirTypes; ADR-0074 names the facts, not the mechanism.
- `App`'s constructor makes the `TypeRegistry` app resource and registers FenrirMath, FenrirECS, FenrirScene and FenrirApp, the packages that have no `Install`; every `Install` registers its own package first.
- A bound type's description gains its components (the scalars the tool already reads for constructor matching) so a visitor can present a `Vec3` or `Mat4`; nothing in the ADRs said how a bound type is walked.
- A `list` element may itself be a `list`; an `optional` may not wrap an `optional`; a `string`, `path` and `fixed-string` default is its literal and a `list`, `optional`, `reference` and `entity` default is empty.
- A scalar's default may be any constant expression libclang evaluates, so `RendererConfig::debugValidation`'s named constant describes without a spelling change (ADR-0043 changed spellings instead).
- `[registration]` names `header`, `source`, `function` and `includes`; the header is public so `Describe<T>` is visible to a game calling `Project::Get<T>()`.
- Iris.Bindings skips `data-asset` and `settings` types and the owning kinds until the C# spec, rather than failing on them, so the dirty-tree check passes between the two specs.
- The tool recognises `Fenrir::Entity` and `Fenrir::AssetHandle<T>` by qualified name, as it will the standard library's types; it stays binding-agnostic but not engine-agnostic.
