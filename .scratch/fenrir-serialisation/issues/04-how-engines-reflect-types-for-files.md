# How do C++ engines describe their types for serialisation and editors?

Type: research
Status: open
Blocked by: none

## Question

How do C++ engines know a type's fields in order to read and write it from a file and show it in an inspector, in the areas that constrain how Fenrir widens its API description (ADR-0042, `tools/describe-api`): Unreal's UnrealHeaderTool and `UPROPERTY`, Godot's `ClassDB` and `GDCLASS` with `_bind_methods`, O3DE's `AZ::SerializeContext`, `EditContext` and `BehaviorContext`, Flax's code generator, Bevy's `#[derive(Reflect)]` and type registry, Hazel and other codegen-based engines, and libraries such as RTTR, refl-cpp, Boost.Describe and `magic_enum`, plus the C++26 reflection proposals' state.

- **Mechanism:** header parsing and code generation (libclang or a custom parser), macros registering at static-init time, templates and intrusive declarations, or a runtime registry filled by hand; what each costs the game developer per type and per field.
- **Coverage:** strings, dynamic arrays and maps, nested structs, optional values, enums and flags, pointers and references to other objects (typed, polymorphic), fixed-layout versus owning types; how plain-data components and richer "asset" types share or split the mechanism.
- **Polymorphism:** class hierarchies, interfaces or traits, type ids and how a base-typed field is read when the file holds a derived type.
- **Metadata:** per-field attributes editors need (ranges, display names, categories, read-only, tooltips from doc comments), and default values.
- **Versioning hooks:** per-type version numbers, field renames, custom upgrade functions, and how unknown fields are kept.
- **Scripts:** how script-defined types (C#, GDScript, Lua, Blueprint) are described through the same registry so files name them neutrally, and how a type's description survives script reload.

Write findings to a Markdown file on the `research/engine-reflection` branch (`.scratch/fenrir-serialisation/research/engine-reflection.md`), citing each claim's source and pinning the versions read, and link it here.
