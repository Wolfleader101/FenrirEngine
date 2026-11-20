# 07: Every package's registration committed and compiled in; `App` owns the registry; each `Install` registers its package

**What to build:** a running engine knows every described type by name. Each package with an API description gains a `[registration]` table, its generated header and source are committed and compiled into the package, which links `FenrirTypes`; `App`'s constructor makes one `TypeRegistry` as an app resource and registers the packages beneath `Install` (FenrirMath, FenrirECS, FenrirScene, FenrirApp); every `X::Install(App&, XConfig)` registers its package's types first (ADR-0074 "fills a run-time type registry in a new package at `Install`"). A headless `App` with assets, physics and animation installed can be asked for `Fenrir::RigidBody`, `Fenrir::Transform` or `Fenrir::Sky` and get their fields.

- **Selections:** every `packages/*/api/*.api.toml` with a description gains `[registration]` naming its header, source, function and includes (the types spec's names: `include/<Package>/Generated/<Package>Types.hpp`, `src/Generated/<Package>Types.cpp`, `Fenrir::Generated::Register<Package>Types`); FenrirScene, FenrirCore, FenrirTime, FenrirScheduler, FenrirLogger and FenrirDebugDraw have no description and register nothing (FenrirScene's resources are reached through FenrirRenderer's and others' descriptions, as today).
- **Build:** each described package calls `fenrir_generate_type_registry` beside its `fenrir_describe_api`, compiles its generated source and links `FenrirTypes` `PUBLIC`; the generated header is public so `Describe<T>` is visible to a game (ADR-0084's `Project::Get<T>`). The generated files are committed as the bindings are.
- **`App`:** constructs a `TypeRegistry` app resource first and calls the four Install-less registrations; `App::GetResource<TypeRegistry>()` is how everything reaches it. FenrirECS's and FenrirScene's registrations run before any scene exists.
- **Each `Install`** (`Assets`, `Physics`, `Animation`, `Skeletons`, `Renderer`, `Window`, `Input`, `DebugDrawing`, `Scripting`) calls its package's registration as its first act, before any resource or system, and an `Install` called twice registers once (the registry's duplicate rule makes the second call a no-op with no diagnostic when the description is the same object).
- **Per-package defaults test:** each package's test suite gains a generated-shape test that the registration's defaults equal `T{}` field by field through the visitor, the shape of the binding's `<Package>DefaultsTests.cpp`; the registry generator emits it as a test source the package's `Tests/CMakeLists.txt` includes, or the test is one template the FenrirTypes tests export and each package instantiates, whichever the implementer finds smaller.
- **Docs:** `README.md`'s API-description paragraph names the registration beside the description and binding; each package's `CMakeLists.txt` header comment says it registers types.

**Blocked by:** 06, 11.

**Touches:** every described package's `CMakeLists.txt`, `api/*.api.toml`, new `include/<Package>/Generated/<Package>Types.hpp` and `src/Generated/<Package>Types.cpp`, and its `Install` source and `Tests/`; `packages/FenrirApp/` (`App.hpp`, `App.cpp`, tests); `README.md`

**Status:** ready-for-agent

- [ ] Every described package compiles its committed generated registration on msvc-x64-debug; `fenrir-generate-type-registry` regenerates them byte-identical.
- [ ] A headless `App` holds a `TypeRegistry` that knows `Fenrir::Transform`, `Fenrir::Name`, `Fenrir::Relationship` and FenrirMath's bound types before any `Install`.
- [ ] After `Assets::Install`, `Physics::Install` and `Animation::Install` on the headless `App`, the registry knows `Fenrir::RigidBody`, `Fenrir::Joint` (with `bodyA` as `entity`), `Fenrir::AnimationLayer` and FenrirRenderer's `Fenrir::Sky` when the renderer is built; iterating the `component` role lists every described component.
- [ ] Each package's defaults test passes: a default-constructed instance visited through the registration equals the description's defaults field by field.
- [ ] Installing a package twice on one `App` registers its types once, with no diagnostic.
- [ ] The scripting tests still pass: Iris.Bindings and admission are unchanged by this ticket.
