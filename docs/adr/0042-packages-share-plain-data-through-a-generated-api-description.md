---
status: accepted
---

# Packages share plain data through a generated, language-neutral API description

A physics component was described in about six places: the C++ struct (names, types, defaults, docs), its `IRIS_FIELD` mapping (names again, with their C# spellings), a stand-in struct whenever the real type had private members or an `Entity`, the C# struct (names, types, defaults, docs, explicit offsets for a tagged union), a defaults test, and the tests' own `Same()` copies. Twenty stand-ins existed only to restate layouts Iris could not read. Each place could drift from the others, and only Iris's load-time check stood between a drift and corrupted memory.

We decided that **a package's C++ headers are the one hand-written description of the plain-data types it shares**, and that everything else is generated from them in two stages:

1. An engine tool (`tools/describe-api`) reads the headers a package selects and writes the package's **API description**: a committed, language-neutral file (`packages/<Package>/api/<Package>.api.json`) recording each shared type's fields with their offsets, kinds, default values and doc comments, its enums, and its tagged unions. A small selection file beside it names the headers and types and their roles (component, event, shared struct, bound type). The description records the one target it was read for (64-bit, 8-byte pointers, the natural alignment rules MSVC, Clang and GCC share for standard-layout data).
2. Each **binding** generates its side from the description, never from the headers. C# scripting's generator (an Iris tool) emits the C# mirrors, the Iris mappings by kind and offset, the extension's registration lists, and a test that a C# `new` equals the C++ `T{}` field by field. Its own configuration (namespace, which C# type stands for each bound type, the few field renames) lives with the binding.

The engine stays binding-agnostic: nothing in FenrirPhysics, FenrirECS or FenrirApp names Iris, C# or the tool, at compile time or at build time; the tool and its output are an artefact of the package, as `extension_api.json` is Godot's. A binding for another language reads the same committed description and needs neither libclang nor Iris. The description is not written by hand and the C++ is never generated: the header already holds names, types, defaults, docs and layout, so a hand-written definition file would restate it, and generated C++ would take the engine's components out of its authors' hands.

## Considered options

- **A field list beside each C++ struct and a describe program that instantiates `T{}`.** Exact defaults with no header parsing, but every member name is spelled twice and a running program cannot see doc comments, so the C# side would lose the field docs game developers read.
- **A hand-written, language-neutral definition file generating both the C++ and the C# struct** (s&box's `.def` files taken further than s&box takes them: it describes functions this way and leaves structs hand-written on both sides). One place for everything, but the engine's components become generated code, the generator must emit the tagged-union class API and every hand-written method, and the engine's C++ authors stop editing their own structs.
- **Keep the hand-written mirrors and add a whole-contract hash** (s&box compares a hash of the definition text and every struct's size at start-up). Safer, but no less duplicated; and Iris already checks size, names, offsets and kinds per field at load, which is stricter than s&box, Coral or Mono. The problem was duplication, not safety, so the per-field check stays as the gate and nothing is added to it.

## Consequences

- Adding a field to a shared component is a change to one header, a regeneration, and a review of the generated diff. The ordinary build never runs the tools: CI regenerates and fails on a dirty tree.
- Hand-written C# beside a generated mirror adds behaviour only (constructors, views, equality); a hand-written field in a mirrored struct is an error.
- A C++ type the tool cannot describe (private members that are not the tagged-union shape of a tag followed by one anonymous union, a non-plain type) fails the tool with a diagnostic naming it, instead of getting a stand-in.
- Native functions are not in the description: they are glue specific to one binding. C# scripting generates its declarations and registration from the C++ natives themselves, so a native is spelled once there too, but another binding writes its own glue against the package's public C++ API.
- A second binding, or Iris moving to its own repository, lifts nothing out of the engine: the committed description is already the boundary.
