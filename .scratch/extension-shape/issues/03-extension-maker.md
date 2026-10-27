# 03: The generated extension maker

**What to build:** Iris.Bindings generates the function that makes a package's `ScriptingExtension`, so each package's public function is one line and the binding's name and contract file name are spelled nowhere by hand (spec: "A generated extension maker").

- **`BindingConfig.cs`**: `[registration]` gains the optional `extension` key, a qualified function name. It requires `[natives]` (the state type comes from the natives' extension state) and a natives description whose natives all take one state type; a binding with none fails naming the key.
- **`CppWriter.cs`**: with `extension` set, the registration `.cpp` defines `Fenrir::ScriptingExtension <extension>(std::shared_ptr<State> state)` that sets `name` to the configuration file's stem (`FenrirInput`), calls `SetState(std::move(state))`, sets `sharedAssemblies = {"<namespace>.dll"}`, fills the lists and functions the register function fills today, and returns it; the register function becomes a function in an anonymous namespace that the maker calls. The natives header (ticket 01) declares the maker instead of the register function, and includes `<memory>`.
- **Six configurations** set `extension = "Fenrir::Generated::Make<Pkg>Extension"`; `FenrirScripting.binding.toml` does not.
- **Regenerate** with `fenrir-generate-bindings`.
- **Six public functions**: `Assets`, `DebugDrawing`, `Input`, `Renderer`, `Window` `::ScriptingExtension()` return `Generated::Make<Pkg>Extension(std::make_shared<<Pkg>Scripting>())`; Input's keeps the state in a local and appends `InputClaimsResource(input)` to `resources` after the call. `PhysicsScriptingExtension()` becomes `Physics::ScriptingExtension()` in `PhysicsScripting.hpp` and `.cpp`, with its callers (`examples/PhysicsDemo`, `packages/FenrirPhysics/Scripting/Tests/PhysicsScriptingApp.hpp`, and whatever `grep -rn PhysicsScriptingExtension` finds).
- **README**: the configuration listing shows `[registration] extension`; "What it writes" says what the maker sets.
- If the rewrite shows the maker deleting only the two literals and three lines per package, say so in the PR and the user decides whether to merge or close (spec decision 1).

**Blocked by:** 01

**Touches:** `packages/Iris/Tools/Iris.Bindings/src/`, `packages/Iris/Tools/Iris.Bindings/Tests/`, `packages/Iris/Tools/Iris.Bindings/README.md`, `packages/*/Scripting/api/*.binding.toml`, `packages/*/Scripting/src/`, `packages/FenrirPhysics/Scripting/include/`, `packages/FenrirPhysics/Scripting/Tests/`, `examples/PhysicsDemo/`

**Status:** claimed

- [x] `BindingTests.cs`: a case with `extension` set finds the maker's definition setting the name, state and assembly and finds its declaration in the natives header; a case without it finds today's register function; a case fails a configuration with `extension` and no `[natives]`.
- [x] `grep -rn "sharedAssemblies\|extension.name" packages/*/Scripting/src/*.cpp` finds nothing hand-written; `grep -rn PhysicsScriptingExtension` finds nothing.
- [x] Every scripting test passes under the msvc-x64-debug preset; `fenrir-generate-bindings` is a no-op after the commit; `check_tickets.py` and clang-format pass.

## Comments

Filed from the extension-shape grilling (spec decisions 1, 4, 8, 9). Cut from `extension-shape-01-natives-header`, beside 02, with which it shares no file.
