Status: ready-for-agent

# Binding defaults: a package binding states only what varies

Origin: the architecture review of `dotnet-scripting` against `main` (2026-10-26), candidate 3 ("inherit binding defaults and bound types through uses", rated Strong), grilled to twelve decisions in one session. Candidate 6 (span-taking generated declarations) is a stacked follow-up with its own grilling and spec, not part of this one.

The vocabulary is the repo's: *binding*, *API description*, *bound type*, *native function*, *facade* from `GLOSSARY.md`; *module*, *interface*, *seam*, *deep*, *shallow*, *locality*, *leverage* from the codebase-design skill. No new ADR: ADR-0042 (defaults never come from the tool) and ADR-0044 (`[uses]`) each gain one sentence, written by ticket 02 and 03.

## Problem statement

Iris.Bindings (`packages/Iris/Tools/Iris.Bindings`) turns API descriptions and a per-binding `*.binding.toml` into C# mirrors, Iris mappings and native registrations. The tool is engine-neutral (ADR-0042): every SDK name it emits comes from the configuration. The eight configurations (`packages/{FenrirAssets,FenrirDebugDraw,FenrirInput,FenrirPhysics,FenrirRenderer,FenrirWindow}/Scripting/api/*.binding.toml`, `packages/FenrirScripting/api/{FenrirScripting,FenrirMath}.binding.toml`) show the cost: the configuration's interface is as wide as its implementation.

- **About fifteen keys hold the same FenrirScripting constant in seven or eight of eight files**: `component-attribute`, `component-interface`, `read-only-component-interface`, `event-attribute`, `event-interface`, `tagged-union-attribute`, `extension`, `make-component`, `make-read-only-component`, `make-event`, `make-shared-struct`, `context`, `attribute`, `make`, `zero-outputs`, plus the three resource names where a binding has resources, the three `FenrirScripting/*.hpp` registration includes, `usings = ["Fenrir.Scripting"]`, and `[tests] component = "Fenrir::ScriptingProbe::Component(app.scripting)"` in all seven that have a test. A rename in the SDK is an edit to eight files.
- **Every key is required** (`reader.String` in `BindingConfig.cs`), so FenrirMath's and FenrirAssets's files carry six component and event names under the comment "None of these is used: the contract has no components or events". The resource keys are already optional and demanded only when a resource is mirrored (`CheckResources`); the component, event and union keys are not.
- **`uses` imports another binding's mirrors but not its `[bound-types]`**: Math's five glm types are restated in six files, FenrirAssets's three asset handles in Renderer, and `Fenrir::EntityValue = { type = "Entity", constructor = ["value"], wrapper = "u32" }` in Physics, Renderer and DebugDraw with no binding owning it (FenrirScripting's description is FenrirECS's, which does not select it). Each of those three also includes `FenrirScripting/EntityValueMapping.hpp` by hand. ADR-0044 introduced `[uses]` to avoid "dozens of hand-kept lines"; nine restated bound types are the lines it left.
- **`[natives]` is read by two tools**: describe-api (`tools/describe-api/src/Natives.cs`) reads `sources` and `context` and ignores the rest; Iris.Bindings reads `class`, `attribute`, `make`, `zero-outputs`, `types` and accepts the other two as known. `context` is the one SDK constant describe-api needs.

Why defaults cannot simply flow through `uses`: every package binding uses FenrirMath's, which has no `[registration]` and no `[natives]`, so it holds none of those constants; the one binding that holds all of them, FenrirScripting's, is used by nobody, and Physics cannot use it as the code stands (Physics selects `Fenrir::Transform` and `Fenrir::Entity` as bound types while FenrirScripting mirrors them, and `CheckUses` refuses that). Mirrors flow one way through the dependency graph; SDK names belong to the SDK.

## Solution

Three changes to Iris.Bindings (one to describe-api), each adopted across the eight bindings and the tool's fixture in the same PR, so every PR's `fenrir-generate-bindings` is a no-op and CI's api-descriptions check proves it.

### Keys required only when something needs them

Every SDK key becomes optional-until-needed, the way the resource keys already are: the component attribute and interface are demanded when a component is mirrored, the read-only component interface when one is registered read-only, the event pair when an event is mirrored, `tagged-union-attribute` when a tagged union is, each `make-*` factory when the generated registration lists that kind, and `[natives]` `attribute`, `make`, `zero-outputs` and `context` when `[natives]` is present. The diagnostic names the type that needs the key and the key, as `CheckResources` does. `CSharpConfig` and `RegistrationConfig` hold these as nullable, and the writers read them through accessors that throw the diagnostic, so no writer checks for null.

FenrirMath's and FenrirAssets's "None of these is used" blocks go, and the fixture's `Uses/Common.binding.toml` loses the six names it never needed.

### A defaults file

A top-level `defaults` key names one file, `<Name>.binding-defaults.toml` (the suffix is checked, as `.binding.toml` is), relative to the configuration. The file holds SDK names in the configuration's own sections and nothing else. The keys it may state:

| Section | Keys |
|---|---|
| `[csharp]` | `component-attribute`, `component-interface`, `read-only-component-interface`, `event-attribute`, `event-interface`, `tagged-union-attribute`, `resource-attribute`, `resource-interface`, `read-only-resource-interface`, `pointer-type`, `usings` |
| `[cpp]` | `includes` |
| `[registration]` | `extension`, `make-component`, `make-read-only-component`, `make-event`, `make-shared-struct`, `make-scene-resource`, `make-read-only-scene-resource`, `make-app-resource`, `make-read-only-app-resource`, `includes` |
| `[tests]` | `includes`, `component` |
| `[natives]` | `context`, `attribute`, `make`, `zero-outputs` |
| `[bound-types]` | any entry |

Any other key (a path, a role list, a name of one binding's: `namespace`, `descriptions`, `uses`, `exclude`, `contract`, `generated`, `mappings`, `source`, `function`, `class`, `sources`, `types`, `names`, `nouns`, `readonly-*`, `private-fields`, `fixed-buffers`, `internal-structs`, `read-only`, `by-hand`, `setup`, `tags`, the test paths, and `defaults` itself) is refused with a diagnostic naming it. A scalar the binding states replaces the default; a list the binding states is appended after the default's entries (so the SDK's includes come first, as they do today). A binding restating a default, scalar, list entry or bound type, byte for byte, is refused: "already the default from `<file>`; delete it". A different value is an override.

The engine's file is `packages/FenrirScripting/api/FenrirScripting.binding-defaults.toml`, owned by FenrirScripting, so Iris.Bindings still knows no engine name (ADR-0042). It holds the six component, event and union names, the three resource names, `usings = ["Fenrir.Scripting"]`, the three registration includes, `extension`, the four `make-*` and `make-scene-resource`, `[tests] includes = ["ScriptingProbe.hpp"]` and `component`, `[natives] context`, `attribute`, `make`, `zero-outputs`, and `[bound-types] "Fenrir::EntityValue"` with its mapping header (below). Seven bindings name it; FenrirMath's does not: after the first ticket it needs no SDK key, `Fenrir.Math` does not reference `Fenrir.Scripting` (its mirrors cannot carry `using Fenrir.Scripting;`), and its glm bound types are its own. The fixture gets `tests/Fixture/Host.binding-defaults.toml`, named by `Fixture.binding.toml`.

describe-api follows `defaults` for `context` when the binding does not state it: one more TOML read in `NativesSelection.Read`, which reads nothing else of the file. The split is then: describe-api reads `sources` and `context` (from the binding or its defaults); Iris.Bindings reads the rest and alone enforces what a defaults file may hold.

### Bound types through `uses`

A binding's bound types are its own `[bound-types]`, every entry of the bindings it uses (transitively, as mirrors are), and its defaults file's. The checks on own entries stay as they are (an own entry must be a described bound type); an inherited entry is used only when a description here holds that type. Two inherited sources giving one name differently is an error naming both; identically is fine. An own entry for an inherited name is an override, unless identical, when it is refused as above.

A bound-type entry may carry `include = "<header>"`, its mapping's header, written into this binding's mappings header only when this binding binds that type, after the binding's own `[cpp] includes` and before the used bindings' mappings headers. `Fenrir::EntityValue`'s entry in the defaults file carries `FenrirScripting/EntityValueMapping.hpp`, and the three hand-written include lines go. Math's bound types need no `include`: the used mappings header already brings `FenrirScripting/BoundMappings.hpp`.

A `using` of the owner's namespace is added where an inherited bound type is held or taken, exactly as `UsedNamespaces` does for a used mirror today (mirrors, natives), so `usings = ["Fenrir.Math"]` leaves every file. A bound type from the defaults file has no owner namespace; the defaults' `usings` covers it.

### What a binding states afterwards

Its namespace, `descriptions` and `uses`, `defaults`, `exclude`, the contract and generated paths, the per-binding `[csharp]` lists (`readonly-*`, `private-fields`, `fixed-buffers`, `internal-structs`), `[cpp] mappings` and the package's own includes, the registration source, function and package includes, `read-only` and `by-hand`, the test paths, class, setup and tags, `[natives] sources`, `class` and `types`, its own bound types (Physics's `Transform` and `Entity` with their defaults, Assets's handles), `[names]` and `[nouns]`.

### Out of scope

- Physics binding `Fenrir::Transform` and `Fenrir::Entity` as FenrirScripting's mirrors through `uses` rather than as bound types: that needs a selection change and a `CheckUses` relaxation, and is why Physics keeps two bound types nothing else has.
- `[tests] setup` and `tags`, which genuinely vary per binding.
- A new ADR: this changes a tool's configuration, not a boundary.
- Candidate 6 (span-taking generated declarations), grilled and specified after this stack.
- `packages/FenrirScripting/src`, which a sibling session is changing; nothing here touches it.

## Decisions (from the grilling)

1. SDK names come from a `defaults` key naming one FenrirScripting-owned file, not through `uses`; bound types come through `uses` and the defaults file.
2. The defaults file may hold only SDK-naming keys (the table above); a binding's scalar replaces, a binding's list appends.
3. Bound types are inherited transitively through `uses` and from the defaults file; an own entry overrides; two inherited sources disagreeing is an error.
4. A bound-type entry may carry `include`, written only where the type is bound; `Fenrir::EntityValue` moves into the defaults file with its header.
5. Every SDK key is required only when something needs it, the resource precedent applied uniformly.
6. describe-api follows `defaults` for `context`; the `[natives]` split is otherwise unchanged.
7. Three stacked PRs, each changing the tool and adopting the change across the eight bindings and the fixture, each a regeneration no-op.
8. Out of scope: Physics's two bound types, `setup` and `tags`, a new ADR, candidate 6.
9. FenrirMath's binding does not name the defaults file.
10. Files are `FenrirScripting.binding-defaults.toml` and the fixture's `Host.binding-defaults.toml`; the suffix is checked.
11. Restating any default is refused, not only a bound type.
12. The spec and tickets go in their own PR first; ticket 01's branch is cut from it.

## Tickets

| Ticket | Scope | Blocked by |
|---|---|---|
| 01 | Iris.Bindings: SDK keys required only when needed; FenrirMath, FenrirAssets and the fixture's Common binding lose their unused keys | none |
| 02 | Iris.Bindings and describe-api: the `defaults` key and file; FenrirScripting's and the fixture's defaults files; seven bindings trimmed | 01 |
| 03 | Iris.Bindings: bound types and usings through `uses` and the defaults file, the `include` field; bindings trimmed | 02 |

All three touch `packages/Iris/Tools/Iris.Bindings/` and the `*.binding.toml` files, so they are a stack in that order, each PR cut from the previous ticket's branch and targeting it until it merges.

## Done looks like

- Every package binding states its namespace, descriptions, uses, defaults, paths, natives sources and class, its own types and names, and nothing else; no `*.binding.toml` under `packages/` states `component-attribute`, `make-component`, `context`, `attribute`, `make`, `zero-outputs`, `usings = ["Fenrir.Scripting"]` or a glm or `EntityValue` bound type.
- `grep -c "Fenrir::ZeroingOutputs" packages/*/Scripting/api/*.binding.toml packages/FenrirScripting/api/*.binding.toml` finds it in none; it is in the defaults file once.
- `fenrir-generate-bindings` is a no-op after each PR. The generated code is the same code: the one-time changes are the generated `using`s sorted (the writer canonicalises them, so their order stops depending on which file lists a namespace), the defaults test's includes with the SDK's first, and Assets's registration gaining the SDK's component-registry include the defaults list for every registration.
- `dotnet test` for Iris.Bindings and describe-api pass with the new cases; the README documents `defaults`, the defaults file, inherited bound types, `include` and which keys each tool reads.
- The msvc-x64-debug preset builds and its scripting tests pass locally; the other presets are CI's.
